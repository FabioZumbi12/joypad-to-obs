/*
Joypad to OBS
Copyright (C) 2026 FabioZumbi12 <admin@areaz12server.net.br>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "joypad-input.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include <xinput.h>
#include <tchar.h>
#elif defined(__linux__)
#include <dirent.h>
#include <fcntl.h>
#include <linux/joystick.h>
#include <sys/ioctl.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#endif

JoypadInputManager::JoypadInputManager() = default;

JoypadInputManager::~JoypadInputManager()
{
	Stop();
}

void JoypadInputManager::Start()
{
	if (running_.exchange(true)) {
		return;
	}

	RefreshDevices();

#if defined(__APPLE__)
	poll_thread_ = std::thread([this]() { PollLoop(); });
#else
	poll_thread_ = std::thread([this]() { PollLoop(); });
#endif
}

void JoypadInputManager::Stop()
{
	if (!running_.exchange(false)) {
		return;
	}

#if defined(__APPLE__)
	if (hid_run_loop_) {
		CFRunLoopStop((CFRunLoopRef)hid_run_loop_);
	}
#endif

	if (poll_thread_.joinable()) {
		poll_thread_.join();
	}

#if defined(__APPLE__)
	if (hid_manager_) {
		IOHIDManagerClose((IOHIDManagerRef)hid_manager_,
				  kIOHIDOptionsTypeNone);
		CFRelease((IOHIDManagerRef)hid_manager_);
		hid_manager_ = nullptr;
		hid_run_loop_ = nullptr;
	}
#elif defined(__linux__)
	{
		std::lock_guard<std::mutex> lock(devices_mutex_);
		for (auto &state : device_states_) {
			if (state.fd >= 0) {
				close(state.fd);
				state.fd = -1;
			}
		}
	}
#endif
}

std::vector<JoypadDeviceInfo> JoypadInputManager::GetDevices() const
{
	std::lock_guard<std::mutex> lock(devices_mutex_);
	return devices_;
}

void JoypadInputManager::RefreshDevices()
{
	std::lock_guard<std::mutex> lock(devices_mutex_);
	std::vector<DeviceState> next_states;
	std::vector<JoypadDeviceInfo> next_devices;

	[[maybe_unused]] auto find_existing = [&](const std::string &id) -> DeviceState * {
		for (auto &state : device_states_) {
			if (state.id == id) {
				return &state;
			}
		}
		return nullptr;
	};

#ifdef _WIN32
	auto to_utf8 = [](const TCHAR *value) -> std::string {
#ifdef UNICODE
		if (!value || !*value) {
			return std::string();
		}
		int len = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr,
					      0, nullptr, nullptr);
		if (len <= 0) {
			return std::string();
		}
		std::vector<char> buf(len);
		WideCharToMultiByte(CP_UTF8, 0, value, -1, buf.data(), len,
				    nullptr, nullptr);
		return std::string(buf.data());
#else
		return value ? std::string(value) : std::string();
#endif
	};

	auto query_registry_tstring = [](HKEY root, const TCHAR *subkey,
					 const TCHAR *value)
		-> std::basic_string<TCHAR> {
		HKEY key = nullptr;
		if (RegOpenKeyEx(root, subkey, 0, KEY_READ, &key) !=
		    ERROR_SUCCESS) {
			return std::basic_string<TCHAR>();
		}

		DWORD type = 0;
		DWORD size = 0;
		if (RegQueryValueEx(key, value, nullptr, &type, nullptr,
				    &size) != ERROR_SUCCESS ||
		    (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0) {
			RegCloseKey(key);
			return std::basic_string<TCHAR>();
		}

		std::vector<TCHAR> buffer(size / sizeof(TCHAR) + 1, 0);
		DWORD buf_size = (DWORD)(buffer.size() * sizeof(TCHAR));
		if (RegQueryValueEx(key, value, nullptr, &type,
				    (LPBYTE)buffer.data(),
				    &buf_size) != ERROR_SUCCESS) {
			RegCloseKey(key);
			return std::basic_string<TCHAR>();
		}
		buffer.back() = 0;
		RegCloseKey(key);
		return std::basic_string<TCHAR>(buffer.data());
	};

	auto get_joy_friendly_name = [&](const JOYCAPS &caps,
					 UINT id) -> std::string {
		TCHAR settings_path[MAX_PATH];
		_stprintf_s(
			settings_path,
			TEXT("System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\Joystick\\JoystickSettings"));

		TCHAR value_name[64];
		_stprintf_s(value_name, TEXT("Joystick%uOEMName"), id + 1);

		auto key_name = query_registry_tstring(
			HKEY_CURRENT_USER, settings_path, value_name);
		if (key_name.empty()) {
			key_name = query_registry_tstring(
				HKEY_LOCAL_MACHINE, settings_path, value_name);
		}

		if (key_name.empty() && caps.szRegKey[0]) {
			key_name = caps.szRegKey;
		}

		if (key_name.empty()) {
			return std::string();
		}

		std::basic_string<TCHAR> oem_path =
			TEXT("System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\Joystick\\OEM\\") +
			key_name;

		auto name = query_registry_tstring(
			HKEY_CURRENT_USER, oem_path.c_str(), TEXT("OEMName"));
		if (name.empty()) {
			name = query_registry_tstring(
				HKEY_LOCAL_MACHINE, oem_path.c_str(), TEXT("OEMName"));
		}
		return to_utf8(name.c_str());
	};

	auto xinput_name_for_index = [&](DWORD index) -> std::string {
		switch (index) {
		case 0:
			return "Xbox Controller 1";
		case 1:
			return "Xbox Controller 2";
		case 2:
			return "Xbox Controller 3";
		case 3:
			return "Xbox Controller 4";
		default:
			return "Xbox Controller";
		}
	};

	bool xinput_present = false;
	for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
		XINPUT_STATE state = {};
		if (XInputGetState(i, &state) != ERROR_SUCCESS) {
			continue;
		}
		xinput_present = true;
		std::string id = "xinput:" + std::to_string(i);
		DeviceState dev;
		DeviceState *existing = find_existing(id);
		if (existing) {
			dev = *existing;
		} else {
			dev.is_xinput = true;
			dev.xinput_id = (uint32_t)i;
			dev.id = id;
			dev.name = xinput_name_for_index(i);
			dev.connected = true;
			for (size_t k = 0; k < 8; ++k) {
				dev.axis_initialized[k] = false;
			}
		}

		JoypadDeviceInfo info;
		info.id = dev.id;
		info.name = dev.name;
		next_devices.push_back(info);
		next_states.push_back(dev);
	}

	auto is_xinput_like = [](const std::string &name) -> bool {
		std::string lower = name;
		std::transform(lower.begin(), lower.end(), lower.begin(),
			       [](unsigned char c) {
				       return (char)std::tolower(c);
			       });
		return lower.find("xinput") != std::string::npos ||
		       lower.find("xbox") != std::string::npos;
	};

	UINT count = joyGetNumDevs();
	for (UINT id = 0; id < count; ++id) {
		JOYCAPS caps = {};
		if (joyGetDevCaps(id, &caps, sizeof(caps)) != JOYERR_NOERROR) {
			continue;
		}
		caps.szPname[(sizeof(caps.szPname) / sizeof(TCHAR)) - 1] = 0;
		caps.szRegKey[(sizeof(caps.szRegKey) / sizeof(TCHAR)) - 1] = 0;
		std::string dev_id = "winmm:" + std::to_string(id);
		DeviceState state;
		DeviceState *existing = find_existing(dev_id);
		if (existing) {
			state = *existing;
		} else {
			state.winmm_id = (int)id;
			state.id = dev_id;
			state.name = to_utf8(caps.szPname);
			state.connected = true;
			for (size_t k = 0; k < 8; ++k) {
				state.axis_initialized[k] = false;
			}
			std::string friendly = get_joy_friendly_name(caps, id);
				if (!friendly.empty()) {
					state.name = friendly;
				}
		}
		state.axis_min[0] = caps.wXmin;
		state.axis_max[0] = caps.wXmax;
		state.axis_min[1] = caps.wYmin;
		state.axis_max[1] = caps.wYmax;
		state.axis_min[2] = caps.wZmin;
		state.axis_max[2] = caps.wZmax;
		state.axis_min[3] = caps.wRmin;
		state.axis_max[3] = caps.wRmax;
		state.axis_min[4] = caps.wUmin;
		state.axis_max[4] = caps.wUmax;
		state.axis_min[5] = caps.wVmin;
		state.axis_max[5] = caps.wVmax;

		if (xinput_present && is_xinput_like(state.name)) {
			continue;
		}

		JoypadDeviceInfo info;
		info.id = state.id;
		info.name = state.name;
		next_devices.push_back(info);
		next_states.push_back(state);
	}
#elif defined(__linux__)
	DIR *dir = opendir("/dev/input");
	if (!dir) {
		return;
	}
	struct dirent *ent = nullptr;
	while ((ent = readdir(dir)) != nullptr) {
		if (strncmp(ent->d_name, "js", 2) != 0) {
			continue;
		}
		std::string path = std::string("/dev/input/") + ent->d_name;
		std::string id = "js:" + std::string(ent->d_name);

		DeviceState state;
		DeviceState *existing = find_existing(id);
		if (existing) {
			state = *existing;
		} else {
			int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
			if (fd < 0) {
				continue;
			}
			char name[128] = {};
			if (ioctl(fd, JSIOCGNAME(sizeof(name)), name) < 0) {
				snprintf(name, sizeof(name), "Joystick %s",
					 ent->d_name);
			}
			state.fd = fd;
			state.id = id;
			state.name = name;
			state.connected = true;
			for (size_t k = 0; k < 8; ++k) {
				state.axis_initialized[k] = false;
			}
		}

		JoypadDeviceInfo info;
		info.id = state.id;
		info.name = state.name;
		next_devices.push_back(info);
		next_states.push_back(state);
	}
	closedir(dir);

	for (auto &old_state : device_states_) {
		bool kept = false;
		for (const auto &new_state : next_states) {
			if (new_state.id == old_state.id) {
				kept = true;
				break;
			}
		}
		if (!kept && old_state.fd >= 0) {
			close(old_state.fd);
		}
	}
#elif defined(__APPLE__)
	// Device list is managed by IOHID callbacks. Leave list empty until
	// we receive device attach events.
	next_devices = devices_;
	next_states = device_states_;
#endif

	devices_ = std::move(next_devices);
	device_states_ = std::move(next_states);
}

void JoypadInputManager::SetOnButtonPressed(
	std::function<void(const JoypadEvent &)> handler)
{
	std::lock_guard<std::mutex> lock(handler_mutex_);
	on_button_pressed_ = std::move(handler);
}

void JoypadInputManager::SetOnAxisChanged(
	std::function<void(const JoypadEvent &)> handler)
{
	std::lock_guard<std::mutex> lock(handler_mutex_);
	axis_handlers_.clear();
	if (handler) {
		AxisHandlerEntry entry;
		entry.id = 0;
		entry.handler = std::move(handler);
		axis_handlers_.push_back(std::move(entry));
	}
}

int JoypadInputManager::AddOnAxisChanged(
	std::function<void(const JoypadEvent &)> handler)
{
	if (!handler) {
		return 0;
	}
	std::lock_guard<std::mutex> lock(handler_mutex_);
	AxisHandlerEntry entry;
	entry.id = next_axis_handler_id_++;
	entry.handler = std::move(handler);
	axis_handlers_.push_back(std::move(entry));
	return axis_handlers_.back().id;
}

void JoypadInputManager::RemoveOnAxisChanged(int handler_id)
{
	if (handler_id <= 0) {
		return;
	}
	std::lock_guard<std::mutex> lock(handler_mutex_);
	auto it = std::remove_if(axis_handlers_.begin(), axis_handlers_.end(),
				 [handler_id](const AxisHandlerEntry &entry) {
					 return entry.id == handler_id;
				 });
	axis_handlers_.erase(it, axis_handlers_.end());
}

bool JoypadInputManager::GetAxisRawValue(const std::string &device_id,
					 int axis_index,
					 double &raw_out) const
{
	if (axis_index < 0 || axis_index >= 8) {
		return false;
	}
	std::lock_guard<std::mutex> lock(devices_mutex_);
	for (const auto &state : device_states_) {
		if (!device_id.empty() && state.id != device_id) {
			continue;
		}
		if (!state.axis_initialized[axis_index]) {
			return false;
		}
		raw_out = state.last_axes[axis_index];
		return true;
	}
	return false;
}

bool JoypadInputManager::BeginLearn(
	std::function<void(const JoypadEvent &)> handler)
{
	std::lock_guard<std::mutex> lock(handler_mutex_);
	if (learn_handler_) {
		return false;
	}
	learn_handler_ = std::move(handler);
	return true;
}

void JoypadInputManager::CancelLearn()
{
	std::lock_guard<std::mutex> lock(handler_mutex_);
	learn_handler_ = nullptr;
}

void JoypadInputManager::PollLoop()
{
	auto last_refresh = std::chrono::steady_clock::now();
	[[maybe_unused]] const double default_threshold = 0.1;
	[[maybe_unused]] const int default_interval_ms = 0;
	while (running_.load()) {
#ifdef _WIN32
		{
			std::lock_guard<std::mutex> lock(devices_mutex_);
			for (auto &state : device_states_) {
				if (state.is_xinput) {
					XINPUT_STATE xi_state = {};
					if (XInputGetState((DWORD)state.xinput_id,
							   &xi_state) !=
					    ERROR_SUCCESS) {
						state.connected = false;
						continue;
					}
					state.connected = true;
					uint32_t buttons =
						(uint32_t)xi_state.Gamepad.wButtons;
					uint32_t changed =
						buttons & ~state.last_buttons;
					if (changed) {
						for (int bit = 0; bit < 16;
						     ++bit) {
							if (changed &
							    (1u << bit)) {
								JoypadEvent event;
								event.device_id =
									state.id;
								event.device_name =
									state.name;
								event.button =
									bit + 1;
								DispatchEvent(
									event);
							}
						}
					}
					state.last_buttons = buttons;

					auto push_axis =
						[this, &state, default_interval_ms](
							int axis_index,
							double value) {
							std::string key =
								state.id + ":" +
								std::to_string(
									axis_index) +
								(value >= 0.0 ? "+" :
										     "-");
							auto now =
								std::chrono::steady_clock::now();
							auto it =
								axis_last_trigger_.find(
									key);
							double prev_raw =
								state.last_axes
									[axis_index];
							double raw = value;
							if (raw == prev_raw) {
								return;
							}
							if (it !=
								    axis_last_trigger_
									    .end() &&
							    std::chrono::duration_cast<
								    std::chrono::milliseconds>(
								    now -
								    it->second)
								    .count() <
							    default_interval_ms) {
								state.last_axes
									[axis_index] =
										raw;
								return;
							}
							axis_last_trigger_[key] = now;
							JoypadEvent event;
							event.device_id = state.id;
							event.device_name =
								state.name;
							event.is_axis = true;
							event.axis_index =
								axis_index;
							event.axis_value = value;
							DispatchEvent(event);
							state.last_axes[axis_index] =
								raw;
						};

					auto norm_thumb = [](SHORT v) {
						double out = 0.0;
						if (v >= 0) {
							out = v / 32767.0;
						} else {
							out = v / 32768.0;
						}
						return std::clamp(out, -1.0,
								  1.0);
					};
					auto norm_trigger = [](BYTE v) {
						return std::clamp(
							v / 255.0, 0.0, 1.0);
					};

					auto emit_axis = [&](int idx, double norm,
							     double raw) {
						JoypadEvent event;
						event.device_id = state.id;
						event.device_name = state.name;
						event.is_axis = true;
						event.axis_index = idx;
						event.axis_value = norm;
						event.axis_raw_value = raw;
						DispatchAxisAbsolute(event);
					};

					auto push_axis_raw =
						[this, &state, default_interval_ms,
						 emit_axis](int axis_index,
							    double norm,
							    double raw) {
							std::string key =
								state.id + ":" +
								std::to_string(
									axis_index) +
								(raw >= 0.0 ? "+" :
										  "-");
							auto now =
								std::chrono::steady_clock::now();
							auto it =
								axis_last_trigger_.find(
									key);
							if (!state.axis_initialized[axis_index]) {
								state.last_axes[axis_index] =
									raw;
								state.axis_initialized[axis_index] =
									true;
								return;
							}
							double prev_raw =
								state.last_axes
									[axis_index];
							if (raw == prev_raw) {
								return;
							}
							if (it !=
								    axis_last_trigger_
									    .end() &&
							    std::chrono::duration_cast<
								    std::chrono::milliseconds>(
								    now -
								    it->second)
								    .count() <
							    default_interval_ms) {
								state.last_axes
									[axis_index] =
										raw;
								return;
							}
							axis_last_trigger_[key] = now;
							emit_axis(axis_index, norm, raw);
							state.last_axes[axis_index] =
								raw;
						};

					push_axis_raw(0,
						      norm_thumb(xi_state.Gamepad.sThumbLX),
						      (double)xi_state.Gamepad.sThumbLX);
					push_axis_raw(1,
						      norm_thumb(xi_state.Gamepad.sThumbLY),
						      (double)xi_state.Gamepad.sThumbLY);
					push_axis_raw(2,
						      norm_thumb(xi_state.Gamepad.sThumbRX),
						      (double)xi_state.Gamepad.sThumbRX);
					push_axis_raw(3,
						      norm_thumb(xi_state.Gamepad.sThumbRY),
						      (double)xi_state.Gamepad.sThumbRY);
					push_axis_raw(4,
						      norm_trigger(xi_state.Gamepad.bLeftTrigger),
						      (double)xi_state.Gamepad.bLeftTrigger);
					push_axis_raw(5,
						      norm_trigger(xi_state.Gamepad.bRightTrigger),
						      (double)xi_state.Gamepad.bRightTrigger);
					continue;
				}
				JOYINFOEX info = {};
				info.dwSize = sizeof(info);
				info.dwFlags = JOY_RETURNBUTTONS | JOY_RETURNX |
					       JOY_RETURNY | JOY_RETURNZ |
					       JOY_RETURNR | JOY_RETURNU |
					       JOY_RETURNV;
				MMRESULT res =
					joyGetPosEx((UINT)state.winmm_id, &info);
				if (res != JOYERR_NOERROR) {
					state.connected = false;
					continue;
				}

				state.connected = true;
				uint32_t buttons = (uint32_t)info.dwButtons;
				uint32_t changed = buttons & ~state.last_buttons;
				if (changed) {
					for (int bit = 0; bit < 32; ++bit) {
						if (changed & (1u << bit)) {
							JoypadEvent event;
							event.device_id = state.id;
							event.device_name = state.name;
							event.button = bit + 1;
							DispatchEvent(event);
						}
					}
				}
				state.last_buttons = buttons;

				auto norm_axis = [&state](int idx, DWORD val) {
					int minv = state.axis_min[idx];
					int maxv = state.axis_max[idx];
					if (maxv <= minv) {
						return 0.0;
					}
					double out =
						((double)val - minv) /
							(double)(maxv - minv) *
							2.0 -
						1.0;
					return std::clamp(out, -1.0, 1.0);
				};

				double axes[6] = {
					norm_axis(0, info.dwXpos),
					norm_axis(1, info.dwYpos),
					norm_axis(2, info.dwZpos),
					norm_axis(3, info.dwRpos),
					norm_axis(4, info.dwUpos),
					norm_axis(5, info.dwVpos)};

				for (int i = 0; i < 6; ++i) {
					double value = axes[i];
					double raw = 0.0;
					switch (i) {
					case 0:
						raw = info.dwXpos;
						break;
					case 1:
						raw = info.dwYpos;
						break;
					case 2:
						raw = info.dwZpos;
						break;
					case 3:
						raw = info.dwRpos;
						break;
					case 4:
						raw = info.dwUpos;
						break;
					case 5:
						raw = info.dwVpos;
						break;
					}
					std::string key =
						state.id + ":" +
						std::to_string(i) +
						(raw >= 0.0 ? "+" : "-");
					auto now =
						std::chrono::steady_clock::now();
					auto it =
						axis_last_trigger_.find(key);
					if (!state.axis_initialized[i]) {
						state.last_axes[i] = raw;
						state.axis_initialized[i] = true;
						continue;
					}
					double prev_raw = state.last_axes[i];
					if (raw == prev_raw) {
						continue;
					}
					if (it != axis_last_trigger_.end() &&
					    std::chrono::duration_cast<
						    std::chrono::milliseconds>(
						    now - it->second)
							    .count() <
						    default_interval_ms) {
						state.last_axes[i] = raw;
						continue;
					}
					axis_last_trigger_[key] = now;
					JoypadEvent event;
					event.device_id = state.id;
					event.device_name = state.name;
					event.is_axis = true;
					event.axis_index = i;
					event.axis_value = value;
					event.axis_raw_value = raw;
					DispatchAxisAbsolute(event);
					state.last_axes[i] = raw;
				}
			}
		}
#endif
#if defined(__linux__)
		{
			std::lock_guard<std::mutex> lock(devices_mutex_);
			for (auto &state : device_states_) {
				if (state.fd < 0) {
					continue;
				}
				js_event e = {};
				while (read(state.fd, &e, sizeof(e)) ==
				       (ssize_t)sizeof(e)) {
					if (e.type & JS_EVENT_INIT) {
						continue;
					}
					if ((e.type & JS_EVENT_BUTTON) != 0 &&
					    e.value) {
						JoypadEvent event;
						event.device_id = state.id;
						event.device_name = state.name;
						event.button = (int)e.number + 1;
						DispatchEvent(event);
					}
					if ((e.type & JS_EVENT_AXIS) != 0) {
						double value = (double)e.value /
							       32767.0;
						double raw = (double)e.value;
						if (value < -1.0) {
							value = -1.0;
						}
						if (value > 1.0) {
							value = 1.0;
						}
						std::string key =
							state.id + ":" +
							std::to_string(e.number) +
							(value >= 0.0 ? "+" : "-");
						auto now =
							std::chrono::steady_clock::now();
						auto it =
							axis_last_trigger_.find(
								key);
						if (!state.axis_initialized[e.number]) {
							state.last_axes[e.number] = raw;
							state.axis_initialized[e.number] =
								true;
							continue;
						}
						double prev =
							state.last_axes[e.number];
						if (raw == prev) {
							continue;
						}
						if (it != axis_last_trigger_.end() &&
						    std::chrono::duration_cast<
							    std::chrono::milliseconds>(
							    now - it->second)
								    .count() <
							    default_interval_ms) {
							state.last_axes[e.number] =
								raw;
							continue;
						}
						axis_last_trigger_[key] = now;
						JoypadEvent event;
						event.device_id = state.id;
						event.device_name = state.name;
						event.is_axis = true;
						event.axis_index = e.number;
						event.axis_value = value;
						event.axis_raw_value = raw;
						DispatchAxisAbsolute(event);
						state.last_axes[e.number] = raw;
					}
				}
			}
		}
#elif defined(__APPLE__)
		// macOS uses a CFRunLoop in this thread.
		if (!hid_manager_) {
			hid_manager_ = IOHIDManagerCreate(
				kCFAllocatorDefault, kIOHIDOptionsTypeNone);
			if (hid_manager_) {
				IOHIDManagerSetDeviceMatching(
					(IOHIDManagerRef)hid_manager_,
					nullptr);
				IOHIDManagerRegisterDeviceMatchingCallback(
					(IOHIDManagerRef)hid_manager_,
					[](void *context, IOReturn, void *,
					   IOHIDDeviceRef device) {
						auto *self =
							static_cast<
								JoypadInputManager *>(
								context);
						if (!self || !device) {
							return;
						}
						CFStringRef name_ref =
							(CFStringRef)
								IOHIDDeviceGetProperty(
									device,
									CFSTR(
										kIOHIDProductKey));
						char name[256] = "Gamepad";
						if (name_ref) {
							CFStringGetCString(
								name_ref, name,
								sizeof(name),
								kCFStringEncodingUTF8);
						}
						CFTypeRef vendor =
							IOHIDDeviceGetProperty(
								device,
								CFSTR(
									kIOHIDVendorIDKey));
						CFTypeRef product =
							IOHIDDeviceGetProperty(
								device,
								CFSTR(
									kIOHIDProductIDKey));
						int vid = 0;
						int pid = 0;
						if (vendor &&
						    CFGetTypeID(vendor) ==
							    CFNumberGetTypeID()) {
							CFNumberGetValue(
								(CFNumberRef)
									vendor,
								kCFNumberIntType,
								&vid);
						}
						if (product &&
						    CFGetTypeID(product) ==
							    CFNumberGetTypeID()) {
							CFNumberGetValue(
								(CFNumberRef)
									product,
								kCFNumberIntType,
								&pid);
						}

						std::lock_guard<std::mutex>
							lock(self->devices_mutex_);
						JoypadDeviceInfo info;
						info.name = name;
						info.id = "hid:" +
							  std::to_string(vid) +
							  ":" +
							  std::to_string(pid);
						self->devices_.push_back(info);

						DeviceState state;
						state.id = info.id;
						state.name = info.name;
						state.hid_device = device;
						state.connected = true;
						self->device_states_.push_back(
							state);
					},
					this);

				IOHIDManagerRegisterInputValueCallback(
					(IOHIDManagerRef)hid_manager_,
					[](void *context, IOReturn, void *,
					   IOHIDValueRef value) {
						auto *self =
							static_cast<
								JoypadInputManager *>(
								context);
						if (!self || !value) {
							return;
						}
						IOHIDElementRef element =
							IOHIDValueGetElement(
								value);
						if (!element) {
							return;
						}
						uint32_t usage_page =
							IOHIDElementGetUsagePage(
								element);
						uint32_t usage =
							IOHIDElementGetUsage(
								element);
						if (usage_page ==
						    kHIDPage_Button) {
							CFIndex v =
								IOHIDValueGetIntegerValue(
									value);
							if (v == 0) {
								return;
							}
						} else if (usage_page !=
							   kHIDPage_GenericDesktop) {
							return;
						}
						IOHIDDeviceRef device =
							IOHIDElementGetDevice(
								element);
						if (!device) {
							return;
						}

						std::string device_id;
						std::string device_name =
							"Gamepad";
						{
							std::lock_guard<
								std::mutex>
								lock(self->devices_mutex_);
							for (const auto &state :
							     self->device_states_) {
								if (state.hid_device ==
								    device) {
									device_id =
										state.id;
									device_name =
										state.name;
									break;
								}
							}
						}

						JoypadEvent event;
						event.device_id = device_id;
						event.device_name = device_name;
						if (usage_page ==
						    kHIDPage_Button) {
							event.button = (int)usage;
							self->DispatchEvent(event);
							return;
						}

						if (usage != kHIDUsage_GD_X &&
						    usage != kHIDUsage_GD_Y &&
						    usage != kHIDUsage_GD_Z &&
						    usage != kHIDUsage_GD_Rx &&
						    usage != kHIDUsage_GD_Ry &&
						    usage != kHIDUsage_GD_Rz &&
						    usage != kHIDUsage_GD_Slider &&
						    usage != kHIDUsage_GD_Dial &&
						    usage != kHIDUsage_GD_Wheel) {
							return;
						}

						double scaled =
							IOHIDValueGetScaledValue(
								value,
								kIOHIDValueScaleTypeCalibrated);
						double raw = (double)IOHIDValueGetIntegerValue(value);
						IOHIDElementRef el =
							IOHIDValueGetElement(
								value);
						CFIndex min =
							IOHIDElementGetLogicalMin(
								el);
						CFIndex max =
							IOHIDElementGetLogicalMax(
								el);
						double norm = 0.0;
						if (max > min) {
							norm = (scaled - min) /
								       (double)(max -
										min) *
								       2.0 -
							       1.0;
						}
						norm = std::clamp(norm, -1.0,
								  1.0);

						int axis_index = 0;
						switch (usage) {
						case kHIDUsage_GD_X:
							axis_index = 0;
							break;
						case kHIDUsage_GD_Y:
							axis_index = 1;
							break;
						case kHIDUsage_GD_Z:
							axis_index = 2;
							break;
						case kHIDUsage_GD_Rx:
							axis_index = 3;
							break;
						case kHIDUsage_GD_Ry:
							axis_index = 4;
							break;
						case kHIDUsage_GD_Rz:
							axis_index = 5;
							break;
						case kHIDUsage_GD_Slider:
							axis_index = 6;
							break;
						case kHIDUsage_GD_Dial:
							axis_index = 7;
							break;
						default:
							axis_index = 0;
							break;
						}

						const int interval_ms = 0;
						std::string key =
							device_id + ":" +
							std::to_string(axis_index) +
							(raw >= 0.0 ? "+" : "-");
						auto now =
							std::chrono::steady_clock::now();
						auto it =
							self->axis_last_trigger_
								.find(key);
						double prev = 0.0;
						bool init_only = false;
						{
							std::lock_guard<
								std::mutex>
								lock(self->devices_mutex_);
							for (auto &state :
							     self->device_states_) {
								if (state.hid_device ==
								    device) {
									if (axis_index >= 0 &&
									    axis_index < 8) {
										if (!state.axis_initialized
											     [axis_index]) {
											state.axis_initialized
												[axis_index] =
												true;
											state.last_axes[axis_index] =
												raw;
											init_only = true;
										} else {
											prev = state.last_axes[axis_index];
											state.last_axes[axis_index] = raw;
										}
									}
									break;
								}
							}
						}
						if (init_only) {
							return;
						}
						if (raw == prev) {
							return;
						}
						if (it != self->axis_last_trigger_.end() &&
						    std::chrono::duration_cast<
							    std::chrono::milliseconds>(
							    now - it->second)
								    .count() <
							    interval_ms) {
							return;
						}
						self->axis_last_trigger_[key] = now;

						event.is_axis = true;
						event.axis_index = axis_index;
						event.axis_value = norm;
						event.axis_raw_value = raw;
						self->DispatchAxisAbsolute(event);
					},
					this);

				IOHIDManagerScheduleWithRunLoop(
					(IOHIDManagerRef)hid_manager_,
					CFRunLoopGetCurrent(),
					kCFRunLoopDefaultMode);
				IOHIDManagerOpen(
					(IOHIDManagerRef)hid_manager_,
					kIOHIDOptionsTypeNone);
				hid_run_loop_ = CFRunLoopGetCurrent();
			}
		}

		CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
#endif

		auto now = std::chrono::steady_clock::now();
		if (now - last_refresh > std::chrono::seconds(4)) {
			RefreshDevices();
			last_refresh = now;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}

void JoypadInputManager::DispatchEvent(const JoypadEvent &event)
{
	std::function<void(const JoypadEvent &)> button_handler;
	std::function<void(const JoypadEvent &)> learn_handler;
	std::vector<std::function<void(const JoypadEvent &)>> axis_handlers;
	{
		std::lock_guard<std::mutex> lock(handler_mutex_);
		button_handler = on_button_pressed_;
		learn_handler = learn_handler_;
		axis_handlers.reserve(axis_handlers_.size());
		for (const auto &entry : axis_handlers_) {
			axis_handlers.push_back(entry.handler);
		}
		if (learn_handler_) {
			learn_handler_ = nullptr;
		}
	}

	if (learn_handler) {
		learn_handler(event);
	}

	if (event.is_axis) {
		for (const auto &handler : axis_handlers) {
			if (handler) {
				handler(event);
			}
		}
	}

	if (!event.is_axis && button_handler) {
		button_handler(event);
	}
}

void JoypadInputManager::DispatchAxisAbsolute(const JoypadEvent &event)
{
	std::function<void(const JoypadEvent &)> learn_handler;
	std::vector<std::function<void(const JoypadEvent &)>> axis_handlers;
	{
		std::lock_guard<std::mutex> lock(handler_mutex_);
		learn_handler = learn_handler_;
		axis_handlers.reserve(axis_handlers_.size());
		for (const auto &entry : axis_handlers_) {
			axis_handlers.push_back(entry.handler);
		}
		if (learn_handler_) {
			learn_handler_ = nullptr;
		}
	}
	if (learn_handler) {
		learn_handler(event);
	}
	for (const auto &handler : axis_handlers) {
		if (handler) {
			handler(event);
		}
	}
}

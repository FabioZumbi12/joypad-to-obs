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
#include <plugin-support.h>

#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <unordered_set>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#include <dbt.h>
#include <xinput.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <wbemidl.h>
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

namespace {
constexpr int kMaxTrackedAxes = 8;
constexpr double kAxisContinuousHoldThreshold = 0.01;

#ifdef _WIN32
struct DiControllerInfo {
	IDirectInputDevice8W *device = nullptr;
	std::string id;
	std::string stable_id;
	std::string type_id;
	std::string name;
};

struct XInputApi {
	HMODULE module = nullptr;
	DWORD(WINAPI *get_state)(DWORD, XINPUT_STATE *) = nullptr;
	DWORD(WINAPI *get_capabilities)(DWORD, DWORD, XINPUT_CAPABILITIES *) = nullptr;
};

struct XInputControllerInfo {
	DWORD slot = 0;
	std::string id;
	std::string stable_id;
	std::string type_id;
	std::string name;
};

std::string wide_to_utf8(const wchar_t *wstr);

XInputApi g_xinput_api;

bool xinput_ready()
{
	return g_xinput_api.module != nullptr && g_xinput_api.get_state != nullptr;
}

void load_xinput_api()
{
	if (xinput_ready()) {
		return;
	}
	const char *dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
	for (const char *dll_name : dlls) {
		HMODULE mod = LoadLibraryA(dll_name);
		if (!mod) {
			continue;
		}
		auto fn_get_state = (DWORD(WINAPI *)(DWORD, XINPUT_STATE *))GetProcAddress(mod, "XInputGetState");
		if (!fn_get_state) {
			FreeLibrary(mod);
			continue;
		}
		auto fn_get_caps = (DWORD(WINAPI *)(DWORD, DWORD,
						    XINPUT_CAPABILITIES *))GetProcAddress(mod, "XInputGetCapabilities");
		g_xinput_api.module = mod;
		g_xinput_api.get_state = fn_get_state;
		g_xinput_api.get_capabilities = fn_get_caps;
		return;
	}
}

void unload_xinput_api()
{
	if (g_xinput_api.module) {
		FreeLibrary(g_xinput_api.module);
	}
	g_xinput_api = {};
}

std::string xinput_type_id(DWORD slot)
{
	if (!xinput_ready() || !g_xinput_api.get_capabilities) {
		return "VID_045E&PID_XINPUT";
	}
	XINPUT_CAPABILITIES caps = {};
	if (g_xinput_api.get_capabilities(slot, XINPUT_FLAG_GAMEPAD, &caps) != ERROR_SUCCESS) {
		return "VID_045E&PID_XINPUT";
	}
	if (caps.SubType == XINPUT_DEVSUBTYPE_GAMEPAD) {
		return "VID_045E&PID_XINPUT";
	}
	return "XINPUT_GAMEPAD";
}

std::vector<XInputControllerInfo> enumerate_xinput_controllers()
{
	std::vector<XInputControllerInfo> out;
	if (!xinput_ready()) {
		return out;
	}
	for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot) {
		XINPUT_STATE state = {};
		if (g_xinput_api.get_state(slot, &state) != ERROR_SUCCESS) {
			continue;
		}
		XInputControllerInfo info;
		info.slot = slot;
		info.id = "xinput:" + std::to_string((unsigned long long)slot);
		info.stable_id = info.id;
		info.type_id = xinput_type_id(slot);
		info.name = "Xbox Controller " + std::to_string((unsigned long long)(slot + 1));
		out.push_back(std::move(info));
	}
	return out;
}

std::string to_upper_ascii(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::toupper(c); });
	return text;
}

bool extract_vid_pid(const std::string &text, uint16_t &vid_out, uint16_t &pid_out)
{
	std::string upper = to_upper_ascii(text);
	size_t vid_pos = upper.find("VID_");
	size_t pid_pos = upper.find("PID_");
	if (vid_pos == std::string::npos || pid_pos == std::string::npos || vid_pos + 8 > upper.size() ||
	    pid_pos + 8 > upper.size()) {
		return false;
	}
	char *endp = nullptr;
	unsigned long vid = strtoul(upper.substr(vid_pos + 4, 4).c_str(), &endp, 16);
	if (!endp || *endp != '\0' || vid > 0xFFFF) {
		return false;
	}
	unsigned long pid = strtoul(upper.substr(pid_pos + 4, 4).c_str(), &endp, 16);
	if (!endp || *endp != '\0' || pid > 0xFFFF) {
		return false;
	}
	vid_out = (uint16_t)vid;
	pid_out = (uint16_t)pid;
	return true;
}

std::unordered_set<uint32_t> query_xinput_vidpid_from_wmi()
{
	std::unordered_set<uint32_t> out;

	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	const bool should_uninit = SUCCEEDED(hr);
	if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
		return out;
	}

	hr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
				  nullptr, EOAC_NONE, nullptr);
	if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
		if (should_uninit) {
			CoUninitialize();
		}
		return out;
	}

	IWbemLocator *locator = nullptr;
	hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
			      reinterpret_cast<void **>(&locator));
	if (FAILED(hr) || !locator) {
		if (should_uninit) {
			CoUninitialize();
		}
		return out;
	}

	IWbemServices *services = nullptr;
	BSTR ns = SysAllocString(L"ROOT\\CIMV2");
	hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
	SysFreeString(ns);
	if (FAILED(hr) || !services) {
		locator->Release();
		if (should_uninit) {
			CoUninitialize();
		}
		return out;
	}

	hr = CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
			       RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
	if (FAILED(hr)) {
		services->Release();
		locator->Release();
		if (should_uninit) {
			CoUninitialize();
		}
		return out;
	}

	IEnumWbemClassObject *enumerator = nullptr;
	BSTR lang = SysAllocString(L"WQL");
	BSTR query = SysAllocString(L"SELECT DeviceID FROM Win32_PNPEntity WHERE DeviceID LIKE '%IG_%'");
	hr = services->ExecQuery(lang, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
				 &enumerator);
	SysFreeString(query);
	SysFreeString(lang);
	if (SUCCEEDED(hr) && enumerator) {
		while (true) {
			IWbemClassObject *obj = nullptr;
			ULONG ret = 0;
			hr = enumerator->Next(200, 1, &obj, &ret);
			if (FAILED(hr) || ret == 0 || !obj) {
				break;
			}

			VARIANT var;
			VariantInit(&var);
			HRESULT get_hr = obj->Get(L"DeviceID", 0, &var, nullptr, nullptr);
			if (SUCCEEDED(get_hr) && var.vt == VT_BSTR && var.bstrVal) {
				std::string device_id = wide_to_utf8(var.bstrVal);
				uint16_t vid = 0;
				uint16_t pid = 0;
				if (extract_vid_pid(device_id, vid, pid)) {
					out.insert((uint32_t)MAKELONG(vid, pid));
				}
			}
			VariantClear(&var);
			obj->Release();
		}
		enumerator->Release();
	}

	services->Release();
	locator->Release();
	if (should_uninit) {
		CoUninitialize();
	}
	return out;
}

bool is_xinput_vidpid(uint16_t vid, uint16_t pid)
{
	static std::once_flag once;
	static std::unordered_set<uint32_t> xinput_vidpid;
	std::call_once(once, []() { xinput_vidpid = query_xinput_vidpid_from_wmi(); });
	return xinput_vidpid.find((uint32_t)MAKELONG(vid, pid)) != xinput_vidpid.end();
}

bool is_xinput_shadow_device(const DiControllerInfo &di_info, const std::vector<XInputControllerInfo> &xinput_infos)
{
	if (xinput_infos.empty()) {
		return false;
	}

	uint16_t vid = 0;
	uint16_t pid = 0;
	if (extract_vid_pid(di_info.type_id, vid, pid) && is_xinput_vidpid(vid, pid)) {
		return true;
	}

	std::string name_up = to_upper_ascii(di_info.name);
	if (name_up.find("XBOX") != std::string::npos || name_up.find("XINPUT") != std::string::npos) {
		return true;
	}
	return false;
}

double xinput_normalize_thumb(short v)
{
	if (v >= 0) {
		return std::clamp((double)v / 32767.0, 0.0, 1.0);
	}
	return std::clamp((double)v / 32768.0, -1.0, 0.0);
}

void xinput_read_buttons_axes(const XINPUT_STATE &state, uint32_t &buttons_out,
			      std::array<double, kMaxTrackedAxes> &axes_out)
{
	buttons_out = 0;
	const WORD b = state.Gamepad.wButtons;
	if (b & XINPUT_GAMEPAD_A)
		buttons_out |= (1u << 0);
	if (b & XINPUT_GAMEPAD_B)
		buttons_out |= (1u << 1);
	if (b & XINPUT_GAMEPAD_X)
		buttons_out |= (1u << 2);
	if (b & XINPUT_GAMEPAD_Y)
		buttons_out |= (1u << 3);
	if (b & XINPUT_GAMEPAD_LEFT_SHOULDER)
		buttons_out |= (1u << 4);
	if (b & XINPUT_GAMEPAD_RIGHT_SHOULDER)
		buttons_out |= (1u << 5);
	if (b & XINPUT_GAMEPAD_BACK)
		buttons_out |= (1u << 6);
	if (b & XINPUT_GAMEPAD_START)
		buttons_out |= (1u << 7);
	if (b & XINPUT_GAMEPAD_LEFT_THUMB)
		buttons_out |= (1u << 8);
	if (b & XINPUT_GAMEPAD_RIGHT_THUMB)
		buttons_out |= (1u << 9);
	if (b & XINPUT_GAMEPAD_DPAD_UP)
		buttons_out |= (1u << 10);
	if (b & XINPUT_GAMEPAD_DPAD_DOWN)
		buttons_out |= (1u << 11);
	if (b & XINPUT_GAMEPAD_DPAD_LEFT)
		buttons_out |= (1u << 12);
	if (b & XINPUT_GAMEPAD_DPAD_RIGHT)
		buttons_out |= (1u << 13);

	axes_out[0] = xinput_normalize_thumb(state.Gamepad.sThumbLX);
	axes_out[1] = xinput_normalize_thumb(state.Gamepad.sThumbLY);
	axes_out[2] = xinput_normalize_thumb(state.Gamepad.sThumbRX);
	axes_out[3] = xinput_normalize_thumb(state.Gamepad.sThumbRY);
	axes_out[4] = std::clamp((double)state.Gamepad.bLeftTrigger / 255.0, 0.0, 1.0);
	axes_out[5] = std::clamp((double)state.Gamepad.bRightTrigger / 255.0, 0.0, 1.0);
	axes_out[6] = 0.0;
	axes_out[7] = 0.0;
}

std::string wide_to_utf8(const wchar_t *wstr)
{
	if (!wstr || !*wstr) {
		return {};
	}
	int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 0) {
		return {};
	}
	std::vector<char> out((size_t)len);
	WideCharToMultiByte(CP_UTF8, 0, wstr, -1, out.data(), len, nullptr, nullptr);
	return std::string(out.data());
}

std::string guid_to_string(const GUID &guid)
{
	char buf[64] = {};
	snprintf(buf, sizeof(buf), "%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX",
		 (unsigned long)guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2],
		 guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
	return std::string(buf);
}

std::string dinput_type_id(const GUID &product_guid)
{
	uint16_t vid = LOWORD(product_guid.Data1);
	uint16_t pid = HIWORD(product_guid.Data1);
	if (vid != 0 || pid != 0) {
		char buf[32] = {};
		snprintf(buf, sizeof(buf), "VID_%04X&PID_%04X", vid, pid);
		return std::string(buf);
	}
	return "DINPUT_" + guid_to_string(product_guid);
}

BOOL CALLBACK enum_axis_callback(const DIDEVICEOBJECTINSTANCEW *instance, void *context)
{
	(void)instance;
	auto *device = static_cast<IDirectInputDevice8W *>(context);
	if (!device) {
		return DIENUM_CONTINUE;
	}
	DIPROPRANGE range = {};
	range.diph.dwSize = sizeof(DIPROPRANGE);
	range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
	range.diph.dwObj = instance->dwType;
	range.diph.dwHow = DIPH_BYID;
	range.lMin = -1000;
	range.lMax = 1000;
	device->SetProperty(DIPROP_RANGE, &range.diph);
	return DIENUM_CONTINUE;
}

struct EnumContext {
	IDirectInput8W *dinput = nullptr;
	HWND hwnd = nullptr;
	std::vector<DiControllerInfo> *out = nullptr;
};

BOOL CALLBACK enum_device_callback(const DIDEVICEINSTANCEW *instance, void *context_ptr)
{
	auto *context = static_cast<EnumContext *>(context_ptr);
	if (!context || !context->dinput || !context->out) {
		return DIENUM_STOP;
	}

	IDirectInputDevice8W *device = nullptr;
	if (FAILED(context->dinput->CreateDevice(instance->guidInstance, &device, nullptr)) || !device) {
		return DIENUM_CONTINUE;
	}

	if (FAILED(device->SetDataFormat(&c_dfDIJoystick2))) {
		device->Release();
		return DIENUM_CONTINUE;
	}

	if (FAILED(device->SetCooperativeLevel(context->hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE))) {
		device->Release();
		return DIENUM_CONTINUE;
	}

	device->EnumObjects(enum_axis_callback, device, DIDFT_AXIS);
	device->Acquire();

	DiControllerInfo info;
	info.device = device;
	info.id = "dinput:" + guid_to_string(instance->guidInstance);
	info.stable_id = info.id;
	info.type_id = dinput_type_id(instance->guidProduct);
	info.name = wide_to_utf8(instance->tszProductName);
	if (info.name.empty()) {
		info.name = wide_to_utf8(instance->tszInstanceName);
	}
	if (info.name.empty()) {
		info.name = "Controller";
	}
	context->out->push_back(std::move(info));

	return DIENUM_CONTINUE;
}

HWND create_input_window()
{
	static const wchar_t *kClassName = L"JoypadToOBSDeviceNotifyWindow";
	static bool class_registered = false;
	HINSTANCE hinst = GetModuleHandleW(nullptr);

	if (!class_registered) {
		WNDCLASSW wc = {};
		wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) -> LRESULT {
			switch (msg) {
			case WM_NCCREATE: {
				CREATESTRUCTW *cs = reinterpret_cast<CREATESTRUCTW *>(lparam);
				SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
				return TRUE;
			}
			case WM_DEVICECHANGE: {
				if (wparam == DBT_DEVICEARRIVAL || wparam == DBT_DEVICEREMOVECOMPLETE ||
				    wparam == DBT_DEVNODES_CHANGED) {
					auto *flag = reinterpret_cast<std::atomic<bool> *>(
						GetWindowLongPtrW(hwnd, GWLP_USERDATA));
					if (flag) {
						flag->store(true);
					}
				}
				return 0;
			}
			default:
				break;
			}
			return DefWindowProcW(hwnd, msg, wparam, lparam);
		};
		wc.hInstance = hinst;
		wc.lpszClassName = kClassName;
		class_registered = (RegisterClassW(&wc) != 0);
	}

	if (!class_registered) {
		return nullptr;
	}

	HWND hwnd = CreateWindowExW(0, kClassName, L"joypad-to-obs-device-notify", WS_POPUP, 0, 0, 1, 1, nullptr,
				    nullptr, hinst, nullptr);
	return hwnd;
}

std::vector<DiControllerInfo> enumerate_dinput_controllers(IDirectInput8W *dinput, HWND hwnd)
{
	std::vector<DiControllerInfo> out;
	if (!dinput || !hwnd) {
		return out;
	}
	EnumContext context;
	context.dinput = dinput;
	context.hwnd = hwnd;
	context.out = &out;
	dinput->EnumDevices(DI8DEVCLASS_GAMECTRL, enum_device_callback, &context, DIEDFL_ATTACHEDONLY);
	std::sort(out.begin(), out.end(),
		  [](const DiControllerInfo &a, const DiControllerInfo &b) { return a.id < b.id; });
	return out;
}
#endif
} // namespace

JoypadInputManager::JoypadInputManager() = default;

JoypadInputManager::~JoypadInputManager()
{
	Stop();
}

void JoypadInputManager::SetNativeWindowHandle(void *hwnd)
{
#if defined(_WIN32)
	std::lock_guard<std::mutex> lock(devices_mutex_);
	native_hwnd_ = hwnd;
#else
	(void)hwnd;
#endif
}

void JoypadInputManager::Start()
{
	if (running_.exchange(true)) {
		return;
	}

#if defined(_WIN32)
	device_change_pending_.store(false);
	load_xinput_api();
	HWND notify_hwnd = create_input_window();
	if (notify_hwnd) {
		SetWindowLongPtrW(notify_hwnd, GWLP_USERDATA, (LONG_PTR)&device_change_pending_);
		dinput_notify_hwnd_ = notify_hwnd;

		DEV_BROADCAST_DEVICEINTERFACE_W filter = {};
		filter.dbcc_size = sizeof(filter);
		filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
		filter.dbcc_classguid = {0x4D1E55B2, 0xF16F, 0x11CF, {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
		dinput_devnotify_ = RegisterDeviceNotificationW(notify_hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
	}

	HWND coop_hwnd = static_cast<HWND>(native_hwnd_);
	if (!coop_hwnd) {
		coop_hwnd = notify_hwnd;
	}
	if (coop_hwnd) {
		IDirectInput8W *dinput = nullptr;
		HINSTANCE hinst = GetModuleHandleW(nullptr);
		if (SUCCEEDED(DirectInput8Create(hinst, DIRECTINPUT_VERSION, IID_IDirectInput8W,
						 reinterpret_cast<void **>(&dinput), nullptr)) &&
		    dinput) {
			dinput_ = dinput;
			dinput_hwnd_ = coop_hwnd;
		} else {
			dinput_hwnd_ = nullptr;
		}
	}
#endif

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

#if defined(_WIN32)
	{
		std::lock_guard<std::mutex> lock(devices_mutex_);
		for (auto &state : device_states_) {
			auto *dev = static_cast<IDirectInputDevice8W *>(state.di_device);
			if (dev) {
				dev->Unacquire();
				dev->Release();
				state.di_device = nullptr;
			}
		}
		devices_.clear();
		device_states_.clear();
	}
	if (dinput_) {
		auto *di = static_cast<IDirectInput8W *>(dinput_);
		di->Release();
		dinput_ = nullptr;
	}
	unload_xinput_api();
	if (dinput_devnotify_) {
		UnregisterDeviceNotification(static_cast<HDEVNOTIFY>(dinput_devnotify_));
		dinput_devnotify_ = nullptr;
	}
	if (dinput_notify_hwnd_) {
		DestroyWindow(static_cast<HWND>(dinput_notify_hwnd_));
		dinput_notify_hwnd_ = nullptr;
	}
	dinput_hwnd_ = nullptr;
#elif defined(__APPLE__)
	if (hid_manager_) {
		IOHIDManagerClose((IOHIDManagerRef)hid_manager_, kIOHIDOptionsTypeNone);
		CFRelease((IOHIDManagerRef)hid_manager_);
		hid_manager_ = nullptr;
		hid_run_loop_ = nullptr;
	}
	{
		std::lock_guard<std::mutex> lock(devices_mutex_);
		devices_.clear();
		device_states_.clear();
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
	std::unordered_map<std::string, std::string> previous_devices;
	previous_devices.reserve(device_states_.size());
	for (const auto &state : device_states_) {
		previous_devices[state.id] = state.name;
	}
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
	IDirectInput8W *dinput = static_cast<IDirectInput8W *>(dinput_);
	HWND hwnd = static_cast<HWND>(dinput_hwnd_);
	if (!dinput || !hwnd) {
		for (auto &old_state : device_states_) {
			auto *dev = static_cast<IDirectInputDevice8W *>(old_state.di_device);
			if (dev) {
				dev->Unacquire();
				dev->Release();
			}
		}
		devices_.clear();
		device_states_.clear();
		return;
	}

	auto xinput_controllers = enumerate_xinput_controllers();
	auto controllers = enumerate_dinput_controllers(dinput, hwnd);
	std::unordered_set<std::string> seen_ids;
	seen_ids.reserve(controllers.size() + xinput_controllers.size());

	for (const auto &xinfo : xinput_controllers) {
		seen_ids.insert(xinfo.id);
		DeviceState state;
		DeviceState *existing = find_existing(xinfo.id);
		if (existing) {
			state = *existing;
		} else {
			for (size_t k = 0; k < 8; ++k) {
				state.axis_initialized[k] = false;
			}
		}
		state.id = xinfo.id;
		state.stable_id = xinfo.stable_id;
		state.type_id = xinfo.type_id;
		state.name = xinfo.name;
		state.connected = true;
		state.di_device = nullptr;
		state.is_xinput = true;
		state.xinput_slot = xinfo.slot;

		JoypadDeviceInfo info;
		info.id = state.id;
		info.stable_id = state.stable_id;
		info.type_id = state.type_id;
		info.name = state.name;
		next_devices.push_back(info);
		next_states.push_back(state);
	}

	for (const auto &controller : controllers) {
		if (is_xinput_shadow_device(controller, xinput_controllers)) {
			if (controller.device) {
				controller.device->Unacquire();
				controller.device->Release();
			}
			continue;
		}
		seen_ids.insert(controller.id);
		DeviceState state;
		DeviceState *existing = find_existing(controller.id);
		IDirectInputDevice8W *old_dev = existing ? static_cast<IDirectInputDevice8W *>(existing->di_device)
							 : nullptr;
		if (existing) {
			state = *existing;
		} else {
			for (size_t k = 0; k < 8; ++k) {
				state.axis_initialized[k] = false;
			}
		}

		state.id = controller.id;
		state.stable_id = controller.stable_id;
		state.type_id = controller.type_id;
		state.name = controller.name;
		state.connected = true;
		state.di_device = controller.device;
		state.is_xinput = false;
		state.xinput_slot = 0;
		if (old_dev && old_dev != controller.device) {
			old_dev->Unacquire();
			old_dev->Release();
		}

		JoypadDeviceInfo info;
		info.id = state.id;
		info.stable_id = state.stable_id;
		info.type_id = state.type_id;
		info.name = state.name;
		next_devices.push_back(info);
		next_states.push_back(state);
	}

	for (auto &old_state : device_states_) {
		if (seen_ids.find(old_state.id) != seen_ids.end()) {
			continue;
		}
		auto *dev = static_cast<IDirectInputDevice8W *>(old_state.di_device);
		if (dev) {
			dev->Unacquire();
			dev->Release();
		}
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
			if (state.fd < 0) {
				int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
				if (fd < 0) {
					continue;
				}
				state.fd = fd;
				char name[128] = {};
				if (ioctl(fd, JSIOCGNAME(sizeof(name)), name) >= 0 && name[0] != '\0') {
					state.name = name;
				}
			}
		} else {
			int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
			if (fd < 0) {
				continue;
			}
			char name[128] = {};
			if (ioctl(fd, JSIOCGNAME(sizeof(name)), name) < 0) {
				snprintf(name, sizeof(name), "Joystick %s", ent->d_name);
			}
			state.fd = fd;
			state.id = id;
			state.stable_id = id;
			state.type_id = id;
			state.name = name;
			state.connected = true;
			for (size_t k = 0; k < 8; ++k) {
				state.axis_initialized[k] = false;
			}
		}
		state.connected = true;
		state.stable_id = state.id;
		state.type_id = state.id;

		JoypadDeviceInfo info;
		info.id = state.id;
		info.stable_id = state.stable_id;
		info.type_id = state.type_id;
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

	std::unordered_map<std::string, std::string> current_devices;
	current_devices.reserve(device_states_.size());
	for (const auto &state : device_states_) {
		current_devices[state.id] = state.name;
	}

	for (const auto &entry : current_devices) {
		if (previous_devices.find(entry.first) == previous_devices.end()) {
			obs_log(LOG_INFO, "joypad-to-obs device connected: %s (%s)", entry.first.c_str(),
				entry.second.c_str());
		}
	}
	for (const auto &entry : previous_devices) {
		if (current_devices.find(entry.first) == current_devices.end()) {
			obs_log(LOG_INFO, "joypad-to-obs device disconnected: %s (%s)", entry.first.c_str(),
				entry.second.c_str());
		}
	}
}

void JoypadInputManager::SetOnButtonPressed(std::function<void(const JoypadEvent &)> handler)
{
	std::lock_guard<std::mutex> lock(handler_mutex_);
	on_button_pressed_ = std::move(handler);
}

void JoypadInputManager::SetOnAxisChanged(std::function<void(const JoypadEvent &)> handler)
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

int JoypadInputManager::AddOnAxisChanged(std::function<void(const JoypadEvent &)> handler)
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
				 [handler_id](const AxisHandlerEntry &entry) { return entry.id == handler_id; });
	axis_handlers_.erase(it, axis_handlers_.end());
}

bool JoypadInputManager::GetAxisRawValue(const std::string &device_id, int axis_index, double &raw_out) const
{
	if (axis_index < 0 || axis_index >= kMaxTrackedAxes) {
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

bool JoypadInputManager::IsButtonPressed(const std::string &device_id, const std::string &device_stable_id,
					 const std::string &device_type_id, int button) const
{
	if (button <= 0 || button > 32) {
		return false;
	}

	const uint32_t mask = 1u << (uint32_t)(button - 1);
	std::lock_guard<std::mutex> lock(devices_mutex_);
	for (const auto &state : device_states_) {
		const bool same_id = !device_id.empty() && state.id == device_id;
		const bool same_stable = !device_stable_id.empty() && state.stable_id == device_stable_id;
		const bool same_type = !device_type_id.empty() && state.type_id == device_type_id;
		const bool any_device = device_id.empty() && device_stable_id.empty() && device_type_id.empty();
		if (!same_id && !same_stable && !same_type && !any_device) {
			continue;
		}
		return (state.last_buttons & mask) != 0;
	}
	return false;
}

bool JoypadInputManager::BeginLearn(std::function<void(const JoypadEvent &)> handler)
{
	std::lock_guard<std::mutex> lock(handler_mutex_);
	if (learn_handler_) {
		return false;
	}
	learn_handler_ = std::move(handler);
	learn_active_.store(true, std::memory_order_release);
	return true;
}

void JoypadInputManager::CancelLearn()
{
	std::lock_guard<std::mutex> lock(handler_mutex_);
	learn_handler_ = nullptr;
	learn_active_.store(false, std::memory_order_release);
}

void JoypadInputManager::MarkDeviceDisconnected(DeviceState &state)
{
	state.connected = false;
	state.last_buttons = 0;
	state.resync_axes = true;
	for (int i = 0; i < kMaxTrackedAxes; ++i) {
		state.axis_initialized[i] = false;
		state.last_axes[i] = 0.0;
	}
}

void JoypadInputManager::PollLoop()
{
	auto last_refresh = std::chrono::steady_clock::now();
	[[maybe_unused]] const double default_threshold = 0.1;
	[[maybe_unused]] const int default_interval_ms = 0;
	while (running_.load()) {
		std::vector<JoypadEvent> pending_button_events;
		std::vector<JoypadEvent> pending_axis_events;
#ifdef _WIN32
		{
			std::lock_guard<std::mutex> lock(devices_mutex_);
			for (auto &state : device_states_) {
				uint32_t buttons = 0;
				std::array<double, kMaxTrackedAxes> axis_values = {0.0, 0.0, 0.0, 0.0,
										   0.0, 0.0, 0.0, 0.0};
				int axes_to_read = kMaxTrackedAxes;
				uint32_t digital_count = 20;

				if (state.is_xinput) {
					if (!xinput_ready()) {
						continue;
					}
					XINPUT_STATE xstate = {};
					if (g_xinput_api.get_state(state.xinput_slot, &xstate) != ERROR_SUCCESS) {
						continue;
					}
					state.connected = true;
					digital_count = 14;
					axes_to_read = 6;
					xinput_read_buttons_axes(xstate, buttons, axis_values);
				} else {
					auto *dev = static_cast<IDirectInputDevice8W *>(state.di_device);
					if (!dev) {
						MarkDeviceDisconnected(state);
						continue;
					}

					HRESULT hr = dev->Poll();
					if (FAILED(hr)) {
						MarkDeviceDisconnected(state);
						hr = dev->Acquire();
						// Avoid infinite loops when device is unplugged/lost.
						for (int tries = 0; hr == DIERR_INPUTLOST && tries < 8; ++tries) {
							hr = dev->Acquire();
						}
						hr = dev->Poll();
					}
					if (FAILED(hr)) {
						// Disconnection detection is event-driven (WM_DEVICECHANGE).
						MarkDeviceDisconnected(state);
						continue;
					}

					DIJOYSTATE2 js = {};
					hr = dev->GetDeviceState(sizeof(js), &js);
					if (FAILED(hr)) {
						// Disconnection detection is event-driven (WM_DEVICECHANGE).
						MarkDeviceDisconnected(state);
						continue;
					}
					const bool needs_resync = !state.connected || state.resync_axes;
					state.connected = true;
					state.resync_axes = false;

					for (uint32_t i = 0; i < 16; ++i) {
						if (js.rgbButtons[i] & 0x80) {
							buttons |= (1u << i);
						}
					}

					DWORD pov = js.rgdwPOV[0];
					if (LOWORD(pov) != 0xFFFF) {
						if (pov <= 4500 || pov >= 31500)
							buttons |= (1u << 16); // up
						if (pov >= 4500 && pov <= 13500)
							buttons |= (1u << 17); // right
						if (pov >= 13500 && pov <= 22500)
							buttons |= (1u << 18); // down
						if (pov >= 22500 && pov <= 31500)
							buttons |= (1u << 19); // left
					}

					axis_values[0] = std::clamp((double)js.lX / 1000.0, -1.0, 1.0);
					axis_values[1] = std::clamp((double)js.lY / 1000.0, -1.0, 1.0);
					axis_values[2] = std::clamp((double)js.lZ / 1000.0, -1.0, 1.0);
					axis_values[3] = std::clamp((double)js.lRx / 1000.0, -1.0, 1.0);
					axis_values[4] = std::clamp((double)js.lRy / 1000.0, -1.0, 1.0);
					axis_values[5] = std::clamp((double)js.lRz / 1000.0, -1.0, 1.0);
					axis_values[6] = std::clamp((double)js.rglSlider[0] / 1000.0, -1.0, 1.0);
					axis_values[7] = std::clamp((double)js.rglSlider[1] / 1000.0, -1.0, 1.0);

					if (needs_resync) {
						state.last_buttons = buttons;
						for (int i = 0; i < axes_to_read; ++i) {
							double normalized = axis_values[(size_t)i];
							double raw = ((normalized + 1.0) * 0.5) * 1024.0;
							state.last_axes[i] = raw;
							state.axis_initialized[i] = true;
						}
						for (int i = axes_to_read; i < kMaxTrackedAxes; ++i) {
							state.axis_initialized[i] = false;
							state.last_axes[i] = 0.0;
						}
						continue;
					}
				}

				uint32_t changed = buttons & ~state.last_buttons;
				if (changed) {
					for (uint32_t bit = 0; bit < digital_count; ++bit) {
						if (changed & (1u << bit)) {
							JoypadEvent event;
							event.device_id = state.id;
							event.device_stable_id = state.stable_id;
							event.device_type_id = state.type_id;
							event.device_name = state.name;
							event.button = bit + 1;
							pending_button_events.push_back(std::move(event));
						}
					}
				}
				state.last_buttons = buttons;

				for (int i = 0; i < axes_to_read; ++i) {
					double normalized = axis_values[(size_t)i];
					double raw = ((normalized + 1.0) * 0.5) * 1024.0;
					std::string key = state.id + ":" + std::to_string(i) + (raw >= 0.0 ? "+" : "-");
					auto now = std::chrono::steady_clock::now();
					auto it = axis_last_trigger_.find(key);
					if (!state.axis_initialized[i]) {
						state.last_axes[i] = raw;
						state.axis_initialized[i] = true;
						continue;
					}
					double prev_raw = state.last_axes[i];
					const bool hold_active = std::fabs(normalized) >= kAxisContinuousHoldThreshold;
					const bool learning_active = learn_active_.load(std::memory_order_acquire);
					if (std::fabs(raw - prev_raw) < 0.000001 && (!hold_active || learning_active)) {
						continue;
					}
					if (it != axis_last_trigger_.end() &&
					    std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second)
							    .count() < default_interval_ms) {
						state.last_axes[i] = raw;
						continue;
					}
					axis_last_trigger_[key] = now;
					JoypadEvent event;
					event.device_id = state.id;
					event.device_stable_id = state.stable_id;
					event.device_type_id = state.type_id;
					event.device_name = state.name;
					event.is_axis = true;
					event.axis_index = i;
					event.axis_value = normalized;
					event.axis_raw_value = raw;
					pending_axis_events.push_back(std::move(event));
					state.last_axes[i] = raw;
				}

				for (int i = axes_to_read; i < kMaxTrackedAxes; ++i) {
					state.axis_initialized[i] = false;
					state.last_axes[i] = 0.0;
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
				ssize_t bytes_read = 0;
				while ((bytes_read = read(state.fd, &e, sizeof(e))) == (ssize_t)sizeof(e)) {
					if (e.type & JS_EVENT_INIT) {
						continue;
					}
					if ((e.type & JS_EVENT_BUTTON) != 0 && e.value) {
						JoypadEvent event;
						event.device_id = state.id;
						event.device_stable_id = state.stable_id;
						event.device_type_id = state.type_id;
						event.device_name = state.name;
						event.button = (int)e.number + 1;
						pending_button_events.push_back(std::move(event));
					}
					if ((e.type & JS_EVENT_AXIS) != 0) {
						const int axis_index = (int)e.number;
						if (axis_index < 0 || axis_index >= kMaxTrackedAxes) {
							continue;
						}
						double value = (double)e.value / 32767.0;
						double raw = (double)e.value;
						if (value < -1.0) {
							value = -1.0;
						}
						if (value > 1.0) {
							value = 1.0;
						}
						std::string key = state.id + ":" + std::to_string(axis_index) +
								  (value >= 0.0 ? "+" : "-");
						auto now = std::chrono::steady_clock::now();
						auto it = axis_last_trigger_.find(key);
						if (!state.axis_initialized[axis_index]) {
							state.last_axes[axis_index] = raw;
							state.axis_initialized[axis_index] = true;
							continue;
						}
						double prev = state.last_axes[axis_index];
						const bool hold_active = std::fabs(value) >=
									 kAxisContinuousHoldThreshold;
						const bool learning_active =
							learn_active_.load(std::memory_order_acquire);
						if (raw == prev && (!hold_active || learning_active)) {
							continue;
						}
						if (it != axis_last_trigger_.end() &&
						    std::chrono::duration_cast<std::chrono::milliseconds>(now -
													  it->second)
								    .count() < default_interval_ms) {
							state.last_axes[axis_index] = raw;
							continue;
						}
						axis_last_trigger_[key] = now;
						JoypadEvent event;
						event.device_id = state.id;
						event.device_stable_id = state.stable_id;
						event.device_type_id = state.type_id;
						event.device_name = state.name;
						event.is_axis = true;
						event.axis_index = axis_index;
						event.axis_value = value;
						event.axis_raw_value = raw;
						pending_axis_events.push_back(std::move(event));
						state.last_axes[axis_index] = raw;
					}
				}
				if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
					MarkDeviceDisconnected(state);
					if (state.fd >= 0) {
						close(state.fd);
						state.fd = -1;
					}
				}
			}
		}
#elif defined(__APPLE__)
		// macOS uses a CFRunLoop in this thread.
		if (!hid_manager_) {
			hid_manager_ = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
			if (hid_manager_) {
				IOHIDManagerSetDeviceMatching((IOHIDManagerRef)hid_manager_, nullptr);
				IOHIDManagerRegisterDeviceMatchingCallback(
					(IOHIDManagerRef)hid_manager_,
					[](void *context, IOReturn, void *, IOHIDDeviceRef device) {
						auto *self = static_cast<JoypadInputManager *>(context);
						if (!self || !device) {
							return;
						}
						CFStringRef name_ref = (CFStringRef)IOHIDDeviceGetProperty(
							device, CFSTR(kIOHIDProductKey));
						char name[256] = "Gamepad";
						if (name_ref) {
							CFStringGetCString(name_ref, name, sizeof(name),
									   kCFStringEncodingUTF8);
						}
						CFTypeRef vendor =
							IOHIDDeviceGetProperty(device, CFSTR(kIOHIDVendorIDKey));
						CFTypeRef product =
							IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey));
						int vid = 0;
						int pid = 0;
						if (vendor && CFGetTypeID(vendor) == CFNumberGetTypeID()) {
							CFNumberGetValue((CFNumberRef)vendor, kCFNumberIntType, &vid);
						}
						if (product && CFGetTypeID(product) == CFNumberGetTypeID()) {
							CFNumberGetValue((CFNumberRef)product, kCFNumberIntType, &pid);
						}
						std::string device_id =
							"hid:" + std::to_string(vid) + ":" + std::to_string(pid);
						std::string device_stable_id = device_id;
						char type_buf[32] = {};
						snprintf(type_buf, sizeof(type_buf), "VID_%04X&PID_%04X", vid & 0xFFFF,
							 pid & 0xFFFF);
						std::string device_type_id = type_buf;

						std::lock_guard<std::mutex> lock(self->devices_mutex_);
						for (auto &state : self->device_states_) {
							if (state.hid_device == device) {
								state.connected = true;
								state.name = name;
								state.id = device_id;
								state.stable_id = device_stable_id;
								state.type_id = device_type_id;
								return;
							}
						}

						bool known_device = false;
						for (auto &info : self->devices_) {
							if (info.id == device_id) {
								info.name = name;
								info.stable_id = device_stable_id;
								info.type_id = device_type_id;
								known_device = true;
								break;
							}
						}
						if (!known_device) {
							JoypadDeviceInfo info;
							info.name = name;
							info.id = device_id;
							info.stable_id = device_stable_id;
							info.type_id = device_type_id;
							self->devices_.push_back(info);
						}

						DeviceState state;
						state.id = device_id;
						state.stable_id = device_stable_id;
						state.type_id = device_type_id;
						state.name = name;
						state.hid_device = device;
						state.connected = true;
						self->device_states_.push_back(state);
					},
					this);
				IOHIDManagerRegisterDeviceRemovalCallback(
					(IOHIDManagerRef)hid_manager_,
					[](void *context, IOReturn, void *, IOHIDDeviceRef device) {
						auto *self = static_cast<JoypadInputManager *>(context);
						if (!self || !device) {
							return;
						}
						std::lock_guard<std::mutex> lock(self->devices_mutex_);
						std::string removed_id;
						for (const auto &state : self->device_states_) {
							if (state.hid_device == device) {
								removed_id = state.id;
								break;
							}
						}
						self->device_states_.erase(
							std::remove_if(self->device_states_.begin(),
								       self->device_states_.end(),
								       [device](const DeviceState &state) {
									       return state.hid_device == device;
								       }),
							self->device_states_.end());

						if (!removed_id.empty()) {
							bool still_present = false;
							for (const auto &state : self->device_states_) {
								if (state.id == removed_id) {
									still_present = true;
									break;
								}
							}
							if (!still_present) {
								self->devices_.erase(
									std::remove_if(
										self->devices_.begin(),
										self->devices_.end(),
										[&removed_id](
											const JoypadDeviceInfo &info) {
											return info.id == removed_id;
										}),
									self->devices_.end());
							}
						}
					},
					this);

				IOHIDManagerRegisterInputValueCallback(
					(IOHIDManagerRef)hid_manager_,
					[](void *context, IOReturn, void *, IOHIDValueRef value) {
						auto *self = static_cast<JoypadInputManager *>(context);
						if (!self || !value) {
							return;
						}
						IOHIDElementRef element = IOHIDValueGetElement(value);
						if (!element) {
							return;
						}
						uint32_t usage_page = IOHIDElementGetUsagePage(element);
						uint32_t usage = IOHIDElementGetUsage(element);
						if (usage_page == kHIDPage_Button) {
							CFIndex v = IOHIDValueGetIntegerValue(value);
							if (v == 0) {
								return;
							}
						} else if (usage_page != kHIDPage_GenericDesktop) {
							return;
						}
						IOHIDDeviceRef device = IOHIDElementGetDevice(element);
						if (!device) {
							return;
						}

						std::string device_id;
						std::string device_stable_id;
						std::string device_type_id;
						std::string device_name = "Gamepad";
						{
							std::lock_guard<std::mutex> lock(self->devices_mutex_);
							for (const auto &state : self->device_states_) {
								if (state.hid_device == device) {
									device_id = state.id;
									device_stable_id = state.stable_id;
									device_type_id = state.type_id;
									device_name = state.name;
									break;
								}
							}
						}
						if (device_id.empty()) {
							// Device may have been removed between callback delivery and lookup.
							return;
						}

						JoypadEvent event;
						event.device_id = device_id;
						event.device_stable_id = device_stable_id;
						event.device_type_id = device_type_id;
						event.device_name = device_name;
						if (usage_page == kHIDPage_Button) {
							event.button = (int)usage;
							self->DispatchEvent(event);
							return;
						}

						if (usage != kHIDUsage_GD_X && usage != kHIDUsage_GD_Y &&
						    usage != kHIDUsage_GD_Z && usage != kHIDUsage_GD_Rx &&
						    usage != kHIDUsage_GD_Ry && usage != kHIDUsage_GD_Rz &&
						    usage != kHIDUsage_GD_Slider && usage != kHIDUsage_GD_Dial &&
						    usage != kHIDUsage_GD_Wheel) {
							return;
						}

						double scaled =
							IOHIDValueGetScaledValue(value, kIOHIDValueScaleTypeCalibrated);
						double raw = (double)IOHIDValueGetIntegerValue(value);
						IOHIDElementRef el = IOHIDValueGetElement(value);
						CFIndex min = IOHIDElementGetLogicalMin(el);
						CFIndex max = IOHIDElementGetLogicalMax(el);
						double norm = 0.0;
						if (max > min) {
							norm = (scaled - min) / (double)(max - min) * 2.0 - 1.0;
						}
						norm = std::clamp(norm, -1.0, 1.0);

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
						if (axis_index < 0 || axis_index >= kMaxTrackedAxes) {
							return;
						}

						const int interval_ms = 0;
						std::string key = device_id + ":" + std::to_string(axis_index) +
								  (raw >= 0.0 ? "+" : "-");
						auto now = std::chrono::steady_clock::now();
						auto it = self->axis_last_trigger_.find(key);
						double prev = 0.0;
						bool init_only = false;
						{
							std::lock_guard<std::mutex> lock(self->devices_mutex_);
							for (auto &state : self->device_states_) {
								if (state.hid_device == device) {
									if (axis_index >= 0 && axis_index < 8) {
										if (!state.axis_initialized[axis_index]) {
											state.axis_initialized
												[axis_index] = true;
											state.last_axes[axis_index] =
												raw;
											init_only = true;
										} else {
											prev = state.last_axes
												       [axis_index];
											state.last_axes[axis_index] =
												raw;
										}
									}
									break;
								}
							}
						}
						if (init_only) {
							return;
						}
						const bool hold_active = std::fabs(norm) >=
									 kAxisContinuousHoldThreshold;
						const bool learning_active =
							self->learn_active_.load(std::memory_order_acquire);
						if (raw == prev && (!hold_active || learning_active)) {
							return;
						}
						if (it != self->axis_last_trigger_.end() &&
						    std::chrono::duration_cast<std::chrono::milliseconds>(now -
													  it->second)
								    .count() < interval_ms) {
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

				IOHIDManagerScheduleWithRunLoop((IOHIDManagerRef)hid_manager_, CFRunLoopGetCurrent(),
								kCFRunLoopDefaultMode);
				IOHIDManagerOpen((IOHIDManagerRef)hid_manager_, kIOHIDOptionsTypeNone);
				hid_run_loop_ = CFRunLoopGetCurrent();
			}
		}

		CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
#endif

		for (const auto &event : pending_button_events) {
			DispatchEvent(event);
		}
		for (const auto &event : pending_axis_events) {
			DispatchAxisAbsolute(event);
		}

		[[maybe_unused]] auto now = std::chrono::steady_clock::now();
#if defined(_WIN32)
		if (dinput_notify_hwnd_) {
			MSG msg;
			while (PeekMessageW(&msg, static_cast<HWND>(dinput_notify_hwnd_), 0, 0, PM_REMOVE)) {
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}
		}
		if (device_change_pending_.exchange(false)) {
			RefreshDevices();
		}
#else
		if (now - last_refresh > std::chrono::seconds(4)) {
			RefreshDevices();
			last_refresh = now;
		}
#endif

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
			learn_active_.store(false, std::memory_order_release);
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
			learn_active_.store(false, std::memory_order_release);
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

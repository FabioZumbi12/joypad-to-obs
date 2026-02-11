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

#pragma once

#include "joypad-config.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct JoypadDeviceInfo {
	std::string id;
	std::string name;
};

class JoypadInputManager {
public:
	JoypadInputManager();
	~JoypadInputManager();

	void Start();
	void Stop();

	std::vector<JoypadDeviceInfo> GetDevices() const;
	void RefreshDevices();

	void SetOnButtonPressed(std::function<void(const JoypadEvent &)> handler);
	void SetOnAxisChanged(std::function<void(const JoypadEvent &)> handler);
	int AddOnAxisChanged(std::function<void(const JoypadEvent &)> handler);
	void RemoveOnAxisChanged(int handler_id);
	bool GetAxisRawValue(const std::string &device_id, int axis_index,
			     double &raw_out) const;

	bool BeginLearn(std::function<void(const JoypadEvent &)> handler);
	void CancelLearn();

private:
	struct DeviceState {
		std::string id;
		std::string name;
		uint32_t last_buttons = 0;
		double last_axes[8] = {0};
		bool axis_initialized[8] = {false};
		bool connected = false;
		int winmm_id = -1;
		bool is_xinput = false;
		uint32_t xinput_id = 0;
#ifdef _WIN32
		int axis_min[6] = {0, 0, 0, 0, 0, 0};
		int axis_max[6] = {0, 0, 0, 0, 0, 0};
#endif
#if defined(__linux__)
		int fd = -1;
#elif defined(__APPLE__)
		void *hid_device = nullptr;
#endif
	};

	void PollLoop();
	void DispatchEvent(const JoypadEvent &event);
	void DispatchAxisAbsolute(const JoypadEvent &event);

	std::atomic<bool> running_{false};
	std::thread poll_thread_;

	mutable std::mutex devices_mutex_;
	std::vector<JoypadDeviceInfo> devices_;
	std::vector<DeviceState> device_states_;

	std::mutex handler_mutex_;
	std::function<void(const JoypadEvent &)> on_button_pressed_;
	std::function<void(const JoypadEvent &)> learn_handler_;
	struct AxisHandlerEntry {
		int id = 0;
		std::function<void(const JoypadEvent &)> handler;
	};
	int next_axis_handler_id_ = 1;
	std::vector<AxisHandlerEntry> axis_handlers_;
	std::unordered_map<std::string, std::chrono::steady_clock::time_point>
		axis_last_trigger_;

#if defined(__APPLE__)
	void *hid_manager_ = nullptr;
	void *hid_run_loop_ = nullptr;
#endif
};

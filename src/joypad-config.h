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

#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>

enum class JoypadActionType {
	SwitchScene = 0,
	ToggleSourceVisibility = 1,
	SetSourceVisibility = 2,
	ToggleSourceMute = 3,
	SetSourceMute = 4,
	SetSourceVolume = 5,
	MediaPlayPause = 6,
	MediaRestart = 7,
	MediaStop = 8,
	ToggleFilterEnabled = 9,
	SetFilterEnabled = 10,
	AdjustSourceVolume = 11,
	SetSourceVolumePercent = 12,
};

enum class JoypadInputType {
	Button = 0,
	Axis = 1,
};

enum class JoypadAxisDirection {
	Both = 0,
	Negative = -1,
	Positive = 1,
};

struct JoypadBinding {
	std::string device_id;
	std::string device_name;
	int button = -1;
	JoypadInputType input_type = JoypadInputType::Button;
	int axis_index = -1;
	JoypadAxisDirection axis_direction = JoypadAxisDirection::Positive;
	double axis_threshold = 0.5;
	int axis_interval_ms = 150;
	double axis_min_value = 0.0;
	double axis_max_value = 1024.0;

	JoypadActionType action = JoypadActionType::SwitchScene;

	bool use_current_scene = false;
	std::string scene_name;

	std::string source_name;
	std::string filter_name;

	bool bool_value = false;
	bool allow_above_unity = false;
	double volume_value = 1.0;
	double slider_gamma = 0.6;
	bool enabled = true;
};

struct JoypadEvent {
	std::string device_id;
	std::string device_name;
	int button = -1;
	bool is_axis = false;
	int axis_index = -1;
	double axis_value = 0.0;
	double axis_raw_value = 0.0;
};

class JoypadConfigStore {
public:
	void Load();
	void Save();

	void AddBinding(const JoypadBinding &binding);
	void RemoveBinding(size_t index);
	void UpdateBinding(size_t index, const JoypadBinding &binding);

	std::vector<JoypadBinding> GetBindingsSnapshot() const;
	std::vector<JoypadBinding> FindMatchingBindings(const JoypadEvent &event) const;
	void SetAxisLastRaw(const std::string &key, double raw);
	bool ConsumeAxisLastRaw(const std::string &key, double &raw_out);
	void ClearAxisLastRaw();

private:
	std::vector<JoypadBinding> bindings_;
	mutable std::mutex mutex_;
	mutable std::unordered_map<std::string, bool> axis_active_;
	std::unordered_map<std::string, double> axis_last_raw_;
};

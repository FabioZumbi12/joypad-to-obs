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
#include <atomic>
#include <string>
#include <vector>
#include <unordered_map>
#include <obs.h>
#include <functional>

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
	NextScene = 13,
	PreviousScene = 14,
	ToggleStreaming = 15,
	ToggleRecording = 16,
	ToggleVirtualCam = 17,
	ToggleStudioMode = 18,
	TransitionToProgram = 19,
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

enum class JoypadOsdPosition {
	TopLeft = 0,
	TopCenter = 1,
	TopRight = 2,
	CenterLeft = 3,
	Center = 4,
	CenterRight = 5,
	BottomLeft = 6,
	BottomCenter = 7,
	BottomRight = 8
};

struct JoypadBinding {
	int64_t uid = 0;
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

struct JoypadProfile {
	std::string name;
	std::string comment;
	std::vector<JoypadBinding> bindings;
	obs_hotkey_id hotkey_id = OBS_INVALID_HOTKEY_ID;
};

class JoypadConfigStore {
public:
	using ProfileSwitchCallback = std::function<void(const std::string &)>;
	void SetProfileSwitchCallback(ProfileSwitchCallback callback);

	void Load();
	void Save();
	void Unload();
	bool HasUnsavedChanges() const;
	void DiscardChanges();

	void AddBinding(const JoypadBinding &binding);
	void RemoveBinding(size_t index);
	void UpdateBinding(size_t index, const JoypadBinding &binding);
	void ClearCurrentProfileBindings();

	std::vector<JoypadBinding> GetBindingsSnapshot() const;
	std::vector<JoypadBinding> FindMatchingBindings(const JoypadEvent &event) const;
	void SetAxisLastRaw(const std::string &key, double raw);
	bool ConsumeAxisLastRaw(const std::string &key, double &raw_out);
	void ClearAxisLastRaw();
	void SwitchProfileByHotkey(obs_hotkey_id id);

	// Profile Management
	std::vector<std::string> GetProfileNames() const;
	int GetCurrentProfileIndex() const;
	void SetCurrentProfile(int index);
	void AddProfile(const std::string &name);
	void RenameProfile(int index, const std::string &new_name);
	void SetProfileComment(int index, const std::string &comment);
	std::string GetProfileComment(int index) const;
	void RemoveProfile(int index);
	void DuplicateProfile(int index, const std::string &new_name);
	bool ExportProfile(int index, const std::string &filepath);
	bool ImportProfile(const std::string &filepath);
	std::string GetProfileHotkeyString(int index) const;
	std::string GetLastFilePath() const;
	void SetLastFilePath(const std::string &path);

	bool GetOsdEnabled() const;
	void SetOsdEnabled(bool enabled);
	std::string GetOsdColor() const;
	void SetOsdColor(const std::string &color);
	std::string GetOsdBackgroundColor() const;
	void SetOsdBackgroundColor(const std::string &color);
	int GetOsdFontSize() const;
	void SetOsdFontSize(int size);
	JoypadOsdPosition GetOsdPosition() const;
	void SetOsdPosition(JoypadOsdPosition position);

private:
	std::vector<JoypadProfile> profiles_;
	int current_profile_index_ = 0;
	mutable std::mutex mutex_;
	std::atomic<bool> dirty_{false};
	mutable std::unordered_map<std::string, bool> axis_active_;
	std::unordered_map<std::string, double> axis_last_raw_;
	std::string last_file_path_;
	ProfileSwitchCallback on_profile_switch_;
	bool osd_enabled_ = true;
	std::string osd_color_ = "#ffffff";
	int osd_font_size_ = 24;
	JoypadOsdPosition osd_position_ = JoypadOsdPosition::BottomCenter;
	std::string osd_background_color_ = "rgba(0, 0, 0, 230)";
	void SortAndRegisterHotkeys(std::unique_lock<std::mutex> &lock);
};

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

#include "joypad-config.h"

#include <obs-module.h>
#include <obs-properties.h>
#include <cmath>
#include <algorithm>
#include <plugin-support.h>
#include <util/platform.h>
#include <fstream>
#include <cctype>
#include <obs-frontend-api.h>
#include <map>
#include <util/dstr.h>
#include <cstring>
#include <chrono>

namespace {
const char *kConfigFileName = "joypad-to-obs.json";
constexpr int kOsdPositionMin = (int)JoypadOsdPosition::TopLeft;
constexpr int kOsdPositionMax = (int)JoypadOsdPosition::BottomRight;

std::string to_upper_copy(const std::string &s)
{
	std::string out = s;
	std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::toupper(c); });
	return out;
}

bool contains_upper(const std::string &haystack_upper, const char *needle_upper)
{
	return haystack_upper.find(needle_upper) != std::string::npos;
}

bool is_xbox_like(const std::string &device_id, const std::string &device_type_id, const std::string &device_name)
{
	(void)device_id;
	const std::string type_up = to_upper_copy(device_type_id);
	const std::string name_up = to_upper_copy(device_name);
	if (contains_upper(type_up, "VID_045E")) {
		return true;
	}
	if (contains_upper(name_up, "XBOX")) {
		return true;
	}
	return false;
}

void profile_hotkey_callback(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	if (!pressed)
		return;
	auto *store = static_cast<JoypadConfigStore *>(data);
	store->SwitchProfileByHotkey(id);
	(void)hotkey;
}

void register_profile_hotkey(JoypadConfigStore *store, JoypadProfile &profile)
{
	if (profile.hotkey_id != OBS_INVALID_HOTKEY_ID)
		return;

	std::string name = "JoypadToOBS.Profile.Switch." + profile.name;
	std::string desc = obs_module_text("JoypadToOBS.Hotkey.SwitchProfile");
	size_t pos = desc.find("%1");
	if (pos != std::string::npos) {
		desc.replace(pos, 2, profile.name);
	} else {
		desc += ": " + profile.name;
	}

	profile.hotkey_id = obs_hotkey_register_frontend(name.c_str(), desc.c_str(), profile_hotkey_callback, store);
}

void unregister_profile_hotkey(JoypadProfile &profile)
{
	if (profile.hotkey_id != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(profile.hotkey_id);
		profile.hotkey_id = OBS_INVALID_HOTKEY_ID;
	}
}
} // namespace

static void ensure_config_dir()
{
	char *config_dir = obs_module_config_path("");
	if (!config_dir) {
		return;
	}
	os_mkdirs(config_dir);
	bfree(config_dir);
}

static void load_binding_from_data(JoypadBinding &binding, obs_data_t *data)
{
	binding.uid = obs_data_get_int(data, "uid");
	binding.device_id = obs_data_get_string(data, "device_id");
	binding.device_stable_id = obs_data_get_string(data, "device_stable_id");
	binding.device_type_id = obs_data_get_string(data, "device_type_id");
	if (binding.device_stable_id.empty() && binding.device_id.rfind("winmm:", 0) != 0) {
		binding.device_stable_id = binding.device_id;
	}
	if (binding.device_type_id.empty() && binding.device_id.rfind("winmm:", 0) != 0) {
		binding.device_type_id = binding.device_id;
	}
	binding.device_name = obs_data_get_string(data, "device_name");
	binding.button = (int)obs_data_get_int(data, "button");
	binding.input_type = (JoypadInputType)obs_data_get_int(data, "input_type");
	binding.axis_index = (int)obs_data_get_int(data, "axis_index");
	binding.axis_direction = (JoypadAxisDirection)obs_data_get_int(data, "axis_direction");
	if (binding.axis_direction != JoypadAxisDirection::Negative &&
	    binding.axis_direction != JoypadAxisDirection::Both) {
		binding.axis_direction = JoypadAxisDirection::Positive;
	}
	binding.axis_inverted = obs_data_get_bool(data, "axis_inverted");
	binding.axis_threshold = obs_data_get_double(data, "axis_threshold");
	bool axis_threshold_set = obs_data_get_bool(data, "axis_threshold_set");
	if (!axis_threshold_set) {
		binding.axis_threshold = 0.10;
	}
	binding.axis_min_per_second = obs_data_get_double(data, "axis_min_per_second");
	if (!obs_data_has_user_value(data, "axis_min_per_second")) {
		// Backward compatibility with older configs.
		double legacy_min = obs_data_get_double(data, "axis_activation_min");
		binding.axis_min_per_second = legacy_min > 0.0 ? legacy_min * 10.0 : 2.5;
	}
	binding.axis_max_per_second = obs_data_get_double(data, "axis_max_per_second");
	if (!obs_data_has_user_value(data, "axis_max_per_second")) {
		// Backward compatibility with older configs.
		double legacy_max = obs_data_get_double(data, "axis_activation_max");
		binding.axis_max_per_second = legacy_max > 0.0 ? legacy_max * 10.0 : 20.0;
	}
	binding.axis_threshold = std::clamp(binding.axis_threshold, 0.0, 0.95);
	binding.axis_min_per_second = std::clamp(binding.axis_min_per_second, 1.0, 60.0);
	binding.axis_max_per_second = std::clamp(binding.axis_max_per_second, 1.0, 60.0);
	if (binding.axis_max_per_second < binding.axis_min_per_second) {
		binding.axis_max_per_second = binding.axis_min_per_second;
	}
	binding.axis_interval_ms = (int)obs_data_get_int(data, "axis_interval_ms");
	if (binding.axis_interval_ms <= 0) {
		binding.axis_interval_ms = 150;
	}
	binding.axis_min_value = obs_data_get_double(data, "axis_min_value");
	binding.axis_max_value = obs_data_get_double(data, "axis_max_value");
	if (binding.axis_max_value <= binding.axis_min_value) {
		binding.axis_min_value = 0.0;
		binding.axis_max_value = 1024.0;
	}
	binding.action = (JoypadActionType)obs_data_get_int(data, "action");
	binding.use_current_scene = obs_data_get_bool(data, "use_current_scene");
	binding.scene_name = obs_data_get_string(data, "scene_name");
	binding.source_name = obs_data_get_string(data, "source_name");
	binding.filter_name = obs_data_get_string(data, "filter_name");
	binding.filter_property_name = obs_data_get_string(data, "filter_property_name");
	binding.filter_property_type = (int)obs_data_get_int(data, "filter_property_type");
	binding.filter_property_value = obs_data_get_double(data, "filter_property_value");
	binding.filter_property_min = obs_data_get_double(data, "filter_property_min");
	binding.filter_property_max = obs_data_get_double(data, "filter_property_max");
	if (binding.filter_property_max <= binding.filter_property_min) {
		binding.filter_property_min = 0.0;
		binding.filter_property_max = 1.0;
	}
	binding.filter_property_list_format = (int)obs_data_get_int(data, "filter_property_list_format");
	binding.filter_property_list_string = obs_data_get_string(data, "filter_property_list_string");
	binding.filter_property_list_int = obs_data_get_int(data, "filter_property_list_int");
	binding.filter_property_list_float = obs_data_get_double(data, "filter_property_list_float");
	binding.source_transform_op = (JoypadSourceTransformOp)obs_data_get_int(data, "source_transform_op");
	binding.bool_value = obs_data_get_bool(data, "bool_value");
	binding.allow_above_unity = obs_data_get_bool(data, "allow_above_unity");
	if (!binding.allow_above_unity) {
		binding.allow_above_unity = obs_data_get_bool(data, "allow_negative_volume");
	}
	binding.volume_value = obs_data_get_double(data, "volume_value");
	binding.slider_gamma = obs_data_get_double(data, "slider_gamma");
	if (binding.action == JoypadActionType::SetSourceVolumePercent) {
		if (!binding.axis_inverted && binding.axis_direction == JoypadAxisDirection::Negative) {
			binding.axis_inverted = true;
		}
		binding.axis_direction = JoypadAxisDirection::Both;
		if (binding.slider_gamma <= 0.0) {
			binding.slider_gamma = binding.volume_value > 0.0 ? binding.volume_value : 0.6;
		}
		binding.volume_value = 0.0;
	}
	binding.enabled = true;
	if (obs_data_has_user_value(data, "enabled")) {
		binding.enabled = obs_data_get_bool(data, "enabled");
	}
}

static void save_binding_to_data(const JoypadBinding &binding, obs_data_t *data)
{
	obs_data_set_int(data, "uid", binding.uid);
	obs_data_set_string(data, "device_id", binding.device_id.c_str());
	obs_data_set_string(data, "device_stable_id", binding.device_stable_id.c_str());
	obs_data_set_string(data, "device_type_id", binding.device_type_id.c_str());
	obs_data_set_string(data, "device_name", binding.device_name.c_str());
	obs_data_set_int(data, "button", binding.button);
	obs_data_set_int(data, "input_type", (int)binding.input_type);
	if (binding.input_type == JoypadInputType::Axis) {
		obs_data_set_int(data, "axis_index", binding.axis_index);
		obs_data_set_int(data, "axis_direction", (int)binding.axis_direction);
		obs_data_set_bool(data, "axis_inverted", binding.axis_inverted);
		obs_data_set_double(data, "axis_threshold", binding.axis_threshold);
		obs_data_set_bool(data, "axis_threshold_set", true);
		obs_data_set_double(data, "axis_min_per_second", binding.axis_min_per_second);
		obs_data_set_double(data, "axis_max_per_second", binding.axis_max_per_second);
		obs_data_set_int(data, "axis_interval_ms", binding.axis_interval_ms);
		obs_data_set_double(data, "axis_min_value", binding.axis_min_value);
		obs_data_set_double(data, "axis_max_value", binding.axis_max_value);
	}
	obs_data_set_int(data, "action", (int)binding.action);
	obs_data_set_bool(data, "enabled", binding.enabled);

	switch (binding.action) {
	case JoypadActionType::SwitchScene:
		obs_data_set_string(data, "scene_name", binding.scene_name.c_str());
		break;
	case JoypadActionType::ToggleSourceVisibility:
	case JoypadActionType::SetSourceVisibility:
		obs_data_set_bool(data, "use_current_scene", binding.use_current_scene);
		if (!binding.use_current_scene) {
			obs_data_set_string(data, "scene_name", binding.scene_name.c_str());
		}
		obs_data_set_string(data, "source_name", binding.source_name.c_str());
		if (binding.action == JoypadActionType::SetSourceVisibility) {
			obs_data_set_bool(data, "bool_value", binding.bool_value);
		}
		break;
	case JoypadActionType::ToggleSourceMute:
	case JoypadActionType::SetSourceMute:
		obs_data_set_string(data, "source_name", binding.source_name.c_str());
		if (binding.action == JoypadActionType::SetSourceMute) {
			obs_data_set_bool(data, "bool_value", binding.bool_value);
		}
		break;
	case JoypadActionType::SetSourceVolume:
	case JoypadActionType::AdjustSourceVolume:
		obs_data_set_string(data, "source_name", binding.source_name.c_str());
		obs_data_set_double(data, "volume_value", binding.volume_value);
		obs_data_set_bool(data, "allow_above_unity", binding.allow_above_unity);
		break;
	case JoypadActionType::SetSourceVolumePercent:
		obs_data_set_string(data, "source_name", binding.source_name.c_str());
		obs_data_set_double(data, "slider_gamma", binding.slider_gamma);
		break;
	case JoypadActionType::MediaPlayPause:
	case JoypadActionType::MediaRestart:
	case JoypadActionType::MediaStop:
		obs_data_set_string(data, "source_name", binding.source_name.c_str());
		break;
	case JoypadActionType::ToggleFilterEnabled:
	case JoypadActionType::SetFilterEnabled:
		obs_data_set_string(data, "source_name", binding.source_name.c_str());
		obs_data_set_string(data, "filter_name", binding.filter_name.c_str());
		if (binding.action == JoypadActionType::SetFilterEnabled) {
			obs_data_set_bool(data, "bool_value", binding.bool_value);
		}
		break;
	case JoypadActionType::SetFilterProperty:
		obs_data_set_string(data, "source_name", binding.source_name.c_str());
		obs_data_set_string(data, "filter_name", binding.filter_name.c_str());
		obs_data_set_string(data, "filter_property_name", binding.filter_property_name.c_str());
		obs_data_set_int(data, "filter_property_type", binding.filter_property_type);
		obs_data_set_double(data, "filter_property_value", binding.filter_property_value);
		obs_data_set_double(data, "filter_property_min", binding.filter_property_min);
		obs_data_set_double(data, "filter_property_max", binding.filter_property_max);
		obs_data_set_int(data, "filter_property_list_format", binding.filter_property_list_format);
		obs_data_set_string(data, "filter_property_list_string", binding.filter_property_list_string.c_str());
		obs_data_set_int(data, "filter_property_list_int", binding.filter_property_list_int);
		obs_data_set_double(data, "filter_property_list_float", binding.filter_property_list_float);
		obs_data_set_bool(data, "bool_value", binding.bool_value);
		break;
	case JoypadActionType::AdjustFilterProperty:
		obs_data_set_string(data, "source_name", binding.source_name.c_str());
		obs_data_set_string(data, "filter_name", binding.filter_name.c_str());
		obs_data_set_string(data, "filter_property_name", binding.filter_property_name.c_str());
		obs_data_set_int(data, "filter_property_type", binding.filter_property_type);
		obs_data_set_double(data, "filter_property_min", binding.filter_property_min);
		obs_data_set_double(data, "filter_property_max", binding.filter_property_max);
		obs_data_set_double(data, "volume_value", binding.volume_value);
		break;
	case JoypadActionType::SourceTransform:
		obs_data_set_bool(data, "use_current_scene", binding.use_current_scene);
		if (!binding.use_current_scene) {
			obs_data_set_string(data, "scene_name", binding.scene_name.c_str());
		}
		obs_data_set_string(data, "source_name", binding.source_name.c_str());
		obs_data_set_int(data, "source_transform_op", (int)binding.source_transform_op);
		break;
	default:
		break;
	}
}

void JoypadConfigStore::Load()
{
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto &profile : profiles_) {
		unregister_profile_hotkey(profile);
	}

	profiles_.clear();
	current_profile_index_ = 0;
	axis_active_.clear();
	dirty_ = false;

	ensure_config_dir();

	char *config_path = obs_module_config_path(kConfigFileName);
	if (!config_path) {
		return;
	}

	obs_data_t *data = obs_data_create_from_json_file_safe(config_path, "backup");
	bfree(config_path);

	if (!data) {
		profiles_.push_back({"Default", {}});
		current_profile_index_ = 0;
		register_profile_hotkey(this, profiles_[0]);

		if (profiles_[0].hotkey_id != OBS_INVALID_HOTKEY_ID) {
			obs_data_t *hk_item = obs_data_create();
			obs_data_set_bool(hk_item, "control", true);
			obs_data_set_string(hk_item, "key", "OBS_KEY_NUM1");

			obs_data_array_t *hk_array = obs_data_array_create();
			obs_data_array_push_back(hk_array, hk_item);

			obs_hotkey_load(profiles_[0].hotkey_id, hk_array);

			obs_data_release(hk_item);
			obs_data_array_release(hk_array);
		}

#ifdef _WIN32
		JoypadProfile xbox_profile;
		xbox_profile.name = "Xbox One Controller";
		xbox_profile.comment =
			"!!! EXPAND THIS TEXTBOX !!!\n\nThis template is an example to use with XBOX ONE Controller and OBS Studio Mode.\nThis is the mapped buttons:\n\nB - Enable/Disable OBS Studio Mode\nLB - Select previous Preview scene\nRB - Select next Preview scene\nA - Transition scene from Preview to Program\nControl + Numpad2 - Set this profile active (if other profiles setup)";

		JoypadBinding b1;
		b1.uid = 1;
		b1.device_id = "xinput:0";
		b1.device_type_id = "VID_045E&PID_XINPUT";
		b1.device_name = "Xbox Controller 1";
		b1.button = 1;
		b1.input_type = JoypadInputType::Button;
		b1.action = JoypadActionType::TransitionToProgram;
		b1.enabled = true;
		xbox_profile.bindings.push_back(b1);

		JoypadBinding b2;
		b2.uid = 2;
		b2.device_id = "xinput:0";
		b2.device_type_id = "VID_045E&PID_XINPUT";
		b2.device_name = "Xbox Controller 1";
		b2.button = 6;
		b2.input_type = JoypadInputType::Button;
		b2.action = JoypadActionType::NextScene;
		b2.enabled = true;
		xbox_profile.bindings.push_back(b2);

		JoypadBinding b3;
		b3.uid = 3;
		b3.device_id = "xinput:0";
		b3.device_type_id = "VID_045E&PID_XINPUT";
		b3.device_name = "Xbox Controller 1";
		b3.button = 5;
		b3.input_type = JoypadInputType::Button;
		b3.action = JoypadActionType::PreviousScene;
		b3.enabled = true;
		xbox_profile.bindings.push_back(b3);

		JoypadBinding b4;
		b4.uid = 4;
		b4.device_id = "xinput:0";
		b4.device_type_id = "VID_045E&PID_XINPUT";
		b4.device_name = "Xbox Controller 1";
		b4.button = 2;
		b4.input_type = JoypadInputType::Button;
		b4.action = JoypadActionType::ToggleStudioMode;
		b4.enabled = true;
		xbox_profile.bindings.push_back(b4);

		register_profile_hotkey(this, xbox_profile);

		if (xbox_profile.hotkey_id != OBS_INVALID_HOTKEY_ID) {
			obs_data_t *hk_item = obs_data_create();
			obs_data_set_bool(hk_item, "control", true);
			obs_data_set_string(hk_item, "key", "OBS_KEY_NUM2");

			obs_data_array_t *hk_array = obs_data_array_create();
			obs_data_array_push_back(hk_array, hk_item);

			obs_hotkey_load(xbox_profile.hotkey_id, hk_array);

			obs_data_release(hk_item);
			obs_data_array_release(hk_array);
		}

		profiles_.push_back(xbox_profile);
#endif
		return;
	}

	obs_data_array_t *profiles_array = obs_data_get_array(data, "profiles");
	if (profiles_array) {
		size_t count = obs_data_array_count(profiles_array);
		for (size_t i = 0; i < count; ++i) {
			obs_data_t *p_item = obs_data_array_item(profiles_array, i);
			JoypadProfile profile;
			profile.name = obs_data_get_string(p_item, "name");
			profile.comment = obs_data_get_string(p_item, "comment");
			if (profile.name.empty()) {
				obs_data_release(p_item);
				continue;
			}

			obs_data_array_t *bindings_array = obs_data_get_array(p_item, "bindings");
			if (bindings_array) {
				size_t b_count = obs_data_array_count(bindings_array);
				for (size_t j = 0; j < b_count; ++j) {
					obs_data_t *b_item = obs_data_array_item(bindings_array, j);
					JoypadBinding binding;
					load_binding_from_data(binding, b_item);
					profile.bindings.push_back(binding);
					obs_data_release(b_item);
				}
				obs_data_array_release(bindings_array);
			}

			register_profile_hotkey(this, profile);
			obs_data_array_t *hotkey_data = obs_data_get_array(p_item, "hotkey_data");
			if (hotkey_data) {
				size_t count = obs_data_array_count(hotkey_data);
				if (profile.hotkey_id != OBS_INVALID_HOTKEY_ID) {
					obs_hotkey_load(profile.hotkey_id, hotkey_data);
					obs_log(LOG_INFO, "Loaded %d hotkey bindings for profile '%s'", (int)count,
						profile.name.c_str());
				}
				obs_data_array_release(hotkey_data);
			}

			profiles_.push_back(profile);
			obs_data_release(p_item);
		}
		obs_data_array_release(profiles_array);
		current_profile_index_ = (int)obs_data_get_int(data, "current_profile_index");
	} else {
		// Legacy migration or new file
		JoypadProfile default_profile;
		default_profile.name = "Default";
		obs_data_array_t *bindings_array = obs_data_get_array(data, "bindings");
		if (bindings_array) {
			size_t count = obs_data_array_count(bindings_array);
			for (size_t i = 0; i < count; ++i) {
				obs_data_t *item = obs_data_array_item(bindings_array, i);
				JoypadBinding binding;
				load_binding_from_data(binding, item);
				default_profile.bindings.push_back(binding);
				obs_data_release(item);
			}
			obs_data_array_release(bindings_array);
		}
		profiles_.push_back(default_profile);
		current_profile_index_ = 0;
	}

	osd_enabled_ = true;
	if (obs_data_has_user_value(data, "osd_enabled")) {
		osd_enabled_ = obs_data_get_bool(data, "osd_enabled");
	}
	const char *color = obs_data_get_string(data, "osd_color");
	osd_color_ = (color && *color) ? color : "#ffffff";
	osd_font_size_ = (int)obs_data_get_int(data, "osd_font_size");
	if (osd_font_size_ <= 0)
		osd_font_size_ = 24;
	int osd_position = (int)obs_data_get_int(data, "osd_position");
	if (osd_position < kOsdPositionMin || osd_position > kOsdPositionMax) {
		osd_position = (int)JoypadOsdPosition::BottomCenter;
	}
	osd_position_ = (JoypadOsdPosition)osd_position;
	const char *bg_color = obs_data_get_string(data, "osd_background_color");
	osd_background_color_ = (bg_color && *bg_color) ? bg_color : "rgba(0, 0, 0, 230)";

	for (auto &profile : profiles_) {
		int64_t max_uid = 0;
		for (const auto &b : profile.bindings) {
			if (b.uid > max_uid)
				max_uid = b.uid;
		}
		int64_t local_gen = max_uid + 1;
		for (auto &binding : profile.bindings) {
			if (binding.uid == 0) {
				binding.uid = local_gen++;
			}
		}
	}

	if (profiles_.empty()) {
		profiles_.push_back({"Default", {}});
	}

	for (auto &profile : profiles_) {
		register_profile_hotkey(this, profile);
	}

	obs_data_release(data);
}

bool JoypadConfigStore::HasUnsavedChanges() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return dirty_;
}

void JoypadConfigStore::DiscardChanges()
{
	Load();
}

void JoypadConfigStore::Unload()
{
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto &profile : profiles_) {
		unregister_profile_hotkey(profile);
	}
	profiles_.clear();
}

void JoypadConfigStore::Save()
{
	std::lock_guard<std::mutex> lock(mutex_);

	ensure_config_dir();

	char *config_path = obs_module_config_path(kConfigFileName);
	if (!config_path) {
		return;
	}

	obs_data_t *data = obs_data_create();

	obs_data_array_t *profiles_array = obs_data_array_create();
	for (const auto &profile : profiles_) {
		obs_data_t *p_item = obs_data_create();
		obs_data_set_string(p_item, "name", profile.name.c_str());
		obs_data_set_string(p_item, "comment", profile.comment.c_str());

		obs_data_array_t *bindings_array = obs_data_array_create();
		for (const auto &binding : profile.bindings) {
			obs_data_t *b_item = obs_data_create();
			save_binding_to_data(binding, b_item);
			obs_data_array_push_back(bindings_array, b_item);
			obs_data_release(b_item);
		}
		obs_data_set_array(p_item, "bindings", bindings_array);
		obs_data_array_release(bindings_array);

		if (profile.hotkey_id != OBS_INVALID_HOTKEY_ID) {
			obs_data_array_t *hotkey_data = obs_hotkey_save(profile.hotkey_id);
			if (hotkey_data) {
				obs_data_set_array(p_item, "hotkey_data", hotkey_data);
				obs_data_array_release(hotkey_data);
			}
		}

		obs_data_array_push_back(profiles_array, p_item);
		obs_data_release(p_item);
	}
	obs_data_set_array(data, "profiles", profiles_array);
	obs_data_array_release(profiles_array);

	obs_data_set_int(data, "current_profile_index", current_profile_index_);

	obs_data_set_bool(data, "osd_enabled", osd_enabled_);
	obs_data_set_string(data, "osd_color", osd_color_.c_str());
	obs_data_set_string(data, "osd_background_color", osd_background_color_.c_str());
	obs_data_set_int(data, "osd_font_size", osd_font_size_);
	obs_data_set_int(data, "osd_position", (int)osd_position_);

	if (!obs_data_save_json(data, config_path)) {
		obs_log(LOG_WARNING, "Nao foi possivel salvar %s", config_path);
	}
	dirty_ = false;

	obs_data_release(data);
	bfree(config_path);
}

void JoypadConfigStore::SetProfileSwitchCallback(ProfileSwitchCallback callback)
{
	std::lock_guard<std::mutex> lock(mutex_);
	on_profile_switch_ = std::move(callback);
}

void JoypadConfigStore::SwitchProfileByHotkey(obs_hotkey_id id)
{
	bool changed = false;
	std::string name;
	ProfileSwitchCallback callback;

	{
		std::lock_guard<std::mutex> lock(mutex_);
		for (size_t i = 0; i < profiles_.size(); ++i) {
			if (profiles_[i].hotkey_id == id) {
				if ((int)i != current_profile_index_) {
					current_profile_index_ = (int)i;
					name = profiles_[i].name;
					changed = true;
					dirty_ = true;
				}
				break;
			}
		}
		if (changed) {
			callback = on_profile_switch_;
		}
	}
	if (changed) {
		if (callback) {
			callback(name);
		}
	}
}

void JoypadConfigStore::SortAndRegisterHotkeys(std::unique_lock<std::mutex> &lock)
{
	std::string current_name;
	if (current_profile_index_ >= 0 && current_profile_index_ < (int)profiles_.size()) {
		current_name = profiles_[current_profile_index_].name;
	}

	struct TempProfile {
		JoypadProfile profile;
		obs_data_array_t *hotkey_data = nullptr;
	};
	std::vector<TempProfile> temp_list;
	temp_list.reserve(profiles_.size());

	// Move profiles to temp list and clear main list to avoid access during unlock
	for (auto &profile : profiles_) {
		TempProfile tp;
		tp.profile = std::move(profile);
		temp_list.push_back(std::move(tp));
	}
	profiles_.clear();

	// Unlock to perform OBS API calls safely
	lock.unlock();

	for (auto &tp : temp_list) {
		if (tp.profile.hotkey_id != OBS_INVALID_HOTKEY_ID) {
			tp.hotkey_data = obs_hotkey_save(tp.profile.hotkey_id);
			obs_hotkey_unregister(tp.profile.hotkey_id);
			tp.profile.hotkey_id = OBS_INVALID_HOTKEY_ID;
		}
	}

	std::sort(temp_list.begin(), temp_list.end(), [](const TempProfile &a, const TempProfile &b) {
		std::string sa = a.profile.name;
		std::string sb = b.profile.name;
		std::transform(sa.begin(), sa.end(), sa.begin(), ::tolower);
		std::transform(sb.begin(), sb.end(), sb.begin(), ::tolower);
		return sa < sb;
	});

	for (auto &tp : temp_list) {
		register_profile_hotkey(this, tp.profile);
		if (tp.hotkey_data) {
			if (tp.profile.hotkey_id != OBS_INVALID_HOTKEY_ID) {
				obs_hotkey_load(tp.profile.hotkey_id, tp.hotkey_data);
			}
			obs_data_array_release(tp.hotkey_data);
			tp.hotkey_data = nullptr;
		}
	}

	// Re-lock to update state
	lock.lock();

	// Re-check current name in case it changed (though unlikely with cleared profiles)
	if (current_profile_index_ >= 0 && current_profile_index_ < (int)profiles_.size()) {
		current_name = profiles_[current_profile_index_].name;
	}

	current_profile_index_ = 0;
	for (size_t i = 0; i < temp_list.size(); ++i) {
		auto &tp = temp_list[i];
		if (tp.profile.name == current_name) {
			current_profile_index_ = (int)i;
		}
		profiles_.push_back(std::move(tp.profile));
	}
}

void JoypadConfigStore::AddBinding(const JoypadBinding &binding)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (current_profile_index_ >= 0 && current_profile_index_ < (int)profiles_.size()) {
			int64_t max_uid = 0;
			for (const auto &b : profiles_[current_profile_index_].bindings) {
				if (b.uid > max_uid)
					max_uid = b.uid;
			}
			JoypadBinding b = binding;
			b.uid = max_uid + 1;
			profiles_[current_profile_index_].bindings.push_back(b);
		}
	}
	dirty_ = true;
}

JoypadOsdPosition JoypadConfigStore::GetOsdPosition() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return osd_position_;
}

void JoypadConfigStore::SetOsdPosition(JoypadOsdPosition position)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		osd_position_ = position;
	}
	dirty_ = true;
}

bool JoypadConfigStore::GetOsdEnabled() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return osd_enabled_;
}

void JoypadConfigStore::SetOsdEnabled(bool enabled)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		osd_enabled_ = enabled;
	}
	dirty_ = true;
}

std::string JoypadConfigStore::GetOsdColor() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return osd_color_;
}

void JoypadConfigStore::SetOsdColor(const std::string &color)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		osd_color_ = color.empty() ? "#ffffff" : color;
	}
	dirty_ = true;
}

std::string JoypadConfigStore::GetOsdBackgroundColor() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return osd_background_color_;
}

void JoypadConfigStore::SetOsdBackgroundColor(const std::string &color)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		osd_background_color_ = color.empty() ? "rgba(0, 0, 0, 230)" : color;
	}
	dirty_ = true;
}

int JoypadConfigStore::GetOsdFontSize() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return osd_font_size_;
}

void JoypadConfigStore::SetOsdFontSize(int size)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		osd_font_size_ = size;
	}
	dirty_ = true;
}

void JoypadConfigStore::RemoveBinding(size_t index)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (current_profile_index_ >= 0 && current_profile_index_ < (int)profiles_.size()) {
			auto &bindings = profiles_[current_profile_index_].bindings;
			if (index < bindings.size())
				bindings.erase(bindings.begin() + (ptrdiff_t)index);
		}
	}
	dirty_ = true;
}

void JoypadConfigStore::UpdateBinding(size_t index, const JoypadBinding &binding)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (current_profile_index_ >= 0 && current_profile_index_ < (int)profiles_.size()) {
			auto &bindings = profiles_[current_profile_index_].bindings;
			if (index < bindings.size())
				bindings[index] = binding;
		}
	}
	dirty_ = true;
}

void JoypadConfigStore::ClearCurrentProfileBindings()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (current_profile_index_ >= 0 && current_profile_index_ < (int)profiles_.size()) {
			profiles_[current_profile_index_].bindings.clear();
		}
	}
	dirty_ = true;
}

std::vector<JoypadBinding> JoypadConfigStore::GetBindingsSnapshot() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (current_profile_index_ >= 0 && current_profile_index_ < (int)profiles_.size()) {
		return profiles_[current_profile_index_].bindings;
	}
	return {};
}

std::vector<JoypadBinding> JoypadConfigStore::FindMatchingBindings(const JoypadEvent &event) const
{
	std::vector<JoypadBinding> matches;
	const auto now = std::chrono::steady_clock::now();
	std::lock_guard<std::mutex> lock(mutex_);
	if (current_profile_index_ < 0 || current_profile_index_ >= (int)profiles_.size()) {
		return matches;
	}
	const auto &current_bindings = profiles_[current_profile_index_].bindings;
	for (const auto &binding_ref : current_bindings) {
		JoypadBinding binding = binding_ref;
		if (!binding.enabled) {
			continue;
		}
		if (binding.input_type == JoypadInputType::Axis) {
			if (!event.is_axis || binding.axis_index != event.axis_index) {
				continue;
			}
			const bool is_percent_axis = (binding.action == JoypadActionType::SetSourceVolumePercent);
			const bool is_filter_numeric_axis = (binding.action == JoypadActionType::SetFilterProperty) &&
							    (binding.filter_property_type == OBS_PROPERTY_INT ||
							     binding.filter_property_type == OBS_PROPERTY_FLOAT);
			if (binding.action == JoypadActionType::SetSourceVolumePercent) {
				// Map axis min..max to 0..100%
				double minv = binding.axis_min_value;
				double maxv = binding.axis_max_value;
				if (maxv <= minv) {
					minv = 0.0;
					maxv = 1024.0;
				}
				const double raw = event.axis_raw_value;
				double percent = ((raw - minv) / (maxv - minv)) * 100.0;
				if (binding.axis_inverted || binding.axis_direction == JoypadAxisDirection::Negative) {
					percent = 100.0 - percent;
				}
				if (percent < 0.0) {
					percent = 0.0;
				}
				if (percent > 100.0) {
					percent = 100.0;
				}
				double base = percent / 100.0;
				base = std::clamp(base, 0.0, 1.0);
				double gamma = binding.slider_gamma > 0.0 ? binding.slider_gamma : 0.6;
				gamma = std::clamp(gamma, 0.1, 50.0);
				double curved = std::pow(base, gamma);
				binding.volume_value = std::clamp(curved * 100.0, 0.0, 100.0);
			} else if (is_filter_numeric_axis) {
				double minv = binding.axis_min_value;
				double maxv = binding.axis_max_value;
				if (maxv <= minv) {
					minv = 0.0;
					maxv = 1024.0;
				}
				double normalized = (event.axis_raw_value - minv) / (maxv - minv);
				normalized = std::clamp(normalized, 0.0, 1.0);
				if (binding.axis_inverted || binding.axis_direction == JoypadAxisDirection::Negative) {
					normalized = 1.0 - normalized;
				}
				double target_min = binding.filter_property_min;
				double target_max = binding.filter_property_max;
				if (target_max <= target_min) {
					target_min = 0.0;
					target_max = 1.0;
				}
				binding.filter_property_value = target_min + normalized * (target_max - target_min);
			}
			double value = event.axis_value;
			if (binding.axis_inverted) {
				value = -value;
			}
			const double abs_value = std::fabs(value);
			if (!is_percent_axis && !is_filter_numeric_axis &&
			    binding.axis_direction != JoypadAxisDirection::Both) {
				int dir = value >= 0.0 ? 1 : -1;
				if (dir != (int)binding.axis_direction) {
					continue;
				}
			}

			const double threshold_on = std::clamp(binding.axis_threshold, 0.0, 0.95);
			const double threshold_off = threshold_on * 0.4;
			std::string axis_key = event.device_id + ":" + std::to_string(binding.axis_index) + ":" +
					       std::to_string((int)binding.axis_direction) + ":" +
					       (binding.axis_inverted ? "1" : "0");
			if (!is_percent_axis && !is_filter_numeric_axis) {
				bool active = axis_active_[axis_key];
				if (!active) {
					if (abs_value < threshold_on) {
						continue;
					}
					axis_active_[axis_key] = true;
				} else {
					if (abs_value < threshold_off) {
						axis_active_[axis_key] = false;
						continue;
					}
				}
			}
			if (binding.action == JoypadActionType::AdjustSourceVolume ||
			    binding.action == JoypadActionType::AdjustFilterProperty) {
				double sign = value >= 0.0 ? 1.0 : -1.0;
				binding.volume_value = std::fabs(binding.volume_value) * sign;
			}
			const bool is_continuous_axis_action =
				(binding.action == JoypadActionType::SetSourceVolumePercent) || is_filter_numeric_axis;
			if (!is_continuous_axis_action) {
				const double min_rate = std::clamp(binding.axis_min_per_second, 1.0, 60.0);
				const double max_rate = std::clamp(binding.axis_max_per_second, min_rate, 60.0);
				double intensity =
					std::clamp((abs_value - threshold_on) / (1.0 - threshold_on), 0.0, 1.0);
				double rate = min_rate + (max_rate - min_rate) * intensity;
				int dynamic_interval_ms = (int)std::round(1000.0 / std::max(rate, 0.001));
				dynamic_interval_ms = std::max(dynamic_interval_ms, 1);
				std::string interval_key;
				if (binding.uid > 0) {
					interval_key = std::to_string((long long)binding.uid);
				} else {
					interval_key = event.device_id + ":" + std::to_string(binding.axis_index) +
						       ":" + std::to_string((int)binding.action) + ":" +
						       binding.source_name + ":" + binding.filter_name;
				}
				auto it_last = axis_last_dispatch_.find(interval_key);
				if (it_last != axis_last_dispatch_.end()) {
					const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
								     now - it_last->second)
								     .count();
					if (elapsed < dynamic_interval_ms) {
						continue;
					}
				}
				axis_last_dispatch_[interval_key] = now;
			}
		} else {
			if (event.is_axis || binding.button != event.button) {
				continue;
			}
		}
		bool device_match = binding.device_id.empty() || binding.device_id == event.device_id;
		if (!device_match && !binding.device_stable_id.empty() &&
		    binding.device_stable_id == event.device_stable_id) {
			device_match = true;
		}
		if (!device_match && !binding.device_type_id.empty() &&
		    binding.device_type_id == event.device_type_id) {
			device_match = true;
		}
		if (!device_match) {
			const bool binding_xbox =
				is_xbox_like(binding.device_id, binding.device_type_id, binding.device_name);
			const bool event_xbox = is_xbox_like(event.device_id, event.device_type_id, event.device_name);
			if (binding_xbox && event_xbox) {
				device_match = true;
			}
		}
		if (!device_match) {
			continue;
		}
		matches.push_back(binding);
	}
	return matches;
}

std::vector<std::string> JoypadConfigStore::GetProfileNames() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<std::string> names;
	for (const auto &p : profiles_) {
		names.push_back(p.name);
	}
	return names;
}

int JoypadConfigStore::GetCurrentProfileIndex() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return current_profile_index_;
}

void JoypadConfigStore::SetCurrentProfile(int index)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (index >= 0 && index < (int)profiles_.size()) {
			current_profile_index_ = index;
		}
	}
	dirty_ = true;
}

void JoypadConfigStore::AddProfile(const std::string &name)
{
	{
		std::unique_lock<std::mutex> lock(mutex_);
		JoypadProfile new_profile = {name, {}};
		profiles_.push_back(new_profile);
		current_profile_index_ = (int)profiles_.size() - 1;
		SortAndRegisterHotkeys(lock);
	}
	dirty_ = true;
}

void JoypadConfigStore::RenameProfile(int index, const std::string &new_name)
{
	{
		std::unique_lock<std::mutex> lock(mutex_);
		if (index >= 0 && index < (int)profiles_.size()) {
			profiles_[index].name = new_name;
			SortAndRegisterHotkeys(lock);
		}
	}
	dirty_ = true;
}

void JoypadConfigStore::SetProfileComment(int index, const std::string &comment)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (index >= 0 && index < (int)profiles_.size()) {
			profiles_[index].comment = comment;
		}
	}
	dirty_ = true;
}

std::string JoypadConfigStore::GetProfileComment(int index) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (index >= 0 && index < (int)profiles_.size()) {
		return profiles_[index].comment;
	}
	return "";
}

void JoypadConfigStore::RemoveProfile(int index)
{
	{
		std::unique_lock<std::mutex> lock(mutex_);
		if (profiles_.size() <= 1) {
			return; // Cannot remove last profile
		}
		if (index >= 0 && index < (int)profiles_.size()) {
			obs_hotkey_id hid = profiles_[index].hotkey_id;
			profiles_[index].hotkey_id = OBS_INVALID_HOTKEY_ID;

			lock.unlock();
			if (hid != OBS_INVALID_HOTKEY_ID) {
				obs_hotkey_unregister(hid);
			}
			lock.lock();

			if (index < (int)profiles_.size()) {
				profiles_.erase(profiles_.begin() + index);
				if (index < current_profile_index_) {
					current_profile_index_--;
				}
				if (current_profile_index_ >= (int)profiles_.size()) {
					current_profile_index_ = (int)profiles_.size() - 1;
				}
			}
		}
	}
	dirty_ = true;
}

void JoypadConfigStore::DuplicateProfile(int index, const std::string &new_name)
{
	{
		std::unique_lock<std::mutex> lock(mutex_);
		if (index >= 0 && index < (int)profiles_.size()) {
			JoypadProfile new_profile = profiles_[index];
			new_profile.name = new_name;
			// Comment is copied automatically
			new_profile.hotkey_id = OBS_INVALID_HOTKEY_ID;
			profiles_.push_back(new_profile);
			current_profile_index_ = (int)profiles_.size() - 1;
			SortAndRegisterHotkeys(lock);
		}
	}
	dirty_ = true;
}

bool JoypadConfigStore::ExportProfile(int index, const std::string &filepath)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (index < 0 || index >= (int)profiles_.size()) {
		return false;
	}
	const auto &profile = profiles_[index];

	obs_data_t *root = obs_data_create();
	obs_data_set_string(root, "profile_name", profile.name.c_str());
	obs_data_set_string(root, "profile_comment", profile.comment.c_str());
	obs_data_array_t *arr = obs_data_array_create();
	for (const auto &b : profile.bindings) {
		obs_data_t *item = obs_data_create();
		save_binding_to_data(b, item);
		// Keep exported profiles portable and less device-specific.
		// Resolution will still work via device_id/device_type_id matching.
		obs_data_erase(item, "device_stable_id");
		obs_data_array_push_back(arr, item);
		obs_data_release(item);
	}
	obs_data_set_array(root, "bindings", arr);
	obs_data_array_release(arr);

	if (profile.hotkey_id != OBS_INVALID_HOTKEY_ID) {
		obs_data_array_t *hotkey_data = obs_hotkey_save(profile.hotkey_id);
		if (hotkey_data) {
			obs_data_set_array(root, "hotkey_data", hotkey_data);
			obs_data_array_release(hotkey_data);
		}
	}

	bool success = obs_data_save_json(root, filepath.c_str());
	obs_data_release(root);
	return success;
}

bool JoypadConfigStore::ImportProfile(const std::string &filepath)
{
	obs_data_t *root = obs_data_create_from_json_file_safe(filepath.c_str(), "backup");
	if (!root) {
		return false;
	}

	JoypadProfile profile;
	profile.name = obs_data_get_string(root, "profile_name");
	if (profile.name.empty()) {
		profile.name = "Imported";
	}
	profile.comment = obs_data_get_string(root, "profile_comment");

	obs_data_array_t *hotkey_data = obs_data_get_array(root, "hotkey_data");

	obs_data_array_t *arr = obs_data_get_array(root, "bindings");
	if (arr) {
		size_t count = obs_data_array_count(arr);
		for (size_t i = 0; i < count; ++i) {
			obs_data_t *item = obs_data_array_item(arr, i);
			JoypadBinding b;
			load_binding_from_data(b, item);
			profile.bindings.push_back(b);
			obs_data_release(item);
		}
		obs_data_array_release(arr);
	}
	obs_data_release(root);

	{
		std::unique_lock<std::mutex> lock(mutex_);
		std::string base_name = profile.name;
		int counter = 1;
		bool collision = true;
		while (collision) {
			collision = false;
			for (const auto &p : profiles_) {
				if (p.name.size() == profile.name.size() &&
				    std::equal(p.name.begin(), p.name.end(), profile.name.begin(), [](char a, char b) {
					    return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
				    })) {
					collision = true;
					break;
				}
			}
			if (collision) {
				profile.name = base_name + " (" + std::to_string(counter++) + ")";
			}
		}

		int64_t max_uid = 0;
		for (const auto &b : profile.bindings) {
			if (b.uid > max_uid)
				max_uid = b.uid;
		}
		int64_t local_gen = max_uid + 1;
		for (auto &b : profile.bindings) {
			if (b.uid == 0)
				b.uid = local_gen++;
		}
		profile.hotkey_id = OBS_INVALID_HOTKEY_ID;
		profiles_.push_back(profile);
		current_profile_index_ = (int)profiles_.size() - 1;
		SortAndRegisterHotkeys(lock);

		if (hotkey_data) {
			obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
			if (current_profile_index_ >= 0 && current_profile_index_ < (int)profiles_.size()) {
				id = profiles_[current_profile_index_].hotkey_id;
			}
			lock.unlock();
			if (id != OBS_INVALID_HOTKEY_ID) {
				obs_hotkey_load(id, hotkey_data);
			}
			obs_data_array_release(hotkey_data);
		}
	}
	dirty_ = true;
	return true;
}

std::string JoypadConfigStore::GetProfileHotkeyString(int index) const
{
	obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (index >= 0 && index < (int)profiles_.size()) {
			id = profiles_[index].hotkey_id;
		}
	}

	if (id == OBS_INVALID_HOTKEY_ID) {
		return "";
	}

	obs_data_array_t *bindings = obs_hotkey_save(id);
	if (!bindings) {
		return "";
	}

	std::string result;
	size_t count = obs_data_array_count(bindings);
	for (size_t i = 0; i < count; ++i) {
		obs_data_t *item = obs_data_array_item(bindings, i);
		const char *key_name = obs_data_get_string(item, "key");
		bool shift = obs_data_get_bool(item, "shift");
		bool control = obs_data_get_bool(item, "control");
		bool alt = obs_data_get_bool(item, "alt");
		bool command = obs_data_get_bool(item, "command");

		bool has_key = key_name && *key_name && strcmp(key_name, "OBS_KEY_NONE") != 0;

		// Skip empty bindings
		if (!has_key && !shift && !control && !alt && !command) {
			obs_data_release(item);
			continue;
		}

		if (control)
			result += "Ctrl + ";
		if (shift)
			result += "Shift + ";
#ifdef __APPLE__
		if (alt)
			result += "Option + ";
		if (command)
			result += "Cmd + ";
#else
		if (alt)
			result += "Alt + ";
		if (command)
			result += "Win + ";
#endif

		if (has_key) {
			if (strncmp(key_name, "OBS_KEY_", 8) == 0) {
				result += (key_name + 8);
			} else {
				result += key_name;
			}
		} else {
			if (result.length() >= 3 && result.substr(result.length() - 3) == " + ") {
				result = result.substr(0, result.length() - 3);
			}
		}
		obs_data_release(item);
		if (!result.empty())
			break;
	}
	obs_data_array_release(bindings);
	return result;
}

std::string JoypadConfigStore::GetLastFilePath() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return last_file_path_;
}

void JoypadConfigStore::SetLastFilePath(const std::string &path)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		last_file_path_ = path;
	}
	dirty_ = true;
}

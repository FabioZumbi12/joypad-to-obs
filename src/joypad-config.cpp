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
#include <cmath>
#include <algorithm>
#include <plugin-support.h>
#include <util/platform.h>

namespace {
const char *kConfigFileName = "joypad-to-obs.json";
}

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
	binding.device_id = obs_data_get_string(data, "device_id");
	binding.device_name = obs_data_get_string(data, "device_name");
	binding.button = (int)obs_data_get_int(data, "button");
	binding.input_type =
		(JoypadInputType)obs_data_get_int(data, "input_type");
	binding.axis_index = (int)obs_data_get_int(data, "axis_index");
	binding.axis_direction =
		(JoypadAxisDirection)obs_data_get_int(data, "axis_direction");
	if (binding.axis_direction != JoypadAxisDirection::Negative &&
	    binding.axis_direction != JoypadAxisDirection::Both) {
		binding.axis_direction = JoypadAxisDirection::Positive;
	}
	binding.axis_threshold = obs_data_get_double(data, "axis_threshold");
	bool axis_threshold_set =
		obs_data_get_bool(data, "axis_threshold_set");
	if (!axis_threshold_set) {
		binding.axis_threshold = 0.5;
	}
	binding.axis_interval_ms =
		(int)obs_data_get_int(data, "axis_interval_ms");
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
	binding.bool_value = obs_data_get_bool(data, "bool_value");
	binding.allow_above_unity =
		obs_data_get_bool(data, "allow_above_unity");
	if (!binding.allow_above_unity) {
		binding.allow_above_unity =
			obs_data_get_bool(data, "allow_negative_volume");
	}
	binding.volume_value = obs_data_get_double(data, "volume_value");
	binding.slider_gamma = obs_data_get_double(data, "slider_gamma");
	if (binding.action == JoypadActionType::SetSourceVolumePercent) {
		if (binding.slider_gamma <= 0.0) {
			binding.slider_gamma =
				binding.volume_value > 0.0
					? binding.volume_value
					: 0.6;
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
	obs_data_set_string(data, "device_id", binding.device_id.c_str());
	obs_data_set_string(data, "device_name", binding.device_name.c_str());
	obs_data_set_int(data, "button", binding.button);
	obs_data_set_int(data, "input_type", (int)binding.input_type);
	obs_data_set_int(data, "axis_index", binding.axis_index);
	obs_data_set_int(data, "axis_direction",
			 (int)binding.axis_direction);
	obs_data_set_double(data, "axis_threshold",
			    binding.axis_threshold);
	obs_data_set_bool(data, "axis_threshold_set", true);
	obs_data_set_int(data, "axis_interval_ms",
			 binding.axis_interval_ms);
	obs_data_set_double(data, "axis_min_value",
			    binding.axis_min_value);
	obs_data_set_double(data, "axis_max_value",
			    binding.axis_max_value);
	obs_data_set_int(data, "action", (int)binding.action);
	obs_data_set_bool(data, "use_current_scene", binding.use_current_scene);
	obs_data_set_string(data, "scene_name", binding.scene_name.c_str());
	obs_data_set_string(data, "source_name", binding.source_name.c_str());
	obs_data_set_string(data, "filter_name", binding.filter_name.c_str());
	obs_data_set_bool(data, "bool_value", binding.bool_value);
	obs_data_set_bool(data, "allow_above_unity",
			  binding.allow_above_unity);
	obs_data_set_double(data, "volume_value", binding.volume_value);
	obs_data_set_double(data, "slider_gamma", binding.slider_gamma);
	obs_data_set_bool(data, "enabled", binding.enabled);
}

void JoypadConfigStore::Load()
{
	std::lock_guard<std::mutex> lock(mutex_);
	bindings_.clear();
	axis_active_.clear();
	axis_last_raw_.clear();

	ensure_config_dir();

	char *config_path = obs_module_config_path(kConfigFileName);
	if (!config_path) {
		return;
	}

	obs_data_t *data =
		obs_data_create_from_json_file_safe(config_path, "backup");
	bfree(config_path);

	if (!data) {
		return;
	}

	obs_data_array_t *bindings_array =
		obs_data_get_array(data, "bindings");
	if (bindings_array) {
		size_t count = obs_data_array_count(bindings_array);
		bindings_.reserve(count);
		for (size_t i = 0; i < count; ++i) {
			obs_data_t *item = obs_data_array_item(bindings_array, i);
			if (!item) {
				continue;
			}
			JoypadBinding binding;
			load_binding_from_data(binding, item);
			bindings_.push_back(binding);
			obs_data_release(item);
		}
		obs_data_array_release(bindings_array);
	}

	obs_data_array_t *axis_array =
		obs_data_get_array(data, "axis_last_values");
	if (axis_array) {
		size_t count = obs_data_array_count(axis_array);
		for (size_t i = 0; i < count; ++i) {
			obs_data_t *item = obs_data_array_item(axis_array, i);
			if (!item) {
				continue;
			}
			const char *key =
				obs_data_get_string(item, "key");
			double raw = obs_data_get_double(item, "raw");
			if (key && *key) {
				axis_last_raw_[key] = raw;
			}
			obs_data_release(item);
		}
		obs_data_array_release(axis_array);
	}

	obs_data_release(data);
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
	obs_data_array_t *bindings_array = obs_data_array_create();

	for (const auto &binding : bindings_) {
		obs_data_t *item = obs_data_create();
		save_binding_to_data(binding, item);
		obs_data_array_push_back(bindings_array, item);
		obs_data_release(item);
	}

	obs_data_set_array(data, "bindings", bindings_array);
	obs_data_array_release(bindings_array);

	obs_data_array_t *axis_array = obs_data_array_create();
	for (const auto &entry : axis_last_raw_) {
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "key", entry.first.c_str());
		obs_data_set_double(item, "raw", entry.second);
		obs_data_array_push_back(axis_array, item);
		obs_data_release(item);
	}
	obs_data_set_array(data, "axis_last_values", axis_array);
	obs_data_array_release(axis_array);

	if (!obs_data_save_json(data, config_path)) {
		obs_log(LOG_WARNING, "Nao foi possivel salvar %s", config_path);
	}

	obs_data_release(data);
	bfree(config_path);
}

void JoypadConfigStore::SetAxisLastRaw(const std::string &key, double raw)
{
	if (key.empty()) {
		return;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	axis_last_raw_[key] = raw;
}

bool JoypadConfigStore::ConsumeAxisLastRaw(const std::string &key,
					   double &raw_out)
{
	if (key.empty()) {
		return false;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = axis_last_raw_.find(key);
	if (it == axis_last_raw_.end()) {
		return false;
	}
	raw_out = it->second;
	axis_last_raw_.erase(it);
	return true;
}

void JoypadConfigStore::ClearAxisLastRaw()
{
	std::lock_guard<std::mutex> lock(mutex_);
	axis_last_raw_.clear();
}

void JoypadConfigStore::AddBinding(const JoypadBinding &binding)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		bindings_.push_back(binding);
	}
	Save();
}

void JoypadConfigStore::RemoveBinding(size_t index)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (index >= bindings_.size()) {
			return;
		}
		bindings_.erase(bindings_.begin() + (ptrdiff_t)index);
	}
	Save();
}

void JoypadConfigStore::UpdateBinding(size_t index,
				      const JoypadBinding &binding)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (index >= bindings_.size()) {
			return;
		}
		bindings_[index] = binding;
	}
	Save();
}

std::vector<JoypadBinding> JoypadConfigStore::GetBindingsSnapshot() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return bindings_;
}

std::vector<JoypadBinding>
JoypadConfigStore::FindMatchingBindings(const JoypadEvent &event) const
{
	std::vector<JoypadBinding> matches;
	std::lock_guard<std::mutex> lock(mutex_);
	for (const auto &binding_ref : bindings_) {
		JoypadBinding binding = binding_ref;
		if (!binding.enabled) {
			continue;
		}
		if (binding.input_type == JoypadInputType::Axis) {
			if (!event.is_axis ||
			    binding.axis_index != event.axis_index) {
				continue;
			}
			const bool is_percent_axis =
				(binding.action ==
				 JoypadActionType::SetSourceVolumePercent);
			if (binding.action ==
			    JoypadActionType::SetSourceVolumePercent) {
				// Map axis min..max to 0..100%
				double minv = binding.axis_min_value;
				double maxv = binding.axis_max_value;
				if (maxv <= minv) {
					minv = 0.0;
					maxv = 1024.0;
				}
				const double raw = event.axis_raw_value;
				double percent =
					((raw - minv) / (maxv - minv)) * 100.0;
				if (binding.axis_direction == JoypadAxisDirection::Negative) {
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
				double gamma = binding.slider_gamma > 0.0
						       ? binding.slider_gamma
						       : 0.6;
				gamma = std::clamp(gamma, 0.1, 50.0);
				double curved = std::pow(base, gamma);
				binding.volume_value =
					std::clamp(curved * 100.0, 0.0, 100.0);
			}
			const double value = event.axis_value;
			const double abs_value = std::fabs(value);
			if (!is_percent_axis &&
			    binding.axis_direction !=
				    JoypadAxisDirection::Both) {
				int dir = value >= 0.0 ? 1 : -1;
				if (dir != (int)binding.axis_direction) {
					continue;
				}
			}

			const double threshold_on = binding.axis_threshold;
			const double threshold_off = binding.axis_threshold * 0.4;
			std::string axis_key = event.device_id + ":" +
					      std::to_string(binding.axis_index) +
					      ":" +
					      std::to_string(
						      (int)binding.axis_direction);
			if (!is_percent_axis) {
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
			if (binding.action ==
			    JoypadActionType::AdjustSourceVolume) {
				double sign = value >= 0.0 ? 1.0 : -1.0;
				double intensity = std::clamp(abs_value, 0.0, 1.0);
				binding.volume_value =
					std::fabs(binding.volume_value) *
					intensity * sign;
			}
		} else {
			if (event.is_axis || binding.button != event.button) {
				continue;
			}
		}
		if (!binding.device_id.empty() &&
		    binding.device_id != event.device_id) {
			continue;
		}
		matches.push_back(binding);
	}
	return matches;
}

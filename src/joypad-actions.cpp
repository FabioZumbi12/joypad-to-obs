/*
Joypad to OBS
Copyright (C) <Year> <Developer> <Email Address>

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

#include "joypad-actions.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <cmath>

namespace {
constexpr float kMinDb = -60.0f;
constexpr float kMaxDb = 50.0f;

static float db_to_mul(float db)
{
	return std::pow(10.0f, db / 20.0f);
}

static float mul_to_db(float mul)
{
	if (mul <= 0.000001f) {
		return -100.0f;
	}
	return 20.0f * std::log10(mul);
}

obs_source_t *get_scene_source(const JoypadBinding &binding)
{
	if (binding.use_current_scene) {
		return obs_frontend_get_current_scene();
	}

	if (!binding.scene_name.empty()) {
		return obs_get_source_by_name(binding.scene_name.c_str());
	}

	return nullptr;
}

void release_scene_source(obs_source_t *scene_source,
			  const JoypadBinding &binding)
{
	if (!scene_source) {
		return;
	}
	obs_source_release(scene_source);
	(void)binding;
}

obs_sceneitem_t *get_scene_item_from_binding(const JoypadBinding &binding,
					     obs_source_t **scene_source_out)
{
	obs_source_t *scene_source = get_scene_source(binding);
	if (!scene_source) {
		return nullptr;
	}

	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (!scene) {
		release_scene_source(scene_source, binding);
		return nullptr;
	}

	obs_sceneitem_t *item =
		obs_scene_find_source(scene, binding.source_name.c_str());
	if (!item) {
		release_scene_source(scene_source, binding);
		return nullptr;
	}

	if (scene_source_out) {
		*scene_source_out = scene_source;
	} else {
		release_scene_source(scene_source, binding);
	}

	return item;
}

} // namespace

void JoypadActionEngine::Execute(const JoypadBinding &binding)
{
	switch (binding.action) {
	case JoypadActionType::SwitchScene: {
		if (binding.scene_name.empty()) {
			return;
		}
		obs_source_t *scene =
			obs_get_source_by_name(binding.scene_name.c_str());
		if (!scene) {
			return;
		}
		obs_frontend_set_current_scene(scene);
		obs_source_release(scene);
		break;
	}
	case JoypadActionType::ToggleSourceVisibility:
	case JoypadActionType::SetSourceVisibility: {
		if (binding.source_name.empty()) {
			return;
		}

		obs_source_t *scene_source = nullptr;
		obs_sceneitem_t *item =
			get_scene_item_from_binding(binding, &scene_source);
		if (!item) {
			return;
		}

		bool visible = obs_sceneitem_visible(item);
		bool new_visible = (binding.action ==
				    JoypadActionType::ToggleSourceVisibility)
					   ? !visible
					   : binding.bool_value;
		obs_sceneitem_set_visible(item, new_visible);
		release_scene_source(scene_source, binding);
		break;
	}
	case JoypadActionType::ToggleSourceMute:
	case JoypadActionType::SetSourceMute: {
		if (binding.source_name.empty()) {
			return;
		}
		obs_source_t *source =
			obs_get_source_by_name(binding.source_name.c_str());
		if (!source) {
			return;
		}
		bool muted = obs_source_muted(source);
		bool new_muted =
			(binding.action == JoypadActionType::ToggleSourceMute)
				? !muted
				: binding.bool_value;
		obs_source_set_muted(source, new_muted);
		obs_source_release(source);
		break;
	}
	case JoypadActionType::SetSourceVolume: {
		if (binding.source_name.empty()) {
			return;
		}
		obs_source_t *source =
			obs_get_source_by_name(binding.source_name.c_str());
		if (!source) {
			return;
		}
		float target_db = (float)binding.volume_value;
		if (!binding.allow_above_unity && target_db > 0.0f) {
			target_db = 0.0f;
		}
		if (target_db < kMinDb) {
			target_db = kMinDb;
		}
		if (target_db > kMaxDb) {
			target_db = kMaxDb;
		}
		obs_source_set_volume(source, db_to_mul(target_db));
		obs_source_release(source);
		break;
	}
	case JoypadActionType::SetSourceVolumePercent: {
		if (binding.source_name.empty()) {
			return;
		}
		obs_source_t *source =
			obs_get_source_by_name(binding.source_name.c_str());
		if (!source) {
			return;
		}
		float percent = (float)binding.volume_value;
		if (percent < 0.0f) {
			percent = 0.0f;
		}
		if (percent > 100.0f) {
			percent = 100.0f;
		}
		float target_db = kMinDb + (percent / 100.0f) * (0.0f - kMinDb);
		if (target_db < kMinDb) {
			target_db = kMinDb;
		}
		if (target_db > 0.0f) {
			target_db = 0.0f;
		}
		obs_source_set_volume(source, db_to_mul(target_db));
		obs_source_release(source);
		break;
	}
	case JoypadActionType::AdjustSourceVolume: {
		if (binding.source_name.empty()) {
			return;
		}
		obs_source_t *source =
			obs_get_source_by_name(binding.source_name.c_str());
		if (!source) {
			return;
		}
		float current_mul = obs_source_get_volume(source);
		float next_mul = current_mul + (float)binding.volume_value;
		if (next_mul < 0.0f) {
			next_mul = 0.0f;
		}
		if (!binding.allow_above_unity && next_mul > 1.0f) {
			next_mul = 1.0f;
		}
		float min_mul = db_to_mul(kMinDb);
		float max_mul = db_to_mul(kMaxDb);
		if (next_mul < min_mul) {
			next_mul = min_mul;
		}
		if (next_mul > max_mul) {
			next_mul = max_mul;
		}
		obs_source_set_volume(source, next_mul);
		obs_source_release(source);
		break;
	}
	case JoypadActionType::MediaPlayPause:
	case JoypadActionType::MediaRestart:
	case JoypadActionType::MediaStop: {
		if (binding.source_name.empty()) {
			return;
		}
		obs_source_t *source =
			obs_get_source_by_name(binding.source_name.c_str());
		if (!source) {
			return;
		}

		switch (binding.action) {
		case JoypadActionType::MediaPlayPause:
		{
			enum obs_media_state state =
				obs_source_media_get_state(source);
			bool pause = (state == OBS_MEDIA_STATE_PLAYING);
			obs_source_media_play_pause(source, pause);
			break;
		}
			break;
		case JoypadActionType::MediaRestart:
			obs_source_media_restart(source);
			break;
		case JoypadActionType::MediaStop:
			obs_source_media_stop(source);
			break;
		default:
			break;
		}

		obs_source_release(source);
		break;
	}
	case JoypadActionType::ToggleFilterEnabled:
	case JoypadActionType::SetFilterEnabled: {
		if (binding.source_name.empty() || binding.filter_name.empty()) {
			return;
		}
		obs_source_t *source =
			obs_get_source_by_name(binding.source_name.c_str());
		if (!source) {
			return;
		}
		obs_source_t *filter = obs_source_get_filter_by_name(
			source, binding.filter_name.c_str());
		if (!filter) {
			obs_source_release(source);
			return;
		}
		bool enabled = obs_source_enabled(filter);
		bool new_enabled =
			(binding.action == JoypadActionType::ToggleFilterEnabled)
				? !enabled
				: binding.bool_value;
		obs_source_set_enabled(filter, new_enabled);
		obs_source_release(filter);
		obs_source_release(source);
		break;
	}
	default:
		break;
	}
}

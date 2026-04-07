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

#include "joypad-actions.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs-properties.h>
#include <algorithm>
#include <cmath>
#include <QMetaObject>
#include <QCoreApplication>

namespace {
constexpr float kMinDb = -60.0f;
constexpr float kMaxDb = 50.0f;
constexpr float kVolumeEpsilon = 0.0005f;
constexpr uint32_t kAlignCenter = 0;

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

void release_scene_source(obs_source_t *scene_source, const JoypadBinding &binding)
{
	if (!scene_source) {
		return;
	}
	obs_source_release(scene_source);
	(void)binding;
}

obs_sceneitem_t *get_scene_item_from_binding(const JoypadBinding &binding, obs_source_t **scene_source_out)
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

	obs_sceneitem_t *item = obs_scene_find_source(scene, binding.source_name.c_str());
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

void apply_sceneitem_alignment(obs_sceneitem_t *item, JoypadSourceTransformOp op)
{
	obs_video_info ovi = {};
	if (!obs_get_video_info(&ovi) || ovi.base_width == 0 || ovi.base_height == 0) {
		return;
	}

	struct vec2 box = {};
	obs_sceneitem_get_box_scale(item, &box);
	const float width = std::fabs(box.x);
	const float height = std::fabs(box.y);

	struct vec2 pos = {};
	obs_sceneitem_get_pos(item, &pos);
	obs_sceneitem_set_alignment(item, kAlignCenter);

	switch (op) {
	case JoypadSourceTransformOp::AlignLeft:
		pos.x = width * 0.5f;
		break;
	case JoypadSourceTransformOp::AlignRight:
		pos.x = (float)ovi.base_width - width * 0.5f;
		break;
	case JoypadSourceTransformOp::AlignTop:
		pos.y = height * 0.5f;
		break;
	case JoypadSourceTransformOp::AlignBottom:
		pos.y = (float)ovi.base_height - height * 0.5f;
		break;
	case JoypadSourceTransformOp::AlignTopLeft:
		pos.x = width * 0.5f;
		pos.y = height * 0.5f;
		break;
	case JoypadSourceTransformOp::AlignTopRight:
		pos.x = (float)ovi.base_width - width * 0.5f;
		pos.y = height * 0.5f;
		break;
	case JoypadSourceTransformOp::AlignBottomLeft:
		pos.x = width * 0.5f;
		pos.y = (float)ovi.base_height - height * 0.5f;
		break;
	case JoypadSourceTransformOp::AlignBottomRight:
		pos.x = (float)ovi.base_width - width * 0.5f;
		pos.y = (float)ovi.base_height - height * 0.5f;
		break;
	case JoypadSourceTransformOp::AlignCenterLeft:
		pos.x = width * 0.5f;
		pos.y = (float)ovi.base_height * 0.5f;
		break;
	case JoypadSourceTransformOp::AlignCenterRight:
		pos.x = (float)ovi.base_width - width * 0.5f;
		pos.y = (float)ovi.base_height * 0.5f;
		break;
	case JoypadSourceTransformOp::CenterToScreen:
		pos.x = (float)ovi.base_width * 0.5f;
		pos.y = (float)ovi.base_height * 0.5f;
		break;
	default:
		break;
	}

	obs_sceneitem_set_pos(item, &pos);
}

} // namespace

void JoypadActionEngine::Execute(const JoypadBinding &binding)
{
	switch (binding.action) {
	case JoypadActionType::SwitchScene: {
		if (binding.scene_name.empty()) {
			return;
		}
		obs_source_t *scene = obs_get_source_by_name(binding.scene_name.c_str());
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
		obs_sceneitem_t *item = get_scene_item_from_binding(binding, &scene_source);
		if (!item) {
			return;
		}

		bool visible = obs_sceneitem_visible(item);
		bool new_visible = (binding.action == JoypadActionType::ToggleSourceVisibility) ? !visible
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
		obs_source_t *source = obs_get_source_by_name(binding.source_name.c_str());
		if (!source) {
			return;
		}
		bool muted = obs_source_muted(source);
		bool new_muted = (binding.action == JoypadActionType::ToggleSourceMute) ? !muted : binding.bool_value;
		obs_source_set_muted(source, new_muted);
		obs_source_release(source);
		break;
	}
	case JoypadActionType::SetSourceVolume: {
		if (binding.source_name.empty()) {
			return;
		}
		obs_source_t *source = obs_get_source_by_name(binding.source_name.c_str());
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
		const float target_mul = db_to_mul(target_db);
		const float current_mul = obs_source_get_volume(source);
		if (std::fabs(current_mul - target_mul) > kVolumeEpsilon) {
			obs_source_set_volume(source, target_mul);
		}
		obs_source_release(source);
		break;
	}
	case JoypadActionType::SetSourceVolumePercent: {
		if (binding.source_name.empty()) {
			return;
		}
		obs_source_t *source = obs_get_source_by_name(binding.source_name.c_str());
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
		const float target_mul = db_to_mul(target_db);
		const float current_mul = obs_source_get_volume(source);
		if (std::fabs(current_mul - target_mul) > kVolumeEpsilon) {
			obs_source_set_volume(source, target_mul);
		}
		obs_source_release(source);
		break;
	}
	case JoypadActionType::AdjustSourceVolume: {
		if (binding.source_name.empty()) {
			return;
		}
		obs_source_t *source = obs_get_source_by_name(binding.source_name.c_str());
		if (!source) {
			return;
		}
		const float current_mul = obs_source_get_volume(source);
		float current_db = mul_to_db(current_mul);
		if (current_db < kMinDb) {
			current_db = kMinDb;
		}
		float next_db = current_db + (float)binding.volume_value;
		if (!binding.allow_above_unity && next_db > 0.0f) {
			next_db = 0.0f;
		}
		if (next_db < kMinDb) {
			next_db = kMinDb;
		}
		if (next_db > kMaxDb) {
			next_db = kMaxDb;
		}
		const float next_mul = db_to_mul(next_db);
		if (std::fabs(current_mul - next_mul) > kVolumeEpsilon) {
			obs_source_set_volume(source, next_mul);
		}
		obs_source_release(source);
		break;
	}
	case JoypadActionType::MediaPlayPause:
	case JoypadActionType::MediaRestart:
	case JoypadActionType::MediaStop: {
		if (binding.source_name.empty()) {
			return;
		}
		obs_source_t *source = obs_get_source_by_name(binding.source_name.c_str());
		if (!source) {
			return;
		}

		switch (binding.action) {
		case JoypadActionType::MediaPlayPause: {
			enum obs_media_state state = obs_source_media_get_state(source);
			bool pause = (state == OBS_MEDIA_STATE_PLAYING);
			obs_source_media_play_pause(source, pause);
			break;
		} break;
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
		obs_source_t *source = obs_get_source_by_name(binding.source_name.c_str());
		if (!source) {
			return;
		}
		obs_source_t *filter = obs_source_get_filter_by_name(source, binding.filter_name.c_str());
		if (!filter) {
			obs_source_release(source);
			return;
		}
		bool enabled = obs_source_enabled(filter);
		bool new_enabled = (binding.action == JoypadActionType::ToggleFilterEnabled) ? !enabled
											     : binding.bool_value;
		obs_source_set_enabled(filter, new_enabled);
		obs_source_release(filter);
		obs_source_release(source);
		break;
	}
	case JoypadActionType::SetFilterProperty: {
		if (binding.source_name.empty() || binding.filter_name.empty() ||
		    binding.filter_property_name.empty()) {
			return;
		}
		obs_source_t *source = obs_get_source_by_name(binding.source_name.c_str());
		if (!source) {
			return;
		}
		obs_source_t *filter = obs_source_get_filter_by_name(source, binding.filter_name.c_str());
		if (!filter) {
			obs_source_release(source);
			return;
		}

		obs_properties_t *props = obs_source_properties(filter);
		if (!props) {
			obs_source_release(filter);
			obs_source_release(source);
			return;
		}
		obs_property_t *prop = obs_properties_get(props, binding.filter_property_name.c_str());
		if (!prop) {
			obs_properties_destroy(props);
			obs_source_release(filter);
			obs_source_release(source);
			return;
		}
		obs_data_t *settings = obs_source_get_settings(filter);
		if (!settings) {
			obs_properties_destroy(props);
			obs_source_release(filter);
			obs_source_release(source);
			return;
		}

		const enum obs_property_type prop_type = obs_property_get_type(prop);
		switch (prop_type) {
		case OBS_PROPERTY_BOOL:
			obs_data_set_bool(settings, binding.filter_property_name.c_str(), binding.bool_value);
			break;
		case OBS_PROPERTY_INT: {
			const int minv = obs_property_int_min(prop);
			const int maxv = obs_property_int_max(prop);
			long long value = (long long)std::llround(binding.filter_property_value);
			value = std::clamp(value, (long long)minv, (long long)maxv);
			obs_data_set_int(settings, binding.filter_property_name.c_str(), value);
		} break;
		case OBS_PROPERTY_FLOAT: {
			const double minv = obs_property_float_min(prop);
			const double maxv = obs_property_float_max(prop);
			double value = std::clamp(binding.filter_property_value, minv, maxv);
			obs_data_set_double(settings, binding.filter_property_name.c_str(), value);
		} break;
		case OBS_PROPERTY_LIST: {
			const enum obs_combo_format format = obs_property_list_format(prop);
			if (format == OBS_COMBO_FORMAT_INT) {
				obs_data_set_int(settings, binding.filter_property_name.c_str(),
						 binding.filter_property_list_int);
			} else if (format == OBS_COMBO_FORMAT_FLOAT) {
				obs_data_set_double(settings, binding.filter_property_name.c_str(),
						    binding.filter_property_list_float);
			} else {
				obs_data_set_string(settings, binding.filter_property_name.c_str(),
						    binding.filter_property_list_string.c_str());
			}
		} break;
		default:
			break;
		}

		obs_source_update(filter, settings);
		obs_data_release(settings);
		obs_properties_destroy(props);
		obs_source_release(filter);
		obs_source_release(source);
		break;
	}
	case JoypadActionType::AdjustFilterProperty: {
		if (binding.source_name.empty() || binding.filter_name.empty() ||
		    binding.filter_property_name.empty()) {
			return;
		}
		obs_source_t *source = obs_get_source_by_name(binding.source_name.c_str());
		if (!source) {
			return;
		}
		obs_source_t *filter = obs_source_get_filter_by_name(source, binding.filter_name.c_str());
		if (!filter) {
			obs_source_release(source);
			return;
		}

		obs_properties_t *props = obs_source_properties(filter);
		if (!props) {
			obs_source_release(filter);
			obs_source_release(source);
			return;
		}
		obs_property_t *prop = obs_properties_get(props, binding.filter_property_name.c_str());
		if (!prop) {
			obs_properties_destroy(props);
			obs_source_release(filter);
			obs_source_release(source);
			return;
		}
		obs_data_t *settings = obs_source_get_settings(filter);
		if (!settings) {
			obs_properties_destroy(props);
			obs_source_release(filter);
			obs_source_release(source);
			return;
		}

		const enum obs_property_type prop_type = obs_property_get_type(prop);
		if (prop_type == OBS_PROPERTY_INT) {
			const int minv = obs_property_int_min(prop);
			const int maxv = obs_property_int_max(prop);
			const long long current = obs_data_get_int(settings, binding.filter_property_name.c_str());
			const long long delta = (long long)std::llround(binding.volume_value);
			long long next = current + delta;
			next = std::clamp(next, (long long)minv, (long long)maxv);
			obs_data_set_int(settings, binding.filter_property_name.c_str(), next);
			obs_source_update(filter, settings);
		} else if (prop_type == OBS_PROPERTY_FLOAT) {
			const double minv = obs_property_float_min(prop);
			const double maxv = obs_property_float_max(prop);
			const double current = obs_data_get_double(settings, binding.filter_property_name.c_str());
			double next = current + binding.volume_value;
			next = std::clamp(next, minv, maxv);
			obs_data_set_double(settings, binding.filter_property_name.c_str(), next);
			obs_source_update(filter, settings);
		}

		obs_data_release(settings);
		obs_properties_destroy(props);
		obs_source_release(filter);
		obs_source_release(source);
		break;
	}
	case JoypadActionType::SourceTransform: {
		if (binding.source_name.empty()) {
			return;
		}

		obs_source_t *scene_source = nullptr;
		obs_sceneitem_t *item = get_scene_item_from_binding(binding, &scene_source);
		if (!item) {
			return;
		}

		switch (binding.source_transform_op) {
		case JoypadSourceTransformOp::FlipHorizontal: {
			struct vec2 scale = {};
			obs_sceneitem_get_scale(item, &scale);
			scale.x = -scale.x;
			obs_sceneitem_set_scale(item, &scale);
			break;
		}
		case JoypadSourceTransformOp::FlipVertical: {
			struct vec2 scale = {};
			obs_sceneitem_get_scale(item, &scale);
			scale.y = -scale.y;
			obs_sceneitem_set_scale(item, &scale);
			break;
		}
		case JoypadSourceTransformOp::Rotate90CW:
			obs_sceneitem_set_rot(item, obs_sceneitem_get_rot(item) + 90.0f);
			break;
		case JoypadSourceTransformOp::Rotate90CCW:
			obs_sceneitem_set_rot(item, obs_sceneitem_get_rot(item) - 90.0f);
			break;
		case JoypadSourceTransformOp::Rotate180:
			obs_sceneitem_set_rot(item, obs_sceneitem_get_rot(item) + 180.0f);
			break;
		case JoypadSourceTransformOp::FitToScreen: {
			obs_video_info ovi = {};
			if (obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0) {
				struct vec2 pos = {(float)ovi.base_width * 0.5f, (float)ovi.base_height * 0.5f};
				struct vec2 bounds = {(float)ovi.base_width, (float)ovi.base_height};
				obs_sceneitem_set_alignment(item, kAlignCenter);
				obs_sceneitem_set_pos(item, &pos);
				obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
				obs_sceneitem_set_bounds_alignment(item, kAlignCenter);
				obs_sceneitem_set_bounds(item, &bounds);
			}
			break;
		}
		case JoypadSourceTransformOp::StretchToScreen: {
			obs_video_info ovi = {};
			if (obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0) {
				struct vec2 pos = {(float)ovi.base_width * 0.5f, (float)ovi.base_height * 0.5f};
				struct vec2 bounds = {(float)ovi.base_width, (float)ovi.base_height};
				obs_sceneitem_set_alignment(item, kAlignCenter);
				obs_sceneitem_set_pos(item, &pos);
				obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_STRETCH);
				obs_sceneitem_set_bounds_alignment(item, kAlignCenter);
				obs_sceneitem_set_bounds(item, &bounds);
			}
			break;
		}
		case JoypadSourceTransformOp::AlignLeft:
		case JoypadSourceTransformOp::AlignRight:
		case JoypadSourceTransformOp::AlignTop:
		case JoypadSourceTransformOp::AlignBottom:
		case JoypadSourceTransformOp::AlignTopLeft:
		case JoypadSourceTransformOp::AlignTopRight:
		case JoypadSourceTransformOp::AlignBottomLeft:
		case JoypadSourceTransformOp::AlignBottomRight:
		case JoypadSourceTransformOp::AlignCenterLeft:
		case JoypadSourceTransformOp::AlignCenterRight:
		case JoypadSourceTransformOp::CenterToScreen:
			apply_sceneitem_alignment(item, binding.source_transform_op);
			break;
		default:
			break;
		}

		release_scene_source(scene_source, binding);
		break;
	}
	case JoypadActionType::NextScene:
	case JoypadActionType::PreviousScene: {
		obs_source_t *current_scene = nullptr;
		if (obs_frontend_preview_program_mode_active()) {
			current_scene = obs_frontend_get_current_preview_scene();
		} else {
			current_scene = obs_frontend_get_current_scene();
		}

		if (!current_scene) {
			return;
		}

		obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);

		size_t current_index = (size_t)-1;
		for (size_t i = 0; i < scenes.sources.num; ++i) {
			if (scenes.sources.array[i] == current_scene) {
				current_index = i;
				break;
			}
		}

		if (current_index != (size_t)-1) {
			size_t new_index = current_index;
			if (binding.action == JoypadActionType::NextScene) {
				new_index++;
				if (new_index >= scenes.sources.num) {
					new_index = 0;
				}
			} else {
				if (new_index == 0) {
					new_index = scenes.sources.num - 1;
				} else {
					new_index--;
				}
			}
			if (new_index < scenes.sources.num) {
				if (obs_frontend_preview_program_mode_active()) {
					obs_frontend_set_current_preview_scene(scenes.sources.array[new_index]);
				} else {
					obs_frontend_set_current_scene(scenes.sources.array[new_index]);
				}
			}
		}

		obs_frontend_source_list_free(&scenes);
		obs_source_release(current_scene);
		break;
	}
	case JoypadActionType::ToggleStreaming:
		if (obs_frontend_streaming_active()) {
			obs_frontend_streaming_stop();
		} else {
			obs_frontend_streaming_start();
		}
		break;
	case JoypadActionType::ToggleRecording:
		if (obs_frontend_recording_active()) {
			obs_frontend_recording_stop();
		} else {
			obs_frontend_recording_start();
		}
		break;
	case JoypadActionType::ToggleVirtualCam:
		if (obs_frontend_virtualcam_active()) {
			obs_frontend_stop_virtualcam();
		} else {
			obs_frontend_start_virtualcam();
		}
		break;
	case JoypadActionType::ToggleStudioMode:
		if (QCoreApplication *app = QCoreApplication::instance()) {
			QMetaObject::invokeMethod(app, []() {
				obs_frontend_set_preview_program_mode(!obs_frontend_preview_program_mode_active());
			});
		}
		break;
	case JoypadActionType::TransitionToProgram:
		if (QCoreApplication *app = QCoreApplication::instance()) {
			QMetaObject::invokeMethod(app, []() {
				if (obs_frontend_preview_program_mode_active()) {
					obs_frontend_preview_program_trigger_transition();
				}
			});
		}
		break;
	case JoypadActionType::StartReplayBuffer:
		if (!obs_frontend_replay_buffer_active()) {
			obs_frontend_replay_buffer_start();
		}
		break;
	case JoypadActionType::StopReplayBuffer:
		if (obs_frontend_replay_buffer_active()) {
			obs_frontend_replay_buffer_stop();
		}
		break;
	case JoypadActionType::ToggleReplayBuffer:
		if (obs_frontend_replay_buffer_active()) {
			obs_frontend_replay_buffer_stop();
		} else {
			obs_frontend_replay_buffer_start();
		}
		break;
	case JoypadActionType::SaveReplayBuffer:
		if (obs_frontend_replay_buffer_active()) {
			obs_frontend_replay_buffer_save();
		}
		break;
	case JoypadActionType::Screenshot:
		if (binding.screenshot_target == JoypadScreenshotTarget::Program) {
			obs_frontend_take_screenshot();
			break;
		}
		if (binding.source_name.empty()) {
			return;
		}
		if (obs_source_t *source = obs_get_source_by_name(binding.source_name.c_str())) {
			obs_frontend_take_source_screenshot(source);
			obs_source_release(source);
		}
		break;
	default:
		break;
	}
}

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

#include "joypad-ui.h"
#include "joypad-actions.h"
#include "plugin-support.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs-properties.h>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QStringList>
#include <QVBoxLayout>
#include <QToolButton>
#include <QPlainTextEdit>
#include <QMetaObject>
#include <QInputDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QSlider>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QTimer>
#include <QStyle>
#include <QFileInfo>
#include <QDir>
#include <QSplitter>
#include <QCloseEvent>
#include <QColorDialog>
#include <QSpinBox>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {
constexpr int kDeviceIdRole = Qt::UserRole;
constexpr int kDeviceStableIdRole = Qt::UserRole + 1;
constexpr int kDeviceTypeIdRole = Qt::UserRole + 2;

std::atomic<int> g_binding_dialog_open_count{0};
std::atomic<bool> g_input_listening_enabled{true};
struct DialogTestState {
	bool enabled = false;
	JoypadBinding binding;
};
std::mutex g_dialog_test_mutex;
DialogTestState g_dialog_test_state;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_dialog_axis_last_dispatch;
constexpr double kLearnAxisDeadzone = 0.65;

inline QString L(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

bool looks_generic_device_name(const QString &name, const QString &type_id)
{
	const QString type = type_id.trimmed().toUpper();
	// MID/PID comes from winmm caps and usually indicates generic driver naming.
	if (type.startsWith("MID_")) {
		return true;
	}

	const QString lower = name.trimmed().toLower();
	if (lower.isEmpty()) {
		return true;
	}

	// "Controller" alone is too generic in practice and should include type info in label.
	if (lower == "controller" || lower == "gamepad" || lower == "joystick") {
		return true;
	}

	return lower.contains("pc-joystick") || lower.contains("usb input device") || lower.contains("hid-compliant") ||
	       lower.contains("hid compliant");
}

QString format_device_label(const QString &name, const QString &type_id)
{
	QString base = name.trimmed();
	if (base.isEmpty()) {
		base = L("JoypadToOBS.Common.Unknown");
	}
	if (looks_generic_device_name(base, type_id)) {
		const QString generic = L("JoypadToOBS.Common.Controller");
		if (!type_id.isEmpty()) {
			return QString("%1 [%2]").arg(generic, type_id);
		}
		return generic;
	}
	// When we already have a readable device name (e.g. from WGI),
	// avoid appending VID/PID noise to the UI label.
	return base;
}

QString device_label_from_info(const JoypadDeviceInfo &device)
{
	return format_device_label(QString::fromStdString(device.name), QString::fromStdString(device.type_id));
}

QString device_label_from_binding(const JoypadBinding &binding)
{
	if (binding.input_type == JoypadInputType::Button && !binding.button_combo.empty()) {
		if (binding.button_combo.size() > 1) {
			return L("JoypadToOBS.Common.MultipleDevices");
		}
		const auto &entry = binding.button_combo.front();
		if (entry.device_id.empty()) {
			return L("JoypadToOBS.Common.Any");
		}
		return format_device_label(QString::fromStdString(entry.device_name),
					   QString::fromStdString(entry.device_type_id));
	}
	if (binding.device_id.empty()) {
		return L("JoypadToOBS.Common.Any");
	}
	return format_device_label(QString::fromStdString(binding.device_name),
				   QString::fromStdString(binding.device_type_id));
}

QString combo_entry_to_text(const JoypadButtonComboEntry &entry)
{
	const QString device = entry.device_id.empty()
				       ? L("JoypadToOBS.Common.Any")
				       : format_device_label(QString::fromStdString(entry.device_name),
							     QString::fromStdString(entry.device_type_id));
	return QStringLiteral("%1: %2").arg(device, L("JoypadToOBS.Common.ButtonNumber").arg(entry.button));
}

QString add_listen_button_text()
{
	return QStringLiteral("+ %1").arg(L("JoypadToOBS.Common.Listen"));
}

QString action_to_text(JoypadActionType action)
{
	switch (action) {
	case JoypadActionType::SwitchScene:
		return L("JoypadToOBS.Action.SwitchScene");
	case JoypadActionType::ToggleSourceVisibility:
		return L("JoypadToOBS.Action.ToggleSourceVisibility");
	case JoypadActionType::SetSourceVisibility:
		return L("JoypadToOBS.Action.SetSourceVisibility");
	case JoypadActionType::ToggleSourceMute:
		return L("JoypadToOBS.Action.ToggleSourceMute");
	case JoypadActionType::SetSourceMute:
		return L("JoypadToOBS.Action.SetSourceMute");
	case JoypadActionType::SetSourceVolume:
		return L("JoypadToOBS.Action.SetSourceVolume");
	case JoypadActionType::AdjustSourceVolume:
		return L("JoypadToOBS.Action.AdjustSourceVolume");
	case JoypadActionType::SetSourceVolumePercent:
		return L("JoypadToOBS.Action.SetSourceVolumeSlider");
	case JoypadActionType::MediaPlayPause:
		return L("JoypadToOBS.Action.MediaPlayPause");
	case JoypadActionType::MediaRestart:
		return L("JoypadToOBS.Action.MediaRestart");
	case JoypadActionType::MediaStop:
		return L("JoypadToOBS.Action.MediaStop");
	case JoypadActionType::ToggleFilterEnabled:
		return L("JoypadToOBS.Action.ToggleFilter");
	case JoypadActionType::SetFilterEnabled:
		return L("JoypadToOBS.Action.SetFilter");
	case JoypadActionType::NextScene:
		return L("JoypadToOBS.Action.NextScene");
	case JoypadActionType::PreviousScene:
		return L("JoypadToOBS.Action.PreviousScene");
	case JoypadActionType::ToggleStreaming:
		return L("JoypadToOBS.Action.ToggleStreaming");
	case JoypadActionType::ToggleRecording:
		return L("JoypadToOBS.Action.ToggleRecording");
	case JoypadActionType::ToggleVirtualCam:
		return L("JoypadToOBS.Action.ToggleVirtualCam");
	case JoypadActionType::ToggleStudioMode:
		return L("JoypadToOBS.Action.ToggleStudioMode");
	case JoypadActionType::TransitionToProgram:
		return L("JoypadToOBS.Action.TransitionToProgram");
	case JoypadActionType::StartReplayBuffer:
		return L("JoypadToOBS.Action.StartReplayBuffer");
	case JoypadActionType::StopReplayBuffer:
		return L("JoypadToOBS.Action.StopReplayBuffer");
	case JoypadActionType::ToggleReplayBuffer:
		return L("JoypadToOBS.Action.ToggleReplayBuffer");
	case JoypadActionType::SaveReplayBuffer:
		return L("JoypadToOBS.Action.SaveReplayBuffer");
	case JoypadActionType::SetFilterProperty:
		return L("JoypadToOBS.Action.SetFilterProperty");
	case JoypadActionType::AdjustFilterProperty:
		return L("JoypadToOBS.Action.AdjustFilterProperty");
	case JoypadActionType::SourceTransform:
		return L("JoypadToOBS.Action.SourceTransform");
	case JoypadActionType::Screenshot:
		return L("JoypadToOBS.Action.Screenshot");
	default:
		return L("JoypadToOBS.Common.Unknown");
	}
}

QString screenshot_target_to_text(JoypadScreenshotTarget target)
{
	switch (target) {
	case JoypadScreenshotTarget::Program:
		return L("JoypadToOBS.ScreenshotTarget.Program");
	case JoypadScreenshotTarget::Source:
		return L("JoypadToOBS.ScreenshotTarget.Source");
	default:
		return L("JoypadToOBS.Common.Unknown");
	}
}

QString source_transform_to_text(JoypadSourceTransformOp op)
{
	switch (op) {
	case JoypadSourceTransformOp::FlipHorizontal:
		return L("JoypadToOBS.Transform.FlipHorizontal");
	case JoypadSourceTransformOp::FlipVertical:
		return L("JoypadToOBS.Transform.FlipVertical");
	case JoypadSourceTransformOp::AlignLeft:
		return L("JoypadToOBS.Transform.AlignLeft");
	case JoypadSourceTransformOp::AlignRight:
		return L("JoypadToOBS.Transform.AlignRight");
	case JoypadSourceTransformOp::AlignTop:
		return L("JoypadToOBS.Transform.AlignTop");
	case JoypadSourceTransformOp::AlignBottom:
		return L("JoypadToOBS.Transform.AlignBottom");
	case JoypadSourceTransformOp::AlignTopLeft:
		return L("JoypadToOBS.Transform.AlignTopLeft");
	case JoypadSourceTransformOp::AlignTopRight:
		return L("JoypadToOBS.Transform.AlignTopRight");
	case JoypadSourceTransformOp::AlignBottomLeft:
		return L("JoypadToOBS.Transform.AlignBottomLeft");
	case JoypadSourceTransformOp::AlignBottomRight:
		return L("JoypadToOBS.Transform.AlignBottomRight");
	case JoypadSourceTransformOp::AlignCenterLeft:
		return L("JoypadToOBS.Transform.AlignCenterLeft");
	case JoypadSourceTransformOp::AlignCenterRight:
		return L("JoypadToOBS.Transform.AlignCenterRight");
	case JoypadSourceTransformOp::Rotate90CW:
		return L("JoypadToOBS.Transform.Rotate90CW");
	case JoypadSourceTransformOp::Rotate90CCW:
		return L("JoypadToOBS.Transform.Rotate90CCW");
	case JoypadSourceTransformOp::Rotate180:
		return L("JoypadToOBS.Transform.Rotate180");
	case JoypadSourceTransformOp::CenterToScreen:
		return L("JoypadToOBS.Transform.CenterToScreen");
	case JoypadSourceTransformOp::FitToScreen:
		return L("JoypadToOBS.Transform.FitToScreen");
	case JoypadSourceTransformOp::StretchToScreen:
		return L("JoypadToOBS.Transform.StretchToScreen");
	default:
		return L("JoypadToOBS.Common.Unknown");
	}
}

QString binding_details(const JoypadBinding &binding)
{
	switch (binding.action) {
	case JoypadActionType::SetSourceVisibility:
	case JoypadActionType::SetSourceMute:
		return binding.bool_value ? L("JoypadToOBS.Common.On") : L("JoypadToOBS.Common.Off");
	case JoypadActionType::SetSourceVolume:
		return L("JoypadToOBS.Common.DbValue").arg(binding.volume_value, 0, 'f', 1);
	case JoypadActionType::AdjustSourceVolume:
		if (binding.volume_value >= 0.0) {
			return L("JoypadToOBS.Common.PositiveValue").arg(binding.volume_value, 0, 'f', 1) + " dB";
		}
		return L("JoypadToOBS.Common.DbValue").arg(binding.volume_value, 0, 'f', 1);
	case JoypadActionType::SetSourceVolumePercent:
		return QString::number(binding.slider_gamma, 'f', 2) + " " + L("JoypadToOBS.Common.MultiplierSuffix");
	case JoypadActionType::SetFilterEnabled:
		return binding.bool_value ? L("JoypadToOBS.Common.On") : L("JoypadToOBS.Common.Off");
	case JoypadActionType::SetFilterProperty:
		if (binding.filter_property_type == OBS_PROPERTY_BOOL) {
			return binding.bool_value ? L("JoypadToOBS.Common.On") : L("JoypadToOBS.Common.Off");
		}
		if (binding.filter_property_type == OBS_PROPERTY_INT) {
			return QString::number((int)std::llround(binding.filter_property_value));
		}
		if (binding.filter_property_type == OBS_PROPERTY_FLOAT) {
			return QString::number(binding.filter_property_value, 'f', 3);
		}
		if (binding.filter_property_type == OBS_PROPERTY_LIST) {
			if (!binding.filter_property_list_string.empty()) {
				return QString::fromStdString(binding.filter_property_list_string);
			}
			if (binding.filter_property_list_format == OBS_COMBO_FORMAT_INT) {
				return QString::number(binding.filter_property_list_int);
			}
			if (binding.filter_property_list_format == OBS_COMBO_FORMAT_FLOAT) {
				return QString::number(binding.filter_property_list_float, 'f', 3);
			}
		}
		return QString::fromStdString(binding.filter_property_name);
	case JoypadActionType::AdjustFilterProperty:
		if (binding.volume_value >= 0.0) {
			return L("JoypadToOBS.Common.PositiveValue").arg(binding.volume_value, 0, 'f', 3);
		}
		return QString::number(binding.volume_value, 'f', 3);
	case JoypadActionType::SourceTransform:
		return source_transform_to_text(binding.source_transform_op);
	case JoypadActionType::Screenshot:
		return screenshot_target_to_text(binding.screenshot_target);
	default:
		return QString();
	}
}

std::vector<std::string> get_scene_names()
{
	std::vector<std::string> names;
	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);

	for (size_t i = 0; i < scenes.sources.num; ++i) {
		obs_source_t *source = scenes.sources.array[i];
		if (!source) {
			continue;
		}
		const char *name = obs_source_get_name(source);
		if (name && *name) {
			names.emplace_back(name);
		}
	}

	obs_frontend_source_list_free(&scenes);
	return names;
}

std::vector<std::string> get_source_names()
{
	std::vector<std::string> names;
	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			if (!source) {
				return true;
			}
			auto *list = static_cast<std::vector<std::string> *>(data);
			const char *name = obs_source_get_name(source);
			if (name && *name) {
				list->emplace_back(name);
			}
			return true;
		},
		&names);

	std::sort(names.begin(), names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());
	return names;
}

struct SourceItem {
	std::string name;
	std::string type_id;
	bool has_audio = false;
	bool has_video = false;
	bool is_media = false;
};

std::vector<SourceItem> get_sources_for_action(JoypadActionType action)
{
	std::vector<SourceItem> items;
	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			if (!source) {
				return true;
			}
			auto *list = static_cast<std::vector<SourceItem> *>(data);
			const char *name = obs_source_get_name(source);
			const char *type_id = obs_source_get_id(source);
			if (!name || !*name) {
				return true;
			}

			SourceItem item;
			item.name = name;
			item.type_id = type_id ? type_id : "";
			const uint32_t output_flags = obs_source_get_output_flags(source);
			item.has_audio = (output_flags & OBS_SOURCE_AUDIO) != 0;
			item.has_video = (output_flags & OBS_SOURCE_VIDEO) != 0;
			item.is_media = obs_source_media_get_state(source) != OBS_MEDIA_STATE_NONE;
			list->push_back(item);
			return true;
		},
		&items);

	auto is_audio_action =
		(action == JoypadActionType::ToggleSourceMute) || (action == JoypadActionType::SetSourceMute) ||
		(action == JoypadActionType::SetSourceVolume) || (action == JoypadActionType::AdjustSourceVolume) ||
		(action == JoypadActionType::SetSourceVolumePercent);
	auto is_media_action = (action == JoypadActionType::MediaPlayPause) ||
			       (action == JoypadActionType::MediaRestart) || (action == JoypadActionType::MediaStop);
	auto is_screenshot_source_action = (action == JoypadActionType::Screenshot);

	items.erase(
		std::remove_if(items.begin(), items.end(),
			       [is_audio_action, is_media_action, is_screenshot_source_action](const SourceItem &item) {
				       if (is_media_action) {
					       return !item.is_media;
				       }
				       if (is_audio_action) {
					       return !item.has_audio;
				       }
				       if (is_screenshot_source_action) {
					       return !item.has_video;
				       }
				       return false;
			       }),
		items.end());

	std::sort(items.begin(), items.end(), [](const SourceItem &a, const SourceItem &b) { return a.name < b.name; });
	items.erase(std::unique(items.begin(), items.end(),
				[](const SourceItem &a, const SourceItem &b) { return a.name == b.name; }),
		    items.end());

	return items;
}

std::vector<std::string> get_filter_names_for_source(const std::string &source_name)
{
	std::vector<std::string> names;
	if (source_name.empty()) {
		return names;
	}
	obs_source_t *source = obs_get_source_by_name(source_name.c_str());
	if (!source) {
		return names;
	}
	obs_source_enum_filters(
		source,
		[](obs_source_t *parent, obs_source_t *filter, void *data) {
			(void)parent;
			if (!filter) {
				return;
			}
			auto *list = static_cast<std::vector<std::string> *>(data);
			const char *name = obs_source_get_name(filter);
			if (name && *name) {
				list->emplace_back(name);
			}
		},
		&names);
	obs_source_release(source);
	return names;
}

struct FilterPropertyListItem {
	std::string name;
	std::string string_value;
	long long int_value = 0;
	double float_value = 0.0;
};

struct FilterPropertyInfo {
	std::string name;
	std::string description;
	obs_property_type type = OBS_PROPERTY_INVALID;
	double min_value = 0.0;
	double max_value = 1.0;
	obs_combo_format list_format = OBS_COMBO_FORMAT_INVALID;
	std::vector<FilterPropertyListItem> list_items;
};

std::vector<FilterPropertyInfo> get_filter_properties_for_source_filter(const std::string &source_name,
									const std::string &filter_name)
{
	std::vector<FilterPropertyInfo> infos;
	if (source_name.empty() || filter_name.empty()) {
		return infos;
	}

	obs_source_t *source = obs_get_source_by_name(source_name.c_str());
	if (!source) {
		return infos;
	}
	obs_source_t *filter = obs_source_get_filter_by_name(source, filter_name.c_str());
	obs_source_release(source);
	if (!filter) {
		return infos;
	}

	obs_properties_t *props = obs_source_properties(filter);
	obs_source_release(filter);
	if (!props) {
		return infos;
	}

	for (obs_property_t *prop = obs_properties_first(props); prop; obs_property_next(&prop)) {
		const obs_property_type type = obs_property_get_type(prop);
		if (type != OBS_PROPERTY_BOOL && type != OBS_PROPERTY_INT && type != OBS_PROPERTY_FLOAT &&
		    type != OBS_PROPERTY_LIST) {
			continue;
		}

		FilterPropertyInfo info;
		const char *name = obs_property_name(prop);
		const char *desc = obs_property_description(prop);
		if (!name || !*name) {
			continue;
		}
		info.name = name;
		info.description = (desc && *desc) ? desc : name;
		info.type = type;

		if (type == OBS_PROPERTY_INT) {
			info.min_value = (double)obs_property_int_min(prop);
			info.max_value = (double)obs_property_int_max(prop);
		} else if (type == OBS_PROPERTY_FLOAT) {
			info.min_value = obs_property_float_min(prop);
			info.max_value = obs_property_float_max(prop);
		} else if (type == OBS_PROPERTY_LIST) {
			info.list_format = obs_property_list_format(prop);
			const size_t count = obs_property_list_item_count(prop);
			for (size_t i = 0; i < count; ++i) {
				FilterPropertyListItem item;
				const char *item_name = obs_property_list_item_name(prop, i);
				item.name = (item_name && *item_name) ? item_name : "";
				if (info.list_format == OBS_COMBO_FORMAT_INT) {
					item.int_value = obs_property_list_item_int(prop, i);
				} else if (info.list_format == OBS_COMBO_FORMAT_FLOAT) {
					item.float_value = obs_property_list_item_float(prop, i);
				} else {
					const char *item_value = obs_property_list_item_string(prop, i);
					item.string_value = (item_value && *item_value) ? item_value : "";
				}
				info.list_items.push_back(item);
			}
		}

		infos.push_back(std::move(info));
	}

	obs_properties_destroy(props);
	return infos;
}

QString input_label_from_event(const JoypadEvent &event)
{
	if (event.is_axis) {
		const QString dir = event.axis_value >= 0.0 ? QStringLiteral("+") : QStringLiteral("-");
		return L("JoypadToOBS.Common.AxisNumber").arg(event.axis_index + 1).arg(dir);
	}
	return L("JoypadToOBS.Common.ButtonNumber").arg(event.button);
}

QString input_label_from_binding(const JoypadBinding &binding)
{
	if (binding.input_type == JoypadInputType::Axis) {
		QString dir = QStringLiteral("+");
		if (binding.axis_direction == JoypadAxisDirection::Negative) {
			dir = QStringLiteral("-");
		} else if (binding.axis_direction == JoypadAxisDirection::Both) {
			dir = QStringLiteral("+/-");
		}
		return L("JoypadToOBS.Common.AxisNumber").arg(binding.axis_index + 1).arg(dir);
	}
	if (!binding.button_combo.empty()) {
		QStringList parts;
		for (const auto &entry : binding.button_combo) {
			parts.push_back(combo_entry_to_text(entry));
		}
		return parts.join(QStringLiteral(" + "));
	}
	return L("JoypadToOBS.Common.ButtonNumber").arg(binding.button);
}

double map_axis_raw_to_percent_for_test(const JoypadBinding &binding, double raw)
{
	double minv = binding.axis_min_value;
	double maxv = binding.axis_max_value;
	if (maxv <= minv) {
		minv = 0.0;
		maxv = 1024.0;
	}
	double percent = ((raw - minv) / (maxv - minv)) * 100.0;
	if (binding.axis_inverted) {
		percent = 100.0 - percent;
	}
	percent = std::clamp(percent, 0.0, 100.0);
	double base = std::clamp(percent / 100.0, 0.0, 1.0);
	double gamma = binding.slider_gamma > 0.0 ? binding.slider_gamma : 0.6;
	gamma = std::clamp(gamma, 0.1, 50.0);
	double curved = std::pow(base, gamma);
	return std::clamp(curved * 100.0, 0.0, 100.0);
}

double map_axis_raw_to_range_for_test(const JoypadBinding &binding, double raw, double target_min, double target_max)
{
	double minv = binding.axis_min_value;
	double maxv = binding.axis_max_value;
	if (maxv <= minv) {
		minv = 0.0;
		maxv = 1024.0;
	}
	double normalized = (raw - minv) / (maxv - minv);
	normalized = std::clamp(normalized, 0.0, 1.0);
	if (binding.axis_inverted || binding.axis_direction == JoypadAxisDirection::Negative) {
		normalized = 1.0 - normalized;
	}
	return target_min + normalized * (target_max - target_min);
}

class JoypadBindingDialog : public QDialog {
public:
	JoypadBindingDialog(QWidget *parent, JoypadConfigStore *config, JoypadInputManager *input,
			    const JoypadBinding *existing = nullptr)
		: QDialog(parent),
		  config_(config),
		  input_(input),
		  existing_(existing)
	{
		g_binding_dialog_open_count.fetch_add(1, std::memory_order_relaxed);
		setWindowTitle(L("JoypadToOBS.Dialog.AddTitle"));
		setModal(true);
		setSizeGripEnabled(true);

		auto *layout = new QVBoxLayout(this);
		layout->setSizeConstraint(QLayout::SetDefaultConstraint);
		layout->setContentsMargins(10, 8, 10, 10);
		layout->setSpacing(8);

		auto *description = new QLabel(L("JoypadToOBS.Dialog.AddDescription"), this);
		description->setWordWrap(true);
		description->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
		layout->addWidget(description);

		auto *device_group = new QGroupBox(L("JoypadToOBS.Group.DeviceButton"));
		device_group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
		auto *device_layout = new QGridLayout(device_group);
		device_layout->setContentsMargins(8, 8, 8, 8);
		device_layout->setHorizontalSpacing(6);
		device_layout->setVerticalSpacing(4);

		device_combo_ = new QComboBox(device_group);
		PopulateDeviceCombo();
		device_combo_->hide();

		axis_name_label_ = new QLabel(L("JoypadToOBS.Field.Axis"), device_group);
		button_label_ = new QLabel(L("JoypadToOBS.Common.NoButtonSelected"), device_group);
		listen_button_ = new QPushButton(add_listen_button_text(), device_group);
		button_combo_frame_ = new QFrame(device_group);
		button_combo_frame_->setFrameShape(QFrame::StyledPanel);
		button_combo_frame_->setFrameShadow(QFrame::Sunken);
		button_combo_frame_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
		auto *button_combo_layout = new QVBoxLayout(button_combo_frame_);
		button_combo_layout->setContentsMargins(6, 6, 6, 6);
		button_combo_layout->setSpacing(0);
		button_combo_list_ = new QListWidget(button_combo_frame_);
		button_combo_list_->setSelectionMode(QAbstractItemView::NoSelection);
		button_combo_list_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		button_combo_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		button_combo_list_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
		button_combo_layout->addWidget(button_combo_list_);
		clear_combo_button_ = new QPushButton(L("JoypadToOBS.Button.ClearButtons"), device_group);
		device_hint_label_ = new QLabel(L("JoypadToOBS.DeviceHint.ComboButtonsOnly"), device_group);
		device_hint_label_->setWordWrap(true);
		device_hint_label_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

		axis_name_label_->hide();
		button_label_->hide();
		device_layout->addWidget(device_hint_label_, 0, 0, 1, 3);
		device_layout->addWidget(button_combo_frame_, 1, 0, 1, 3);
		device_layout->addWidget(listen_button_, 2, 0);
		device_layout->addWidget(clear_combo_button_, 2, 2);
		device_layout->addWidget(axis_name_label_, 3, 0);
		device_layout->addWidget(button_label_, 3, 1, 1, 2);

		axis_value_label_ = new QLabel(L("JoypadToOBS.Field.AxisValue"), device_group);
		axis_value_slider_ = new QSlider(Qt::Horizontal, device_group);
		axis_value_slider_->setRange(0, 1024);
		axis_value_slider_->setValue(0);
		axis_value_slider_->setEnabled(false);
		axis_live_value_label_ = new QLabel("0.00", device_group);

		axis_threshold_label_ = new QLabel(L("JoypadToOBS.Field.AxisThreshold"), device_group);
		axis_threshold_spin_ = new QDoubleSpinBox(device_group);
		axis_threshold_spin_->setRange(0.0, 0.95);
		axis_threshold_spin_->setSingleStep(0.01);
		axis_threshold_spin_->setDecimals(2);
		axis_threshold_spin_->setValue(0.10);
		axis_min_per_second_label_ = new QLabel(L("JoypadToOBS.Field.AxisMinPerSecond"), device_group);
		axis_min_per_second_spin_ = new QDoubleSpinBox(device_group);
		axis_min_per_second_spin_->setRange(1.0, 60.0);
		axis_min_per_second_spin_->setSingleStep(0.5);
		axis_min_per_second_spin_->setDecimals(1);
		axis_min_per_second_spin_->setValue(2.5);
		axis_min_per_second_spin_->setSuffix(" /s");
		axis_max_per_second_label_ = new QLabel(L("JoypadToOBS.Field.AxisMaxPerSecond"), device_group);
		axis_max_per_second_spin_ = new QDoubleSpinBox(device_group);
		axis_max_per_second_spin_->setRange(1.0, 60.0);
		axis_max_per_second_spin_->setSingleStep(0.5);
		axis_max_per_second_spin_->setDecimals(1);
		axis_max_per_second_spin_->setValue(20.0);
		axis_max_per_second_spin_->setSuffix(" /s");
		axis_both_checkbox_ = new QCheckBox(L("JoypadToOBS.Field.AxisBothDirections"), device_group);
		axis_both_checkbox_->setChecked(true);
		axis_min_label_ = new QLabel(L("JoypadToOBS.Field.AxisMinValue"), device_group);
		axis_max_label_ = new QLabel(L("JoypadToOBS.Field.AxisMaxValue"), device_group);
		axis_set_min_button_ = new QPushButton(L("JoypadToOBS.Button.SetMin"), device_group);
		axis_set_max_button_ = new QPushButton(L("JoypadToOBS.Button.SetMax"), device_group);
		axis_min_label_->setText(L("JoypadToOBS.Field.AxisMinValue") + ": 0");
		axis_max_label_->setText(L("JoypadToOBS.Field.AxisMaxValue") + ": 1024");

		device_layout->addWidget(axis_value_label_, 4, 0);
		device_layout->addWidget(axis_value_slider_, 4, 1);
		device_layout->addWidget(axis_live_value_label_, 4, 2);
		device_layout->addWidget(axis_threshold_label_, 5, 0);
		device_layout->addWidget(axis_threshold_spin_, 5, 1, 1, 2);
		device_layout->addWidget(axis_min_per_second_label_, 6, 0);
		device_layout->addWidget(axis_min_per_second_spin_, 6, 1, 1, 2);
		device_layout->addWidget(axis_max_per_second_label_, 7, 0);
		device_layout->addWidget(axis_max_per_second_spin_, 7, 1, 1, 2);
		device_layout->addWidget(axis_both_checkbox_, 8, 0, 1, 2);
		device_layout->addWidget(axis_min_label_, 9, 0);
		device_layout->addWidget(axis_set_min_button_, 9, 1);
		device_layout->addWidget(axis_max_label_, 10, 0);
		device_layout->addWidget(axis_set_max_button_, 10, 1);

		layout->addWidget(device_group);

		auto *target_group = new QGroupBox(L("JoypadToOBS.Group.Target"));
		target_group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
		auto *target_layout = new QGridLayout(target_group);
		target_layout->setContentsMargins(8, 8, 8, 8);
		target_layout->setHorizontalSpacing(6);
		target_layout->setVerticalSpacing(4);

		use_current_scene_ = new QCheckBox(L("JoypadToOBS.Field.UseCurrentScene"), target_group);
		scene_combo_ = new QComboBox(target_group);
		source_combo_ = new QComboBox(target_group);
		filter_combo_ = new QComboBox(target_group);
		filter_property_combo_ = new QComboBox(target_group);

		target_layout->addWidget(use_current_scene_, 0, 0, 1, 2);
		target_layout->addWidget(new QLabel(L("JoypadToOBS.Field.Scene")), 1, 0);
		target_layout->addWidget(scene_combo_, 1, 1);
		target_layout->addWidget(new QLabel(L("JoypadToOBS.Field.Source")), 2, 0);
		target_layout->addWidget(source_combo_, 2, 1);
		target_layout->addWidget(new QLabel(L("JoypadToOBS.Field.Filter")), 3, 0);
		target_layout->addWidget(filter_combo_, 3, 1);
		filter_property_label_ = new QLabel(L("JoypadToOBS.Field.FilterProperty"), target_group);
		target_layout->addWidget(filter_property_label_, 4, 0);
		target_layout->addWidget(filter_property_combo_, 4, 1);
		transform_op_label_ = new QLabel(L("JoypadToOBS.Field.Transform"), target_group);
		transform_op_combo_ = new QComboBox(target_group);
		transform_op_combo_->addItem(source_transform_to_text(JoypadSourceTransformOp::FlipHorizontal),
					     (int)JoypadSourceTransformOp::FlipHorizontal);
		transform_op_combo_->addItem(source_transform_to_text(JoypadSourceTransformOp::FlipVertical),
					     (int)JoypadSourceTransformOp::FlipVertical);
		transform_op_combo_->addItem(source_transform_to_text(JoypadSourceTransformOp::AlignLeft),
					     (int)JoypadSourceTransformOp::AlignLeft);
		transform_op_combo_->addItem(source_transform_to_text(JoypadSourceTransformOp::AlignRight),
					     (int)JoypadSourceTransformOp::AlignRight);
		transform_op_combo_->addItem(source_transform_to_text(JoypadSourceTransformOp::AlignTop),
					     (int)JoypadSourceTransformOp::AlignTop);
		transform_op_combo_->addItem(source_transform_to_text(JoypadSourceTransformOp::AlignBottom),
					     (int)JoypadSourceTransformOp::AlignBottom);
		transform_op_combo_->addItem(source_transform_to_text(JoypadSourceTransformOp::AlignTopLeft),
					     (int)JoypadSourceTransformOp::AlignTopLeft);
		transform_op_combo_->addItem(source_transform_to_text(JoypadSourceTransformOp::AlignTopRight),
					     (int)JoypadSourceTransformOp::AlignTopRight);
		transform_op_combo_->addItem(source_transform_to_text(JoypadSourceTransformOp::AlignBottomLeft),
					     (int)JoypadSourceTransformOp::AlignBottomLeft);
		transform_op_combo_->addItem(source_transform_to_text(JoypadSourceTransformOp::AlignBottomRight),
					     (int)JoypadSourceTransformOp::AlignBottomRight);
		transform_op_combo_->addItem(source_transform_to_text(JoypadSourceTransformOp::AlignCenterLeft),
					     (int)JoypadSourceTransformOp::AlignCenterLeft);
		transform_op_combo_->addItem(source_transform_to_text(JoypadSourceTransformOp::AlignCenterRight),
					     (int)JoypadSourceTransformOp::AlignCenterRight);
		transform_op_combo_->addItem(source_transform_to_text(JoypadSourceTransformOp::Rotate90CW),
					     (int)JoypadSourceTransformOp::Rotate90CW);
		transform_op_combo_->addItem(source_transform_to_text(JoypadSourceTransformOp::Rotate90CCW),
					     (int)JoypadSourceTransformOp::Rotate90CCW);
		transform_op_combo_->addItem(source_transform_to_text(JoypadSourceTransformOp::Rotate180),
					     (int)JoypadSourceTransformOp::Rotate180);
		transform_op_combo_->addItem(source_transform_to_text(JoypadSourceTransformOp::CenterToScreen),
					     (int)JoypadSourceTransformOp::CenterToScreen);
		transform_op_combo_->addItem(source_transform_to_text(JoypadSourceTransformOp::FitToScreen),
					     (int)JoypadSourceTransformOp::FitToScreen);
		transform_op_combo_->addItem(source_transform_to_text(JoypadSourceTransformOp::StretchToScreen),
					     (int)JoypadSourceTransformOp::StretchToScreen);
		target_layout->addWidget(transform_op_label_, 5, 0);
		target_layout->addWidget(transform_op_combo_, 5, 1);

		auto *action_group = new QGroupBox(L("JoypadToOBS.Group.Action"));
		action_group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
		auto *action_layout = new QGridLayout(action_group);
		action_layout->setContentsMargins(8, 8, 8, 8);
		action_layout->setHorizontalSpacing(6);
		action_layout->setVerticalSpacing(4);

		action_combo_ = new QComboBox(action_group);
		action_combo_->addItem(action_to_text(JoypadActionType::SwitchScene),
				       (int)JoypadActionType::SwitchScene);
		action_combo_->addItem(action_to_text(JoypadActionType::NextScene), (int)JoypadActionType::NextScene);
		action_combo_->addItem(action_to_text(JoypadActionType::PreviousScene),
				       (int)JoypadActionType::PreviousScene);
		action_combo_->addItem(action_to_text(JoypadActionType::ToggleSourceVisibility),
				       (int)JoypadActionType::ToggleSourceVisibility);
		action_combo_->addItem(action_to_text(JoypadActionType::SetSourceVisibility),
				       (int)JoypadActionType::SetSourceVisibility);
		action_combo_->addItem(action_to_text(JoypadActionType::ToggleSourceMute),
				       (int)JoypadActionType::ToggleSourceMute);
		action_combo_->addItem(action_to_text(JoypadActionType::SetSourceMute),
				       (int)JoypadActionType::SetSourceMute);
		action_combo_->addItem(action_to_text(JoypadActionType::SetSourceVolume),
				       (int)JoypadActionType::SetSourceVolume);
		action_combo_->addItem(action_to_text(JoypadActionType::AdjustSourceVolume),
				       (int)JoypadActionType::AdjustSourceVolume);
		action_combo_->addItem(action_to_text(JoypadActionType::SetSourceVolumePercent),
				       (int)JoypadActionType::SetSourceVolumePercent);
		action_combo_->addItem(action_to_text(JoypadActionType::MediaPlayPause),
				       (int)JoypadActionType::MediaPlayPause);
		action_combo_->addItem(action_to_text(JoypadActionType::MediaRestart),
				       (int)JoypadActionType::MediaRestart);
		action_combo_->addItem(action_to_text(JoypadActionType::MediaStop), (int)JoypadActionType::MediaStop);
		action_combo_->addItem(action_to_text(JoypadActionType::ToggleFilterEnabled),
				       (int)JoypadActionType::ToggleFilterEnabled);
		action_combo_->addItem(action_to_text(JoypadActionType::SetFilterEnabled),
				       (int)JoypadActionType::SetFilterEnabled);
		action_combo_->addItem(action_to_text(JoypadActionType::SetFilterProperty),
				       (int)JoypadActionType::SetFilterProperty);
		action_combo_->addItem(action_to_text(JoypadActionType::AdjustFilterProperty),
				       (int)JoypadActionType::AdjustFilterProperty);
		action_combo_->addItem(action_to_text(JoypadActionType::SourceTransform),
				       (int)JoypadActionType::SourceTransform);
		action_combo_->addItem(action_to_text(JoypadActionType::ToggleStreaming),
				       (int)JoypadActionType::ToggleStreaming);
		action_combo_->addItem(action_to_text(JoypadActionType::ToggleRecording),
				       (int)JoypadActionType::ToggleRecording);
		action_combo_->addItem(action_to_text(JoypadActionType::ToggleVirtualCam),
				       (int)JoypadActionType::ToggleVirtualCam);
		action_combo_->addItem(action_to_text(JoypadActionType::ToggleStudioMode),
				       (int)JoypadActionType::ToggleStudioMode);
		action_combo_->addItem(action_to_text(JoypadActionType::TransitionToProgram),
				       (int)JoypadActionType::TransitionToProgram);
		action_combo_->addItem(action_to_text(JoypadActionType::StartReplayBuffer),
				       (int)JoypadActionType::StartReplayBuffer);
		action_combo_->addItem(action_to_text(JoypadActionType::StopReplayBuffer),
				       (int)JoypadActionType::StopReplayBuffer);
		action_combo_->addItem(action_to_text(JoypadActionType::ToggleReplayBuffer),
				       (int)JoypadActionType::ToggleReplayBuffer);
		action_combo_->addItem(action_to_text(JoypadActionType::SaveReplayBuffer),
				       (int)JoypadActionType::SaveReplayBuffer);
		action_combo_->addItem(action_to_text(JoypadActionType::Screenshot), (int)JoypadActionType::Screenshot);

		bool_checkbox_ = new QCheckBox(L("JoypadToOBS.Common.Enable"), action_group);
		volume_spin_ = new QDoubleSpinBox(action_group);
		screenshot_target_label_ = new QLabel(L("JoypadToOBS.Field.ScreenshotTarget"), action_group);
		screenshot_target_combo_ = new QComboBox(action_group);
		screenshot_target_combo_->addItem(screenshot_target_to_text(JoypadScreenshotTarget::Program),
						  (int)JoypadScreenshotTarget::Program);
		screenshot_target_combo_->addItem(screenshot_target_to_text(JoypadScreenshotTarget::Source),
						  (int)JoypadScreenshotTarget::Source);
		filter_property_list_label_ = new QLabel(L("JoypadToOBS.Field.PropertyValue"), action_group);
		filter_property_list_combo_ = new QComboBox(action_group);
		volume_allow_above_unity_ = new QCheckBox(L("JoypadToOBS.Field.AllowAboveDb"), action_group);
		invert_axis_checkbox_ = new QCheckBox(L("JoypadToOBS.Field.InvertAxis"), action_group);
		test_mode_checkbox_ = new QCheckBox(L("JoypadToOBS.Field.TestMode"), action_group);
		volume_spin_->setRange(-60.0, 20.0);
		volume_spin_->setSingleStep(1.0);
		volume_spin_->setValue(0.0);
		volume_spin_->setSuffix(" dB");

		action_layout->addWidget(new QLabel(L("JoypadToOBS.Field.Action")), 0, 0);
		action_layout->addWidget(action_combo_, 0, 1);
		action_layout->addWidget(screenshot_target_label_, 1, 0);
		action_layout->addWidget(screenshot_target_combo_, 1, 1);
		action_layout->addWidget(bool_checkbox_, 2, 0, 1, 2);
		volume_label_ = new QLabel(L("JoypadToOBS.Field.Volume"), action_group);
		action_layout->addWidget(volume_label_, 3, 0);
		action_layout->addWidget(volume_spin_, 3, 1);
		action_layout->addWidget(filter_property_list_label_, 4, 0);
		action_layout->addWidget(filter_property_list_combo_, 4, 1);
		action_layout->addWidget(volume_allow_above_unity_, 5, 0, 1, 2);
		action_layout->addWidget(invert_axis_checkbox_, 6, 0, 1, 2);
		action_layout->addWidget(test_mode_checkbox_, 7, 0, 1, 2);

		layout->addWidget(action_group);
		layout->addWidget(target_group);
		layout->addStretch(1);

		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		layout->addWidget(buttons);

		connect(buttons, &QDialogButtonBox::accepted, this, &JoypadBindingDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, this, &JoypadBindingDialog::reject);
		connect(test_mode_checkbox_, &QCheckBox::toggled, this, [this](bool) { PublishTestBinding(); });
		connect(listen_button_, &QPushButton::clicked, this, &JoypadBindingDialog::OnListen);
		connect(clear_combo_button_, &QPushButton::clicked, this, [this]() {
			binding_.button_combo.clear();
			learned_event_ = JoypadEvent{};
			UpdateButtonComboUi();
			DisableTestModeOnConfigChange();
		});
		connect(action_combo_, &QComboBox::currentIndexChanged, this, &JoypadBindingDialog::UpdateActionUi);
		connect(action_combo_, &QComboBox::currentIndexChanged, this,
			[this](int) { DisableTestModeOnConfigChange(); });
		connect(device_combo_, &QComboBox::currentIndexChanged, this,
			[this](int) { DisableTestModeOnConfigChange(); });
		connect(scene_combo_, &QComboBox::currentIndexChanged, this,
			[this](int) { DisableTestModeOnConfigChange(); });
		connect(source_combo_, &QComboBox::currentIndexChanged, this,
			[this](int) { DisableTestModeOnConfigChange(); });
		connect(filter_combo_, &QComboBox::currentIndexChanged, this,
			[this](int) { DisableTestModeOnConfigChange(); });
		connect(filter_property_combo_, &QComboBox::currentIndexChanged, this,
			[this](int) { DisableTestModeOnConfigChange(); });
		connect(filter_property_combo_, &QComboBox::currentIndexChanged, this,
			[this](int) { UpdateActionUi(); });
		connect(filter_property_list_combo_, &QComboBox::currentIndexChanged, this,
			[this](int) { DisableTestModeOnConfigChange(); });
		connect(screenshot_target_combo_, &QComboBox::currentIndexChanged, this,
			[this](int) { DisableTestModeOnConfigChange(); });
		connect(screenshot_target_combo_, &QComboBox::currentIndexChanged, this,
			[this](int) { UpdateActionUi(); });
		connect(transform_op_combo_, &QComboBox::currentIndexChanged, this,
			[this](int) { DisableTestModeOnConfigChange(); });
		connect(bool_checkbox_, &QCheckBox::toggled, this, [this](bool) { DisableTestModeOnConfigChange(); });
		connect(use_current_scene_, &QCheckBox::toggled, this,
			[this](bool) { DisableTestModeOnConfigChange(); });
		connect(axis_both_checkbox_, &QCheckBox::toggled, this,
			[this](bool) { DisableTestModeOnConfigChange(); });
		connect(axis_threshold_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
			[this](double) { DisableTestModeOnConfigChange(); });
		connect(axis_min_per_second_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
			[this](double) { DisableTestModeOnConfigChange(); });
		connect(axis_max_per_second_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
			[this](double) { DisableTestModeOnConfigChange(); });
		connect(volume_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
			DisableTestModeOnConfigChange();
			if (CurrentAction() != JoypadActionType::SetSourceVolumePercent) {
				return;
			}
			binding_.slider_gamma = value;
			if (!learned_event_.is_axis) {
				return;
			}
			double percent = MapRawToPercent(last_axis_value_);
			double db = MapPercentToDb(percent);
			axis_value_slider_->setValue((int)percent);
			axis_live_value_label_->setText(L("JoypadToOBS.Common.PercentValue").arg(percent, 0, 'f', 0) +
							" " + L("JoypadToOBS.Common.DbValue").arg(db, 0, 'f', 1));
		});
		connect(invert_axis_checkbox_, &QCheckBox::toggled, this, [this](bool) {
			DisableTestModeOnConfigChange();
			if (CurrentAction() != JoypadActionType::SetSourceVolumePercent) {
				return;
			}
			if (!learned_event_.is_axis) {
				return;
			}
			double percent = MapRawToPercent(last_axis_value_);
			double db = MapPercentToDb(percent);
			axis_value_slider_->setValue((int)percent);
			axis_live_value_label_->setText(L("JoypadToOBS.Common.PercentValue").arg(percent, 0, 'f', 0) +
							" " + L("JoypadToOBS.Common.DbValue").arg(db, 0, 'f', 1));
		});
		connect(volume_allow_above_unity_, &QCheckBox::toggled, this, [this](bool checked) {
			binding_.allow_above_unity = checked;
			DisableTestModeOnConfigChange();
		});
		connect(source_combo_, &QComboBox::currentIndexChanged, this, &JoypadBindingDialog::ReloadFilters);
		connect(filter_combo_, &QComboBox::currentIndexChanged, this,
			[this](int) { ReloadFilterProperties(); });
		connect(axis_set_min_button_, &QPushButton::clicked, this, [this]() {
			binding_.axis_min_value = last_axis_value_;
			axis_min_label_->setText(L("JoypadToOBS.Field.AxisMinValue") + ": " +
						 QString::number(binding_.axis_min_value, 'f', 2));
			DisableTestModeOnConfigChange();
		});
		connect(axis_set_max_button_, &QPushButton::clicked, this, [this]() {
			binding_.axis_max_value = last_axis_value_;
			axis_max_label_->setText(L("JoypadToOBS.Field.AxisMaxValue") + ": " +
						 QString::number(binding_.axis_max_value, 'f', 2));
			DisableTestModeOnConfigChange();
		});
		if (input_) {
			axis_handler_id_ = input_->AddOnAxisChanged([this](const JoypadEvent &event) {
				if (!event.is_axis) {
					return;
				}
				if (!learned_event_.is_axis) {
					return;
				}
				if (event.axis_index != learned_event_.axis_index) {
					return;
				}
				if (!learned_event_.device_id.empty()) {
					const bool same_id = event.device_id == learned_event_.device_id;
					const bool same_stable = !learned_event_.device_stable_id.empty() &&
								 event.device_stable_id ==
									 learned_event_.device_stable_id;
					const bool same_type = !learned_event_.device_type_id.empty() &&
							       event.device_type_id == learned_event_.device_type_id;
					if (!same_id && !same_stable && !same_type) {
						return;
					}
				}
				QMetaObject::invokeMethod(
					this,
					[this, event]() {
						last_axis_value_ = event.axis_raw_value;
						if (CurrentAction() == JoypadActionType::SetSourceVolumePercent) {
							double percent = MapRawToPercent(event.axis_raw_value);
							double db = MapPercentToDb(percent);
							axis_value_slider_->setValue((int)percent);
							axis_live_value_label_->setText(
								L("JoypadToOBS.Common.PercentValue")
									.arg(percent, 0, 'f', 0) +
								" " +
								L("JoypadToOBS.Common.DbValue").arg(db, 0, 'f', 1));
						} else if (CurrentAction() == JoypadActionType::SetFilterProperty) {
							const FilterPropertyInfo *info = CurrentFilterPropertyInfo();
							if (info && (info->type == OBS_PROPERTY_INT ||
								     info->type == OBS_PROPERTY_FLOAT)) {
								double mapped = map_axis_raw_to_range_for_test(
									binding_, event.axis_raw_value, info->min_value,
									info->max_value);
								axis_value_slider_->setValue((int)event.axis_raw_value);
								axis_live_value_label_->setText(QString::number(
									mapped, 'f',
									info->type == OBS_PROPERTY_INT ? 0 : 3));
							} else {
								axis_value_slider_->setValue((int)event.axis_raw_value);
								axis_live_value_label_->setText(
									QString::number(last_axis_value_, 'f', 2));
							}
						} else {
							axis_value_slider_->setValue((int)event.axis_raw_value);
							axis_live_value_label_->setText(
								QString::number(last_axis_value_, 'f', 2));
						}
					},
					Qt::QueuedConnection);
			});
		}
		connect(use_current_scene_, &QCheckBox::toggled, this, [this](bool checked) {
			DisableTestModeOnConfigChange();
			if (checked) {
				obs_source_t *scene = obs_frontend_get_current_scene();
				if (scene) {
					scene_combo_->setCurrentText(obs_source_get_name(scene));
					obs_source_release(scene);
				}
			}
			scene_combo_->setEnabled(!checked);
		});

		ReloadTargets();
		ReloadFilters();
		if (existing) {
			ApplyBinding(*existing);
		} else {
			binding_.enabled = true;
			UpdateButtonComboUi();
			UpdateActionUi();
			UpdateAxisUi(false);
		}
		PublishTestBinding();
	}

	~JoypadBindingDialog() override
	{
		{
			std::lock_guard<std::mutex> lock(g_dialog_test_mutex);
			g_dialog_test_state.enabled = false;
		}
		if (input_) {
			input_->CancelLearn();
			if (axis_handler_id_ > 0) {
				input_->RemoveOnAxisChanged(axis_handler_id_);
				axis_handler_id_ = 0;
			}
		}
		g_binding_dialog_open_count.fetch_sub(1, std::memory_order_relaxed);
	}

	JoypadBinding Binding() const { return binding_; }

protected:
	void accept() override
	{
		if (!ReadBinding(true)) {
			return;
		}
		QDialog::accept();
	}

private:
	void DisableTestModeOnConfigChange()
	{
		if (test_mode_checkbox_ && test_mode_checkbox_->isChecked()) {
			test_mode_checkbox_->setChecked(false);
		}
	}

	void PublishTestBinding()
	{
		if (!test_mode_checkbox_ || !test_mode_checkbox_->isChecked()) {
			std::lock_guard<std::mutex> lock(g_dialog_test_mutex);
			g_dialog_test_state.enabled = false;
			g_dialog_axis_last_dispatch.clear();
			return;
		}
		if (!ReadBinding(false)) {
			std::lock_guard<std::mutex> lock(g_dialog_test_mutex);
			g_dialog_test_state.enabled = false;
			g_dialog_axis_last_dispatch.clear();
			return;
		}
		JoypadBinding test = binding_;
		if (test.action == JoypadActionType::SetSourceVolumePercent) {
			double percent = learned_event_.is_axis ? MapRawToPercent(last_axis_value_) : 50.0;
			test.volume_value = std::clamp(percent, 0.0, 100.0);
		} else if (test.action == JoypadActionType::SetFilterProperty &&
			   (test.filter_property_type == OBS_PROPERTY_INT ||
			    test.filter_property_type == OBS_PROPERTY_FLOAT)) {
			double target_min = test.filter_property_min;
			double target_max = test.filter_property_max;
			if (target_max <= target_min) {
				target_min = 0.0;
				target_max = 1.0;
			}
			test.filter_property_value =
				map_axis_raw_to_range_for_test(test, last_axis_value_, target_min, target_max);
		}
		std::lock_guard<std::mutex> lock(g_dialog_test_mutex);
		g_dialog_test_state.enabled = true;
		g_dialog_test_state.binding = test;
		g_dialog_axis_last_dispatch.clear();
	}

	void PopulateDeviceCombo()
	{
		device_combo_->blockSignals(true);
		device_combo_->clear();
		device_combo_->addItem(L("JoypadToOBS.Common.AnyDevice"), QString());
		device_combo_->setItemData(0, QString(), kDeviceStableIdRole);
		device_combo_->setItemData(0, QString(), kDeviceTypeIdRole);
		if (input_) {
			// Ensure the combobox reflects devices connected right now.
			input_->RefreshDevices();
			for (const auto &device : input_->GetDevices()) {
				const int idx = device_combo_->count();
				device_combo_->addItem(device_label_from_info(device),
						       QString::fromStdString(device.id));
				device_combo_->setItemData(idx, QString::fromStdString(device.stable_id),
							   kDeviceStableIdRole);
				device_combo_->setItemData(idx, QString::fromStdString(device.type_id),
							   kDeviceTypeIdRole);
			}
		}
		device_combo_->blockSignals(false);
	}

	double MapRawToPercent(double raw) const
	{
		double minv = binding_.axis_min_value;
		double maxv = binding_.axis_max_value;
		if (maxv <= minv) {
			minv = 0.0;
			maxv = 1024.0;
		}
		double percent = ((raw - minv) / (maxv - minv)) * 100.0;
		if (invert_axis_checkbox_->isChecked()) {
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
		double gamma = binding_.slider_gamma > 0.0 ? binding_.slider_gamma : 1.0;
		gamma = std::clamp(gamma, 0.1, 50.0);
		double curved = std::pow(base, gamma);
		return std::clamp(curved * 100.0, 0.0, 100.0);
	}

	double MapPercentToDb(double percent) const
	{
		if (percent < 0.0) {
			percent = 0.0;
		}
		if (percent > 100.0) {
			percent = 100.0;
		}
		return -60.0 + (percent / 100.0) * 60.0;
	}

	void UpdateAxisUi(bool visible)
	{
		bool hide_axis_options = (CurrentAction() == JoypadActionType::SetSourceVolumePercent);
		if (CurrentAction() == JoypadActionType::SetFilterProperty) {
			const FilterPropertyInfo *info = CurrentFilterPropertyInfo();
			if (info && (info->type == OBS_PROPERTY_INT || info->type == OBS_PROPERTY_FLOAT)) {
				hide_axis_options = true;
			}
		}
		if (hide_axis_options) {
			axis_value_slider_->setRange(0, 100);
		} else {
			int minv = (int)binding_.axis_min_value;
			int maxv = (int)binding_.axis_max_value;
			if (maxv <= minv) {
				minv = 0;
				maxv = 1024;
			}
			axis_value_slider_->setRange(minv, maxv);
		}
		axis_value_label_->setVisible(visible);
		axis_value_slider_->setVisible(visible);
		axis_live_value_label_->setVisible(visible);
		axis_name_label_->setVisible(visible);
		axis_threshold_label_->setVisible(visible && !hide_axis_options);
		axis_threshold_spin_->setVisible(visible && !hide_axis_options);
		axis_min_per_second_label_->setVisible(visible && !hide_axis_options);
		axis_min_per_second_spin_->setVisible(visible && !hide_axis_options);
		axis_max_per_second_label_->setVisible(visible && !hide_axis_options);
		axis_max_per_second_spin_->setVisible(visible && !hide_axis_options);
		axis_both_checkbox_->setVisible(visible && !hide_axis_options);
		invert_axis_checkbox_->setVisible(visible);
		axis_min_label_->setVisible(visible && hide_axis_options);
		axis_max_label_->setVisible(visible && hide_axis_options);
		axis_set_min_button_->setVisible(visible && hide_axis_options);
		axis_set_max_button_->setVisible(visible && hide_axis_options);
		button_label_->setVisible(visible);
		button_combo_frame_->setVisible(!visible);
		button_combo_list_->setVisible(!visible);
		clear_combo_button_->setVisible(!visible);
		if (QLayout *lyt = layout()) {
			lyt->activate();
		}
		adjustSize();
	}

	void UpdateButtonComboUi()
	{
		button_combo_list_->clear();
		for (size_t i = 0; i < binding_.button_combo.size(); ++i) {
			const auto &entry = binding_.button_combo[i];
			auto *item = new QListWidgetItem(button_combo_list_);
			auto *row_widget = new QWidget(button_combo_list_);
			auto *row_layout = new QHBoxLayout(row_widget);
			row_layout->setContentsMargins(4, 2, 4, 2);
			row_layout->setSpacing(6);

			auto *label = new QLabel(combo_entry_to_text(entry), row_widget);
			label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
			auto *remove_button = new QToolButton(row_widget);
			remove_button->setText("X");
			remove_button->setAutoRaise(true);
			remove_button->setToolTip(L("JoypadToOBS.Button.RemoveSelected"));
			connect(remove_button, &QToolButton::clicked, this, [this, i]() {
				if (i >= binding_.button_combo.size()) {
					return;
				}
				binding_.button_combo.erase(binding_.button_combo.begin() + (ptrdiff_t)i);
				if (binding_.button_combo.empty()) {
					learned_event_ = JoypadEvent{};
				}
				UpdateButtonComboUi();
				DisableTestModeOnConfigChange();
			});

			row_layout->addWidget(label);
			row_layout->addWidget(remove_button, 0, Qt::AlignTop);
			item->setSizeHint(row_widget->sizeHint());
			button_combo_list_->addItem(item);
			button_combo_list_->setItemWidget(item, row_widget);
		}
		int row_height = button_combo_list_->count() > 0 ? button_combo_list_->sizeHintForRow(0) : 0;
		if (row_height <= 0) {
			row_height = button_combo_list_->fontMetrics().height() + 8;
		}
		const int visible_rows = std::clamp((int)binding_.button_combo.size(), 1, 5);
		const int frame = button_combo_list_->frameWidth() * 2;
		const int padding = 4;
		const int target_height = frame + (row_height * visible_rows) + padding;
		button_combo_list_->setMinimumHeight(target_height);
		button_combo_list_->setMaximumHeight(target_height);
		clear_combo_button_->setEnabled(!binding_.button_combo.empty());
		if (learned_event_.is_axis) {
			button_label_->setText(input_label_from_event(learned_event_));
			return;
		}
		if (!binding_.button_combo.empty()) {
			button_label_->setText(input_label_from_binding(binding_));
			return;
		}
		if (learned_event_.button > 0) {
			button_label_->setText(input_label_from_event(learned_event_));
			return;
		}
		button_label_->setText(L("JoypadToOBS.Common.NoButtonSelected"));
	}

	void ApplyBinding(const JoypadBinding &binding)
	{
		binding_ = binding;
		if (binding_.input_type == JoypadInputType::Button && binding_.button_combo.empty() &&
		    binding_.button > 0) {
			JoypadButtonComboEntry entry;
			entry.device_id = binding_.device_id;
			entry.device_stable_id = binding_.device_stable_id;
			entry.device_type_id = binding_.device_type_id;
			entry.device_name = binding_.device_name;
			entry.button = binding_.button;
			binding_.button_combo.push_back(std::move(entry));
		}
		learned_event_.button = binding.button;
		learned_event_.is_axis = (binding.input_type == JoypadInputType::Axis);
		learned_event_.axis_index = binding.axis_index;
		learned_event_.axis_value = (binding.axis_direction == JoypadAxisDirection::Negative)
						    ? -binding.axis_threshold
						    : binding.axis_threshold;
		learned_event_.device_id = binding.device_id;
		learned_event_.device_stable_id = binding.device_stable_id;
		learned_event_.device_type_id = binding.device_type_id;
		learned_event_.device_name = binding.device_name;
		UpdateButtonComboUi();
		UpdateAxisUi(learned_event_.is_axis);
		if (learned_event_.is_axis) {
			last_axis_value_ = binding.axis_min_value;
			if (binding.action == JoypadActionType::SetSourceVolumePercent) {
				double percent = MapRawToPercent(last_axis_value_);
				double db = MapPercentToDb(percent);
				axis_value_slider_->setValue((int)percent);
				axis_live_value_label_->setText(
					L("JoypadToOBS.Common.PercentValue").arg(percent, 0, 'f', 0) + " " +
					L("JoypadToOBS.Common.DbValue").arg(db, 0, 'f', 1));
			} else {
				axis_value_slider_->setValue((int)binding.axis_min_value);
				axis_live_value_label_->setText(QString::number(last_axis_value_, 'f', 2));
			}
			axis_threshold_spin_->setValue(std::clamp(binding.axis_threshold, 0.0, 0.95));
			axis_min_per_second_spin_->setValue(std::clamp(binding.axis_min_per_second, 1.0, 60.0));
			axis_max_per_second_spin_->setValue(std::clamp(binding.axis_max_per_second, 1.0, 60.0));
			axis_both_checkbox_->setChecked(binding.axis_direction == JoypadAxisDirection::Both);
			invert_axis_checkbox_->setChecked(binding.axis_inverted ||
							  binding.axis_direction == JoypadAxisDirection::Negative);
			axis_min_label_->setText(L("JoypadToOBS.Field.AxisMinValue") + ": " +
						 QString::number(binding.axis_min_value, 'f', 2));
			axis_max_label_->setText(L("JoypadToOBS.Field.AxisMaxValue") + ": " +
						 QString::number(binding.axis_max_value, 'f', 2));
		}

		int device_index = device_combo_->findData(QString::fromStdString(binding.device_id), kDeviceIdRole);
		if (device_index < 0 && !binding.device_stable_id.empty()) {
			device_index = device_combo_->findData(QString::fromStdString(binding.device_stable_id),
							       kDeviceStableIdRole);
		}
		if (device_index < 0 && !binding.device_type_id.empty()) {
			device_index = device_combo_->findData(QString::fromStdString(binding.device_type_id),
							       kDeviceTypeIdRole);
		}
		if (device_index < 0 && !binding.device_id.empty()) {
			device_combo_->addItem(QString::fromStdString(binding.device_name),
					       QString::fromStdString(binding.device_id));
			const int idx = device_combo_->count() - 1;
			device_combo_->setItemData(idx, QString::fromStdString(binding.device_stable_id),
						   kDeviceStableIdRole);
			device_combo_->setItemData(idx, QString::fromStdString(binding.device_type_id),
						   kDeviceTypeIdRole);
			device_index = device_combo_->count() - 1;
		}
		if (device_index >= 0) {
			device_combo_->setCurrentIndex(device_index);
		}

		int action_index = action_combo_->findData((int)binding.action);
		if (action_index >= 0) {
			action_combo_->setCurrentIndex(action_index);
		}

		use_current_scene_->setChecked(binding.use_current_scene);
		scene_combo_->setCurrentText(QString::fromStdString(binding.scene_name));

		ReloadSourcesForAction(binding.action);
		int source_index = source_combo_->findData(QString::fromStdString(binding.source_name));
		if (source_index >= 0) {
			source_combo_->setCurrentIndex(source_index);
		}

		ReloadFilters();
		filter_combo_->setCurrentText(QString::fromStdString(binding.filter_name));
		ReloadFilterProperties();
		if (!binding.filter_property_name.empty()) {
			int property_idx =
				filter_property_combo_->findData(QString::fromStdString(binding.filter_property_name));
			if (property_idx >= 0) {
				filter_property_combo_->setCurrentIndex(property_idx);
			}
		}
		transform_op_combo_->setCurrentIndex(transform_op_combo_->findData((int)binding.source_transform_op));
		int screenshot_target_index = screenshot_target_combo_->findData((int)binding.screenshot_target);
		if (screenshot_target_index < 0) {
			screenshot_target_index =
				screenshot_target_combo_->findData((int)JoypadScreenshotTarget::Program);
		}
		if (screenshot_target_index >= 0) {
			screenshot_target_combo_->setCurrentIndex(screenshot_target_index);
		}

		bool_checkbox_->setChecked(binding.bool_value);
		volume_allow_above_unity_->setChecked(binding.allow_above_unity);
		if (binding.action == JoypadActionType::SetSourceVolumePercent) {
			volume_spin_->setValue(binding.slider_gamma);
		} else if (binding.action == JoypadActionType::SetFilterProperty) {
			volume_spin_->setValue(binding.filter_property_value);
		} else if (binding.action == JoypadActionType::AdjustFilterProperty) {
			volume_spin_->setValue(binding.volume_value);
		} else {
			volume_spin_->setValue(binding.volume_value);
		}

		UpdateActionUi();
	}
	void ReloadTargets()
	{
		scene_combo_->clear();

		for (const auto &name : get_scene_names()) {
			scene_combo_->addItem(QString::fromStdString(name));
		}
		ReloadSourcesForAction(CurrentAction());
	}

	void ReloadSourcesForAction(JoypadActionType action)
	{
		auto previous = source_combo_->currentData().toString();
		QSignalBlocker blocker(*source_combo_);
		source_combo_->clear();

		auto sources = get_sources_for_action(action);
		for (const auto &item : sources) {
			source_combo_->addItem(QString::fromStdString(item.name), QString::fromStdString(item.name));
		}

		int idx = source_combo_->findData(previous);
		if (idx >= 0) {
			source_combo_->setCurrentIndex(idx);
		}
	}

	void ReloadFilters()
	{
		auto previous = filter_combo_->currentText();
		QSignalBlocker blocker(*filter_combo_);
		filter_combo_->clear();
		auto names = get_filter_names_for_source(source_combo_->currentData().toString().toStdString());
		for (const auto &name : names) {
			filter_combo_->addItem(QString::fromStdString(name));
		}
		int idx = filter_combo_->findText(previous);
		if (idx >= 0) {
			filter_combo_->setCurrentIndex(idx);
		}
		ReloadFilterProperties();
	}

	const FilterPropertyInfo *CurrentFilterPropertyInfo() const
	{
		const QString selected = filter_property_combo_->currentData().toString();
		if (selected.isEmpty()) {
			return nullptr;
		}
		for (const auto &info : filter_properties_) {
			if (QString::fromStdString(info.name) == selected) {
				return &info;
			}
		}
		return nullptr;
	}

	bool ReadCurrentFilterPropertyValue(const FilterPropertyInfo &info, bool &bool_value_out,
					    double &number_value_out, int &list_index_out) const
	{
		bool_value_out = false;
		number_value_out = 0.0;
		list_index_out = 0;

		const std::string source_name = source_combo_->currentData().toString().toStdString();
		const std::string filter_name = filter_combo_->currentText().toStdString();
		if (source_name.empty() || filter_name.empty() || info.name.empty()) {
			return false;
		}

		obs_source_t *source = obs_get_source_by_name(source_name.c_str());
		if (!source) {
			return false;
		}
		obs_source_t *filter = obs_source_get_filter_by_name(source, filter_name.c_str());
		obs_source_release(source);
		if (!filter) {
			return false;
		}
		obs_data_t *settings = obs_source_get_settings(filter);
		obs_source_release(filter);
		if (!settings) {
			return false;
		}

		if (info.type == OBS_PROPERTY_BOOL) {
			bool_value_out = obs_data_get_bool(settings, info.name.c_str());
		} else if (info.type == OBS_PROPERTY_INT) {
			number_value_out = (double)obs_data_get_int(settings, info.name.c_str());
		} else if (info.type == OBS_PROPERTY_FLOAT) {
			number_value_out = obs_data_get_double(settings, info.name.c_str());
		} else if (info.type == OBS_PROPERTY_LIST) {
			if (info.list_format == OBS_COMBO_FORMAT_INT) {
				long long current = obs_data_get_int(settings, info.name.c_str());
				for (size_t i = 0; i < info.list_items.size(); ++i) {
					if (info.list_items[i].int_value == current) {
						list_index_out = (int)i;
						break;
					}
				}
			} else if (info.list_format == OBS_COMBO_FORMAT_FLOAT) {
				double current = obs_data_get_double(settings, info.name.c_str());
				for (size_t i = 0; i < info.list_items.size(); ++i) {
					if (std::fabs(info.list_items[i].float_value - current) < 0.000001) {
						list_index_out = (int)i;
						break;
					}
				}
			} else {
				const char *current = obs_data_get_string(settings, info.name.c_str());
				const std::string current_str = current ? current : "";
				for (size_t i = 0; i < info.list_items.size(); ++i) {
					if (info.list_items[i].string_value == current_str) {
						list_index_out = (int)i;
						break;
					}
				}
			}
		}

		obs_data_release(settings);
		return true;
	}

	void ReloadFilterProperties()
	{
		const QString previous = filter_property_combo_->currentData().toString();
		QSignalBlocker property_blocker(*filter_property_combo_);
		QSignalBlocker list_blocker(*filter_property_list_combo_);
		filter_property_combo_->clear();
		filter_property_list_combo_->clear();
		filter_properties_.clear();

		if (CurrentAction() != JoypadActionType::SetFilterProperty &&
		    CurrentAction() != JoypadActionType::AdjustFilterProperty) {
			return;
		}

		filter_properties_ =
			get_filter_properties_for_source_filter(source_combo_->currentData().toString().toStdString(),
								filter_combo_->currentText().toStdString());
		for (const auto &info : filter_properties_) {
			if (CurrentAction() == JoypadActionType::AdjustFilterProperty &&
			    !(info.type == OBS_PROPERTY_INT || info.type == OBS_PROPERTY_FLOAT)) {
				continue;
			}
			const QString label = QString::fromStdString(info.description);
			filter_property_combo_->addItem(label, QString::fromStdString(info.name));
		}
		int idx = filter_property_combo_->findData(previous);
		if (idx < 0 && !binding_.filter_property_name.empty()) {
			idx = filter_property_combo_->findData(QString::fromStdString(binding_.filter_property_name));
		}
		if (idx >= 0) {
			filter_property_combo_->setCurrentIndex(idx);
		} else if (filter_property_combo_->count() > 0) {
			filter_property_combo_->setCurrentIndex(0);
		}
	}

	void UpdateActionUi()
	{
		if (updating_action_ui_) {
			return;
		}
		updating_action_ui_ = true;
		auto action = CurrentAction();
		const bool is_screenshot_action = (action == JoypadActionType::Screenshot);
		const bool screenshot_uses_source = is_screenshot_action &&
						    (CurrentScreenshotTarget() == JoypadScreenshotTarget::Source);
		bool needs_scene = (action == JoypadActionType::SwitchScene) ||
				   (action == JoypadActionType::ToggleSourceVisibility) ||
				   (action == JoypadActionType::SetSourceVisibility);
		bool needs_source =
			(action == JoypadActionType::ToggleSourceVisibility) ||
			(action == JoypadActionType::SetSourceVisibility) ||
			(action == JoypadActionType::ToggleSourceMute) || (action == JoypadActionType::SetSourceMute) ||
			(action == JoypadActionType::SetSourceVolume) ||
			(action == JoypadActionType::AdjustSourceVolume) ||
			(action == JoypadActionType::SetSourceVolumePercent) ||
			(action == JoypadActionType::MediaPlayPause) || (action == JoypadActionType::MediaRestart) ||
			(action == JoypadActionType::MediaStop) || (action == JoypadActionType::ToggleFilterEnabled) ||
			(action == JoypadActionType::SetFilterEnabled) ||
			(action == JoypadActionType::SetFilterProperty) ||
			(action == JoypadActionType::AdjustFilterProperty) ||
			(action == JoypadActionType::SourceTransform) || screenshot_uses_source;
		if (action == JoypadActionType::SourceTransform) {
			needs_scene = true;
		}
		bool needs_filter = (action == JoypadActionType::ToggleFilterEnabled) ||
				    (action == JoypadActionType::SetFilterEnabled) ||
				    (action == JoypadActionType::SetFilterProperty) ||
				    (action == JoypadActionType::AdjustFilterProperty);
		bool needs_filter_property = (action == JoypadActionType::SetFilterProperty) ||
					     (action == JoypadActionType::AdjustFilterProperty);
		bool needs_transform_op = (action == JoypadActionType::SourceTransform);

		scene_combo_->setEnabled(needs_scene);
		use_current_scene_->setEnabled(needs_scene && action != JoypadActionType::SwitchScene);
		if (use_current_scene_->isEnabled() && use_current_scene_->isChecked()) {
			scene_combo_->setEnabled(false);
		}
		source_combo_->setEnabled(needs_source);
		filter_combo_->setEnabled(needs_filter);
		filter_property_label_->setVisible(needs_filter_property);
		filter_property_combo_->setVisible(needs_filter_property);
		filter_property_combo_->setEnabled(needs_filter_property);
		transform_op_label_->setVisible(needs_transform_op);
		transform_op_combo_->setVisible(needs_transform_op);
		transform_op_combo_->setEnabled(needs_transform_op);
		screenshot_target_label_->setVisible(is_screenshot_action);
		screenshot_target_combo_->setVisible(is_screenshot_action);
		screenshot_target_combo_->setEnabled(is_screenshot_action);

		ReloadSourcesForAction(action);
		ReloadFilters();
		if (needs_filter_property) {
			ReloadFilterProperties();
		}

		bool show_bool = (action == JoypadActionType::SetSourceVisibility) ||
				 (action == JoypadActionType::SetSourceMute) ||
				 (action == JoypadActionType::SetFilterEnabled);
		bool show_volume = (action == JoypadActionType::SetSourceVolume) ||
				   (action == JoypadActionType::AdjustSourceVolume) ||
				   (action == JoypadActionType::SetSourceVolumePercent);
		bool show_property_list = false;
		bool show_above_unity = (action == JoypadActionType::SetSourceVolume) ||
					(action == JoypadActionType::AdjustSourceVolume);

		if (action == JoypadActionType::SetFilterProperty || action == JoypadActionType::AdjustFilterProperty) {
			const FilterPropertyInfo *info = CurrentFilterPropertyInfo();
			if (info) {
				bool current_bool = false;
				double current_number = 0.0;
				int current_list_index = 0;
				const bool has_current_value = ReadCurrentFilterPropertyValue(
					*info, current_bool, current_number, current_list_index);
				if (info->type == OBS_PROPERTY_BOOL) {
					show_bool = show_bool || (action == JoypadActionType::SetFilterProperty);
					if (action == JoypadActionType::SetFilterProperty) {
						if (existing_ && binding_.filter_property_name == info->name) {
							bool_checkbox_->setChecked(binding_.bool_value);
						} else if (has_current_value) {
							bool_checkbox_->setChecked(current_bool);
						}
					}
				} else if (info->type == OBS_PROPERTY_INT || info->type == OBS_PROPERTY_FLOAT) {
					show_volume = true;
				} else if (info->type == OBS_PROPERTY_LIST &&
					   action == JoypadActionType::SetFilterProperty) {
					show_property_list = true;
					filter_property_list_combo_->clear();
					for (size_t i = 0; i < info->list_items.size(); ++i) {
						const auto &item = info->list_items[i];
						QString text = QString::fromStdString(item.name);
						if (text.isEmpty()) {
							if (info->list_format == OBS_COMBO_FORMAT_INT) {
								text = QString::number(item.int_value);
							} else if (info->list_format == OBS_COMBO_FORMAT_FLOAT) {
								text = QString::number(item.float_value, 'f', 3);
							} else {
								text = QString::fromStdString(item.string_value);
							}
						}
						filter_property_list_combo_->addItem(text, (int)i);
					}
					if (existing_ && !binding_.filter_property_name.empty() &&
					    binding_.filter_property_name == info->name) {
						int select_idx = 0;
						for (size_t i = 0; i < info->list_items.size(); ++i) {
							const auto &item = info->list_items[i];
							if (info->list_format == OBS_COMBO_FORMAT_INT &&
							    item.int_value == binding_.filter_property_list_int) {
								select_idx = (int)i;
								break;
							}
							if (info->list_format == OBS_COMBO_FORMAT_FLOAT &&
							    std::fabs(item.float_value -
								      binding_.filter_property_list_float) < 0.000001) {
								select_idx = (int)i;
								break;
							}
							if (info->list_format == OBS_COMBO_FORMAT_STRING &&
							    item.string_value == binding_.filter_property_list_string) {
								select_idx = (int)i;
								break;
							}
						}
						filter_property_list_combo_->setCurrentIndex(select_idx);
					} else if (has_current_value && !info->list_items.empty()) {
						filter_property_list_combo_->setCurrentIndex(current_list_index);
					}
				}
			}
		}

		bool_checkbox_->setVisible(show_bool);
		volume_label_->setVisible(show_volume);
		volume_spin_->setVisible(show_volume);
		filter_property_list_label_->setVisible(show_property_list);
		filter_property_list_combo_->setVisible(show_property_list);
		volume_allow_above_unity_->setVisible(show_above_unity);
		if (!show_above_unity) {
			volume_allow_above_unity_->setChecked(false);
		}

		if (action == JoypadActionType::AdjustSourceVolume) {
			volume_spin_->setRange(-20.0, 20.0);
			volume_spin_->setSingleStep(0.1);
			volume_spin_->setDecimals(1);
			volume_spin_->setSuffix(" dB");
			if (volume_spin_->value() == 0.0) {
				volume_spin_->setValue(0.1);
			}
		} else if (action == JoypadActionType::SetSourceVolume) {
			volume_spin_->setRange(-60.0, 20.0);
			volume_spin_->setSingleStep(0.1);
			volume_spin_->setDecimals(1);
			volume_spin_->setSuffix(" dB");
		} else if (action == JoypadActionType::SetSourceVolumePercent) {
			volume_spin_->setRange(0.1, 50.0);
			volume_spin_->setSingleStep(0.05);
			volume_spin_->setDecimals(2);
			volume_spin_->setSuffix(" x");
			if (volume_spin_->value() <= 0.1) {
				volume_spin_->setValue(1.0);
			}
		} else if (action == JoypadActionType::SetFilterProperty) {
			const FilterPropertyInfo *info = CurrentFilterPropertyInfo();
			if (info && (info->type == OBS_PROPERTY_INT || info->type == OBS_PROPERTY_FLOAT)) {
				bool current_bool = false;
				double current_number = 0.0;
				int current_list_index = 0;
				const bool has_current_value = ReadCurrentFilterPropertyValue(
					*info, current_bool, current_number, current_list_index);
				const double minv = info->min_value;
				const double maxv = info->max_value;
				volume_spin_->setRange(minv, maxv);
				if (info->type == OBS_PROPERTY_INT) {
					volume_spin_->setSingleStep(1.0);
					volume_spin_->setDecimals(0);
				} else {
					volume_spin_->setSingleStep(0.1);
					volume_spin_->setDecimals(3);
				}
				volume_spin_->setSuffix("");
				if (existing_ && binding_.filter_property_name == info->name) {
					volume_spin_->setValue(binding_.filter_property_value);
				} else if (has_current_value) {
					volume_spin_->setValue(std::clamp(current_number, minv, maxv));
				} else if (volume_spin_->value() < minv || volume_spin_->value() > maxv) {
					volume_spin_->setValue(minv);
				}
			}
		} else if (action == JoypadActionType::AdjustFilterProperty) {
			const FilterPropertyInfo *info = CurrentFilterPropertyInfo();
			if (info && (info->type == OBS_PROPERTY_INT || info->type == OBS_PROPERTY_FLOAT)) {
				volume_spin_->setRange(-1000000.0, 1000000.0);
				if (info->type == OBS_PROPERTY_INT) {
					volume_spin_->setSingleStep(1.0);
					volume_spin_->setDecimals(0);
				} else {
					volume_spin_->setSingleStep(0.1);
					volume_spin_->setDecimals(3);
				}
				volume_spin_->setSuffix("");
				if (std::fabs(volume_spin_->value()) < 0.000001) {
					volume_spin_->setValue(info->type == OBS_PROPERTY_INT ? 1.0 : 0.1);
				}
			}
		}
		if (show_volume) {
			volume_allow_above_unity_->setChecked(binding_.allow_above_unity);
		}
		if (action == JoypadActionType::SetSourceVolumePercent) {
			volume_label_->setText(L("JoypadToOBS.Field.Multiplier"));
		} else if (action == JoypadActionType::SetFilterProperty) {
			volume_label_->setText(L("JoypadToOBS.Field.PropertyValue"));
		} else if (action == JoypadActionType::AdjustFilterProperty) {
			volume_label_->setText(L("JoypadToOBS.Field.Step"));
		} else {
			volume_label_->setText(L("JoypadToOBS.Field.Volume"));
		}
		UpdateAxisUi(learned_event_.is_axis);
		if (QLayout *lyt = layout()) {
			lyt->activate();
		}
		adjustSize();
		updating_action_ui_ = false;
	}

	void OnListen()
	{
		if (!input_) {
			return;
		}
		if (is_listening_) {
			input_->CancelLearn();
			is_listening_ = false;
			listen_button_->setText(add_listen_button_text());
			UpdateButtonComboUi();
			return;
		}
		BeginListenCapture();
	}

	void BeginListenCapture()
	{
		if (!input_) {
			return;
		}
		button_label_->setText(L("JoypadToOBS.Common.PressButtonOrAxis"));
		bool ok = input_->BeginLearn([this](const JoypadEvent &event) {
			QMetaObject::invokeMethod(
				this,
				[this, event]() {
					if (event.is_axis) {
						if (!binding_.button_combo.empty()) {
							BeginListenCapture();
							return;
						}
						if (std::fabs(event.axis_value) < kLearnAxisDeadzone) {
							BeginListenCapture();
							return;
						}
					}

					QString conflicts;
					const bool check_conflicts = event.is_axis || binding_.button_combo.empty();
					if (check_conflicts) {
						auto bindings = config_->GetBindingsSnapshot();
						for (const auto &b : bindings) {
							const bool same_device =
								(!b.device_id.empty() &&
								 b.device_id == event.device_id) ||
								(!b.device_stable_id.empty() &&
								 b.device_stable_id == event.device_stable_id) ||
								(!b.device_type_id.empty() &&
								 b.device_type_id == event.device_type_id);
							bool match = false;
							if (event.is_axis) {
								if (b.input_type == JoypadInputType::Axis &&
								    same_device && b.axis_index == event.axis_index) {
									match = true;
								}
							} else {
								if (b.input_type == JoypadInputType::Button &&
								    same_device && b.button == event.button) {
									match = true;
								}
							}

							if (match) {
								if (existing_ && b.uid == existing_->uid) {
									continue;
								}
								QString actionName = action_to_text(b.action);
								QString target;
								if (!b.scene_name.empty())
									target = QString::fromStdString(b.scene_name);
								else if (!b.source_name.empty())
									target = QString::fromStdString(b.source_name);
								conflicts += QStringLiteral("\u2022 %1 (%2)\n")
										     .arg(actionName, target);
							}
						}

						if (!conflicts.isEmpty()) {
							QMessageBox::StandardButton reply;
							reply = QMessageBox::warning(
								this, L("JoypadToOBS.Dialog.ConflictTitle"),
								L("JoypadToOBS.Dialog.ConflictMessage").arg(conflicts),
								QMessageBox::Yes | QMessageBox::No);
							if (reply == QMessageBox::No) {
								return;
							}
						}
					}

					is_listening_ = false;
					listen_button_->setText(add_listen_button_text());
					learned_event_ = event;
					if (event.is_axis) {
						binding_.button_combo.clear();
						UpdateAxisUi(true);
						last_axis_value_ = event.axis_raw_value;
						if (CurrentAction() == JoypadActionType::SetSourceVolumePercent) {
							double percent = MapRawToPercent(event.axis_raw_value);
							double db = MapPercentToDb(percent);
							axis_value_slider_->setValue((int)percent);
							axis_live_value_label_->setText(
								L("JoypadToOBS.Common.PercentValue")
									.arg(percent, 0, 'f', 0) +
								" " +
								L("JoypadToOBS.Common.DbValue").arg(db, 0, 'f', 1));
						} else if (CurrentAction() == JoypadActionType::SetFilterProperty) {
							const FilterPropertyInfo *info = CurrentFilterPropertyInfo();
							if (info && (info->type == OBS_PROPERTY_INT ||
								     info->type == OBS_PROPERTY_FLOAT)) {
								double mapped = map_axis_raw_to_range_for_test(
									binding_, event.axis_raw_value, info->min_value,
									info->max_value);
								axis_value_slider_->setValue((int)event.axis_raw_value);
								axis_live_value_label_->setText(QString::number(
									mapped, 'f',
									info->type == OBS_PROPERTY_INT ? 0 : 3));
							} else {
								axis_value_slider_->setValue((int)event.axis_raw_value);
								axis_live_value_label_->setText(
									QString::number(last_axis_value_, 'f', 2));
							}
						} else {
							axis_value_slider_->setValue((int)event.axis_raw_value);
							axis_live_value_label_->setText(
								QString::number(last_axis_value_, 'f', 2));
						}
					} else {
						UpdateAxisUi(false);
						bool exists = false;
						for (const auto &entry : binding_.button_combo) {
							const bool same_button = entry.button == event.button;
							const bool same_id = !entry.device_id.empty() &&
									     entry.device_id == event.device_id;
							const bool same_stable = !entry.device_stable_id.empty() &&
										 entry.device_stable_id ==
											 event.device_stable_id;
							const bool same_type = !entry.device_type_id.empty() &&
									       entry.device_type_id ==
										       event.device_type_id;
							if (same_button && (same_id || same_stable || same_type)) {
								exists = true;
								break;
							}
						}
						if (!exists) {
							JoypadButtonComboEntry entry;
							entry.device_id = event.device_id;
							entry.device_stable_id = event.device_stable_id;
							entry.device_type_id = event.device_type_id;
							entry.device_name = event.device_name;
							entry.button = event.button;
							binding_.button_combo.push_back(std::move(entry));
						}
					}
					SelectDevice(event);
					UpdateButtonComboUi();
					DisableTestModeOnConfigChange();
				},
				Qt::QueuedConnection);
		});
		if (ok) {
			is_listening_ = true;
			listen_button_->setText(add_listen_button_text() + "...");
		} else {
			button_label_->setText(L("JoypadToOBS.Common.AlreadyListening"));
		}
	}

	void SelectDevice(const JoypadEvent &event)
	{
		int index = device_combo_->findData(QString::fromStdString(event.device_id), kDeviceIdRole);
		if (index < 0 && !event.device_stable_id.empty()) {
			index = device_combo_->findData(QString::fromStdString(event.device_stable_id),
							kDeviceStableIdRole);
		}
		if (index < 0 && !event.device_type_id.empty()) {
			index = device_combo_->findData(QString::fromStdString(event.device_type_id),
							kDeviceTypeIdRole);
		}
		if (index >= 0) {
			device_combo_->setCurrentIndex(index);
		}
	}

	bool ReadBinding(bool require_input)
	{
		if (require_input && !learned_event_.is_axis && binding_.button_combo.empty() &&
		    learned_event_.button <= 0) {
			button_label_->setText(L("JoypadToOBS.Common.PressButtonOrAxisFirst"));
			return false;
		}

		if (require_input && CurrentAction() == JoypadActionType::SetSourceVolumePercent &&
		    !learned_event_.is_axis) {
			button_label_->setText(L("JoypadToOBS.Common.AxisOnlyForSlider"));
			return false;
		}

		if (learned_event_.is_axis) {
			binding_.button_combo.clear();
			binding_.button = -1;
			binding_.device_id = device_combo_->currentData(kDeviceIdRole).toString().toStdString();
			binding_.device_stable_id =
				device_combo_->currentData(kDeviceStableIdRole).toString().toStdString();
			binding_.device_type_id =
				device_combo_->currentData(kDeviceTypeIdRole).toString().toStdString();
			binding_.device_name = device_combo_->currentText().toStdString();
			binding_.input_type = JoypadInputType::Axis;
			binding_.axis_index = learned_event_.axis_index;
			binding_.axis_inverted = invert_axis_checkbox_->isChecked();
			if (CurrentAction() == JoypadActionType::SetSourceVolumePercent) {
				binding_.axis_direction = JoypadAxisDirection::Both;
			} else if (axis_both_checkbox_->isChecked()) {
				binding_.axis_direction = JoypadAxisDirection::Both;
			} else {
				binding_.axis_direction = (learned_event_.axis_value >= 0.0)
								  ? JoypadAxisDirection::Positive
								  : JoypadAxisDirection::Negative;
			}
			binding_.axis_threshold = std::clamp(axis_threshold_spin_->value(), 0.0, 0.95);
			binding_.axis_min_per_second = std::clamp(axis_min_per_second_spin_->value(), 1.0, 60.0);
			binding_.axis_max_per_second = std::clamp(axis_max_per_second_spin_->value(), 1.0, 60.0);
			if (binding_.axis_max_per_second < binding_.axis_min_per_second) {
				binding_.axis_max_per_second = binding_.axis_min_per_second;
			}
			binding_.axis_interval_ms = 150;
			binding_.axis_min_value = binding_.axis_min_value;
			binding_.axis_max_value = binding_.axis_max_value;
		} else {
			if (binding_.button_combo.empty() && learned_event_.button > 0) {
				JoypadButtonComboEntry entry;
				entry.device_id = device_combo_->currentData(kDeviceIdRole).toString().toStdString();
				entry.device_stable_id =
					device_combo_->currentData(kDeviceStableIdRole).toString().toStdString();
				entry.device_type_id =
					device_combo_->currentData(kDeviceTypeIdRole).toString().toStdString();
				entry.device_name = device_combo_->currentText().toStdString();
				entry.button = learned_event_.button;
				binding_.button_combo.push_back(std::move(entry));
			}
			if (!binding_.button_combo.empty()) {
				const auto &primary = binding_.button_combo.front();
				binding_.button = primary.button;
				binding_.device_id = primary.device_id;
				binding_.device_stable_id = primary.device_stable_id;
				binding_.device_type_id = primary.device_type_id;
				binding_.device_name = primary.device_name;
			} else {
				binding_.button = learned_event_.button;
				binding_.device_id = device_combo_->currentData(kDeviceIdRole).toString().toStdString();
				binding_.device_stable_id =
					device_combo_->currentData(kDeviceStableIdRole).toString().toStdString();
				binding_.device_type_id =
					device_combo_->currentData(kDeviceTypeIdRole).toString().toStdString();
				binding_.device_name = device_combo_->currentText().toStdString();
			}
			binding_.input_type = JoypadInputType::Button;
			binding_.axis_index = -1;
			binding_.axis_inverted = false;
			binding_.axis_threshold = 0.10;
			binding_.axis_min_per_second = 2.5;
			binding_.axis_max_per_second = 20.0;
		}

		binding_.action = CurrentAction();
		binding_.use_current_scene = use_current_scene_->isChecked();
		binding_.scene_name = scene_combo_->currentText().toStdString();
		binding_.source_name = source_combo_->currentData().toString().toStdString();
		binding_.filter_name = filter_combo_->currentText().toStdString();
		binding_.filter_property_name = filter_property_combo_->currentData().toString().toStdString();
		binding_.source_transform_op = (JoypadSourceTransformOp)transform_op_combo_->currentData().toInt();
		binding_.screenshot_target = CurrentScreenshotTarget();
		binding_.bool_value = bool_checkbox_->isChecked();
		bool is_volume_action = (binding_.action == JoypadActionType::SetSourceVolume) ||
					(binding_.action == JoypadActionType::AdjustSourceVolume) ||
					(binding_.action == JoypadActionType::SetSourceVolumePercent) ||
					(binding_.action == JoypadActionType::AdjustFilterProperty);
		binding_.allow_above_unity = (binding_.action == JoypadActionType::SetSourceVolume ||
					      binding_.action == JoypadActionType::AdjustSourceVolume)
						     ? volume_allow_above_unity_->isChecked()
						     : false;
		if (binding_.action == JoypadActionType::SetSourceVolumePercent) {
			binding_.slider_gamma = volume_spin_->value();
			binding_.volume_value = 0.0;
		} else {
			binding_.volume_value = is_volume_action ? volume_spin_->value() : 0.0;
		}
		if (binding_.action == JoypadActionType::SetFilterProperty ||
		    binding_.action == JoypadActionType::AdjustFilterProperty) {
			const FilterPropertyInfo *info = CurrentFilterPropertyInfo();
			if (!info) {
				button_label_->setText(L("JoypadToOBS.Common.NoFilterPropertySelected"));
				return false;
			}
			binding_.filter_property_name = info->name;
			binding_.filter_property_type = (int)info->type;
			binding_.filter_property_min = info->min_value;
			binding_.filter_property_max = info->max_value;
			binding_.filter_property_value = volume_spin_->value();
			binding_.filter_property_list_format = (int)info->list_format;
			binding_.filter_property_list_string.clear();
			binding_.filter_property_list_int = 0;
			binding_.filter_property_list_float = 0.0;
			if (binding_.action == JoypadActionType::SetFilterProperty && info->type == OBS_PROPERTY_LIST) {
				const int idx = filter_property_list_combo_->currentData().toInt();
				if (idx < 0 || idx >= (int)info->list_items.size()) {
					button_label_->setText(L("JoypadToOBS.Common.NoFilterPropertySelected"));
					return false;
				}
				const auto &item = info->list_items[(size_t)idx];
				if (info->list_format == OBS_COMBO_FORMAT_INT) {
					binding_.filter_property_list_int = item.int_value;
				} else if (info->list_format == OBS_COMBO_FORMAT_FLOAT) {
					binding_.filter_property_list_float = item.float_value;
				} else {
					binding_.filter_property_list_string = item.string_value;
				}
			}
			if (binding_.action == JoypadActionType::AdjustFilterProperty &&
			    !(info->type == OBS_PROPERTY_INT || info->type == OBS_PROPERTY_FLOAT)) {
				button_label_->setText(L("JoypadToOBS.Common.NumericPropertyOnly"));
				return false;
			}
		}

		// Cleanup unused fields based on action
		if (binding_.input_type != JoypadInputType::Axis) {
			binding_.axis_index = -1;
			binding_.axis_inverted = false;
			binding_.axis_threshold = 0.10;
			binding_.axis_min_per_second = 2.5;
			binding_.axis_max_per_second = 20.0;
			binding_.axis_min_value = 0.0;
			binding_.axis_max_value = 1024.0;
		}
		if (binding_.input_type != JoypadInputType::Button) {
			binding_.button_combo.clear();
		}

		bool needs_scene = (binding_.action == JoypadActionType::SwitchScene) ||
				   (binding_.action == JoypadActionType::ToggleSourceVisibility) ||
				   (binding_.action == JoypadActionType::SetSourceVisibility) ||
				   (binding_.action == JoypadActionType::SourceTransform);
		bool needs_use_current = (binding_.action == JoypadActionType::ToggleSourceVisibility ||
					  binding_.action == JoypadActionType::SetSourceVisibility ||
					  binding_.action == JoypadActionType::SourceTransform);
		bool needs_source = (binding_.action == JoypadActionType::ToggleSourceVisibility) ||
				    (binding_.action == JoypadActionType::SetSourceVisibility) ||
				    (binding_.action == JoypadActionType::ToggleSourceMute) ||
				    (binding_.action == JoypadActionType::SetSourceMute) ||
				    (binding_.action == JoypadActionType::SetSourceVolume) ||
				    (binding_.action == JoypadActionType::AdjustSourceVolume) ||
				    (binding_.action == JoypadActionType::SetSourceVolumePercent) ||
				    (binding_.action == JoypadActionType::MediaPlayPause) ||
				    (binding_.action == JoypadActionType::MediaRestart) ||
				    (binding_.action == JoypadActionType::MediaStop) ||
				    (binding_.action == JoypadActionType::ToggleFilterEnabled) ||
				    (binding_.action == JoypadActionType::SetFilterEnabled) ||
				    (binding_.action == JoypadActionType::SetFilterProperty) ||
				    (binding_.action == JoypadActionType::AdjustFilterProperty) ||
				    (binding_.action == JoypadActionType::SourceTransform) ||
				    ((binding_.action == JoypadActionType::Screenshot) &&
				     (binding_.screenshot_target == JoypadScreenshotTarget::Source));
		bool needs_filter = (binding_.action == JoypadActionType::ToggleFilterEnabled ||
				     binding_.action == JoypadActionType::SetFilterEnabled ||
				     binding_.action == JoypadActionType::SetFilterProperty ||
				     binding_.action == JoypadActionType::AdjustFilterProperty);
		bool needs_filter_property = (binding_.action == JoypadActionType::SetFilterProperty ||
					      binding_.action == JoypadActionType::AdjustFilterProperty);
		bool needs_bool = (binding_.action == JoypadActionType::SetSourceVisibility ||
				   binding_.action == JoypadActionType::SetSourceMute ||
				   binding_.action == JoypadActionType::SetFilterEnabled);
		bool needs_volume = (binding_.action == JoypadActionType::SetSourceVolume ||
				     binding_.action == JoypadActionType::AdjustSourceVolume);
		bool needs_gamma = (binding_.action == JoypadActionType::SetSourceVolumePercent);
		bool needs_unity = (binding_.action == JoypadActionType::SetSourceVolume ||
				    binding_.action == JoypadActionType::AdjustSourceVolume);
		if (binding_.action == JoypadActionType::SetFilterProperty ||
		    binding_.action == JoypadActionType::AdjustFilterProperty) {
			needs_bool = needs_bool || (binding_.filter_property_type == OBS_PROPERTY_BOOL);
			needs_volume = needs_volume || (binding_.filter_property_type == OBS_PROPERTY_INT ||
							binding_.filter_property_type == OBS_PROPERTY_FLOAT);
		}

		if (!needs_scene) {
			binding_.scene_name.clear();
		}
		if (!needs_use_current) {
			binding_.use_current_scene = false;
		} else if (binding_.use_current_scene) {
			binding_.scene_name.clear();
		}
		if (!needs_source)
			binding_.source_name.clear();
		if (!needs_filter)
			binding_.filter_name.clear();
		if (!needs_filter_property) {
			binding_.filter_property_name.clear();
			binding_.filter_property_type = 0;
			binding_.filter_property_value = 0.0;
			binding_.filter_property_min = 0.0;
			binding_.filter_property_max = 1.0;
			binding_.filter_property_list_format = 0;
			binding_.filter_property_list_string.clear();
			binding_.filter_property_list_int = 0;
			binding_.filter_property_list_float = 0.0;
		}
		if (binding_.action != JoypadActionType::SourceTransform) {
			binding_.source_transform_op = JoypadSourceTransformOp::CenterToScreen;
		}
		if (binding_.action != JoypadActionType::Screenshot) {
			binding_.screenshot_target = JoypadScreenshotTarget::Program;
		}
		if (!needs_bool)
			binding_.bool_value = false;
		if (!needs_volume)
			binding_.volume_value = 0.0;
		if (!needs_gamma)
			binding_.slider_gamma = 0.6;
		if (!needs_unity)
			binding_.allow_above_unity = false;

		return true;
	}

	JoypadActionType CurrentAction() const { return (JoypadActionType)action_combo_->currentData().toInt(); }
	JoypadScreenshotTarget CurrentScreenshotTarget() const
	{
		return (JoypadScreenshotTarget)screenshot_target_combo_->currentData().toInt();
	}

	JoypadConfigStore *config_ = nullptr;
	JoypadInputManager *input_ = nullptr;
	const JoypadBinding *existing_ = nullptr;
	JoypadBinding binding_;
	JoypadEvent learned_event_;

	QComboBox *device_combo_ = nullptr;
	QLabel *axis_name_label_ = nullptr;
	QLabel *button_label_ = nullptr;
	QPushButton *listen_button_ = nullptr;
	QFrame *button_combo_frame_ = nullptr;
	QListWidget *button_combo_list_ = nullptr;
	QPushButton *clear_combo_button_ = nullptr;
	QLabel *device_hint_label_ = nullptr;
	QLabel *axis_value_label_ = nullptr;
	QSlider *axis_value_slider_ = nullptr;
	QLabel *axis_threshold_label_ = nullptr;
	QDoubleSpinBox *axis_threshold_spin_ = nullptr;
	QLabel *axis_min_per_second_label_ = nullptr;
	QDoubleSpinBox *axis_min_per_second_spin_ = nullptr;
	QLabel *axis_max_per_second_label_ = nullptr;
	QDoubleSpinBox *axis_max_per_second_spin_ = nullptr;
	QCheckBox *axis_both_checkbox_ = nullptr;
	QLabel *axis_live_value_label_ = nullptr;
	QLabel *axis_min_label_ = nullptr;
	QLabel *axis_max_label_ = nullptr;
	QPushButton *axis_set_min_button_ = nullptr;
	QPushButton *axis_set_max_button_ = nullptr;
	double last_axis_value_ = 0.0;

	QCheckBox *use_current_scene_ = nullptr;
	QComboBox *scene_combo_ = nullptr;
	QComboBox *source_combo_ = nullptr;
	QComboBox *filter_combo_ = nullptr;
	QLabel *filter_property_label_ = nullptr;
	QComboBox *filter_property_combo_ = nullptr;
	QLabel *transform_op_label_ = nullptr;
	QComboBox *transform_op_combo_ = nullptr;

	QComboBox *action_combo_ = nullptr;
	QCheckBox *bool_checkbox_ = nullptr;
	QLabel *volume_label_ = nullptr;
	QDoubleSpinBox *volume_spin_ = nullptr;
	QLabel *screenshot_target_label_ = nullptr;
	QComboBox *screenshot_target_combo_ = nullptr;
	QLabel *filter_property_list_label_ = nullptr;
	QComboBox *filter_property_list_combo_ = nullptr;
	QCheckBox *volume_allow_above_unity_ = nullptr;
	QCheckBox *invert_axis_checkbox_ = nullptr;
	QCheckBox *test_mode_checkbox_ = nullptr;
	int axis_handler_id_ = 0;
	bool is_listening_ = false;
	bool updating_action_ui_ = false;
	std::vector<FilterPropertyInfo> filter_properties_;
};

} // namespace

bool JoypadUiIsBindingDialogOpen()
{
	return g_binding_dialog_open_count.load(std::memory_order_relaxed) > 0;
}

bool JoypadUiIsInputListeningEnabled()
{
	return g_input_listening_enabled.load(std::memory_order_relaxed);
}

bool JoypadUiToggleInputListeningEnabled()
{
	bool expected = g_input_listening_enabled.load(std::memory_order_relaxed);
	while (!g_input_listening_enabled.compare_exchange_weak(expected, !expected, std::memory_order_relaxed,
								std::memory_order_relaxed)) {
	}
	return !expected;
}

void JoypadUiSetInputListeningEnabled(bool enabled)
{
	g_input_listening_enabled.store(enabled, std::memory_order_relaxed);
}

bool JoypadUiEmulateBindingDialogAction(const JoypadEvent &event, JoypadActionEngine *actions)
{
	DialogTestState state;
	{
		std::lock_guard<std::mutex> lock(g_dialog_test_mutex);
		if (!g_dialog_test_state.enabled) {
			return false;
		}
		state = g_dialog_test_state;
	}

	if (!actions) {
		return true;
	}

	const JoypadBinding &binding = state.binding;
	JoypadBinding adjusted = binding;

	if (binding.input_type == JoypadInputType::Button) {
		if (event.is_axis) {
			return true;
		}
		if (!binding.button_combo.empty()) {
			bool matched_combo_button = false;
			for (const auto &entry : binding.button_combo) {
				const bool same_button = entry.button == event.button;
				const bool same_id = !entry.device_id.empty() && entry.device_id == event.device_id;
				const bool same_stable = !entry.device_stable_id.empty() &&
							 entry.device_stable_id == event.device_stable_id;
				const bool same_type = !entry.device_type_id.empty() &&
						       entry.device_type_id == event.device_type_id;
				if (same_button && (same_id || same_stable || same_type)) {
					matched_combo_button = true;
					break;
				}
			}
			if (!matched_combo_button) {
				return true;
			}
		} else if (binding.button > 0 && event.button != binding.button) {
			return true;
		}
		actions->Execute(adjusted);
		return true;
	}

	if (!event.is_axis) {
		return true;
	}
	if (binding.axis_index >= 0 && event.axis_index != binding.axis_index) {
		return true;
	}

	double value = binding.axis_inverted ? -event.axis_value : event.axis_value;
	double abs_value = std::fabs(value);
	if (binding.action == JoypadActionType::SetSourceVolumePercent) {
		adjusted.volume_value = map_axis_raw_to_percent_for_test(binding, event.axis_raw_value);
		actions->Execute(adjusted);
		return true;
	}
	if (binding.action == JoypadActionType::SetFilterProperty &&
	    (binding.filter_property_type == OBS_PROPERTY_INT || binding.filter_property_type == OBS_PROPERTY_FLOAT)) {
		double target_min = binding.filter_property_min;
		double target_max = binding.filter_property_max;
		if (target_max <= target_min) {
			target_min = 0.0;
			target_max = 1.0;
		}
		adjusted.filter_property_value =
			map_axis_raw_to_range_for_test(binding, event.axis_raw_value, target_min, target_max);
		actions->Execute(adjusted);
		return true;
	}
	if (binding.axis_direction != JoypadAxisDirection::Both) {
		int dir = value >= 0.0 ? 1 : -1;
		if (dir != (int)binding.axis_direction) {
			return true;
		}
	}
	double threshold_clamped = std::clamp(binding.axis_threshold, 0.0, 0.95);
	if (abs_value < threshold_clamped) {
		return true;
	}
	const auto now = std::chrono::steady_clock::now();
	const double min_rate = std::clamp(binding.axis_min_per_second, 1.0, 60.0);
	const double max_rate = std::clamp(binding.axis_max_per_second, min_rate, 60.0);
	const double intensity = std::clamp((abs_value - threshold_clamped) / (1.0 - threshold_clamped), 0.0, 1.0);
	const double rate = min_rate + (max_rate - min_rate) * intensity;
	const int dynamic_interval_ms = std::max((int)std::round(1000.0 / std::max(rate, 0.001)), 1);
	std::string interval_key = binding.device_id + ":" + std::to_string(binding.axis_index) + ":" +
				   std::to_string((int)binding.action) + ":" + binding.source_name + ":" +
				   binding.filter_name;
	{
		std::lock_guard<std::mutex> lock(g_dialog_test_mutex);
		auto it_last = g_dialog_axis_last_dispatch.find(interval_key);
		if (it_last != g_dialog_axis_last_dispatch.end()) {
			const auto elapsed =
				std::chrono::duration_cast<std::chrono::milliseconds>(now - it_last->second).count();
			if (elapsed < dynamic_interval_ms) {
				return true;
			}
		}
		g_dialog_axis_last_dispatch[interval_key] = now;
	}
	if (binding.action == JoypadActionType::AdjustSourceVolume ||
	    binding.action == JoypadActionType::AdjustFilterProperty) {
		double sign = value >= 0.0 ? 1.0 : -1.0;
		adjusted.volume_value = std::fabs(binding.volume_value) * sign;
	}
	actions->Execute(adjusted);
	return true;
}

JoypadToolsDialog::JoypadToolsDialog(QWidget *parent, JoypadConfigStore *config, JoypadInputManager *input)
	: QDialog(parent),
	  config_(config),
	  input_(input)
{
	setWindowTitle(obs_module_text("JoypadToOBS.DialogTitle"));
	setModal(false);
	resize(800, 600);

	auto *layout = new QVBoxLayout(this);

	auto *description = new QLabel(L("JoypadToOBS.Dialog.Description"), this);
	description->setWordWrap(true);
	layout->addWidget(description);

	// Profile Management UI
	auto *profile_group = new QGroupBox(L("JoypadToOBS.Profile.Group"));
	auto *profile_layout = new QGridLayout(profile_group);

	profile_combo_ = new QComboBox();
	auto *add_profile_btn = new QToolButton();
	add_profile_btn->setText("+");
	add_profile_btn->setToolTip(L("JoypadToOBS.Profile.Add"));
	auto *rename_profile_btn = new QToolButton();
	rename_profile_btn->setText("✎");
	rename_profile_btn->setToolTip(L("JoypadToOBS.Profile.Rename"));
	auto *duplicate_profile_btn = new QToolButton();
	duplicate_profile_btn->setText("❐");
	duplicate_profile_btn->setToolTip(L("JoypadToOBS.Profile.Duplicate"));
	auto *remove_profile_btn = new QToolButton();
	remove_profile_btn->setText("-");
	remove_profile_btn->setToolTip(L("JoypadToOBS.Profile.Remove"));
	auto *import_profile_btn = new QPushButton(L("JoypadToOBS.Profile.Import"));
	auto *export_profile_btn = new QPushButton(L("JoypadToOBS.Profile.Export"));

	profile_comment_ = new QPlainTextEdit();
	profile_comment_->setPlaceholderText(L("JoypadToOBS.Profile.CommentPlaceholder"));
	profile_comment_->setTabChangesFocus(true);
	profile_comment_->setMinimumHeight(70);

	profile_layout->addWidget(new QLabel(L("JoypadToOBS.Profile.Name")), 0, 0);
	profile_layout->addWidget(profile_combo_, 0, 1);

	auto *btn_layout = new QHBoxLayout();
	btn_layout->setContentsMargins(0, 0, 0, 0);
	btn_layout->addWidget(add_profile_btn);
	btn_layout->addWidget(rename_profile_btn);
	btn_layout->addWidget(duplicate_profile_btn);
	btn_layout->addWidget(remove_profile_btn);
	btn_layout->addStretch();
	btn_layout->addWidget(import_profile_btn);
	btn_layout->addWidget(export_profile_btn);
	profile_layout->addLayout(btn_layout, 0, 2);

	profile_layout->addWidget(new QLabel(L("JoypadToOBS.Profile.Comment")), 1, 0);
	profile_layout->addWidget(profile_comment_, 1, 1, 1, 2);
	profile_layout->setColumnStretch(1, 1);
	profile_layout->setRowStretch(1, 1);

	comment_debounce_timer_ = new QTimer(this);
	comment_debounce_timer_->setSingleShot(true);
	comment_debounce_timer_->setInterval(1000);

	connect(comment_debounce_timer_, &QTimer::timeout, this, [this]() {
		int idx = config_->GetCurrentProfileIndex();
		config_->SetProfileComment(idx, profile_comment_->toPlainText().toStdString());
	});

	connect(profile_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
		if (index >= 0) {
			if (comment_debounce_timer_->isActive()) {
				comment_debounce_timer_->stop();
				int old_idx = config_->GetCurrentProfileIndex();
				config_->SetProfileComment(old_idx, profile_comment_->toPlainText().toStdString());
			}
			config_->SetCurrentProfile(index);
			profile_comment_->blockSignals(true);
			profile_comment_->setPlainText(QString::fromStdString(config_->GetProfileComment(index)));
			profile_comment_->blockSignals(false);
			RefreshBindings();
		}
	});

	connect(profile_comment_, &QPlainTextEdit::textChanged, this, [this]() { comment_debounce_timer_->start(); });

	connect(add_profile_btn, &QToolButton::clicked, this, [this]() {
		bool ok;
		QString text = QInputDialog::getText(this, L("JoypadToOBS.Profile.Add"),
						     L("JoypadToOBS.Profile.NewName"), QLineEdit::Normal, "", &ok);
		if (ok) {
			QString trimmed = text.trimmed();
			if (trimmed.isEmpty()) {
				QMessageBox::warning(this, L("JoypadToOBS.Profile.Add"),
						     L("JoypadToOBS.Profile.EmptyName"));
				return;
			}
			auto names = config_->GetProfileNames();
			for (const auto &name : names) {
				if (QString::fromStdString(name).compare(trimmed, Qt::CaseInsensitive) == 0) {
					QMessageBox::warning(this, L("JoypadToOBS.Profile.Add"),
							     L("JoypadToOBS.Profile.NameExists"));
					return;
				}
			}
			config_->AddProfile(trimmed.toStdString());
			RefreshProfiles();
			QMessageBox msg(this);
			msg.setWindowTitle(L("JoypadToOBS.Profile.Add"));
			msg.setText(L("JoypadToOBS.Profile.HotkeyCreated"));
			msg.setIconPixmap(style()->standardIcon(QStyle::SP_DialogApplyButton).pixmap(32, 32));
			msg.setStandardButtons(QMessageBox::Ok);
			msg.exec();
		}
	});

	connect(rename_profile_btn, &QToolButton::clicked, this, [this]() {
		int idx = config_->GetCurrentProfileIndex();
		auto names = config_->GetProfileNames();
		if (idx < 0 || idx >= (int)names.size())
			return;
		QString currentName = QString::fromStdString(names[idx]);
		bool ok;
		QString text = QInputDialog::getText(this, L("JoypadToOBS.Profile.Rename"),
						     L("JoypadToOBS.Profile.NewName"), QLineEdit::Normal, currentName,
						     &ok);
		if (ok) {
			QString trimmed = text.trimmed();
			if (trimmed.isEmpty()) {
				QMessageBox::warning(this, L("JoypadToOBS.Profile.Rename"),
						     L("JoypadToOBS.Profile.EmptyName"));
				return;
			}
			if (trimmed != currentName) {
				for (size_t i = 0; i < names.size(); ++i) {
					if ((int)i == idx)
						continue;
					if (QString::fromStdString(names[i]).compare(trimmed, Qt::CaseInsensitive) ==
					    0) {
						QMessageBox::warning(this, L("JoypadToOBS.Profile.Rename"),
								     L("JoypadToOBS.Profile.NameExists"));
						return;
					}
				}
				config_->RenameProfile(idx, trimmed.toStdString());
				RefreshProfiles();
			}
		}
	});

	connect(duplicate_profile_btn, &QToolButton::clicked, this, [this]() {
		int idx = config_->GetCurrentProfileIndex();
		auto names = config_->GetProfileNames();
		if (idx < 0 || idx >= (int)names.size())
			return;
		QString currentName = QString::fromStdString(names[idx]);
		bool ok;
		QString text = QInputDialog::getText(this, L("JoypadToOBS.Profile.Duplicate"),
						     L("JoypadToOBS.Profile.NewName"), QLineEdit::Normal,
						     currentName + " - Copy", &ok);
		if (ok) {
			QString trimmed = text.trimmed();
			if (trimmed.isEmpty()) {
				QMessageBox::warning(this, L("JoypadToOBS.Profile.Duplicate"),
						     L("JoypadToOBS.Profile.EmptyName"));
				return;
			}
			for (const auto &name : names) {
				if (QString::fromStdString(name).compare(trimmed, Qt::CaseInsensitive) == 0) {
					QMessageBox::warning(this, L("JoypadToOBS.Profile.Duplicate"),
							     L("JoypadToOBS.Profile.NameExists"));
					return;
				}
			}
			config_->DuplicateProfile(idx, trimmed.toStdString());
			RefreshProfiles();
			QMessageBox msg(this);
			msg.setWindowTitle(L("JoypadToOBS.Profile.Duplicate"));
			msg.setText(L("JoypadToOBS.Profile.HotkeyCreated"));
			msg.setIconPixmap(style()->standardIcon(QStyle::SP_DialogApplyButton).pixmap(32, 32));
			msg.setStandardButtons(QMessageBox::Ok);
			msg.exec();
		}
	});

	connect(remove_profile_btn, &QToolButton::clicked, this, [this]() {
		if (profile_combo_->count() <= 1)
			return;
		if (QMessageBox::question(this, L("JoypadToOBS.Profile.Remove"),
					  L("JoypadToOBS.Profile.ConfirmRemove")) == QMessageBox::Yes) {
			config_->RemoveProfile(config_->GetCurrentProfileIndex());
			RefreshProfiles();
		}
	});

	connect(export_profile_btn, &QPushButton::clicked, this, [this]() {
		int idx = config_->GetCurrentProfileIndex();
		auto names = config_->GetProfileNames();
		if (idx < 0 || idx >= (int)names.size())
			return;
		QString defaultName = QString::fromStdString(names[idx]) + ".json";
		QString lastPath = QString::fromStdString(config_->GetLastFilePath());
		QString initialPath = defaultName;
		if (!lastPath.isEmpty()) {
			initialPath = QDir(lastPath).filePath(defaultName);
		}
		QString path = QFileDialog::getSaveFileName(this, L("JoypadToOBS.Profile.Export"), initialPath,
							    "JSON Files (*.json)");
		if (!path.isEmpty()) {
			config_->ExportProfile(config_->GetCurrentProfileIndex(), path.toStdString());
			config_->SetLastFilePath(QFileInfo(path).absolutePath().toStdString());
		}
	});

	connect(import_profile_btn, &QPushButton::clicked, this, [this]() {
		QString lastPath = QString::fromStdString(config_->GetLastFilePath());
		QString path = QFileDialog::getOpenFileName(this, L("JoypadToOBS.Profile.Import"), lastPath,
							    "JSON Files (*.json)");
		if (!path.isEmpty()) {
			config_->ImportProfile(path.toStdString());
			config_->SetLastFilePath(QFileInfo(path).absolutePath().toStdString());
			RefreshProfiles();
		}
	});

	table_ = new QTableWidget(this);
	table_->setColumnCount(9);
	table_->setHorizontalHeaderLabels(
		{L("JoypadToOBS.Table.Enabled"), L("JoypadToOBS.Table.Device"), L("JoypadToOBS.Table.Input"),
		 L("JoypadToOBS.Table.Action"), L("JoypadToOBS.Table.Scene"), L("JoypadToOBS.Table.SourceFilter"),
		 L("JoypadToOBS.Table.Details"), L("JoypadToOBS.Table.Edit"), L("JoypadToOBS.Table.Delete")});
	table_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	auto *header = table_->horizontalHeader();
	header->setSectionResizeMode(QHeaderView::Interactive);
	table_->setSelectionBehavior(QAbstractItemView::SelectRows);
	table_->setSelectionMode(QAbstractItemView::SingleSelection);
	table_->setEditTriggers(QAbstractItemView::NoEditTriggers);

	auto *splitter = new QSplitter(Qt::Vertical);
	splitter->addWidget(profile_group);
	splitter->addWidget(table_);
	splitter->setStretchFactor(1, 1);
	splitter->setCollapsible(0, false);
	splitter->setCollapsible(1, false);
	splitter->setSizes({0, 100000});
	layout->addWidget(splitter);

	auto *button_row = new QHBoxLayout();
	add_button_ = new QPushButton(L("JoypadToOBS.Button.AddCommand"), this);
	clear_button_ = new QPushButton(L("JoypadToOBS.Button.ClearAll"), this);
	auto *osd_button = new QPushButton(L("JoypadToOBS.Button.OSDSettings"), this);
	save_button_ = new QPushButton(L("JoypadToOBS.Button.Save"), this);
	save_button_->setEnabled(config_->HasUnsavedChanges());
	auto *close_button = new QPushButton(L("JoypadToOBS.Button.Close"), this);

	const QString plugin_version = QString::fromUtf8(PLUGIN_VERSION ? PLUGIN_VERSION : "");
	QLabel *developerLabel = new QLabel(QString("<a href=\"https://github.com/FabioZumbi12/joypad-to-obs/\" "
						    "style=\"color: gray; text-decoration: none;\"><i>Developed by "
						    "FabioZumbi12 - v%1</i></a>")
						    .arg(plugin_version),
					    this);
	developerLabel->setOpenExternalLinks(true);

	button_row->addWidget(add_button_);
	button_row->addWidget(clear_button_);
	button_row->addWidget(osd_button);
	button_row->addStretch();
	button_row->addWidget(developerLabel);
	button_row->addWidget(save_button_);
	button_row->addWidget(close_button);

	layout->addLayout(button_row);

	connect(add_button_, &QPushButton::clicked, this, [this]() {
		JoypadBindingDialog dialog(this, config_, input_);
		if (dialog.exec() == QDialog::Accepted) {
			config_->AddBinding(dialog.Binding());
			RefreshBindings();
		}
	});

	connect(clear_button_, &QPushButton::clicked, this, [this]() {
		if (QMessageBox::question(this, L("JoypadToOBS.Dialog.ClearAllTitle"),
					  L("JoypadToOBS.Dialog.ClearAllConfirm")) == QMessageBox::Yes) {
			config_->ClearCurrentProfileBindings();
			RefreshBindings();
		}
	});

	connect(osd_button, &QPushButton::clicked, this, [this]() {
		QDialog osd_dlg(this);

		osd_dlg.setWindowTitle(L("JoypadToOBS.Dialog.OSDSettings"));
		auto *layout = new QVBoxLayout(&osd_dlg);

		auto *chk = new QCheckBox(L("JoypadToOBS.Settings.EnableOSD"), &osd_dlg);
		chk->setChecked(config_->GetOsdEnabled());
		layout->addWidget(chk);

		auto *grid = new QGridLayout();
		QString currentColor = QString::fromStdString(config_->GetOsdColor());
		QString currentBgColor = QString::fromStdString(config_->GetOsdBackgroundColor());

		auto *color_label = new QLabel(L("JoypadToOBS.Settings.OSDColor"), &osd_dlg);
		auto *color_btn = new QPushButton(&osd_dlg);
		color_btn->setMinimumWidth(140);
		grid->addWidget(color_label, 0, 0);
		grid->addWidget(color_btn, 0, 1);

		auto *bg_color_label = new QLabel(L("JoypadToOBS.Settings.OSDBgColor"), &osd_dlg);
		auto *bg_color_btn = new QPushButton(&osd_dlg);
		bg_color_btn->setMinimumWidth(140);
		grid->addWidget(bg_color_label, 1, 0);
		grid->addWidget(bg_color_btn, 1, 1);

		QLabel *size_label = new QLabel(L("JoypadToOBS.Settings.OSDSize"), &osd_dlg);
		grid->addWidget(size_label, 2, 0);
		auto *spin = new QSpinBox(&osd_dlg);
		spin->setRange(8, 100);
		spin->setValue(config_->GetOsdFontSize());
		grid->addWidget(spin, 2, 1);

		QLabel *pos_label = new QLabel(L("JoypadToOBS.Settings.OSDPosition"), &osd_dlg);
		grid->addWidget(pos_label, 3, 0);
		auto *pos_combo = new QComboBox(&osd_dlg);
		pos_combo->addItem(L("JoypadToOBS.Position.TopLeft"), (int)JoypadOsdPosition::TopLeft);
		pos_combo->addItem(L("JoypadToOBS.Position.TopCenter"), (int)JoypadOsdPosition::TopCenter);
		pos_combo->addItem(L("JoypadToOBS.Position.TopRight"), (int)JoypadOsdPosition::TopRight);
		pos_combo->addItem(L("JoypadToOBS.Position.CenterLeft"), (int)JoypadOsdPosition::CenterLeft);
		pos_combo->addItem(L("JoypadToOBS.Position.Center"), (int)JoypadOsdPosition::Center);
		pos_combo->addItem(L("JoypadToOBS.Position.CenterRight"), (int)JoypadOsdPosition::CenterRight);
		pos_combo->addItem(L("JoypadToOBS.Position.BottomLeft"), (int)JoypadOsdPosition::BottomLeft);
		pos_combo->addItem(L("JoypadToOBS.Position.BottomCenter"), (int)JoypadOsdPosition::BottomCenter);
		pos_combo->addItem(L("JoypadToOBS.Position.BottomRight"), (int)JoypadOsdPosition::BottomRight);
		pos_combo->setCurrentIndex(pos_combo->findData((int)config_->GetOsdPosition()));
		grid->addWidget(pos_combo, 3, 1);
		layout->addLayout(grid);

		auto *preview = new QLabel(L("JoypadToOBS.Settings.OSDPreviewText"), &osd_dlg);
		preview->setAlignment(Qt::AlignCenter);
		preview->setMinimumHeight(48);
		layout->addWidget(preview);

		auto update_color_button = [](QPushButton *button, const QString &color) {
			button->setText(color);
			button->setStyleSheet(QString("QPushButton { background-color: %1; border: 1px solid #555; "
						      "padding: 4px 8px; }")
						      .arg(color));
		};
		auto update_preview = [preview, &currentColor, &currentBgColor, spin]() {
			preview->setStyleSheet(QString("QLabel { background-color: %1; color: %2; border-radius: 6px; "
						       "padding: 8px; font-weight: bold; font-size: %3px; }")
						       .arg(currentBgColor)
						       .arg(currentColor)
						       .arg(spin->value()));
		};
		update_color_button(color_btn, currentColor);
		update_color_button(bg_color_btn, currentBgColor);
		update_preview();

		auto *bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &osd_dlg);
		auto *reset_button =
			bbox->addButton(L("JoypadToOBS.Button.ResetOSDDefaults"), QDialogButtonBox::ResetRole);
		layout->addWidget(bbox);

		connect(bbox, &QDialogButtonBox::accepted, &osd_dlg, &QDialog::accept);
		connect(bbox, &QDialogButtonBox::rejected, &osd_dlg, &QDialog::reject);
		connect(color_btn, &QPushButton::clicked, [&]() {
			QColor c = QColorDialog::getColor(QColor(currentColor), &osd_dlg,
							  L("JoypadToOBS.Settings.SelectColor"));
			if (c.isValid()) {
				currentColor = c.name();
				update_color_button(color_btn, currentColor);
				update_preview();
			}
		});
		connect(bg_color_btn, &QPushButton::clicked, [&]() {
			QColor c = QColorDialog::getColor(QColor(currentBgColor), &osd_dlg,
							  L("JoypadToOBS.Settings.SelectColor"));
			if (c.isValid()) {
				currentBgColor = c.name(QColor::HexArgb);
				update_color_button(bg_color_btn, currentBgColor);
				update_preview();
			}
		});
		connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), &osd_dlg,
			[update_preview](int) { update_preview(); });
		connect(reset_button, &QPushButton::clicked, &osd_dlg, [=, &currentColor, &currentBgColor]() {
			chk->setChecked(true);
			currentColor = QStringLiteral("#ffffff");
			currentBgColor = QStringLiteral("#000000");
			spin->setValue(24);
			const int bottom_center_idx = pos_combo->findData((int)JoypadOsdPosition::BottomCenter);
			pos_combo->setCurrentIndex(bottom_center_idx >= 0 ? bottom_center_idx : 0);
			update_color_button(color_btn, currentColor);
			update_color_button(bg_color_btn, currentBgColor);
			update_preview();
		});
		auto update_enabled_state = [=](bool enabled) {
			color_label->setEnabled(enabled);
			color_btn->setEnabled(enabled);
			bg_color_label->setEnabled(enabled);
			bg_color_btn->setEnabled(enabled);
			size_label->setEnabled(enabled);
			spin->setEnabled(enabled);
			pos_label->setEnabled(enabled);
			pos_combo->setEnabled(enabled);
			preview->setEnabled(enabled);
		};
		connect(chk, &QCheckBox::toggled, &osd_dlg, update_enabled_state);
		update_enabled_state(chk->isChecked());

		if (osd_dlg.exec() == QDialog::Accepted) {
			config_->SetOsdEnabled(chk->isChecked());
			config_->SetOsdColor(currentColor.toStdString());
			config_->SetOsdBackgroundColor(currentBgColor.toStdString());
			config_->SetOsdFontSize(spin->value());
			config_->SetOsdPosition((JoypadOsdPosition)pos_combo->currentData().toInt());
		}
	});

	connect(save_button_, &QPushButton::clicked, this, [this]() { config_->Save(); });

	connect(close_button, &QPushButton::clicked, this, &QDialog::close);

	update_timer_ = new QTimer(this);
	connect(update_timer_, &QTimer::timeout, this, [this]() {
		int actual = config_->GetCurrentProfileIndex();

		if (profile_combo_->currentIndex() != actual) {
			profile_combo_->blockSignals(true);

			if (actual >= 0 && actual < profile_combo_->count()) {
				profile_combo_->setCurrentIndex(actual);
				profile_comment_->blockSignals(true);
				profile_comment_->setPlainText(
					QString::fromStdString(config_->GetProfileComment(actual)));
				profile_comment_->blockSignals(false);
				RefreshBindings();
			}

			profile_combo_->blockSignals(false);
		}
		save_button_->setEnabled(config_->HasUnsavedChanges());
	});
	update_timer_->start(200);

	RefreshProfiles();
	table_->resizeColumnsToContents();
}

JoypadToolsDialog::~JoypadToolsDialog()
{
	if (comment_debounce_timer_ && comment_debounce_timer_->isActive()) {
		int idx = config_->GetCurrentProfileIndex();
		config_->SetProfileComment(idx, profile_comment_->toPlainText().toStdString());
	}
}

void JoypadToolsDialog::closeEvent(QCloseEvent *event)
{
	if (config_->HasUnsavedChanges()) {
		QMessageBox::StandardButton reply;
		reply = QMessageBox::question(this, L("JoypadToOBS.Dialog.UnsavedChangesTitle"),
					      L("JoypadToOBS.Dialog.UnsavedChangesText"),
					      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
		if (reply == QMessageBox::Save) {
			config_->Save();
			event->accept();
		} else if (reply == QMessageBox::Discard) {
			config_->DiscardChanges();
			RefreshProfiles(); // Refresh UI to reflect discarded changes
			event->accept();
		} else {
			event->ignore();
		}
	} else {
		event->accept();
	}
}

void JoypadToolsDialog::RefreshProfiles()
{
	profile_combo_->blockSignals(true);
	profile_combo_->clear();
	auto names = config_->GetProfileNames();
	for (size_t i = 0; i < names.size(); ++i) {
		std::string hotkey = config_->GetProfileHotkeyString((int)i);
		QString display = QString::fromStdString(names[i]);
		if (!hotkey.empty()) {
			display += QString(" [%1]").arg(QString::fromStdString(hotkey));
		}
		profile_combo_->addItem(display);
	}
	int current = config_->GetCurrentProfileIndex();
	if (current >= 0 && current < profile_combo_->count()) {
		profile_combo_->setCurrentIndex(current);
	}
	profile_comment_->blockSignals(true);
	profile_comment_->setPlainText(QString::fromStdString(config_->GetProfileComment(current)));
	profile_comment_->blockSignals(false);
	profile_combo_->blockSignals(false);

	RefreshBindings();
}

void JoypadToolsDialog::RefreshBindings()
{
	auto bindings = config_->GetBindingsSnapshot();
	table_->setRowCount((int)bindings.size());

	for (int row = 0; row < (int)bindings.size(); ++row) {
		const auto &binding = bindings[(size_t)row];
		const QString device = device_label_from_binding(binding);

		QWidget *chk_widget = new QWidget();
		QHBoxLayout *chk_layout = new QHBoxLayout(chk_widget);
		chk_layout->setContentsMargins(0, 0, 0, 0);
		chk_layout->setAlignment(Qt::AlignCenter);
		QCheckBox *chk = new QCheckBox();
		chk->setChecked(binding.enabled);
		chk_layout->addWidget(chk);
		table_->setCellWidget(row, 0, chk_widget);

		int64_t uid = binding.uid;

		connect(chk, &QCheckBox::toggled, this, [this, uid](bool checked) {
			auto current = config_->GetBindingsSnapshot();
			for (size_t i = 0; i < current.size(); ++i) {
				if (current[i].uid == uid) {
					JoypadBinding updated = current[i];
					updated.enabled = checked;
					config_->UpdateBinding(i, updated);
					break;
				}
			}
		});

		table_->setItem(row, 1, new QTableWidgetItem(device));
		table_->setItem(row, 2, new QTableWidgetItem(input_label_from_binding(binding)));
		table_->setItem(row, 3, new QTableWidgetItem(action_to_text(binding.action)));

		QString scene_text;
		QString source_filter_text;

		switch (binding.action) {
		case JoypadActionType::SwitchScene:
			scene_text = QString::fromStdString(binding.scene_name);
			break;
		case JoypadActionType::ToggleSourceVisibility:
		case JoypadActionType::SetSourceVisibility:
			scene_text = binding.use_current_scene ? L("JoypadToOBS.Common.Current")
							       : QString::fromStdString(binding.scene_name);
			source_filter_text = QString::fromStdString(binding.source_name);
			break;
		case JoypadActionType::ToggleSourceMute:
		case JoypadActionType::SetSourceMute:
		case JoypadActionType::SetSourceVolume:
		case JoypadActionType::AdjustSourceVolume:
		case JoypadActionType::SetSourceVolumePercent:
		case JoypadActionType::MediaPlayPause:
		case JoypadActionType::MediaRestart:
		case JoypadActionType::MediaStop:
			source_filter_text = QString::fromStdString(binding.source_name);
			break;
		case JoypadActionType::ToggleFilterEnabled:
		case JoypadActionType::SetFilterEnabled:
			source_filter_text = QString::fromStdString(binding.filter_name);
			break;
		case JoypadActionType::SetFilterProperty:
			source_filter_text = QString::fromStdString(binding.filter_name);
			if (!binding.filter_property_name.empty()) {
				source_filter_text += " :: " + QString::fromStdString(binding.filter_property_name);
			}
			break;
		case JoypadActionType::AdjustFilterProperty:
			source_filter_text = QString::fromStdString(binding.filter_name);
			if (!binding.filter_property_name.empty()) {
				source_filter_text += " :: " + QString::fromStdString(binding.filter_property_name);
			}
			break;
		case JoypadActionType::SourceTransform:
			scene_text = binding.use_current_scene ? L("JoypadToOBS.Common.Current")
							       : QString::fromStdString(binding.scene_name);
			source_filter_text = QString::fromStdString(binding.source_name);
			break;
		case JoypadActionType::Screenshot:
			if (binding.screenshot_target == JoypadScreenshotTarget::Source) {
				source_filter_text = QString::fromStdString(binding.source_name);
			}
			break;
		default:
			break;
		}

		table_->setItem(row, 4, new QTableWidgetItem(scene_text));
		table_->setItem(row, 5, new QTableWidgetItem(source_filter_text));
		table_->setItem(row, 6, new QTableWidgetItem(binding_details(binding)));

		auto *edit_button = new QToolButton(table_);
		edit_button->setText(L("JoypadToOBS.Button.Edit"));
		const QVariant binding_index_value(row);
		edit_button->setProperty("binding_index", binding_index_value);
		table_->setCellWidget(row, 7, edit_button);

		auto *delete_button = new QToolButton(table_);
		delete_button->setText(L("JoypadToOBS.Button.Delete"));
		const QVariant binding_index_value_delete(row);
		delete_button->setProperty("binding_index", binding_index_value_delete);
		table_->setCellWidget(row, 8, delete_button);

		connect(edit_button, &QToolButton::clicked, this, [this, uid]() {
			auto current = config_->GetBindingsSnapshot();
			size_t target_index = (size_t)-1;
			for (size_t i = 0; i < current.size(); ++i) {
				if (current[i].uid == uid) {
					target_index = i;
					break;
				}
			}
			if (target_index == (size_t)-1) {
				return;
			}
			JoypadBindingDialog dialog(this, config_, input_, &current[target_index]);
			if (dialog.exec() == QDialog::Accepted) {
				config_->UpdateBinding(target_index, dialog.Binding());
				RefreshBindings();
			}
		});

		connect(delete_button, &QToolButton::clicked, this, [this, uid]() {
			auto current = config_->GetBindingsSnapshot();
			for (size_t i = 0; i < current.size(); ++i) {
				if (current[i].uid == uid) {
					config_->RemoveBinding(i);
					RefreshBindings();
					break;
				}
			}
		});
	}
	table_->resizeColumnsToContents();
}

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

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QGridLayout>
#include <QHBoxLayout>
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
#include <QTimer>
#include <QStyle>
#include <QFileInfo>
#include <QDir>
#include <QSplitter>
#include <QCloseEvent>
#include <QColorDialog>
#include <QSpinBox>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

inline QString L(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
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
		return (binding.volume_value >= 0.0
				? L("JoypadToOBS.Common.PositiveValue").arg(binding.volume_value, 0, 'f', 2)
				: L("JoypadToOBS.Common.NegativeValue").arg(binding.volume_value, 0, 'f', 2)) +
		       L("JoypadToOBS.Common.MultiplierSuffix");
	case JoypadActionType::SetSourceVolumePercent:
		return QString::number(binding.slider_gamma, 'f', 2) + " " + L("JoypadToOBS.Common.MultiplierSuffix");
	case JoypadActionType::SetFilterEnabled:
		return binding.bool_value ? L("JoypadToOBS.Common.On") : L("JoypadToOBS.Common.Off");
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
			item.has_audio = (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) != 0;
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

	items.erase(std::remove_if(items.begin(), items.end(),
				   [is_audio_action, is_media_action](const SourceItem &item) {
					   if (is_media_action) {
						   return !item.is_media;
					   }
					   if (is_audio_action) {
						   return !item.has_audio;
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

QString input_label_from_event(const JoypadEvent &event)
{
	if (event.is_axis) {
		QString dir = event.axis_value >= 0.0 ? "+" : "-";
		return L("JoypadToOBS.Common.AxisNumber").arg(event.axis_index + 1).arg(dir);
	}
	return L("JoypadToOBS.Common.ButtonNumber").arg(event.button);
}

QString input_label_from_binding(const JoypadBinding &binding)
{
	if (binding.input_type == JoypadInputType::Axis) {
		QString dir = "+";
		if (binding.axis_direction == JoypadAxisDirection::Negative) {
			dir = "-";
		} else if (binding.axis_direction == JoypadAxisDirection::Both) {
			dir = "+/-";
		}
		return L("JoypadToOBS.Common.AxisNumber").arg(binding.axis_index + 1).arg(dir);
	}
	return L("JoypadToOBS.Common.ButtonNumber").arg(binding.button);
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
		setWindowTitle(L("JoypadToOBS.Dialog.AddTitle"));
		setModal(true);

		auto *layout = new QVBoxLayout(this);

		auto *description = new QLabel(L("JoypadToOBS.Dialog.AddDescription"), this);
		description->setWordWrap(true);
		layout->addWidget(description);

		auto *device_group = new QGroupBox(L("JoypadToOBS.Group.DeviceButton"));
		auto *device_layout = new QGridLayout(device_group);

		device_combo_ = new QComboBox(device_group);
		device_combo_->addItem(L("JoypadToOBS.Common.AnyDevice"), QString());

		for (const auto &device : input_->GetDevices()) {
			device_combo_->addItem(QString::fromStdString(device.name), QString::fromStdString(device.id));
		}

		button_label_ = new QLabel(L("JoypadToOBS.Common.NoButtonSelected"), device_group);
		listen_button_ = new QPushButton(L("JoypadToOBS.Common.Listen"), device_group);

		device_layout->addWidget(new QLabel(L("JoypadToOBS.Field.Device")), 0, 0);
		device_layout->addWidget(device_combo_, 0, 1);
		device_layout->addWidget(new QLabel(L("JoypadToOBS.Field.Button")), 1, 0);
		device_layout->addWidget(button_label_, 1, 1);
		device_layout->addWidget(listen_button_, 1, 2);

		axis_value_label_ = new QLabel(L("JoypadToOBS.Field.AxisValue"), device_group);
		axis_value_slider_ = new QSlider(Qt::Horizontal, device_group);
		axis_value_slider_->setRange(0, 1024);
		axis_value_slider_->setValue(0);
		axis_value_slider_->setEnabled(false);
		axis_live_value_label_ = new QLabel("0.00", device_group);

		axis_threshold_label_ = new QLabel(L("JoypadToOBS.Field.AxisThreshold"), device_group);
		axis_threshold_combo_ = new QComboBox(device_group);
		axis_threshold_combo_->addItem(L("JoypadToOBS.AxisThreshold.Any"), 0.0);
		axis_threshold_combo_->addItem(L("JoypadToOBS.AxisThreshold.Strong"), 0.5);
		axis_both_checkbox_ = new QCheckBox(L("JoypadToOBS.Field.AxisBothDirections"), device_group);
		axis_min_label_ = new QLabel(L("JoypadToOBS.Field.AxisMinValue"), device_group);
		axis_max_label_ = new QLabel(L("JoypadToOBS.Field.AxisMaxValue"), device_group);
		axis_set_min_button_ = new QPushButton(L("JoypadToOBS.Button.SetMin"), device_group);
		axis_set_max_button_ = new QPushButton(L("JoypadToOBS.Button.SetMax"), device_group);
		axis_min_label_->setText(L("JoypadToOBS.Field.AxisMinValue") + ": 0");
		axis_max_label_->setText(L("JoypadToOBS.Field.AxisMaxValue") + ": 1024");

		device_layout->addWidget(axis_value_label_, 2, 0);
		device_layout->addWidget(axis_value_slider_, 2, 1);
		device_layout->addWidget(axis_live_value_label_, 2, 2);
		device_layout->addWidget(axis_threshold_label_, 3, 0);
		device_layout->addWidget(axis_threshold_combo_, 3, 1, 1, 2);
		device_layout->addWidget(axis_both_checkbox_, 4, 0, 1, 2);
		device_layout->addWidget(axis_min_label_, 5, 0);
		device_layout->addWidget(axis_set_min_button_, 5, 1);
		device_layout->addWidget(axis_max_label_, 6, 0);
		device_layout->addWidget(axis_set_max_button_, 6, 1);

		layout->addWidget(device_group);

		auto *target_group = new QGroupBox(L("JoypadToOBS.Group.Target"));
		auto *target_layout = new QGridLayout(target_group);

		use_current_scene_ = new QCheckBox(L("JoypadToOBS.Field.UseCurrentScene"), target_group);
		scene_combo_ = new QComboBox(target_group);
		source_combo_ = new QComboBox(target_group);
		filter_combo_ = new QComboBox(target_group);

		target_layout->addWidget(use_current_scene_, 0, 0, 1, 2);
		target_layout->addWidget(new QLabel(L("JoypadToOBS.Field.Scene")), 1, 0);
		target_layout->addWidget(scene_combo_, 1, 1);
		target_layout->addWidget(new QLabel(L("JoypadToOBS.Field.Source")), 2, 0);
		target_layout->addWidget(source_combo_, 2, 1);
		target_layout->addWidget(new QLabel(L("JoypadToOBS.Field.Filter")), 3, 0);
		target_layout->addWidget(filter_combo_, 3, 1);

		auto *action_group = new QGroupBox(L("JoypadToOBS.Group.Action"));
		auto *action_layout = new QGridLayout(action_group);

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

		bool_checkbox_ = new QCheckBox(L("JoypadToOBS.Common.Enable"), action_group);
		volume_spin_ = new QDoubleSpinBox(action_group);
		volume_allow_above_unity_ = new QCheckBox(L("JoypadToOBS.Field.AllowAboveDb"), action_group);
		invert_axis_checkbox_ = new QCheckBox(L("JoypadToOBS.Field.InvertAxis"), action_group);
		volume_spin_->setRange(-60.0, 20.0);
		volume_spin_->setSingleStep(1.0);
		volume_spin_->setValue(0.0);
		volume_spin_->setSuffix(" dB");

		action_layout->addWidget(new QLabel(L("JoypadToOBS.Field.Action")), 0, 0);
		action_layout->addWidget(action_combo_, 0, 1);
		action_layout->addWidget(bool_checkbox_, 1, 0, 1, 2);
		volume_label_ = new QLabel(L("JoypadToOBS.Field.Volume"), action_group);
		action_layout->addWidget(volume_label_, 2, 0);
		action_layout->addWidget(volume_spin_, 2, 1);
		action_layout->addWidget(volume_allow_above_unity_, 3, 0, 1, 2);
		action_layout->addWidget(invert_axis_checkbox_, 4, 0, 1, 2);

		layout->addWidget(action_group);
		layout->addWidget(target_group);

		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		layout->addWidget(buttons);

		connect(buttons, &QDialogButtonBox::accepted, this, &JoypadBindingDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, this, &JoypadBindingDialog::reject);
		connect(listen_button_, &QPushButton::clicked, this, &JoypadBindingDialog::OnListen);
		connect(action_combo_, &QComboBox::currentIndexChanged, this, &JoypadBindingDialog::UpdateActionUi);
		connect(volume_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
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
		connect(source_combo_, &QComboBox::currentIndexChanged, this, &JoypadBindingDialog::ReloadFilters);
		connect(axis_set_min_button_, &QPushButton::clicked, this, [this]() {
			binding_.axis_min_value = last_axis_value_;
			axis_min_label_->setText(L("JoypadToOBS.Field.AxisMinValue") + ": " +
						 QString::number(binding_.axis_min_value, 'f', 2));
		});
		connect(axis_set_max_button_, &QPushButton::clicked, this, [this]() {
			binding_.axis_max_value = last_axis_value_;
			axis_max_label_->setText(L("JoypadToOBS.Field.AxisMaxValue") + ": " +
						 QString::number(binding_.axis_max_value, 'f', 2));
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
				if (!learned_event_.device_id.empty() && event.device_id != learned_event_.device_id) {
					return;
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
			UpdateActionUi();
			UpdateAxisUi(false);
		}
		refresh_timer_ = new QTimer(this);
		connect(refresh_timer_, &QTimer::timeout, this, &JoypadBindingDialog::RefreshDeviceList);
		refresh_timer_->start(1000);
	}

	~JoypadBindingDialog() override
	{
		if (refresh_timer_) {
			refresh_timer_->stop();
			refresh_timer_ = nullptr;
		}
		if (input_) {
			input_->CancelLearn();
			if (axis_handler_id_ > 0) {
				input_->RemoveOnAxisChanged(axis_handler_id_);
				axis_handler_id_ = 0;
			}
		}
	}

	JoypadBinding Binding() const { return binding_; }

protected:
	void accept() override
	{
		if (!ReadBinding()) {
			return;
		}
		QDialog::accept();
	}

private:
	void RefreshDeviceList()
	{
		if (!input_) {
			return;
		}
		auto devices = input_->GetDevices();
		bool changed = false;
		if (device_combo_->count() - 1 != (int)devices.size()) {
			changed = true;
		} else {
			for (size_t i = 0; i < devices.size(); ++i) {
				QString id = device_combo_->itemData((int)i + 1).toString();
				if (id.toStdString() != devices[i].id) {
					changed = true;
					break;
				}
			}
		}

		if (changed) {
			QString current_id = device_combo_->currentData().toString();
			QString current_text = device_combo_->currentText();
			device_combo_->blockSignals(true);
			device_combo_->clear();
			device_combo_->addItem(L("JoypadToOBS.Common.AnyDevice"), QString());
			for (const auto &device : devices) {
				device_combo_->addItem(QString::fromStdString(device.name),
						       QString::fromStdString(device.id));
			}
			int idx = device_combo_->findData(current_id);
			if (idx >= 0) {
				device_combo_->setCurrentIndex(idx);
			} else if (!current_id.isEmpty()) {
				device_combo_->addItem(current_text, current_id);
				device_combo_->setCurrentIndex(device_combo_->count() - 1);
			}
			device_combo_->blockSignals(false);
		}
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
		axis_threshold_label_->setVisible(visible && !hide_axis_options);
		axis_threshold_combo_->setVisible(visible && !hide_axis_options);
		axis_both_checkbox_->setVisible(visible && !hide_axis_options);
		axis_min_label_->setVisible(visible && hide_axis_options);
		axis_max_label_->setVisible(visible && hide_axis_options);
		axis_set_min_button_->setVisible(visible && hide_axis_options);
		axis_set_max_button_->setVisible(visible && hide_axis_options);
		if (!visible) {
			axis_both_checkbox_->setChecked(false);
		}
		if (hide_axis_options) {
			axis_both_checkbox_->setChecked(false);
		}
	}

	void ApplyBinding(const JoypadBinding &binding)
	{
		binding_ = binding;
		learned_event_.button = binding.button;
		learned_event_.is_axis = (binding.input_type == JoypadInputType::Axis);
		learned_event_.axis_index = binding.axis_index;
		learned_event_.axis_value = (binding.axis_direction == JoypadAxisDirection::Negative)
						    ? -binding.axis_threshold
						    : binding.axis_threshold;
		learned_event_.device_id = binding.device_id;
		learned_event_.device_name = binding.device_name;
		button_label_->setText(input_label_from_event(learned_event_));
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
			int idx = axis_threshold_combo_->findData(binding.axis_threshold);
			if (idx >= 0) {
				axis_threshold_combo_->setCurrentIndex(idx);
			}
			axis_both_checkbox_->setChecked(binding.axis_direction == JoypadAxisDirection::Both);
			axis_min_label_->setText(L("JoypadToOBS.Field.AxisMinValue") + ": " +
						 QString::number(binding.axis_min_value, 'f', 2));
			axis_max_label_->setText(L("JoypadToOBS.Field.AxisMaxValue") + ": " +
						 QString::number(binding.axis_max_value, 'f', 2));
		}

		int device_index = device_combo_->findData(QString::fromStdString(binding.device_id));
		if (device_index < 0 && !binding.device_id.empty()) {
			device_combo_->addItem(QString::fromStdString(binding.device_name),
					       QString::fromStdString(binding.device_id));
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

		bool_checkbox_->setChecked(binding.bool_value);
		volume_allow_above_unity_->setChecked(binding.allow_above_unity);
		if (binding.action == JoypadActionType::SetSourceVolumePercent) {
			volume_spin_->setValue(binding.slider_gamma);
			invert_axis_checkbox_->setChecked(binding.axis_direction == JoypadAxisDirection::Negative);
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
		filter_combo_->clear();
		auto names = get_filter_names_for_source(source_combo_->currentData().toString().toStdString());
		for (const auto &name : names) {
			filter_combo_->addItem(QString::fromStdString(name));
		}
	}

	void UpdateActionUi()
	{
		auto action = CurrentAction();
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
			(action == JoypadActionType::SetFilterEnabled);
		bool needs_filter = (action == JoypadActionType::ToggleFilterEnabled) ||
				    (action == JoypadActionType::SetFilterEnabled);

		scene_combo_->setEnabled(needs_scene);
		use_current_scene_->setEnabled(needs_scene && action != JoypadActionType::SwitchScene);
		if (use_current_scene_->isEnabled() && use_current_scene_->isChecked()) {
			scene_combo_->setEnabled(false);
		}
		source_combo_->setEnabled(needs_source);
		filter_combo_->setEnabled(needs_filter);

		ReloadSourcesForAction(action);
		ReloadFilters();

		bool show_bool = (action == JoypadActionType::SetSourceVisibility) ||
				 (action == JoypadActionType::SetSourceMute) ||
				 (action == JoypadActionType::SetFilterEnabled);
		bool show_volume = (action == JoypadActionType::SetSourceVolume) ||
				   (action == JoypadActionType::AdjustSourceVolume) ||
				   (action == JoypadActionType::SetSourceVolumePercent);
		bool show_above_unity = (action == JoypadActionType::SetSourceVolume) ||
					(action == JoypadActionType::AdjustSourceVolume);
		bool show_invert = (action == JoypadActionType::SetSourceVolumePercent);

		bool_checkbox_->setVisible(show_bool);
		volume_label_->setVisible(show_volume);
		volume_spin_->setVisible(show_volume);
		volume_allow_above_unity_->setVisible(show_above_unity);
		invert_axis_checkbox_->setVisible(show_invert);
		if (!show_above_unity) {
			volume_allow_above_unity_->setChecked(false);
		}

		if (action == JoypadActionType::AdjustSourceVolume) {
			volume_spin_->setRange(-1.0, 1.0);
			volume_spin_->setSingleStep(0.05);
			volume_spin_->setDecimals(2);
			volume_spin_->setSuffix("x");
			if (volume_spin_->value() == 0.0) {
				volume_spin_->setValue(0.05);
			}
		} else if (action == JoypadActionType::SetSourceVolume) {
			volume_spin_->setRange(-60.0, 20.0);
			volume_spin_->setSingleStep(1.0);
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
		}
		if (show_volume) {
			volume_allow_above_unity_->setChecked(binding_.allow_above_unity);
		}
		if (action == JoypadActionType::SetSourceVolumePercent) {
			volume_label_->setText(L("JoypadToOBS.Field.Multiplier"));
		} else {
			volume_label_->setText(L("JoypadToOBS.Field.Volume"));
		}
		UpdateAxisUi(learned_event_.is_axis);
	}

	void OnListen()
	{
		if (!input_) {
			return;
		}
		if (is_listening_) {
			input_->CancelLearn();
			is_listening_ = false;
			listen_button_->setText(L("JoypadToOBS.Common.Listen"));
			if (learned_event_.button > 0 || learned_event_.is_axis) {
				button_label_->setText(input_label_from_event(learned_event_));
			} else {
				button_label_->setText(L("JoypadToOBS.Common.NoButtonSelected"));
			}
			return;
		}
		button_label_->setText(L("JoypadToOBS.Common.PressButtonOrAxis"));
		bool ok = input_->BeginLearn([this](const JoypadEvent &event) {
			QMetaObject::invokeMethod(
				this,
				[this, event]() {
					QString conflicts;
					auto bindings = config_->GetBindingsSnapshot();
					for (const auto &b : bindings) {
						bool match = false;
						if (event.is_axis) {
							if (b.input_type == JoypadInputType::Axis &&
							    b.device_id == event.device_id &&
							    b.axis_index == event.axis_index) {
								match = true;
							}
						} else {
							if (b.input_type == JoypadInputType::Button &&
							    b.device_id == event.device_id &&
							    b.button == event.button) {
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
							conflicts += QString("• %1 (%2)\n").arg(actionName, target);
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

					is_listening_ = false;
					listen_button_->setText(L("JoypadToOBS.Common.Listen"));
					learned_event_ = event;
					button_label_->setText(input_label_from_event(event));
					UpdateAxisUi(event.is_axis);
					if (event.is_axis) {
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
						} else {
							axis_value_slider_->setValue((int)event.axis_raw_value);
							axis_live_value_label_->setText(
								QString::number(last_axis_value_, 'f', 2));
						}
					}
					SelectDevice(event);
				},
				Qt::QueuedConnection);
		});
		if (ok) {
			is_listening_ = true;
			listen_button_->setText(L("JoypadToOBS.Common.Listen") + "...");
		} else {
			button_label_->setText(L("JoypadToOBS.Common.AlreadyListening"));
		}
	}

	void SelectDevice(const JoypadEvent &event)
	{
		int index = device_combo_->findData(QString::fromStdString(event.device_id));
		if (index >= 0) {
			device_combo_->setCurrentIndex(index);
		}
	}

	bool ReadBinding()
	{
		if (!learned_event_.is_axis && learned_event_.button <= 0) {
			button_label_->setText(L("JoypadToOBS.Common.PressButtonOrAxisFirst"));
			return false;
		}

		binding_.button = learned_event_.button;
		binding_.device_id = device_combo_->currentData().toString().toStdString();
		binding_.device_name = device_combo_->currentText().toStdString();

		if (CurrentAction() == JoypadActionType::SetSourceVolumePercent && !learned_event_.is_axis) {
			button_label_->setText(L("JoypadToOBS.Common.AxisOnlyForSlider"));
			return false;
		}

		if (learned_event_.is_axis) {
			binding_.input_type = JoypadInputType::Axis;
			binding_.axis_index = learned_event_.axis_index;
			if (CurrentAction() == JoypadActionType::SetSourceVolumePercent) {
				binding_.axis_direction = invert_axis_checkbox_->isChecked()
								  ? JoypadAxisDirection::Negative
								  : JoypadAxisDirection::Both;
			} else if (axis_both_checkbox_->isChecked()) {
				binding_.axis_direction = JoypadAxisDirection::Both;
			} else {
				binding_.axis_direction = (learned_event_.axis_value >= 0.0)
								  ? JoypadAxisDirection::Positive
								  : JoypadAxisDirection::Negative;
			}
			binding_.axis_threshold = axis_threshold_combo_->currentData().toDouble();
			binding_.axis_interval_ms = 150;
			binding_.axis_min_value = binding_.axis_min_value;
			binding_.axis_max_value = binding_.axis_max_value;
		} else {
			binding_.input_type = JoypadInputType::Button;
			binding_.axis_index = -1;
			binding_.axis_threshold = 0.5;
		}

		binding_.action = CurrentAction();
		binding_.use_current_scene = use_current_scene_->isChecked();
		binding_.scene_name = scene_combo_->currentText().toStdString();
		binding_.source_name = source_combo_->currentData().toString().toStdString();
		binding_.filter_name = filter_combo_->currentText().toStdString();
		binding_.bool_value = bool_checkbox_->isChecked();
		bool is_volume_action = (binding_.action == JoypadActionType::SetSourceVolume) ||
					(binding_.action == JoypadActionType::AdjustSourceVolume) ||
					(binding_.action == JoypadActionType::SetSourceVolumePercent);
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

		// Cleanup unused fields based on action
		if (binding_.input_type != JoypadInputType::Axis) {
			binding_.axis_index = -1;
			binding_.axis_threshold = 0.5;
			binding_.axis_min_value = 0.0;
			binding_.axis_max_value = 1024.0;
		}

		bool needs_scene = (binding_.action == JoypadActionType::SwitchScene) ||
				   (binding_.action == JoypadActionType::ToggleSourceVisibility) ||
				   (binding_.action == JoypadActionType::SetSourceVisibility);
		bool needs_use_current = (binding_.action == JoypadActionType::ToggleSourceVisibility ||
					  binding_.action == JoypadActionType::SetSourceVisibility);
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
				    (binding_.action == JoypadActionType::SetFilterEnabled);
		bool needs_filter = (binding_.action == JoypadActionType::ToggleFilterEnabled ||
				     binding_.action == JoypadActionType::SetFilterEnabled);
		bool needs_bool = (binding_.action == JoypadActionType::SetSourceVisibility ||
				   binding_.action == JoypadActionType::SetSourceMute ||
				   binding_.action == JoypadActionType::SetFilterEnabled);
		bool needs_volume = (binding_.action == JoypadActionType::SetSourceVolume ||
				     binding_.action == JoypadActionType::AdjustSourceVolume);
		bool needs_gamma = (binding_.action == JoypadActionType::SetSourceVolumePercent);
		bool needs_unity = (binding_.action == JoypadActionType::SetSourceVolume ||
				    binding_.action == JoypadActionType::AdjustSourceVolume);

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

	JoypadConfigStore *config_ = nullptr;
	JoypadInputManager *input_ = nullptr;
	const JoypadBinding *existing_ = nullptr;
	JoypadBinding binding_;
	JoypadEvent learned_event_;

	QComboBox *device_combo_ = nullptr;
	QLabel *button_label_ = nullptr;
	QPushButton *listen_button_ = nullptr;
	QLabel *axis_value_label_ = nullptr;
	QSlider *axis_value_slider_ = nullptr;
	QLabel *axis_threshold_label_ = nullptr;
	QComboBox *axis_threshold_combo_ = nullptr;
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

	QComboBox *action_combo_ = nullptr;
	QCheckBox *bool_checkbox_ = nullptr;
	QLabel *volume_label_ = nullptr;
	QDoubleSpinBox *volume_spin_ = nullptr;
	QCheckBox *volume_allow_above_unity_ = nullptr;
	QCheckBox *invert_axis_checkbox_ = nullptr;
	int axis_handler_id_ = 0;
	QTimer *refresh_timer_ = nullptr;
	bool is_listening_ = false;
};

} // namespace

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

	QLabel *developerLabel = new QLabel(
		"<a href=\"https://github.com/FabioZumbi12\" style=\"color: gray; text-decoration: none;\"><i>Developed by FabioZumbi12</i></a>",
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
			currentColor = "#ffffff";
			currentBgColor = "#000000";
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
		const QString device = binding.device_id.empty() ? L("JoypadToOBS.Common.Any")
								 : QString::fromStdString(binding.device_name);

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
		default:
			break;
		}

		table_->setItem(row, 4, new QTableWidgetItem(scene_text));
		table_->setItem(row, 5, new QTableWidgetItem(source_filter_text));
		table_->setItem(row, 6, new QTableWidgetItem(binding_details(binding)));

		auto *edit_button = new QToolButton(table_);
		edit_button->setText(L("JoypadToOBS.Button.Edit"));
		edit_button->setProperty("binding_index", row);
		table_->setCellWidget(row, 7, edit_button);

		auto *delete_button = new QToolButton(table_);
		delete_button->setText(L("JoypadToOBS.Button.Delete"));
		delete_button->setProperty("binding_index", row);
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

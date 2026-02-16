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
#include "joypad-config.h"
#include "joypad-input.h"
#include "joypad-ui.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QAction>
#include <QWidget>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <QMetaObject>
#include <QCoreApplication>
#include <QLabel>
#include <QTimer>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {
JoypadConfigStore g_config;
JoypadInputManager g_input;
JoypadActionEngine g_actions;

QAction *g_tools_action = nullptr;
JoypadToolsDialog *g_dialog = nullptr;

std::string MakeAxisKey(const JoypadBinding &binding)
{
	std::string key = binding.device_id;
	key += "|";
	key += binding.source_name;
	key += "|";
	key += std::to_string(binding.axis_index);
	return key;
}

double MapRawToPercentWithGamma(const JoypadBinding &binding, double raw)
{
	double minv = binding.axis_min_value;
	double maxv = binding.axis_max_value;
	if (maxv <= minv) {
		minv = 0.0;
		maxv = 1024.0;
	}
	double percent = ((raw - minv) / (maxv - minv)) * 100.0;
	if (binding.axis_direction == JoypadAxisDirection::Negative) {
		percent = 100.0 - percent;
	}
	percent = std::clamp(percent, 0.0, 100.0);
	double base = percent / 100.0;
	base = std::clamp(base, 0.0, 1.0);
	double gamma = binding.slider_gamma > 0.0 ? binding.slider_gamma : 0.6;
	gamma = std::clamp(gamma, 0.1, 50.0);
	double curved = std::pow(base, gamma);
	return std::clamp(curved * 100.0, 0.0, 100.0);
}

void ApplyStoredAxisValues()
{
	auto bindings = g_config.GetBindingsSnapshot();
	for (const auto &binding_ref : bindings) {
		if (!binding_ref.enabled) {
			continue;
		}
		if (binding_ref.action != JoypadActionType::SetSourceVolumePercent ||
		    binding_ref.input_type != JoypadInputType::Axis) {
			continue;
		}
		double stored_raw = 0.0;
		std::string key = MakeAxisKey(binding_ref);
		if (!g_config.ConsumeAxisLastRaw(key, stored_raw)) {
			continue;
		}
		JoypadBinding adjusted = binding_ref;
		adjusted.volume_value = MapRawToPercentWithGamma(binding_ref, stored_raw);
		g_actions.Execute(adjusted);
	}
}

void StoreAxisLastRawOnShutdown()
{
	// Values are now updated in real-time in OnAxisChanged
}

static void save_hotkeys(obs_data_t *save_data, bool saving, void *private_data)
{
	(void)save_data;
	(void)saving;
	(void)private_data;
	g_config.Save();
	if (g_dialog) {
		QMetaObject::invokeMethod(g_dialog, []() { g_dialog->RefreshProfiles(); }, Qt::QueuedConnection);
	}
}
} // namespace

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "joypad-to-obs loaded (version %s)", PLUGIN_VERSION);

	g_config.Load();

	g_config.SetProfileSwitchCallback([](const std::string &name) {
		if (!g_config.GetOsdEnabled())
			return;

		QMetaObject::invokeMethod(QCoreApplication::instance(), [name]() {
			QWidget *main_window = (QWidget *)obs_frontend_get_main_window();
			if (!main_window)
				return;

			QLabel *label = new QLabel(main_window);
			label->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
			label->setAttribute(Qt::WA_TranslucentBackground);
			label->setAttribute(Qt::WA_ShowWithoutActivating);
			QString color = QString::fromStdString(g_config.GetOsdColor());
			int size = g_config.GetOsdFontSize();
			QString bg_color = QString::fromStdString(g_config.GetOsdBackgroundColor());
			label->setStyleSheet(QString("QLabel { background-color: %3; color: %1; "
						     "border-radius: 8px; padding: 12px; font-size: %2px; "
						     "font-weight: bold; border: 2px solid %1; }")
						     .arg(color)
						     .arg(size)
						     .arg(bg_color));
			label->setText(QString("Joypad Profile: %1").arg(QString::fromStdString(name)));
			label->adjustSize();

			QRect r = main_window->geometry();
			int m = 40; // Margin
			int x = 0;
			int y = 0;
			int w = label->width();
			int h = label->height();

			switch (g_config.GetOsdPosition()) {
			case JoypadOsdPosition::TopLeft:
				x = r.x() + m;
				y = r.y() + m;
				break;
			case JoypadOsdPosition::TopCenter:
				x = r.x() + (r.width() - w) / 2;
				y = r.y() + m;
				break;
			case JoypadOsdPosition::TopRight:
				x = r.x() + r.width() - w - m;
				y = r.y() + m;
				break;
			case JoypadOsdPosition::CenterLeft:
				x = r.x() + m;
				y = r.y() + (r.height() - h) / 2;
				break;
			case JoypadOsdPosition::Center:
				x = r.x() + (r.width() - w) / 2;
				y = r.y() + (r.height() - h) / 2;
				break;
			case JoypadOsdPosition::CenterRight:
				x = r.x() + r.width() - w - m;
				y = r.y() + (r.height() - h) / 2;
				break;
			case JoypadOsdPosition::BottomLeft:
				x = r.x() + m;
				y = r.y() + r.height() - h - m;
				break;
			case JoypadOsdPosition::BottomCenter:
				x = r.x() + (r.width() - w) / 2;
				y = r.y() + r.height() - h - m;
				break;
			case JoypadOsdPosition::BottomRight:
				x = r.x() + r.width() - w - m;
				y = r.y() + r.height() - h - m;
				break;
			}
			label->move(x, y);
			label->show();

			QTimer::singleShot(2000, label, [label]() {
				QGraphicsOpacityEffect *eff = new QGraphicsOpacityEffect(label);
				label->setGraphicsEffect(eff);
				QPropertyAnimation *a = new QPropertyAnimation(eff, "opacity");
				a->setDuration(500);
				a->setStartValue(1);
				a->setEndValue(0);
				a->setEasingCurve(QEasingCurve::OutQuad);
				QObject::connect(a, &QPropertyAnimation::finished, label, &QLabel::deleteLater);
				a->start(QAbstractAnimation::DeleteWhenStopped);
			});
		});
	});

	g_input.SetOnButtonPressed([](const JoypadEvent &event) {
		auto matches = g_config.FindMatchingBindings(event);
		for (const auto &binding : matches) {
			g_actions.Execute(binding);
		}
	});
	g_input.SetOnAxisChanged([](const JoypadEvent &event) {
		if (!event.is_axis) {
			return;
		}
		auto matches = g_config.FindMatchingBindings(event);
		if (matches.empty()) {
			return;
		}
		obs_log(LOG_INFO, "axis raw: device=%s axis=%d value=%.3f", event.device_name.c_str(), event.axis_index,
			event.axis_raw_value);
		for (const auto &binding : matches) {
			g_actions.Execute(binding);
			if (binding.action == JoypadActionType::SetSourceVolumePercent) {
				g_config.SetAxisLastRaw(MakeAxisKey(binding), event.axis_raw_value);
			}
		}
	});
	ApplyStoredAxisValues();
	g_input.Start();

	obs_frontend_add_save_callback(save_hotkeys, nullptr);

	g_tools_action = reinterpret_cast<QAction *>(
		obs_frontend_add_tools_menu_qaction(obs_module_text("JoypadToOBS.MenuTitle")));
	QObject::connect(g_tools_action, &QAction::triggered, []() {
		if (!g_dialog) {
			auto *parent = reinterpret_cast<QWidget *>(obs_frontend_get_main_window());
			g_dialog = new JoypadToolsDialog(parent, &g_config, &g_input);
		}
		g_dialog->show();
		g_dialog->raise();
		g_dialog->activateWindow();
	});

	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_save_callback(save_hotkeys, nullptr);
	StoreAxisLastRawOnShutdown();
	g_config.Unload();
	g_input.Stop();

	if (g_dialog) {
		g_dialog->close();
		delete g_dialog;
		g_dialog = nullptr;
	}

	if (g_tools_action) {
		g_tools_action->deleteLater();
		g_tools_action = nullptr;
	}

	obs_log(LOG_INFO, "joypad-to-obs unloaded");
}

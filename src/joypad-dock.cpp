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

#include "joypad-dock.h"
#include "joypad-ui.h"

#include <obs-module.h>

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>

namespace {
inline QString L(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}
} // namespace

void JoypadPluginOpenToolsDialog();

JoypadControlDock::JoypadControlDock(QWidget *parent, JoypadConfigStore *config) : QDockWidget(parent), config_(config)
{
	setObjectName(QStringLiteral("joypad_to_obs_dock"));
	setWindowTitle(L("JoypadToOBS.Dock.Title"));

	auto *content = new QWidget(this);
	auto *layout = new QVBoxLayout(content);
	layout->setContentsMargins(6, 6, 6, 6);
	layout->setSpacing(6);
	layout->setAlignment(Qt::AlignTop);

	auto *profile_row = new QHBoxLayout();
	profile_row->setContentsMargins(0, 0, 0, 0);
	profile_row->setSpacing(6);
	profile_row->addWidget(new QLabel(L("JoypadToOBS.Profile.Name"), content), 0);

	profile_combo_ = new QComboBox(content);
	profile_combo_->setMinimumContentsLength(12);
	profile_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	profile_combo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	profile_row->addWidget(profile_combo_, 2);

	open_config_button_ = new QPushButton(content);
	open_config_button_->setText(QString());
	open_config_button_->setFixedWidth(34);
	open_config_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
	open_config_button_->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
	open_config_button_->setToolTip(L("JoypadToOBS.Dock.OpenConfig"));
	profile_row->addWidget(open_config_button_, 0);

	input_toggle_button_ = new QPushButton(content);
	input_toggle_button_->setCheckable(true);
	input_toggle_button_->setText(QString());
	input_toggle_button_->setFixedWidth(34);
	input_toggle_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
	profile_row->addWidget(input_toggle_button_, 0);

	layout->addLayout(profile_row);
	setWidget(content);

	connect(profile_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
		if (!config_) {
			return;
		}
		config_->SetCurrentProfile(index);
		RefreshState();
	});
	connect(input_toggle_button_, &QPushButton::toggled, this,
		[](bool enabled) { JoypadUiSetInputListeningEnabled(enabled); });
	connect(open_config_button_, &QPushButton::clicked, this, []() { JoypadPluginOpenToolsDialog(); });

	update_timer_ = new QTimer(this);
	connect(update_timer_, &QTimer::timeout, this, [this]() { RefreshState(); });
	update_timer_->start(200);

	RefreshState();
}

void JoypadControlDock::RefreshState()
{
	RefreshProfiles();
	if (!input_toggle_button_) {
		return;
	}

	const bool enabled = JoypadUiIsInputListeningEnabled();
	if (input_toggle_button_->isChecked() != enabled) {
		const QSignalBlocker blocker(input_toggle_button_);
		input_toggle_button_->setChecked(enabled);
	}
	const QString status = enabled ? L("JoypadToOBS.Common.On") : L("JoypadToOBS.Common.Off");
	const QStyle::StandardPixmap icon_type = enabled ? QStyle::SP_DialogApplyButton : QStyle::SP_DialogCancelButton;
	input_toggle_button_->setIcon(style()->standardIcon(icon_type));
	input_toggle_button_->setToolTip(L("JoypadToOBS.Dock.ListeningButton").arg(status));
}

void JoypadControlDock::RefreshProfiles()
{
	if (!config_ || !profile_combo_) {
		return;
	}

	const auto names = config_->GetProfileNames();
	bool needs_rebuild = profile_combo_->count() != (int)names.size();
	if (!needs_rebuild) {
		for (int i = 0; i < profile_combo_->count(); ++i) {
			if (profile_combo_->itemData(i).toString() != QString::fromStdString(names[(size_t)i])) {
				needs_rebuild = true;
				break;
			}
		}
	}

	if (needs_rebuild) {
		const QSignalBlocker blocker(profile_combo_);
		profile_combo_->clear();
		for (const auto &name : names) {
			const QString q_name = QString::fromStdString(name);
			profile_combo_->addItem(q_name, q_name);
		}
	}

	const int current = config_->GetCurrentProfileIndex();
	if (current >= 0 && current < profile_combo_->count() && profile_combo_->currentIndex() != current) {
		const QSignalBlocker blocker(profile_combo_);
		profile_combo_->setCurrentIndex(current);
	}
}

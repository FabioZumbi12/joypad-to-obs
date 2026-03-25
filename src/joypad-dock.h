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

#include "joypad-config.h"

#include <QDockWidget>

class QComboBox;
class QPushButton;
class QTimer;

class JoypadControlDock : public QDockWidget {
public:
	JoypadControlDock(QWidget *parent, JoypadConfigStore *config);
	void RefreshState();

private:
	void RefreshProfiles();

	JoypadConfigStore *config_ = nullptr;
	QComboBox *profile_combo_ = nullptr;
	QPushButton *input_toggle_button_ = nullptr;
	QTimer *update_timer_ = nullptr;
};

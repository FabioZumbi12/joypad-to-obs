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

#pragma once

#include "joypad-config.h"
#include "joypad-input.h"

#include <QDialog>

class QTableWidget;
class QPushButton;
class QLabel;

class JoypadToolsDialog : public QDialog {
public:
	JoypadToolsDialog(QWidget *parent, JoypadConfigStore *config,
			  JoypadInputManager *input);

	void RefreshBindings();

private:
	int SelectedRow() const;

	JoypadConfigStore *config_ = nullptr;
	JoypadInputManager *input_ = nullptr;

	QTableWidget *table_ = nullptr;
	QPushButton *add_button_ = nullptr;
	QPushButton *remove_button_ = nullptr;
	QLabel *axis_live_label_ = nullptr;
};

/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace Ui {
class RippleButton;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Telegator {

// Buttons placed next to the attach button in the compose area:
// the one that shows or hides the side panel, for accounts with the panel.
class ComposeButtons final {
public:
	ComposeButtons(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		rpl::producer<bool> shown);
	~ComposeButtons();

	// Horizontal space the buttons take in the compose area.
	[[nodiscard]] int width() const;

	// Places the buttons starting at left, returns the next free left.
	[[nodiscard]] int moveToLeft(int left, int top);

private:
	object_ptr<Ui::RippleButton> _panel;

};

} // namespace Telegator

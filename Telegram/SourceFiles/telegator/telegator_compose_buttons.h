/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace Ui {
class RoundButton;
} // namespace Ui

namespace Telegator {

// Action buttons placed next to the attach button in the compose area.
class ComposeButtons final {
public:
	ComposeButtons(
		not_null<QWidget*> parent,
		rpl::producer<bool> shown,
		Fn<void(const QString &text)> sendText);
	~ComposeButtons();

	// Horizontal space the buttons take in the compose area.
	[[nodiscard]] int width() const;

	// Places the buttons starting at left, returns the next free left.
	[[nodiscard]] int moveToLeft(int left, int top);

private:
	object_ptr<Ui::RoundButton> _test;

};

} // namespace Telegator

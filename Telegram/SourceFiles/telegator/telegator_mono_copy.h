/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Ui::Text {
class String;
} // namespace Ui::Text

namespace Telegator {

// WHY: lib_ui gives the copy link only to one-line `code`, so a click on
// multi-line `code` copied nothing; Telegram for macOS copies both.
// The same text under the pointer must return the same link object.
[[nodiscard]] ClickHandlerPtr MultilineCodeLink(
	not_null<const HistoryItem*> item,
	const Ui::Text::String &text,
	QPoint point,
	int width);

} // namespace Telegator

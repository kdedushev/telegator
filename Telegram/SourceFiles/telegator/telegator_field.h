/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class InputField;
} // namespace Ui

namespace Telegator {

// Puts text at the cursor of the message field. An empty field takes the
// text with its formatting, otherwise it goes in as plain text so that the
// field keeps its own formatting around the cursor.
void InsertAtCursor(not_null<Ui::InputField*> field, TextWithTags text);

} // namespace Telegator

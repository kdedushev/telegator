/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Telegator {

// Adds "Реквизиты" to the message context menu of the panel account.
// The message goes to the owner's script (requisites.url in telegator.json)
// as JSON {action, account_id, chat, message}; the script answers
// {"text": "...", "pin": true} or {"error": "..."}. The text is put into
// the message field to be checked and sent by hand; with "pin" the message
// sent with exactly that text is pinned for the account only, silently.
void AddMessageActions(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	HistoryItem *item);

} // namespace Telegator

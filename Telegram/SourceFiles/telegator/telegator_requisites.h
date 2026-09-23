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

void AddRequisitesAction(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	HistoryItem *item);

} // namespace Telegator

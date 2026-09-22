/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

namespace Main {
class Session;
} // namespace Main

namespace Dialogs {
class InnerWidget;
} // namespace Dialogs

namespace Telegator {

// Shows a chat by its numeric id typed into the chats list search:
// "123456789", "1 234 567 890" as the profile shows it, "id: 123456789",
// Bot API "-100123..." for channels and "-123..." for groups.
//
// Same idea as searchUserById in AyuGram Desktop
// (Telegram/SourceFiles/ayu/utils/telegram_helpers.cpp, GPLv3), without its
// lookup through a third-party bot: ids of our clients never leave the app.
// Telegram opens a chat only with a known access hash, so only chats and
// contacts of this account are found. An unknown id waits until the chats
// list and the archive are loaded, the archive is loaded on purpose.
class IdSearch final : public base::has_weak_ptr {
public:
	IdSearch(
		not_null<Dialogs::InnerWidget*> inner,
		not_null<Main::Session*> session);

	// True when the ids in the query differ from the last search() call:
	// the added rows must go even if Telegram's normalized filter stays
	// the same, as for "12345" and "-12345".
	[[nodiscard]] bool idsChanged(const QString &query) const;

	// Called on every search state change, chatsList tells whether
	// the chats list is searched now.
	void search(const QString &query, bool chatsList);

private:
	bool showLoaded();
	void step();
	void requestMore();
	[[nodiscard]] bool everythingLoaded() const;

	const not_null<Dialogs::InnerWidget*> _inner;
	const not_null<Main::Session*> _session;
	std::vector<PeerId> _ids;
	int _generation = 0;
	rpl::lifetime _waiting;

};

} // namespace Telegator

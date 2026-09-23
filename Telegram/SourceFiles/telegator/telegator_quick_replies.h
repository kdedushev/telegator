/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Data {
struct Shortcut;
} // namespace Data

namespace Telegator {

struct QuickReply {
	BusinessShortcutId id = 0;
	QString name;
	int count = 0;
	QString preview; // Text of the first message, empty until it is loaded.
};

// Quick replies in the order set on the phone, the way Telegram shows them
// on iPhone and Android. Desktop Telegram sorts them by id instead.
[[nodiscard]] std::vector<Data::Shortcut> OrderedShortcuts(
	not_null<Main::Session*> session);

// Text of the first message in one line, empty until the messages load.
[[nodiscard]] QString ShortcutPreview(
	not_null<Main::Session*> session,
	BusinessShortcutId id);

// Business quick replies of the account ("заготовки"), edited on the phone
// in Settings > Telegram Business > Quick Replies and synced everywhere.
// The side panel shows them as buttons.
class QuickReplies final {
public:
	explicit QuickReplies(not_null<Main::Session*> session);

	[[nodiscard]] rpl::producer<> changes() const;
	[[nodiscard]] std::vector<QuickReply> list() const;

	// A reply of one text message goes into the field of peer's chat to be
	// checked before sending. Replies with media or several messages are sent
	// right away, the way Telegram sends a quick reply chosen after "/".
	void use(
		BusinessShortcutId id,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer);

private:
	void loadMessages();
	[[nodiscard]] std::vector<not_null<HistoryItem*>> messages(
		BusinessShortcutId id) const;

	const not_null<Main::Session*> _session;
	base::flat_map<BusinessShortcutId, rpl::lifetime> _loading;
	rpl::event_stream<> _changes;
	rpl::lifetime _lifetime;

};

} // namespace Telegator

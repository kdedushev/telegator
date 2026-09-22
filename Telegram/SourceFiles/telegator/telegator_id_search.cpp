/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telegator/telegator_id_search.h"

#include "apiwrap.h"
#include "data/data_folder.h"
#include "data/data_session.h"
#include "dialogs/dialogs_inner_widget.h"
#include "dialogs/dialogs_main_list.h"
#include "main/main_session.h"

namespace Telegator {
namespace {

// Shorter numbers are rather a part of a name, a phone or an amount.
constexpr auto kMinIdLength = 5;

[[nodiscard]] std::optional<BareId> ParseBare(const QString &digits) {
	if (digits.size() < kMinIdLength) {
		return std::nullopt;
	}
	auto ok = false;
	const auto result = digits.toULongLong(&ok);
	return (ok && result && result <= PeerId::kChatTypeMask)
		? std::make_optional(BareId(result))
		: std::nullopt;
}

// Peers that may have the typed id, empty if the query is not an id.
[[nodiscard]] std::vector<PeerId> ParseIds(QString query) {
	query = query.trimmed().toLower();
	if (query.startsWith(u"id"_q)) {
		query = query.mid(2).trimmed();
		if (query.startsWith(QChar(u':'))) {
			query = query.mid(1).trimmed();
		}
	}
	const auto negative = query.startsWith(QChar(u'-'));
	auto digits = QString();
	for (const auto ch : QStringView(query).mid(negative ? 1 : 0)) {
		if (ch >= u'0' && ch <= u'9') {
			digits.append(ch);
		} else if (!ch.isSpace()
			&& ch != u','
			&& ch != u'.'
			&& ch != u'\'') {
			return {};
		}
	}
	auto result = std::vector<PeerId>();
	if (!negative) {
		if (const auto bare = ParseBare(digits)) {
			result.push_back(peerFromUser(UserId(*bare)));
			result.push_back(peerFromChannel(ChannelId(*bare)));
			result.push_back(peerFromChat(ChatId(*bare)));
		}
		return result;
	}
	// Bot API: "-100" and a channel id, "-" and a group id.
	if (digits.startsWith(u"100"_q)) {
		if (const auto bare = ParseBare(digits.mid(3))) {
			result.push_back(peerFromChannel(ChannelId(*bare)));
		}
	}
	if (const auto bare = ParseBare(digits)) {
		result.push_back(peerFromChat(ChatId(*bare)));
	}
	return result;
}

} // namespace

IdSearch::IdSearch(
	not_null<Dialogs::InnerWidget*> inner,
	not_null<Main::Session*> session)
: _inner(inner)
, _session(session) {
}

bool IdSearch::idsChanged(const QString &query) const {
	return (ParseIds(query) != _ids);
}

void IdSearch::search(const QString &query, bool chatsList) {
	_waiting.destroy();
	++_generation;
	_ids = ParseIds(query);
	if (!chatsList || _ids.empty() || showLoaded()) {
		return;
	}
	const auto data = &_session->data();
	rpl::merge(
		data->chatsListChanges() | rpl::to_empty,
		data->chatsListLoadedEvents() | rpl::to_empty,
		data->contactsLoaded().changes() | rpl::to_empty
	) | rpl::on_next([=, generation = _generation] {
		// "Loaded" marks are set in postponed calls, check after them.
		crl::on_main(this, [=] {
			if (generation == _generation) {
				step();
			}
		});
	}, _waiting);
	requestMore();
}

void IdSearch::step() {
	if (showLoaded() || everythingLoaded()) {
		_waiting.destroy();
	} else {
		requestMore();
	}
}

bool IdSearch::showLoaded() {
	if (_inner->state() != Dialogs::WidgetState::Filtered) {
		return false;
	}
	const auto data = &_session->data();
	auto found = false;
	for (const auto id : _ids) {
		if (const auto peer = data->peerLoaded(id)) {
			_inner->appendToFiltered(data->history(peer));
			found = true;
		}
	}
	if (found) {
		_inner->refresh();
	}
	return found;
}

void IdSearch::requestMore() {
	// The main chats list loads by itself, the archive only when shown.
	const auto data = &_session->data();
	if (const auto archive = data->folderLoaded(Data::Folder::kId)) {
		if (!archive->chatsList()->loaded()) {
			_session->api().requestDialogs(archive);
		}
	}
}

bool IdSearch::everythingLoaded() const {
	const auto data = &_session->data();
	const auto archive = data->folderLoaded(Data::Folder::kId);
	return data->chatsListLoaded()
		&& (!archive || archive->chatsList()->loaded())
		&& data->contactsLoaded().current();
}

} // namespace Telegator

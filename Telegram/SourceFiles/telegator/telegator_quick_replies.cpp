/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telegator/telegator_quick_replies.h"

#include "apiwrap.h"
#include "data/business/data_shortcut_messages.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "telegator/telegator_panel.h"
#include "window/window_session_controller.h"

namespace Telegator {
namespace {

constexpr auto kPreviewLength = 80;

[[nodiscard]] bool TextOnly(not_null<HistoryItem*> item) {
	const auto media = item->media();
	return !item->originalText().text.isEmpty()
		&& (!media || media->webpage());
}

} // namespace

QuickReplies::QuickReplies(not_null<Main::Session*> session)
: _session(session) {
	const auto messages = &session->data().shortcutMessages();
	messages->shortcutsChanged() | rpl::on_next([=] {
		loadMessages();
		_changes.fire({});
	}, _lifetime);
	messages->preloadShortcuts();
	loadMessages();
}

rpl::producer<> QuickReplies::changes() const {
	return _changes.events();
}

void QuickReplies::loadMessages() {
	// Subscribing to updates loads the messages, their texts make previews.
	const auto messages = &_session->data().shortcutMessages();
	const auto &list = messages->shortcuts().list;
	for (auto i = begin(_loading); i != end(_loading);) {
		if (list.contains(i->first)) {
			++i;
		} else {
			i = _loading.erase(i);
		}
	}
	for (const auto &[id, shortcut] : list) {
		if (_loading.contains(id)) {
			continue;
		}
		messages->updates(id) | rpl::on_next([=] {
			_changes.fire({});
		}, _loading[id]);
	}
}

std::vector<not_null<HistoryItem*>> QuickReplies::messages(
		BusinessShortcutId id) const {
	auto result = std::vector<not_null<HistoryItem*>>();
	const auto slice = _session->data().shortcutMessages().list(id);
	for (const auto &fullId : slice.ids) {
		if (const auto item = _session->data().message(fullId)) {
			result.push_back(item);
		}
	}
	return result;
}

std::vector<QuickReply> QuickReplies::list() const {
	auto result = std::vector<QuickReply>();
	const auto &shortcuts
		= _session->data().shortcutMessages().shortcuts().list;
	// The order of Settings > Telegram Business > Quick Replies.
	for (const auto &[id, shortcut] : shortcuts | ranges::views::reverse) {
		if (!shortcut.count) {
			continue;
		}
		const auto items = messages(id);
		auto preview = items.empty()
			? QString()
			: items.front()->originalText().text;
		if (!items.empty() && preview.isEmpty()) {
			preview = u"(медиа)"_q;
		}
		preview.replace(QChar('\n'), QChar(' '));
		if (preview.size() > kPreviewLength) {
			preview = preview.left(kPreviewLength - 1) + QChar(0x2026);
		}
		result.push_back({
			.id = id,
			.name = shortcut.name,
			.count = shortcut.count,
			.preview = preview,
		});
	}
	return result;
}

void QuickReplies::use(
		BusinessShortcutId id,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	const auto items = messages(id);
	const auto shortcut
		= _session->data().shortcutMessages().lookupShortcut(id);
	if (items.empty() || int(items.size()) < shortcut.count) {
		controller->showToast(
			u"Заготовка ещё загружается, нажмите через секунду."_q);
		return;
	}
	if (items.size() == 1 && TextOnly(items.front())) {
		const auto &text = items.front()->originalText();
		InsertIntoChat(controller, peer, {
			text.text,
			TextUtilities::ConvertEntitiesToTextTags(text.entities),
		});
		return;
	}
	if (!_session->premium()) {
		controller->showToast(
			u"Заготовки с медиа отправляет только Telegram Premium."_q);
		return;
	}
	_session->api().sendShortcutMessages(peer, id);
}

} // namespace Telegator

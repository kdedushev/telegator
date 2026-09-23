/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telegator/telegator_requisites.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "data/data_chat_participant_status.h"
#include "data/data_media_types.h"
#include "data/data_messages.h"
#include "data/data_peer.h"
#include "data/data_premium_limits.h"
#include "data/data_search_controller.h"
#include "data/data_session.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "storage/storage_shared_media.h"
#include "telegator/telegator_config.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"

#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Telegator {
namespace {

constexpr auto kTimeout = 15 * crl::time(1000);
constexpr auto kPinnedTimeout = 5 * crl::time(1000);
constexpr auto kContextLimit = 10;
constexpr auto kPinnedLimit = 10;
constexpr auto kScriptMaxBody = 60 * 1024;

struct PendingPins {
	base::flat_set<FullMsgId> localIds;
	rpl::lifetime lifetime;
};

// Self-destructing content never leaves the app.
[[nodiscard]] bool CanSend(not_null<HistoryItem*> item) {
	const auto media = item->media();
	return !item->isService()
		&& !item->isEphemeral()
		&& !(media && media->ttlSeconds())
		&& !item->originalText().text.isEmpty();
}

[[nodiscard]] QNetworkAccessManager *Network() {
	static const auto result = new QNetworkAccessManager(qApp);
	return result;
}

[[nodiscard]] QString SendError(not_null<History*> history) {
	if (!Data::CanSendTexts(history)) {
		return u"В этот чат нельзя отправить сообщение."_q;
	} else if (!history->resolveForwardDraft(MsgId(), PeerId()).items.empty()) {
		// ApiWrap::sendMessage() would send the pending forward along.
		return u"В этом чате ждёт пересылка: отправьте или отмените её."_q;
	}
	return QString();
}

void PinForBoth(not_null<HistoryItem*> item) {
	const auto peer = item->history()->peer;
	const auto api = &peer->session().api();
	api->request(MTPmessages_UpdatePinnedMessage(
		MTP_flags(MTPmessages_UpdatePinnedMessage::Flag::f_silent),
		peer->input(),
		MTP_int(item->id)
	)).done([=](const MTPUpdates &result) {
		api->applyUpdates(result);
	}).fail([=](const MTP::Error &error) {
		const auto window = peer->session().tryResolveWindow(peer);
		if (window && !MTP::IgnoreError(error)) {
			window->showToast(u"Реквизиты отправлены, но не закреплены."_q);
		}
	}).send();
}

void PinWhenSent(not_null<Main::Session*> session, FullMsgId localId) {
	static auto map = base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<PendingPins>>();
	auto i = map.find(session);
	if (i == end(map)) {
		i = map.emplace(session, std::make_unique<PendingPins>()).first;
		const auto pins = i->second.get();
		session->data().itemIdChanged(
		) | rpl::on_next([=](const Data::Session::IdChange &change) {
			const auto was = FullMsgId(change.newId.peer, change.oldId);
			if (pins->localIds.remove(was)) {
				if (const auto item = session->data().message(change.newId)) {
					PinForBoth(item);
				}
			}
		}, pins->lifetime);
		session->data().itemRemoved(
		) | rpl::on_next([=](not_null<const HistoryItem*> item) {
			pins->localIds.remove(item->fullId());
		}, pins->lifetime);
		session->lifetime().add([=] {
			map.remove(session);
		});
	}
	i->second->localIds.emplace(localId);
}

void SendRequisites(
		not_null<Window::SessionController*> controller,
		not_null<History*> history,
		const TextWithEntities &text,
		bool pin) {
	const auto error = SendError(history);
	if (!error.isEmpty()) {
		controller->showToast(error);
		return;
	}
	auto action = Api::SendAction(history, { .silent = true });
	action.clearDraft = false;
	auto message = Api::MessageToSend(action);
	message.textWithTags = {
		text.text,
		TextUtilities::ConvertEntitiesToTextTags(text.entities),
	};
	const auto localId = history->owner().nextLocalMessageId();
	history->session().api().sendMessage(std::move(message), localId);
	if (pin && history->owner().message(history->peer, localId)) {
		PinWhenSent(&history->session(), { history->peer->id, localId });
	}
}

void RequisitesBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<History*> history,
		TextWithEntities text,
		QString title,
		QStringList warnings,
		bool pin) {
	if (pin && !history->peer->canPinMessages()) {
		pin = false;
		warnings.push_back(u"Закрепить в этом чате нельзя."_q);
	}
	const auto weak = base::make_weak(history);
	const auto sent = box->lifetime().make_state<bool>(false);
	Ui::ConfirmBox(box, {
		.text = text,
		.confirmed = [=](Fn<void()> close) {
			if (std::exchange(*sent, true)) {
				return;
			} else if (const auto strong = weak.get()) {
				SendRequisites(controller, strong, text, pin);
			}
			close();
		},
		.confirmText = (pin
			? u"Отправить и закрепить"_q
			: u"Отправить"_q),
		.cancelText = u"Отмена"_q,
		.title = (title.isEmpty() ? u"Реквизиты"_q : title),
	});
	for (const auto &warning : warnings) {
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			u"⚠️ "_q + warning,
			st::boxLabel));
	}
}

[[nodiscard]] bool ReadCodeEntities(
		const QJsonValue &value,
		TextWithEntities &text) {
	if (!value.isUndefined() && !value.isArray()) {
		return false;
	}
	auto end = 0;
	for (const auto &entry : value.toArray()) {
		const auto entity = entry.toObject();
		const auto offset = entity.value(u"offset"_q).toInt(-1);
		const auto length = entity.value(u"length"_q).toInt();
		if (entity.value(u"type"_q).toString() != u"code"_q
			|| offset < end
			|| length <= 0
			|| length > text.text.size() - offset) {
			return false;
		}
		text.entities.push_back(
			EntityInText(EntityType::Code, offset, length));
		end = offset + length;
	}
	return true;
}

void Answered(
		not_null<Window::SessionController*> controller,
		base::weak_ptr<History> weak,
		not_null<QNetworkReply*> reply) {
	const auto object = QJsonDocument::fromJson(reply->readAll()).object();
	const auto error = object.value(u"error"_q).toString();
	if (reply->error() != QNetworkReply::NoError) {
		LOG(("Telegator: requisites script failed: %1"
			).arg(reply->errorString()));
		controller->showToast(error.isEmpty()
			? u"Скрипт «Реквизиты» не ответил."_q
			: error);
		return;
	}
	auto text = tr::marked(object.value(u"text"_q).toString());
	if (!ReadCodeEntities(object.value(u"entities"_q), text)) {
		controller->showToast(
			u"Скрипт «Реквизиты» вернул неверную разметку."_q);
		return;
	}
	TextUtilities::PrepareForSending(text, 0);
	const auto limit = Data::PremiumLimits(
		&controller->session()).messageLengthCurrent();
	if (text.text.isEmpty()) {
		controller->showToast(error.isEmpty()
			? u"Скрипт «Реквизиты» не вернул текст."_q
			: error);
		return;
	} else if (text.text.size() > limit) {
		controller->showToast(
			u"Ответ скрипта «Реквизиты» длиннее одного сообщения."_q);
		return;
	}
	const auto history = weak.get();
	if (!history) {
		return;
	}
	const auto sendError = SendError(history);
	if (!sendError.isEmpty()) {
		controller->showToast(sendError);
		return;
	}
	auto warnings = QStringList();
	for (const auto &value : object.value(u"warnings"_q).toArray()) {
		const auto warning = value.toString().trimmed();
		if (!warning.isEmpty()) {
			warnings.push_back(warning);
		}
	}
	controller->show(Box(
		RequisitesBox,
		controller,
		history,
		text,
		object.value(u"title"_q).toString().trimmed(),
		warnings,
		object.value(u"pin"_q).toBool()));
}

[[nodiscard]] QJsonObject MessageJson(not_null<HistoryItem*> item) {
	auto result = QJsonObject();
	result.insert(u"id"_q, QString::number(item->id.bare));
	result.insert(u"text"_q, item->originalText().text);
	result.insert(u"date"_q, item->date());
	result.insert(u"out"_q, item->out());
	return result;
}

[[nodiscard]] std::optional<QJsonArray> MessagesBefore(
		not_null<HistoryItem*> item) {
	const auto view = item->mainView();
	if (!view) {
		return std::nullopt;
	}
	auto result = QJsonArray();
	for (auto element = view->previousInBlocks()
		; element && result.size() < kContextLimit
		; element = element->previousInBlocks()) {
		if (CanSend(element->data())) {
			result.prepend(MessageJson(element->data()));
		}
	}
	return result;
}

[[nodiscard]] std::optional<QJsonArray> PinnedJson(
		not_null<PeerData*> peer,
		const MTPmessages_Messages &result) {
	const auto parsed = Api::ParseSearchResult(
		peer,
		Storage::SharedMediaType::kCount,
		ServerMaxMsgId - 1,
		Data::LoadDirection::Before,
		result);
	if (parsed.fullCount > kPinnedLimit
		|| parsed.fullCount != int(parsed.messageIds.size())) {
		return std::nullopt;
	}
	auto list = QJsonArray();
	for (const auto id : parsed.messageIds) {
		const auto item = peer->owner().message(peer, id);
		if (item && CanSend(item)) {
			list.push_back(MessageJson(item));
		}
	}
	return list;
}

// WHY: the script borrows from a pin only while the pins hold one set of
// requisites, so a pinned list it cannot see whole is not sent at all.
void RequestPinned(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		Fn<void(std::optional<QJsonArray>)> done) {
	auto request = Api::PrepareSearchRequest(
		peer,
		MsgId(),
		PeerId(),
		Storage::SharedMediaType::Pinned,
		QString(),
		ServerMaxMsgId - 1,
		Data::LoadDirection::Before);
	if (!request) {
		done(std::nullopt);
		return;
	}
	const auto finished = std::make_shared<bool>(false);
	const auto finish = [=](std::optional<QJsonArray> result) {
		if (!std::exchange(*finished, true)) {
			done(std::move(result));
		}
	};
	const auto api = &peer->session().api();
	const auto requestId = api->request(
		std::move(*request)
	).done([=](const MTPmessages_Messages &result) {
		finish(PinnedJson(peer, result));
	}).fail([=] {
		finish(std::nullopt);
	}).send();
	base::call_delayed(kPinnedTimeout, controller, [=] {
		api->request(requestId).cancel();
		finish(std::nullopt);
	});
}

[[nodiscard]] QJsonObject RequestBody(not_null<HistoryItem*> item) {
	const auto peer = item->history()->peer;
	auto chat = QJsonObject();
	chat.insert(u"key"_q, QString::number(peer->id.value));
	chat.insert(u"name"_q, peer->name());
	chat.insert(u"username"_q, peer->username());
	auto result = QJsonObject();
	result.insert(u"action"_q, u"requisites"_q);
	result.insert(
		u"account_id"_q,
		QString::number(peer->session().userId().bare));
	result.insert(u"chat"_q, chat);
	result.insert(u"message"_q, MessageJson(item));
	if (const auto context = MessagesBefore(item)) {
		result.insert(u"context"_q, *context);
	}
	return result;
}

[[nodiscard]] QByteArray SerializeWithinLimit(QJsonObject body) {
	auto result = QJsonDocument(body).toJson(QJsonDocument::Compact);
	for (const auto &optional : { u"pinned"_q, u"context"_q }) {
		if (result.size() <= kScriptMaxBody) {
			break;
		}
		body.remove(optional);
		result = QJsonDocument(body).toJson(QJsonDocument::Compact);
	}
	return result;
}

void Post(
		not_null<Window::SessionController*> controller,
		const QString &url,
		const QJsonObject &body,
		base::weak_ptr<History> history) {
	auto request = QNetworkRequest(QUrl(url));
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	request.setTransferTimeout(int(kTimeout));
	const auto reply = Network()->post(request, SerializeWithinLimit(body));
	const auto weak = base::make_weak(controller.get());
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (const auto strong = weak.get()) {
			Answered(strong, history, reply);
		}
	});
}

void Request(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId) {
	const auto item = controller->session().data().message(itemId);
	if (!item || !CanSend(item)) {
		return;
	}
	const auto url = Requisites().url;
	if (url.isEmpty()) {
		controller->showToast(u"«Реквизиты» не настроены: нужен адрес "
			"скрипта в telegator.json (requisites.url)."_q);
		return;
	}
	const auto body = RequestBody(item);
	const auto history = base::make_weak(item->history());
	const auto weak = base::make_weak(controller.get());
	const auto peer = item->history()->peer;
	RequestPinned(controller, peer, [=](std::optional<QJsonArray> pinned) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		auto full = body;
		if (pinned) {
			full.insert(u"pinned"_q, *pinned);
		}
		Post(strong, url, full, history);
	});
}

} // namespace

void AddMessageActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		HistoryItem *item) {
	if (!item
		|| !CanSend(item)
		|| item->history()->isForum()
		|| !PanelAllowed(&controller->session())) {
		return;
	}
	const auto itemId = item->fullId();
	menu->addAction(u"Реквизиты"_q, [=] {
		Request(controller, itemId);
	}, &st::menuIconPayment);
}

} // namespace Telegator

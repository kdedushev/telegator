/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telegator/telegator_requisites.h"

#include "apiwrap.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "telegator/telegator_config.h"
#include "telegator/telegator_panel.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Telegator {
namespace {

constexpr auto kTimeout = 15 * crl::time(1000);
constexpr auto kPinWait = 10 * 60 * crl::time(1000);

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

// The inserted text waiting to be sent, to pin the sent message.
struct PendingPin {
	PeerId peer;
	QString text;
	crl::time till = 0;
	rpl::lifetime lifetime;
};

[[nodiscard]] PendingPin &Pending(not_null<Main::Session*> session) {
	static auto map = base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<PendingPin>>();
	auto i = map.find(session);
	if (i == end(map)) {
		i = map.emplace(session, std::make_unique<PendingPin>()).first;
		session->lifetime().add([=] {
			map.remove(session);
		});
	}
	return *i->second;
}

void Pin(not_null<HistoryItem*> item) {
	const auto peer = item->history()->peer;
	using Flag = MTPmessages_UpdatePinnedMessage::Flag;
	const auto flags = Flag::f_silent
		| (peer->isUser() ? Flag::f_pm_oneside : Flag());
	const auto api = &peer->session().api();
	api->request(MTPmessages_UpdatePinnedMessage(
		MTP_flags(flags),
		peer->input(),
		MTP_int(item->id)
	)).done([=](const MTPUpdates &result) {
		api->applyUpdates(result);
	}).send();
}

void PinWhenSent(not_null<PeerData*> peer, const QString &text) {
	const auto session = &peer->session();
	auto &pending = Pending(session);
	pending.lifetime.destroy();
	pending.peer = peer->id;
	pending.text = text.trimmed();
	pending.till = crl::now() + kPinWait;
	// The server id comes when the message is sent.
	session->data().itemIdChanged(
	) | rpl::on_next([=](const Data::Session::IdChange &change) {
		auto &pending = Pending(session);
		if (pending.text.isEmpty() || crl::now() > pending.till) {
			pending.text = QString();
			return;
		}
		const auto item = session->data().message(change.newId);
		if (!item
			|| !item->out()
			|| item->history()->peer->id != pending.peer
			|| item->originalText().text.trimmed() != pending.text) {
			return;
		}
		pending.text = QString();
		Pin(item);
	}, pending.lifetime);
}

[[nodiscard]] QJsonObject RequestBody(not_null<HistoryItem*> item) {
	const auto peer = item->history()->peer;
	auto chat = QJsonObject();
	chat.insert(u"key"_q, QString::number(peer->id.value));
	chat.insert(u"name"_q, peer->name());
	chat.insert(u"username"_q, peer->username());
	auto message = QJsonObject();
	message.insert(u"id"_q, QString::number(item->id.bare));
	message.insert(u"text"_q, item->originalText().text);
	message.insert(u"date"_q, item->date());
	message.insert(u"out"_q, item->out());
	auto result = QJsonObject();
	result.insert(u"action"_q, u"requisites"_q);
	result.insert(
		u"account_id"_q,
		QString::number(peer->session().userId().bare));
	result.insert(u"chat"_q, chat);
	result.insert(u"message"_q, message);
	return result;
}

void Answered(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		not_null<QNetworkReply*> reply) {
	if (reply->error() != QNetworkReply::NoError) {
		LOG(("Telegator: requisites script failed: %1"
			).arg(reply->errorString()));
		controller->showToast(u"Скрипт «Реквизиты» не ответил."_q);
		return;
	}
	const auto object = QJsonDocument::fromJson(reply->readAll()).object();
	const auto text = object.value(u"text"_q).toString();
	if (text.trimmed().isEmpty()) {
		const auto error = object.value(u"error"_q).toString();
		controller->showToast(error.isEmpty()
			? u"Скрипт «Реквизиты» не вернул текст."_q
			: error);
		return;
	}
	if (!InsertIntoChat(controller, peer, { text, {} })) {
		controller->showToast(
			u"Реквизиты готовы, но открыт другой чат. Повторите."_q);
		return;
	}
	if (object.value(u"pin"_q).toBool()) {
		PinWhenSent(peer, text);
	}
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
	auto request = QNetworkRequest(QUrl(url));
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	request.setTransferTimeout(int(kTimeout));
	const auto reply = Network()->post(
		request,
		QJsonDocument(RequestBody(item)).toJson(QJsonDocument::Compact));
	const auto peer = item->history()->peer;
	const auto weak = base::make_weak(controller.get());
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (const auto strong = weak.get()) {
			Answered(strong, peer, reply);
		}
	});
}

} // namespace

void AddMessageActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		HistoryItem *item) {
	if (!item
		|| !CanSend(item)
		|| !PanelAllowed(&controller->session())) {
		return;
	}
	const auto itemId = item->fullId();
	menu->addAction(u"Реквизиты"_q, [=] {
		Request(controller, itemId);
	}, &st::menuIconPayment);
}

} // namespace Telegator

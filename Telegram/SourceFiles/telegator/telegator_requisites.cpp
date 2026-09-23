/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telegator/telegator_requisites.h"

#include "api/api_common.h"
#include "api/api_editing.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_chat_participant_status.h"
#include "data/data_drafts.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "telegator/telegator_config.h"
#include "telegator/telegator_requisites_format.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"

#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace Telegator {
namespace {

struct PendingPins {
	base::flat_set<FullMsgId> localIds;
	rpl::lifetime lifetime;
};

struct Prepared {
	TextWithEntities text;
	QString title;
};

// Self-destructing content never leaves the app.
[[nodiscard]] bool CanSend(not_null<HistoryItem*> item) {
	const auto media = item->media();
	return !item->isService()
		&& !item->isEphemeral()
		&& !(media && media->ttlSeconds())
		&& !item->originalText().text.isEmpty();
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

void EditRequisites(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		const TextWithEntities &text,
		bool pin) {
	const auto session = &item->history()->session();
	const auto itemId = item->fullId();
	const auto edited = [=] {
		const auto current = session->data().message(itemId);
		if (current && pin) {
			PinForBoth(current);
		}
	};
	const auto weak = base::make_weak(controller);
	const auto media = item->media();
	Api::EditTextMessage(
		item,
		text,
		Data::WebPageDraft(),
		Api::SendOptions(),
		crl::guard(session, [=](mtpRequestId) { edited(); }),
		crl::guard(session, [=](const QString &error, mtpRequestId) {
			if (error == u"MESSAGE_NOT_MODIFIED"_q) {
				edited();
			} else if (const auto strong = weak.get()) {
				strong->showToast(u"Не получилось изменить сообщение."_q);
			}
		}),
		media && media->hasSpoiler());
}

void RequisitesBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		FullMsgId itemId,
		Prepared prepared,
		QStringList warnings,
		bool edit,
		bool pin) {
	const auto item = controller->session().data().message(itemId);
	const auto history = item ? item->history().get() : nullptr;
	if (history && pin && !history->peer->canPinMessages()) {
		pin = false;
		warnings.push_back(u"Закрепить в этом чате нельзя."_q);
	}
	if (edit && !(item && item->allowsEdit(base::unixtime::now()))) {
		edit = false;
		warnings.push_back(u"Своё сообщение уже не изменить — реквизиты "
			"уйдут новым сообщением."_q);
	}
	const auto weak = base::make_weak(history);
	const auto sent = box->lifetime().make_state<bool>(false);
	Ui::ConfirmBox(box, {
		.text = prepared.text,
		.confirmed = [=](Fn<void()> close) {
			if (std::exchange(*sent, true)) {
				return;
			}
			const auto &owner = controller->session().data();
			const auto current = owner.message(itemId);
			if (current && edit) {
				EditRequisites(controller, current, prepared.text, pin);
			} else if (const auto strong = weak.get()) {
				SendRequisites(controller, strong, prepared.text, pin);
			}
			close();
		},
		.confirmText = (edit
			? (pin ? u"Изменить и закрепить"_q : u"Изменить"_q)
			: (pin ? u"Отправить и закрепить"_q : u"Отправить"_q)),
		.cancelText = u"Отмена"_q,
		.title = (prepared.title.isEmpty()
			? u"Реквизиты"_q
			: prepared.title),
	});
	for (const auto &warning : warnings) {
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			u"⚠️ "_q + warning,
			st::boxLabel));
	}
}

[[nodiscard]] std::optional<Prepared> Prepare(
		not_null<Window::SessionController*> controller,
		const Requisites::Variant &variant) {
	auto text = TextWithEntities{ variant.text };
	for (const auto &span : variant.code) {
		text.entities.push_back(
			EntityInText(EntityType::Code, span.offset, span.length));
	}
	TextUtilities::PrepareForSending(text, 0);
	const auto limit = Data::PremiumLimits(
		&controller->session()).messageLengthCurrent();
	if (text.text.isEmpty()) {
		controller->showToast(u"«Реквизиты» не нашли, что отправить."_q);
		return std::nullopt;
	} else if (text.text.size() > limit) {
		controller->showToast(
			u"Реквизиты не помещаются в одно сообщение."_q);
		return std::nullopt;
	}
	return Prepared{ std::move(text), variant.title.trimmed() };
}

void Confirm(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId,
		const Requisites::Variant &variant,
		const Requisites::Result &result) {
	if (!controller->session().data().message(itemId)) {
		return;
	}
	auto prepared = Prepare(controller, variant);
	if (!prepared) {
		return;
	}
	controller->show(Box(
		RequisitesBox,
		controller,
		itemId,
		std::move(*prepared),
		result.warnings,
		result.edit,
		result.pin));
}

void Request(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId) {
	const auto item = controller->session().data().message(itemId);
	if (!item || !CanSend(item)) {
		return;
	}
	const auto result = Requisites::Format({
		.text = item->originalText().text,
		.out = item->out(),
		.forwarded = (item->Get<HistoryMessageForwarded>() != nullptr),
	});
	if (!result.error.isEmpty()) {
		controller->showToast(result.error);
		return;
	} else if (result.variants.empty()) {
		return;
	} else if (!result.edit) {
		const auto error = SendError(item->history());
		if (!error.isEmpty()) {
			controller->showToast(error);
			return;
		}
	}
	if (result.variants.size() == 1) {
		Confirm(controller, itemId, result.variants.front(), result);
		return;
	}
	auto labels = std::vector<QString>();
	for (const auto &variant : result.variants) {
		labels.push_back(variant.label);
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		SingleChoiceBox(box, {
			.title = rpl::single(u"Какие реквизиты?"_q),
			.options = labels,
			.initialSelection = -1,
			.callback = [=](int index) {
				Confirm(controller, itemId, result.variants[index], result);
			},
		});
	}));
}

} // namespace

void AddRequisitesAction(
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

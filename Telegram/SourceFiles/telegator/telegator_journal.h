/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"

#include <QtCore/QJsonObject>

#include "telegator/telegator_journal_mtp.h"

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Telegator {

// Journal of people's actions: local JSONL, queued upload to the server.

// WHY: deleted messages leave memory before the request goes, so their chat
// and text are taken here, from the one place of deleting chosen messages.
void JournalDelete(
	not_null<Main::Session*> session,
	const MessageIdsList &ids,
	bool revoke);

// Actions inside Telegator itself: the panel page, its menu items.
void JournalPanel(
	not_null<Main::Session*> session,
	const QString &action,
	PeerData *peer,
	MsgId msgId,
	const QString &text,
	QJsonObject details = {});

// Who is signed in the panel page of this account, empty when nobody.
void JournalSetOperator(
	not_null<Main::Session*> session,
	const QString &name);

} // namespace Telegator

/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telegator/telegator_journal.h"

#include "base/timer.h"
#include "core/application.h"
#include "core/version.h"
#include "data/data_message_reaction_id.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/details/mtproto_serialized_request.h"
#include "settings.h"
#include "telegator/telegator_config.h"

#include <QtCore/QDir>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QSysInfo>
#include <QtCore/QThread>
#include <QtCore/QUuid>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Telegator {
namespace {

constexpr auto kTextLimit = 500;
constexpr auto kFileLimit = qint64(4 * 1024 * 1024);
constexpr auto kKeepFiles = 20;
constexpr auto kBatchRecords = 200;
constexpr auto kUploadDelay = crl::time(3000);
constexpr auto kRetryDelay = crl::time(60000);
constexpr auto kCurrentName = "current.jsonl";

using SerializedRequest = MTP::details::SerializedRequest;

[[nodiscard]] QString Folder() {
	return cWorkingDir() + u"telegator_journal/"_q;
}

struct Device {
	QString id;
	QString name;
	QString os;
};

// Random id kept next to telegator.json: copied data keeps it, the name tells.
[[nodiscard]] Device ReadDevice() {
	auto result = Device{
		.name = QSysInfo::machineHostName(),
		.os = QSysInfo::prettyProductName(),
	};
	auto file = QFile(cWorkingDir() + u"telegator_device.json"_q);
	if (file.open(QIODevice::ReadOnly)) {
		result.id = QJsonDocument::fromJson(file.readAll()).object().value(
			u"id"_q).toString();
		file.close();
	}
	if (result.id.isEmpty()) {
		result.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		if (file.open(QIODevice::WriteOnly)) {
			auto object = QJsonObject();
			object.insert(u"id"_q, result.id);
			file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
		}
	}
	return result;
}

[[nodiscard]] const Device &CurrentDevice() {
	static const auto result = ReadDevice();
	return result;
}

[[nodiscard]] QString Short(QString text) {
	return (text.size() > kTextLimit)
		? (text.left(kTextLimit) + QChar(0x2026))
		: text;
}

[[nodiscard]] QString ChatType(not_null<PeerData*> peer) {
	return peer->isUser()
		? u"user"_q
		: peer->isChat()
		? u"chat"_q
		: u"channel"_q;
}

[[nodiscard]] QJsonObject ChatObject(PeerData *peer) {
	auto result = QJsonObject();
	if (!peer) {
		return result;
	}
	const auto bare = peer->isUser()
		? peerToUser(peer->id).bare
		: peer->isChat()
		? peerToChat(peer->id).bare
		: peerToChannel(peer->id).bare;
	result.insert(u"id"_q, QString::number(bare));
	result.insert(u"type"_q, ChatType(peer));
	result.insert(u"name"_q, peer->name());
	result.insert(u"username"_q, peer->username());
	if (const auto user = peer->asUser()) {
		result.insert(u"bot"_q, user->isBot());
	}
	return result;
}

[[nodiscard]] QJsonArray IdsArray(const QVector<MTPint> &ids) {
	auto result = QJsonArray();
	for (const auto &id : ids) {
		result.push_back(double(id.v));
	}
	return result;
}

class Store final {
public:
	Store();

	void write(QJsonObject record);
	void setOperator(uint64 accountId, const QString &name);
	[[nodiscard]] QString op(uint64 accountId) const;

private:
	struct Cursor {
		QString file;
		qint64 offset = 0;
	};

	void rotate();
	void dropOld();
	[[nodiscard]] QStringList files() const;
	[[nodiscard]] Cursor readCursor() const;
	void saveCursor(const Cursor &cursor);
	void upload();
	void uploaded(Cursor next);

	base::Timer _timer;
	std::unique_ptr<QNetworkAccessManager> _network;
	base::flat_map<uint64, QString> _operators;
	QString _rotatedWhileSending;
	bool _sending = false;

};

Store::Store() : _timer([=] { upload(); }) {
	QDir().mkpath(Folder());
	_timer.callOnce(kUploadDelay);
}

void Store::write(QJsonObject record) {
	const auto &device = CurrentDevice();
	record.insert(u"id"_q, QUuid::createUuid().toString(QUuid::WithoutBraces));
	record.insert(
		u"at"_q,
		QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
	record.insert(u"device_id"_q, device.id);
	record.insert(u"device_name"_q, device.name);
	record.insert(u"os"_q, device.os);
	record.insert(u"app"_q, QString::fromLatin1(AppVersionStr));

	auto file = QFile(Folder() + kCurrentName);
	if (!file.open(QIODevice::Append)) {
		LOG(("Telegator: journal not writable: %1").arg(file.errorString()));
		return;
	}
	file.write(QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n');
	const auto size = file.size();
	file.close();
	if (size >= kFileLimit) {
		rotate();
	}
	if (!_sending && !_timer.isActive()) {
		_timer.callOnce(kUploadDelay);
	}
}

void Store::setOperator(uint64 accountId, const QString &name) {
	_operators[accountId] = name;
}

QString Store::op(uint64 accountId) const {
	const auto i = _operators.find(accountId);
	return (i != end(_operators)) ? i->second : QString();
}

// Closed files are named by time, so the name order is the write order.
void Store::rotate() {
	const auto name = u"%1.jsonl"_q.arg(
		QDateTime::currentDateTimeUtc().toString(u"yyyyMMdd-HHmmss-zzz"_q));
	if (!QFile::rename(Folder() + kCurrentName, Folder() + name)) {
		return;
	}
	auto cursor = readCursor();
	if (cursor.file == kCurrentName) {
		cursor.file = name;
		saveCursor(cursor);
	}
	if (_sending && _rotatedWhileSending.isEmpty()) {
		_rotatedWhileSending = name;
	}
	dropOld();
}

void Store::dropOld() {
	auto closed = files();
	closed.removeAll(kCurrentName);
	const auto cursor = readCursor();
	while (closed.size() > kKeepFiles) {
		const auto name = closed.takeFirst();
		if (!Journal().url.isEmpty()
			&& (cursor.file.isEmpty() || name >= cursor.file)) {
			LOG(("Telegator: journal file %1 dropped before upload").arg(name));
		}
		QFile::remove(Folder() + name);
	}
}

QStringList Store::files() const {
	auto result = QDir(Folder()).entryList(
		{ u"*.jsonl"_q },
		QDir::Files,
		QDir::Name);
	if (result.removeAll(kCurrentName)) {
		result.push_back(kCurrentName);
	}
	return result;
}

Store::Cursor Store::readCursor() const {
	auto file = QFile(Folder() + u"cursor.json"_q);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	const auto object = QJsonDocument::fromJson(file.readAll()).object();
	return {
		.file = object.value(u"file"_q).toString(),
		.offset = qint64(object.value(u"offset"_q).toDouble()),
	};
}

void Store::saveCursor(const Cursor &cursor) {
	auto object = QJsonObject();
	object.insert(u"file"_q, cursor.file);
	object.insert(u"offset"_q, double(cursor.offset));
	auto file = QFile(Folder() + u"cursor.json"_q);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
	}
}

void Store::upload() {
	const auto &config = Journal();
	if (config.url.isEmpty() || _sending) {
		return;
	}
	auto cursor = readCursor();
	const auto all = files();
	auto index = int(all.indexOf(cursor.file));
	if (index < 0) {
		// WHY: the cursor file was dropped: continue from the next kept one.
		index = 0;
		while (index < all.size()
			&& all[index] != kCurrentName
			&& all[index] < cursor.file) {
			++index;
		}
		cursor = { .file = all.value(index), .offset = 0 };
	}
	auto records = QJsonArray();
	auto next = Cursor();
	for (; index >= 0 && index < all.size(); ++index) {
		const auto name = all[index];
		auto file = QFile(Folder() + name);
		if (!file.open(QIODevice::ReadOnly)) {
			break;
		}
		auto offset = (name == cursor.file) ? cursor.offset : qint64(0);
		file.seek(offset);
		while (records.size() < kBatchRecords) {
			const auto line = file.readLine();
			if (line.isEmpty() || !line.endsWith('\n')) {
				break;
			}
			offset += line.size();
			const auto record = QJsonDocument::fromJson(line);
			if (record.isObject()) {
				records.push_back(record.object());
			}
		}
		next = { .file = name, .offset = offset };
		if (records.size() >= kBatchRecords || name == kCurrentName) {
			break;
		}
	}
	if (records.isEmpty()) {
		if (!next.file.isEmpty()
			&& (next.file != cursor.file || next.offset != cursor.offset)) {
			saveCursor(next);
		}
		return;
	}
	if (!_network) {
		_network = std::make_unique<QNetworkAccessManager>();
	}
	auto request = QNetworkRequest(QUrl(config.url));
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	request.setRawHeader("Authorization", "Bearer " + config.key);
	auto body = QJsonObject();
	body.insert(u"records"_q, records);
	_sending = true;
	const auto reply = _network->post(
		request,
		QJsonDocument(body).toJson(QJsonDocument::Compact));
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		_sending = false;
		const auto guard = gsl::finally([&] {
			_rotatedWhileSending = QString();
		});
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() == QNetworkReply::NoError
			&& status >= 200
			&& status < 300) {
			uploaded(next);
		} else if (status == 400 || status == 413) {
			// WHY: the server will refuse this batch every time, waiting
			// would stop the whole queue of this computer.
			LOG(("Telegator: journal batch refused, status %1, skipped"
				).arg(status));
			uploaded(next);
		} else {
			LOG(("Telegator: journal upload failed, status %1, %2"
				).arg(status
				).arg(reply->errorString()));
			_timer.callOnce(kRetryDelay);
		}
	});
}

void Store::uploaded(Cursor next) {
	if (next.file == kCurrentName && !_rotatedWhileSending.isEmpty()) {
		next.file = _rotatedWhileSending;
	}
	saveCursor(next);
	_timer.callOnce(0);
}

[[nodiscard]] Store &Log() {
	static auto result = Store();
	return result;
}

[[nodiscard]] Main::Session *SessionOf(not_null<MTP::Instance*> instance) {
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (account->sessionExists() && (&account->mtp() == instance)) {
			return &account->session();
		}
	}
	return nullptr;
}

void Write(
		not_null<Main::Session*> session,
		const QString &action,
		PeerData *peer,
		MsgId msgId,
		const QString &text,
		QJsonObject details) {
	const auto user = session->user();
	auto record = QJsonObject();
	record.insert(u"account_id"_q, QString::number(user->id.value));
	record.insert(u"account_name"_q, user->name());
	record.insert(u"account_username"_q, user->username());
	record.insert(u"operator"_q, Log().op(user->id.value));
	record.insert(u"action"_q, action);
	record.insert(u"chat"_q, ChatObject(peer));
	if (msgId) {
		record.insert(u"message_id"_q, double(msgId.bare));
	}
	if (!text.isEmpty()) {
		record.insert(u"text"_q, Short(text));
	}
	if (!details.isEmpty()) {
		record.insert(u"details"_q, details);
	}
	Log().write(std::move(record));
}

// Reads fields of a request in the order of the scheme.
class Reader final {
public:
	explicit Reader(const SerializedRequest &request)
	: _from(request->constData() + SerializedRequest::kMessageBodyPosition)
	, _end(request->constData() + request->size()) {
	}

	[[nodiscard]] bool ok() const {
		return _ok;
	}
	[[nodiscard]] mtpTypeId type() {
		return prime();
	}
	[[nodiscard]] uint32 flags() {
		return prime();
	}
	[[nodiscard]] int32 integer() {
		return int32(prime());
	}

	template <typename Type>
	[[nodiscard]] Type read() {
		auto result = Type();
		if (_ok && !result.read(_from, _end)) {
			_ok = false;
		}
		return result;
	}

private:
	[[nodiscard]] uint32 prime() {
		if (!_ok || _from >= _end) {
			_ok = false;
			return 0;
		}
		return uint32(*_from++);
	}

	const mtpPrime *_from = nullptr;
	const mtpPrime *_end = nullptr;
	bool _ok = true;

};

[[nodiscard]] PeerData *Peer(
		not_null<Main::Session*> session,
		const MTPInputPeer &input) {
	return Data::PeerFromInputMTP(&session->data(), input);
}

[[nodiscard]] QString BytesText(const QByteArray &data) {
	const auto text = QString::fromUtf8(data);
	return (text.toUtf8() == data)
		? text
		: (u"base64:"_q + QString::fromLatin1(data.toBase64()));
}

[[nodiscard]] QString ReactionsText(const QVector<MTPReaction> &list) {
	auto result = QStringList();
	for (const auto &reaction : list) {
		const auto id = Data::ReactionFromMTP(reaction);
		if (const auto emoji = std::get_if<QString>(&id.data)) {
			result.push_back(id.paid() ? u"paid"_q : *emoji);
		} else if (const auto custom = std::get_if<DocumentId>(&id.data)) {
			result.push_back(u"custom:%1"_q.arg(*custom));
		}
	}
	return result.join(' ');
}

void BotButton(
		not_null<Main::Session*> session,
		PeerData *peer,
		MsgId msgId,
		const std::optional<QByteArray> &data) {
	using Type = HistoryMessageMarkupButton::Type;
	auto details = QJsonObject();
	auto button = QString();
	const auto item = peer
		? session->data().message(peer->id, msgId)
		: nullptr;
	if (item) {
		if (const auto bot = item->getMessageBot()) {
			details.insert(u"bot_id"_q, QString::number(bot->id.value));
			details.insert(u"bot_username"_q, bot->username());
		}
		if (const auto markup = item->Get<HistoryMessageReplyMarkup>()) {
			for (const auto &row : markup->data.rows) {
				for (const auto &entry : row) {
					const auto match = data
						? ((entry.type == Type::Callback
							|| entry.type == Type::CallbackWithPassword)
							&& entry.data == *data)
						: (entry.type == Type::Game);
					if (match && button.isEmpty()) {
						button = entry.text;
					}
				}
			}
		}
		details.insert(u"message_text"_q, Short(item->originalText().text));
	}
	if (data) {
		details.insert(u"data"_q, BytesText(*data));
	}
	Write(session, u"bot_button"_q, peer, msgId, button, details);
}

// Bot's own start and mini app openings: the bot and where it was opened.
void WebApp(
		not_null<Main::Session*> session,
		const QString &action,
		PeerData *peer,
		const MTPInputUser &input,
		const QString &param) {
	auto details = QJsonObject();
	if (const auto bot = Data::UserFromInputMTP(&session->data(), input)) {
		details.insert(u"bot_id"_q, QString::number(bot->id.value));
		details.insert(u"bot_username"_q, bot->username());
	}
	if (!param.isEmpty()) {
		details.insert(u"param"_q, Short(param));
	}
	Write(session, action, peer, 0, {}, details);
}

void Parse(
		not_null<Main::Session*> session,
		const SerializedRequest &request) {
	auto reader = Reader(request);
	const auto type = reader.type();
	const auto replyTo = [&](uint32 flags, uint32 flag) {
		if (flags & flag) {
			(void)reader.read<MTPInputReplyTo>();
		}
	};
	switch (type) {
	case mtpc_messages_sendMessage: {
		const auto flags = reader.flags();
		const auto peer = reader.read<MTPInputPeer>();
		replyTo(flags, 1U << 0);
		const auto text = reader.read<MTPstring>();
		if (reader.ok()) {
			Write(session, u"send_message"_q, Peer(session, peer), 0, qs(text), {});
		}
	} break;
	case mtpc_messages_sendMedia: {
		const auto flags = reader.flags();
		const auto peer = reader.read<MTPInputPeer>();
		replyTo(flags, 1U << 0);
		(void)reader.read<MTPInputMedia>();
		const auto text = reader.read<MTPstring>();
		if (reader.ok()) {
			Write(session, u"send_media"_q, Peer(session, peer), 0, qs(text), {});
		}
	} break;
	case mtpc_messages_sendMultiMedia: {
		const auto flags = reader.flags();
		const auto peer = reader.read<MTPInputPeer>();
		replyTo(flags, 1U << 0);
		const auto list = reader.read<MTPVector<MTPInputSingleMedia>>();
		if (reader.ok()) {
			auto text = QString();
			for (const auto &media : list.v) {
				const auto caption = qs(media.data().vmessage());
				if (!caption.isEmpty()) {
					text = caption;
				}
			}
			auto details = QJsonObject();
			details.insert(u"count"_q, int(list.v.size()));
			Write(session, u"send_album"_q, Peer(session, peer), 0, text, details);
		}
	} break;
	case mtpc_messages_sendInlineBotResult: {
		const auto flags = reader.flags();
		const auto peer = reader.read<MTPInputPeer>();
		if (reader.ok()) {
			(void)flags;
			Write(session, u"send_inline"_q, Peer(session, peer), 0, {}, {});
		}
	} break;
	case mtpc_messages_forwardMessages: {
		(void)reader.flags();
		const auto from = reader.read<MTPInputPeer>();
		const auto ids = reader.read<MTPVector<MTPint>>();
		(void)reader.read<MTPVector<MTPlong>>();
		const auto to = reader.read<MTPInputPeer>();
		if (reader.ok()) {
			const auto source = Peer(session, from);
			auto details = QJsonObject();
			details.insert(u"from"_q, ChatObject(source));
			details.insert(u"ids"_q, IdsArray(ids.v));
			const auto first = ids.v.isEmpty() ? MsgId() : MsgId(ids.v[0].v);
			const auto item = (source && first)
				? session->data().message(source->id, first)
				: nullptr;
			Write(
				session,
				u"forward"_q,
				Peer(session, to),
				0,
				item ? item->originalText().text : QString(),
				details);
		}
	} break;
	case mtpc_messages_editMessage: {
		const auto flags = reader.flags();
		const auto peer = reader.read<MTPInputPeer>();
		const auto id = reader.integer();
		const auto text = (flags & (1U << 11))
			? qs(reader.read<MTPstring>())
			: QString();
		if (reader.ok()) {
			Write(session, u"edit"_q, Peer(session, peer), id, text, {});
		}
	} break;
	case mtpc_messages_deleteHistory: {
		const auto flags = reader.flags();
		const auto peer = reader.read<MTPInputPeer>();
		if (reader.ok()) {
			auto details = QJsonObject();
			details.insert(u"revoke"_q, bool(flags & (1U << 1)));
			details.insert(u"just_clear"_q, bool(flags & (1U << 0)));
			Write(session, u"delete_history"_q, Peer(session, peer), 0, {}, details);
		}
	} break;
	case mtpc_messages_sendReaction: {
		const auto flags = reader.flags();
		const auto peer = reader.read<MTPInputPeer>();
		const auto id = reader.integer();
		const auto list = (flags & (1U << 0))
			? reader.read<MTPVector<MTPReaction>>()
			: MTPVector<MTPReaction>();
		if (reader.ok()) {
			const auto text = ReactionsText(list.v);
			Write(
				session,
				text.isEmpty() ? u"reaction_removed"_q : u"reaction"_q,
				Peer(session, peer),
				id,
				text,
				{});
		}
	} break;
	case mtpc_messages_updatePinnedMessage: {
		const auto flags = reader.flags();
		const auto peer = reader.read<MTPInputPeer>();
		const auto id = reader.integer();
		if (reader.ok()) {
			Write(
				session,
				(flags & (1U << 1)) ? u"unpin"_q : u"pin"_q,
				Peer(session, peer),
				id,
				{},
				{});
		}
	} break;
	case mtpc_messages_unpinAllMessages: {
		(void)reader.flags();
		const auto peer = reader.read<MTPInputPeer>();
		if (reader.ok()) {
			Write(session, u"unpin_all"_q, Peer(session, peer), 0, {}, {});
		}
	} break;
	case mtpc_messages_startBot: {
		const auto bot = reader.read<MTPInputUser>();
		const auto peer = reader.read<MTPInputPeer>();
		(void)reader.read<MTPlong>();
		const auto param = reader.read<MTPstring>();
		if (reader.ok()) {
			WebApp(session, u"bot_start"_q, Peer(session, peer), bot, qs(param));
		}
	} break;
	case mtpc_messages_requestWebView: {
		const auto flags = reader.flags();
		const auto peer = reader.read<MTPInputPeer>();
		const auto bot = reader.read<MTPInputUser>();
		const auto url = (flags & (1U << 1))
			? qs(reader.read<MTPstring>())
			: QString();
		if (reader.ok()) {
			WebApp(session, u"bot_webapp"_q, Peer(session, peer), bot, url);
		}
	} break;
	case mtpc_messages_requestSimpleWebView: {
		const auto flags = reader.flags();
		const auto bot = reader.read<MTPInputUser>();
		const auto url = (flags & (1U << 3))
			? qs(reader.read<MTPstring>())
			: QString();
		if (reader.ok()) {
			WebApp(session, u"bot_webapp"_q, nullptr, bot, url);
		}
	} break;
	case mtpc_messages_requestMainWebView: {
		(void)reader.flags();
		const auto peer = reader.read<MTPInputPeer>();
		const auto bot = reader.read<MTPInputUser>();
		if (reader.ok()) {
			WebApp(session, u"bot_webapp"_q, Peer(session, peer), bot, {});
		}
	} break;
	case mtpc_messages_sendVote: {
		const auto peer = reader.read<MTPInputPeer>();
		const auto id = reader.integer();
		const auto options = reader.read<MTPVector<MTPbytes>>();
		if (reader.ok()) {
			auto list = QJsonArray();
			for (const auto &option : options.v) {
				list.push_back(BytesText(option.v));
			}
			auto details = QJsonObject();
			details.insert(u"options"_q, list);
			Write(session, u"vote"_q, Peer(session, peer), id, {}, details);
		}
	} break;
	case mtpc_messages_getBotCallbackAnswer: {
		const auto flags = reader.flags();
		const auto peer = reader.read<MTPInputPeer>();
		const auto id = reader.integer();
		auto data = std::optional<QByteArray>();
		if (flags & (1U << 0)) {
			data = reader.read<MTPbytes>().v;
		}
		if (reader.ok()) {
			BotButton(session, Peer(session, peer), id, data);
		}
	} break;
	}
}

[[nodiscard]] bool Interesting(mtpTypeId type) {
	switch (type) {
	case mtpc_messages_sendMessage:
	case mtpc_messages_sendMedia:
	case mtpc_messages_sendMultiMedia:
	case mtpc_messages_sendInlineBotResult:
	case mtpc_messages_forwardMessages:
	case mtpc_messages_editMessage:
	case mtpc_messages_deleteHistory:
	case mtpc_messages_sendReaction:
	case mtpc_messages_updatePinnedMessage:
	case mtpc_messages_unpinAllMessages:
	case mtpc_messages_getBotCallbackAnswer:
	case mtpc_messages_startBot:
	case mtpc_messages_requestWebView:
	case mtpc_messages_requestSimpleWebView:
	case mtpc_messages_requestMainWebView:
	case mtpc_messages_sendVote:
		return true;
	}
	return false;
}

} // namespace

void JournalRequest(
		not_null<MTP::Instance*> instance,
		const SerializedRequest &request) {
	const auto main = (QThread::currentThread() == qApp->thread());
	if (main) {
		// WHY: the first request starts the store, it uploads what the
		// previous run left queued.
		(void)Log();
	}
	const auto position = SerializedRequest::kMessageBodyPosition;
	if (!request || request->size() <= position) {
		return;
	} else if (!Interesting(mtpTypeId((*request)[position]))) {
		return;
	}
	const auto parse = [=, copy = request] {
		if (const auto session = SessionOf(instance)) {
			Parse(session, copy);
		}
	};
	if (main) {
		parse();
	} else {
		crl::on_main(parse);
	}
}

void JournalDelete(
		not_null<Main::Session*> session,
		const MessageIdsList &ids,
		bool revoke) {
	auto byChat = base::flat_map<not_null<PeerData*>, QJsonArray>();
	auto texts = base::flat_map<not_null<PeerData*>, QString>();
	auto first = base::flat_map<not_null<PeerData*>, MsgId>();
	for (const auto &id : ids) {
		const auto item = session->data().message(id);
		if (!item) {
			continue;
		}
		const auto peer = item->history()->peer;
		byChat[peer].push_back(double(item->id.bare));
		if (!first.contains(peer)) {
			first.emplace(peer, item->id);
			texts.emplace(peer, item->originalText().text);
		}
	}
	for (const auto &[peer, list] : byChat) {
		auto details = QJsonObject();
		details.insert(u"ids"_q, list);
		details.insert(u"revoke"_q, revoke);
		Write(session, u"delete"_q, peer, first[peer], texts[peer], details);
	}
}

void JournalPanel(
		not_null<Main::Session*> session,
		const QString &action,
		PeerData *peer,
		MsgId msgId,
		const QString &text,
		QJsonObject details) {
	Write(session, action, peer, msgId, text, std::move(details));
}

void JournalSetOperator(
		not_null<Main::Session*> session,
		const QString &name) {
	Log().setOperator(session->user()->id.value, name.trimmed());
}

} // namespace Telegator

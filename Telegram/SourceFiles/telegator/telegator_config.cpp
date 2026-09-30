/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telegator/telegator_config.h"

#include "main/main_session.h"
#include "settings.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Telegator {
namespace {

struct Config {
	PanelConfig panel;
	JournalConfig journal;
};

[[nodiscard]] QJsonObject ReadObject(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError) {
		LOG(("Telegator: bad %1: %2").arg(path, error.errorString()));
		return {};
	}
	return document.object();
}

// Defaults put into the package by the installer build, next to the
// program: the panel settings shared by everyone who gets this package.
[[nodiscard]] QString PackagedPath() {
	const auto dir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MAC
	return dir + u"/../Resources/telegator.json"_q;
#else // Q_OS_MAC
	return dir + u"/telegator.json"_q;
#endif // Q_OS_MAC
}

[[nodiscard]] Config ReadConfig() {
	const auto local = ReadObject(cWorkingDir() + u"telegator.json"_q);
	auto result = Config();
	const auto panel = local.contains(u"panel"_q)
		? local.value(u"panel"_q).toObject()
		: ReadObject(PackagedPath()).value(u"panel"_q).toObject();
	for (const auto &value : panel.value(u"accounts"_q).toArray()) {
		const auto id = value.toVariant().toULongLong();
		if (id) {
			result.panel.accounts.push_back(id);
		}
	}
	result.panel.url = panel.value(u"url"_q).toString().trimmed();
	const auto journal = local.value(u"journal"_q).toObject();
	result.journal.url = journal.value(u"url"_q).toString().trimmed();
	result.journal.key = journal.value(u"key"_q).toString().trimmed().toUtf8();
	return result;
}

[[nodiscard]] const Config &Read() {
	static const auto result = ReadConfig();
	return result;
}

} // namespace

const PanelConfig &Panel() {
	return Read().panel;
}

const JournalConfig &Journal() {
	return Read().journal;
}

bool PanelAllowed(not_null<Main::Session*> session) {
	const auto &accounts = Panel().accounts;
	return ranges::contains(accounts, session->userId().bare);
}

} // namespace Telegator

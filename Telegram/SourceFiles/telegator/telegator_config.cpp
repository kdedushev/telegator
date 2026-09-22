/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telegator/telegator_config.h"

#include "main/main_session.h"
#include "settings.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Telegator {
namespace {

struct Config {
	PanelConfig panel;
	RequisitesConfig requisites;
};

[[nodiscard]] Config ReadConfig() {
	auto file = QFile(cWorkingDir() + u"telegator.json"_q);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError) {
		LOG(("Telegator: bad telegator.json: %1").arg(error.errorString()));
		return {};
	}
	auto result = Config();
	const auto panel = document.object().value(u"panel"_q).toObject();
	for (const auto &value : panel.value(u"accounts"_q).toArray()) {
		const auto id = value.toVariant().toULongLong();
		if (id) {
			result.panel.accounts.push_back(id);
		}
	}
	result.panel.url = panel.value(u"url"_q).toString().trimmed();
	const auto requisites = document.object().value(
		u"requisites"_q).toObject();
	result.requisites.url = requisites.value(
		u"url"_q).toString().trimmed();
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

const RequisitesConfig &Requisites() {
	return Read().requisites;
}

bool PanelAllowed(not_null<Main::Session*> session) {
	const auto &accounts = Panel().accounts;
	return ranges::contains(accounts, session->userId().bare);
}

} // namespace Telegator

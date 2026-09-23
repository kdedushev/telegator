/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telegator/telegator_requisites_format.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <iostream>

namespace {

using namespace Telegator::Requisites;

int Failed = 0;
int Total = 0;

void Check(bool condition, const QString &name, const QString &details) {
	++Total;
	if (!condition) {
		++Failed;
		std::cout << "FAILED: " << name.toStdString() << std::endl;
		if (!details.isEmpty()) {
			std::cout << details.toStdString() << std::endl;
		}
	}
}

[[nodiscard]] Request ReadRequest(const QJsonObject &object) {
	return {
		.text = object.value(u"text"_q).toString(),
		.out = object.value(u"out"_q).toBool(),
		.forwarded = object.value(u"forwarded"_q).toBool(),
	};
}

[[nodiscard]] QJsonArray Entities(const std::vector<CodeSpan> &code) {
	auto result = QJsonArray();
	for (const auto &span : code) {
		result.push_back(QJsonObject{
			{ u"type"_q, u"code"_q },
			{ u"offset"_q, span.offset },
			{ u"length"_q, span.length },
		});
	}
	return result;
}

[[nodiscard]] QJsonObject Serialize(const Result &result) {
	if (!result.error.isEmpty()) {
		return QJsonObject{ { u"error"_q, result.error } };
	}
	auto object = QJsonObject{
		{ u"mode"_q, result.edit ? u"edit"_q : u"send"_q },
		{ u"pin"_q, result.pin },
		{ u"warnings"_q, QJsonArray::fromStringList(result.warnings) },
	};
	if (result.variants.size() == 1) {
		const auto &variant = result.variants.front();
		object.insert(u"text"_q, variant.text);
		object.insert(u"entities"_q, Entities(variant.code));
		object.insert(u"title"_q, variant.title);
		return object;
	}
	auto choices = QJsonArray();
	for (const auto &variant : result.variants) {
		choices.push_back(QJsonObject{
			{ u"label"_q, variant.label },
			{ u"text"_q, variant.text },
			{ u"entities"_q, Entities(variant.code) },
			{ u"title"_q, variant.title },
		});
	}
	object.insert(u"choices"_q, choices);
	return object;
}

[[nodiscard]] QByteArray Canonical(const QJsonObject &object) {
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

[[nodiscard]] QString Describe(const Result &result) {
	return u"  actual:   "_q + QString::fromUtf8(Canonical(Serialize(result)));
}

[[nodiscard]] QString Spans(const Variant &variant) {
	auto result = QStringList();
	for (const auto &span : variant.code) {
		result.push_back(variant.text.mid(span.offset, span.length));
	}
	return result.join(u'\n');
}

void CheckDigitsKeptAsWritten() {
	const auto phoneId = u"+79000000000"_q;
	const auto phone = phoneId + u"\n Мария Иванова\n Сбербанк"_q;
	const auto cardId = u"2200700123456781"_q;
	const auto card = cardId + u"\n Мария Иванова\n Т-Банк"_q;
	const auto foreign = u"TBC\n+995 555 00 00 00\n"
		"GE29 NB00 0000 0101 9049 17\nMaria Ivanova"_q;
	const auto tron = u"TAb1Ab1Ab1Ab1Ab1Ab1Ab1Ab1Ab1Ab1Ab1"_q;
	const auto evm = u"0x1234567890abcdef1234567890abcdef12345678"_q;
	const auto ton = u"UQAb_1-Ab_1-Ab_1-Ab_1-Ab_1-Ab_1-Ab_1-Ab_1-Ab_1-A"_q;
	struct Case {
		QString text;
		QString expected;
		QString value;
	};
	const auto cases = std::vector<Case>{
		{ u"8 (900) 000-00-00\nСбер\nМария Иванова"_q, phone, phoneId },
		{ u"+7 900 000 00 00 Сбер Мария Иванова"_q, phone, phoneId },
		{ u"=79000000000 Сбер Мария Иванова"_q, phone, phoneId },
		{ u"2200 7001 2345 6781\nТинькофф\nМария Иванова"_q, card, cardId },
		{ u"2200-7001-2345-6781 Тинькофф Мария Иванова"_q, card, cardId },
		{ foreign, foreign, u"GE29 NB00 0000 0101 9049 17"_q },
		{ u"usdt trc20 "_q + tron, u"USDT TRC20: "_q + tron, tron },
		{ evm + u" usdt bep20"_q, u"USDT BEP20: "_q + evm, evm },
		{ u"usdt ton "_q + ton, u"USDT TON: "_q + ton, ton },
	};
	for (const auto &entry : cases) {
		const auto result = Format({ .text = entry.text });
		const auto ok = result.error.isEmpty()
			&& (result.variants.size() == 1)
			&& (result.variants.front().text == entry.expected)
			&& Spans(result.variants.front()).contains(entry.value);
		Check(
			ok,
			u"digits kept as written: "_q + entry.value,
			Describe(result));
	}
}

void CheckTwoPhonesGiveChoice() {
	const auto result = Format({
		.text = u"+79000000001 или +79111111111\nСбер\nМария Иванова"_q,
	});
	const auto texts = [&] {
		auto list = QStringList();
		for (const auto &variant : result.variants) {
			list.push_back(variant.text);
		}
		return list;
	}();
	Check(
		result.error.isEmpty() && (texts == QStringList{
			u"+79000000001\n Мария Иванова\n Сбербанк"_q,
			u"+79111111111\n Мария Иванова\n Сбербанк"_q,
		}),
		u"two phones give a choice"_q,
		Describe(result));
}

[[nodiscard]] QStringList Texts(const Result &result) {
	auto list = QStringList();
	for (const auto &variant : result.variants) {
		list.push_back(variant.text);
	}
	return list;
}

void CheckExpected(
		const QString &name,
		const QString &text,
		const QStringList &expected) {
	const auto result = Format({ .text = text });
	Check(
		result.error.isEmpty() && (Texts(result) == expected),
		name,
		Describe(result));
}

void CheckForeignLabelsDropped() {
	const auto iban = u"GE29NB0000000101904917"_q;
	const auto expected = u"TBC\n"_q + iban + u"\nMaria Ivanova"_q;
	CheckExpected(
		u"foreign labels dropped: Банк, IBAN"_q,
		u"Банк: TBC\nIBAN: "_q + iban + u"\nMaria Ivanova"_q,
		{ expected });
	CheckExpected(
		u"foreign labels dropped: Account, Name"_q,
		u"TBC\nAccount: "_q + iban + u"\nName: Maria Ivanova"_q,
		{ expected });
}

void CheckNamesAsWritten() {
	CheckExpected(
		u"two names with or give a choice"_q,
		u"+79000000000 Сбер Мария Иванова или Анна Петрова"_q,
		{
			u"+79000000000\n Мария Иванова\n Сбербанк"_q,
			u"+79000000000\n Анна Петрова\n Сбербанк"_q,
		});
	CheckExpected(
		u"initial is part of the name"_q,
		u"+79000000000 Сбер Мария И"_q,
		{ u"+79000000000\n Мария И\n Сбербанк"_q });
	CheckExpected(
		u"number does not spoil the name"_q,
		u"+79000000000 Сбер Иван Иванов 666"_q,
		{ u"+79000000000\n Иван Иванов\n Сбербанк"_q });
	CheckExpected(
		u"initials kept as written"_q,
		u"+79000000000 Сбер Иванов Д.С."_q,
		{ u"+79000000000\n Иванов Д.С.\n Сбербанк"_q });
}

void CheckForwardedRefused() {
	for (const auto out : { false, true }) {
		const auto result = Format({
			.text = u"+79000000000 Мария Иванова Сбер"_q,
			.out = out,
			.forwarded = true,
		});
		Check(
			!result.error.isEmpty() && result.variants.empty(),
			u"forwarded message refused"_q,
			Describe(result));
	}
}

void CheckOwnMessageEdited() {
	for (const auto out : { false, true }) {
		const auto result = Format({
			.text = u"+79000000000 Мария Иванова Сбер"_q,
			.out = out,
		});
		Check(
			result.error.isEmpty()
				&& (result.variants.size() == 1)
				&& (result.edit == out),
			out ? u"own message: edit"_q : u"client message: send"_q,
			Describe(result));
	}
}

void CheckVectors(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		Check(false, u"vectors: "_q + path, u"cannot open"_q);
		return;
	}
	const auto vectors = QJsonDocument::fromJson(
		file.readAll()
	).object().value(u"vectors"_q).toArray();
	Check(!vectors.isEmpty(), u"vectors: "_q + path, u"no vectors"_q);
	for (const auto &entry : vectors) {
		const auto vector = entry.toObject();
		const auto expected = Canonical(
			vector.value(u"expected"_q).toObject());
		const auto actual = Canonical(Serialize(Format(ReadRequest(
			vector.value(u"input"_q).toObject()))));
		Check(
			actual == expected,
			vector.value(u"name"_q).toString(),
			u"  expected: "_q + QString::fromUtf8(expected)
				+ u"\n  actual:   "_q + QString::fromUtf8(actual));
	}
}

} // namespace

int main(int argc, char *argv[]) {
	if (argc < 2) {
		std::cout << "Usage: test_telegator_requisites <vectors.json>"
			<< std::endl;
		return 2;
	}
	CheckDigitsKeptAsWritten();
	CheckTwoPhonesGiveChoice();
	CheckForeignLabelsDropped();
	CheckNamesAsWritten();
	CheckForwardedRefused();
	CheckOwnMessageEdited();
	CheckVectors(QString::fromUtf8(argv[1]));
	std::cout << (Total - Failed) << "/" << Total << " passed" << std::endl;
	return Failed ? 1 : 0;
}

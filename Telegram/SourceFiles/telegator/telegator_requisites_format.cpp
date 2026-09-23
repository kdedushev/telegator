/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telegator/telegator_requisites_format.h"

#include "base/flat_set.h"

#include <QtCore/QRegularExpression>

#include <algorithm>
#include <array>
#include <map>
#include <optional>

namespace Telegator::Requisites {
namespace {

constexpr auto kMaxChoices = 6;
constexpr auto kAmountWindow = 25;

enum class Kind {
	Phone,
	Card,
	Account,
	Iban,
	Uid,
	Wallet,
	Bank,
	Exchange,
	Coin,
	Network,
	Memo,
};

struct Found {
	Kind kind = Kind::Phone;
	QString value;
	int start = 0;
	int end = 0;
	QString extra;
};

struct Row {
	QString alias;
	QString name;
	QString extra;
};

struct PhoneCode {
	QString code;
	QString country;
	int national = 0;
};

enum class SetKind {
	Fiat,
	Uid,
	Wallet,
};

struct Set {
	SetKind kind = SetKind::Fiat;
	QString country;
	std::optional<Found> ident;
	std::optional<Found> card;
	std::optional<QStringList> rawLines;
	QString bank;
	QString name;
	std::vector<Found> accounts;
	QString swift;
	QString exchange;
	QString uid;
	QString coin;
	QString network;
	QString address;
	QString memo;
};

struct Parsed {
	std::vector<std::vector<Set>> variants;
	QStringList warnings;
};

struct Rendered {
	QString text;
	std::vector<CodeSpan> code;
	QString title;
};

const auto kWordClass = u"\\p{L}\\p{N}_"_q;
const auto kSpaceClass = u"\\t\\n\\x{0B}\\f\\r\\x{1C}-\\x{1F} \\x{85}\\x{A0}"
	"\\x{1680}\\x{2000}-\\x{200A}\\x{2028}\\x{2029}\\x{202F}\\x{205F}"
	"\\x{3000}"_q;

// Python re semantics of \w \d \s \b, pinned apart from PCRE2 options.
[[nodiscard]] QString PyPattern(const QString &pattern) {
	auto result = QString();
	auto inClass = false;
	for (auto i = 0; i < pattern.size(); ++i) {
		const auto ch = pattern[i];
		if (ch != u'\\' || i + 1 == pattern.size()) {
			if (ch == u'[' && !inClass) {
				inClass = true;
			} else if (ch == u']' && inClass) {
				inClass = false;
			}
			result += ch;
			continue;
		}
		const auto next = pattern[++i];
		if (next == u'w') {
			result += inClass ? kWordClass : (u'[' + kWordClass + u']');
		} else if (next == u'd') {
			result += u"\\p{Nd}"_q;
		} else if (next == u's') {
			result += inClass ? kSpaceClass : (u'[' + kSpaceClass + u']');
		} else if (next == u'b' && !inClass) {
			const auto word = u'[' + kWordClass + u']';
			result += u"(?:(?<="_q + word + u")(?!"_q + word + u")|(?<!"_q
				+ word + u")(?="_q + word + u"))"_q;
		} else {
			result += ch;
			result += next;
		}
	}
	return result;
}

[[nodiscard]] const QRegularExpression &Re(
		const QString &pattern,
		bool caseless = false) {
	static auto cache = std::map<
		std::pair<QString, bool>,
		QRegularExpression>();
	const auto key = std::make_pair(pattern, caseless);
	auto i = cache.find(key);
	if (i == end(cache)) {
		i = cache.emplace(key, QRegularExpression(
			PyPattern(pattern),
			(caseless
				? QRegularExpression::CaseInsensitiveOption
				: QRegularExpression::NoPatternOption))).first;
	}
	return i->second;
}

[[nodiscard]] QRegularExpressionMatch Search(
		const QString &pattern,
		const QString &subject,
		int from = 0,
		bool caseless = false) {
	return Re(pattern, caseless).match(subject, from);
}

[[nodiscard]] QRegularExpressionMatch MatchAt(
		const QString &pattern,
		const QString &subject,
		int at = 0) {
	return Re(pattern).match(
		subject,
		at,
		QRegularExpression::NormalMatch,
		QRegularExpression::AnchorAtOffsetMatchOption);
}

[[nodiscard]] bool FullMatch(const QString &pattern, const QString &subject) {
	return Re(u"\\A(?:"_q + pattern + u")\\z"_q).match(subject).hasMatch();
}

[[nodiscard]] std::vector<QRegularExpressionMatch> FindAll(
		const QString &pattern,
		const QString &subject,
		bool caseless = false) {
	auto result = std::vector<QRegularExpressionMatch>();
	auto i = Re(pattern, caseless).globalMatch(subject);
	while (i.hasNext()) {
		result.push_back(i.next());
	}
	return result;
}

[[nodiscard]] QStringList Split(const QString &pattern, const QString &text) {
	auto result = QStringList();
	auto from = 0;
	for (const auto &match : FindAll(pattern, text)) {
		result.push_back(text.mid(from, match.capturedStart() - from));
		from = match.capturedEnd();
	}
	result.push_back(text.mid(from));
	return result;
}

[[nodiscard]] char32_t CodeAt(const QString &text, int index) {
	const auto ch = text[index];
	if (ch.isHighSurrogate()
		&& index + 1 < text.size()
		&& text[index + 1].isLowSurrogate()) {
		return QChar::surrogateToUcs4(ch, text[index + 1]);
	}
	return ch.unicode();
}

[[nodiscard]] int CodeSize(const QString &text, int index) {
	return (CodeAt(text, index) > 0xFFFF) ? 2 : 1;
}

[[nodiscard]] int CodeCount(const QString &text) {
	auto result = 0;
	for (auto i = 0; i < text.size(); i += CodeSize(text, i)) {
		++result;
	}
	return result;
}

[[nodiscard]] QString LeftCodes(const QString &text, int count) {
	auto till = 0;
	while (count-- > 0 && till < text.size()) {
		till += CodeSize(text, till);
	}
	return text.left(till);
}

[[nodiscard]] int BackCodes(const QString &text, int index, int count) {
	while (count-- > 0 && index > 0) {
		--index;
		if (index > 0
			&& text[index].isLowSurrogate()
			&& text[index - 1].isHighSurrogate()) {
			--index;
		}
	}
	return index;
}

[[nodiscard]] bool IsSpace(char32_t code) {
	static const auto list = std::array<char32_t, 29>{
		0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x85,
		0xA0, 0x1680, 0x2000, 0x2001, 0x2002, 0x2003, 0x2004, 0x2005,
		0x2006, 0x2007, 0x2008, 0x2009, 0x200A, 0x2028, 0x2029, 0x202F,
		0x205F, 0x3000,
	};
	return std::find(begin(list), end(list), code) != end(list);
}

[[nodiscard]] bool IsDigit(char32_t code) {
	return QChar::digitValue(code) >= 0;
}

[[nodiscard]] bool IsDecimal(char32_t code) {
	return QChar::category(code) == QChar::Number_DecimalDigit;
}

[[nodiscard]] bool IsAlnum(char32_t code) {
	return QChar::isLetterOrNumber(code);
}

[[nodiscard]] QString Strip(const QString &text) {
	auto from = 0;
	auto till = int(text.size());
	while (from < till && IsSpace(text[from].unicode())) {
		++from;
	}
	while (till > from && IsSpace(text[till - 1].unicode())) {
		--till;
	}
	return text.mid(from, till - from);
}

[[nodiscard]] QString StripDots(QString text) {
	while (text.endsWith(u'.')) {
		text.chop(1);
	}
	return text;
}

[[nodiscard]] QString Lower(const QString &text) {
	static auto last = std::pair<QString, QString>();
	if (text.isSharedWith(last.first) || text == last.first) {
		return last.second;
	}
	auto result = QString();
	result.reserve(text.size());
	for (auto i = 0; i < text.size();) {
		const auto code = CodeAt(text, i);
		const auto size = CodeSize(text, i);
		const auto lower = QChar::toLower(code);
		const auto use = ((lower > 0xFFFF ? 2 : 1) == size) ? lower : code;
		if (use == 0x0451) {
			result += QChar(0x0435);
		} else if (size == 1) {
			result += QChar(char16_t(use));
		} else {
			result += QChar(QChar::highSurrogate(use));
			result += QChar(QChar::lowSurrogate(use));
		}
		i += size;
	}
	last = { text, result };
	return result;
}

[[nodiscard]] bool IsLowerWord(const QString &word) {
	auto cased = false;
	for (const auto ch : word) {
		if (ch.isUpper() || ch.isTitleCase()) {
			return false;
		} else if (ch.isLower()) {
			cased = true;
		}
	}
	return cased;
}

[[nodiscard]] bool IsUpperWord(const QString &word) {
	auto cased = false;
	for (const auto ch : word) {
		if (ch.isLower() || ch.isTitleCase()) {
			return false;
		} else if (ch.isUpper()) {
			cased = true;
		}
	}
	return cased;
}

[[nodiscard]] QString Decimals(const QString &text) {
	auto result = QString();
	for (auto i = 0; i < text.size(); i += CodeSize(text, i)) {
		if (IsDecimal(CodeAt(text, i))) {
			result += text.mid(i, CodeSize(text, i));
		}
	}
	return result;
}

[[nodiscard]] int FindBefore(const QString &text, QChar ch, int till) {
	for (auto i = std::min(till, int(text.size())) - 1; i >= 0; --i) {
		if (text[i] == ch) {
			return i;
		}
	}
	return -1;
}

[[nodiscard]] QStringList SplitLines(const QString &text) {
	static const auto breaks = std::array<char16_t, 10>{
		0x0A, 0x0B, 0x0C, 0x0D, 0x1C, 0x1D, 0x1E, 0x85, 0x2028, 0x2029,
	};
	auto result = QStringList();
	auto from = 0;
	for (auto i = 0; i < text.size(); ++i) {
		const auto ch = text[i].unicode();
		if (std::find(begin(breaks), end(breaks), ch) == end(breaks)) {
			continue;
		}
		result.push_back(text.mid(from, i - from));
		if (ch == 0x0D && i + 1 < text.size() && text[i + 1] == u'\n') {
			++i;
		}
		from = i + 1;
	}
	if (from < text.size()) {
		result.push_back(text.mid(from));
	}
	return result;
}

[[nodiscard]] QStringList Unique(const QStringList &list) {
	auto result = QStringList();
	for (const auto &value : list) {
		if (!result.contains(value)) {
			result.push_back(value);
		}
	}
	return result;
}

[[nodiscard]] std::vector<Found> UniqueFound(const std::vector<Found> &list) {
	auto result = std::vector<Found>();
	for (const auto &found : list) {
		const auto same = [&](const Found &other) {
			return other.value == found.value;
		};
		if (std::find_if(begin(result), end(result), same) == end(result)) {
			result.push_back(found);
		}
	}
	return result;
}

[[nodiscard]] base::flat_set<QString> WordSet(const QString &words) {
	auto result = base::flat_set<QString>();
	for (const auto &word : words.split(u' ', Qt::SkipEmptyParts)) {
		result.emplace(word);
	}
	return result;
}

[[nodiscard]] bool LuhnOk(const QString &digits) {
	const auto codes = digits.toUcs4();
	auto total = 0;
	auto index = 0;
	for (auto i = codes.size(); i != 0; --i, ++index) {
		auto value = QChar::digitValue(codes[i - 1]);
		if (index % 2) {
			value *= 2;
			if (value > 9) {
				value -= 9;
			}
		}
		total += value;
	}
	return total % 10 == 0;
}

[[nodiscard]] bool IbanOk(const QString &iban) {
	if (!FullMatch(uR"re([A-Z]{2}\d{2}[A-Z0-9]{11,30})re"_q, iban)) {
		return false;
	}
	auto rest = 0;
	for (const auto code : (iban.mid(4) + iban.left(4)).toUcs4()) {
		const auto value = (code >= U'A' && code <= U'Z')
			? int(code - U'A' + 10)
			: QChar::digitValue(code);
		for (const auto digit : QString::number(value)) {
			rest = (rest * 10 + digit.digitValue()) % 97;
		}
	}
	return rest == 1;
}

[[nodiscard]] const std::vector<Row> &BankTable() {
	static const auto result = std::vector<Row>{
		{ uR"re(сбер(?:банк\w*|[аеуы]|ом)?|sber(?:bank)?)re"_q, u"Сбербанк"_q, u"RU"_q },
		{ uR"re(т[\s\-]?банк\w*|тинь?коф+\w*|тиньков\w*|тинек\w*|tinkoff|t[\s\-]?bank|тиньк)re"_q, u"Т-Банк"_q, u"RU"_q },
		{ uR"re(втб|vtb)re"_q, u"ВТБ"_q, u"RU"_q },
		{ uR"re(альф[аеуы]\w*|alfa[\s\-]?bank|alfa)re"_q, u"Альфа-Банк"_q, u"RU"_q },
		{ uR"re(озон\w*|ozon)re"_q, u"Озон Банк"_q, u"RU"_q },
		{ uR"re(ю[\s\-]?мани\w*|ю[\s\-]?money|yoo?money|ю[\s\-]?моней|яндекс[\s\-]?деньг\w*)re"_q, u"ЮMoney"_q, u"RU"_q },
		{ uR"re(яндекс\w*|yandex)re"_q, u"Яндекс Банк"_q, u"RU"_q },
		{ uR"re(мтс[\s\-]?деньг\w*|экси[\s\-]?банк\w*|эксибанк\w*)re"_q, u"Экси-Банк (МТС Деньги)"_q, u"RU"_q },
		{ uR"re(мтс|mts)re"_q, u"МТС Банк"_q, u"RU"_q },
		{ uR"re(газпром\w*|гпб|gazprom\w*)re"_q, u"Газпромбанк"_q, u"RU"_q },
		{ uR"re(рай?ф+ай?з+ен\w*|райф\w*|raif+eisen\w*)re"_q, u"Райффайзенбанк"_q, u"RU"_q },
		{ uR"re(совком\w*|халв[аеуы])re"_q, u"Совкомбанк"_q, u"RU"_q },
		{ uR"re(почта[\s\-]?банк\w*)re"_q, u"Почта Банк"_q, u"RU"_q },
		{ uR"re(рсхб|рос+ельхоз\w*)re"_q, u"Россельхозбанк"_q, u"RU"_q },
		{ uR"re(псб|промсвязь\w*)re"_q, u"ПСБ"_q, u"RU"_q },
		{ uR"re(уралсиб\w*)re"_q, u"Уралсиб"_q, u"RU"_q },
		{ uR"re(ак[\s\-]?барс\w*)re"_q, u"Ак Барс"_q, u"RU"_q },
		{ uR"re(мкб|московск\w+\s+кредитн\w+)re"_q, u"МКБ"_q, u"RU"_q },
		{ uR"re(росбанк\w*)re"_q, u"Росбанк"_q, u"RU"_q },
		{ uR"re(хоум\w*|home[\s\-]?credit)re"_q, u"Хоум Банк"_q, u"RU"_q },
		{ uR"re(русск\w+\s+стандарт\w*|русстандарт\w*)re"_q, u"Русский Стандарт"_q, u"RU"_q },
		{ uR"re(отп[\s\-]?банк\w*|otp[\s\-]?bank|otp)re"_q, u"ОТП Банк"_q, u"RU"_q },
		{ uR"re(ренессанс\w*)re"_q, u"Ренессанс Банк"_q, u"RU"_q },
		{ uR"re(ва[йил]{0,2}д?б[еи]р+[иые]?[зс]\w*|валбер[еи]с\w*|wildberries|wb[\s\-]?банк\w*|вб[\s\-]?банк\w*|вб|wb)re"_q, u"Вайлдберриз Банк"_q, u"RU"_q },
		{ uR"re(рокет\w*|ракет[\s\-]?банк\w*|rocket[\s\-]?bank)re"_q, u"Рокетбанк"_q, u"RU"_q },
		{ uR"re(фор[аеуы][\s\-]?банк\w*|forabank)re"_q, u"Фора-Банк"_q, u"RU"_q },
		{ uR"re(ингос+трах\w*)re"_q, u"Ингосстрах Банк"_q, u"RU"_q },
		{ uR"re(точка[\s\-]?банк\w*)re"_q, u"Точка"_q, u"RU"_q },
		{ uR"re(модуль[\s\-]?банк\w*)re"_q, u"Модульбанк"_q, u"RU"_q },
		{ uR"re(дом\.?\s?рф)re"_q, u"Дом.РФ"_q, u"RU"_q },
		{ uR"re(бспб|банк\w*\s+санкт-петербург)re"_q, u"Банк Санкт-Петербург"_q, u"RU"_q },
		{ uR"re(цифра[\s\-]?банк\w*)re"_q, u"Цифра Банк"_q, u"RU"_q },
		{ uR"re(кубань[\s\-]?кредит\w*)re"_q, u"Кубань Кредит"_q, u"RU"_q },
		{ uR"re(зенит\w*)re"_q, u"Зенит"_q, u"RU"_q },
		{ uR"re(синара)re"_q, u"Синара"_q, u"RU"_q },
		{ uR"re(банк\w*\s+открыти[ея]|открыти[ея][\s\-]банк\w*)re"_q, u"Открытие"_q, u"RU"_q },
		{ uR"re(каспи\w*|kaspi\w*)re"_q, u"Kaspi Bank"_q, u"KZ"_q },
		{ uR"re(халык\w*|halyk\w*)re"_q, u"Halyk Bank"_q, u"KZ"_q },
		{ uR"re(jusan|жусан\w*)re"_q, u"Jusan Bank"_q, u"KZ"_q },
		{ uR"re(forte\w*|форте[\s\-]?банк\w*)re"_q, u"ForteBank"_q, u"KZ"_q },
		{ uR"re(tbc|тбс|тбц|тибиси)re"_q, u"TBC Bank"_q, u"GE"_q },
		{ uR"re(bank\s+of\s+georgia|банк\w*\s+грузии)re"_q, u"Bank of Georgia"_q, u"GE"_q },
		{ uR"re(credo)re"_q, u"Credo Bank"_q, u"GE"_q },
		{ uR"re(liberty)re"_q, u"Liberty Bank"_q, u"GE"_q },
		{ uR"re(basis\s?bank)re"_q, u"Basisbank"_q, u"GE"_q },
		{ uR"re(procredit)re"_q, u"ProCredit Bank"_q, u"GE"_q },
		{ uR"re(беларусбанк\w*|belarusbank)re"_q, u"Беларусбанк"_q, u"BY"_q },
		{ uR"re(приорбанк\w*|priorbank)re"_q, u"Приорбанк"_q, u"BY"_q },
		{ uR"re(моно[\s\-]?банк\w*|mono[\s\-]?bank|монобанк\w*)re"_q, u"Monobank"_q, u"UA"_q },
		{ uR"re(приват[\s\-]?банк\w*|privat[\s\-]?bank)re"_q, u"ПриватБанк"_q, u"UA"_q },
		{ uR"re(humo|хумо)re"_q, u"Humo"_q, u"UZ"_q },
		{ uR"re(uzcard|узкард\w*)re"_q, u"Uzcard"_q, u"UZ"_q },
		{ uR"re(mbank|мбанк\w*)re"_q, u"MBank"_q, u"KG"_q },
		{ uR"re(optima|оптима[\s\-]?банк\w*)re"_q, u"Optima Bank"_q, u"KG"_q },
		{ uR"re(ameria\w*|америя\w*)re"_q, u"Ameriabank"_q, u"AM"_q },
		{ uR"re(revolut|револют\w*)re"_q, u"Revolut"_q, u"EU"_q },
	};
	return result;
}

[[nodiscard]] const std::vector<Row> &ExactCaseBankTable() {
	static const auto result = std::vector<Row>{
		{ uR"re(ОТП)re"_q, u"ОТП Банк"_q, u"RU"_q },
	};
	return result;
}

[[nodiscard]] const std::vector<Row> &ExchangeTable() {
	static const auto result = std::vector<Row>{
		{ uR"re(bybit|by\s?bit|байбит\w*|бай\s?бит\w*|бибит\w*)re"_q, u"Bybit"_q, QString() },
		{ uR"re(binance|бинанс\w*)re"_q, u"Binance"_q, QString() },
		{ uR"re(okx|окх)re"_q, u"OKX"_q, QString() },
		{ uR"re(htx|huobi|хуоби)re"_q, u"HTX"_q, QString() },
		{ uR"re(mexc|мекс\w*)re"_q, u"MEXC"_q, QString() },
		{ uR"re(bitget|битгет\w*)re"_q, u"Bitget"_q, QString() },
	};
	return result;
}

[[nodiscard]] const std::vector<Row> &CoinTable() {
	static const auto result = std::vector<Row>{
		{ uR"re(usdt|юсдт|тезер\w*|tether)re"_q, u"USDT"_q, QString() },
		{ uR"re(usdc)re"_q, u"USDC"_q, QString() },
		{ uR"re(trx)re"_q, u"TRX"_q, QString() },
		{ uR"re(btc|биткоин\w*|bitcoin)re"_q, u"BTC"_q, QString() },
		{ uR"re(ton|тон)re"_q, u"TON"_q, QString() },
	};
	return result;
}

[[nodiscard]] const std::vector<Row> &NetworkTable() {
	static const auto result = std::vector<Row>{
		{ uR"re(trc[\s\-]?20|tron|трон)re"_q, u"TRC20"_q, u"tron"_q },
		{ uR"re(bep[\s\-]?20|bsc|bnb\s*(?:smart\s*)?chain)re"_q, u"BEP20"_q, u"evm"_q },
		{ uR"re(erc[\s\-]?20|ethereum)re"_q, u"ERC20"_q, u"evm"_q },
		{ uR"re(arbitrum)re"_q, u"Arbitrum"_q, u"evm"_q },
		{ uR"re(polygon|matic)re"_q, u"Polygon"_q, u"evm"_q },
		{ uR"re(ton|тон)re"_q, u"TON"_q, u"ton"_q },
		{ uR"re(btc|bitcoin|биткоин\w*)re"_q, u"BTC"_q, u"btc"_q },
	};
	return result;
}

[[nodiscard]] const std::vector<PhoneCode> &PhoneCodes() {
	static const auto result = std::vector<PhoneCode>{
		{ u"375"_q, u"BY"_q, 9 },
		{ u"380"_q, u"UA"_q, 9 },
		{ u"995"_q, u"GE"_q, 9 },
		{ u"998"_q, u"UZ"_q, 9 },
		{ u"996"_q, u"KG"_q, 9 },
		{ u"992"_q, u"TJ"_q, 9 },
		{ u"374"_q, u"AM"_q, 8 },
		{ u"994"_q, u"AZ"_q, 9 },
		{ u"373"_q, u"MD"_q, 8 },
		{ u"90"_q, u"TR"_q, 10 },
	};
	return result;
}

[[nodiscard]] QString CardCountry(const QString &digits) {
	using Pair = std::pair<QStringView, QStringView>;
	static const auto prefixes = std::array<Pair, 3>{ {
		{ u"9112", u"BY" },
		{ u"9860", u"UZ" },
		{ u"8600", u"UZ" },
	} };
	for (const auto &[prefix, country] : prefixes) {
		if (digits.startsWith(prefix)) {
			return country.toString();
		}
	}
	return u"RU"_q;
}

[[nodiscard]] QString CountryName(const QString &country) {
	using Pair = std::pair<QStringView, QStringView>;
	static const auto names = std::array<Pair, 12>{ {
		{ u"KZ", u"Казахстан" },
		{ u"GE", u"Грузия" },
		{ u"BY", u"Беларусь" },
		{ u"UA", u"Украина" },
		{ u"UZ", u"Узбекистан" },
		{ u"KG", u"Кыргызстан" },
		{ u"AM", u"Армения" },
		{ u"AZ", u"Азербайджан" },
		{ u"TR", u"Турция" },
		{ u"EU", u"Европа" },
		{ u"TJ", u"Таджикистан" },
		{ u"MD", u"Молдова" },
	} };
	for (const auto &[code, name] : names) {
		if (country == code) {
			return name.toString();
		}
	}
	return u"Иностранные реквизиты"_q;
}

[[nodiscard]] const base::flat_set<QString> &NotBankWords() {
	static const auto result = WordSet(u"any my of the your в ваш другой "
		"запасной из какой любой мобильный мой моя на наш один онлайн "
		"основной основным по свой твой тот через этот"_q);
	return result;
}

[[nodiscard]] const base::flat_set<QString> &StopWords() {
	static const auto result = WordSet(u"bank bep card erc here is maestro "
		"mastercard memo my name number of phone please send thank thanks "
		"the to ton trc uid usdc usdt visa you а адрес банк бик благодарю "
		"буду вам вечер виза вот все всего всё вывести вывод готово да "
		"давайте день доброе добрый договор договора другие другой ждать "
		"ждем жду ждём же закрепите заранее здравствуйте здраствуйте и "
		"изменились изменился или имя инн карта карту карты корсчет кошелек "
		"кошелёк кпп лучше меня мир мне можете можно мои мой монета моя на "
		"надо нам нет но новая новому новые новый номер нужно ок окей "
		"отправил отправила отправьте переведите перезакрепите пж пжл плиз "
		"по пожалуйста получатель получателя поменялся привет пришли "
		"реквизит реквизиты сбп сеть сменился снова спасибо спасибочки "
		"старому старые старый счет счёт счёта сюда тел телефон типнул "
		"типнула тоже только тот туда уже утро фио хорошего хорошо хотел "
		"хотела хочу это"_q);
	return result;
}

const auto kUidWords = uR"re(uid|u\.?id|юид\w*|уид\w*|uuid|айди)re"_q;
const auto kNegative = uR"re((?<!\w)не(?!\w)|(?<!\w)нельзя|блок|блоч|арест|закры|боюсь|не\s*актуал|удал|стар(?:ые|ый|ая|ую|ой|ого)|прежн|больше\s+не|устар|неактуал|недейств|аннулир|истек)re"_q;
const auto kAmountTail = uR"re([ \t]*(?:ток|тк|tk|руб|р\b|₽|\$|usd\b|долл|k\b|к\b|%))re"_q;
const auto kAmountHead = uR"re((?<!\w)(?:сумм\w*|вывести|вывод\w*|обменя\w*|обмен\w*|перевести|отправила?|типнула?)[\s:\-]*$)re"_q;
const auto kGenericBank = uR"re((?<![\w])(?:([A-Za-zА-Яа-яЁё][\w\-]{1,})[ \-]?(?:банк|bank)|(?:банк|bank)\s+([A-Za-zА-Яа-яЁё][\w\-]{1,})|([А-Яа-яЁё]{3,}банк))(?![\w]))re"_q;
const auto kPatronymic = uR"re((ович|евич|ьич|овна|евна|ична|инична|оглы|кызы)$)re"_q;
const auto kWord = uR"re([A-Za-zА-Яа-яЁё]+(?:-[A-Za-zА-Яа-яЁё]+)*\.?)re"_q;
const auto kSegmentBreak = uR"re([\n\x00,;:()!?/|«»\"—–]|(?<=[а-яёa-z]{2})\.\s+|(?<!\w)(?i:или|либо)(?!\w))re"_q;
const auto kLabel = uR"re((?i)^(?:(?P<bank>(?:beneficiary\s+)?bank(?:\s+name)?|банк(?:\s+получателя)?|название\s+банка)|(?P<swift>swift(?:\s*/\s*bic)?(?:\s+code)?|bic(?:\s+code)?|свифт|бик)|iban|acc(?:ount)?(?:\s*(?:number|no\.?|№))?|(?:номер\s+)?сч[её]та?|р\s*/\s*с|card(?:\s+number)?|(?:номер\s+)?карты|карта|phone|(?:номер\s+)?телефона?|тел\.?|(?:full\s+)?name|beneficiary(?:\s+name)?|recipient(?:\s+name)?|имя(?:\s+получателя)?|фио|получатель)\s*:\s*)re"_q;
const auto kSwiftLine = uR"re((?i)(?<!\w)(?:swift|bic|бик|свифт)(?!\w))re"_q;
const auto kBic = uR"re((?<![A-Za-z0-9])[A-Z]{6}[A-Z0-9]{2}(?:[A-Z0-9]{3})?(?![A-Za-z0-9]))re"_q;
const auto kHintClient = u"Напишите реквизиты своим сообщением и нажмите "
	"«Реквизиты» на нём."_q;
const auto kHintOwn = u"Проверьте, что в сообщении есть номер и банк, и "
	"нажмите ещё раз."_q;
const auto kBadCard = u"Номер карты не проходит проверку — видимо, опечатка. "
	"Уточните у клиента."_q;

[[nodiscard]] QString WordPattern(const QString &alias) {
	return u"(?<![\\w])(?:"_q + alias + u")(?![\\w])"_q;
}

[[nodiscard]] QString Clause(const QString &text, int start, int end) {
	const auto low = Lower(text);
	auto left = -1;
	auto right = int(low.size());
	for (const auto ch : u".!?\n;,"_q) {
		left = std::max(left, FindBefore(low, ch, start));
		const auto found = int(low.indexOf(ch, end));
		if (found >= 0) {
			right = std::min(right, found);
		}
	}
	return low.mid(left + 1, right - (left + 1));
}

[[nodiscard]] bool Negative(const QString &text, const Found &found) {
	return Search(kNegative, Clause(text, found.start, found.end)).hasMatch();
}

[[nodiscard]] int LineOf(const QString &text, int position) {
	return int(text.left(position).count(u'\n'));
}

[[nodiscard]] std::vector<Found> Identifiers(const QString &text) {
	auto found = std::vector<Found>();
	auto taken = std::vector<std::pair<int, int>>();
	const auto isFree = [&](int start, int end) {
		for (const auto &[from, till] : taken) {
			if (!(end <= from || start >= till)) {
				return false;
			}
		}
		return true;
	};
	const auto add = [&](
			Kind kind,
			int start,
			int end,
			const QString &value,
			const QString &extra,
			bool claim) {
		found.push_back({ kind, value, start, end, extra });
		if (claim) {
			taken.emplace_back(start, end);
		}
	};
	const auto wallets = std::array<std::pair<QString, QString>, 4>{ {
		{ uR"re((?<![A-Za-z0-9])T[1-9A-HJ-NP-Za-km-z]{33}(?![A-Za-z0-9]))re"_q, u"tron"_q },
		{ uR"re((?<![A-Za-z0-9])0x[0-9a-fA-F]{40}(?![A-Za-z0-9]))re"_q, u"evm"_q },
		{ uR"re((?<![A-Za-z0-9_\-])(?:EQ|UQ|Ef|Uf|kQ|0Q)[A-Za-z0-9_\-]{46}(?![A-Za-z0-9_\-]))re"_q, u"ton"_q },
		{ uR"re((?<![A-Za-z0-9])bc1[ac-hj-np-z02-9]{11,71}(?![A-Za-z0-9]))re"_q, u"btc"_q },
	} };
	for (const auto &[pattern, family] : wallets) {
		for (const auto &m : FindAll(pattern, text)) {
			add(
				Kind::Wallet,
				int(m.capturedStart()),
				int(m.capturedEnd()),
				m.captured(),
				family,
				true);
		}
	}

	for (const auto &m : FindAll(uR"re((?<![A-Za-z0-9])[A-Z]{2}\d{2}(?:[ ]?[A-Z0-9]){11,32})re"_q, text)) {
		if (!isFree(int(m.capturedStart()), int(m.capturedEnd()))) {
			continue;
		}
		const auto compact = m.captured().remove(u' ');
		const auto longest = std::min(CodeCount(compact), 34);
		for (auto length = longest; length > 14; --length) {
			const auto candidate = LeftCodes(compact, length);
			if (!IbanOk(candidate)) {
				continue;
			}
			auto till = int(m.capturedStart());
			auto seen = 0;
			while (seen < length) {
				if (CodeAt(text, till) != U' ') {
					++seen;
				}
				till += CodeSize(text, till);
			}
			if (till < text.size() && IsAlnum(CodeAt(text, till))) {
				continue;
			}
			add(
				Kind::Iban,
				int(m.capturedStart()),
				till,
				candidate,
				candidate.left(2),
				true);
			break;
		}
	}

	for (const auto &m : FindAll(uR"re((?<!\d)\d{20}(?!\d))re"_q, text)) {
		if (isFree(int(m.capturedStart()), int(m.capturedEnd()))) {
			add(
				Kind::Account,
				int(m.capturedStart()),
				int(m.capturedEnd()),
				m.captured(),
				QString(),
				true);
		}
	}

	const auto card = uR"re((?<!\d)(?:\d{4}[ \-]\d{4}[ \-]\d{4}[ \-]\d{4}(?:[ \-]\d{3})?|\d{16,19})(?!\d))re"_q;
	for (const auto &m : FindAll(card, text)) {
		if (isFree(int(m.capturedStart()), int(m.capturedEnd()))) {
			const auto digits = Decimals(m.captured());
			add(
				Kind::Card,
				int(m.capturedStart()),
				int(m.capturedEnd()),
				digits,
				CardCountry(digits),
				true);
		}
	}

	const auto ru = uR"re((?<![\w+=])(?:[+=]\s?)?[78][ \t\-]*\(?[ \t]*(\d{3})[ \t]*\)?[ \t\-]*(\d{3})[ \t\-]*(\d{2})[ \t\-]*(\d{2})(?!\d))re"_q;
	for (const auto &m : FindAll(ru, text)) {
		if (isFree(int(m.capturedStart()), int(m.capturedEnd()))) {
			const auto digits = m.captured(1)
				+ m.captured(2)
				+ m.captured(3)
				+ m.captured(4);
			const auto kz = (digits[0] == u'6' || digits[0] == u'7');
			add(
				Kind::Phone,
				int(m.capturedStart()),
				int(m.capturedEnd()),
				u"+7"_q + digits,
				kz ? u"KZ"_q : u"RU"_q,
				true);
		}
	}
	auto codes = PhoneCodes();
	const auto longer = [](const PhoneCode &a, const PhoneCode &b) {
		return a.code.size() > b.code.size();
	};
	std::stable_sort(begin(codes), end(codes), longer);
	for (const auto &m : FindAll(uR"re((?<![\w+])\+\s?(\d[\d \t\-\(\)]*))re"_q, text)) {
		const auto run = Decimals(m.captured(1));
		const auto prefix = [&](const PhoneCode &entry) {
			return run.startsWith(entry.code);
		};
		const auto code = std::find_if(begin(codes), end(codes), prefix);
		if (code == end(codes)
			|| !isFree(int(m.capturedStart()), int(m.capturedEnd()))) {
			continue;
		}
		const auto need = int(code->code.size()) + code->national;
		if (CodeCount(run) < need) {
			continue;
		}
		auto till = int(m.capturedStart(1));
		auto seen = 0;
		while (seen < need) {
			if (IsDigit(CodeAt(text, till))) {
				++seen;
			}
			till += CodeSize(text, till);
		}
		if (till < text.size() && IsDigit(CodeAt(text, till))) {
			continue;
		}
		add(
			Kind::Phone,
			int(m.capturedStart()),
			till,
			u'+' + LeftCodes(run, need),
			code->country,
			true);
	}

	const auto low = Lower(text);
	for (const auto &m : FindAll(uR"re((?<![\d\w.,+])(\d{6,10})(?!\d)(?![.,]\d))re"_q, text)) {
		const auto start = int(m.capturedStart());
		const auto till = int(m.capturedEnd());
		if (!isFree(start, till)) {
			continue;
		}
		const auto window = BackCodes(low, start, kAmountWindow);
		if (MatchAt(kAmountTail, low, till).hasMatch()
			|| Search(kAmountHead, low.left(start), window).hasMatch()) {
			continue;
		}
		add(Kind::Uid, start, till, m.captured(1), QString(), false);
		if (FullMatch(uR"re(9\d{9})re"_q, m.captured(1))) {
			const auto phone = u"+7"_q + m.captured(1);
			add(Kind::Phone, start, till, phone, u"RU"_q, false);
		}
	}
	return found;
}

[[nodiscard]] std::vector<Found> Banks(const QString &text) {
	const auto low = Lower(text);
	auto out = std::vector<Found>();
	auto taken = std::vector<std::pair<int, int>>();
	for (const auto &bank : BankTable()) {
		for (const auto &m : FindAll(WordPattern(bank.alias), low)) {
			const auto start = int(m.capturedStart());
			auto till = int(m.capturedEnd());
			const auto overlaps = [&](const std::pair<int, int> &span) {
				return !(till <= span.first || start >= span.second);
			};
			if (std::any_of(begin(taken), end(taken), overlaps)) {
				continue;
			}
			const auto tail = MatchAt(uR"re([\s\-]*(?:банк\w*|bank)(?!\w))re"_q, low, till);
			if (tail.hasMatch()) {
				till = int(tail.capturedEnd());
			}
			out.push_back({ Kind::Bank, bank.name, start, till, bank.extra });
			taken.emplace_back(start, till);
		}
	}
	for (const auto &bank : ExactCaseBankTable()) {
		for (const auto &m : FindAll(WordPattern(bank.alias), text)) {
			out.push_back({
				Kind::Bank,
				bank.name,
				int(m.capturedStart()),
				int(m.capturedEnd()),
				bank.extra,
			});
		}
	}
	return out;
}

[[nodiscard]] std::vector<Found> GenericBanks(const QString &text) {
	auto out = std::vector<Found>();
	for (const auto &m : FindAll(kGenericBank, text, true)) {
		const auto word = !m.captured(1).isEmpty()
			? m.captured(1)
			: !m.captured(2).isEmpty()
			? m.captured(2)
			: m.captured(3);
		if (NotBankWords().contains(Lower(word))) {
			continue;
		}
		out.push_back({
			Kind::Bank,
			Strip(m.captured()),
			int(m.capturedStart()),
			int(m.capturedEnd()),
			QString(),
		});
	}
	return out;
}

[[nodiscard]] std::vector<Found> Words(
		const QString &text,
		const std::vector<Row> &table,
		Kind kind) {
	const auto low = Lower(text);
	auto out = std::vector<Found>();
	for (const auto &row : table) {
		for (const auto &m : FindAll(WordPattern(row.alias), low)) {
			out.push_back({
				kind,
				row.name,
				int(m.capturedStart()),
				int(m.capturedEnd()),
				row.extra,
			});
		}
	}
	return out;
}

[[nodiscard]] std::vector<Found> Memos(const QString &text) {
	const auto pattern = uR"re((?<!\w)(?:memo|мемо|комментари\w*|comment|тег|tag)\s*[:\-]?\s*([A-Za-z0-9]{1,40})(?![A-Za-z0-9]))re"_q;
	auto out = std::vector<Found>();
	for (const auto &m : FindAll(pattern, text, true)) {
		out.push_back({
			Kind::Memo,
			m.captured(1),
			int(m.capturedStart(1)),
			int(m.capturedEnd(1)),
			QString(),
		});
	}
	return out;
}

[[nodiscard]] std::vector<std::pair<int, int>> UidKeywords(
		const QString &text) {
	auto out = std::vector<std::pair<int, int>>();
	for (const auto &m : FindAll(WordPattern(kUidWords), Lower(text))) {
		out.emplace_back(int(m.capturedStart()), int(m.capturedEnd()));
	}
	return out;
}

[[nodiscard]] std::vector<Found> PositiveWords(
		const QString &text,
		const std::vector<Row> &table,
		Kind kind) {
	auto out = std::vector<Found>();
	for (const auto &word : Words(text, table, kind)) {
		if (!Negative(text, word)) {
			out.push_back(word);
		}
	}
	return out;
}

[[nodiscard]] QString Line(const QString &text, const Found &found) {
	const auto start = FindBefore(text, u'\n', found.start) + 1;
	const auto till = int(text.indexOf(u'\n', found.end));
	return text.mid(start, (till >= 0 ? till : int(text.size())) - start);
}

[[nodiscard]] bool Weak(const QString &text, const Found &found) {
	return (found.kind == Kind::Phone)
		&& !MatchAt(uR"re([+=78])re"_q, text, found.start).hasMatch();
}

[[nodiscard]] QString Case(const QString &word) {
	const auto bare = StripDots(word);
	if (FullMatch(uR"re([A-Za-z\-]+)re"_q, bare) || bare.size() == 1) {
		return word;
	} else if (IsLowerWord(bare) || IsUpperWord(bare)) {
		return word[0].toUpper() + word.mid(1).toLower();
	}
	return word;
}

[[nodiscard]] QString Name(
		const QString &segment,
		bool keepCase = false,
		bool strict = false) {
	const auto stripped = Strip(segment);
	if (stripped.isEmpty()
		|| Search(uR"re([0-9@#$%&*+=_\x00])re"_q, stripped).hasMatch()) {
		return QString();
	}
	const auto matches = FindAll(kWord, segment);
	auto words = QStringList();
	for (const auto &m : matches) {
		words.push_back(m.captured());
	}
	auto squeezed = QString();
	for (auto i = 0; i < segment.size(); i += CodeSize(segment, i)) {
		const auto code = CodeAt(segment, i);
		if (!IsSpace(code) && code != U'.' && code != U'\'' && code != U'-') {
			squeezed += segment.mid(i, CodeSize(segment, i));
		}
	}
	auto joined = QString();
	for (const auto &word : words) {
		joined += QString(word).remove(u'.').remove(u'-');
	}
	if (words.isEmpty() || words.size() > 4 || squeezed != joined) {
		return QString();
	}
	auto bare = QStringList();
	for (const auto &word : words) {
		bare.push_back(StripDots(word));
	}
	const auto capital = [](const QString &word) {
		return word[0].isUpper();
	};
	if (strict
		&& (words.size() < 2
			|| !std::all_of(bare.begin(), bare.end(), capital))) {
		return QString();
	}
	auto common = QStringList();
	for (auto i = 0; i < int(bare.size()); ++i) {
		const auto initial = (bare[i].size() == 1)
			&& bare[i][0].isUpper()
			&& (i > 0 || words[i].endsWith(u'.'));
		if (!initial) {
			common.push_back(bare[i]);
		}
	}
	const auto stop = [](const QString &word) {
		return StopWords().contains(Lower(word));
	};
	const auto single = [](const QString &word) {
		return word.size() == 1;
	};
	if (std::any_of(common.begin(), common.end(), stop)
		|| std::all_of(bare.begin(), bare.end(), single)) {
		return QString();
	} else if (words.size() == 1 && bare[0].size() < 3) {
		return QString();
	} else if (keepCase) {
		return stripped;
	}
	auto fixed = QStringList();
	for (const auto &word : words) {
		fixed.push_back(Case(word));
	}
	auto gaps = QStringList();
	for (auto i = 1; i < int(matches.size()); ++i) {
		const auto joined = (matches[i - 1].capturedEnd()
			== matches[i].capturedStart());
		gaps.push_back(joined ? QString() : u" "_q);
	}
	if (fixed.size() == 3
		&& Search(kPatronymic, Lower(fixed[2])).hasMatch()
		&& !Search(kPatronymic, Lower(fixed[1])).hasMatch()
		&& StripDots(fixed[0]).size() > 1) {
		return fixed[1] + gaps[1] + fixed[2] + u' ' + fixed[0];
	}
	auto result = fixed[0];
	for (auto i = 1; i < int(fixed.size()); ++i) {
		result += gaps[i - 1] + fixed[i];
	}
	return result;
}

[[nodiscard]] bool ForeignName(const QString &line) {
	const auto name = Name(line, true);
	if (name.isEmpty()) {
		return false;
	}
	auto capitals = 0;
	const auto words = FindAll(kWord, name);
	for (const auto &m : words) {
		if (StripDots(m.captured())[0].isUpper()) {
			++capitals;
		}
	}
	return (words.size() == 1)
		|| (capitals == int(words.size()))
		|| (capitals == 0);
}

[[nodiscard]] QStringList Names(
		const QString &text,
		const std::vector<Found> &found) {
	auto masked = text;
	auto spans = std::vector<std::pair<int, int>>();
	for (const auto &entry : found) {
		spans.emplace_back(entry.start, entry.end);
	}
	for (const auto &bank : Banks(text)) {
		spans.emplace_back(bank.start, bank.end);
	}
	const auto tables = { &ExchangeTable(), &CoinTable(), &NetworkTable() };
	for (const auto table : tables) {
		for (const auto &word : Words(text, *table, Kind::Exchange)) {
			spans.emplace_back(word.start, word.end);
		}
	}
	for (const auto &span : UidKeywords(text)) {
		spans.push_back(span);
	}
	for (const auto &m : FindAll(uR"re((?i)memo|мемо|tag|тег)re"_q, text)) {
		spans.emplace_back(int(m.capturedStart()), int(m.capturedEnd()));
	}
	for (const auto &[from, till] : spans) {
		for (auto i = from; i < till; ++i) {
			masked[i] = QChar(0);
		}
	}
	auto out = QStringList();
	auto start = 0;
	const auto breaks = FindAll(kSegmentBreak, masked);
	for (auto i = 0; i <= int(breaks.size()); ++i) {
		const auto last = (i == int(breaks.size()));
		const auto till = last
			? int(masked.size())
			: int(breaks[i].capturedStart());
		const auto segment = masked.mid(start, till - start);
		const auto position = start;
		start = last ? int(masked.size()) : int(breaks[i].capturedEnd());
		const auto pieces = Split(uR"re([0-9]+)re"_q, segment);
		for (const auto &piece : pieces) {
			const auto name = Name(piece, false, pieces.size() > 1);
			const auto clause = Clause(text, position, till);
			if (!name.isEmpty() && !Search(kNegative, clause).hasMatch()) {
				out.push_back(name);
			}
		}
	}
	return Unique(out);
}

[[nodiscard]] QString Swift(const QString &text) {
	if (!Search(uR"re((?<!\w)(?:swift|bic|свифт|бик)(?!\w))re"_q, Lower(text)).hasMatch()) {
		return QString();
	}
	auto codes = QStringList();
	for (const auto &m : FindAll(kBic, text)) {
		codes.push_back(m.captured());
	}
	codes = Unique(codes);
	return (codes.size() == 1) ? codes[0] : QString();
}

[[nodiscard]] std::vector<Set> Fiat(
		const QString &text,
		const std::vector<Found> &phones,
		const std::vector<Found> &cards,
		const std::vector<Found> &accounts,
		const std::vector<Found> &banks,
		const std::vector<Found> &found,
		QStringList &warnings,
		QString &refused) {
	for (const auto &card : cards) {
		if (!LuhnOk(card.value)) {
			refused = kBadCard;
			return {};
		}
	}
	if (accounts.size() > 1) {
		refused = u"В сообщении несколько счетов — не понял, какой нужен."_q;
		return {};
	}
	const auto isAccount = [](const Found &found) {
		return found.kind == Kind::Account;
	};
	auto bottom = std::vector<std::optional<Found>>{ std::nullopt };
	auto idents = std::vector<std::optional<Found>>();
	if (std::any_of(begin(accounts), end(accounts), isAccount)
		&& !cards.empty()) {
		idents.assign(begin(cards), end(cards));
	} else if (!phones.empty()) {
		idents.assign(begin(phones), end(phones));
		if (!cards.empty()) {
			bottom.assign(begin(cards), end(cards));
		}
	} else if (!cards.empty()) {
		idents.assign(begin(cards), end(cards));
	} else {
		idents.push_back(std::nullopt);
	}
	auto bankNames = QStringList();
	for (const auto &bank : banks) {
		bankNames.push_back(bank.value);
	}
	bankNames = Unique(bankNames);
	if (bankNames.isEmpty()) {
		const auto isIban = [](const Found &found) {
			return found.kind == Kind::Iban;
		};
		if (std::none_of(begin(accounts), end(accounts), isIban)) {
			refused = u"Нашёл номер, но не нашёл банк."_q;
			return {};
		}
		bankNames.push_back(QString());
	}
	auto bankCountry = std::map<QString, QString>();
	for (const auto &bank : banks) {
		bankCountry[bank.value] = bank.extra;
	}
	auto names = Names(text, found);
	if (names.isEmpty()) {
		names.push_back(QString());
		warnings.push_back(u"Имя получателя не нашёл."_q);
	}
	const auto swift = Swift(text);
	auto options = std::vector<Set>();
	for (const auto &ident : idents) {
		for (const auto &lowCard : bottom) {
			for (const auto &bank : bankNames) {
				for (const auto &name : names) {
					auto candidates = QStringList{
						ident ? ident->extra : QString(),
						(bankCountry.contains(bank)
							? bankCountry[bank]
							: u"RU"_q),
					};
					for (const auto &account : accounts) {
						candidates.push_back(account.extra);
					}
					auto country = u"RU"_q;
					for (const auto &candidate : candidates) {
						if (!candidate.isEmpty() && candidate != u"RU"_q) {
							country = candidate;
							break;
						}
					}
					options.push_back({
						.kind = SetKind::Fiat,
						.country = country,
						.ident = ident,
						.card = lowCard,
						.bank = bank,
						.name = name,
						.accounts = accounts,
						.swift = swift,
					});
				}
			}
		}
	}
	return options;
}

[[nodiscard]] Set Foreign(
		const QString &text,
		const std::vector<Found> &banks,
		const std::vector<Found> &idents,
		QString &refused) {
	auto kept = std::vector<std::pair<bool, QString>>();
	for (const auto &line : text.split(u'\n')) {
		const auto clean = Strip(line);
		if (clean.isEmpty() || Search(kNegative, Lower(clean)).hasMatch()) {
			continue;
		}
		const auto label = Search(kLabel, clean);
		const auto value = label.hasMatch()
			? Strip(clean.mid(label.capturedEnd()))
			: clean;
		if (value.isEmpty()) {
			continue;
		}
		auto ids = false;
		for (const auto &found : Identifiers(value)) {
			if (found.kind == Kind::Card && !LuhnOk(found.value)) {
				refused = kBadCard;
				return {};
			} else if (found.kind == Kind::Phone
				|| found.kind == Kind::Card
				|| found.kind == Kind::Iban
				|| found.kind == Kind::Account) {
				ids = true;
			}
		}
		const auto bank = !label.captured(u"bank"_q).isEmpty()
			|| !Banks(value).empty()
			|| !GenericBanks(value).empty();
		const auto swift = !label.captured(u"swift"_q).isEmpty()
			|| Search(kSwiftLine, value).hasMatch();
		if (label.hasMatch() || ids || bank || swift || ForeignName(value)) {
			kept.emplace_back(bank, value);
		}
	}
	if (kept.empty()) {
		refused = u"Не нашёл строк с реквизитами."_q;
		return {};
	}
	auto country = u"EU"_q;
	auto all = idents;
	all.insert(end(all), begin(banks), end(banks));
	for (const auto &found : all) {
		if (!found.extra.isEmpty() && found.extra != u"RU"_q) {
			country = found.extra;
			break;
		}
	}
	auto lines = QStringList();
	for (const auto bankFirst : { true, false }) {
		for (const auto &[bank, line] : kept) {
			if (bank == bankFirst) {
				lines.push_back(line);
			}
		}
	}
	return { .kind = SetKind::Fiat, .country = country, .rawLines = lines };
}

[[nodiscard]] std::vector<Set> Uid(
		const std::vector<Found> &uids,
		const std::vector<Found> &exchanges,
		QStringList &warnings) {
	auto names = QStringList();
	for (const auto &exchange : exchanges) {
		names.push_back(exchange.value);
	}
	names = Unique(names);
	if (names.isEmpty()) {
		names.push_back(u"Bybit"_q);
		warnings.push_back(
			u"Биржа не названа — поставил Bybit, проверьте."_q);
	}
	auto result = std::vector<Set>();
	for (const auto &uid : uids) {
		for (const auto &name : names) {
			result.push_back({
				.kind = SetKind::Uid,
				.exchange = name,
				.uid = uid.value,
			});
		}
	}
	return result;
}

[[nodiscard]] QString Memo(
		const QString &text,
		const Found &wallet,
		const std::vector<Found> &wallets,
		QString &refused) {
	auto memos = std::vector<Found>();
	for (const auto &memo : Memos(text)) {
		if (!Negative(text, memo)) {
			memos.push_back(memo);
		}
	}
	if (wallets.size() > 1) {
		const auto line = LineOf(text, wallet.start);
		auto walletLines = base::flat_set<int>();
		for (const auto &other : wallets) {
			walletLines.emplace(LineOf(text, other.start));
		}
		for (const auto &memo : memos) {
			if (!walletLines.contains(LineOf(text, memo.start))) {
				refused = u"Memo стоит отдельно от адресов — не понял, "
					"к какому оно."_q;
				return QString();
			}
		}
		const auto other = [&](const Found &memo) {
			return LineOf(text, memo.start) != line;
		};
		memos.erase(
			std::remove_if(begin(memos), end(memos), other),
			end(memos));
	}
	auto values = QStringList();
	for (const auto &memo : memos) {
		values.push_back(memo.value);
	}
	values = Unique(values);
	if (values.size() > 1) {
		refused = u"У адреса несколько memo — не понял, какое нужно."_q;
		return QString();
	}
	return values.isEmpty() ? QString() : values[0];
}

[[nodiscard]] std::vector<Set> Wallet(
		const QString &text,
		const Found &wallet,
		const std::vector<Found> &wallets,
		QString &refused) {
	const auto &family = wallet.extra;
	const auto scope = (wallets.size() > 1) ? Line(text, wallet) : text;
	auto coins = QStringList();
	for (const auto &coin : PositiveWords(scope, CoinTable(), Kind::Coin)) {
		coins.push_back(coin.value);
	}
	coins = Unique(coins);
	const auto nets = PositiveWords(scope, NetworkTable(), Kind::Network);
	const auto memo = Memo(text, wallet, wallets, refused);
	if (!refused.isEmpty()) {
		return {};
	}
	const auto named = [&](const QStringList &allowed) {
		auto result = QStringList();
		for (const auto &coin : coins) {
			if (allowed.contains(coin)) {
				result.push_back(coin);
			}
		}
		return result;
	};
	auto pairs = std::vector<std::pair<QString, QString>>();
	if (family == u"btc"_q) {
		pairs.emplace_back(u"BTC"_q, u"BTC"_q);
	} else if (family == u"tron"_q) {
		auto list = named({ u"USDT"_q, u"USDC"_q, u"TRX"_q });
		if (list.isEmpty()) {
			list.push_back(u"USDT"_q);
		}
		for (const auto &coin : list) {
			pairs.emplace_back(coin, u"TRC20"_q);
		}
	} else if (family == u"ton"_q) {
		for (const auto &coin : named({ u"USDT"_q, u"USDC"_q })) {
			pairs.emplace_back(coin, u"TON"_q);
		}
		if (pairs.empty()) {
			pairs.emplace_back(u"USDT"_q, u"TON"_q);
			pairs.emplace_back(u"TON"_q, u"TON"_q);
		}
	} else {
		auto evm = QStringList();
		for (const auto &net : nets) {
			if (net.extra == u"evm"_q) {
				evm.push_back(net.value);
			}
		}
		if (evm.isEmpty()) {
			evm = QStringList{ u"BEP20"_q, u"ERC20"_q };
		}
		auto list = named({ u"USDT"_q, u"USDC"_q });
		if (list.isEmpty()) {
			list.push_back(u"USDT"_q);
		}
		for (const auto &coin : list) {
			for (const auto &network : Unique(evm)) {
				pairs.emplace_back(coin, network);
			}
		}
	}
	auto result = std::vector<Set>();
	for (const auto &[coin, network] : pairs) {
		result.push_back({
			.kind = SetKind::Wallet,
			.coin = coin,
			.network = network,
			.address = wallet.value,
			.memo = memo,
		});
	}
	return result;
}

[[nodiscard]] Parsed ParseBlock(const QString &text, QString &refused) {
	auto warnings = QStringList();
	const auto found = Identifiers(text);
	auto positive = std::vector<Found>();
	for (const auto &entry : found) {
		if (!Negative(text, entry)) {
			positive.push_back(entry);
		}
	}
	const auto known = Banks(text);
	auto all = known;
	for (const auto &generic : GenericBanks(text)) {
		const auto apart = [&](const Found &bank) {
			return generic.end <= bank.start || generic.start >= bank.end;
		};
		if (std::all_of(begin(known), end(known), apart)) {
			all.push_back(generic);
		}
	}
	auto banks = std::vector<Found>();
	for (const auto &bank : all) {
		if (!Negative(text, bank)) {
			banks.push_back(bank);
		}
	}
	const auto exchanges = PositiveWords(text, ExchangeTable(), Kind::Exchange);
	const auto uidContext = !UidKeywords(text).empty() || !exchanges.empty();

	const auto pick = [&](auto &&good) {
		auto result = std::vector<Found>();
		for (const auto &entry : positive) {
			if (good(entry)) {
				result.push_back(entry);
			}
		}
		return UniqueFound(result);
	};
	const auto phones = pick([&](const Found &entry) {
		return entry.kind == Kind::Phone
			&& !(uidContext && Weak(text, entry));
	});
	const auto cards = pick([](const Found &entry) {
		return entry.kind == Kind::Card;
	});
	const auto accounts = pick([](const Found &entry) {
		return entry.kind == Kind::Account || entry.kind == Kind::Iban;
	});
	const auto wallets = pick([](const Found &entry) {
		return entry.kind == Kind::Wallet;
	});
	const auto uids = pick([&](const Found &entry) {
		const auto phoneSpan = [&](const Found &phone) {
			return phone.start == entry.start && phone.end == entry.end;
		};
		return entry.kind == Kind::Uid
			&& uidContext
			&& std::none_of(begin(phones), end(phones), phoneSpan);
	});

	auto groups = std::vector<std::vector<Set>>();
	auto foreign = false;
	const auto lists = std::array<const std::vector<Found>*, 4>{
		&phones,
		&cards,
		&accounts,
		&banks,
	};
	for (const auto list : lists) {
		for (const auto &entry : *list) {
			if (!entry.extra.isEmpty() && entry.extra != u"RU"_q) {
				foreign = true;
			}
		}
	}
	for (const auto &bank : banks) {
		if (Search(uR"re((?i)(?<!\w)bank(?!\w))re"_q, bank.value).hasMatch()) {
			foreign = true;
		}
	}
	const auto fiat = !phones.empty() || !cards.empty() || !accounts.empty();
	if (fiat && foreign) {
		auto idents = phones;
		idents.insert(end(idents), begin(cards), end(cards));
		idents.insert(end(idents), begin(accounts), end(accounts));
		auto set = Foreign(text, banks, idents, refused);
		if (!refused.isEmpty()) {
			return {};
		}
		groups.push_back({ std::move(set) });
	} else if (fiat) {
		auto options = Fiat(
			text,
			phones,
			cards,
			accounts,
			banks,
			found,
			warnings,
			refused);
		if (!refused.isEmpty()) {
			return {};
		}
		groups.push_back(std::move(options));
	}
	if (!uids.empty()) {
		groups.push_back(Uid(uids, exchanges, warnings));
	}
	for (const auto &wallet : wallets) {
		auto options = Wallet(text, wallet, wallets, refused);
		if (!refused.isEmpty()) {
			return {};
		}
		groups.push_back(std::move(options));
	}
	if (groups.empty()) {
		refused = u"Не нашёл реквизиты: ни телефона, ни карты, ни счёта, "
			"ни UID, ни адреса."_q;
		return {};
	}
	auto count = int64(1);
	for (const auto &group : groups) {
		count = std::min(count * int64(group.size()), int64(kMaxChoices + 1));
	}
	if (count > kMaxChoices) {
		refused = u"Слишком много вариантов — не понял, какие реквизиты "
			"нужны."_q;
		return {};
	}
	auto variants = std::vector<std::vector<Set>>{ {} };
	for (const auto &group : groups) {
		auto next = std::vector<std::vector<Set>>();
		for (const auto &prefix : variants) {
			for (const auto &set : group) {
				next.push_back(prefix);
				next.back().push_back(set);
			}
		}
		variants = std::move(next);
	}
	return { std::move(variants), Unique(warnings) };
}

[[nodiscard]] Parsed Parse(const QString &text, QString &refused) {
	auto blocks = QStringList();
	for (const auto &block : Split(uR"re(\n\s*\n)re"_q, text)) {
		if (!Strip(block).isEmpty()) {
			blocks.push_back(block);
		}
	}
	if (blocks.size() > 1) {
		auto parts = std::vector<Parsed>();
		auto broken = false;
		for (const auto &block : blocks) {
			if (Identifiers(block).empty()) {
				continue;
			}
			auto blockRefused = QString();
			auto part = ParseBlock(block, blockRefused);
			if (!blockRefused.isEmpty() || part.variants.size() != 1) {
				broken = true;
				break;
			}
			parts.push_back(std::move(part));
		}
		if (!broken && parts.size() >= 2) {
			auto sets = std::vector<Set>();
			auto warnings = QStringList();
			for (const auto &part : parts) {
				const auto &first = part.variants.front();
				sets.insert(end(sets), begin(first), end(first));
				warnings += part.warnings;
			}
			return { { std::move(sets) }, Unique(warnings) };
		}
	}
	return ParseBlock(text, refused);
}

[[nodiscard]] std::pair<std::vector<std::pair<QString, bool>>, QString> Block(
		const Set &set) {
	using Pieces = std::vector<std::pair<QString, bool>>;
	if (set.kind == SetKind::Uid) {
		return {
			Pieces{ { set.exchange + u" UID: "_q, false }, { set.uid, true } },
			set.exchange + u" UID"_q,
		};
	} else if (set.kind == SetKind::Wallet) {
		const auto label = (set.coin == set.network)
			? set.coin
			: (set.coin + u' ' + set.network);
		auto pieces = Pieces{
			{ label + u": "_q, false },
			{ set.address, true },
		};
		if (!set.memo.isEmpty()) {
			pieces.emplace_back(u"\nMemo: "_q, false);
			pieces.emplace_back(set.memo, true);
		}
		return { pieces, label };
	} else if (set.rawLines) {
		return {
			Pieces{ { set.rawLines->join(u'\n'), true } },
			CountryName(set.country),
		};
	}
	const auto ident = set.ident ? set.ident->value : QString();
	auto lines = QStringList();
	auto title = QString();
	if (set.country == u"RU"_q) {
		for (const auto &line : { ident, set.name, set.bank }) {
			if (!line.isEmpty()) {
				lines.push_back(line);
			}
		}
		auto how = (set.ident && set.ident->kind == Kind::Phone)
			? u"по телефону"_q
			: u"карта"_q;
		for (const auto &account : set.accounts) {
			if (account.kind == Kind::Account) {
				how = u"карта и счёт"_q;
			}
		}
		title = u"РФ, "_q + how;
	} else {
		for (const auto &line : { set.bank, ident, set.name }) {
			if (!line.isEmpty()) {
				lines.push_back(line);
			}
		}
		title = CountryName(set.country);
	}
	auto pieces = Pieces();
	if (!lines.isEmpty()) {
		pieces.emplace_back(lines.join(u"\n "_q), true);
	}
	auto extras = std::vector<std::pair<QString, QString>>();
	for (const auto &account : set.accounts) {
		extras.emplace_back(
			(account.kind == Kind::Iban) ? u"IBAN"_q : u"Счёт"_q,
			account.value);
	}
	if (!set.swift.isEmpty()) {
		extras.emplace_back(u"SWIFT"_q, set.swift);
	}
	for (auto i = 0; i < int(extras.size()); ++i) {
		const auto separator = pieces.empty()
			? QString()
			: (i == 0)
			? u"\n\n"_q
			: u"\n"_q;
		pieces.emplace_back(separator + extras[i].first + u": "_q, false);
		pieces.emplace_back(extras[i].second, true);
	}
	if (set.card) {
		pieces.emplace_back(pieces.empty() ? QString() : u"\n\n"_q, false);
		pieces.emplace_back(set.card->value, true);
	}
	return { pieces, title };
}

[[nodiscard]] Rendered Render(const std::vector<Set> &sets) {
	auto result = Rendered();
	auto titles = QStringList();
	for (auto i = 0; i < int(sets.size()); ++i) {
		if (i) {
			result.text += u"\n\n"_q;
		}
		const auto [pieces, title] = Block(sets[i]);
		for (const auto &[text, code] : pieces) {
			if (code && !text.isEmpty()) {
				result.code.push_back({
					.offset = int(result.text.size()),
					.length = int(text.size()),
				});
			}
			result.text += text;
		}
		titles.push_back(title);
	}
	result.title = titles.join(u" + "_q);
	return result;
}

[[nodiscard]] QString Label(const QString &text) {
	auto lines = QStringList();
	for (const auto &line : SplitLines(text)) {
		const auto stripped = Strip(line);
		if (!stripped.isEmpty()) {
			lines.push_back(stripped);
		}
	}
	return lines.join(u" · "_q);
}

} // namespace

Result Format(const Request &request) {
	if (request.forwarded) {
		return {
			.error = u"Пересланное сообщение не беру. "_q + kHintClient,
		};
	}
	auto refused = QString();
	const auto parsed = Parse(request.text, refused);
	if (!refused.isEmpty()) {
		return {
			.error = refused + u' ' + (request.out ? kHintOwn : kHintClient),
		};
	}
	auto result = Result{
		.edit = request.out,
		.pin = true,
		.warnings = parsed.warnings,
	};
	for (const auto &variant : parsed.variants) {
		auto rendered = Render(variant);
		result.variants.push_back({
			.label = (parsed.variants.size() > 1)
				? Label(rendered.text)
				: QString(),
			.text = std::move(rendered.text),
			.code = std::move(rendered.code),
			.title = std::move(rendered.title),
		});
	}
	return result;
}

} // namespace Telegator::Requisites

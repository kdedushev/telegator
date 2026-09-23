/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QStringList>

#include <vector>

namespace Telegator::Requisites {

struct Request {
	QString text;
	bool out = false;
	bool forwarded = false;
};

struct CodeSpan {
	int offset = 0;
	int length = 0;
};

struct Variant {
	QString label;
	QString text;
	std::vector<CodeSpan> code;
	QString title;
};

struct Result {
	QString error;
	bool edit = false;
	bool pin = false;
	std::vector<Variant> variants;
	QStringList warnings;
};

// Empty error and one variant: ready; several variants: operator chooses.
[[nodiscard]] Result Format(const Request &request);

} // namespace Telegator::Requisites

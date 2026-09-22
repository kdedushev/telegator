/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telegator/telegator_mono_copy.h"

#include "core/click_handler_types.h"
#include "history/history_item.h"
#include "ui/text/text.h"

namespace Telegator {
namespace {

struct CachedLink {
	const Ui::Text::String *text = nullptr;
	QString code;
	ClickHandlerPtr link;
};

[[nodiscard]] bool IsMultilineCode(const QString &code) {
	return code.trimmed().contains(QChar('\n'));
}

[[nodiscard]] bool HasMultilineCode(const TextWithEntities &text) {
	return ranges::any_of(text.entities, [&](const EntityInText &entity) {
		return (entity.type() == EntityType::Code)
			&& IsMultilineCode(
				text.text.mid(entity.offset(), entity.length()));
	});
}

} // namespace

ClickHandlerPtr MultilineCodeLink(
		not_null<const HistoryItem*> item,
		const Ui::Text::String &text,
		QPoint point,
		int width) {
	// A cheap check first: this runs on every mouse move over the text.
	if (!HasMultilineCode(item->originalText())
		&& !HasMultilineCode(item->translatedText())) {
		return nullptr;
	}
	auto request = Ui::Text::StateRequest();
	request.flags = Ui::Text::StateRequest::Flag::LookupSymbol;
	const auto state = text.getState(point, width, request);
	if (!state.uponSymbol) {
		return nullptr;
	}
	const auto symbol = state.symbol;
	const auto shown = text.toTextWithEntities();
	const auto i = ranges::find_if(shown.entities, [&](
			const EntityInText &entity) {
		return (entity.type() == EntityType::Code)
			&& (symbol >= entity.offset())
			&& (symbol < entity.offset() + entity.length());
	});
	if (i == shown.entities.end()) {
		return nullptr;
	}
	const auto code = shown.text.mid(i->offset(), i->length());
	if (!IsMultilineCode(code)) {
		return nullptr;
	}
	static auto cached = CachedLink();
	if (cached.text != &text || cached.code != code) {
		cached = CachedLink{
			.text = &text,
			.code = code,
			.link = std::make_shared<MonospaceClickHandler>(
				code,
				EntityType::Code),
		};
	}
	return cached.link;
}

} // namespace Telegator

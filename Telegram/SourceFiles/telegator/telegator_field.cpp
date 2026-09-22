/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telegator/telegator_field.h"

#include "ui/widgets/fields/input_field.h"

namespace Telegator {

void InsertAtCursor(not_null<Ui::InputField*> field, TextWithTags text) {
	if (text.text.isEmpty()) {
		return;
	}
	if (field->getTextWithTags().text.isEmpty()) {
		field->setTextWithTags(
			std::move(text),
			Ui::InputField::HistoryAction::NewEntry);
		auto cursor = field->textCursor();
		cursor.movePosition(QTextCursor::End);
		field->setTextCursor(cursor);
		return;
	}
	auto cursor = field->textCursor();
	cursor.insertText(text.text);
	field->setTextCursor(cursor);
}

} // namespace Telegator

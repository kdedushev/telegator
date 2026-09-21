/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telegator/telegator_compose_buttons.h"

#include "ui/widgets/buttons.h"
#include "styles/style_chat.h"

namespace Telegator {

ComposeButtons::ComposeButtons(
	not_null<QWidget*> parent,
	rpl::producer<bool> shown,
	Fn<void(const QString &text)> sendText)
: _test(parent, rpl::single(u"TEST"_q), st::historyBotMenuButton) {
	_test->setFullRadius(true);
	_test->setClickedCallback([=] {
		sendText(u"Тестовая заготовка"_q);
	});
	std::move(shown) | rpl::on_next([=](bool shown) {
		_test->setVisible(shown);
	}, _test->lifetime());
}

ComposeButtons::~ComposeButtons() = default;

int ComposeButtons::width() const {
	return _test->isHidden()
		? 0
		: (st::historyBotMenuSkip + _test->width());
}

int ComposeButtons::moveToLeft(int left, int top) {
	if (_test->isHidden()) {
		return left;
	}
	const auto skip = st::historyBotMenuSkip;
	_test->moveToLeft(left + skip, top + skip);
	return left + skip + _test->width();
}

} // namespace Telegator

/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telegator/telegator_compose_buttons.h"

#include "telegator/telegator_panel.h"
#include "ui/widgets/buttons.h"

namespace Telegator {

ComposeButtons::ComposeButtons(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller,
	rpl::producer<bool> shown)
: _panel(MakePanelButton(parent, controller, PanelButtonPlace::Compose)) {
	if (!_panel) {
		return;
	}
	std::move(shown) | rpl::on_next([=](bool shown) {
		_panel->setVisible(shown);
	}, _panel->lifetime());
}

ComposeButtons::~ComposeButtons() = default;

int ComposeButtons::width() const {
	return (!_panel || _panel->isHidden()) ? 0 : _panel->width();
}

int ComposeButtons::moveToLeft(int left, int top) {
	if (!_panel || _panel->isHidden()) {
		return left;
	}
	_panel->moveToLeft(left, top);
	return left + _panel->width();
}

} // namespace Telegator

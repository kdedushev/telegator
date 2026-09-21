/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/unique_qptr.h"

class QJsonDocument;

class HistoryWidget;

namespace Ui {
class RpWidget;
class IconButton;
} // namespace Ui

namespace Webview {
class Window;
} // namespace Webview

namespace Window {
class SessionController;
} // namespace Window

namespace Telegator {

// Button in the chat top bar that shows or hides the side panel.
class PanelToggle final {
public:
	PanelToggle(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller);
	~PanelToggle();

	void setAvailable(bool available);

	// Places the button to the left of right, returns the new right.
	[[nodiscard]] int moveToRight(int right, int top);

private:
	object_ptr<Ui::IconButton> _button = { nullptr };

};

// Web page from the owner's server shown at the right of the open chat.
// The page gets the open chat and may ask to insert text into the field.
class SidePanel final {
public:
	SidePanel(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<HistoryWidget*> history,
		Fn<void()> relayout);
	~SidePanel();

	// Takes place at the right side of the area between left and right,
	// returns the taken width.
	[[nodiscard]] int layout(int left, int top, int right, int bottom);

private:
	void createWebview();
	void handleMessage(const QJsonDocument &message);
	void sendChat();
	[[nodiscard]] bool allowedNavigation(const QString &uri) const;

	const not_null<Window::SessionController*> _controller;
	const not_null<HistoryWidget*> _history;
	const bool _allowed = false;
	base::unique_qptr<Ui::RpWidget> _body;
	std::unique_ptr<Webview::Window> _webview;
	bool _pageReady = false;

};

} // namespace Telegator

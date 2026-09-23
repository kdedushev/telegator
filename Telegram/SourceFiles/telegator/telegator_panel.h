/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/unique_qptr.h"

class QJsonDocument;

class HistoryItem;
class HistoryWidget;

namespace Ui {
class RpWidget;
class RippleButton;
class PopupMenu;
} // namespace Ui

namespace Webview {
class Window;
} // namespace Webview

namespace Window {
class SessionController;
} // namespace Window

namespace Telegator {

enum class PanelButtonPlace {
	TopBar,
	Compose,
};

// Icon button that shows or hides the side panel, looks like the other
// buttons of its place. Null for accounts without the panel.
[[nodiscard]] object_ptr<Ui::RippleButton> MakePanelButton(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller,
	PanelButtonPlace place);

// Puts text into the message field if peer's chat is the open one,
// focuses the field so that Enter sends it.
bool InsertIntoChat(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	TextWithTags text);

// "Реквизиты", then items of Telegator.setMenu() that open the page.
void AddMessageActions(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	HistoryItem *item);

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
	object_ptr<Ui::RippleButton> _button = { nullptr };

};

// The page gets the open chat and may put text into the message field.
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

	[[nodiscard]] not_null<HistoryWidget*> history() const;

	void chooseMenu(QByteArray event);

private:
	void show();
	void createWebview();
	void showNotice(const QString &text);
	void setupResize();
	void handleMessage(const QJsonDocument &message);
	void pageReady();
	void sendChat();
	void sendTheme();
	void sendMenuEvent();
	void eval(const QByteArray &script);
	[[nodiscard]] bool allowedNavigation(const QString &uri) const;

	const not_null<Window::SessionController*> _controller;
	const not_null<HistoryWidget*> _history;
	const Fn<void()> _relayout;
	const bool _allowed = false;
	base::unique_qptr<Ui::RpWidget> _body;
	std::unique_ptr<Webview::Window> _webview;
	std::optional<QByteArray> _menuEvent;
	int _width = 0;
	int _maxWidth = 0;
	bool _created = false;
	bool _pageReady = false;

};

} // namespace Telegator

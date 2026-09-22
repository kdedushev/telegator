/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telegator/telegator_panel.h"

#include "core/file_utilities.h"
#include "data/data_peer.h"
#include "dialogs/dialogs_key.h"
#include "history/history_widget.h"
#include "main/main_session.h"
#include "settings.h"
#include "telegator/telegator_config.h"
#include "telegator/telegator_field.h"
#include "telegator/telegator_quick_replies.h"
#include "ui/effects/ripple_animation.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "webview/webview_data_stream_memory.h"
#include "webview/webview_embed.h"
#include "webview/webview_interface.h"
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>

namespace Telegator {
namespace {

constexpr auto kPanelWidth = 360;
constexpr auto kPanelMinWidth = 240;
constexpr auto kResizeArea = 6;

[[nodiscard]] rpl::variable<bool> &Shown(
		not_null<Window::SessionController*> controller) {
	static auto map = base::flat_map<
		not_null<Window::SessionController*>,
		std::unique_ptr<rpl::variable<bool>>>();
	auto i = map.find(controller);
	if (i == end(map)) {
		i = map.emplace(
			controller,
			std::make_unique<rpl::variable<bool>>(false)).first;
		controller->lifetime().add([=] {
			map.remove(controller);
		});
	}
	return *i->second;
}

// Chat widgets of the windows with the panel, for text insertion.
[[nodiscard]] auto Histories()
-> base::flat_map<
		not_null<Window::SessionController*>,
		not_null<HistoryWidget*>> & {
	static auto result = base::flat_map<
		not_null<Window::SessionController*>,
		not_null<HistoryWidget*>>();
	return result;
}

// Panel width set by dragging its left edge, kept between launches.
[[nodiscard]] QString StatePath() {
	return cWorkingDir() + u"tdata/telegator_state.json"_q;
}

[[nodiscard]] int ReadSavedWidth() {
	auto file = QFile(StatePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return 0;
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	return document.object().value(u"panel_width"_q).toInt();
}

void SaveWidth(int width) {
	auto object = QJsonObject();
	object.insert(u"panel_width"_q, width);
	auto file = QFile(StatePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
	}
}

[[nodiscard]] Webview::StorageId PanelStorageId() {
	const auto path = cWorkingDir() + u"tdata/telegator-webview"_q;
	auto file = QFile(path + u".token"_q);
	auto token = QByteArray();
	if (file.open(QIODevice::ReadOnly)) {
		token = file.readAll().trimmed();
		file.close();
	}
	if (token.isEmpty()) {
		token = QByteArray::fromStdString(Webview::GenerateStorageToken());
		if (file.open(QIODevice::WriteOnly)) {
			file.write(token);
		}
	}
	return { .path = path, .token = token };
}

// Page API:
//   Telegator.chat, .onchat(chat) - the open chat, chat.peer.key tells it;
//   Telegator.quickReplies, .onquickreplies(list) - account quick replies;
//   Telegator.theme, .ontheme(theme) - app theme colors for CSS;
//   Telegator.insertText(text, chatKey) - puts text into the message field
//     only while chat.peer.key == chatKey is the open chat: take the key
//     when the request starts, so that a late answer never goes elsewhere;
//   Telegator.useQuickReply(id) - inserts or sends a quick reply into the
//     chat the page shows now, nothing if another chat was opened since.
[[nodiscard]] QByteArray BridgeScript() {
	return R"JS(
window.Telegator = {
	chat: null,
	onchat: null,
	quickReplies: [],
	onquickreplies: null,
	theme: null,
	ontheme: null,
	send: function (message) {
		if (window.external && window.external.invoke) {
			window.external.invoke(JSON.stringify(message));
		}
	},
	insertText: function (text, chatKey) {
		if (!chatKey) {
			console.warn('Telegator.insertText needs chat.peer.key');
			return;
		}
		this.send({
			event: 'insert_text',
			text: String(text),
			chat: String(chatKey)
		});
	},
	useQuickReply: function (id) {
		var chat = this.chat && this.chat.peer;
		if (!chat) {
			return;
		}
		this.send({
			event: 'use_quick_reply',
			id: String(id),
			chat: String(chat.key)
		});
	},
	_set: function (field, callback, value) {
		this[field] = value;
		if (typeof this[callback] === 'function') {
			this[callback](value);
		}
	}
};
document.addEventListener('DOMContentLoaded', function () {
	window.Telegator.send({ event: 'ready' });
});
)JS";
}

// Shown when telegator.json has no panel url: the quick replies.
[[nodiscard]] QString QuickActionsPage() {
	return uR"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<style>
:root { --bg: #ffffff; --fg: #000000; --sub: #8a8a8a; --over: #f1f1f1; }
body { font: 14px -apple-system, 'Segoe UI', sans-serif; margin: 0;
	padding: 12px; background: var(--bg); color: var(--fg); }
h3 { margin: 4px 0 2px; font-size: 15px; }
#chat { color: var(--sub); font-size: 12px; margin-bottom: 12px; }
.reply { display: block; width: 100%; text-align: left; border: 0;
	border-radius: 8px; padding: 8px 10px; margin: 0 0 6px;
	background: var(--over); color: var(--fg); font: inherit;
	cursor: pointer; }
.reply:active { opacity: .7; }
.name { font-weight: 600; }
.preview { color: var(--sub); font-size: 12px; margin-top: 2px;
	overflow-wrap: anywhere; }
.empty { color: var(--sub); font-size: 13px; line-height: 1.4; }
</style></head>
<body>
<h3>Быстрые действия</h3>
<div id="chat"></div>
<div id="list"></div>
<script>
function render(replies) {
	var list = document.getElementById('list');
	list.textContent = '';
	if (!replies || !replies.length) {
		var empty = document.createElement('div');
		empty.className = 'empty';
		empty.textContent = 'Заготовок пока нет. Добавьте их в телефоне: '
			+ 'Настройки → Telegram для бизнеса → Быстрые ответы.';
		list.appendChild(empty);
		return;
	}
	replies.forEach(function (reply) {
		var button = document.createElement('button');
		button.className = 'reply';
		var name = document.createElement('div');
		name.className = 'name';
		name.textContent = reply.name;
		var preview = document.createElement('div');
		preview.className = 'preview';
		preview.textContent = reply.preview || '…';
		button.appendChild(name);
		button.appendChild(preview);
		button.onclick = function () {
			Telegator.useQuickReply(reply.id);
		};
		list.appendChild(button);
	});
}
Telegator.onquickreplies = render;
Telegator.onchat = function (chat) {
	document.getElementById('chat').textContent = (chat && chat.peer)
		? chat.peer.name
		: 'Чат не выбран';
};
Telegator.ontheme = function (theme) {
	for (var key in theme) {
		document.documentElement.style.setProperty('--' + key, theme[key]);
	}
};
render(Telegator.quickReplies);
</script>
</body></html>)HTML"_q;
}

[[nodiscard]] QString CssColor(const style::color &color) {
	const auto c = color->c;
	return u"rgba(%1,%2,%3,%4)"_q
		.arg(c.red())
		.arg(c.green())
		.arg(c.blue())
		.arg(c.alphaF());
}

[[nodiscard]] QJsonObject ThemeObject() {
	auto result = QJsonObject();
	result.insert(u"bg"_q, CssColor(st::windowBg));
	result.insert(u"fg"_q, CssColor(st::windowFg));
	result.insert(u"sub"_q, CssColor(st::windowSubTextFg));
	result.insert(u"over"_q, CssColor(st::windowBgOver));
	result.insert(u"accent"_q, CssColor(st::windowBgActive));
	result.insert(u"accent-fg"_q, CssColor(st::windowFgActive));
	return result;
}

[[nodiscard]] QJsonObject ChatObject(
		not_null<Window::SessionController*> controller) {
	auto result = QJsonObject();
	result.insert(
		u"account_id"_q,
		QString::number(controller->session().userId().bare));
	const auto peer = controller->activeChatCurrent().peer();
	if (!peer) {
		return result;
	}
	const auto id = peer->id;
	const auto type = peer->isUser()
		? u"user"_q
		: peer->isChat()
		? u"chat"_q
		: u"channel"_q;
	const auto bare = peer->isUser()
		? peerToUser(id).bare
		: peer->isChat()
		? peerToChat(id).bare
		: peerToChannel(id).bare;
	auto chat = QJsonObject();
	chat.insert(u"key"_q, QString::number(id.value));
	chat.insert(u"type"_q, type);
	chat.insert(u"id"_q, QString::number(bare));
	chat.insert(u"name"_q, peer->name());
	chat.insert(u"username"_q, peer->username());
	result.insert(u"peer"_q, chat);
	return result;
}

// Looks like the other buttons of its place.
class PanelButton final : public Ui::RippleButton {
public:
	PanelButton(
		QWidget *parent,
		const style::IconButton &st,
		const style::color &fg,
		const style::color &fgOver)
	: RippleButton(parent, st.ripple)
	, _st(st)
	, _fg(fg)
	, _fgOver(fgOver) {
		resize(st.width, st.height);
		setCursor(style::cur_pointer);
	}

	void setActive(bool active) {
		_active = active;
		update();
	}

private:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		paintRipple(p, _st.rippleAreaPosition);
		const auto &icon = st::menuIconManage;
		const auto position = (_st.iconPosition.x() < 0)
			? QPoint(
				(width() - icon.width()) / 2,
				(height() - icon.height()) / 2)
			: (_st.iconPosition + QPoint(
				(_st.icon.width() - icon.width()) / 2,
				(_st.icon.height() - icon.height()) / 2));
		const auto &color = _active
			? st::windowActiveTextFg
			: isOver()
			? _fgOver
			: _fg;
		icon.paint(p, position, width(), color->c);
	}

	void onStateChanged(State was, StateChangeSource source) override {
		RippleButton::onStateChanged(was, source);
		update();
	}

	QImage prepareRippleMask() const override {
		const auto size = _st.rippleAreaSize;
		return Ui::RippleAnimation::EllipseMask(QSize(size, size));
	}

	QPoint prepareRippleStartPosition() const override {
		return mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition;
	}

	const style::IconButton &_st;
	const style::color &_fg;
	const style::color &_fgOver;
	bool _active = false;

};

} // namespace

object_ptr<Ui::RippleButton> MakePanelButton(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		PanelButtonPlace place) {
	if (!PanelAllowed(&controller->session())) {
		return { nullptr };
	}
	const auto compose = (place == PanelButtonPlace::Compose);
	auto result = object_ptr<PanelButton>(
		parent,
		compose ? st::historyAttach : st::topBarInfo,
		compose ? st::historyComposeIconFg : st::menuIconFg,
		compose ? st::historyComposeIconFgOver : st::menuIconFgOver);
	const auto raw = result.data();
	raw->setClickedCallback([=] {
		auto &shown = Shown(controller);
		shown = !shown.current();
	});
	Shown(controller).value() | rpl::on_next([=](bool shown) {
		raw->setActive(shown);
	}, raw->lifetime());
	return result;
}

bool InsertIntoChat(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		TextWithTags text) {
	const auto i = Histories().find(controller);
	if (i == end(Histories())) {
		return false;
	}
	const auto history = i->second;
	const auto field = history->telegatorField();
	// History widget is hidden while another section (a topic) is shown.
	if (history->isHidden()
		|| history->peer() != peer.get()
		|| field->isHidden()) {
		return false;
	}
	InsertAtCursor(field, std::move(text));
	field->setFocus();
	return true;
}

PanelToggle::PanelToggle(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller)
: _button(MakePanelButton(parent, controller, PanelButtonPlace::TopBar)) {
}

PanelToggle::~PanelToggle() = default;

void PanelToggle::setAvailable(bool available) {
	if (_button) {
		_button->setVisible(available);
	}
}

int PanelToggle::moveToRight(int right, int top) {
	if (!_button || _button->isHidden()) {
		return right;
	}
	_button->moveToRight(right, top);
	return right + _button->width();
}

SidePanel::SidePanel(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller,
	not_null<HistoryWidget*> history,
	Fn<void()> relayout)
: _controller(controller)
, _history(history)
, _relayout(std::move(relayout))
, _allowed(PanelAllowed(&controller->session())) {
	if (!_allowed) {
		return;
	}
	Histories().remove(controller);
	Histories().emplace(controller, history);
	_quickReplies = std::make_unique<QuickReplies>(&controller->session());
	_width = ReadSavedWidth();
	if (_width <= 0) {
		_width = style::ConvertScale(kPanelWidth);
	}

	_body = base::make_unique_q<Ui::RpWidget>(parent);
	_body->hide();
	_body->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(_body.get());
		p.fillRect(_body->rect(), st::windowBg);
		p.fillRect(0, 0, st::lineWidth, _body->height(), st::shadowFg);
	}, _body->lifetime());
	setupResize();

	Shown(controller).changes() | rpl::on_next([=](bool shown) {
		if (shown) {
			// The panel takes the place of the profile column.
			controller->closeThirdSection();
			if (!_webview) {
				createWebview();
			}
		}
		_relayout();
	}, _body->lifetime());

	controller->activeChatValue(
	) | rpl::on_next([=] {
		sendChat();
	}, _body->lifetime());

	_quickReplies->changes() | rpl::on_next([=] {
		sendQuickReplies();
	}, _body->lifetime());

	style::PaletteChanged(
	) | rpl::on_next([=] {
		sendTheme();
	}, _body->lifetime());
}

SidePanel::~SidePanel() {
	const auto i = Histories().find(_controller);
	if (i != end(Histories()) && i->second == _history) {
		Histories().erase(i);
	}
}

int SidePanel::layout(int left, int top, int right, int bottom) {
	if (!_allowed) {
		return 0;
	}
	const auto area = QRect(left, top, right - left, bottom - top);
	_maxWidth = area.width() - st::columnMinimalWidthMain;
	const auto width = std::min(_width, _maxWidth);
	if (!Shown(_controller).current()
		|| width < style::ConvertScale(kPanelMinWidth)) {
		_body->hide();
		return 0;
	}
	_body->setGeometry(
		area.x() + area.width() - width,
		area.y(),
		width,
		area.height());
	if (_webview && _webview->widget()) {
		// The native page must not cover the resize area.
		const auto skip = style::ConvertScale(kResizeArea);
		_webview->widget()->setGeometry(
			skip,
			0,
			width - skip,
			area.height());
	}
	_body->show();
	return width;
}

void SidePanel::setupResize() {
	const auto handle = Ui::CreateChild<Ui::RpWidget>(_body.get());
	handle->setCursor(style::cur_sizehor);
	_body->sizeValue() | rpl::on_next([=](QSize size) {
		handle->setGeometry(
			0,
			0,
			style::ConvertScale(kResizeArea),
			size.height());
	}, handle->lifetime());

	struct Drag {
		std::optional<int> fromX;
		int fromWidth = 0;
	};
	const auto drag = handle->lifetime().make_state<Drag>();
	handle->events() | rpl::on_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type != QEvent::MouseButtonPress
			&& type != QEvent::MouseMove
			&& type != QEvent::MouseButtonRelease) {
			return;
		}
		const auto x = static_cast<QMouseEvent*>(
			e.get())->globalPosition().toPoint().x();
		if (type == QEvent::MouseButtonPress) {
			drag->fromX = x;
			drag->fromWidth = _body->width();
		} else if (!drag->fromX) {
			return;
		} else if (type == QEvent::MouseMove) {
			const auto min = style::ConvertScale(kPanelMinWidth);
			_width = std::clamp(
				drag->fromWidth + (*drag->fromX - x),
				min,
				std::max(min, _maxWidth));
			_relayout();
		} else {
			drag->fromX = std::nullopt;
			SaveWidth(_width);
		}
	}, handle->lifetime());
	handle->show();
}

void SidePanel::createWebview() {
	_webview = std::make_unique<Webview::Window>(
		_body.get(),
		Webview::WindowConfig{
			.opaqueBg = st::windowBg->c,
			.storageId = PanelStorageId(),
		});
	const auto raw = _webview.get();
	if (!raw->widget()) {
		LOG(("Telegator: panel webview is not available."));
		_webview = nullptr;
		const auto label = Ui::CreateChild<Ui::FlatLabel>(
			_body.get(),
			u"Built-in browser is not available."_q);
		_body->sizeValue() | rpl::on_next([=](QSize size) {
			const auto skip = style::ConvertScale(16);
			label->resizeToWidth(size.width() - 2 * skip);
			label->moveToLeft(skip, skip);
		}, label->lifetime());
		label->show();
		return;
	}
	raw->widget()->show();
	raw->setNavigationStartHandler([=](const QString &uri, bool newWindow) {
		if (newWindow) {
			File::OpenUrl(uri);
			return false;
		}
		return allowedNavigation(uri);
	});
	raw->setNavigationDoneHandler([=](bool success) {
		LOG(("Telegator: panel page %1."
			).arg(success ? "loaded" : "failed to load"));
		if (success) {
			_pageReady = true;
			sendTheme();
			sendChat();
			sendQuickReplies();
		}
	});
	raw->setMessageHandler([=](const QJsonDocument &message) {
		crl::on_main(_body.get(), [=] {
			handleMessage(message);
		});
	});
	raw->setDataRequestHandler([=](Webview::DataRequest request) {
		if (!request.id.starts_with("telegator/quick.html")) {
			return Webview::DataResult::Failed;
		}
		request.done({
			.stream = std::make_unique<Webview::DataStreamFromMemory>(
				QuickActionsPage().toUtf8(),
				"text/html; charset=utf-8"),
		});
		return Webview::DataResult::Done;
	});
	raw->init(BridgeScript());
	if (const auto url = Panel().url; !url.isEmpty()) {
		raw->navigate(url);
	} else {
		raw->navigateToData(u"telegator/quick.html"_q);
	}
}

bool SidePanel::allowedNavigation(const QString &uri) const {
	const auto url = Panel().url;
	if (url.isEmpty()) {
		// Built-in page is served by lib_webview's data domain (mac, windows).
		return uri.startsWith(u"desktopappresource://"_q)
			|| uri.startsWith(u"http://desktop-app-resource/"_q);
	}
	const auto target = QUrl(uri);
	const auto base = QUrl(url);
	return (target.scheme() == base.scheme())
		&& (target.host() == base.host())
		&& (target.port() == base.port());
}

void SidePanel::handleMessage(const QJsonDocument &message) {
	const auto object = message.object();
	const auto event = object.value(u"event"_q).toString();
	if (event == u"ready"_q) {
		_pageReady = true;
		sendTheme();
		sendChat();
		sendQuickReplies();
	} else if (event == u"insert_text"_q || event == u"use_quick_reply"_q) {
		// The chat the page meant must still be the open one.
		const auto chat = object.value(u"chat"_q).toString();
		const auto peer = _controller->activeChatCurrent().peer();
		if (!peer) {
			return;
		} else if (chat != QString::number(peer->id.value)) {
			_controller->showToast(u"Не выполнено: открыт другой чат."_q);
			return;
		} else if (event == u"use_quick_reply"_q) {
			const auto id = object.value(u"id"_q).toString().toInt();
			if (id) {
				_quickReplies->use(id, _controller, peer);
			}
			return;
		}
		const auto text = object.value(u"text"_q).toString();
		if (!text.isEmpty()) {
			InsertIntoChat(_controller, peer, { text, {} });
		}
	}
}

void SidePanel::eval(const QByteArray &script) {
	if (_webview && _pageReady) {
		_webview->eval(script);
	}
}

void SidePanel::sendChat() {
	const auto json = QJsonDocument(ChatObject(_controller)).toJson(
		QJsonDocument::Compact);
	eval("window.Telegator && window.Telegator._set("
		"'chat', 'onchat', " + json + ");");
}

void SidePanel::sendQuickReplies() {
	auto list = QJsonArray();
	for (const auto &reply : _quickReplies->list()) {
		auto object = QJsonObject();
		object.insert(u"id"_q, QString::number(reply.id));
		object.insert(u"name"_q, reply.name);
		object.insert(u"count"_q, reply.count);
		object.insert(u"preview"_q, reply.preview);
		list.push_back(object);
	}
	const auto json = QJsonDocument(list).toJson(QJsonDocument::Compact);
	eval("window.Telegator && window.Telegator._set("
		"'quickReplies', 'onquickreplies', " + json + ");");
}

void SidePanel::sendTheme() {
	const auto json = QJsonDocument(ThemeObject()).toJson(
		QJsonDocument::Compact);
	eval("window.Telegator && window.Telegator._set("
		"'theme', 'ontheme', " + json + ");");
}

} // namespace Telegator

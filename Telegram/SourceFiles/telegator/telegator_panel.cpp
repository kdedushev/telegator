/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telegator/telegator_panel.h"

#include "core/file_utilities.h"
#include "data/data_peer.h"
#include "dialogs/dialogs_key.h"
#include "main/main_session.h"
#include "settings.h"
#include "telegator/telegator_config.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "webview/webview_embed.h"
#include "webview/webview_interface.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>

namespace Telegator {
namespace {

constexpr auto kPanelWidth = 360;
constexpr auto kPanelMinWidth = 240;

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

// Page API: window.Telegator.chat, .onchat = fn(chat), .insertText(text).
[[nodiscard]] QByteArray BridgeScript() {
	return R"JS(
window.Telegator = {
	chat: null,
	onchat: null,
	send: function (message) {
		if (window.external && window.external.invoke) {
			window.external.invoke(JSON.stringify(message));
		}
	},
	insertText: function (text) {
		this.send({ event: 'insert_text', text: String(text) });
	},
	_setChat: function (chat) {
		this.chat = chat;
		if (typeof this.onchat === 'function') {
			this.onchat(chat);
		}
	}
};
document.addEventListener('DOMContentLoaded', function () {
	window.Telegator.send({ event: 'ready' });
});
)JS";
}

// Shown when telegator.json has no panel url.
[[nodiscard]] QString DemoPage() {
	return uR"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<style>
body { font-family: -apple-system, 'Segoe UI', sans-serif; margin: 16px; }
pre { white-space: pre-wrap; background: rgba(127,127,127,.12);
	padding: 8px; border-radius: 6px; }
button { padding: 8px 12px; border-radius: 6px; }
</style></head>
<body>
<h3>Telegator</h3>
<p>Demo panel. Set "panel.url" in telegator.json to load your page.</p>
<p>Open chat:</p>
<pre id="chat">none</pre>
<button onclick="Telegator.insertText('Test template')">Insert template</button>
<script>
Telegator.onchat = function (chat) {
	document.getElementById('chat').textContent =
		JSON.stringify(chat, null, 2);
};
if (Telegator.chat) Telegator.onchat(Telegator.chat);
</script>
</body></html>)HTML"_q;
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
	chat.insert(u"type"_q, type);
	chat.insert(u"id"_q, QString::number(bare));
	chat.insert(u"name"_q, peer->name());
	chat.insert(u"username"_q, peer->username());
	result.insert(u"peer"_q, chat);
	return result;
}

} // namespace

PanelToggle::PanelToggle(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller) {
	if (!PanelAllowed(&controller->session())) {
		return;
	}
	_button.create(parent, st::topBarInfo);
	_button->setIconOverride(&st::menuIconManage, &st::menuIconManage);
	_button->setClickedCallback([=] {
		auto &shown = Shown(controller);
		shown = !shown.current();
	});
	Shown(controller).value() | rpl::on_next([=](bool shown) {
		_button->setForceRippled(shown);
	}, _button->lifetime());
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
	Fn<void(const QString &text)> insertText,
	Fn<void()> relayout)
: _controller(controller)
, _insertText(std::move(insertText))
, _allowed(PanelAllowed(&controller->session())) {
	if (!_allowed) {
		return;
	}
	_body = base::make_unique_q<Ui::RpWidget>(parent);
	_body->hide();
	_body->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(_body.get());
		p.fillRect(_body->rect(), st::windowBg);
		p.fillRect(0, 0, st::lineWidth, _body->height(), st::shadowFg);
	}, _body->lifetime());

	Shown(controller).changes() | rpl::on_next([=](bool shown) {
		if (shown && !_webview) {
			createWebview();
		}
		relayout();
	}, _body->lifetime());

	controller->activeChatValue(
	) | rpl::on_next([=] {
		sendChat();
	}, _body->lifetime());
}

SidePanel::~SidePanel() = default;

int SidePanel::layout(QRect area) {
	if (!_allowed) {
		return 0;
	}
	const auto width = std::min(
		style::ConvertScale(kPanelWidth),
		area.width() - st::columnMinimalWidthMain);
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
		_webview->widget()->setGeometry(
			st::lineWidth,
			0,
			width - st::lineWidth,
			area.height());
	}
	_body->show();
	return width;
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
		if (success) {
			_pageReady = true;
			sendChat();
		}
	});
	raw->setMessageHandler([=](const QJsonDocument &message) {
		crl::on_main(_body.get(), [=] {
			handleMessage(message);
		});
	});
	raw->init(BridgeScript());
	if (const auto url = Panel().url; !url.isEmpty()) {
		raw->navigate(url);
	} else {
		raw->loadHtml(DemoPage(), u"about:blank"_q);
	}
}

bool SidePanel::allowedNavigation(const QString &uri) const {
	const auto url = Panel().url;
	if (url.isEmpty()) {
		return uri.startsWith(u"about:"_q);
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
		sendChat();
	} else if (event == u"insert_text"_q) {
		const auto text = object.value(u"text"_q).toString();
		if (!text.isEmpty()) {
			_insertText(text);
		}
	}
}

void SidePanel::sendChat() {
	if (!_webview || !_pageReady) {
		return;
	}
	const auto json = QJsonDocument(ChatObject(_controller)).toJson(
		QJsonDocument::Compact);
	_webview->eval(
		"window.Telegator && window.Telegator._setChat(" + json + ");");
}

} // namespace Telegator

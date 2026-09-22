/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Telegator {

// Local settings of this installation, never stored in the repository.
// Read once from <working dir>/telegator.json:
// {
//   "panel": {
//     "accounts": [ 123456789 ],        // Telegram user ids with the panel
//     "url": "https://example.com/panel" // empty or missing: built-in page
//   },
//   "requisites": {
//     "url": "https://example.com/requisites" // script for the message menu
//   }
// }
struct PanelConfig {
	std::vector<uint64> accounts;
	QString url;
};

struct RequisitesConfig {
	QString url;
};

[[nodiscard]] const PanelConfig &Panel();
[[nodiscard]] const RequisitesConfig &Requisites();
[[nodiscard]] bool PanelAllowed(not_null<Main::Session*> session);

} // namespace Telegator

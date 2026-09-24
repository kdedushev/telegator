/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace MTP {
class Instance;
namespace details {
class SerializedRequest;
} // namespace details
} // namespace MTP

namespace Telegator {

// WHY: every request to Telegram passes here, so a press or a send is
// written to the journal whatever part of the app made it.
void JournalRequest(
	not_null<MTP::Instance*> instance,
	const MTP::details::SerializedRequest &request);

} // namespace Telegator

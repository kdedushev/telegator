/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Telegator {

// WHY: Telegram opens documents in the app the system picks for them
// (a code editor for .txt and .csv, for example); Telegram for macOS
// from the App Store shows them in Quick Look, with "Open with" there.
// Returns false for programs and where there is no preview (Windows
// yet), the caller launches the file as before.
bool PreviewFile(const QString &path);

} // namespace Telegator

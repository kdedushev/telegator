/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telegator/telegator_file_preview.h"

namespace Telegator {

#ifndef Q_OS_MAC
bool PreviewFile(const QString &path) {
	return false;
}
#endif // !Q_OS_MAC

} // namespace Telegator

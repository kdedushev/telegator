#!/bin/bash
# Telegator on the owner's Mac: exactly one installed copy, /Applications/Telegator.app.
#
#   tools/telegator/install_mac.sh            # move out/Release/Telegator.app into /Applications
#   tools/telegator/install_mac.sh --restore  # after an agent ran a Debug build: back to the installed one
#
# The build is moved, not copied, and the agents' Debug bundle is removed
# (the next Debug build recreates it in minutes), so Spotlight and "Open With"
# show one Telegator. Build bundles under the home folder are removed
# from Launch Services. Login data lives in ~/Library/Application Support/Telegator
# and is shared by every build, so replacing the app keeps the session.
set -euo pipefail

DEV="${TELEGATOR_DEV:-$HOME/Projects/telegator-wt/dev}"
SRC="$DEV/out/Release/Telegator.app"
DEST=/Applications/Telegator.app
LSREGISTER=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister

quit_all() {
	local id
	for id in io.github.kdedushev.telegator io.github.kdedushev.telegatorDebug; do
		osascript -e "tell application id \"$id\" to quit" >/dev/null 2>&1 || true
	done
	for _ in $(seq 1 30); do
		pgrep -f 'Telegator\.app/Contents/MacOS/Telegator' >/dev/null || return 0
		sleep 1
	done
	echo "Telegator did not quit in 30 s — close it and run again" >&2
	exit 1
}

forget_builds() {
	# Build and stale bundles (under $HOME or already deleted) leave Launch Services.
	"$LSREGISTER" -dump 2>/dev/null \
		| sed -n 's/^path: *\(.*\/Tele[a-z]*\.app\) (0x[0-9a-f]*)$/\1/p' \
		| sort -u \
		| while IFS= read -r path; do
			case "$path" in
				"$HOME"/*|*/.Trash/*) "$LSREGISTER" -u "$path" >/dev/null 2>&1 || true ;;
				*) [ -e "$path" ] || "$LSREGISTER" -u "$path" >/dev/null 2>&1 || true ;;
			esac
		done
}

drop_debug() {
	rm -rf "$DEV/out/Debug/Telegator.app"
}

if [ "${1:-}" = "--restore" ]; then
	quit_all
	drop_debug
	forget_builds
	open "$DEST"
	exit 0
fi

[ -d "$SRC" ] || { echo "no $SRC — build Release first (TELEGATOR.md)" >&2; exit 1; }
quit_all
rm -rf "$DEST.old"
[ -d "$DEST" ] && mv "$DEST" "$DEST.old"
mv "$SRC" "$DEST"
rm -rf "$DEST.old"
drop_debug
forget_builds
"$LSREGISTER" -f "$DEST"
open "$DEST"
echo "installed: $DEST"

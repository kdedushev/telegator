#!/bin/bash
# Установщик Telegator для macOS: .dmg с окном «перетащите в Программы».
#
#   tools/telegator/package_mac.sh [Telegator.app]
#
# Берёт Release-сборку (по умолчанию ~/Projects/telegator-wt/dev/out/Release),
# убирает отладочные символы, кладёт в пакет настройки панели и подписывает.
# Настройки панели — ~/Projects/telegator-wt/package.json (вне git: адрес
# сервера и аккаунты — бизнес-специфика), только раздел "panel"; ключ журнала
# у каждого компьютера свой и в пакет не попадает. Подпись — ad hoc, либо
# сертификат Developer ID из TELEGATOR_SIGN_IDENTITY. Результат —
# ~/Projects/telegator-wt/dist/Telegator-<версия>-<коммит>.dmg.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=~/Projects/telegator-wt
app=${1:-$root/dev/out/Release/Telegator.app}
config=$root/package.json
venv=$root/.package-venv
dist=$root/dist

[ -d "$app" ] || { echo "Нет сборки: $app" >&2; exit 1; }
[ -f "$config" ] || { echo "Нет настроек панели: $config" >&2; exit 1; }
/usr/bin/python3 - "$config" <<'PY'
import json, sys
data = json.load(open(sys.argv[1]))
if set(data) != {"panel"} or not data["panel"].get("url") or not data["panel"].get("accounts"):
    sys.exit("package.json: нужен только раздел panel с url и accounts")
PY

if [ ! -x "$venv/bin/dmgbuild" ]; then
	/opt/homebrew/bin/python3.12 -m venv "$venv"
	"$venv/bin/pip" -q install dmgbuild Pillow
fi

version=$(plutil -extract CFBundleShortVersionString raw "$app/Contents/Info.plist")
commit=$(git -C "$(dirname "$app")/../.." rev-parse --short HEAD 2>/dev/null || echo local)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

ditto "$app" "$work/Telegator.app"
xattr -cr "$work/Telegator.app"
strip -S -x "$work/Telegator.app/Contents/MacOS/Telegator"
cp "$config" "$work/Telegator.app/Contents/Resources/telegator.json"
if [ -n "${TELEGATOR_SIGN_IDENTITY:-}" ]; then
	codesign --force --deep --options runtime --timestamp -s "$TELEGATOR_SIGN_IDENTITY" "$work/Telegator.app"
else
	codesign --force --deep -s - "$work/Telegator.app"
fi
codesign --verify --deep --strict "$work/Telegator.app"

"$venv/bin/python" "$here/dmg/background.py" "$work/background.png"
mkdir -p "$dist"
out="$dist/Telegator-$version-$commit.dmg"
rm -f "$out"
"$venv/bin/dmgbuild" -s "$here/dmg/settings.py" \
	-D app="$work/Telegator.app" -D background="$work/background.png" \
	"Telegator" "$out" >/dev/null
echo "$out ($(du -h "$out" | cut -f1 | tr -d ' '))"

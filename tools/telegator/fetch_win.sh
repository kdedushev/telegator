#!/bin/bash
# Установщик Telegator для Windows из облачной сборки.
#
#   tools/telegator/fetch_win.sh [номер запуска]
#
# Берёт артефакт Telegator-windows последнего удачного запуска workflow
# "Telegator Windows" (или названного), расшифровывает ключом
# ~/Projects/telegator-wt/artifact.key (тот же — секрет TELEGATOR_ARTIFACT_KEY)
# и кладёт ~/Projects/telegator-wt/dist/Telegator-setup.exe.
set -euo pipefail

repo=kdedushev/telegator
root=~/Projects/telegator-wt
key=$root/artifact.key
run=${1:-$(gh run list --repo $repo --workflow telegator-win.yml --status success --limit 1 --json databaseId --jq '.[0].databaseId')}
[ -n "$run" ] || { echo "Нет удачной сборки Windows" >&2; exit 1; }
[ -f "$key" ] || { echo "Нет ключа $key" >&2; exit 1; }
command -v 7zz >/dev/null || brew install -q sevenzip

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
gh run download "$run" --repo $repo --name Telegator-windows --dir "$work"
7zz x -y "-p$(tr -d '\n' < "$key")" "-o$work" "$work/Telegator-windows.7z" >/dev/null
mkdir -p "$root/dist"
mv -f "$work/Telegator-setup.exe" "$root/dist/Telegator-setup.exe"
echo "$root/dist/Telegator-setup.exe ($(du -h "$root/dist/Telegator-setup.exe" | cut -f1 | tr -d ' '), сборка $run)"

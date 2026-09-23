# Telegator — гид для агента

Прочитай целиком перед любой работой. Правила самого Telegram Desktop — в
`AGENTS.md`; этот файл их дополняет и при конфликте имеет приоритет.

## Что это

Форк `telegramdesktop/tdesktop` для внутреннего использования (macOS + Windows):
кнопки действий в окне чата и несколько фич, которых нет в официальном клиенте.
Владелец — не программист: пиши ему коротко, по-русски, технические решения
принимай сам. Цели, решения и очередь фич — `docs/telegator/ROADMAP.md`.

## Где что

| Что | Где |
|---|---|
| Локальный репозиторий | `~/Projects/telegator` |
| GitHub (публичный) | `kdedushev/telegator`, ветка `main` |
| Оригинал Telegram | remote `upstream`, ветка `dev`, теги `vX.Y.Z` |
| Наш код | `Telegram/SourceFiles/telegator/` |
| Скачанные сборки | `builds/` — локально, в git не попадает |

## Главное правило: модульность

Каждая правка в файле Telegram — это будущий конфликт при обновлении.

1. Логика, строки, стили, состояние — только в `Telegram/SourceFiles/telegator/`.
2. В файлах Telegram — минимальная точка вызова (1–3 строки), помеченная
   комментарием `// Telegator`. Найти все: `grep -rn "// Telegator" Telegram/SourceFiles`.
3. Новые исходники регистрируются в своём `Telegram/cmake/telegator.cmake`,
   а не в общем списке `Telegram/CMakeLists.txt`.
4. Центральные точки лучше разбросанных: если поведение проходит через одну
   функцию Telegram (например, все исходящие звонки — `Calls::Instance::startOutgoingCall`),
   вставляйся туда, а не в каждую кнопку.
5. Фичу из чужого форка (AyuGram и др.) не копируй целиком: бери нужное,
   отвязывай от их настроек, указывай источник в шапке файла (лицензия GPLv3).

## Сборка

Локально на Mac владельца (Xcode 27). Облако — запасной путь.

**Библиотеки** — один раз и после того, как Telegram изменит
`Telegram/build/prepare/prepare.py` (~1 час, 27 ГБ):

```bash
cd ~/Projects/telegator-wt/dev && ./Telegram/build/prepare/mac.sh skip-release silent
```

- Скрипт кладёт `Libraries/` и `ThirdParty/` в папку над своей копией
  репозитория, то есть в `~/Projects/telegator-wt/`, общие для всех worktree.
  Не запускай его из `~/Projects/telegator`: библиотеки лягут в `~/Projects`.
- Xcode 27 собирает только под macOS 12.0 и новее: в `prepare.py` версия
  поднята до 12.0 (метка `# Telegator`), клиенту — `CMAKE_OSX_DEPLOYMENT_TARGET`.
- Библиотеки собраны в Debug (`skip-release`), поэтому и клиент Debug. Для
  раздачи сотрудникам — пересобрать библиотеки без `skip-release`.

**Клиент** (первый раз ~40 минут, дальше — только изменённое):

```bash
cd ~/Projects/telegator-wt/dev/Telegram
set -a; . ~/Projects/telegator-wt/telegram_api.env; set +a
./configure.sh -D CMAKE_CONFIGURATION_TYPES=Debug -D CMAKE_COMPILE_WARNING_AS_ERROR=OFF -D CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO -D DESKTOP_APP_DISABLE_AUTOUPDATE=ON -D DESKTOP_APP_DISABLE_CRASH_REPORTS=ON -D CMAKE_OSX_DEPLOYMENT_TARGET=12.0 -D CMAKE_CXX_FLAGS=-DMETA_NO_STD_FORWARD_DECLARATIONS -D TDESKTOP_API_ID="$TDESKTOP_API_ID" -D TDESKTOP_API_HASH="$TDESKTOP_API_HASH"
nice -n 10 cmake --build ../out --config Debug --parallel 4
```

- `--parallel 4` и `nice`: на Mac владельца 16 ГБ памяти, без ограничения
  Xcode запускает ~20 компиляторов, Mac уходит в подкачку и зависает.
- Результат — `out/Debug/Telegator.app` внутри worktree. Configure нужен один
  раз на worktree; дальше только `cmake --build`.
- `META_NO_STD_FORWARD_DECLARATIONS` — штатный выключатель range-v3: её
  предварительные объявления `std::` не собираются с libc++ из Xcode 27.
- Ключи Telegram API — `~/Projects/telegator-wt/telegram_api.env` (вне git,
  права 600), строки `TDESKTOP_API_ID=` и `TDESKTOP_API_HASH=`. Те же ключи —
  в секретах GitHub. Значения не печатать: вывод configure их содержит.

**Облако**: GitHub Actions → **Telegator macOS**, ~85 минут, артефакт
`Telegator-macos` (zip с `.app`).

```bash
gh workflow run "Telegator macOS" --repo kdedushev/telegator --ref main -f only_cache=false
```

- `only_cache=true` — только собрать библиотеки в кеш. Кеш привязан к хэшу
  `prepare.py`, поэтому после его изменения первая сборка — с библиотеками.
- Ключи — секреты репозитория `TDESKTOP_API_ID` / `TDESKTOP_API_HASH`; без них
  подставляются тестовые ключи Telegram (вход сильно ограничен).
- Все унаследованные workflow Telegram отключены через `gh workflow disable`.
  Не включай их и не удаляй их файлы (удаление = конфликты при обновлении).

## Запуск собранного клиента на Mac владельца

Клиент называется Telegator (bundle id `io.github.kdedushev.telegator`) и
ставится рядом с Telegram Desktop, не трогая его сессию. Все сборки на Mac —
локальные, облачные, отладочные — берут одну папку данных
`~/Library/Application Support/Telegator/`: там вход в аккаунт и
`telegator.json` (формат — `telegator/telegator_config.h`). Владелец входит
один раз, новая сборка и запуск двойным щелчком вход не теряют. Отладочная
сборка Telegram хранила бы данные рядом с приложением — метка `// Telegator`
в `logs.cpp`.

```bash
open -n ~/Projects/telegator-wt/dev/out/Debug/Telegator.app
```

- Второй Telegator с той же папкой не запускается, а открывает окно первого:
  чтобы проверить новую сборку, закрой работающую.
- `-workdir <папка>` — только для опыта на отдельном аккаунте: там свой вход,
  перед удалением папки — выйти из аккаунта.

Для ручной проверки сообщений — чат «Избранное», не живые собеседники.

## Обновление с Telegram

```bash
git fetch upstream --tags
git rebase <новый тег или upstream/dev>
```

Конфликты — только в местах `// Telegator`. После rebase — сборка и проверка
в клиенте. `main` на GitHub не переписывать без явного согласия владельца.

## Коммиты

По правилам `AGENTS.md`: одна строка ~50–60 символов, без `Co-Authored-By`
и любых подписей инструментов; префикс `[ai] ` — только для коммитов, целиком
про агентскую документацию. Добавлять в git только конкретные файлы.

## Нельзя

- Коммитить api_id/api_hash и любые секреты.
- Класть в репозиторий бизнес-специфику владельца: адреса серверов, названия
  действий, ключи. Кнопки — универсальный механизм; конкретика — в локальном
  конфиге вне репозитория.
- Сохранять контент с таймером самоуничтожения (п. 1.4 условий Telegram API
  прямо это запрещает — даже если так делает чужой форк).
- Выполнять рабочие действия в клиенте: кнопка сообщает серверу владельца
  «нажали X в чате Y», решает и исполняет сервер.

Исключение из двух правил выше — «Реквизиты» (владелец 23.09.2026): правила
оформления, справочник банков, отправка и закреп — в самом клиенте.

## «Реквизиты»

Пункт меню сообщения — `telegator_requisites.*`, оформление —
`telegator_requisites_format.*`: перенос модуля админки
`tokenator-admin/src/apps/requisites` (там правила владельца и их тесты).
Поменять правила: сначала модуль и его тесты, затем эталоны, затем перенос
в C++, пока проверка не станет зелёной:

```bash
cd ~/Projects/tokenator/tokenator-admin
PYTHONPATH=src .venv/bin/python ~/.tokenator-konveyer/rekvizity-telegator/export_vectors.py
cd ~/Projects/telegator-wt/dev/Telegram
nice -n 10 cmake --build ../out --config Debug --target test_telegator_requisites
../out/Debug/test_telegator_requisites ~/.tokenator-konveyer/rekvizity-telegator/vectors.json
```

- Эталоны (`vectors.json`, «вход → ответ модуля», сравнение байт в байт,
  `entities` в единицах UTF-16) лежат вне репозитория: он публичный, а в
  тестах модуля — примеры владельца.
- Встроенные проверки теста — на выдуманных номерах: цифры не искажаются,
  пересланное — отказ, своё — правка, два телефона — выбор.
- Регулярные выражения модуля перенесены дословно; `\w \d \s \b` в
  `PyPattern()` переводятся в классы Python (сверено по всем символам
  Unicode 15.1, как у Python 3.13 модуля).

## Гигиена — чтобы не копился мусор

- **Сборка — в постоянном worktree `~/Projects/telegator-wt/dev`**: в нём `out/`
  (полная сборка ~30 минут, дальше минуты). Каждая задача — своя ветка,
  переключаемая в `dev`; отдельный worktree — только для параллельной задачи
  в другом чате, со своей полной сборкой. `git worktree move` с подмодулями
  не работает. После слияния в `main` — удалить ветку локально и на GitHub.
- **`builds/` — только последняя сборка.** Перед удалением старой — закрыть
  клиент. Если в папке сборки есть свои данные (`tdata/` или `workdir/`,
  от запусков до общей папки), сначала выйти в ней из аккаунта (иначе на
  сервере Telegram остаётся активная сессия), затем удалить папку целиком.
- **Временные клоны** чужих форков и логи — в `/tmp`, удалить в конце задачи.
- **Документы** — только `TELEGATOR.md` и `docs/telegator/ROADMAP.md`. Новых
  планов не заводить; сделанное переносить в «Сделано» ROADMAP, пометки
  `TEST-BUTTON-POC`/`// Telegator` не оставлять мёртвыми.
- **GitHub чистит себя сам:** артефакты сборок живут 14 дней; кеш библиотек
  удаляется, если 7 дней не было сборок — тогда сначала прогон `only_cache=true`
  (~75 мин), потом обычная сборка.

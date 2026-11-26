# Процесс релиза RS918 N96K-Pro

Документ описывает, как из проверенного дерева получить релиз **v1.x**: версии, обязательные проверки,
сборку, упаковку артефактов в `release/vX`, контрольные суммы, аннотированный тег и публикацию.
Сборка — [docs/BUILD.md](BUILD.md), безопасность — [docs/SAFETY.md](SAFETY.md),
прошивка загрузчика — [docs/BOOTLOADER.md](BOOTLOADER.md), защита по КСВ — [docs/VSWR.md](VSWR.md),
история — [docs/CHANGELOG.md](CHANGELOG.md).

> **Текущее состояние (v1.2):** тег `v1.2` стоит на `15af8d1f`; артефакты — в `release/v1.2`
> (14 файлов, 13 контрольных сумм проверены); публичного remote у продукта **нет** (в репозитории
> `origin` указывает на upstream `df8oe/UHSDR`); `docker build` и GitHub Actions не запускались;
> аппаратных испытаний не было — релиз имеет статус «собран и проверен статически», но не «проверен на
> железе».

## 1. Версии и правила

| Сущность | Правило |
|---|---|
| Тег этапа | `v1.x` — по одному тегу на завершённый этап: `v1.0` (P0), `v1.1` (P1/P2), `v1.2` (этап A). Следующий — `v1.3` и т. д. |
| Формат тега | **аннотированный** (`git tag -a`), сообщение — как у `v1.0`–`v1.2`: `RS918 N96K-Pro vX: <краткий состав>` |
| Строка продукта | `UHSDR_PRODUCT_VERSION = 2.9.48-n96kpN` (видна на устройстве); меняется при изменении кода/документации, влияющей на сборку |
| Числовая версия | `UHSDR_VER_MAJOR/MINOR/RELEASE = 2.9.48` — **не менять**: её разбирает `Makefile`, по ней работает логика версий/EEPROM (`ui_driver.c:6131-6138`) |
| Версия загрузчика | `UHSDR_BOOT_VERS = 5.0.4` — менять только вместе с пересборкой/проверкой загрузчика |
| Артефакты | `release/vX/` — **вне git** (`release/` в `.gitignore`); в репозитории живут исходники и `docs/` |
| RELEASE-NOTES | обязателен начиная с v1.1 (`release/vX/RELEASE-NOTES.md`), в git его копии нет — сводка (сокращённо) дублируется в [docs/CHANGELOG.md](CHANGELOG.md) |

### 1.1. Где менять версию при bump

| Файл | Что менять |
|---|---|
| `mchf-eclipse/src/uhsdr_version.h` | `UHSDR_PRODUCT_VERSION` (строка продукта) |
| `tests/check_versions.py` | `EXPECTED_PRODUCT_VERSION` — тест падает, если версия не синхронизирована |
| `README.md` | строка версии/состояние/цифры |
| `docs/BUILD.md` | §3: размеры, flash/RAM/CCM, предупреждения |
| `docs/PATCHES.md` | шапка: срез HEAD, статусы, цифры |
| `docs/SAFETY.md` | §5: цифры бюджета |
| `docs/CI.md` | ожидаемая строка `UHSDR_PRODUCT_VERSION` |
| `docs/CHANGELOG.md` | новый раздел версии |
| `release/vX/RELEASE-NOTES.md` | сводка релиза (создаётся при упаковке) |

Правило: bump версии меняет **содержимое строки** в `.bin`, поэтому размер прошивки может измениться
(v1.2: `n96kp2` → 321 384 Б, `n96kp3` → 321 392 Б, +8 Б — проверено A/B-сборками). Все цифры в
документации обязаны совпадать с `release/vX/build-summary.json`, а не с промежуточными отчётами.

## 2. Обязательные проверки перед тегом

Все команды — из корня репозитория (`RS918_Nizar96K_Pro`). Для релиза v1.2 эталонные значения приведены
в последнем столбце; для следующего релиза их надо пересчитать по фактической сборке.

| # | Проверка | Команда/критерий | v1.2 |
|---|---|---|---|
| 1 | Дерево чистое, нет незакоммиченных правок | `git status --porcelain` — пусто | PASS |
| 2 | Host-тесты | `python tests/run_all.py` — exit 0, `RESULT: PASS` | 1134 проверки, 0 failed, 3 known |
| 3 | Чистая сборка | `python tools/build.py clean` затем `python tools/build.py all` — exit 0 | fw + bl собраны |
| 4 | Размеры/бюджет | `build/build-summary.json` совпадает с документацией; flash ≤ 983 040, RAM ≤ 131 072, CCM ≤ 65 536 | fw 321 392, RAM 42 616, CCM 63 476/65 536 |
| 5 | Предупреждения | число строк `build/warnings.log` равно зафиксированному; новых на строках патчей быть не должно | 78 строк (fw 70 + bl 8) |
| 6 | Проверка артефактов | `python docker/verify_artifacts.py build` — exit 0 (бюджеты + `SHA256SUMS.txt`) | PASS (проверено на свежей сборке HEAD v1.2) |
| 7 | Воспроизводимость (рекомендуется) | повторный `python tools/build.py all --force`: те же размеры/секции; `bl-mchf.bin` побайтно; `fw-mchf.bin` отличается только байтами `__DATE__/__TIME__` | PASS (bl побайтно, fw — 5 байт даты) |
| 8 | Версия в бинаре | строка `fwv-2.9.48-n96kpN` присутствует в `build/fw-mchf.bin` (проверка поиском по `.bin`); тест `version` в `tests/run_all.py` проверяет заголовок версии | n96kp3 |
| 9 | Документация | цифры в README/BUILD/PATCHES/SAFETY/CHANGELOG совпадают с `build-summary.json` | PASS |
| 10 | Чистота диффа | `git diff --check` — пусто (нет пробельных ошибок) | PASS |
| 11 | Docker/CI (если доступны) | `docker build -t rs918-n96k-build .`; workflow `.github/workflows/build.yml` | не запускались |

Важно: **пункты 1–6 и 8–10 обязательны**; 7 — рекомендуется (проверка воспроизводимости), 11 —
при наличии Docker/CI (на машине разработки Docker и раннер отсутствуют, см. [docs/CI.md](CI.md) §7). Релиз без аппаратных испытаний допустим, но в RELEASE-NOTES обязательна
фраза «на железе не испытывалась» и перечень непроверенного.

## 3. Сборка и контроль артефактов

```powershell
cd "E:\harnet\sdr 11\RS918_Nizar96K_Pro"
python tools\build.py clean
python tools\build.py all            # fw-mchf + bl-mchf в build\
python tests\run_all.py               # host-tests
python docker\verify_artifacts.py build   # бюджеты + build\SHA256SUMS.txt
```

Что должно получиться в `build/`:

| Файл | Назначение |
|---|---|
| `fw-mchf.bin/.hex/.elf/.map` | прошивка (bin — для загрузчика/DFU, hex — для STM32CubeProgrammer, elf/map — отладка/карта) |
| `bl-mchf.bin/.hex/.elf/.map` | загрузчик 5.0.4 |
| `build-summary.json` | машинночитаемые размеры (эталон для документации) |
| `warnings.log` | предупреждения компилятора/линкера |
| `compile_commands.json` | база компиляции (clangd/IDE) |

Ориентиры v1.2 (из `release/v1.2/build-summary.json`): `fw-mchf.bin` **321 392 Б** (flash 32,7 %,
запас 661 648 Б), RAM 42 616/131 072, CCM 63 476/65 536 (свободно 2 060); `bl-mchf.bin` **31 620/32 768 Б**
(запас 1 148); предупреждений **78 строк** (`fw 70 + bl 8`), новых этап A не добавил.

**Проверено в этапе B2 (2026-11-27):** чистая сборка HEAD `15af8d1f` (= тег `v1.2`; относительно
сборки `619e9187` — только docs) в `_tmp/b2-build`: exit 0 за 36,7 с; fw **321 392 Б**, bl **31 620 Б**,
CCM 63 476/65 536, warnings **78**; `docker/verify_artifacts.py` — **OK**; `bl-mchf.bin` SHA-256
`B1FE331C…9B9A` **совпал** с `release/v1.2`, `fw-mchf.bin` отличается от релизного **5 байтами**
в строке build-date (смещения 307 417–307 433) при том же размере.

Проверка «артефакт — это действительно наш релиз»:

* в `fw-mchf.bin` есть строка `fwv-2.9.48-n96kpN` (v1.2 — смещение 307 388);
* в `bl-mchf-5.0.4.bin` есть строка `Version: 5.0.4` (смещение 27 503), строки `4.1.2` нет;
* размеры `.bin` совпадают с `build-summary.json`;
* контрольные суммы — по `SHA256SUMS.txt` (см. §4.2).

## 4. Упаковка `release/vX`

### 4.1. Состав каталога

`release/` в `.gitignore`; каталог создаётся вручную и публикуется как артефакты релиза.
**В git артефактов нет** — храните копию `release/vX` вне рабочей машины и прикладывайте её к Release.

| Файл в `release/vX` | Откуда | Примечание |
|---|---|---|
| `fw-mchf.bin/.hex/.elf/.map` | `build/` | имена не меняются |
| `bl-mchf-5.0.4.bin/.hex/.elf/.map` | `build/bl-mchf.*` | переименовать, добавив версию загрузчика |
| `bl-mchf-4.1.2-known-good.bin` | `release/v1.0/bl-mchf.bin` | **не пересобирать**; 31 496 Б, SHA-256 `9332495…3B8CA1` |
| `build-summary.json`, `warnings.log`, `compile_commands.json` | `build/` | как есть |
| `RELEASE-NOTES.md` | создаётся | шаблон — §4.3 |
| `SHA256SUMS.txt` | создаётся | §4.2 |

В v1.2 это ровно 14 файлов; в v1.0 их было 12 (не было RELEASE-NOTES и known-good загрузчика).

### 4.2. `SHA256SUMS.txt`

Формат — как в `release/v1.0`–`v1.2`: **HEX в верхнем регистре, два пробела, имя файла**, LF в конце
строки; сам `SHA256SUMS.txt` в список не входит. Windows/PowerShell:

```powershell
$dir = "release\v1.3"
$lines = Get-ChildItem -LiteralPath $dir -File |
    Where-Object { $_.Name -ne "SHA256SUMS.txt" } | Sort-Object Name |
    ForEach-Object { "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash)  $($_.Name)" }
[System.IO.File]::WriteAllText((Join-Path $dir "SHA256SUMS.txt"), ($lines -join "`n") + "`n")
```

Проверка (должны совпасть все строки):

```powershell
cd release\v1.3
$bad = 0
Get-Content -LiteralPath "SHA256SUMS.txt" | ForEach-Object {
    if ($_ -match "^([0-9A-F]{64})  (.+)$") {
        $ok = (Test-Path -LiteralPath $Matches[2]) -and ((Get-FileHash -LiteralPath $Matches[2] -Algorithm SHA256).Hash -eq $Matches[1])
        if (-not $ok) { $bad++; Write-Output ("BAD: " + $Matches[2]) }
    }
}
Write-Output ("bad=" + $bad)
```

Для v1.2 проверено: **13 из 13** сумм совпадают. `docker/verify_artifacts.py` сам пишет `SHA256SUMS.txt`
для `build/` (8 артефактов + `build-summary.json`) — запускайте его **до переименования** файлов
загрузчика: в `release/vX` они называются `bl-mchf-5.0.4.*`, а скрипт ждёт `bl-mchf.{bin,elf,hex,map}`.
Для релизного каталога используйте команды выше.

### 4.3. `RELEASE-NOTES.md`

Обязательные разделы (стиль — `release/v1.1`, `release/v1.2`):

| Раздел | Что указать |
|---|---|
| Заголовок | `# RS918 N96K-Pro vX — release` |
| Сборка | commit (build commit), ветка, тег; версия `fwv-2.9.48-n96kpN` |
| Артефакты | список файлов, размеры, версия/особенности загрузчика (5.0.4 — «не испытан на железе», 4.1.2 known-good) |
| Бюджет | flash/RAM/CCM, предупреждения, host-тесты (N/0/K PASS) |
| Что нового | патчи/темы релиза (со ссылкой на `docs/PATCHES.md`) |
| Прошивка | краткий порядок (бэкап → `fw-mchf.bin` на флешку → обновление; DFU через BAND+); ссылка на docs/BOOTLOADER.md |
| Ограничения | что не проверено на железе, известные риски (F1 и т. п.); FreeDV — если применимо |
| Лицензия | GNU GPLv3, исходники — тег vX |

Цифры из RELEASE-NOTES обязаны совпадать с `build-summary.json` и документацией; для v1.2 пример —
`commit 619e9187`, fw 321 392 Б, bl 31 620 Б, CCM 63 476/65 536, 78 строк предупреждений, тесты 1134/0/3.

### 4.4. Проверка упаковки

| Проверка | Критерий |
|---|---|
| Размеры `.bin` | `bl` 31 620 Б (5.0.4) и `bl` 31 496 Б (4.1.2 known-good), `fw` из `build-summary.json` |
| Версия в `fw-mchf.bin` | строка `fwv-2.9.48-n96kpN` есть |
| Версия в `bl-mchf-5.0.4.bin` | `Version: 5.0.4` есть, `4.1.2` нет |
| SHA256SUMS | 13/13 (или N/N) совпадают, формат верхнего регистра |
| `git status` | дерево чистое (артефакты не попали в git) |

## 5. Тег и проверка

```powershell
# 1) финальные документация/цифры закоммичены, дерево чистое
git status --porcelain
git log --oneline -5

# 2) аннотированный тег на финальный commit
git tag -a v1.3 -m "RS918 N96K-Pro v1.3: <краткий состав>"

# 3) проверки
git cat-file -t v1.3          # tag (аннотированный)
git show v1.3 --no-patch      # tagger, дата, сообщение, commit
git describe --tags --always
```

Правила:

* тег ставится на тот commit, из которого собраны артефакты; если после сборки добавлен только
  docs-commit (без изменений кода/`.mak`/`uhsdr_version.h`), допустимо поставить тег на него — но тогда
  в RELEASE-NOTES укажите **оба** commit-а и подтвердите, что `git diff <build-commit> <tag-commit>`
  содержит только документацию (так у v1.2: сборка `619e9187`, тег `15af8d1f`, разница — только
  `PLAN-DEV.md`);
* после тега **не менять** код/версии в том же релизе — иначе новый тег (см. §7);
* тег **не ставить**, пока не выполнены §2 и §4.

## 6. Публикация

### 6.1. Remote и push

Публичного remote продукта сейчас нет; `origin` в локальном дереве указывает на upstream
`https://github.com/df8oe/UHSDR.git` — **пушить туда нельзя**.

```powershell
# создать пустой репозиторий продукта (GitHub/GitLab, GPLv3-совместимый), затем:
git remote add product https://<хост>/<аккаунт>/rs918-n96k-pro.git
git push product main
git push product --tags
```

После появления remote:

* создать **Release** для тега `vX` и приложить файлы из `release/vX` (все 14, включая RELEASE-NOTES и
  SHA256SUMS); GitHub автоматически отдаст исходники по тегу;
* заполнить `UHSDR_PRODUCT_REPO` в `mchf-eclipse/src/uhsdr_version.h` и строку «Репозиторий продукта»
  в README (макрос в коде не используется — бинарь не меняется, но пересоберите и сверьте размер,
  если правка попала в релизный commit);
* проверить, что `.gitignore`/`.gitattributes` не тащат `release/`, `build/`, логи.

### 6.2. GPLv3 и атрибуция

* исходники обязаны быть доступны для любой распространяемой прошивки: тег `vX` в публичном
  репозитории + `LICENCE.md` (GNU GPLv3, не менялся);
* атрибуция: UHSDR (`github.com/df8oe/UHSDR`, `UHSDR_REPO` в `src/uhsdr_version.h`), доработки Nizar
  (3V8MN) — маркеры NIZZ/NIZ96/NIZ32 сохранены; полный список — [docs/CREDITS.md](CREDITS.md);
* в RELEASE-NOTES указывать лицензию и что исходники — по тегу.

### 6.3. CI-релиз — отдельный этап

Сейчас `.github/workflows/build.yml` проверяет host-тесты и Docker-сборку, но **не создаёт GitHub Release**
и не публикует артефакты (workflow не запускался ни разу — нет remote/раннера; в файле к тому же
дублируются ключи `env`/`jobs` — известный дефект F12, исправить до включения CI). Автоматическая
публикация артефактов — отдельная задача; до неё шаги §6.1 выполняются вручную.

## 7. Откат релиза

| Ситуация | Действие |
|---|---|
| Тег ещё не запушен | исправить, пересобрать, `git tag -d vX` и поставить тег заново; `release/vX` пересоздать |
| Тег запушен/релиз опубликован | **не перемещать** тег; выпустить `vX+1` с исправлением; в CHANGELOG/RELEASE-NOTES пометить `vX` как отозванный (yanked), артефакты убрать из Release |
| Плохая прошивка у пользователя (firmware rollback) | прошить `fw-mchf.bin` предыдущего релиза (USB-флешка или DFU); см. docs/BOOTLOADER.md §6/§8. `.old`-дамп предыдущей прошивки остаётся на флешке |
| Плохой загрузчик 5.0.4 | откатить `bl-mchf-4.1.2-known-good.bin` по `0x08000000` (только SWD/DFU, живой загрузчик); см. docs/BOOTLOADER.md §5.3 |
| Настройки после отката | раскладка EEPROM между сборками различается — проверьте меню; дефолты загружаются F1+F3+F5 при включении (docs/SAFETY.md §1) |

## 8. Чек-лист релиза

| Фаза | # | Пункт | PASS-критерий |
|---|---|---|---|
| Подготовка | 1 | Версия bump в `uhsdr_version.h` и `tests/check_versions.py` | тест `version` PASS |
| | 2 | Обновлены README/BUILD/PATCHES/SAFETY/CI/CHANGELOG | цифры совпадают с будущим build-summary |
| | 3 | `git status` чистый | пусто |
| Сборка | 4 | `python tools/build.py clean` + `all` | exit 0, fw/bl собраны |
| | 5 | `python tests/run_all.py` | exit 0, RESULT: PASS (1134/0/3 для v1.2) |
| | 6 | `python docker/verify_artifacts.py build` | exit 0, SHA256SUMS записан |
| | 7 | Размеры/бюджеты/warnings | как в документации (v1.2: 321 392/31 620; CCM 63 476; 78 строк) |
| | 8 | (Опц.) повторная сборка `--force` | размеры/секции те же, bl побайтно |
| Упаковка | 9 | `release/vX` из 14 файлов (§4.1) | все файлы на месте |
| | 10 | `SHA256SUMS.txt` | N/N сумм совпадают |
| | 11 | `RELEASE-NOTES.md` | все разделы §4.3; цифры совпадают; оговорка «на железе не испытывалась», если так |
| | 12 | Правильный `bl-mchf-4.1.2-known-good.bin` | 31 496 Б, SHA `9332495…` |
| | 13 | `release/` не попал в git | `git status --porcelain` пуст |
| Тег | 14 | `git tag -a vX -m "…"` | `git cat-file -t vX` = tag |
| | 15 | Commit сборки и тега согласованы | diff — только docs (если так), записано в RELEASE-NOTES |
| Публикация | 16 | `git push product main` + `--tags` | тег и main на remote |
| | 17 | Release с 14 артефактами + SHA256SUMS | скачивание проверено по сумме |
| | 18 | GPLv3: исходники по тегу доступны, атрибуция | LICENCE.md/CREDITS.md, `UHSDR_REPO` |
| После | 19 | Записать в CHANGELOG статус ревью/испытаний | обновлено |
| | 20 | Аппаратные проверки (если были) | docs/VSWR.md §6, docs/BOOTLOADER.md §9 — PASS, результаты в отчёте |

## 9. История релизов (факты из git и release/)

| Релиз | Дата | Commit тега | Прошивка | Загрузчик | `release/` |
|---|---|---|---|---|---|
| v1.0 | 2026-11-26 | `82056486` | 320 024 Б, `2.9.48-n96kp1` | 4.1.2, 31 496 Б | 12 файлов |
| v1.1 | 2026-11-26 | `cf43b068` | 321 128 Б, `2.9.48-n96kp2` | 5.0.4, 31 620 Б (+4.1.2 known-good) | 14 файлов |
| v1.2 | 2026-11-27 | `15af8d1f` (сборка `619e9187`) | 321 392 Б, `2.9.48-n96kp3` | 5.0.4, 31 620 Б (+4.1.2 known-good) | 14 файлов, 13 SHA-256 проверены |

Сообщения тегов (для единообразия):

* `v1.0` — `RS918 N96K-Pro v1.0: Nizar 96k/12k tree + P0 patches + reproducible CLI build`;
* `v1.1` — `RS918 N96K-Pro v1.1: P1 VSWR/EEPROM/CAT fixes, bootloader 5.0.4, FreeDV rev 4121, CI/tests, AGC/Tune/CW backports`;
* `v1.2` — `RS918 N96K-Pro v1.2 (stage A): VSWR power-enforcement/hysteresis-trial fixes, NR+FreeDV #1749 fixes, docs correction (FreeDV compiled out)`.

## 10. Ссылки

| Документ | Что смотреть |
|---|---|
| [docs/BUILD.md](BUILD.md) | сборка, артефакты, воспроизводимость, прошивка |
| [docs/SAFETY.md](SAFETY.md) | правила эксплуатации и прошивки |
| [docs/CI.md](CI.md) | Docker/CI, ограничения (не запускались) |
| [docs/BOOTLOADER.md](BOOTLOADER.md) | загрузчик 5.0.4, обновление/откат |
| [docs/VSWR.md](VSWR.md) | защита по КСВ, калибровка, стендовые сценарии |
| [docs/PATCHES.md](PATCHES.md), [docs/CHANGELOG.md](CHANGELOG.md) | состав патчей и история |
| [docs/TESTPLAN.md](TESTPLAN.md) | протокол приёмочных испытаний на столе (артефакты, прошивка, ID, VSWR) |
| [LICENCE.md](../LICENCE.md), [docs/CREDITS.md](CREDITS.md) | GPLv3 и атрибуция |

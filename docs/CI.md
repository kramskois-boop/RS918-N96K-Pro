# CI и воспроизводимая сборка (Docker)

> N96K P2-2 + релизная автоматизация B1. Этот документ описывает
> автоматические проверки репозитория **RS918 N96K-Pro**: сборку прошивки в
> Docker с зафиксированным тулчейном, хост-тесты ЦОС-таблиц и упаковку
> GitHub Release. Настройка собственного remote и первый push — см. [GITHUB.md](GITHUB.md).
> Обычная сборка без Docker — см. [BUILD.md](BUILD.md).

---

## 1. Что и зачем проверяется

| Проверка | Где | Что ловит |
|---|---|---|
| Хост-тесты ЦОС-таблиц | `tests/run_all.py` | несоответствие `numTaps`/`numStages` длинам таблиц, nan/inf и нулевые коэффициенты, таблицы IQ-фильтров, отсутствие файла фильтра в `files.mak` (регрессия P0-2), инварианты версии/продукта, карту EEPROM |
| Сборка прошивки | `docker build .` | что дерево собирается фиксированным GCC 15.2.1 **с нуля**; проверяются `fw-mchf`/`bl-mchf` и бюджеты flash/RAM/CCM |
| Предупреждения компилятора | `tools/check_warnings.py` + `tests/warnings-baseline.txt` | новые/усилившиеся предупреждения GCC и линкера относительно baseline (warning-гейт B5); исчезновение известных предупреждений допускается |
| Артефакты | `docker/verify_artifacts.py` | наличие `.elf/.bin/.hex/.map`, `build-summary.json`, размеры в бюджете, `SHA256SUMS.txt` |
| Публикация релиза | job `release` + `tools/release.py` | полнота состава `release/vX`, формат `SHA256SUMS.txt`, тело GitHub Release из `RELEASE-NOTES.md`; только на push тега `v*` |

GitHub Actions (`.github/workflows/build.yml`) запускает это тремя job'ами:
`host-tests` (чистый Python, быстро), `docker-build` (образ + артефакты) и
`release` (только на тег `v*`: упаковка и GitHub Release).

---

## 2. Требования к окружению

**Для хост-тестов** (то, что нужно в 99% случаев):

* Python **3.8+**, только стандартная библиотека — `pip install` не нужен;
* ничего больше: тесты не компилируют и не запускают прошивку.

**Для Docker-сборки:**

* Docker **20.10+** (используется `docker build`, BuildKit желателен);
* ~2.5 ГБ на образ (базовый Ubuntu + тулчейн xPack + объектные файлы);
* интернет при первой сборке: скачивается архив тулчейна ~307 МБ;
* процессор: сборка «с нуля» ~30–60 с на 4+ ядрах.

---

## 3. Хост-тесты

Запуск из корня репозитория (или из любого каталога — путь определяется
относительно `tests/run_all.py`):

```bash
python3 tests/run_all.py
```

Коды возврата: **0** — всё в порядке (KNOWN/SKIP не считаются ошибкой),
**1** — есть FAIL, **2** — не удалось запустить сам тест (нет `mchf-eclipse/`
и т.п.). Ключ `--verbose` печатает имена запускаемых модулей.

Что проверяется (модули в `tests/`):

| Модуль | Проверки |
|---|---|
| `check_filters.py` | все `arm_fir_*`/`arm_iir_lattice_*` дескрипторы в `mchf-eclipse/drivers/audio/filters/*.c`: `numTaps == len(pCoeffs)`, `numStages == len(pkCoeffs)`, `len(pvCoeffs) == numStages+1`, `pState` (если задан inline) = `2*numStages`; массивы дескрипторов (`FirZoomFFTDecimate[6]`) — поэлементно и против `extern ...[N]`; все коэффициенты конечны (нет nan/inf), не переполняют float32, таблица не «все нули» |
| `check_iq.py` | большие таблицы Низара: `iq_tx_filter.c` (`IQ_FilterDescriptor`, `.i`/`.q` = `.num_taps`), `iq_rx_filter.c`, `iq_rx_filter_am.c` — размеры массивов = объявленным, сверка с `extern` в `filters.h` и с `FilterPathInfo` в `audio_filter.c` |
| `check_files_mak.py` | каждый `drivers/audio/filters/*.c` есть в `SRC` из `files.mak` (в т.ч. `iir_2_8k.c`, `iir_16k5_96ks.c` — регрессия P0-2); нет «мёртвых» путей |
| `check_versions.py` | `UHSDR_VER_*` = 2.9.48 (не менять!), `UHSDR_PRODUCT_VERSION = 2.9.48-n96kp3`, `UHSDR_PRODUCT_NAME`, GPLv3, атрибуция `df8oe/UHSDR`; `UHSDR_BOOT_VERS = 5.0.4` — единственное допустимое значение, 4.1.2 больше не принимается как KNOWN (F5 исправлен: `ACCEPTED_BOOT_VERS` убран, откат версии загрузчика даёт FAIL) |
| `check_eeprom.py` | карта EEPROM: `MAX_VAR_ADDR = EEPROM_FIRST_UNUSED-1 < NB_OF_VAR`, уникальность адресов, диапазон `[0, MAX_VAR_ADDR]`, наличие `#error`-стража в `ui_configuration.h`, все `EEPROM_*`, на которые ссылается `ui_configuration.c`, определены |
| `check_audio.py` | статические biquad-инстансы в `audio_driver.c`: `pCoeffs = 5*numStages`, `pState = 4*numStages` |

**Известные дефекты** (`tests/known_issues.py`): найденные, но не входящие в
зону задачи тестов дефекты таблиц принимаются как KNOWN (XFAIL), чтобы CI не
блокировался. Сейчас это:
* `iir_3k.c:IIR_3k` — `numStages=10`, но 11 `pkCoeffs` (символ нигде не
  используется, latent-дефект);
* `audio_driver.c:IIR_biquad_Zoom_FFT_I/Q` — placeholder-инициализаторы
  (`numStages=4`, 10 коэффициентов вместо 20); `pCoeffs` заменяется в
  `AudioDriver_SetRxAudioProcessing()` до первого запуска фильтра.

Когда дефект исправлен, запись из `known_issues.py` надо удалить — тест сам
предупредит, что KNOWN-запись больше не воспроизводится (stale).

---

## 4. Сборка в Docker

`Dockerfile` (две стадии):

1. **toolchain** — Ubuntu 22.04 + скачивание xPack GNU Arm Embedded GCC и
   проверка SHA256:

   | Параметр | Значение |
   |---|---|
   | Версия | **15.2.1-1.1** (та же, которой собраны `release/v1.0`) |
   | URL | `https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v15.2.1-1.1/xpack-arm-none-eabi-gcc-15.2.1-1.1-linux-x64.tar.gz` |
   | SHA256 | `da6a49ad4003944b823c6c93702a8787c922ab34bd7e918ec0eaf6933a9b1ff6` |
   | Размер | 307 090 703 байт |
   | Распаковка | `xpack-arm-none-eabi-gcc-15.2.1-1.1/bin/...` -> `/opt/xpack` |

   Хэш сверяется `sha256sum -c`, поэтому подмена/повреждение архива ломает
   сборку. Версия и хэш заданы `ARG` — их можно переопределить при обновлении
   тулчейна.

2. **build** — копирует `mchf-eclipse/`, `tools/`, `tests/`, `docker/`,
   запускает `python3 tests/run_all.py`, затем `python3 tools/build.py all`,
   затем `python3 docker/verify_artifacts.py build`.

Warning-гейт (`tools/check_warnings.py`) внутри образа не запускается: он
работает в job `docker-build` на извлечённом `artifacts/warnings.log` (см. §5.3).

Сборка и извлечение артефактов:

```bash
# 1) собрать образ (внутри выполняются тесты, сборка и проверка артефактов)
docker build -t rs918-n96k-build .

# 2) достать готовые артефакты из образа
docker create --name n96k rs918-n96k-build
mkdir -p artifacts
docker cp n96k:/src/build/. ./artifacts/
docker rm n96k

# 3) (опционально) проверить скачанные артефакты ещё раз
python3 docker/verify_artifacts.py artifacts
```

`tools/build.py` ищет компилятор в порядке `--toolchain`, `$ARM_GCC_BIN`,
`E:\tools\gcc-arm\bin`, `PATH`; в образе задан `ARM_GCC_BIN`, поэтому
Windows-путь по умолчанию не мешает.

### Ожидаемые артефакты и бюджеты

| Артефакт | Ожидание |
|---|---|
| `fw-mchf.bin` | эталон v1.0 — **320 024 байта**; допускается дрейф от патчей (по умолчанию ±8192, вне допуска — предупреждение); жёстко проверяется бюджет flash 983 040 байт (960 КиБ) |
| `bl-mchf.bin` | наличие и бюджет 32 768 байт; загрузчик 5.0.4 уже собран (31 620 Б в v1.2), проверяется только бюджетами |
| `fw-mchf.elf/.hex/.map`, `bl-*` | наличие, ненулевой размер, SHA256 в `SHA256SUMS.txt` |
| `build-summary.json` | flash/RAM/CCM в бюджетах (RAM 128 КиБ, CCM 64 КиБ); CCM < 1 КиБ свободно — предупреждение |

---

## 5. GitHub Actions

`.github/workflows/build.yml` — три job'а; верхнеуровневые ключи (`name`,
`on`, `permissions`, `concurrency`, `env`, `jobs`) встречаются ровно по
одному разу. Без PyYAML это проверяется локально:

```bash
python tools/check_workflow.py .github/workflows/build.yml
```

| Job | Когда | Что делает |
|---|---|---|
| `host-tests` | push/PR, любой ref | Ubuntu 24.04 + `actions/setup-python` 3.12, запуск `tests/run_all.py`, затем `tools/check_workflow.py` (YAML-линт), лог `host-tests.log` в артефактах |
| `docker-build` | после `host-tests` | `docker/setup-buildx-action` + `docker/build-push-action` (кэш слоёв в GitHub Actions cache, чтобы не качать 307 МБ тулчейна каждый раз), извлечение артефактов `docker cp`, warning-гейт `tools/check_warnings.py artifacts/warnings.log` (см. §5.3), повторная проверка `verify_artifacts.py`, выгрузка `n96k-firmware` (bin/hex/elf/map, `build-summary.json`, `compile_commands.json`, `warnings.log`, `SHA256SUMS.txt`) |
| `release` | **только push тега `v*`** (`if: startsWith(github.ref, 'refs/tags/v')`, `needs: docker-build`) | `actions/download-artifact@v4` (name `n96k-firmware`) -> `python3 tools/release.py --version <tag> --build-dir artifacts --out release/<tag>` -> `sha256sum -c SHA256SUMS.txt` -> `softprops/action-gh-release@v2` (файлы каталога, тело — `RELEASE-NOTES.md`). `permissions: contents: write` выдан только этому job'у |

Триггеры: push в `main` и теги `v*`, pull request, ручной запуск
(`workflow_dispatch`); job `release` на ручной запуск не реагирует — только на
тег.

### 5.1. `tools/release.py` — упаковка каталога релиза

Скрипт (только stdlib) собирает `release/vX` из `build/` или из скачанных
CI-артефактов:

```bash
python tools/release.py --version v1.3 --build-dir build --out release/v1.3 \
    [--notes PATH] [--known-good release/v1.2/bl-mchf-4.1.2-known-good.bin] [--force]
```

* `fw-mchf.{bin,hex,elf,map}` копируются как есть, `bl-mchf.*` — с версией
  загрузчика из `uhsdr_version.h` (`bl-mchf-5.0.4.*`);
* `build-summary.json`, `compile_commands.json`, `warnings.log` (если лога нет,
  создаётся пустой); опционально known-good загрузчик 4.1.2 (`--known-good`):
  в CI-артефактах его нет, поэтому без флага файл не включается;
* `RELEASE-NOTES.md` — из `--notes`, затем из `docs/release-notes/<version>.md`,
  иначе шаблон с TODO-маркерами;
* `SHA256SUMS.txt` — HEX в верхнем регистре, два пробела, сортировка без учёта
  регистра (как в `release/v1.0-v1.2`): 13 строк в полном локальном наборе,
  12 — без known-good; сам файл в список не входит;
* размеры и бюджеты перепроверяются `docker/verify_artifacts.py` (subprocess;
  пропускается, если каталог не похож на сырой `build/`, и при `--no-verify`);
* `--force` пересоздаёт непустой каталог; скрипт откажется удалять репозиторий,
  `build/` и корень `release/`. Код возврата: 0 — успех, 1 — ошибка упаковки
  или проверки, 2 — ошибка аргументов.

### 5.2. Статус: Actions запускаются и проходят (2026-11-27)

Репозиторий опубликован: <https://github.com/kramskois-boop/RS918-N96K-Pro> (public).
Живой прогон workflow после первого push в `main`:

| Job | Результат | Детали |
|---|---|---|
| `host-tests` | **success** | 07:04:46–07:05:03 UTC (17 с): `tests/run_all.py` PASS + YAML-линт |
| `docker-build` | **success** | 07:05:06–07:10:45 UTC (5 мин 39 с): сборка в pinned GCC 15.2.1, бюджеты и `SHA256SUMS.txt` |
| `release` | skipped | push в `main` — не тег (ожидаемо) |

Job `release` проверен отдельно на временном теге `v0.0.0-citest`: `download-artifact` →
`tools/release.py` → `sha256sum -c` → `softprops/action-gh-release` — **success**,
GitHub Release с артефактами создан; после проверки тестовый релиз и тег удалены
(в репозитории остаются только `v1.0`, `v1.1`, `v1.2`).

**Последующие прогоны (включая B5):** каждый push в `main` проходит те же job'ы; PR #1 (B3/B4)
и PR #2 (B5) влиты 2026-11-27, HEAD `main` — `ee5e06ed`. Последний прогон на `ee5e06ed`
([run 45971492786](https://github.com/kramskois-boop/RS918-N96K-Pro/actions/runs/45971492786)):
`host-tests` — **success** (10 с), `docker-build` с warning-гейтом — **success** (2 мин 3 с),
`release` — skipped (не тег).

Локальный `docker build` на машине разработки по-прежнему не выполнялся (Docker не
установлен) — но Docker-сборка теперь воспроизводится в Actions.

CI собирает **только default-конфигурацию**. Opt-in FreeDV-сборка (`tools/build.py --freedv`,
см. [FREEDV.md](FREEDV.md)) в CI пока не входит: её нужно собирать локально и проверять на стенде;
при желании добавляется отдельным job/матрицей.

### 5.3. Warning-гейт: `tools/check_warnings.py` (B5)

`tools/build.py` складывает все предупреждения компилятора и линкера в
`build/warnings.log`. Job `docker-build` после извлечения артефактов выполняет

```bash
python3 tools/check_warnings.py artifacts/warnings.log
```

и валит сборку, если появилось **новое** предупреждение (его текста нет в
baseline) или известное предупреждение стало встречаться **чаще**, чем в
baseline. Если известные предупреждения исчезли — это OK (baseline стоит
понизить, чтобы зафиксировать улучшение).

* Baseline: `tests/warnings-baseline.txt`, формат `<сколько раз> <нормализованный текст>`;
  строки-комментарии `#` игнорируются, порядок — по алфавиту. На момент B5
  baseline содержит 73 предупреждения (35 различных текстов) — это состояние
  default-сборки после исправления пяти `-Waddress`-дефектов;
* нормализация: у диагностик GCC убираются абсолютные пути и номера
  строк/столбцов (относительный файл берётся из префикса, который пишет
  `build.py`); в сообщениях линкера дополнительно убираются абсолютные пути,
  суффикс `.exe` у `lto-wrapper` и случайные числа. Поэтому один и тот же
  baseline работает и на Windows-хосте, и в Linux-образе с тем же pinned
  GCC 15.2.1;
* локальная проверка (без пересборки) — по существующему логу:
  `python tools/check_warnings.py build` (принимается и каталог сборки, и
  сам файл `warnings.log`);
* обновление baseline после осознанного исправления/принятия предупреждений:

  ```bash
  python tools/build.py all --force
  python tools/check_warnings.py build --update-baseline
  git diff tests/warnings-baseline.txt   # посмотреть, что именно изменилось
  ```

* коды возврата: **0** — OK, **1** — есть новые/усилившиеся предупреждения,
  **2** — не удалось прочитать лог или baseline (например, сборка не
  выполнялась);
* гейт относится к **default-конфигурации** (её собирает CI). Лог
  `--freedv`-сборки с этим baseline сравнивать не нужно: baseline фиксирует
  default-сборку (сейчас FreeDV-вариант даёт тот же набор предупреждений, но
  это не гарантировано; если opt-in сборка появится в CI — ей нужен свой
  baseline).

---

## 6. Воспроизводимость: что гарантировано, а что нет

* **Тулчейн** зафиксирован версией и SHA256 архива — одинаковая версия
  компилятора/линкера гарантирована.
* **Базовый образ** `ubuntu:22.04` — тег, а не digest. Для абсолютной
  воспроизводимости можно добавить `@sha256:...` к `FROM` (нужно обновлять
  вручную).
* **Байт-в-байт одинаковый `fw-mchf.bin` между сборками невозможен**: прошивка
  содержит `UHSDR_BUILD_DAT` = `"fwt-"__DATE__" - "__TIME__` (см. BUILD.md
  §3). Совпадают размеры, карта памяти и `bl-mchf.bin` (проверено в v1.0).
  Для сравнения сборок используйте размеры из `build-summary.json` и карту
  `.map`, а не SHA256 прошивки.
* Скрипт `docker/verify_artifacts.py` проверяет бюджеты и пишет
  `SHA256SUMS.txt` для контроля целостности скачанных файлов, а не для
  сравнения независимых сборок.

---

## 7. Ограничения и что осталось

* Полный `docker build` **не проверялся на машине разработки**: Docker там не
  установлен. Проверено статически: Dockerfile-синтаксис, URL/хэш/раскладка
  архива тулчейна (HTTP range-запрос; xPack публикует `.sha`, digest также есть
  в GitHub API), `docker/verify_artifacts.py` на реальных артефактах v1.0/v1.2.
* GitHub Actions **проверены вживую** (2026-11-27, включая B5): `host-tests` и `docker-build`
  зелёные на push в `main` (последний — `ee5e06ed`, run 45971492786), job `release` — на
  временном теге; см. §5.2.
  Локального `docker build` по-прежнему нет (Docker не установлен на машине
  разработки), поэтому воспроизводимость обеспечивает CI-раннер.
* В `docs/BUILD.md` ссылку на этот документ стоит добавить владельцу
  `BUILD.md` (файл вне зоны задачи P2-2).

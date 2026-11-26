# CI и воспроизводимая сборка (Docker)

> N96K P2-2. Этот документ описывает автоматические проверки репозитория
> **RS918 N96K-Pro**: сборку прошивки в Docker с зафиксированным тулчейном и
> хост-тесты ЦОС-таблиц. Обычная сборка без Docker — см. [BUILD.md](BUILD.md).

---

## 1. Что и зачем проверяется

| Проверка | Где | Что ловит |
|---|---|---|
| Хост-тесты ЦОС-таблиц | `tests/run_all.py` | несоответствие `numTaps`/`numStages` длинам таблиц, nan/inf и нулевые коэффициенты, таблицы IQ-фильтров, отсутствие файла фильтра в `files.mak` (регрессия P0-2), инварианты версии/продукта, карту EEPROM |
| Сборка прошивки | `docker build .` | что дерево собирается фиксированным GCC 15.2.1 **с нуля**; проверяются `fw-mchf`/`bl-mchf` и бюджеты flash/RAM/CCM |
| Артефакты | `docker/verify_artifacts.py` | наличие `.elf/.bin/.hex/.map`, `build-summary.json`, размеры в бюджете, `SHA256SUMS.txt` |

GitHub Actions (`.github/workflows/build.yml`) запускает это двумя job'ами:
`host-tests` (чистый Python, быстро) и `docker-build` (образ + артефакты).

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

`BT`bash
python3 tests/run_all.py
`BT`

Коды возврата: **0** — всё в порядке (KNOWN/SKIP не считаются ошибкой),
**1** — есть FAIL, **2** — не удалось запустить сам тест (нет `mchf-eclipse/`
и т.п.). Ключ `--verbose` печатает имена запускаемых модулей.

Что проверяется (модули в `tests/`):

| Модуль | Проверки |
|---|---|
| `check_filters.py` | все `arm_fir_*`/`arm_iir_lattice_*` дескрипторы в `mchf-eclipse/drivers/audio/filters/*.c`: `numTaps == len(pCoeffs)`, `numStages == len(pkCoeffs)`, `len(pvCoeffs) == numStages+1`, `pState` (если задан inline) = `2*numStages`; массивы дескрипторов (`FirZoomFFTDecimate[6]`) — поэлементно и против `extern ...[N]`; все коэффициенты конечны (нет nan/inf), не переполняют float32, таблица не «все нули» |
| `check_iq.py` | большие таблицы Низара: `iq_tx_filter.c` (`IQ_FilterDescriptor`, `.i`/`.q` = `.num_taps`), `iq_rx_filter.c`, `iq_rx_filter_am.c` — размеры массивов = объявленным, сверка с `extern` в `filters.h` и с `FilterPathInfo` в `audio_filter.c` |
| `check_files_mak.py` | каждый `drivers/audio/filters/*.c` есть в `SRC` из `files.mak` (в т.ч. `iir_2_8k.c`, `iir_16k5_96ks.c` — регрессия P0-2); нет «мёртвых» путей |
| `check_versions.py` | `UHSDR_VER_*` = 2.9.48 (не менять!), `UHSDR_PRODUCT_VERSION = 2.9.48-n96kp1`, `UHSDR_PRODUCT_NAME`, GPLv3, атрибуция `df8oe/UHSDR`; `UHSDR_BOOT_VERS` — целевое значение 5.0.4 (до завершения параллельного порта загрузчика 4.1.2 принимается как KNOWN) |
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

Сборка и извлечение артефактов:

`BT`bash
# 1) собрать образ (внутри выполняются тесты, сборка и проверка артефактов)
docker build -t rs918-n96k-build .

# 2) достать готовые артефакты из образа
docker create --name n96k rs918-n96k-build
mkdir -p artifacts
docker cp n96k:/src/build/. ./artifacts/
docker rm n96k

# 3) (опционально) проверить скачанные артефакты ещё раз
python3 docker/verify_artifacts.py artifacts
`BT`

`tools/build.py` ищет компилятор в порядке `--toolchain`, `$ARM_GCC_BIN`,
`E:\tools\gcc-arm\bin`, `PATH`; в образе задан `ARM_GCC_BIN`, поэтому
Windows-путь по умолчанию не мешает.

### Ожидаемые артефакты и бюджеты

| Артефакт | Ожидание |
|---|---|
| `fw-mchf.bin` | эталон v1.0 — **320 024 байта**; допускается дрейф от патчей (по умолчанию ±8192, вне допуска — предупреждение); жёстко проверяется бюджет flash 983 040 байт (960 КиБ) |
| `bl-mchf.bin` | только наличие и бюджет 32 768 байт: версия загрузчика 5.0.4 портируется отдельной задачей |
| `fw-mchf.elf/.hex/.map`, `bl-*` | наличие, ненулевой размер, SHA256 в `SHA256SUMS.txt` |
| `build-summary.json` | flash/RAM/CCM в бюджетах (RAM 128 КиБ, CCM 64 КиБ); CCM < 1 КиБ свободно — предупреждение |

---

## 5. GitHub Actions

`.github/workflows/build.yml`:

* **host-tests** — Ubuntu 24.04 + `actions/setup-python` (3.12), запуск
  `tests/run_all.py`, лог `host-tests.log` в артефактах;
* **docker-build** — `docker/setup-buildx-action` + `docker/build-push-action`
  (кэш слоёв в GitHub Actions cache, чтобы не качать 307 МБ тулчейна каждый
  раз), извлечение артефактов `docker cp`, повторная проверка
  `verify_artifacts.py`, выгрузка `n96k-firmware` (bin/hex/elf/map,
  `build-summary.json`, `SHA256SUMS.txt`).

Триггеры: push в `main` и теги `v*`, pull request, ручной запуск
(`workflow_dispatch`).

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
  архива тулчейна (HTTP range-запрос + `.sha256` из релиза xPack), YAML
  workflow, `docker/verify_artifacts.py` на реальных артефактах v1.0.
* GitHub Actions workflow не запускался (нет удалённого репозитория/раннера) —
  YAML провалидирован локально (PyYAML) и вручную.
* В `docs/BUILD.md` ссылку на этот документ стоит добавить владельцу
  `BUILD.md` (файл вне зоны задачи P2-2).

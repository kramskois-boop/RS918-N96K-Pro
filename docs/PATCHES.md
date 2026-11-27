# Каталог патчей RS918 N96K-Pro

Источники: аудит исходного дерева Nizar (`_audit/REPORT.md`, `_audit/parts/*`), план
[PLAN-DEV.md](../PLAN-DEV.md) и отчёты исполнителей в `_audit/dev/*` (вне репозитория).

Нумерация: **P0** — исправления до любого применения прошивки (v1.0); **P1** — качество,
безопасность и конфигурация (v1.1); **P2** — бэкпорты upstream и инфраструктура (v1.1);
**A1/A2** — этап A (v1.2): закрытие остаточных P2/P3-замечаний независимого ревью S3c по VSWR
и порт NR+FreeDV #1749; **B1–B5** — инструментальные этапы после v1.2 (тесты/CI/релиз,
документация, аудио/DSP-hardening, opt-in FreeDV, warning-гейт/фиксы/оптимизация).
Историческое замечание: в PLAN-DEV.md патч гигиены репозитория носил номер P1-1; в v1.1 номера
P1-1…P1-5 — новая серия (PA/КСВ/EEPROM/CAT), совпадающая с маркерами в коде, а патч гигиены ниже
для однозначности называется **P1-H**.

Правила: каждое изменение помечено в коде комментарием `// N96K P0-x / P1-x / P2 / A1 / A2: <кратко>`;
диффы минимальные; поведение и алгоритмы Nizar не меняются — только исправление дефекта или
добавление защиты.

Срез статусов — 2026-11-27, `main` HEAD **34d28730** = v1.2 + B1–B6 + **обязательный GCC 9.3.1**
(причина зависания найдена и устранена: GCC 15.2.1-прошивки зависают после splash, GCC 9.3.1
работает). Коммиты последней волны: Nizar-audit; фиксы H1/H2/H3 `aa5fdcb9`, H5 `a4042337`,
H4/H6/H9/H10/H11/H13 `69ccd736`; opt-in `--full` `e8a78590`; cleanup; docs; GCC 9.3.1-пересборка
`34d28730`. **Финальные сборки и пакет пересобраны GCC 9.3.1**; **релизы v1.0–v1.2 и прежний
GCC15-пакет — невалидны (не загружаются), не прошивать.** H7/H8/H12/VID-PID — стенд/решение lead,
H14/H15/H16/N11 — известные отклонения (не исправлялись).
Теги: **v1.0** = `82056486`, **v1.1** = `cf43b068`,
**v1.2** = annotated `4ca85bba` → commit `dc204cc1` (сборка/версия `619e9187`); артефакты —
`release/v1.0`…`release/v1.2` (GCC15, исторические), актуальный пакет —
`release/bench-2026-11-27` (GCC 9.3.1, финализирует lead). Живой CI: Actions
(`host-tests` + `docker-build` + release-job по тегу) — см. [CI.md](CI.md) §5.2; Dockerfile/CI
pinned на GCC 9.3.1-1.3, живой прогон — при следующем push. Статусы:
«реализовано» = код в дереве; «сборка подтверждена» = `python tools/build.py firmware` завершился
с exit 0; «на железе не проверялось» = полевых испытаний не было.

Независимое ревью v1.1 (S3c, агент p-verify2) завершено: P1-дефектов в коде не найдено,
замечания F1–F13 — `_audit/dev/p-verify2.md` (вне репозитория); этап A закрывает F2/F6 (A1) и
F3 (A2 — в конфигурации с включённым FreeDV). Независимое ревью v1.2 (S3d, агент p-verify3)
завершено 2026-11-27: A1/A2/сборка/тесты — PASS, P1/P2-дефектов нет, находки только P3
(`_audit/dev/p-verify3.md`); его замечания по PLAN-DEV закрыты в B1–B5.

**GCC 9.3.1 — актуальные размеры (main 34d28730, пакет release/bench-2026-11-27):** default
`fw-mchf.bin` **312 216 Б** (31,8 % области 960 КиБ, запас 670 824; RAM 42 392, CCM 63 476/65 536,
Build `Nov 27 2026 - 14:55:12`); FreeDV opt-in **555 608 Б** (RAM 48 552, CCM 63 972, `15:05:45`);
full opt-in **561 720 Б** (RAM 48 552, CCM 63 972, `15:05:12`; стендовая); O3 opt-in **412 536 Б**
(RAM 42 436, CCM 63 476, `15:06:17`; стендовая); `bl-mchf.bin`: в пакете — известный рабочий 5.0.4
31 620 Б, GCC9 all-сборка — **32 416/32 768 Б** (запас 352; CI, в пакет не входит). Предупреждений
**50** (all-сборка; firmware-only 46), 16 различных текстов — baseline warning-гейта; host-тесты
**1754 PASS** (0 failed, 3 known); версия `2.9.48-n96kp3`. Источник — `build-final-gcc9/`,
`build-gcc9-full/`, `build-gcc9-freedv/`, `build-gcc9-o3/` (build-summary.json, warnings.log;
2026-11-27).

**Исторические сборки GCC 15.2.1 — НЕВАЛИДНЫ для железа (зависают после splash, не прошивать):**
v1.0 `fw` 320 024 / bl 31 496 / 80 строк; v1.1 321 128 / 31 620 / 78; v1.2 321 392 / 31 620 / 78;
main ee5e06ed: 322 312 (RAM 42 668), FreeDV 565 552 (48 836), O3 425 500 (42 696), 73 warnings;
B6/69ccd736: 323 304 (42 684), FreeDV 567 040 (48 840), full 573 088 (48 840), O3 428 236 (42 700),
73 warnings. Числа оставлены как история; для железа годятся только GCC9-сборки.

## Сводная таблица

> Проверки/размеры в строках этой таблицы и в разделах ниже — исторические снимки
> **GCC 15.2.1** (для железа невалидны); актуальные GCC 9.3.1 — в шапке документа и в разделе
> «Nizar-review (B6) → Цифры и проверки».

### v1.0 — тег `v1.0` (закрыто)

| ID | Тема | Файлы | Источник в аудите | Статус |
|---|---|---|---|---|
| P0-1 | EEPROM/конфигурация: мёртвая проверка диапазона, ошибки ConfigStorage, клампы | `drivers/ui/ui_configuration.c`, `misc/config_storage.c` | REPORT §7 U2 | закрыт в v1.0 |
| P0-2 | Сборка: файлы фильтров в `*.mak`, CLI-сборка, блокеры GCC 10+ | `files.mak`, `f4-files.mak`, `Makefile`, `tools/build.py` + 10 файлов правок GCC 15 | REPORT §7 U1, §9 | закрыт в v1.0 |
| P0-3 | Аудио: NR-буфер, UB в реверберации, `abs()` → `fabsf()` | `drivers/audio/audio_driver.c/.h` | REPORT §7 A1–A3 | закрыт в v1.0 |
| P0-4 | PA/КСВ: guard деления на ноль + foldback по КСВ | `drivers/ui/radio_management.c/.h` | REPORT §7 U3, §6.5 п.1–2 | закрыт в v1.0 (порог/индикация/retry — P1-1…P1-3) |
| P0-5 | TX-безопасность: CAT при передаче, запрет TX вне бэндов | `cat_driver.c`, `radio_management.c`, `ui_menu_structure.c` | REPORT §7 U4 | закрыт в v1.0 (остаток CAT — P1-5) |
| P0-6 | Идентификация/брендинг: `UHSDR_REPO`, строки продукта, README/docs | `src/uhsdr_version.h`, `README.md`, `docs/**` | REPORT §6.3, §10 п.9 | закрыт в v1.0 (версия/загрузчик — v1.1) |
| P1-H | Гигиена репозитория: `.gitattributes`, кодировки/EOL (в PLAN-DEV.md — P1-1) | `.gitattributes`, `.editorconfig`, `iir_2_8k.c`, `iir_2_9k.c` | REPORT §7 U7 | закрыт в v1.0 |

### v1.1 — тег `v1.1` (P1/P2)

| ID | Тема | Файлы (основные) | Источник | Статус |
|---|---|---|---|---|
| P1-1 | VSWR-порог в EEPROM (слот 426) + пункт меню Configuration | `hardware/uhsdr_board.h`, `drivers/ui/radio_management.h/.c`, `ui_configuration.c/.h`, `menu/ui_menu*.c/.h` | audit R2 | реализовано; сборка и host-tests — PASS; на железе нет |
| P1-2 | Индикация foldback: мигающее `SWR!` в боксе PWR | `drivers/ui/ui_driver.c` | audit R4 | реализовано; сборка — PASS; вид на LCD не проверен |
| P1-3 | Быстрый foldback: свои 10-мс счётчики, safe retry, независимость от dynamic tune | `drivers/ui/radio_management.c/.h` | audit R1/R3/R5 | реализовано; модель 8 сценариев + сборка — PASS; на железе нет |
| P1-4 | EEPROM read fallback (закрыт UB `value16`) | `drivers/ui/ui_configuration.c` | audit R7 | реализовано; сборка — PASS |
| P1-5/CAT | Закрыты EEPROM-VFO-select и clone RX `df.tune_new` при TX; эмуляция 0x65; поправка про get_vfo | `drivers/cat/cat_driver.c` | audit R8, Hamlib 4.3 | реализовано; сборка из чистого HEAD — PASS; с Hamlib/WSJT-X/железом не проверено |
| P2-1 | Загрузчик upstream 5.0.4: 8 файлов, совместимость API и SBLA/SBLS | `src/bootloader/*`, `src/uhsdr_version.h`, docs/BUILD.md, docs/SAFETY.md | upstream 2.12.2 | реализовано; оба таргета собираются; загрузчик не прошивался |
| P2-2 | FreeDV/Codec2 rev 4121 + фикс #1749 | `drivers/freedv/**`, files.mak, tools/build.py, Makefile | upstream 2.12.2 | vendor синхронизирован; сборка — PASS; по умолчанию выключен (`IS_SMALL_BUILD`); в **B4** — opt-in сборка (после B5: fw 565 552 Б, RAM 48 836, CCM 63 972); интеграционный слой не пересинхронизирован |
| P2-3 | CI/Docker с pinned toolchain + host-тесты ЦОС | `Dockerfile`, `docker/verify_artifacts.py`, `.github/workflows/build.yml`, `tests/**`, docs/CI.md | audit §9 | реализовано; host-tests 1754/0/3 PASS (B1); в **B5** добавлен warning-гейт; CI в Actions зелёный (2026-11-27, PR #1/#2, run main `ee5e06ed`); docker build локально не запускался (Docker нет) — воспроизводимость даёт CI |
| P2-4 | Бэкпорты AGC restore #1730, Tune Power #1750, CW-decoder #1881 | см. раздел P2-4 | upstream 2.12.2 | порт выполнен; сборка/тесты — PASS; на железе нет |

### v1.2 — этап A (тег `v1.2`) и этапы B3–B5 (main `ee5e06ed`)

| ID | Тема | Файлы (основные) | Источник | Статус |
|---|---|---|---|---|
| A1 | VSWR: принудительный минимальный drive при активном foldback; завершение trial в зоне гистерезиса | `drivers/ui/radio_management.c/.h` | S3c F2/F6 (`p-verify2`) | реализовано; `-fsyntax-only`, изолированная сборка (+208 Б), Unicorn-симуляция реальных функций 58/58 PASS (baseline 7 FAIL); на железе нет |
| A2 | NR+FreeDV #1749: `AudioNr_Prepare()` при выходе из DIGI; NR pass-through при активном FreeDV | `drivers/audio/audio_nr.c/.h`, `audio_driver.c` | S3c F3 (`p-verify2`), upstream `df9ef3f9`/`27945d8b` | реализовано; сборка (+48 Б), RAM/CCM 0, host-tests PASS; в B4 добавлена opt-in FreeDV-сборка, где гейты активны |
| B3 | Надёжность/DSP: VLA в audio ISR, согласованный сброс NR, OOB ALC-кольца, диагностика | `drivers/audio/audio_driver.c/.h`, `audio_nr.c/.h`, `codec/uhsdr_hw_i2s.c`, `ui/menu/*` | residual P3 F4/R9/A4, audit §4.6 | реализовано; host-tests 1754 PASS; fw 322 360 (+968), RAM 42 668 (+52), CCM 0, 78 warnings; на железе нет |
| B4 | FreeDV opt-in: `--freedv`, guard-фиксы, MODEM_STATS, docs/FREEDV.md | `hardware/uhsdr_board.h`, `audio_driver.c`, `audio_filter.c`, `freedv_uhsdr.c`, `drivers/freedv/freedv_api*`, `tools/build.py` | B4 workstream | реализовано; default-сборка не меняется; FreeDV 565 584/48 836/63 972 (после B5 — 565 552); на железе нет |
| B5 | Warning-гейт, два реальных дефекта `-Waddress`, bounded `float2fixedstr`, docs/LIBS.md, `--opt-level {O2,O3,Os}` | `ui_driver.c`, `ui_vkeybrd.c`, `ui_menu.c`, `tools/check_warnings.py`, `tests/warnings-baseline.txt`, `tools/build.py`, `docs/LIBS.md`, docs/CI.md | B5 (b5-warn/libs/opt, task-35/36/37/38) | реализовано; **GCC 9.3.1:** default 312 216/42 392/63 476, baseline 50/16, O3 412 536/42 436/63 476; host-tests 1754 PASS; исторические GCC15-цифры 322 312/425 500/73 невалидны; на железе нет |

### B6 — Nizar-review (ветка `nizar-review`, HEAD `69ccd736`)

| ID | Тема | Файлы (основные) | Источник | Статус |
|---|---|---|---|---|
| Nizar-audit | классификация изменений Nizar: 16 вредных/критичных (H1–H16), 14+ положительных, ~12 нейтральных | — (read-only) | task-39; `_audit/dev/nizar-audit.md` | сделано |
| H1 | TX только в любительских участках четырёх расширенных RX-бэндов (10 м 28,0–29,7; 4 м 70,0–70,5; 2 м 144–146; 70 см 430–440 МГц) | `hardware/uhsdr_board.h`, `drivers/ui/radio_management.c` | `aa5fdcb9` | исправлено; на железе не проверялось |
| H2 | безопасные дефолты TX-аудио (MIC 15, ALC postfilt 1, comp out 10, treble 4, EQ CUSTOM) + reset-default в меню | `src/uhsdr_main.c`, `drivers/audio/audio_driver.h`, `ui_configuration.c`, `ui_menu.c` | `aa5fdcb9` | исправлено; спектр TX — стенд |
| H3 | CW-R/CW-USB: меню CW LSB/USB и CW Offset, переключение стороной, CAT 0x02/0x03, дефолт CW-USB | `src/uhsdr_main.c`, `cat/cat_driver.c`, `ui/radio_management.c`, `ui/ui_driver.c`, `ui/ui_vkeybrd.c`, `ui/menu/ui_menu*.c` | `aa5fdcb9` | исправлено; на железе не проверялось |
| H5 | USB-TX guard/overflow, индексация/overflow USB-микрофона, tSamFreq/bNrChannels из макросов | `usb/app/usbd_audio_if.c`, `usb/device/class/AUDIO/Src/usbd_audio_cdc_comp.c`, `usb/app/usbd_desc.h`, `usb/device/class/composite/usbd_composite_desc.c` | `a4042337`; `_audit/dev/b6-usb-fix.md` | исправлено; байты дескрипторов не изменились; USB — стенд |
| --full | opt-in полная сборка без `IS_SMALL_BUILD` (FreeDV + const-окна; firmware only) | `tools/build.py`, `hardware/uhsdr_board.h` | `e8a78590`; `_audit/dev/nizar-full.md` | сделано; стендовая, не продукт |
| H4/H6/H9/H10/H11/H13 | Si5351 PLL reset default/menu; AGC-дефолты/ring_buffsize/меню Mode/Threshold (Hang-пункты не возвращены — осознанно); сдвиг +15 Гц (дефолт 0 Гц); минимумы CW-декодера 2/6; CAT PWR/SWR nibble; TP_IRQ PULLUP/VERY_HIGH | `src`, `drivers/audio`, `drivers/cat`, `drivers/ui`, `drivers/ui/oscillator`, `hardware` | `69ccd736`; `_audit/dev/b6-misc-fix.md` | исправлено; H13-остаток nr_first_time — стенд |
| H7/H8 | пороги ADC-clip и кривая RF-гейна — требуют стенда (генератор+аттенюатор) | — | `nizar-audit.md` §2 | только стенд (в коде не меняются) |
| H12/VID-PID | мёртвый noise gate; USB VID/PID из bit2 реверба (0x0963/0x5733 / Arduino 0x2341/0x8036) | — | `b6-misc-fix.md` §3 | стенд/решение lead (нужна миграция EEPROM для своего VID) |
| H14/H15/H16/N11 | H14 скрытые меню/zoom; H15 bias-reduce/fine power factor; H16 MIC max 150 + codec +20 дБ без клампа; N11 лицензионная строка | — | `nizar-audit.md` §2, `b6-verify.md` D5 | известные отклонения, не исправлялись (отдельные решения) |
| cleanup | −23,2 ГиБ мусора; docs-rebuild; финальные сборки; печатные PDF | `_audit/dev/cleanup.md`, `docs/**`, `release/bench-2026-11-27/printable/` | task-41/42/47; `22fce856` | сделано |

---

## P0-1 — EEPROM и конфигурация

**Проблема.** Проверка диапазона в `ui_configuration.c` содержала `&& 0xff` вместо `& 0xff`,
т.е. была мёртвой; `ConfigStorage_ReadVariable` в ветке `CONFIGSTORE_IN_USE_ERROR` возвращал
«успех», не записывая `*value`; `ts.band` после загрузки использовался как индекс без проверки.
При сбое/битой EEPROM — чтение за границей `bandInfo[]` и запись за границей `vfo[].band[]`.

**Исправление.** `&& 0xff` → `& 0xff`; ошибка ConfigStorage пробрасывается наверх; значения
band/demod/decode ограничиваются допустимыми перед использованием как индексы.

**Проверка.** Валидные значения читаются как раньше; невалидные — корректируются; сборка и запуск
на железе (S3, verifier). Риск: при битой EEPROM настройки теперь сбрасываются в дефолт вместо
неопределённого поведения — это ожидаемо.

## P0-2 — Сборка

**Проблема.** В каталоге фильтров 46 `.c`, в `files.mak` — 44: `iir_2_8k.c` и
`iir_16k5_96ks.c` не зарегистрированы, хотя их символы используются (`IIR_2k8_LPF`,
`IIR_16k5_LPF`, `IIR_16k8_LPF`) → `undefined reference` при сборке через Makefile/CI.
На GCC 10+ дополнительно мешают отсутствие `-fcommon` и `inline` без `static`.

**Исправление.**
- `files.mak`: добавлены **оба** отсутствовавших фильтра (`iir_2_8k.c` — после `iir_2_7k.c`,
  `iir_16k5_96ks.c` — после `iir_15k_hpf_fm_squelch.c`); теперь 46 записей = 46 `.c` в каталоге.
  Автосверка остальных списков (`f4-files.mak`, `bootloader.mak`, `f4-bootloader.mak`) других
  расхождений не выявила (отчёт — `_audit/dev/p-build.md`).
- Добавлен CLI-драйвер `tools/build.py`, читающий те же `*.mak` (набор файлов и флаги не расходятся
  с Makefile), `-fcommon` для GCC ≥ 10; исправлен разбор `.d`-зависимостей для Windows-путей.
- Блокеры GCC 15.2.1/binutils 2.45 (10 файлов исходников/заголовков, только типы/декларации/ассемблерные директивы):
  `profiling.h` (`inline`→`static inline`), `arm_bitreversal2.S` (`.type %function`),
  `audio_driver.c` (каст 16-битного USB-буфера), `ui_menu.c` (каст `bool*`→`uint8_t*`),
  прототипы 4 функций в `audio_driver.h`/`ui_driver.h`/`ui_spectrum.h`/`ui_menu.h`,
  `#include ui_menu.h` в `ui_configuration.c`, прототипы ILI9341/ILI7789 в `ui_lcd_hy28.c`.
  Полный разбор — [BUILD.md](BUILD.md) §4.1.

**Проверка (ПОДТВЕРЖДЕНО; историческая сборка GCC 15.2.1 — невалидна для железа).**
`python tools/build.py all` из чистого дерева: exit 0 за ~30 с, оба таргета — `fw-mchf.bin`
320 024 Б (32,6 % flash 960 КиБ, RAM 42 616 Б, CCM 63 476/65 536) и `bl-mchf.bin` 31 496 Б.
**Актуальная GCC 9.3.1 (main 34d28730):** default fw 312 216 / RAM 42 392 / CCM 63 476, bl all
32 416 (в пакете known-good 5.0.4 31 620), 50 предупреждений. Вторая сборка `--force` дала те же размеры и ту же
карту памяти (идентична после нормализации имён LTO-временных файлов); `bl-mchf.bin` побайтно совпал.
`warnings.log`: 80 строк — 67 предупреждений компилятора (64 уникальных текста) и 13 строк
линковки newlib-nano; все — существующие в коде Nizar/upstream, ни одного на строках product-патчей. Подробности,
секции, символы фильтров — [BUILD.md](BUILD.md) §3 и `_audit/dev/p-build.md`.

## P0-3 — Аудио

**Проблема (по аудиту §7.1).** A1: буфер шумопонижения `NR_dec_buffer` не инициализировался —
в ЦАП попадал мусор при старте/рестарте NR. A2: неопределённое поведение в TX-реверберации
(несколько обращений к изменяемой переменной без sequence point). A3: `abs()` на `float`
в 12 местах — неявное приведение к `int`, потеря/искажение значения.

**Исправление.** Инициализация NR-буфера (или байпас NR при `out_buffer == NULL`);
переписан фрагмент реверберации без UB; `abs()` → `fabsf()` только в float-местах.

**Проверка.** Диф минимальный; поведение NR/реверберации не изменено (кроме устранения мусора/UB);
сборка без новых предупреждений; компиляция и линковка файла в составе прошивки GCC 15.2.1
**подтверждена** (см. `_audit/dev/p-build.md`).

## P0-4 — Защита PA по КСВ

**Проблема (U3).** `swrm.vswr = (1 + sqrtf(rev/fwd)) / (1 - sqrtf(rev/fwd))` без проверки
`fwd_pwr`: при `fwd_pwr == 0` → `0/0` → NaN, при `rev > fwd` — отрицательный знаменатель.
`EEPROM_VSWR_PROTECTION_THRESHOLD` объявлен, но не используется — автоматического снижения
мощности при высоком КСВ нет (в upstream foldback появился в 2018-12).

**Исправление.** Guard: расчёт КСВ только при `fwd_pwr >= SWR_MIN_CALC_POWER` (иначе безопасное
значение); портирован upstream foldback PA по КСВ с гистерезисом. В v1.0 порог был константой
`VSWR_PROTECTION_THRESHOLD_DEFAULT = 3` в единицах КСВ, привязки к EEPROM/меню не было
(P1-backlog); **в v1.1 недоделка закрыта — см. P1-1 ниже**. Отключение защиты в v1.0 требовало
пересборки со значением <= 1.

**Проверка.** Нет деления на ноль/NaN; при имитации высокого КСВ мощность снижается, при
нормальном КСВ поведение TX не меняется; проверка на железе в эквиваленте нагрузки.
Компиляция/линковка в прошивку GCC 15.2.1 **подтверждена** (`_audit/dev/p-build.md`); на железе — S3.

**Обновлено в v1.1:** порог вынесен в EEPROM и меню (P1-1), добавлена индикация `SWR!` (P1-2),
убрано окно ~190 мс и retry-бурст (P1-3); см. разделы ниже.

## P0-5 — TX-безопасность

**Проблема (U4).** Запрет TX вне любительских бэндов фактически отсутствовал: пункт меню
закомментирован, проверка `FLAGS1_TX_OUTSIDE_BANDS` закомментирована, блок авто-запрета — под
незаданным `#ifdef TUNISIANN` (опечатка). Добавленный форком обработчик CAT FT-817 `SET_FREQ`
менял бэнд/LPF без проверки состояния TX: при перестройке из логгера/WSJT во время передачи
реле LPF переключались под мощностью.

**Исправление.** Ни одна CAT-команда не меняет бэнд/фильтр при `ts.txrx_mode == TRX_MODE_TX`;
рабочий программный запрет TX вне любительских диапазонов по флагу; пункт меню восстановлен.

**Проверка.** CAT-команды при TX не переключают бэнд/фильтр; попытка TX вне бэндов блокируется;
ручная смена бэнда при TX, как и раньше, защищена. Компиляция/линковка в прошивку GCC 15.2.1
**подтверждена** (`_audit/dev/p-build.md`); на железе — S3.

## P0-6 — Идентификация и документация

**Проблема (§6.3).** В форке `UHSDR_REPO` заменён на пустую строку, версия оставлена 2.9.48 —
сборка форка неотличима от официальной; продукт не описан, лицензионная атрибуция не оформлена.

**Исправление.**
`src/uhsdr_version.h`: восстановлен `UHSDR_REPO` (`github.com/df8oe/UHSDR`); добавлены
`UHSDR_PRODUCT_NAME` = `"RS918 N96K-Pro"`, `UHSDR_PRODUCT_VERSION` = `"2.9.48-n96kp1"`
(на момент v1.0), `UHSDR_PRODUCT_REPO` (пусто — публичного репозитория продукта нет).
`UHSDR_VERSION` остаётся производной: `"fwv-" UHSDR_PRODUCT_VERSION` — на экране «System Info»
отображается `2.9.48-n96kp1`. Числовая версия `UHSDR_VER_MAJOR/MINOR/RELEASE` = 2.9.48
**не изменялась** (её разбирает `mchf-eclipse/Makefile` и использует логика версий/EEPROM).
`UHSDR_BOOT_VERS` = 4.1.2 на момент v1.0 (в v1.1 обновлён до 5.0.4 — см. P2-1, а версия продукта
поднята до `2.9.48-n96kp2` — см. «Проверка документации и версии» в конце). README заменён на
продуктовый; добавлены `docs/PATCHES.md`, `docs/CHANGELOG.md`, `docs/SAFETY.md`,
`docs/CREDITS.md`. Код вне `uhsdr_version.h` не менялся, `LICENCE.md` не трогался.

**Проверка.** Препроцессинг заголовка компилятором arm-none-eabi-gcc: макросы синтаксически
корректны, `UHSDR_VERSION` = `"fwv-2.9.48-n96kp1"`, смещение `+4` даёт `"2.9.48-n96kp1"`
(13 символов, буфер меню 32); `atoi(UHSDR_VER_*)` возвращает 2/9/48 как раньше. `UHSDR_REPO` в
коде нигде не используется (grep).

## P1-H — Гигиена репозитория (в PLAN-DEV.md — P1-1)

**Проблема (U7).** Нет `.gitattributes` при требовании `.editorconfig` UTF-8/LF; `iir_2_8k.c`
в Latin-1/CRLF, `iir_2_9k.c` с битыми байтами; смешанные EOL → псевдо-диффы и риск порчи патчей.

**Исправление.** `.gitattributes` (`text=auto`, `eol=lf` для исходников/документации, binary для
артефактов); два файла фильтров перекодированы в UTF-8/LF; `.editorconfig` расширен на
документацию и скрипты.

**Проверка.** `git diff` без псевдо-изменений EOL; файлы фильтров читаются компилятором (только
комментарии/таблицы, значения не менялись).

## P1-1 — Порог VSWR в EEPROM и меню Configuration (R2)

**Проблема.** В v1.0 порог foldback был жёстко зашит константой `VSWR_PROTECTION_THRESHOLD_DEFAULT = 3`
в `radio_management.h`; слот `EEPROM_VSWR_PROTECTION_THRESHOLD` (426) был объявлен, но не входил в
ConfigEntry-таблицу, пункта меню не было. Изменить или отключить защиту без пересборки было нельзя;
на антеннах с высоким КСВ на краях диапазонов это давало ложные срабатывания, а документация v1.0
при этом утверждала, что порог в EEPROM (риск R2 ревью S3b; там же отмечено расхождение
документации с кодом).

**Исправление (порт из upstream 2.12.2 `dcbebe5f`, маркер `N96K P1-1`).**
- `hardware/uhsdr_board.h:838-843` — поле `uint8_t ts.vswr_protection_threshold` в ConfigData-части
  `TransceiverState` (на раскладку EEPROM не влияет: адреса задают только ConfigEntry).
- `drivers/ui/radio_management.h:210-218` — макросы `VSWR_PROTECTION_THRESHOLD_OFF = 1` (`MIN`),
  `MAX = 10`, `DEFAULT = 3`; внешняя глобальная переменная порога удалена.
- `drivers/ui/ui_configuration.c:350-353` — ConfigEntry `ConfigEntry_UInt8,
  EEPROM_VSWR_PROTECTION_THRESHOLD, &ts.vswr_protection_threshold, 3, 1, 10`.
- `drivers/ui/ui_configuration.h:616-619` — слот **426** задокументирован как используемый.
- `drivers/ui/menu/ui_menu_internal.h:371` — `CONFIG_VSWR_PROTECTION_THRESHOLD` добавлен **в конец**
  enum перед `MAX_RADIO_CONFIG_ITEM`: численные значения существующих `CONFIG_*`/`MENU_*` не изменились.
- `drivers/ui/menu/ui_menu_structure.c:121-123` — пункт `"VSWR Protection"` в меню **Configuration**
  (в upstream он в PA-меню; перенос — одна строка, если lead решит иначе).
- `drivers/ui/menu/ui_menu.c:2631-2646` — обработчик `UiDriverMenuItemChangeUInt8(1..10)`; значение 1
  показывается как `"OFF"`, 2..10 — числом.
- `drivers/ui/radio_management.c:1876,1913,1959,1969` — порог читается из
  `ts.vswr_protection_threshold`; при `<= 1` защита выключена, и если она была активна, PA отпускается
  (нет «залипания» mute).

**EEPROM-раскладка (доказательство отсутствия сдвига).** Скриптом сверены все `#define EEPROM_*`
рабочей версии и `git show HEAD` (v1.0-база): 464 = 464, ни один адрес не изменён/добавлен/удалён;
`EEPROM_FIRST_UNUSED = 524`, `MAX_VAR_ADDR = 523`, `NB_OF_VAR = 530` не тронуты; слот 426 в базе
свободен и теперь встречается в ConfigEntry-таблице ровно один раз; `config_ramcache[MAX_VAR_ADDR*2+2]`
не расширяется. Старые EEPROM: слот 426 ранее не записывался → мусор/0xFFFF отбрасывается
range-check'ом (1..10) и fallback'ом P1-4 → default 3; существующие настройки не сдвигаются.
(Позже, в P2-4, добавлены ещё слоты 430 и 524 — существующие адреса при этом тоже не сдвинуты;
`EEPROM_FIRST_UNUSED` 524 → 525, см. P2-4.)

**Проверки.** `-fsyntax-only` по флагам `release/v1.0/compile_commands.json` (GCC 15.2.1):
`radio_management.c`, `ui_configuration.c`, `ui_menu.c`, `ui_menu_structure.c`, `ui_driver.c` — rc=0,
новых предупреждений на добавленных строках нет. Приватная полная сборка — exit 0. Host-тесты:
`eeprom` 425 проверок PASS на момент P1-1 (после слотов P2-4 — 426; `MAX_VAR_ADDR < NB_OF_VAR`,
уникальность адресов, guard, ссылки).

**Риски/ограничения.** Default 3 ≠ upstream default 1 (OFF): по требованию задачи защита включена
по умолчанию — для обновления с v1.0 поведение меняется; пользователь может поднять порог или
выключить. Пункт меню — в Configuration, а не в PA (в upstream — PA). Вид на реальном LCD не проверен.
На некалиброванном датчике КСВ возможны ложные срабатывания.

## P1-2 — Индикация срабатывания foldback (R4)

**Проблема.** Флаг `swrm.high_vswr_detected` писался при срабатывании и сбрасывался при release/reset,
но нигде не читался: пользователь видел только падение мощности без причины.

**Исправление (маркер `N96K P1-2`, `drivers/ui/ui_driver.c`).**
- `2133-2134` — файловый флаг `SWR_Indicator_Drawn`.
- `4826-4866` (в `UiDriver_HandleTXMeters()`, TX-ветка) — в боксе уровня мощности `ts.Layout->PW_IND`
  (тот же, где штатно `UiDriver_DisplayPowerLevel()`) мигает красное `"SWR!"` на синем фоне: фаза
  меняется каждый 4-й вызов обработчика (метры обновляются каждые 40 мс → ~3 Гц), чётная фаза
  возвращает штатное значение мощности. Другие индикаторы (S-метр, ALC, AUD, полосы) не затронуты.
- Сброс: при выходе из foldback (`4858-4864`; release очищает `high_vswr_detected`) и в RX-обработчике
  `UiDriver_HandleSMeter()` (`4746-4750`) — после TX→RX бокс перерисовывается штатно, `"SWR!"` не
  остаётся на RX-экране.

**Проверки.** `-fsyntax-only` rc=0; полная сборка exit 0; других писателей `high_vswr_detected` нет
(grep: только release/reset в `radio_management.c`). На железе вид/координаты не проверялись
(расчётно 4 символа font 0 в боксе w=41 совпадают с `"0.5W"`).

**Риски.** Пока защита активна, в боксе PWR виден минимальный power factor (PWR 0.5 W), затем
штатное значение — ожидаемое поведение; индикация не заменяет разбор причины высокого КСВ.

## P1-3 — Быстрый foldback: свои счётчики, безопасный retry, независимость от dynamic tune (R1/R3/R5)

**Проблема.** В v1.0 защита просыпалась по счётчику `ts.dynamic_tune_activ_counter` (порог `>= 2`):
первые ~190 мс TX шли на полной мощности без защиты (R1); любое вращение ручки в TX ставило счётчик
35/42/48 → защита глушилась до ~480 мс (R5); retry включал bias, не проверяя фактор мощности — при
ручной регулировке PWR возможен бурст полной мощности до ~50 мс (R3).

**Исправление (маркер `N96K P1-3`, `drivers/ui/radio_management.c/.h`).**
- Константы (`74-85`): `VSWR_PROTECTION_ARM_CNT = 3` (тики измерений по 10 мс ≈ 30 мс),
  `VSWR_PROTECTION_RETRY_CNT = 8` (~0,5 с), `VSWR_PROTECTION_RECOVERY_CNT = 10` (~0,6 с),
  гистерезис 0,5.
- Состояние (`1799-1808`): `vswr_foldback_active`, `vswr_foldback_trial`, `vswr_foldback_retry`,
  `vswr_foldback_recovery`, `vswr_settle_cnt`, `vswr_foldback_avg`.
- `RadioManagement_UpdatePowerAndVSWR()` (`2013-2021`) инкрементирует `vswr_settle_cnt` на каждом
  10-мс тике (насыщение 255) — собственный счётчик, не связанный с dynamic tune; обнуляется в
  `VswrFoldbackReset()` (`1861`), т.е. на каждый новый TX.
- `RadioManagement_CheckVswrProtection()` (`1864-1980`): guard'ы (не TX / TX-переход / `ptt_req` →
  нет переключений); ожидание `vswr_settle_cnt < 3`; измерение валидно только при
  `swrm.fwd_pwr >= SWR_MIN_CALC_POWER` (0,25 Вт); trip — `DisablePaBias()` +
  `SetMinimalTxPowerFactor()` (PA_LEVEL_0_5W; пользовательский `ts.power_level` не портится) +
  `high_vswr_detected = true`; в foldback на **каждом** измерении повторно выставляются bias off и
  минимальный drive (ручка PWR не возвращает полную мощность); recovery-trial раз в 8 измерений
  включает bias **только на минимальном drive**, при высоком КСВ/невалидном измерении немедленно
  прерывается; release — только после **10 подряд** хороших измерений ниже (порог − 0,5) на
  минимальной мощности, затем восстанавливаются bias и пользовательский power factor.
- Зависимость от `ts.dynamic_tune_activ_counter` удалена из `radio_management.c` (grep: обращение
  осталось только в отрисовке SWR-метра `ui_driver.c:4870` — Nizar-дисплей, намеренно не тронут).

**Тайминг (честно).** Одно полное измерение = `SWR_SAMPLES_CNT = 5` пар АЦП + расчёт ≈ 60 мс
(константа Nizar не менялась). «Свои 10-мс счётчики» дают settle-окно ~30 мс, а первая защитная
реакция физически возможна только на первом полном измерении — **~60 мс вместо ~190–240 мс
(ускорение ≈ 3×)**. После восстановления нагрузки защита держит минимальную мощность до ~1,1 с
(8 retry-измерений + 10 подтверждающих) — осознанный запас; при полевых жалобах
`RETRY`/`RECOVERY` можно уменьшить.

**Проверки.** Модель состояния: скриптовая транслитерация `CheckVswrProtection()` /
`UpdatePowerAndVSWR()` на 8 сценариях (хорошая антенна; КЗ с начала TX; восстановление нагрузки;
ручка PWR в foldback; зона гистерезиса; выключение защиты; TX-переход; dynamic tune) — инвариант
«полная мощность при bias ON во время foldback» = **False** во всех. Отсутствие OOB/деления на 0:
новых индексаций и делений не добавлено, guard P0-4 сохранён. Сборка exit 0; `-fsyntax-only` rc=0.

**Риски.** 60 мс — физический минимум при 5 выборках; ускорять дальше = менять метр Nizar или
быстрый путь по сырым выборкам (риск ложных срабатываний на фронте TX) — не делалось. «Залипание»
на минимальной мощности до ~1,1 с. На железе с эквивалентом/КЗ не проверялось.


## P1-4 — Fallback при ошибке чтения EEPROM (R7)

**Проблема.** В `UiReadSettingEEPROM_*` при `ConfigStorage_ReadVariable() != 0` не было ветки `else`:
вызывающий читал неинициализированную локаль (`value16` в `UiReadSettingsBandMode`/`LoadEepromValues` →
`ts.band`, `ts.dmod_mode`, `df.tune_new`). Клампы P0-1 ограничивали последствия, но формально это UB.

**Исправление (маркер `N96K P1-4`, `drivers/ui/ui_configuration.c:411-539`).** Порт 1:1 из upstream
2.12.2: во все 7 функций добавлена ветка `else { *val_ptr = default_val; }` —
`UiReadSettingEEPROM_UInt8` (411-418), `..._UInt8x2` (436-443 — именно `value16`), `..._UInt16`
(455-462), `..._Int16` (473-480), `..._UInt32_16` (492-499), `..._Int32_16` (510-517), `..._UInt32`
(532-539). Ветка успешного чтения не изменена ни на строку (клампы/маски P0-1 на месте);
`UiReadSettingEEPROM_Bool` остаётся закомментированной, как в upstream.

**Проверки.** Полная сборка exit 0; при успешном чтении поведение идентично v1.0; UB-чтение
`value16` закрыто (по коду/grep ветка есть во всех 7 функциях).

## P1-5/CAT — Лазейки R8 и эмуляция 0x65 (FT-817/Hamlib 4.3)

**Проблема (R8).** Гейт P0-5 закрывал `SET_FREQ`, `MODE_SET`, `TOGGLE_VFO`, `SPLIT`, но оставались
два пути смены band/частоты в обход проверки TX: (1) FT-817 `EEPROM_WRITE 0xbc, addr 0x55` —
VFO-select вызывает `UiAction_ToggleVfoAB()` и меняет активный VFO/частоту/band/фильтры; (2) clone-in
block 8 писал `df.tune_new`, главный цикл применял частоту → `SetHWFiltersForFrequency` →
`Board_SelectLpfBpf` под мощностью.

**Исправление (маркер `N96K P1`, `drivers/cat/cat_driver.c`; +27/−6).**
- `559-568` — VFO-select в EEPROM-эмуляции выполняется только при
  `CatDriver_IsChangeBandModeAllowed()` (тот же хелпер P0-5: `!TX && !tune && !ptt_req`); при TX
  ответ всё равно ACK `0x00`, переключение не применяется.
- `782-790` — clone-in block 8: блок принимается и ACK-ается (`0x06`), но `df.tune_new` не
  присваивается, пока запрещено; клонирование не ломается.
- `603-609` и `658` — добавлена эмуляция EEPROM `0x65` из upstream `b71ccd98` (#1594, требуется
  WSJT-X): `EEPROM_READ 0x65` отдаёт 2 байта `[USER-L/USER-U (0x60/0x80), 0]` как реальный FT-817;
  write-ветка не делает ничего, как в upstream (маркер `N96K P2-1` в коде — см. примечание о
  нумерации в шапке).
- Read-путь (`0x54`/`0x55`) и PTT не блокируются; внутрибэндовый `SET_FREQ` в TX сохранён (WSJT-X
  Fake It).

**ВАЖНАЯ ПОПРАВКА к аудиту.** Утверждение `_audit/parts/upstream-delta.md` (§4 п.5-7, §6 п.5) и
`_audit/REPORT.md` о том, что фиксы Hamlib 4.3 `get_vfo` **отсутствуют**, — **неверно**: все три
уже были в базе Nizar (проверено по исходной архивной папке и `git show HEAD`):
- `5a9f9821` (get_vfo Hamlib 4.3.x) — запись `0x54 = 0xFF` в `ft817_eeprom[]` есть (архив стр. 629,
  HEAD стр. 641);
- `2c9e3d6d` (FT817EE_FUNC → FT817EE_DATA) — в форке уже `DATA` (финальное состояние);
- `1ebbdd87` (#1891 WINTEST) — `PTT_STATE 0xF7` в RX возвращает `0xff`, бита `0x80` нет (архив
  стр. 1205-1207, HEAD стр. 1237-1239).
Портить для `get_vfo` было нечего: read-handler на `0x54` возвращает 2 байта `[0xFF, 0x80/0x81]`
(после чтения `0x54` дочитывается `addr+1 = 0x55`) — это и есть контракт `get_vfo`. Единственным
реальным остатком FT-817-эмуляции 2.12.2 был `0x65`; он и портирован.

**Проверки.** `-fsyntax-only` с точными флагами `compile_commands.json` на чистом дереве HEAD —
rc=0, вывод пуст. Приватная сборка из чистого HEAD+патч — exit 0, `fw-mchf.bin` 320 136 Б
(+112 Б к v1.0 = 0,011 %), CCM 63 476/65 536 без изменений, 70 предупреждений (как в v1.0-классе),
0 в `cat_driver.c`. Байтовая целостность: UTF-8/LF, 2 предсуществующих U+FFFD (стр. 1065) сохранены,
новых не добавлено.

**Риски.** С Hamlib/WSJT-X/HRD и на железе не проверялось. «Тихий ACK» при TX для VFO-select:
модель состояния логгера может разойтись до следующего опроса (то же решение, что в P0-5).
`EEPROM_WRITE > 0x7FFF` во время TX остаётся разрешённым — runtime-состояние не меняется
(`config_storage.c`), но возможны износ/задержка flash (P2-остаток). Гонка «SET_FREQ в RX →
немедленный PTT» — вне `cat_driver.c` (отмечена в P0-5). `0x65` теперь отдаёт 2 байта вместо 1 —
как реальный FT-817/upstream, аддитивно.

## P2-1 — Загрузчик UHSDR 5.0.4

**Проблема.** v1.0 работал на загрузчике 4.1.2 (UHSDR 2.9.48). Upstream 2.12.2 содержит 5.0.4:
переименования API, «чистый» переход в прошивку/DFU, исправления сброса MCU. Обновление важно для
надёжности старта прошивки и совместимости с upstream.

**Исправление (маркер `N96K P2`, 8 файлов из upstream 2.12.2 `dcbebe5f`, суммарно +164/−103 строк).**
`bootloader_main.c/.h`, `command.c/.h`, `flash_if.c/.h`, `uhsdr_boot_hw.c/.h`. 6/8 файлов байт-в-байт
с upstream (mod EOL); 2 файла — upstream + адаптации: `bootloader_main.h` (+7 строк) — псевдонимы
старых точек входа (`bootloader_main`, `BL_Application`, `mchfBl_CheckAndGoForDfuBoot`), т.к.
`basesw/mcHF/Src/main.c` вне зоны; `bootloader_main.c` (+10 строк) — сохранён признак SBLA/SBLS в
SRAM2 (upstream 5.0.4 его удалил, но прошивка 2.9.48 читает `SRAM2_BASE+5 == 0x29` в
`drivers/audio/freedv_uhsdr.c:464`). `UHSDR_BOOT_VERS` 4.1.2 → **5.0.4**.
Поведение 5.0.4: `UhsdrBl_JumpToApplication()` (отключение IRQ, `HAL_SuspendTick()`,
`HAL_RCC_DeInit()`, сброс NVIC) перед прыжком в прошивку/DFU; enum `BOOT_REBOOT = 0x55`,
`BOOT_DFU = 0x99`, `BOOT_FIRMWARE = 0x66993300` — «проводные» значения 0x55/0x99 совместимы с
прежним протоколом; `__DSB()` перед сбросом MCU; `BootFail_Handler(3)` и валидация прошивки.

**Проверки.** `python tools/build.py bootloader --build-dir …` — exit 0 (116 units, 7,2 с):
`bl-mchf.bin` **31 620 / 32 768 Б = 96,5 %**, запас **1 148 Б**; `.text` 31 488 @ 0x08000000,
`.data` 132, `.bss` 39 568, `.ccm` 0; строка `Version: 5.0.4` в `.bin` (offset 27 503), `4.1.2`
отсутствует; карта `rom 0x08000000 length 0x8000`; SHA-256 `bl-mchf.bin` =
`b1fe331c02cf7cf2b36b03e2e996d58f9619ea765980ba76dc28ccf40d3d9b9a`. Рост +124 Б к 4.1.2
(31 496 Б). Прошивка с новым заголовком собирается; `-fsyntax-only` с `-DSBLA`, `-DSBLS`,
`-DSBLA -DSBLS` — OK. 8 предупреждений — все предсуществующие (USB/LL, LCD, newlib).

**Ограничения/остаётся.** Загрузчик **не прошивался** и на железе не испытывался: экран/кнопки,
USB-диск (дамп/запись `fw-mchf.bin`), вход в DFU (BAND+) проверены только сборкой. `release/v1.0`
не пересобирался (там старый bl 31 496); артефакты 5.0.4 — в `_tmp/build-bl`; в `release/v1.1`
включать после аппаратной проверки. Загрузчик 4.1.2 с этой прошивкой совместим.

## P2-2 — FreeDV/Codec2 rev 4121 + фикс #1749

**Проблема.** Vendor-дерево `drivers/freedv/**` соответствовало Codec2 времён 2.9.48; в upstream
2.12.2 — rev 4121, плюс фикс #1749 («FreeDV crashes on mcHF/STM32F4», upstream `750551fc`):
в Makefile не хватало `-DFDV_ARM_MATH` (оптимизация Codec2 через CMSIS-DSP).

**Исправление (маркеры `N96K P2`).**
- Vendor-дерево приведено к upstream `dcbebe5f` **побайтово**: **56 M / 14 A / 4 D**, итого 193 файла
  (192/193 совпадают по git-blob; единственное отличие — локальный compat-блок в `freedv_api.h`).
- `files.mak`: +`codebooknewamp2.c`, +`codebooknewamp2_energy.c`, `fifo.c`→`filter.c`,
  +`HRA_112_112.c`, +`HRAb_396_504.c`, +`newamp2.c`, `octave.c` удалён, +`phi0.c`.
- `-DFDV_ARM_MATH` добавлен в `tools/build.py` (`COMPILEFLAGS_BASE`) и `mchf-eclipse/Makefile`
  (`COMPILEFLAGS`) — это и есть фикс #1749.
- `drivers/freedv/freedv_api.h` (+25 строк, `N96K P2`): compat-блок `#ifdef __MCHF_BOARD_H` с
  `#undef I/bool/true/false` — новые заголовки Codec2 тянут `<complex.h>`/`<stdbool.h>`, а база
  2.9.48 использует `typedef int bool` и члены с именем `I`; без блока сборка падает.
- **Интеграционный слой Nizar НЕ пересинхронизирован** (осознанно): `drivers/audio/freedv_uhsdr.c/.h`
  остались на API 2.9.48; новый API 2.12.2 требует портирования `audio_driver.c`,
  `radio_management.c`, `ui_menu.c`. Старая обвязка компилируется и линкуется с новым vendor-кодом;
  режимы/96k/12k/callback-сообщения Nizar не тронуты.

**Проверки.** Полная приватная сборка — exit 0 (533 объекта; новые собраны, fifo/octave исчезли);
`fw-mchf.bin` 320 136 Б (+112 Б = +0,035 % к v1.0), CCM 63 476 — байт-в-байт как baseline;
предупреждений 78 (в v1.0 — 80), новых классов нет.

**Ограничения/follow-up.** Runtime (FreeDV TX/RX) не проверялся — только компиляция/линковка.
Mode-флаги 2.12.2 F4-конфига (`-DFREEDV_MODE_EN_DEFAULT=0` и др.) не добавлены. NR+FreeDV фиксы
2.12.2 (`df9ef3f9` — тишина после FreeDV+NR, `27945d8b` — запрет NR/NB при FreeDV) не портированы:
в продукте более старая реализация `audio_nr.c`, патчи не ложатся аддитивно — отдельная задача.
Новый API (700D, squelch, выбор режима из меню) не включён. Compat-блок нужно переносить при
следующем обновлении Codec2. Vendor-файлы в рабочем дереве CRLF (git нормализует при коммите).

**Корректировка статуса (v1.2, по `_audit/dev/pa-nrfreedv.md`).** **FreeDV в данной сборке
не включён** — это само по себе важное ограничение, а не «ещё не проверено на железе». Продукт
собирается с `IS_SMALL_BUILD` (`hardware/uhsdr_board.h:70`), поэтому `USE_FREEDV` не определён
(`uhsdr_board.h:114-117`) — как и в исходном форке Nizar; FreeDV-функций в прошивке нет, режим
недоступен из UI. Vendor-дерево FreeDV действительно обновлено до rev 4121, но это только исходники
(`drivers/freedv/**`): к vendor-фиксам NR+FreeDV (A2) добавлены гейты upstream-parity. Формулировка
v1.1 «FreeDV/Codec2 rev 4121 + #1749» относилась к vendor-коду, а не к работающей в прошивке функции.
**Включение FreeDV — отдельная задача**: требует `-DUSE_FREEDV` и проверки **пре-существующего
рассинхрона guard'ов** `audio_driver.c:662-723` (определение `IIR_biquad_FreeDV_I/Q` только при
`!IS_SMALL_BUILD`) vs `audio_driver.c:2434-2435` (использование под `USE_FREEDV`).

## P2-3 — CI/Docker с фиксированным тулчейном + host-тесты ЦОС

**Проблема (аудит §9).** Сборка зависела от локального Eclipse/тулчейна, не было автоматического
контроля бюджета flash/RAM/CCM и хостовых проверок ЦОС-таблиц; предупреждения компилятора не
фиксировались.

**Исправление.**
- `Dockerfile` (83 строки) — 2 стадии: pinned xPack **GNU Arm Embedded GCC 9.3.1-1.3** (URL +
  SHA-256 `4845d261b000d921887fc801427542eed2df1616f63a2969a2640f8be2593686`; до 2026-11-27 был
  15.2.1-1.1 — его прошивки на железе зависают); `python3 tools/build.py all` + тесты + проверка
  артефактов; `docker/verify_artifacts.py` reference **312 216**.
- `docker/verify_artifacts.py` (180 строк) — наличие/размеры артефактов, бюджеты flash/RAM/CCM,
  запись `SHA256SUMS.txt`.
- `.github/workflows/build.yml` — job `host-tests` (python) + `docker-build` (buildx, gha-кэш,
  `docker cp`, upload artifacts).
- `docs/CI.md` — как собирать в Docker, как гонять тесты, что проверяет CI, ограничения.
- `tests/**` — ~2,4 тыс. строк только на stdlib: `run_all.py`, `cparse.py` (мини-парсер C),
  `checkbase.py`, `check_filters.py`, `check_iq.py`, `check_files_mak.py`, `check_versions.py`,
  `check_eeprom.py`, `check_audio.py`, `known_issues.py`, `README.md`.

**Проверки (host).** `python tests/run_all.py` — exit 0: **1133 проверки** на момент P2-3
(после слотов P2-4 — **1134**, 0 failed, 3 known, 0 skipped); `filters` 490, `iq` 176,
`files.mak` 4, `version` 12 (включая 2.9.48 и `UHSDR_BOOT_VERS = 5.0.4`), `eeprom` 425 (426 после
P2-4), `audio_instances` 26. Проверки: (a) FIR-дескрипторы, (b) IIR-lattice, (c) IQ-таблицы
Nizar, (d) все 46 `.c` фильтров в `files.mak`, (e) версия/продукт/загрузчик/лицензия/атрибуция,
(f) карта EEPROM (`MAX_VAR_ADDR < NB_OF_VAR`, уникальность адресов, guard).
`verify_artifacts.py` проверен на реальных артефактах v1.0 — exit 0 (и негативный тест — exit 1).

**Известные KNOWN-дефекты (не блокируют, задокументированы в `tests/known_issues.py`).**
`iir_3k.c:IIR_3k` — `numStages = 10`, а `pkCoeffs` 11 элементов (мёртвый код); `audio_driver.c`
`IIR_biquad_Zoom_FFT_I/_Q` — placeholder-инициализатор (10 вместо 20 `pCoeffs`; заменяется в runtime
до первого запуска, но латентный UB/OOB-read).

**Ограничения (на момент P2-3).** `docker build` **не запускался** (на машине разработки нет
Docker); GitHub Actions на момент P2-3 не запускался (не было remote/раннера); проверены только статически (YAML/PyYAML, разбор
Dockerfile, `verify_artifacts.py` на артефактах v1.0). Первая сборка в Docker качает ~307 МБ; образ
`ubuntu:22.04` прибит тегом, не digest. Прошивка не воспроизводима побайтно (`__DATE__/__TIME__`) —
CI контролирует размеры/бюджеты/наличие, а не SHA. Бюджет-гейт bl (32 768): known-good 5.0.4 —
31 620 (запас 1 148), GCC9 all-сборка — 32 416 (запас 352; следующий рост кода загрузчика может
не влезть).

**Обновление GCC 9.3.1 (2026-11-27, main 34d28730).** Причина зависания прошивок на железе
найдена: GCC 15.2.1 (splash → кратковременно UI → зависание, оба LED); GCC 9.3.1 работает.
Dockerfile/CI переведены на xPack GCC 9.3.1-1.3, baseline предупреждений — 50 (16 текстов),
reference — 312 216; живой прогон Actions с новым тулчейном — при следующем push (локального
Docker нет).

**Обновление 2026-11-27 (B1/live):** Actions запущены и зелёные (`host-tests` 17 с, `docker-build`
5 мин 39 с, job `release` проверен на временном теге; см. docs/CI.md §5.2); Docker на машине
разработки по-прежнему отсутствует.

## P2-4 — Бэкпорты AGC #1730, Tune Power #1750, CW-decoder #1881

**Проблема.** В базе 2.9.48 отсутствовали исправления upstream, важные для повседневной работы:
после перезагрузки не восстанавливалась часть AGC-настроек (#1730, в частности «Clarity»/pop ratio),
Tune Power не выставлялся и не восстанавливался, если отличался от текущей TX-мощности (#1750),
не сохранялась конфигурация CW-декодера (#1881).

**Исправление (порт upstream 2.12.2; маркер `N96K P2`; статус — порт выполнен).**

| Upstream SHA | Тема | Что сделано в продукте |
|---|---|---|
| `cfbaae73` | AGC values are not restored (#1730) | В форке AGC WDSP реализован в `audio_driver.c` (а не в `audio_agc.c`), и upstream-дефект «инициализация затирает загруженные EEPROM-значения» структурно отсутствует — параметры живут в `ts.agc_*` и грузятся до `AudioDriver_Init()`. Добавлено: EEPROM-слот **524** для AGC «Clarity» (`ts.agc_wdsp_pop_thres` — единственный AGC-параметр меню, который не сохранялся), пересчёт производного `ts.float_agc_wdsp_pop_thres` после загрузки EEPROM и в `AudioDriver_SetupAgcWdsp()`, регресс-комментарий по `cfbaae73`. `EEPROM_FIRST_UNUSED` 524 → 525. |
| `684a04c1` + `6d7efe50` + `de6b5e0d` + `eb40e0dc` | Tune Power #1750 | `radio_management.c`, `RadioManagement_Tune`: статический `tune_power_overridden` — восстановление не зависит от текущего значения пункта меню Tune Power; при выходе из Tune восстанавливаются `ts.power_level`, `tx_power_factor` и `current_pwr_adj/PWR`; при отказе TX — откат override. Nizar dynamic tune/`current_pwr_adj` и код VSWR P0-4/P1 не тронуты. |
| `528aa185` | CW Decoder Config Save (#1881) | `cw_decoder.c/.h` (union/bitfields флагов, `CW_DECODER_FLAGS_DEFAULT`), `ui_menu.c` (временные `bool` для битфилдов), `ui_configuration.c/.h`: `ConfigEntry_UInt16 EEPROM_CW_DECODER_FLAGS = 430` — тот же ID, что в upstream 2.12.2 (бывший `EEPROM_FUTURE_RESERVE0`), существующие слоты не сдвигаются, `EEPROM_FIRST_UNUSED` не менялся. Nizar-настройка `show_CW_LED` остаётся в `FLAGS2` (`EEPROM_FLAGS2`), бит в `flags` синхронизируется через union. |

**EEPROM.** Существующие адреса **не сдвинуты**: 430 был зарезервирован и теперь используется
(CW flags), 524 — новый слот (AGC Clarity); `EEPROM_FIRST_UNUSED` 524 → 525,
`MAX_VAR_ADDR` 523 → 524; `NB_OF_VAR` = 530 не менялся. На старых EEPROM новые поля читаются
как мусор → range-check/default (P1-1/P1-4). Раскладка продукта уже не совпадает с upstream
2.12.2 (`EEPROM_FIRST_UNUSED` 525 против 432 у upstream) — совместимость только внутри продукта.

**Проверки.** `-fsyntax-only` 5 TU — OK; приватная сборка
`python tools/build.py firmware --build-dir _tmp/build-p2-backports` — exit 0, `fw-mchf.bin` =
**321 128 Б** (+304 Б к сборке до P2-4; +0,345 % к v1.0), CCM 63 476 — идентично baseline,
предупреждений 78 — как без P2-4; `tests/run_all.py` — PASS: **1134 проверки, 0 failed, 3 known**,
eeprom — **426 проверок** PASS (карта EEPROM, адреса 430/524 уникальны,
`MAX_VAR_ADDR = 524 < NB_OF_VAR = 530`). Полный отчёт — `_audit/dev/p2-backports.md` (вне
репозитория); финальная сборка v1.1 выполнена (тег v1.1, артефакты в `release/v1.1`).

**Риски/ограничения.** Runtime на железе не проверялся: восстановление AGC после ребута, Tune Power
и сохранение CW-настроек подтверждены код-трассировкой сценариев, сборкой и host-тестами. Новые
EEPROM-поля: при откате на v1.0 настройки Clarity/CW-флагов будут проигнорированы старым кодом
(безопасная деградация — default). Раскладка EEPROM по-прежнему несовместима с другими сборками
UHSDR. Это последняя серия бэкпортов v1.1; EEPROM signature #1675 и DMA size #1664 не переносились.

## A1 — Минимальный drive при активном VSWR-foldback (P2 F2 / P3 F6 из S3c)

**Проблема (F2, P2).** Инвариант «при активном foldback нельзя вернуть полную мощность» из P1-3 был
нестрогим: смена уровня мощности в TX (PWR-энкодер, пункт меню band power adjust, CAT, Tune) снова
вызывала `RadioManagement_SetBandPowerFactor()` с пользовательским `ts.power_level`, и в состоянии
recovery-trial (PA bias **включён**, драйв минимальный) на время до следующего измерения (~50–60 мс)
в рассогласование уходил полный драйв. Симуляция реального кода воспроизводит это на baseline:
`factor=0.55, bias_on=1`.

**Проблема (F6, P3).** Trial, завершавшийся внутри зоны гистерезиса `[порог − 0.5; порог]`, не
прерывался: PA оставалась забиасенной на минимальном драйве неограниченно долго (лишний нагрев и
непрерывный минимальный сигнал) — расхождение с отчётом автора P1-3.

**Исправление (маркер `N96K A1`; +42/−5, 2 файла).**
- `radio_management.c:1317-1333` — в начале `RadioManagement_SetBandPowerFactor()` вычисляется
  `effective_power_level`: если `ts.txrx_mode == TRX_MODE_TX` и `RadioManagement_IsVswrFoldbackActive()`,
  уровень принудительно заменяется на `PA_LEVEL_MINIMAL` (0.316 в rescale; 0.1738 при `adj=110`).
  `ts.power_level` **не модифицируется** — сохраняется запрос пользователя, а release заново вызывает
  `SetBandPowerFactor()` уже с ним. Замена применена во всех трёх местах применения фактора
  (out-of-band ветка, выбор таблицы `ts.pwr_adj`, `switch (effective_power_level)` уровня); вне
  foldback поведение бит-в-бит прежнее (`effective_power_level == ts.power_level`).
- `radio_management.c:1841-1852`, `.h:251-253` — публичный
  `RadioManagement_IsVswrFoldbackActive()` (возвращает `vswr_foldback_active`; во время trial флаг
  тоже TRUE). `SetBandPowerFactor()` — единственная функция, пишущая `ts.tx_power_factor` (grep:
  запись только здесь; init — `uhsdr_main.c`), поэтому других обходных путей смены драйва нет.
- `radio_management.c:2029-2035` — ветка «внутри гистерезиса» теперь вызывает
  `RadioManagement_DisablePaBias()`, сбрасывает `vswr_foldback_trial/retry/recovery` и возвращается
  в ожидание; следующий trial начнётся через `VSWR_PROTECTION_RETRY_CNT` (≈0,5 с) — как после
  «плохого» trial. Константы (ARM/RETRY/RECOVERY, гистерезис 0.5) и fast-attack/slow-decay не менялись.

**Проверки.** `-fsyntax-only` по флагам `release/v1.1/compile_commands.json` — rc=0, новых
предупреждений на добавленных строках нет. Изолированная сборка (git archive HEAD + только патч):
**FLASH 321 128 → 321 336 Б (+208 Б)**, RAM/CCM без изменений, набор предупреждений идентичен.
Симуляция **реального кода** продукта: функции `RadioManagement_PowerLevelChange()`,
`RadioManagement_SetBandPowerFactor()`, вся state machine `RadioManagement_CheckVswrProtection()` и
статики дословно вырезаны из `radio_management.c`, скомпилированы arm-none-eabi-gcc для Cortex-M4
и выполнены в эмуляторе Unicorn одним и тем же драйвером сценариев для patched/baseline:
**patched 58/58 PASS, baseline 7/58 FAIL** — ровно F2/F6 (полный драйв при bias ON в trial; trial с
bias ON в гистерезисе; прерванный recovery). Release/OFF/TX-переходы/20 циклов «9 хороших + 1
плохое» — без регрессий; `ts.power_level` после release восстанавливается (S7: 2 Вт → фактор 0.2846).
Артефакты — `_audit/dev/pa-vswr.md`, `_tmp/sim-pa-vswr/` (вне репозитория).

**Ограничения.** Аппарата нет: **F1 (ложные trip на фронте TX/после dynamic tune) остаётся** и
требует стенда — A1 его не закрывает. Симуляция моделирует кадэнс измерений (10 мс × 5), а не
аналоговый тракт. Host-тесты код VSWR не покрывают (как и раньше).

## A2 — NR+FreeDV: сброс NR после FreeDV и bypass NR при активном FreeDV (#1749)

**Проблема (F3, P2).** Upstream #1749 («AudioNr fix for silence after using FreeDV and NR») состоит
из трёх фиксов; в v1.1 был портирован только `750551fc` (`-DFDV_ARM_MATH`). NR и FreeDV делят
`MultiModeBuffer` union (`mmb`): после работы FreeDV в NR-буферах остаются не-аудио значения
(вплоть до NaN для следующих IIR-фильтров), а при активном FreeDV NR писал в тот же union и мог
портить IQ-буферы. Симптом — тишина/NaN после сценария «FreeDV → NR».

**Исправление (маркер `N96K A2`; upstream `df9ef3f9` + `27945d8b`; +58/−2, 3 файла).**
- `audio_nr.c:249-254`, `audio_nr.h:108-110` — `AudioNr_Prepare()`: сброс
  `NR_current_buffer_idx`/`NR_was_here` (перенесены из `alternateNR_handle()` на file scope, чтобы
  функция могла их сбросить; RAM/CCM не растут) и `memset` всего общего union
  (`NR_BUFFER_NUM * sizeof(mmb.nr_audio_buff[0])` = 8192 Б; покрывает и IQ-буферы FreeDV).
- `audio_driver.c:1511-1522` — вызов `AudioNr_Prepare()` из `AudioDriver_SetRxAudioProcessing()`
  при выходе из `DEMOD_DIGI`. Upstream вызывает из `RadioManagement_SetDemodMode()`, но в этом
  продукте она вызывает `AudioDriver_SetRxAudioProcessing()` **до** записи нового `ts.dmod_mode`,
  поэтому старый режим внутри функции ещё виден — это эквивалентный хук и единственный путь смены
  режима (CAT: `cat_driver.c:1191` → `RadioManagement_SetDemodMode()` — покрыт).
- `audio_driver.c:4358-4366` и `audio_nr.c:258-268` — ранний pass-through NR при
  `ts.dmod_mode == DEMOD_DIGI && ts.digital_mode == DigitalMode_FreeDV` (под `#ifdef USE_FREEDV`,
  как в upstream). Второй гейт (в `alternateNR_handle()`, PendSV/UI-путь) — **дополнительно к
  upstream**: в этом продукте именно он (при активном NR) пишет NR-выход в общий union из PendSV и
  без гейта ломал бы IQ-буферы FreeDV.
- NB — общий буфер не использует (`AudioDriver_NoiseBlanker_int()` работает со своим
  `delay_buf[32]`), гейт не нужен. `f365c8a4` (H7 mute/memset) уже закрыт более сильной
  инициализацией P0-3 (`audio_driver.c:4443-4447`); `6e73b644` (UI Nizar, «NR min 1») не делался.
- Nizar-код не тронут: алгоритм NR, 96k/12k-тракт, `NR_dec_buffer`, фильтры, UI — без изменений.

**Проверки.** `-fsyntax-only` в боевой конфигурации и с `-DUSE_FREEDV` (`audio_nr.c`,
`freedv_uhsdr.c`) — exit 0; `-DUSE_FREEDV audio_driver.c` — только 2 **пре-существующие** ошибки
`IIR_biquad_FreeDV_I/Q undeclared` (`audio_driver.c:2434-2435`). Изолированная сборка: FLASH
321 128 → 321 176 Б (**+48 Б**), RAM/CCM — 0. Бинарные проверки: дизассемблер хука в
`AudioDriver_SetRxAudioProcessing` (сброс индексов + `memset` 8192 Б), статические assert'ы
`sizeof(MultiModeBuffer_t) == 8192 == NR_BUFFER_NUM*sizeof(NR_Buffer) >= 3*sizeof(FDV_IQ_Buffer)`
(мутация 8191 ловится компилятором). Host-tests: 1134/0/3 PASS.

**ВАЖНОЕ ОГРАНИЧЕНИЕ: FreeDV в данной сборке не включён.** Продукт собирается с `IS_SMALL_BUILD`
(`hardware/uhsdr_board.h:70`), а `USE_FREEDV` определяется только при `#ifndef IS_SMALL_BUILD`
(`uhsdr_board.h:114-117`) — так же, как в исходном форке Nizar. Проверено фактически: в
`release/v1.1/fw-mchf.elf` и в пересборке v1.2 **нет ни одной FreeDV-функции**
(`freedv_open`, `freedv_comprx`, `FreeDv_HandleFreeDv`); `DigitalMode_FreeDV` отсутствует в enum,
пункт меню FreeDV вырезан. Следствия:
1) в прошитом бинаре сценарий «FreeDV → NR» невозможен — гейты A2 вырезаются препроцессором;
2) порт сделан upstream-parity «на будущее»: при будущем включении FreeDV он заработает без
   доработок; `AudioNr_Prepare()` (гейт `USE_ALTERNATE_NR`) активен на любом выходе из `DEMOD_DIGI`;
3) **включение FreeDV — отдельная задача**: требует `-DUSE_FREEDV` и проверки **пре-существующего
   рассинхрона guard'ов** — `IIR_biquad_FreeDV_I/Q` определены только при `!IS_SMALL_BUILD`
   (`audio_driver.c:662-723`), а используются под `USE_FREEDV` (`audio_driver.c:2434-2435`), плюс
   бюджет RAM/flash и ревизия обвязки 2.9.48 против vendor 2.12.2. Утверждать, что FreeDV
   «работает» или «исправлен в бою», **нельзя**.

**Обновление B4 (2026-11-27).** Opt-in FreeDV-сборка теперь существует: `tools/build.py --freedv`
включает `USE_FREEDV` при сохранении `IS_SMALL_BUILD`; default-сборка не меняется. Исправлены
6 guard-мест `IS_SMALL_BUILD↔USE_FREEDV`, порядок `struct my_callback_state` и (только в opt-in
сборке) удалён `MODEM_STATS` (10,6 КБ) из `struct freedv`. **GCC 9.3.1:** FreeDV-вариант
**555 608 Б / RAM 48 552 / CCM 63 972 / 50 warnings**; 43 FreeDV/Codec2-символа; host-tests
1754 PASS. Исторические GCC15-цифры (565 584/48 836/78, затем 565 552/73) невалидны.
**На железе не испытывался** (CPU/стек/звук) — только после стенда по docs/FREEDV.md можно
утверждать, что FreeDV «работает».

## B5 — Warning-гейт, два реальных дефекта `-Waddress`, bounded `float2fixedstr`, docs/LIBS.md, `--opt-level`

**Проблема (C-7/C-8 из `_audit/dev/b3-audit.md`).** 78 строк предупреждений компилятора/линкера
маскировали новые; среди них были два реальных дефекта: `char*`-указатели сравнивались со
строковыми литералами через `==` (работало только благодаря LTO-склейке литералов — без неё
терялся маркер `LPF*` и кнопка HPF переключала полосу вместо HPF), а `float2fixedstr`
(`ui_menu.c`) писал `.` и финальный NUL без проверки `maxchar`, ограничивал дробную часть по
чужому индексу и мог уйти в отрицательный стартовый индекс — потенциальный OOB (виден под
`-O3`/LTO как `-Wstringop-overflow`). Кандидаты на обновление библиотек требовали проверки:
обновление могло сломать вендорный код.

**Исправление (маркеры `// N96K B5:`).**
- `drivers/ui/ui_driver.c` (`UiDriver_DisplayFilter`), `drivers/ui/ui_vkeybrd.c`
  (`UiVk_HPF_UP_TouchScreen`): сравнения указателей заменены на `!strcmp` (3 места,
  5 предупреждений).
- `drivers/ui/ui_menu.c` (`float2fixedstr`): полный bound по `maxchar`, индекс дробной части
  в пределах буфера, неотрицательный стартовый индекс; эквивалентность вывода проверена
  симуляцией на всём домене `freq_cal` (2999 значений) — 0 расхождений (+32 Б).
- **Warning-гейт:** `tools/check_warnings.py` + `tests/warnings-baseline.txt`
  (создан на GCC 15.2.1 с 73 предупреждениями / 35 текстами; **актуальный baseline GCC 9.3.1 —
  50 предупреждений / 16 текстов**); шаг в CI job `docker-build`; новое/участившееся
  предупреждение валит сборку, исчезнувшие — OK; нормализация путей/строк делает baseline
  кроссплатформенным (Windows-хост и Linux-образ).
- **`docs/LIBS.md`** (b5-libs): пины версий и SHA-1 деревьев CMSIS Core/Device/DSP, HAL F4,
  USB Device/Host, FatFs, Codec2 rev 4121; сравнение с UHSDR 2.12.2; вывод — безопасных
  обновлений нет (CMSIS 5/6, DSP 1.18, HAL 1.8.3, FatFs R0.15 — API-breaking без стенда),
  вендорный код не менялся; CREDITS обновлён.
- **`--opt-level {O2,O3,Os}`** в `tools/build.py`: default **O2** (историческая сборка не
  меняется; загрузчик всегда `-Os`); `-Os` не линкуется из-за C99-`inline` в заголовках;
  LTO-настройки проверены (выигрыша нет, `-flto-partition=one` хуже); микрооптимизации кода не
  применялись (обоснование — в отчёте b5-opt).

**Проверки (GCC 15.2.1 — исторические, невалидны для железа).** default fw 322 312 (RAM 42 668,
CCM 63 476, 73 warnings; было 322 360/78); FreeDV opt-in 565 552 (48 836/63 972/73); O3 425 500
(42 696/63 476/73), O3+FreeDV 703 928; **актуальные GCC9:** default 312 216 (RAM 42 392, CCM 63 476),
FreeDV 555 608 (48 552/63 972), O3 412 536 (42 436/63 476), baseline 50. host-tests **1754 PASS**
(0 failed, 3 known); warning-гейт OK для default и O3; workflow-линт OK; загрузчик не менялся
(known-good 31 620 Б). На железе не проверялось: правки — UI/отображение, проверка только по коду
и сборке.

## Nizar-review (B6): аудит и исправления

Ветка `nizar-review` (от `main` `ee5e06ed`), HEAD **`69ccd736`**; влита в `main` (HEAD **`34d28730`**
= v1.2 + B1–B6 + GCC 9.3.1). Аудит — read-only; исправления — коммиты `e8a78590` (`--full`),
`22fce856` (docs-rebuild), `aa5fdcb9` (H1/H2/H3), `a4042337` (H5), `69ccd736` (H4/H6/H9/H10/H11/H13);
GCC 9.3.1-пересборка — `34d28730`. В `main` ветка
пока не влита. Финальные сборки, печатные PDF (task-47) и пакет `release/bench-2026-11-27`
(собирает lead) — готовятся к стенду. Полные отчёты — `_audit/dev/` (вне репозитория).

### Что нашли (Nizar-audit, task-39)

- Диф `ae93edf6..nizar-baseline`: 152 файла, +39 536 / −19 299; в текущем HEAD 615 маркеров NIZ
  (с нашими B-маркерами).
- Ядро форка (RX 96 кГц / TX 12 кГц integer) работоспособно и самоценно — откат не рекомендуется.
- **16 вредных/критичных:** H1 (расширенные бэнды → TX вне любительских участков), H2 (перекомпрессия
  TX-аудио), H3 (CW-R удалён), H5 (USB-буферы/дескрипторы), H4 (Si5351 PLL reset), H6 (AGC),
  H7 (ADC-clip), H8 (RF-гейн), H9 (+15 Гц), H10 (CW-декодер), H11 (CAT PWR/SWR), H12 (noise gate/VID),
  H13 (тачскрин/DSP), H14 (скрытые меню), H15 (bias-reduce выключен), H16 (MIC gain+boost).
- **14+ положительных:** 96k/12k integer, 32-бит кодек WM8731, новые фильтры, TX EQ/компрессор/ALC,
  11 точек IQ-калибровки, новые меню, CAT 0x65/E7/E8/PTT-STATE, USB 96k, порядок инициализации кодека,
  «тренировка» реле, paddles_active, частоты в Гц, DEBUG info, per-band SWR-коэффициенты.
- Опасных «лимитов PA» нет: `TX_POWER_FACTOR_MAX_INTERNAL = 0.55` сохранён; главный остаточный
  риск — отсутствие стенда.

### Что исправлено в ветке

- **H1 + H2 + H3 (`aa5fdcb9`).** TX-лимиты `BAND_TX_*` и
  `RadioManagement_IsTxDialFrequencyInBand()` (RX-покрытие Nizar сохранено, override
  `FLAGS1_TX_OUTSIDE_BANDS` сохранён — но для частот внутри расширенных RX-участков он даёт
  **обычную per-band мощность**, не пониженную: минимальную выставлять вручную, только эквивалент);
  дефолты TX-аудио MIC 35→15, ALC_POSTFILT 11→1,
  comp out 25→10, treble 20→4, EQ preset→custom (+ reset-default в меню); CW-R восстановлен —
  7 жёстких `ts.cw_lsb = true` убраны (логика по upstream), меню «CW LSB/USB Select» и
  «CW Freq. Offset» возвращены, CAT 0x02/0x03 различаются, длинное нажатие Mode переключает сторону.
- **H5 (`a4042337`).** USB-TX — возвращены guard и счётчик overflow (сравнение с tail, дословная
  upstream-проверка «мёртвая» и переполнение не ловила); USB-микрофон — индексация с wrap и учёт
  overflow, дублирование L/R — корректное моно→стерео (источник уже 96 кГц, ZOH 48→96k не подтверждён);
  частоты/каналы дескриптора — через `SAMPLE_FREQ`/`USBD_AUDIO_*` макросы; байты дескрипторов
  и идентификация устройства не изменились (сравнение ELF до/после).
- **`--full` (`e8a78590`).** `python tools/build.py all --full` — `-DN96K_FULL_BUILD`
  снимает `IS_SMALL_BUILD` и добавляет `-DN96K_FREEDV` (обязательная конфигурация памяти
  FreeDV); **GCC 9.3.1: full 561 720 / 48 552 / 63 972, 50 warnings** (историческая GCC15-сборка
  573 088/73 невалидна); default и загрузчик не меняются; сборка стендовая, не продуктовая
  (heap/стек на границе, CCM 97,6 %).
- **H4/H6/H9/H10/H11/H13 (`69ccd736`).** H4: PLL reset default 2 (reset при смене IQ-делителя),
  форс «Never» убран, debug-пункт меню возвращён. H6: AGC-дефолты по upstream (slope 70, tau
  4000/2000/500/250/50/500, thresh 60, hang 500/45), `% agc_wdsp.ring_buffsize` вместо `%96`,
  меню AGC Mode/Threshold возвращены; **Hang-пункты намеренно не возвращены** (Nizar-AGC без стадии
  hang; поле `agc_wdsp_hang_enable` переиспользовано как «AGC WDSP OFF Gain»). H9: неявный
  сдвиг +15 Гц в SSB-MIC устранён (`tx_bass_sheeft` 0→15 = 0 Гц; явная опция −15…+5 Гц
  остаётся). H10: минимумы CW-декодера 8/24→2/6 (~82 WPM при дефолтном blocksize). H11: CAT 0xBD
  отдаёт реальные PWR/SWR nibble. H13: TP_IRQ PULLUP/VERY_HIGH; сброс DSP
  (`ts.nr_first_time=1`) на каждом band switch **не убран** — стендовая проверка.

### Остаток (стенд/решение, код не менялся)

- **H7/H8:** пороги ADC-clip 22200/10800/3800 и кривая RX-codec-gain `rfg_calc*4−1` с
  `Codec_RestartI2S()` — нужны генератор+аттенюатор.
- **H12:** мёртвый noise gate (следы в EEPROM/меню) и USB VID/PID из bit2 `ts.reverberation_enable`
  (0x0963/0x5733 Win/Linux или Arduino 0x2341/0x8036 Android) — осознанная фича с юридическим
  риском; смена VID/PID требует миграции EEPROM и стендовых драйвер-тестов — решение lead.
- **H13-остаток:** `nr_first_time` на band switch — стенд: 20+ переключений с NR/DNR.
- **H14/H15/H16/N11:** скрытые пункты меню/zoom (H14), bias-reduce/fine power factor (H15),
  MIC max 150 + codec +20 дБ без клампа (H16), лицензионная строка «mchf QRP» (N11) — известные
  отклонения, не исправлялись.

### Цифры и проверки

**Актуальные GCC 9.3.1 (main 34d28730):** default **312 216 / 42 392 / 63 476** (Build
`Nov 27 2026 - 14:55:12`); FreeDV **555 608 / 48 552 / 63 972**; full **561 720 / 48 552 / 63 972**;
O3 **412 536 / 42 436 / 63 476**; `bl-mchf.bin`: пакет — known-good 5.0.4 31 620, GCC9 all-сборка —
32 416; warnings **50 = baseline** (16 текстов; firmware-only 46); host-tests **1754 PASS**
(0 failed, 3 known). Исторические GCC15-цифры B6 (default 323 304 / FreeDV 567 040 / full 573 088 /
O3 428 236, 73 warnings) **невалидны** — прошивки зависают после splash. Независимо подтверждено
для GCC15-среза: `_audit/dev/b6-verify.md` (включая ELF-дескрипторы H5); для GCC9 — сборки и
пакет проверены в task-52/финализации lead. На железе не проверялось: H1 (TX-границы), H2
(спектр TX), H3 (CW-R/CW-USB), H5 (USB-аудио), H4 (PLL), H6 (AGC), H9 (биения), H10 (CW), H11
(CAT), H13 (тачскрин) — стенд по docs/CHECKLIST.md.

## Проверка документации и версии (v1.2; сборка GCC 15.2.1 — историческая, невалидна)

- Версия продукта: `UHSDR_PRODUCT_VERSION`: `2.9.48-n96kp2` → **`2.9.48-n96kp3`**; числовые
  `UHSDR_VER_* = 2.9.48` не менялись (их разбирает Makefile и использует логика версий/EEPROM);
  `UHSDR_BOOT_VERS = 5.0.4` не менялся. Строка на устройстве:
  `UHSDR_VERSION = "fwv-2.9.48-n96kp3"`, отображение `UHSDR_VERSION+4` = `2.9.48-n96kp3`
  (13 символов — та же длина, что n96kp2; буферы UI — `snprintf(out, 32, …)` в
  `ui_menu.c:723-728`, запас большой).
- `tests/check_versions.py` ждёт `2.9.48-n96kp3`; `python tests/run_all.py` — PASS
  (**1754 проверки** после B1 (в v1.2 — 1134), 0 failed, 3 known, 0 skipped). Приватная сборка — PASS.
- Все цифры v1.2 — из чистой сборки HEAD `557e39f1` + bump `n96kp3` (GCC 15.2.1; историческая,
  невалидна): `fw-mchf.bin` 321 392 Б, flash 32,7 % (запас 661 648 Б), RAM 42 616 Б,
  CCM 63 476/65 536 (свободно 2 060), `bl-mchf.bin` 31 620/32 768 Б (запас 1 148),
  предупреждений 78 строк = fw 70 + bl 8. **Актуальные GCC 9.3.1 (main 34d28730):** default
  312 216 / 42 392 / 63 476, FreeDV 555 608, full 561 720, O3 412 536, bl GCC9 all 32 416 (в пакете
  known-good 31 620), 50 предупреждений (16 текстов).
  **Размер зависит от содержимого строки версии:** тот же код с `n96kp2` даёт 321 384 Б, с
  `n96kp3` — 321 392 Б (+8 Б; проверено A/B-сборками одного дерева, различие — только
  `uhsdr_version.h`). Артефакты `release/v1.2` упакованы, тег **v1.2** — annotated
  `4ca85bba` → commit `dc204cc1` (включает B2-документацию); v1.0/v1.1/v1.2 опубликованы; `main`
  `ee5e06ed` = v1.2 + B1–B5, CI зелёный — статус в [STATUS.md](STATUS.md).

## Что не менялось

- Алгоритмы и параметры Nizar (ЦОС-коэффициенты, фильтры, дефолты меню, отрисовка SWR-метра) —
  не изменялись; маркеры авторства NIZZ/NIZ96/NIZ32 сохранены.
- `LICENCE.md` (GNU GPLv3) — не изменялся.
- Числовые `UHSDR_VER_*` = 2.9.48 — не менялись. **Существующие адреса EEPROM не сдвигались**;
  новые записи добавлены в свободные/резервные id: 426 (P1-1), 430 и 524 (P2-4),
  `EEPROM_FIRST_UNUSED` 524 → 525, `MAX_VAR_ADDR` 523 → 524 (см. P2-4).
- CMSIS/HAL и прочие вендорные библиотеки — не переписывались; Codec2/FreeDV обновлён только
  в P2-2 (до upstream rev 4121, с одним задокументированным compat-блоком); в default-сборке
  FreeDV отключён (`IS_SMALL_BUILD`, см. P2-2/A2), есть opt-in `--freedv` (B4).
- Доработки Nizar не откатывались: изменения v1.1 — аддитивные (новые поля/пункты/enum в конец,
  новые ветки), а заменённый в P1-3 блок foldback — код самого v1.0-патча P0-4, не Nizar.
- B5 поведение прошивки не меняет, кроме исправления двух UI-дефектов (`!strcmp` вместо
  сравнения указателей) и bounds-проверок `float2fixedstr`; warning-гейт/LIBS/`--opt-level` —
  инфраструктура, default-сборка остаётся O2.
- Этап A (v1.2) поведение Nizar не менял: A1 сохраняет прежнее поведение вне foldback
  (`effective_power_level == ts.power_level`), A2 только сбрасывает NR-состояние после DIGI и
  добавляет гейты под `#ifdef USE_FREEDV`; ЦОС-коэффициенты, фильтры, EEPROM-раскладка и UI не
  тронуты, новых EEPROM-слотов нет.

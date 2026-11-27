# RS918 N96K-Pro — статус проекта

**Обновлено:** 2026-11-27 (этап docs-final, вторая волна Nizar-review).
**Ветка продукта:** main, HEAD **ee5e06ed** = v1.2 + B1 + B2 + B3 + B4 + B5.
**Текущая работа:** ветка nizar-review (от main), HEAD **a4042337**: Nizar-audit, фиксы
H1/H2/H3 (aa5fdcb9), H5 (a4042337), opt-in --full (e8a78590), docs-rebuild (22fce856),
cleanup; misc-фиксы H4/H6/H9–H12 — в рабочем дереве (без коммита). В main изменения второй
волны ещё не влиты.

Легенда: ✅ сделано · 🔄 в процессе · ⏳ план · ⛔ блок.

## 1. Сводка статусов

| Область | Статус | Итог / ссылка |
|---|---|---|
| v1.0 — P0-патчи | ✅ сделано | тег v1.0 (коммит 82056486), release/v1.0; fw 320 024 Б, bl 4.1.2 31 496 Б |
| v1.1 — P1/P2 | ✅ сделано | тег v1.1 (коммит cf43b068), release/v1.1; fw 321 128 Б, bl 5.0.4 31 620 Б |
| v1.2 — этап A1/A2 | ✅ сделано | тег v1.2 (annotated 4ca85bba → коммит dc204cc1, сборка/версия 619e9187); release/v1.2; fw 321 392 Б |
| B1 — тесты/CI/релиз | ✅ сделано | 1754 проверки; 3 job'а Actions; tools/release.py |
| B2 — документация | ✅ сделано | TESTPLAN 112 тестов, BOOTLOADER, VSWR, RELEASE + печатные DOCX/PDF |
| B3 — аудио/DSP hardening | ✅ сделано | fixed NR-буфер, сброс NR, ALC-ring OOB, DEBUG-диагностика |
| B4 — FreeDV opt-in | ✅ сделано | --freedv: FreeDV 565 552 Б на main; после второй волны — 566 616 Б; на железе не было |
| B5 — warning-гейт/фиксы/opt | ✅ сделано | 73 warnings baseline + гейт в CI; strcmp-фиксы, bounded float2fixedstr, docs/LIBS.md, --opt-level |
| Публикация и CI | ✅ сделано | <https://github.com/kramskois-boop/RS918-N96K-Pro>; PR #1/#2 merged; живые прогоны Actions PASS |
| Тестовые прошивки | ✅ сделано | release/fieldtest-2026-11-27 (default + FreeDV + O3 + bl, чек-лист, SHA256SUMS) |
| Nizar-audit (task-39) | ✅ сделано | 16 вредных/критичных (H1–H16) и 14+ положительных изменений; ядро 96k/12k признано работоспособным; отчёт _audit/dev/nizar-audit.md |
| Nizar-fix H1/H2/H3 (task-43) | ✅ сделано | коммит aa5fdcb9: TX только в любительских участках, безопасные дефолты TX-аудио, CW-R/CW-USB |
| Nizar-fix H5 (task-44) | ✅ сделано | коммит a4042337: guard переполнения USB-TX, индексация/overflow USB-микрофона, дескрипторы от макросов |
| Nizar-fix misc H4/H6/H9–H12 (task-45) | 🔄 в процессе | рабочее дерево (без коммита): Si5351 PLL reset, AGC-дефолты/меню, сдвиг +15 Гц, CW-декодер, CAT PWR/SWR, noise gate/USB ID; H7/H8 — только стенд |
| Nizar-full/--full (task-40) | ✅ сделано | коммит e8a78590: opt-in полная сборка (без IS_SMALL_BUILD); full 572 664 / 48 840 / 63 972; стендовая, не продуктовая |
| Cleanup (task-41) | ✅ сделано | освобождено ≈23,2 ГиБ (рабочая папка 24,8 → 2,8 ГиБ); _audit/dev/cleanup.md |
| Docs-rebuild (task-42) | ✅ сделано | коммит 22fce856: STATUS/CHANGES/CHANGELOG/PATCHES/README/PLAN-DEV/CI/RELEASE |
| Docs-final (task-46) | 🔄 в процессе | этот срез: STATUS/CHANGES/CHANGELOG/README/PATCHES/PLAN-DEV/BUILD/SAFETY/CHECKLIST |
| Стенд H7/H8 и проверка H1/H2/H3/H5 | ⏳ план | ADC-clip пороги, кривая RF-гейна, TX-границы, спектр TX после H2, CW-R, USB-аудио |
| Полевые испытания (этап C) | ⏳ план | docs/TESTPLAN.md, docs/CHECKLIST.md; требует железа |
| v1.3 | ⏳ план | после финальных сборок второй волны и полевых данных: тег, пакет, PDF |
| Печатные PDF второй волны | ⏳ план | CHECKLIST/CHANGES — после финальных сборок (отдельный шаг, make_printable_docs.py) |
| Backlog B6+ | ⏳ план | FreeDV-стенд, F1 (ложные trip), watchdog IWDG, аналоговая защита PA, бэкпорты 2.12.2 |
| Железо для полевых тестов | ⛔ блок | нет доступа к RS-918; все VSWR/загрузчик/FreeDV/CAT/USB/96k-выводы — только код/сборка/симуляция |
| Локальный docker build | ⛔ блок | Docker на машине разработки не установлен; воспроизводимость даёт CI-раннер |

## 2. ✅ Сделано (main ee5e06ed)

### 2.1. Релизы
- **v1.0** — P0-1…P0-6 (EEPROM, сборка, аудио, PA/КСВ, TX-безопасность, брендинг), release/v1.0.
- **v1.1** — P1-1…P1-5, P2-1…P2-4 (VSWR порог/индикация/скорость, EEPROM fallback, CAT-гейты,
  загрузчик 5.0.4, FreeDV vendor rev 4121, CI/тесты, AGC/Tune/CW-бэкпорты), release/v1.1.
- **v1.2** — этап A: A1 (минимальный драйв при активном foldback, завершение trial в гистерезисе),
  A2 (NR+FreeDV #1749), версия n96kp3; release/v1.2 + печатные документы
  (release/v1.2/printable/) и полевая тестовая сборка.

### 2.2. Инструментальные этапы (main)
- **B1** — host-тесты (1754 проверки, 0 failed, 3 known), CI-workflow из 3 job'ов, release-job
  по тегу, tools/release.py, tools/check_workflow.py; живой CI на push в main.
- **B2** — docs/TESTPLAN.md (112 уникальных тестов), BOOTLOADER.md, VSWR.md, RELEASE.md,
  печатные чек-лист и сводная таблица (PDF/DOCX).
- **B3** — надёжность/DSP: VLA в audio ISR заменён фиксированным буфером, согласованный сброс NR,
  выравнивание ALC-кольца при SSB↔AM/FM (был OOB до 24 float в CCM), DEBUG-счётчики ISR/DMA/NR.
- **B4** — opt-in FreeDV: --freedv, 6 guard-фиксов, MODEM_STATS только под opt-in сборку,
  docs/FREEDV.md, бюджет и план стенда; default-сборка не изменилась.
- **B5** — warning-гейт (tools/check_warnings.py + tests/warnings-baseline.txt, 73/35),
  два реальных дефекта -Waddress (strcmp), bounded float2fixedstr (латентный OOB),
  docs/LIBS.md (пины вендорных библиотек), --opt-level O2/O3/Os.

### 2.3. Публикация и CI
- Репозиторий public: <https://github.com/kramskois-boop/RS918-N96K-Pro>; main + теги
  v1.0 / v1.1 / v1.2.
- PR: [#1](https://github.com/kramskois-boop/RS918-N96K-Pro/pull/1) (B3+B4, merged 2026-11-27),
  [#2](https://github.com/kramskois-boop/RS918-N96K-Pro/pull/2) (B5, merged 2026-11-27).
- Последний прогон на main ee5e06ed:
  [run 45971492786](https://github.com/kramskois-boop/RS918-N96K-Pro/actions/runs/45971492786) —
  **success** (host-tests 10 с, docker-build 2 мин 3 с, release skipped — не тег).
- Release-job проверен на временном теге v0.0.0-citest (артефакты → release.py → sha256sum -c
  → GitHub Release); тестовый тег/релиз удалены.

### 2.4. Тестовые прошивки
- release/fieldtest-2026-11-27 (rev 2): default fw 322 312 Б; FreeDV fw 565 552 Б;
  O3 fw 425 500 Б; bl 5.0.4 31 620 Б; build-summary/warnings каждой сборки; docs/CHECKLIST.md,
  docs/TESTPLAN.md, docs/FREEDV.md; SHA256SUMS.txt. Пакет для стенда, **не релиз**.

### 2.5. Вторая волна (ветка nizar-review, коммиты)
- **Nizar-audit (_audit/dev/nizar-audit.md)** — классификация diff ae93edf6..nizar-baseline
  (152 файла, +39 536/−19 299): 16 вредных/критичных изменений (high: H1 TX-границы, H2 TX-аудио,
  H3 CW-R, H5 USB-буферы), 14+ положительных (96k/12k, 32-бит кодек, фильтры, TX EQ/компрессор,
  11 точек IQ-калибровки, меню, CAT, USB 96k, тренировка реле, частоты в Гц), ~12 нейтральных.
  Кламп драйва PA 0,55 сохранён — «лимитов PA» Nizar не повышал; главный риск — отсутствие стенда.
- **H1/H2/H3 (aa5fdcb9)** — TX разрешён только в любительских участках четырёх расширенных
  RX-бэндов (10 м 28,0–29,7; 4 м 70,0–70,5; 2 м 144–146; 70 см 430–440 МГц), RX-покрытие не
  тронуто; дефолты TX-аудио возвращены к нейтральным (MIC 15, ALC postfilt 1, comp out 10,
  treble 4, EQ custom); CW-R/CW-USB восстановлен (меню, кнопки, CAT 0x02/0x03).
- **H5 (a4042337)** — в USB-TX возвращены guard+счётчик переполнения; USB-микрофон: индексация
  с wrap и учёт overflow (дублирование L=R — корректное моно→стерео, а не ZOH 48→96k); tSamFreq/
  bNrChannels выводятся из макросов — байты дескриптора не изменились (проверено по ELF).
- **--full (e8a78590)** — python tools/build.py all --full: -DN96K_FULL_BUILD снимает
  IS_SMALL_BUILD (FreeDV + const-окна Ханна), плюс -DN96K_FREEDV (обязательная конфигурация
  памяти); загрузчик не меняется. На момент коммита (до H1/H2/H3/H5): 571 616 / 48 836 / 63 972;
  текущая стендовая сборка: **572 664 / 48 840 / 63 972**, 73 warnings.
- **Cleanup (task-41)** — освобождено **23 789,9 МиБ ≈ 23,2 ГиБ** (рабочая папка 24 812,5 →
  2 810,3 МиБ): build-артефакты, _tmp, O3-LTO логи; исходники/release/_backup/_audit
  не тронуты (SHA256 55/55 OK).
- **Docs-rebuild (22fce856)** — STATUS/CHANGES/CHANGELOG/PATCHES/README/PLAN-DEV/CI/RELEASE
  приведены к состоянию main; CHECKLIST/CHANGES подготовлены к пересборке PDF.

## 3. 🔄 В процессе

- **Nizar-fix misc (task-45, рабочее дерево, без коммита):** H4 (Si5351 PLL reset default/menu),
  H6 (AGC-дефолты/ring_buffsize/меню), H9 (скрытый сдвиг +15 Гц в SSB-MIC), H10 (минимумы
  CW-декодера), H11 (CAT PWR/SWR nibble), H12 (мёртвый noise gate / USB VID-PID). H7 (пороги
  ADC-clip) и H8 (кривая RF-гейна) — только зафиксировать как стендовые. После слияния —
  повторные сборки default/FreeDV/full и уточнение цифр.
- **Docs-final (task-46, этот срез):** актуализация STATUS/CHANGES/CHANGELOG/README/PATCHES/
  PLAN-DEV/BUILD/SAFETY/CHECKLIST по итогам Nizar-review; без пересборки PDF.

## 4. ⏳ План / backlog

1. **Завершить вторую волну:** слияние misc-фиксов (H4/H6/H9–H12), финальные сборки
   default/FreeDV/full, warning-гейты, host-тесты; зафиксировать итоговые числа (STATUS/CHANGES/
   README/PATCHES) и обновить таблицу метрик.
2. **Пакет второй волны:** tools/release.py (SHA256SUMS, RELEASE-NOTES), стендовая и продуктовая
   сборки; решение lead по составу (default — продукт, --full — только стенд).
3. **Печатные PDF:** пересобрать CHECKLIST/CHANGES после финальных чисел; учесть замечания
   docs-rebuild к make_printable_docs.py (footer «v1.2» → current; strip_md не должен
   съедать «_» в идентификаторах и обратные кавычки; таблицы — 5 колонок).
4. **Стенд (полный список — docs/CHECKLIST.md):** TX-границы H1 (10 м/4 м/2 м/70 см, override
   только с эквивалентом), спектр TX после H2, CW-R/CW-USB (меню/кнопки/CAT), USB-аудио после H5,
   H7 (ADC-clip/auto-RFG), H8 (RF-гейн), H9 (нулевые биения SSB), H4 (PLL reset при смене
   IQ-делителя), H6 (AGC), H11 (CAT PWR/SWR), H12 (VID/PID), H3/H5 регрессии.
5. **Полевые испытания (этап C)** по docs/TESTPLAN.md и docs/CHECKLIST.md: VSWR-защита и A1,
   загрузчик 5.0.4 (USB/DFU), NR/Tune, USB-аудио, CAT/Hamlib, 96k-панорама.
6. **v1.3** — после второй волны и полевых данных: итоговые цифры/решения, тег и release-пакет,
   печатные PDF.
7. **Backlog B6+:**
   - F1 — ложные foldback-trip на фронте TX/после dynamic tune (нужен стенд; C-2 в _audit/dev/b3-audit.md);
   - watchdog IWDG (сейчас выключен: hardware/uhsdr_board.c — mchf_board_watchdog_init() закомментирован);
   - аналоговая защита PA (ток/КСВ) и первые ~50 мс TX без программной КСВ-защиты;
   - FreeDV-стенд: CPU/стек/звук, интеграция API 2.12.2 (700D/squelch), FreeDV↔NR остатки;
   - остальные бэкпорты 2.12.2: #1664 (DMA size), #1744 (CW buffer), #1675 (не для flash-конфига);
   - known issues: iir_3k.c:IIR_3k (мёртвый код, KNOWN), Zoom FFT placeholder ×2 (KNOWN);
   - бюджетные запасы: CCM свободно 2 060 Б (full — 1 564), загрузчик 1 148 Б (нужны гейты/контроль);
   - EEPROM-стратегия (версия раскладки/миграция) и USB-аудио дескрипторы 96k (после стенда);
   - H7/F7 — поддержка сохраняется (решение 2026-11-27), vendor не удаляется.

## 5. ⛔ Блоки
- **Железо**: полевых/аппаратных испытаний не было — все выводы о VSWR, загрузчике, FreeDV,
  CAT/USB, 96k, TX-границах H1, спектре TX H2, ADC-clip H7 и RF-гейне H8 — по коду, сборке и
  симуляции. Это главный остаточный риск (C-1).
- **Локальный Docker**: на машине разработки Docker не установлен; полная Docker-сборка
  выполняется только в GitHub Actions.
- **PDF второй волны**: печатные CHECKLIST/CHANGES пересобираются после финальных цифр второй
  волны (сборки после misc-фиксов), чтобы не публиковать устаревшие числа.

## 6. Текущие цифры (ветка nizar-review, HEAD a4042337 + H1/H2/H3/H5; GCC 15.2.1)

| Конфигурация | fw-mchf.bin | RAM static | CCM | warnings / статус |
|---|---|---|---|---|
| default (-O2) | **323 096 Б** | 42 672 | 63 476 (свободно 2 060) | 73; host-тесты PASS |
| FreeDV opt-in (--freedv) | **566 616 Б** | 48 840 | 63 972 (свободно 1 564) | 73; на железе не было |
| full opt-in (--full) | **572 664 Б** | 48 840 | 63 972 (свободно 1 564) | 73; стендовая, на железе не было |
| O3 opt-in (--opt-level O3) | ≈425 500 Б (main; после фиксов не пересобирался) | ≈42 696 | 63 476 | 73; ориентир, не финал |

Загрузчик: **31 620 / 32 768 Б** (5.0.4, запас 1 148; не менялся). Host-тесты: **1754 проверки,
0 failed, 3 known**. Источник — build/build-summary.json, build-freedv/, build-full/
(2026-11-27); при сверке ориентир — build-summary.json.

Дельта к main ee5e06ed: default **+784 Б** (322 312 → 323 096), FreeDV **+1 064 Б**
(565 552 → 566 616), full **+1 048 Б** (571 616 → 572 664), RAM +4 Б, CCM 0, warnings 73 = 73.
**Misc-фиксы H4/H6/H9–H12 в работе** — после их слияния значения изменятся; финальные цифры
появятся после итоговых сборок и будут отмечены здесь.

## 7. Ссылки

- Репозиторий: <https://github.com/kramskois-boop/RS918-N96K-Pro>
- CI: <https://github.com/kramskois-boop/RS918-N96K-Pro/actions>
- PR #1 (B3/B4): <https://github.com/kramskois-boop/RS918-N96K-Pro/pull/1>
- PR #2 (B5): <https://github.com/kramskois-boop/RS918-N96K-Pro/pull/2>
- Run main ee5e06ed: <https://github.com/kramskois-boop/RS918-N96K-Pro/actions/runs/45971492786>
- Документы: [docs/CHANGES.md](CHANGES.md), [docs/CHANGELOG.md](CHANGELOG.md),
  [docs/PATCHES.md](PATCHES.md), [docs/RELEASE.md](RELEASE.md), [docs/CI.md](CI.md),
  [docs/TESTPLAN.md](TESTPLAN.md), [docs/CHECKLIST.md](CHECKLIST.md), [docs/FREEDV.md](FREEDV.md),
  [docs/LIBS.md](LIBS.md), [PLAN-DEV.md](../PLAN-DEV.md).
- Аудит/отчёты (вне репозитория): E:\harnet\sdr 11\_audit\REPORT.md, _audit/parts/,
  _audit/dev/ (nizar-audit, b6-radio-fix, b6-usb-fix, nizar-full, docs-rebuild, cleanup и др.).
- Релизы: release/v1.0, release/v1.1, release/v1.2, тестовый пакет
  release/fieldtest-2026-11-27.

## 8. Правила обновления статуса

1. Статус меняет только lead или владелец этапа; документ синхронизируется с docs/CHANGELOG.md
   и docs/PATCHES.md.
2. Все цифры — только из build-summary.json/проверочных сборок/CI; если сборка не выполнялась —
   писать «не проверено», а не предполагаемое значение.
3. «Сделано» = код в ветке этапа + сборка/тесты PASS; «на железе не было» указывается отдельно.
4. Ветка nizar-review содержит коммиты второй волны, но не меняет статусы main до merge/тега
   (решение lead — отдельным шагом после финальных сборок).

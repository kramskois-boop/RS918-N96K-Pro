# RS918 N96K-Pro — статус проекта

**Обновлено:** 2026-11-27 (финальный срез B6: PR #3 влит, пакет для стенда готов).
**Ветка продукта:** main, HEAD **77d9899a** = v1.2 + B1…B5 + B6 (merge PR #3).
**Сделано:** Nizar-audit; фиксы H1/H2/H3 (aa5fdcb9), H5 (a4042337), H4/H6/H9/H10/H11/H13
(69ccd736); opt-in --full (e8a78590); docs-rebuild/docs-sync (a06eb86e); cleanup (~23 ГиБ);
финальные сборки default/FreeDV/full/O3; печатные PDF; пакет **release/bench-2026-11-27**
(29 файлов + SHA256SUMS + zip) и его независимая проверка (b6-package-verify: готов к стенду).
**Ветка nizar-review** = a06eb86e (запушена, PR #3 смержен). Живой CI на main — PASS.

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
| B4 — FreeDV opt-in | ✅ сделано | --freedv: FreeDV 565 552 Б на main; в B6 — 567 040 Б; на железе не было |
| B5 — warning-гейт/фиксы/opt | ✅ сделано | 73 warnings baseline + гейт в CI; strcmp-фиксы, bounded float2fixedstr, docs/LIBS.md, --opt-level |
| Публикация и CI | ✅ сделано | <https://github.com/kramskois-boop/RS918-N96K-Pro>; PR #1/#2 merged; живые прогоны Actions PASS |
| Тестовые прошивки | ✅ сделано | release/fieldtest-2026-11-27 (v1.2 + B1–B5) и release/bench-2026-11-27 (B6, собирает lead) |
| Nizar-audit (task-39) | ✅ сделано | 16 вредных/критичных (H1–H16) и 14+ положительных изменений; ядро 96k/12k признано работоспособным; отчёт _audit/dev/nizar-audit.md |
| Nizar-fix H1/H2/H3 (task-43) | ✅ сделано | коммит aa5fdcb9: TX только в любительских участках, безопасные дефолты TX-аудио, CW-R/CW-USB |
| Nizar-fix H5 (task-44) | ✅ сделано | коммит a4042337: guard переполнения USB-TX, индексация/overflow USB-микрофона, дескрипторы от макросов |
| Nizar-fix misc H4/H6/H9/H10/H11/H13 (task-45) | ✅ сделано | коммит 69ccd736: Si5351 PLL reset + меню, upstream-дефолты AGC + меню Mode/Threshold, сдвиг +15 Гц (дефолт 0), минимумы CW-декодера, CAT PWR/SWR nibble, TP_IRQ pull-up/speed |
| H7/H8/H12/VID-PID + H13-остаток (аудит) | ⏳ стенд/решение | H7 (пороги ADC-clip), H8 (кривая RF-гейна) — только стенд; H12 (мёртвый noise gate, USB VID/PID) — стенд/решение lead; H13-остаток: сброс DSP (nr_first_time) на каждом band switch — стендовая проверка; H14/H15/H16/N11 — известные отклонения (не исправлялись, см. b6-verify) |
| Nizar-full/--full (task-40) | ✅ сделано | коммит e8a78590: opt-in полная сборка (без IS_SMALL_BUILD); full 573 088 / 48 840 / 63 972; стендовая, не продуктовая |
| Cleanup (task-41) | ✅ сделано | освобождено ≈23,2 ГиБ (рабочая папка 24,8 → 2,8 ГиБ); _audit/dev/cleanup.md |
| Docs-rebuild (task-42) | ✅ сделано | коммит 22fce856: STATUS/CHANGES/CHANGELOG/PATCHES/README/PLAN-DEV/CI/RELEASE |
| Docs-final (task-46) | ✅ сделано | актуализация docs по итогам Nizar-review (a4042337); отчёт _audit/dev/docs-final.md |
| Docs-sync (task-49) | ✅ сделано | финальные числа/статусы после 69ccd736 + исправление D1–D7 из b6-verify (AGC Hang, H13-остаток, override-мощность, H14/H15/H16/N11); отчёт _audit/dev/docs-sync.md |
| Финальные сборки B6 | ✅ сделано | default 323 304/42 684/63 476; FreeDV 567 040/48 840/63 972; full 573 088/48 840/63 972; O3 428 236/42 700/63 476; bl 31 620; 73 warnings; 1754 теста |
| B6-verify (task-48) | ✅ сделано | независимая проверка: код H1…H13 — PASS; host-тесты 1754 PASS, warning-гейт 73/73, ELF-дескрипторы H5 идентичны; H13 — PARTIAL (TP_IRQ исправлен, nr_first_time — стенд); документарные дефекты D1–D5 исправлены в docs-sync |
| Печатные PDF B6 (task-47/50) | ✅ сделано | release/bench-2026-11-27/printable/: чек-лист 2 стр., сводка 4 стр.; пересобраны после docs-sync (task-50), финальные числа, AGC Hang не обещан |
| Пакет release/bench-2026-11-27 | ✅ сделано | default/freedv/full/o3 + bl + rollback-v1.2 + docs + printable + README + SHA256SUMS (29/29 OK); zip `release/RS918_N96K-Pro-bench-2026-11-27.zip` |
| B6-package-verify (task-51) | ✅ сделано | независимая проверка пакета: SHA 29/29, бинарники/build-summary/hex/README сходятся, docs/PDF актуальны; вердикт «готов к стенду»; отчёт _audit/dev/b6-package-verify.md |
| Стенд H7/H8/H12/VID-PID и проверка фиксов | ⏳ план | docs/CHECKLIST.md: H1/H2/H3/H5/H4/H6/H9/H10/H11/H13 + H7/H8/H12; H13-nr_first_time |
| Полевые испытания (этап C) | ⏳ план | docs/TESTPLAN.md, docs/CHECKLIST.md; требует железа |
| v1.3 | ⏳ план | после пакета/стенда и решения lead: тег и release-пакет по docs/RELEASE.md |
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
- release/fieldtest-2026-11-27 (rev 2, состояние v1.2 + B1–B5): default fw 322 312 Б;
  FreeDV fw 565 552 Б; O3 fw 425 500 Б; bl 5.0.4 31 620 Б; build-summary/warnings каждой сборки;
  docs/CHECKLIST.md, docs/TESTPLAN.md, docs/FREEDV.md; SHA256SUMS.txt. Пакет для стенда, **не релиз**.
- release/bench-2026-11-27 (B6, финальные сборки; собирает lead): default **323 304**,
  freedv **567 040**, full **573 088**, o3 **428 236**, bl 31 620; build-summary/warnings,
  docs, printable (PDF/DOCX), rollback-v1.2, README, SHA256SUMS.

### 2.5. Вторая волна (ветка nizar-review, коммиты)
- **Nizar-audit (_audit/dev/nizar-audit.md)** — классификация diff ae93edf6..nizar-baseline
  (152 файла, +39 536/−19 299): 16 вредных/критичных изменений (high: H1 TX-границы, H2 TX-аудио,
  H3 CW-R, H5 USB-буферы), 14+ положительных (96k/12k, 32-бит кодек, фильтры, TX EQ/компрессор,
  11 точек IQ-калибровки, меню, CAT, USB 96k, тренировка реле, частоты в Гц), ~12 нейтральных.
  Кламп драйва PA 0,55 сохранён — «лимитов PA» Nizar не повышал; главный риск — отсутствие стенда.
- **H1/H2/H3 (aa5fdcb9)** — TX разрешён только в любительских участках четырёх расширенных
  RX-бэндов (10 м 28,0–29,7; 4 м 70,0–70,5; 2 м 144–146; 70 см 430–440 МГц), RX-покрытие не
  тронуто; дефолты TX-аудио возвращены к нейтральным (MIC 15, ALC postfilt 1, comp out 10,
  treble 4, EQ custom); CW-R/CW-USB восстановлен (меню, кнопки, CAT 0x02/0x03). Override
  внеполосной передачи (FLAGS1_TX_OUTSIDE_BANDS) сохранён, но для частот внутри расширенных
  RX-участков он даёт обычную per-band мощность (до клампа 0.55), а не пониженную — минимальную
  выставлять вручную, только с эквивалентом.
- **H5 (a4042337)** — в USB-TX возвращены guard+счётчик переполнения; USB-микрофон: индексация
  с wrap и учёт overflow (дублирование L=R — корректное моно→стерео, а не ZOH 48→96k); tSamFreq/
  bNrChannels выводятся из макросов — байты дескриптора не изменились (проверено по ELF).
- **H4/H6/H9/H10/H11/H13 (69ccd736)** — Si5351 PLL reset default 2 (пункт меню возвращён, форс
  «Never» убран); AGC-дефолты по upstream (slope 70, tau 4000/2000/500/250/50/500, thresh 60,
  hang 500/45) + ring_buffsize вместо %96 + меню Mode/Threshold; скрытый сдвиг SSB-MIC +15 Гц
  устранён (tx_bass_sheeft 0→15, явный пункт меню остаётся); полы CW-декодера 8/24→2/6;
  CAT TX_STATE отдаёт реальные PWR/SWR nibble; TP_IRQ — PULLUP/VERY_HIGH. H6-Hang-пункты
  намеренно не возвращены (активный AGC Nizar без стадии hang; поле занято «OFF Gain»).
  H12 (noise gate/VID-PID) и H13 nr_first_time (сброс DSP на band switch) — стенд/решение.
- **--full (e8a78590)** — python tools/build.py all --full: -DN96K_FULL_BUILD снимает
  IS_SMALL_BUILD (FreeDV + const-окна Ханна), плюс -DN96K_FREEDV (обязательная конфигурация
  памяти); загрузчик не меняется. На момент коммита (до H1…H13): 571 616 / 48 836 / 63 972;
  финальная стендовая сборка: **573 088 / 48 840 / 63 972**, 73 warnings.
- **Cleanup (task-41)** — освобождено **23 789,9 МиБ ≈ 23,2 ГиБ** (рабочая папка 24 812,5 →
  2 810,3 МиБ): build-артефакты, _tmp, O3-LTO логи; исходники/release/_backup/_audit
  не тронуты (SHA256 55/55 OK).
- **Docs-rebuild (22fce856)** — STATUS/CHANGES/CHANGELOG/PATCHES/README/PLAN-DEV/CI/RELEASE
  приведены к состоянию main; CHECKLIST/CHANGES подготовлены к пересборке PDF.
- **Финальные сборки B6 (build-bench-*)** — default 323 304/42 684/63 476; FreeDV
  567 040/48 840/63 972; full 573 088/48 840/63 972; O3 428 236/42 700/63 476; bl 31 620;
  73 warnings на всех вариантах; host-тесты 1754 PASS (0 failed, 3 known).
- **Печатные PDF B6 (task-47, _audit/dev/printable-b6.md)** — release/bench-2026-11-27/printable/:
  чек-лист 2 стр. A4 portrait и сводка 4 стр. A4 landscape, DOCX+PDF, шапка/футер
  «bench-2026-11-27, коммит 69ccd736» и «стр. X из Y»; с финальными числами. После docs-sync
  источники CHECKLIST/CHANGES обновлены (убраны пометки «после misc», Hang, H14–H16/N11) — печать
  пересобирается отдельным агентом; физическая печать не проверялась (только рендер).

## 3. 🔄 В процессе

- **Docs-sync (task-49, этот срез):** финальные числа/статусы после 69ccd736 во всех документах
  (STATUS/CHANGES/CHANGELOG/README/PATCHES/PLAN-DEV/BUILD/SAFETY/CHECKLIST/FREEDV);
  отчёт _audit/dev/docs-sync.md.
- **Пакет release/bench-2026-11-27 (lead):** сборка и проверка пакета (SHA256SUMS, README,
  синхронизация копий docs с обновлёнными; printable с финальными числами, пересборка после
  docs-sync — отдельным агентом). Документы репозитория
  обновляются этим срезом; копии в пакете должны быть перезалиты при финализации.

## 4. ⏳ План / backlog

1. **Финализировать пакет bench-2026-11-27 (lead):** синхронизировать docs-копии, SHA256SUMS,
   README пакета; решение по составу (default — продукт, --full/O3 — стенд).
2. **Стенд (полный список — docs/CHECKLIST.md):** H1 (TX-границы 10 м/4 м/2 м/70 см; override
   только с эквивалентом и с ручной минимальной мощностью), H2 (спектр TX после дефолтов),
   H3 (CW-R/CW-USB), H5 (USB-аудио), H4 (PLL reset), H6 (AGC; меню Mode/Threshold, Hang нет),
   H9 (нулевые биения SSB), H10 (CW-декодер 20–80 WPM), H11 (CAT PWR/SWR), H13 (тачскрин +
   20+ переключений бэнда с NR — остаток nr_first_time), H7/H8 (ADC-clip/RF-гейн с генератором/
   аттенюатором), H12/VID-PID — решение lead; H14/H15/H16/N11 — известные отклонения (не дефекты
   этой сборки).
3. **Полевые испытания (этап C)** по docs/TESTPLAN.md и docs/CHECKLIST.md: VSWR-защита и A1,
   загрузчик 5.0.4 (USB/DFU), NR/Tune, USB-аудио, CAT/Hamlib, 96k-панорама.
4. **v1.3** — после пакета/стенда и решения lead: merge второй волны в main, тег и release-пакет
   по docs/RELEASE.md (печатные PDF уже готовы; при изменениях — перегенерировать скриптом).
5. **Backlog B6+:**
   - F1 — ложные foldback-trip на фронте TX/после dynamic tune (нужен стенд; C-2 в _audit/dev/b3-audit.md);
   - watchdog IWDG (сейчас выключен: hardware/uhsdr_board.c — mchf_board_watchdog_init() закомментирован);
   - аналоговая защита PA (ток/КСВ) и первые ~50 мс TX без программной КСВ-защиты;
   - FreeDV-стенд: CPU/стек/звук, интеграция API 2.12.2 (700D/squelch), FreeDV↔NR остатки;
   - остальные бэкпорты 2.12.2: #1664 (DMA size), #1744 (CW buffer), #1675 (не для flash-конфига);
   - known issues: iir_3k.c:IIR_3k (мёртвый код, KNOWN), Zoom FFT placeholder ×2 (KNOWN);
   - бюджетные запасы: CCM свободно 2 060 Б (full — 1 564), загрузчик 1 148 Б (нужны гейты/контроль);
   - EEPROM-стратегия (версия раскладки/миграция; в т.ч. под USB identity — H12) и USB-аудио
     дескрипторы 96k (после стенда);
   - H7/F7 — поддержка сохраняется (решение 2026-11-27), vendor не удаляется.

## 5. ⛔ Блоки
- **Железо**: полевых/аппаратных испытаний не было — все выводы о VSWR, загрузчике, FreeDV,
  CAT/USB, 96k, TX-границах H1, спектре TX H2, ADC-clip H7, RF-гейне H8 и стендовых пунктах
  H4/H6/H9/H10/H11/H12/H13 — по коду, сборке и симуляции. Это главный остаточный риск (C-1).
- **Локальный Docker**: на машине разработки Docker не установлен; полная Docker-сборка
  выполняется только в GitHub Actions.
- **Печатные PDF**: собраны с финальными числами (task-47); после docs-sync пересобираются
  отдельным агентом (обновлённые источники CHECKLIST/CHANGES) — блок не критичен; физическая
  печать A4 не проверялась (только рендер PDF).

## 6. Текущие цифры (ветка nizar-review, HEAD 69ccd736; GCC 15.2.1)

| Конфигурация | fw-mchf.bin | RAM static | CCM | warnings / статус |
|---|---|---|---|---|
| default (-O2) | **323 304 Б** | 42 684 | 63 476 (свободно 2 060) | 73; host-тесты PASS |
| FreeDV opt-in (--freedv) | **567 040 Б** | 48 840 | 63 972 (свободно 1 564) | 73; на железе не было |
| full opt-in (--full) | **573 088 Б** | 48 840 | 63 972 (свободно 1 564) | 73; стендовая, на железе не было |
| O3 opt-in (--opt-level O3) | **428 236 Б** | 42 700 | 63 476 | 73; стендовая |

Загрузчик: **31 620 / 32 768 Б** (5.0.4, запас 1 148; не менялся). Host-тесты: **1754 проверки,
0 failed, 3 known**. Источник — build-bench-default/, build-bench-freedv/, build-bench-full/,
build-bench-o3/ (build-summary.json, warnings.log; 2026-11-27, HEAD 69ccd736).

Дельта к main ee5e06ed: default **+992 Б** (322 312 → 323 304), FreeDV **+1 488 Б**
(565 552 → 567 040), full **+1 472 Б** (571 616 → 573 088), O3 **+2 736 Б** (425 500 → 428 236),
RAM +16 Б (default; FreeDV/full 48 840, O3 42 700), CCM 0 (default/O3), +496 Б (FreeDV/full),
warnings 73 = 73. Это финальные числа B6 (после H1…H13); при дальнейших правках — пересобрать
и обновить таблицу.

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
  _audit/dev/ (nizar-audit, b6-radio-fix, b6-usb-fix, b6-misc-fix, nizar-full, printable-b6,
  docs-rebuild, docs-final, cleanup и др.).
- Релизы: release/v1.0, release/v1.1, release/v1.2, тестовые пакеты
  release/fieldtest-2026-11-27 и release/bench-2026-11-27 (собирает lead).

## 8. Правила обновления статуса

1. Статус меняет только lead или владелец этапа; документ синхронизируется с docs/CHANGELOG.md
   и docs/PATCHES.md.
2. Все цифры — только из build-summary.json/проверочных сборок/CI; если сборка не выполнялась —
   писать «не проверено», а не предполагаемое значение.
3. «Сделано» = код в ветке этапа + сборка/тесты PASS; «на железе не было» указывается отдельно.
4. Ветка nizar-review содержит все коммиты второй волны (HEAD 69ccd736), но не меняет статусы
   main до merge/тега (решение lead — после пакета и стенда).

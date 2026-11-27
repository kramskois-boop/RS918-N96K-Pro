# RS918 N96K-Pro — статус проекта

**Обновлено:** 2026-11-27 (GCC 9.3.1: причина зависания найдена и устранена; финальные сборки и
пакет пересобраны; следующий шаг — стенд на GCC 9.3.1).
**Ветка продукта:** main, HEAD **34d28730** = v1.2 + B1…B6 + обязательный GCC 9.3.1 (срез 47fadd11 —
финальный B6 на GCC 15.2.1, признан невалидным для железа).
**Причина зависания: ✅ найдена и устранена.** Сборки GCC 15.2.1 запускаются, показывают splash и
кратковременно UI, затем зависают (горят оба LED); GCC 9.3.1 работает на железе (проверено
пользователем: Firmware `2.9.48-n96kp3`, Build `Nov 27 2026 - 14:55:12`, Bootloader `5.0.4`).
Причина — разница компилятора/LTO (UB в коде Nizar); решение — **GCC 9.3.1 обязателен**:
автодетект `tools/build.py`, Docker/CI на 9.3.1-1.3, baseline 50.
**Сделано:** Nizar-audit; фиксы H1/H2/H3 (aa5fdcb9), H5 (a4042337), H4/H6/H9/H10/H11/H13
(69ccd736); opt-in --full (e8a78590); docs-rebuild/docs-sync; cleanup (~23 ГиБ); **все сборки
default/FreeDV/full/O3 пересобраны GCC 9.3.1** (312 216 / 555 608 / 561 720 / 412 536 Б, 50
предупреждений); **v1.0–v1.2 и прежний пакет (GCC 15.2.1) помечены как невалидные/не загружаемые**;
пакет **release/bench-2026-11-27** пересобран на GCC 9.3.1 (default/full/FreeDV/O3, known-good
bootloader 5.0.4, rollback-nizar-gcc9); host-тесты **1754 PASS** (0 failed, 3 known).
**Следующий шаг:** стенд на GCC 9.3.1 по docs/CHECKLIST.md — приёмка Build `Nov 27 2026 - 14:55:12`
и отсутствие зависания интерфейса. Живой CI на main (перезапуск с GCC9) — после push.

Легенда: ✅ сделано · 🔄 в процессе · ⏳ план · ⛔ блок.

## 1. Сводка статусов

| Область | Статус | Итог / ссылка |
|---|---|---|
| v1.0 — P0-патчи | ⚠️ собрана GCC 15.2.1 → **невалидна** | тег v1.0 (коммит 82056486), release/v1.0; fw 320 024 Б, bl 4.1.2 31 496 Б; такие прошивки на железе зависают после splash и **не загружаются** |
| v1.1 — P1/P2 | ⚠️ собрана GCC 15.2.1 → **невалидна** | тег v1.1 (коммит cf43b068), release/v1.1; fw 321 128 Б, bl 5.0.4 31 620 Б; на железе не загружается (GCC15) |
| v1.2 — этап A1/A2 | ⚠️ собрана GCC 15.2.1 → **невалидна** | тег v1.2 (annotated 4ca85bba → коммит dc204cc1, сборка/версия 619e9187); release/v1.2; fw 321 392 Б; на железе не загружается (GCC15) |
| B1 — тесты/CI/релиз | ✅ сделано | 1754 проверки; 3 job'а Actions; tools/release.py |
| B2 — документация | ✅ сделано | TESTPLAN 112 тестов, BOOTLOADER, VSWR, RELEASE + печатные DOCX/PDF |
| B3 — аудио/DSP hardening | ✅ сделано | fixed NR-буфер, сброс NR, ALC-ring OOB, DEBUG-диагностика |
| B4 — FreeDV opt-in | ✅ сделано | --freedv: FreeDV **555 608 Б** (GCC 9.3.1, RAM 48 552, CCM 63 972); историческая GCC15-цифра 567 040 невалидна; на железе не было |
| B5 — warning-гейт/фиксы/opt | ✅ сделано | baseline **50 предупреждений (16 текстов, GCC 9.3.1)** + гейт в CI; strcmp-фиксы, bounded float2fixedstr, docs/LIBS.md, --opt-level |
| **Причина зависания на железе (GCC 15.2.1)** | ✅ найдена и устранена | сборки GCC 15.2.1: splash → кратковременно UI → зависание (оба LED); GCC 9.3.1 работает (проверено на RS-918). Причина — разница компилятора/LTO (UB в коде Nizar); лечение — обязательный GCC 9.3.1 (build.py автодетект + предупреждение, Docker/CI pinned, baseline 50) |
| **Пересборка всех артефактов GCC 9.3.1** | ✅ сделано | default/full/FreeDV/O3 + bootloader пересобраны; verify_artifacts reference 312216; v1.0–v1.2 и прежний пакет помечены невалидными; host-тесты 1754 PASS |
| Публикация и CI | ✅ сделано | <https://github.com/kramskois-boop/RS918-N96K-Pro>; PR #1/#2 merged; живые прогоны Actions PASS |
| Тестовые прошивки | ⚠️ прежние невалидны | release/fieldtest-2026-11-27 и прежний bench-2026-11-27 — GCC 15.2.1, **не прошивать**; актуальный пакет пересобран на GCC 9.3.1 |
| Nizar-audit (task-39) | ✅ сделано | 16 вредных/критичных (H1–H16) и 14+ положительных изменений; ядро 96k/12k признано работоспособным; отчёт _audit/dev/nizar-audit.md |
| Nizar-fix H1/H2/H3 (task-43) | ✅ сделано | коммит aa5fdcb9: TX только в любительских участках, безопасные дефолты TX-аудио, CW-R/CW-USB |
| Nizar-fix H5 (task-44) | ✅ сделано | коммит a4042337: guard переполнения USB-TX, индексация/overflow USB-микрофона, дескрипторы от макросов |
| Nizar-fix misc H4/H6/H9/H10/H11/H13 (task-45) | ✅ сделано | коммит 69ccd736: Si5351 PLL reset + меню, upstream-дефолты AGC + меню Mode/Threshold, сдвиг +15 Гц (дефолт 0), минимумы CW-декодера, CAT PWR/SWR nibble, TP_IRQ pull-up/speed |
| H7/H8/H12/VID-PID + H13-остаток (аудит) | ⏳ стенд/решение | H7 (пороги ADC-clip), H8 (кривая RF-гейна) — только стенд; H12 (мёртвый noise gate, USB VID/PID) — стенд/решение lead; H13-остаток: сброс DSP (nr_first_time) на каждом band switch — стендовая проверка; H14/H15/H16/N11 — известные отклонения (не исправлялись, см. b6-verify) |
| Nizar-full/--full (task-40) | ✅ сделано (GCC 9.3.1) | коммит e8a78590: opt-in полная сборка (без IS_SMALL_BUILD); full **561 720 / 48 552 / 63 972**, Build `15:05:12`; стендовая, не продуктовая (историческая GCC15: 573 088/48 840/63 972 — невалидна) |
| Cleanup (task-41) | ✅ сделано | освобождено ≈23,2 ГиБ (рабочая папка 24,8 → 2,8 ГиБ); _audit/dev/cleanup.md |
| Docs-rebuild (task-42) | ✅ сделано | коммит 22fce856: STATUS/CHANGES/CHANGELOG/PATCHES/README/PLAN-DEV/CI/RELEASE |
| Docs-final (task-46) | ✅ сделано | актуализация docs по итогам Nizar-review (a4042337); отчёт _audit/dev/docs-final.md |
| Docs-sync (task-49) | ✅ сделано | финальные числа/статусы после 69ccd736 + исправление D1–D7 из b6-verify (AGC Hang, H13-остаток, override-мощность, H14/H15/H16/N11); отчёт _audit/dev/docs-sync.md |
| Финальные сборки B6 (GCC 9.3.1) | ✅ сделано | default **312 216/42 392/63 476**, Build `Nov 27 2026 - 14:55:12`; FreeDV **555 608/48 552/63 972**; full **561 720/48 552/63 972**; O3 **412 536/42 436/63 476**; bl GCC9 32 416 (в пакете known-good 5.0.4 31 620); 50 warnings (all; fw 46); 1754 теста |
| B6-verify (task-48) | ✅ сделано (GCC15-срез) | независимая проверка кода H1…H13 — PASS; ELF-дескрипторы H5 идентичны; H13 — PARTIAL (TP_IRQ исправлен, nr_first_time — стенд); для GCC9: host-тесты 1754 PASS, baseline 50; прежний warning-гейт 73/73 относился к GCC15 |
| Печатные PDF B6 (task-47/50) | ✅ сделано | release/bench-2026-11-27/printable/: чек-лист 2 стр., сводка 4 стр.; пересобраны после docs-sync (task-50), финальные числа, AGC Hang не обещан |
| Пакет release/bench-2026-11-27 (GCC 9.3.1) | 🔄 пересобран, финализирует lead | default/freedv/full/o3 (GCC9) + known-good bootloader 5.0.4 + rollback-nizar-gcc9 + docs + README + SHA256SUMS; прежний GCC15-zip — **невалиден** |
| B6-package-verify (task-51) | ⚠️ проверка была для GCC15-пакета | прежний вердикт «готов к стенду» относился к GCC 15.2.1 и **отозван**: такие прошивки на железе зависают; пакет пересобран на GCC 9.3.1 (task-52) |
| **Стенд на GCC 9.3.1** | ⏳ следующий шаг | пакет bench-2026-11-27 (GCC9): приёмка Build `Nov 27 2026 - 14:55:12` + отсутствие зависания UI; затем H1/H2/H3/H5/H4/H6/H9/H10/H11/H13 + H7/H8/H12 (docs/CHECKLIST.md) |
| Полевые испытания (этап C) | ⏳ план | docs/TESTPLAN.md, docs/CHECKLIST.md; требует железа |
| v1.3 | ⏳ план | после пакета/стенда и решения lead: тег и release-пакет по docs/RELEASE.md |
| Backlog B6+ | ⏳ план | FreeDV-стенд, F1 (ложные trip), watchdog IWDG, аналоговая защита PA, бэкпорты 2.12.2 |
| Железо для полевых тестов | 🔄 частично | GCC9-прошивка проверена на RS-918 (загружается, UI работает); VSWR/загрузчик-обновление/FreeDV/CAT/USB/96k-выводы ещё требуют стенда |
| Локальный docker build | ⛔ блок | Docker на машине разработки не установлен; воспроизводимость даёт CI-раннер |

## 2. ✅ Сделано (main 34d28730; сборки v1.0–v1.2 — GCC15, невалидны)

### 2.1. Релизы (все — GCC 15.2.1: **невалидны для железа**, не прошивать)
- **v1.0** — P0-1…P0-6 (EEPROM, сборка, аудио, PA/КСВ, TX-безопасность, брендинг), release/v1.0.
- **v1.1** — P1-1…P1-5, P2-1…P2-4 (VSWR порог/индикация/скорость, EEPROM fallback, CAT-гейты,
  загрузчик 5.0.4, FreeDV vendor rev 4121, CI/тесты, AGC/Tune/CW-бэкпорты), release/v1.1.
- **v1.2** — этап A: A1 (минимальный драйв при активном foldback, завершение trial в гистерезисе),
  A2 (NR+FreeDV #1749), версия n96kp3; release/v1.2 + печатные документы
  (release/v1.2/printable/) и полевая тестовая сборка.
- **Причина невалидности одна:** GCC 15.2.1 — прошивка стартует, показывает splash и кратковременно
  UI, затем зависает (оба LED). На железе работает только GCC 9.3.1; теги/артефакты оставлены
  как история, но **не загружаются** (см. §2.4, docs/BUILD.md §1.1).

### 2.2. Инструментальные этапы (main)
- **B1** — host-тесты (1754 проверки, 0 failed, 3 known), CI-workflow из 3 job'ов, release-job
  по тегу, tools/release.py, tools/check_workflow.py; живой CI на push в main.
- **B2** — docs/TESTPLAN.md (112 уникальных тестов), BOOTLOADER.md, VSWR.md, RELEASE.md,
  печатные чек-лист и сводная таблица (PDF/DOCX).
- **B3** — надёжность/DSP: VLA в audio ISR заменён фиксированным буфером, согласованный сброс NR,
  выравнивание ALC-кольца при SSB↔AM/FM (был OOB до 24 float в CCM), DEBUG-счётчики ISR/DMA/NR.
- **B4** — opt-in FreeDV: --freedv, 6 guard-фиксов, MODEM_STATS только под opt-in сборку,
  docs/FREEDV.md, бюджет и план стенда; default-сборка не изменилась.
- **B5** — warning-гейт (tools/check_warnings.py + tests/warnings-baseline.txt, **50/16** на GCC 9.3.1),
  два реальных дефекта -Waddress (strcmp), bounded float2fixedstr (латентный OOB),
  docs/LIBS.md (пины вендорных библиотек), --opt-level O2/O3/Os.

### 2.3. Публикация и CI
- Репозиторий public: <https://github.com/kramskois-boop/RS918-N96K-Pro>; main + теги
  v1.0 / v1.1 / v1.2.
- PR: [#1](https://github.com/kramskois-boop/RS918-N96K-Pro/pull/1) (B3+B4, merged 2026-11-27),
  [#2](https://github.com/kramskois-boop/RS918-N96K-Pro/pull/2) (B5, merged 2026-11-27).
- Исторический прогон на main ee5e06ed (GCC15-тулчейн):
  [run 45971492786](https://github.com/kramskois-boop/RS918-N96K-Pro/actions/runs/45971492786) —
  **success** (host-tests 10 с, docker-build 2 мин 3 с, release skipped — не тег). Dockerfile теперь
  pinned на GCC 9.3.1-1.3; живой прогон с новым тулчейном — при следующем push.
- Release-job проверен на временном теге v0.0.0-citest (артефакты → release.py → sha256sum -c
  → GitHub Release); тестовый тег/релиз удалены.

### 2.4. Тестовые прошивки
- **Исторические (GCC 15.2.1, невалидны):** release/fieldtest-2026-11-27 (v1.2 + B1–B5: fw
  322 312, FreeDV 565 552, O3 425 500) и прежний release/bench-2026-11-27 (B6: fw 323 304,
  FreeDV 567 040, full 573 088, O3 428 236, 73 warnings) — на железе зависают после splash, **не
  прошивать**.
- **Актуальный пакет release/bench-2026-11-27 (пересобран GCC 9.3.1, main 34d28730):** default
  **312 216** (Build `Nov 27 2026 - 14:55:12`), full **561 720** (`15:05:12`), FreeDV **555 608**
  (`15:05:45`), O3 **412 536** (`15:06:17`); известный рабочий загрузчик 5.0.4 31 620 Б;
  rollback-nizar-gcc9 (проверенная оригинальная Nizar GCC9); build-summary/warnings каждой
  сборки; docs, README, SHA256SUMS. **Не релиз**, а прошивки для стенда — но первый пригодный
  для железа пакет.

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
  памяти); загрузчик не меняется. Финальная стендовая сборка **GCC 9.3.1** (main 34d28730):
  **561 720 / 48 552 / 63 972**, 50 warnings, Build `Nov 27 2026 - 15:05:12` (историческая
  GCC15-сборка 573 088/48 840/63 972 невалидна).
- **Cleanup (task-41)** — освобождено **23 789,9 МиБ ≈ 23,2 ГиБ** (рабочая папка 24 812,5 →
  2 810,3 МиБ): build-артефакты, _tmp, O3-LTO логи; исходники/release/_backup/_audit
  не тронуты (SHA256 55/55 OK).
- **Docs-rebuild (22fce856)** — STATUS/CHANGES/CHANGELOG/PATCHES/README/PLAN-DEV/CI/RELEASE
  приведены к состоянию main; CHECKLIST/CHANGES подготовлены к пересборке PDF.
- **Финальные сборки B6 — пересобраны GCC 9.3.1 (main 34d28730):** default **312 216/42 392/
  63 476** (`Nov 27 2026 - 14:55:12`); FreeDV **555 608/48 552/63 972** (`15:05:45`); full
  **561 720/48 552/63 972** (`15:05:12`); O3 **412 536/42 436/63 476** (`15:06:17`);
  bl GCC9 all-сборки 32 416 (в пакете — known-good 5.0.4 31 620); 50 warnings (all; fw 46);
  host-тесты 1754 PASS (0 failed, 3 known). Прежние GCC15-цифры (323 304/567 040/573 088/428 236,
  73 warnings) **невалидны** — прошивки зависают на железе.
- **Печатные PDF B6 (task-47)** — release/bench-2026-11-27/printable/: чек-лист 2 стр. A4
  portrait и сводка 4 стр. A4 landscape (DOCX+PDF). **Внимание:** PDF собраны с GCC15-числами
  (323 304/573 088/567 040/428 236, 73 warnings) — **невалидны**; после обновления docs/CHECKLIST.md
  и docs/CHANGES.md их нужно пересобрать (вне этого среза). Физическая печать не проверялась.

## 3. 🔄 В процессе

- **Пакет release/bench-2026-11-27 (lead):** пересобран на GCC 9.3.1 (default — проверенный на
  железе Build `14:55:12`; full/FreeDV/O3 — GCC9; known-good bootloader 5.0.4; rollback-nizar-gcc9);
  финализация SHA256SUMS/README и синхронизация копий docs — за lead. Прежний GCC15-zip
  (`release/RS918_N96K-Pro-bench-2026-11-27.zip`) — **невалиден**.
- **Печатные PDF:** в пакете — GCC15-версия (невалидные числа); пересборка из обновлённых
  CHECKLIST/CHANGES — отдельным шагом (не входила в task-52).
- **CI/Docker:** Dockerfile и CI переведены на GCC 9.3.1-1.3 (SHA 4845d261…3686), reference
  verify_artifacts 312216; живой прогон Actions с новым тулчейном — при следующем push
  (Docker на машине разработки не установлен).

## 4. ⏳ План / backlog

1. **Стенд на GCC 9.3.1 — следующий шаг (полный список — docs/CHECKLIST.md):** приёмка — System
   Info Build `Nov 27 2026 - 14:55:12` и **отсутствие зависания UI** (GCC15-сборки зависают после
   splash); затем H1 (TX-границы 10 м/4 м/2 м/70 см; override только с эквивалентом и с ручной
   минимальной мощностью), H2 (спектр TX после дефолтов), H3 (CW-R/CW-USB), H5 (USB-аудио),
   H4 (PLL reset), H6 (AGC; меню Mode/Threshold, Hang нет), H9 (нулевые биения SSB), H10
   (CW-декодер 20–80 WPM), H11 (CAT PWR/SWR), H13 (тачскрин + 20+ переключений бэнда с NR —
   остаток nr_first_time), H7/H8 (ADC-clip/RF-гейн с генератором/аттенюатором), H12/VID-PID —
   решение lead; H14/H15/H16/N11 — известные отклонения.
2. **Финализировать пакет bench-2026-11-27 (lead):** GCC9-артефакты, SHA256SUMS, README,
   синхронизация docs-копий; printable PDF — пересобрать с GCC9-числами (вне этого среза).
3. **Полевые испытания (этап C)** по docs/TESTPLAN.md и docs/CHECKLIST.md: VSWR-защита и A1,
   загрузчик 5.0.4 (USB/DFU), NR/Tune, USB-аудио, CAT/Hamlib, 96k-панорама.
4. **v1.3** — после стенда на GCC 9.3.1 и решения lead: тег и release-пакет из GCC9-сборки по
   docs/RELEASE.md (печатные PDF пересобрать с GCC9-числами при подготовке релиза).
5. **Backlog B6+:**
   - F1 — ложные foldback-trip на фронте TX/после dynamic tune (нужен стенд; C-2 в _audit/dev/b3-audit.md);
   - watchdog IWDG (сейчас выключен: hardware/uhsdr_board.c — mchf_board_watchdog_init() закомментирован);
   - аналоговая защита PA (ток/КСВ) и первые ~50 мс TX без программной КСВ-защиты;
   - FreeDV-стенд: CPU/стек/звук, интеграция API 2.12.2 (700D/squelch), FreeDV↔NR остатки;
   - остальные бэкпорты 2.12.2: #1664 (DMA size), #1744 (CW buffer), #1675 (не для flash-конфига);
   - known issues: iir_3k.c:IIR_3k (мёртвый код, KNOWN), Zoom FFT placeholder ×2 (KNOWN);
   - бюджетные запасы: CCM свободно 2 060 Б (full — 1 564); загрузчик: известный рабочий 5.0.4
     31 620 Б (запас 1 148), GCC9 all-сборка 32 416 Б (запас 352 — CI, следующий рост кода
     загрузчика может не влезть);
   - EEPROM-стратегия (версия раскладки/миграция; в т.ч. под USB identity — H12) и USB-аудио
     дескрипторы 96k (после стенда);
   - H7/F7 — поддержка сохраняется (решение 2026-11-27), vendor не удаляется.

## 5. ⛔ Блоки / ограничения
- **Железо:** GCC9-прошивка проверена на RS-918 (загружается, UI работает); но VSWR-защита,
  обновление загрузчика, FreeDV, CAT/USB-аудио, 96k-панорама и спектр TX на реальном железе ещё
  не проверены — по ним выводы пока по коду/сборке/симуляции (главный остаточный риск C-1).
- **Локальный Docker**: на машине разработки Docker не установлен; Docker-сборка с GCC 9.3.1-1.3
  проверяется только в GitHub Actions (живой прогон с новым тулчейном — при следующем push).
- **Печатные PDF**: в пакете — GCC15-версия (невалидные числа); пересборка из обновлённых
  CHECKLIST/CHANGES — отдельный шаг; физическая печать A4 не проверялась (только рендер PDF).

## 6. Текущие цифры (main HEAD 34d28730; GCC 9.3.1, -O2)

| Конфигурация | fw-mchf.bin | RAM static | CCM | warnings / статус | Build (System Info) |
|---|---|---|---|---|---|
| default (-O2) | **312 216 Б** | 42 392 | 63 476 (свободно 2 060) | 50 (all; fw 46); host-тесты PASS | `Nov 27 2026 - 14:55:12` |
| FreeDV opt-in (--freedv) | **555 608 Б** | 48 552 | 63 972 (свободно 1 564) | 50; на железе не было | `Nov 27 2026 - 15:05:45` |
| full opt-in (--full) | **561 720 Б** | 48 552 | 63 972 (свободно 1 564) | 50; стендовая, на железе не было | `Nov 27 2026 - 15:05:12` |
| O3 opt-in (--opt-level O3) | **412 536 Б** | 42 436 | 63 476 | 50; стендовая | `Nov 27 2026 - 15:06:17` |

Загрузчик: в пакете — известный рабочий **5.0.4 = 31 620 / 32 768 Б** (GCC 15.2.1, не менялся;
прошивать только при необходимости); GCC9-сборка `all` — **32 416 / 32 768 Б** (запас 352;
CI/Docker, в пакет не входит). Host-тесты: **1754 проверки, 0 failed, 3 known**. Warning-гейт:
**50 = baseline** (16 различных текстов, лимит 50). Источник — `build-final-gcc9/`,
`build-gcc9-full/`, `build-gcc9-freedv/`, `build-gcc9-o3/` (build-summary.json, warnings.log;
2026-11-27, GCC 9.3.1).

Исторические GCC15-цифры (**невалидны**, прошивки зависают на железе): default 323 304/42 684/63 476,
FreeDV 567 040/48 840/63 972, full 573 088/48 840/63 972, O3 428 236/42 700/63 476, bl 31 620,
73 warnings. Дельта GCC9 к ним: default **−11 088 Б**, FreeDV **−11 432 Б**, full **−11 368 Б**,
O3 **−15 700 Б**, RAM default **−292 Б**, CCM без изменений, warnings 50 вместо 73.

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
- Релизы (GCC 15.2.1, **невалидны/не загружаются**): release/v1.0, release/v1.1, release/v1.2.
- Тестовые пакеты: прежние release/fieldtest-2026-11-27 и bench-2026-11-27 — GCC15 (невалидны);
  актуальный **release/bench-2026-11-27** — пересобран GCC 9.3.1 (default Build `14:55:12`).

## 8. Правила обновления статуса

1. Статус меняет только lead или владелец этапа; документ синхронизируется с docs/CHANGELOG.md
   и docs/PATCHES.md.
2. Все цифры — только из build-summary.json/проверочных сборок/CI; если сборка не выполнялась —
   писать «не проверено», а не предполагаемое значение.
3. «Сделано» = код в ветке этапа + сборка/тесты PASS; «на железе не было» указывается отдельно.
4. Ветка nizar-review содержит все коммиты второй волны (HEAD 69ccd736), но не меняет статусы
   main до merge/тега (решение lead — после пакета и стенда).
5. **Артефакты для железа — только GCC 9.3.1.** Сборки GCC 15.2.1 (и любые другие) допускаются
   как проверка компиляции/тестов, но не как прошивка/релиз/пакет: на железе они зависают после
   splash. Числа GCC15-сборок в документах должны быть помечены «GCC15, невалидно».

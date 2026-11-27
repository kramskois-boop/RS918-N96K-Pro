# RS918 N96K-Pro — статус проекта

**Обновлено:** 2026-11-27 (этап docs-rebuild, вторая волна работ).
**Ветка продукта:** `main`, HEAD **ee5e06ed** = v1.2 + B1 + B2 + B3 + B4 + B5.
**Текущая работа:** ветка `nizar-review` (создана от `main`): Nizar-audit, Nizar-full/full-build,
cleanup, docs-rebuild. На момент среза изменения второй волны не закоммичены — коммитит lead.

Легенда: ✅ сделано · 🔄 в процессе · ⏳ план · ⛔ блок.

## 1. Сводка статусов

| Область | Статус | Итог / ссылка |
|---|---|---|
| v1.0 — P0-патчи | ✅ сделано | тег `v1.0` (коммит `82056486`), `release/v1.0`; fw 320 024 Б, bl 4.1.2 31 496 Б |
| v1.1 — P1/P2 | ✅ сделано | тег `v1.1` (коммит `cf43b068`), `release/v1.1`; fw 321 128 Б, bl 5.0.4 31 620 Б |
| v1.2 — этап A1/A2 | ✅ сделано | тег `v1.2` (annotated `4ca85bba` → коммит `dc204cc1`, сборка/версия `619e9187`); `release/v1.2`; fw 321 392 Б |
| B1 — тесты/CI/релиз | ✅ сделано | 1754 проверки; 3 job'а Actions; `tools/release.py` |
| B2 — документация | ✅ сделано | TESTPLAN 112 тестов, BOOTLOADER, VSWR, RELEASE + печатные DOCX/PDF |
| B3 — аудио/DSP hardening | ✅ сделано | fixed NR-буфер, сброс NR, ALC-ring OOB, DEBUG-диагностика |
| B4 — FreeDV opt-in | ✅ сделано | `python tools/build.py all --freedv`: fw 565 552 Б, RAM 48 836, CCM 63 972; на железе не было |
| B5 — warning-гейт/фиксы/opt | ✅ сделано | 73 warnings baseline + гейт в CI; `strcmp`-фиксы, bounded `float2fixedstr`, docs/LIBS.md, `--opt-level` |
| Публикация и CI | ✅ сделано | <https://github.com/kramskois-boop/RS918-N96K-Pro>; PR #1/#2 merged; живые прогоны Actions PASS |
| Тестовые прошивки | ✅ сделано | `release/fieldtest-2026-11-27` (default + FreeDV + O3 + bl, чек-лист, SHA256SUMS) |
| Nizar-audit (task-39) | 🔄 в процессе | классификация изменений Nizar (вредные/критичные/полезные), отчёт `_audit/dev/nizar-audit.md` |
| Nizar-full/full-build (task-40) | 🔄 в процессе | opt-in `--full` (снятие `IS_SMALL_BUILD`), сборка/замеры, отчёт `_audit/dev/nizar-full.md`; цифры — после второй волны |
| Cleanup (task-41) | 🔄 в процессе | удаление мусора вне исходников/бэкапа/аудита, отчёт `CLEANUP-REPORT.md` |
| Docs-rebuild (task-42) | 🔄 в процессе | этот документ + CHANGES/CHANGELOG/PATCHES/README/PLAN-DEV/CI/RELEASE; PDF — вторая волна |
| Полевые испытания (этап C) | ⏳ план | docs/TESTPLAN.md, docs/CHECKLIST.md; требует железа |
| v1.3 | ⏳ план | после второй волны: итоговые цифры, решение по `--full`, cleanup, PDF |
| Backlog B6 | ⏳ план | FreeDV-стенд, F1 (ложные trip), watchdog IWDG, аналоговая защита PA, бэкпорты 2.12.2 |
| Железо для полевых тестов | ⛔ блок | нет доступа к RS-918; все VSWR/загрузчик/FreeDV/CAT-выводы — только код/сборка/симуляция |
| Локальный `docker build` | ⛔ блок | Docker на машине разработки не установлен; воспроизводимость даёт CI-раннер |

## 2. ✅ Сделано (main ee5e06ed)

### 2.1. Релизы
- **v1.0** — P0-1…P0-6 (EEPROM, сборка, аудио, PA/КСВ, TX-безопасность, брендинг), `release/v1.0`.
- **v1.1** — P1-1…P1-5, P2-1…P2-4 (VSWR порог/индикация/скорость, EEPROM fallback, CAT-гейты,
  загрузчик 5.0.4, FreeDV vendor rev 4121, CI/тесты, AGC/Tune/CW-бэкпорты), `release/v1.1`.
- **v1.2** — этап A: A1 (минимальный драйв при активном foldback, завершение trial в гистерезисе),
  A2 (NR+FreeDV #1749), версия `n96kp3`; `release/v1.2` + печатные документы
  (`release/v1.2/printable/`) и полевая тестовая сборка.

### 2.2. Инструментальные этапы (main)
- **B1** — host-тесты (1754 проверки, 0 failed, 3 known), CI-workflow из 3 job'ов, release-job
  по тегу, `tools/release.py`, `tools/check_workflow.py`; живой CI на push в `main`.
- **B2** — docs/TESTPLAN.md (112 уникальных тестов), BOOTLOADER.md, VSWR.md, RELEASE.md,
  печатные чек-лист и сводная таблица (PDF/DOCX).
- **B3** — надёжность/DSP: VLA в audio ISR заменён фиксированным буфером, согласованный сброс NR,
  выравнивание ALC-кольца при SSB↔AM/FM (был OOB до 24 float в CCM), DEBUG-счётчики ISR/DMA/NR.
- **B4** — opt-in FreeDV: `--freedv`, 6 guard-фиксов, `MODEM_STATS` только под opt-in сборку,
  docs/FREEDV.md, бюджет и план стенда; default-сборка не изменилась.
- **B5** — warning-гейт (`tools/check_warnings.py` + `tests/warnings-baseline.txt`, 73/35),
  два реальных дефекта `-Waddress` (сравнение указателей с литералами → `strcmp`), bounded
  `float2fixedstr` (латентный OOB), docs/LIBS.md (пины вендорных библиотек), `--opt-level O2/O3/Os`.

### 2.3. Публикация и CI
- Репозиторий public: <https://github.com/kramskois-boop/RS918-N96K-Pro>; `main` + теги
  `v1.0` / `v1.1` / `v1.2`.
- PR: [#1](https://github.com/kramskois-boop/RS918-N96K-Pro/pull/1) (B3+B4, merged 2026-11-27),
  [#2](https://github.com/kramskois-boop/RS918-N96K-Pro/pull/2) (B5, merged 2026-11-27).
- Последний прогон на `main` ee5e06ed:
  [run 45971492786](https://github.com/kramskois-boop/RS918-N96K-Pro/actions/runs/45971492786) —
  **success** (host-tests 10 с, docker-build 2 мин 3 с, release skipped — не тег).
- Release-job проверен на временном теге `v0.0.0-citest` (артефакты → release.py → sha256sum -c
  → GitHub Release); тестовый тег/релиз удалены.

### 2.4. Тестовые прошивки
- `release/fieldtest-2026-11-27` (rev 2): default fw 322 312 Б; FreeDV fw 565 552 Б;
  O3 fw 425 500 Б; bl 5.0.4 31 620 Б; build-summary/warnings каждой сборки; docs/CHECKLIST.md,
  docs/TESTPLAN.md, docs/FREEDV.md; `SHA256SUMS.txt`. Пакет для стенда, **не релиз**.

## 3. 🔄 В процессе (ветка nizar-review, незакоммичено)
- **Nizar-audit** (task-39): классификация diff `ae93edf6..nizar-baseline` — что в Nizar вредит/
  критично (IS_SMALL_BUILD, лимиты, выключенные функции, буферы 96k/12k) и что стоит внести в общую
  сборку; отчёт `_audit/dev/nizar-audit.md`.
- **Nizar-full** (task-40): opt-in `--full` (снятие `IS_SMALL_BUILD`; FreeDV + predefined FFT
  windows), сборка/замеры flash/RAM/CCM/warnings, host-тесты; default не меняется; отчёт
  `_audit/dev/nizar-full.md`. На момент среза: правки `tools/build.py` + `uhsdr_board.h` в работе.
- **Cleanup** (task-41): инвентаризация и удаление мусора (build*/release-временные, `_tmp`,
  temp-логи) без исходников/бэкапа/аудита/релизов; отчёт `CLEANUP-REPORT.md`.
- **Docs-rebuild** (task-42): docs/STATUS.md (этот файл), сводка изменений (docs/CHANGES.md,
  docs/CHANGELOG.md), обновление README/PLAN-DEV/PATCHES/CI/RELEASE, подготовка CHECKLIST/CHANGES
  к пересборке PDF. Итоговые цифры второй волны — в STATUS/CHANGES после сборок.

## 4. ⏳ План / backlog
1. **Полевые испытания (этап C)** по docs/TESTPLAN.md и docs/CHECKLIST.md: VSWR-защита и A1,
   загрузчик 5.0.4 (USB/DFU), NR/Tune, USB-аудио, CAT/Hamlib, 96k-панорама.
2. **v1.3** — после второй волны: итоговые цифры, решение по `--full`/Nizar-ограничениям,
   чистка дерева, пересборка печатных PDF (CHECKLIST/CHANGES) и release-пакета.
3. **Backlog B6:**
   - F1 — ложные foldback-trip на фронте TX/после dynamic tune (нужен стенд; C-2 в `_audit/dev/b3-audit.md`);
   - watchdog IWDG (сейчас выключен: `hardware/uhsdr_board.c` — `mchf_board_watchdog_init()` закомментирован);
   - аналоговая защита PA (ток/КСВ) и первые ~50 мс TX без программной КСВ-защиты;
   - FreeDV-стенд: CPU/стек/звук, интеграция API 2.12.2 (700D/squelch), FreeDV↔NR остатки;
   - остальные бэкпорты 2.12.2: `#1664` (DMA size), `#1744` (CW buffer), `#1675` (не для flash-конфига);
   - known issues: `iir_3k.c:IIR_3k` (мёртвый код, KNOWN), Zoom FFT placeholder ×2 (KNOWN);
   - бюджетные запасы: CCM свободно 2 060 Б, загрузчик 1 148 Б (нужны гейты/контроль);
   - EEPROM-стратегия (версия раскладки/миграция) и USB-аудио дескрипторы 96k (после стенда);
   - H7/F7 — поддержка сохраняется (решение 2026-11-27), vendor не удаляется.

## 5. ⛔ Блоки
- **Железо**: полевых/аппаратных испытаний не было — все выводы о VSWR, загрузчике, FreeDV,
  CAT/USB, 96k — по коду, сборке и симуляции. Это главный остаточный риск (C-1).
- **Локальный Docker**: на машине разработки Docker не установлен; полная Docker-сборка
  выполняется только в GitHub Actions.
- **PDF второй волны**: печатные CHECKLIST/CHANGES пересобираются после получения итоговых цифр
  второй волны (сборки `--full`/cleanup), чтобы не публиковать устаревшие числа.

## 6. Текущие цифры (main ee5e06ed, GCC 15.2.1)

| Конфигурация | fw-mchf.bin | RAM static | CCM | warnings | Статус |
|---|---|---|---|---|---|
| default (-O2) | **322 312 Б** | 42 668 | 63 476 (свободно 2 060) | 73 | проверено, host-тесты PASS |
| FreeDV opt-in (`--freedv`) | **565 552 Б** | 48 836 | 63 972 (свободно 1 564) | 73 | собрано, на железе не было |
| O3 opt-in (`--opt-level O3`) | **425 500 Б** | 42 696 | 63 476 | 73 | собрано, стенд впереди |

Загрузчик: **31 620 / 32 768 Б** (5.0.4, запас 1 148). Host-тесты: **1754 проверки, 0 failed,
3 known**. Цифры — из свежих проверочных сборок `build-test` / `build-test-freedv` /
`build-test-o3` (2026-11-27) и пакета `release/fieldtest-2026-11-27`; при сверке ориентир —
`build-summary.json`. Вторая волна (Nizar-review/full-build) цифры может изменить — они появятся
после сборок и будут отмечены как in-progress.

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
- Аудит/отчёты (вне репозитория): `E:\harnet\sdr 11\_audit\REPORT.md`, `_audit/parts/`,
  `_audit/dev/` (b3-audit, b5-warn, b5-opt, b5-libs и др.).
- Релизы: `release/v1.0`, `release/v1.1`, `release/v1.2`, тестовый пакет
  `release/fieldtest-2026-11-27`.

## 8. Правила обновления статуса

1. Статус меняет только lead или владелец этапа; документ синхронизируется с `docs/CHANGELOG.md`
   и `docs/PATCHES.md`.
2. Все цифры — только из `build-summary.json`/проверочных сборок/CI; если сборка не выполнялась —
   писать «не проверено», а не предполагаемое значение.
3. «Сделано» = код в `main` + сборка/тесты PASS; «на железе не было» указывается отдельно.
4. Ветка `nizar-review` не меняет статусы `main` до коммита/тега.

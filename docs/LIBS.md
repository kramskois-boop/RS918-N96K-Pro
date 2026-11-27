# Сторонние библиотеки: версии и политика обновления

Статус: B5 (ветка `b5`, база main `a765f41c` = B3+B4), 2026-11-27.
Документ фиксирует **пины версий** вендорных библиотек прошивки RS918 N96K-Pro и порядок их
безопасного обновления. Полный аудит с матрицей рисков — `_audit/dev/b5-libs.md` (вне репозитория).

## Состав прошивки (BUILDFOR=F4, mcHF/RS-918)

| Компонент | Каталог | Версия | Доказательство в дереве | Локальные отличия |
|---|---|---|---|---|
| CMSIS Core (Cortex-M4) | `basesw/mcHF/Drivers/CMSIS/Include` | V4.30 | `core_cm4.h:4` | 1 файл: `arm_math.h` (+ прототипы `*_Nizar`); остальное — только EOL |
| CMSIS Device STM32F4xx | `basesw/mcHF/Drivers/CMSIS/Device/ST/STM32F4xx` | 2.6.0 | `stm32f4xx.h:129-131` | нет функциональных правок |
| CMSIS-DSP | `basesw/mcHF/Drivers/CMSIS/DSP_Lib` | V1.4.5b (2015-10-20) | `arm_math.h:4-5` | 22 файла Source: `*_Nizar`-функции и микрооптимизации Nizar |
| STM32F4 HAL | `basesw/mcHF/Drivers/STM32F4xx_HAL_Driver` | 1.6.0 | `Src/stm32f4xx_hal.c:73-76` | 5 файлов (`hal.c`, `hal_gpio.c`, `hal_i2c.c`, `hal.h`, `hal_i2s_ex.h`) |
| STM32 USB Device Library | `basesw/mcHF/Middlewares/ST/STM32_USB_Device_Library` | V2.4.2 | `Core/Src/usbd_core.c:5` | 2 файла (`usbd_core.c`, `usbd_def.h`) |
| STM32 USB Host Library | `basesw/mcHF/Middlewares/ST/STM32_USB_Host_Library` | V3.2.2 | `Core/Src/usbh_core.c:5` | как в 2.9.48; upstream 2.12.2 удалил 17 неиспользуемых class-файлов (AUDIO/CDC/HID/MTP), HID перенесён в `drivers/usb/host/class/HID` |
| FatFs | `basesw/mcHF/Middlewares/Third_Party/FatFs` | R0.11 (32020), 2015 | `src/ff.h:2,20` | нет (семантически = upstream 2.12.2) |
| FreeDV / Codec2 | `drivers/freedv` | Codec2 **rev 4121**, FreeDV API **v12** | UHSDR `b606547f` (2019-03-21, «FreeDV Update to rev 4121»); `freedv_api.c:68` | синхронизировано с UHSDR 2.12.2: 190/193 файла совпадают по git-blob, 3 файла — N96K P2/B4 (`freedv_api.c/.h`, `freedv_api_internal.h`); каталог `codebook/.svn` (61 файл мусора SVN) не переносится |
| intelhex (утилита `support/hex2dfu`) | `support/hex2dfu/intelhex` | 1.5 | `__init__.py:37` | Python-2 утилита, в прошивку не входит |
| newlib-nano | toolchain (ARM GCC 15.2.1) | внешний | `--specs=nano.specs` | не вендорится |

## Зафиксированные деревья (пины B5)

SHA-1 git-дерева (детерминирован по содержимому); проверка:
`git -C <repo> rev-parse HEAD:<путь>`.

| Каталог | Дерево продукта (B5) | UHSDR v2.9.48 `ae93edf6` | UHSDR v2.12.2 `dcbebe5f` |
|---|---|---|---|
| `drivers/freedv` | `a8d7224fbe21b15630de8d74fa81372b5ac57814` | `647d567b53a1704dbd71871a16b58cc849e9c4ff` | `05fc8494ee53d4f0ac285d42d0847e89c0262237` (вкл. `.svn`) |
| `basesw/mcHF/Drivers/CMSIS` | `7e55dfcbd6b0984a63342248f8ddc1d7acc80100` | `9d7950612dc1d3d1078c1d8dc7f6e3b1a421bc6f` | `9d795061...` (= 2.9.48) |
| `basesw/mcHF/Drivers/STM32F4xx_HAL_Driver` | `f6dc0d89db4acde6d3bbc486657440dacfb4172d` | `8ea1b797305a1fcda30651ae648e2836f9a0a1de` | `8ea1b797...` (= 2.9.48) |
| `basesw/mcHF/Middlewares/Third_Party/FatFs` | `0c12e7e232766412c5d668dec4f8cb894049e533` | `0c12e7e2...` | `0c12e7e2...` |
| `basesw/mcHF/Middlewares/ST/STM32_USB_Device_Library` | `a2d2dfd145126085bcc042bea8819d19ae7f2cf3` | `adf917fca36a6e862e2b318ef0d44f33672161de` | `adf917fc...` (= 2.9.48) |
| `basesw/mcHF/Middlewares/ST/STM32_USB_Host_Library` | `ec514b40fbdc044b69dde69a884c45c9030ff5c3` | `ec514b40...` | `9902f86f873574390448391589e73a16e8b9ec73` (−17 файлов) |

База: UHSDR v2.9.48 `ae93edf6140af2cc1149485bf3d763cfdbc060bc`; целевой upstream — UHSDR v2.12.2
`dcbebe5f` (последний релиз, 2023). Эталон сравнения хранится вне репозитория:
`E:\harnet\sdr\_analysis\upstream-research\uhsdr.git` (bare).

## Политика обновления

1. **Источник обновления — только UHSDR upstream.** В B5 проверено: между 2.9.48 и 2.12.2 UHSDR
   **не менял** CMSIS Core/Device/DSP, HAL F4, USB Device/Host, FatFs (кроме удаления неиспользуемых
   USB-host class-файлов). FreeDV/Codec2 уже поднят до 2.12.2 (P2) — в B5 обновлять нечего.
2. **HAL/CMSIS-Device/startup/USB-стек/FatFs — не обновлять без явной необходимости.** Причина:
   высокая цена регрессии на железе; FatFs и USB-host/USB-device/HAL используются **и прошивкой,
   и загрузчиком** (бюджет загрузчика 31 620/32 768 Б, запас 1 148 Б); HAL/CMSIS-DSP содержат
   локальные патчи Nizar (см. таблицу выше) — слепое обновление их потеряет.
3. **Порядок обновления (если понадобится):**
   - сверить файлы с `dcbebe5f` (`git -C <bare> diff --stat <ref1> <ref2> -- <путь>`), оценить API;
   - сделать 3-way merge, сохранив локальные патчи Nizar (`*_Nizar`, `uwTick`, USB-правки);
   - `python tools/build.py all` (прошивка + загрузчик) и `python tools/build.py firmware --freedv`;
     проверить отсутствие новых предупреждений, размеры (fw/default, fw/-freedv, bl ≤ 32 768 Б);
   - `python tests/run_all.py` (эталон B5: 1754 checks passed, 0 failed, 3 known);
   - обновить этот файл и отчёт аудита.
4. **Более новые внешние релизы** (оценка B5, не для слепого апгрейда): CMSIS 5.9/6.x и
   CMSIS-DSP 1.18 — несовместимый API (radix2 CFFT удалён, новые типы), FatFs R0.15 — другая
   конфигурация/API, STM32F4 HAL 1.8.3, ST USB Device 2.11 / Host 3.4 — требуют переписывания
   HAL-адаптеров и полевых испытаний. Не обновлять в рамках текущей ветки.

## Как проверить пин

```bash
# 1) версия и текущее дерево
git rev-parse HEAD:mchf-eclipse/drivers/freedv
# 2) сравнение с upstream (bare-репозиторий аудита)
git -C E:/harnet/sdr/_analysis/upstream-research/uhsdr.git rev-parse dcbebe5f:mchf-eclipse/drivers/freedv
# 3) сборка/тесты
python tools/build.py all --build-dir build --force -j 8
python tools/build.py firmware --freedv --build-dir build-freedv --force -j 8
python tests/run_all.py
```

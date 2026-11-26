# Сборка прошивки UHSDR / RS-918 из командной строки

В этом репозитории лежит прошивка **UHSDR 2.9.48** (форк Nizar 3V8MN) для SDR-трансиверов
mcHF / RS-918. Ниже — как собрать `fw-mchf` (прошивку) и `bl-mchf` (загрузчик) **без Eclipse,
без EmBitz и без GNU make**, одной командой.

Сборка полностью повторяет то, что делает `mchf-eclipse/Makefile` для `BUILDFOR=F4`
(STM32F407, 1 МБ flash / 128 КБ RAM + 64 КБ CCM), но список исходников и флаги берутся из тех
же самых `*.mak`-файлов, поэтому набор файлов и опции не расходятся с оригиналом.

---

## 1. Что нужно установить

| Компонент | Требование | Пример в этой системе |
|---|---|---|
| Python | 3.8+ (стандартная библиотека, никаких пакетов ставить не нужно) | Python 3.12 |
| ARM toolchain | `arm-none-eabi-gcc` / `g++` / `objcopy` / `size` / `nm` — xPack GNU Arm Embedded GCC **15.2.1** (проверено), подойдёт и 10–14 | `E:\tools\gcc-arm\bin` |
| Git | только для работы с репозиторием, для сборки не нужен | — |
| make / Eclipse / EmBitz | **не нужны** | — |

Тулчейн **не обязан быть в `PATH`**: `tools/build.py` ищет его в таком порядке

1. `--toolchain <папка_с_arm-none-eabi-gcc>`
2. переменная окружения `ARM_GCC_BIN`
3. `E:\tools\gcc-arm\bin` (значение по умолчанию для этого проекта)
4. `PATH`

Проверить тулчейн:

```powershell
E:\tools\gcc-arm\bin\arm-none-eabi-gcc --version
# arm-none-eabi-gcc.exe (xPack GNU Arm Embedded GCC x86_64) 15.2.1 20251203
```

Если тулчейн лежит в другом месте:

```powershell
python tools\build.py all --toolchain "C:\Program Files\ArmGNUToolchain\bin"
# или
$env:ARM_GCC_BIN = "C:\Program Files\ArmGNUToolchain\bin"
```

---

## 2. Сборка (одна команда)

Все команды выполняются **из корня репозитория** (`RS918_Nizar96K_Pro`, там, где лежит папка
`mchf-eclipse`). Скрипт можно запускать и из любого другого каталога — пути он определяет
относительно своего расположения.

```powershell
cd E:\harnet\sdr\RS918_Nizar96K_Pro

python tools\build.py all          # прошивка + загрузчик  (то, что нужно обычно)
python tools\build.py firmware     # только fw-mchf
python tools\build.py bootloader   # только bl-mchf
python tools\build.py clean        # удалить каталог build\ целиком
```

Полезные ключи:

| Ключ | Смысл |
|---|---|
| `-j N`, `--jobs N` | число параллельных компиляций (по умолчанию — число ядер CPU) |
| `-f`, `--force` | полная пересборка (игнорировать уже собранные объектные файлы) |
| `-v`, `--verbose` | печатать каждую команду компиляции |
| `-t DIR`, `--toolchain DIR` | каталог с `arm-none-eabi-gcc` |
| `--build-dir DIR` | каталог артефактов (по умолчанию `<repo>\build`) |

Код возврата: **0** — успех, **2** — ошибка сборки (в конце печатается команда и вывод
компилятора/линкера), **130** — прерывание с клавиатуры.

Сборка «с нуля» на 16 ядрах занимает ~32 секунды (527 объектных файлов прошивки +
116 загрузчика), инкрементальная — единицы секунд.

---

## 3. Что получается

Всё складывается в `RS918_Nizar96K_Pro\build\` (каталог в `.gitignore`, исходники не засоряются):

| Файл | Что это |
|---|---|
| `fw-mchf.elf` | прошивка с отладочной информацией (`-g3`), для GDB/ST-Link |
| `fw-mchf.bin` | **прошивка для загрузчика**, её копируют на USB-диск (см. §5) |
| `fw-mchf.hex` | та же прошивка в Intel HEX (для STM32CubeProgrammer и т.п.) |
| `fw-mchf.map` | карта компоновки: адреса, размеры, расход flash/RAM |
| `bl-mchf.elf` / `.bin` / `.hex` / `.map` | то же для загрузчика (**UHSDR bootloader 4.1.2**, см. `src/uhsdr_version.h: UHSDR_BOOT_VERS`) |
| `warnings.log` | все предупреждения компиляции и линковки |
| `compile_commands.json` | база компиляции для clangd / VS Code / CLion |
| `build-summary.json` | размеры и пути артефактов в машинночитаемом виде |
| `obj\firmware\...`, `obj\bootloader\...` | объектные файлы и `.d`-зависимости |

Текущий размер (сборка GCC 15.2.1, `-O2`, см. `_analysis/build-report.md`):

```
прошивка   fw-mchf.bin  436 400 байт  = 44,4 % области 960 КиБ (0x08010000..0x080FFFFF), запас 546 640
           RAM статич.   40 268 байт  = 30,7 % 128 КиБ, на стек остаётся ~90 КиБ
           CCM            65 332 байт  из 65 536 (свободно 204 байта!)
загрузчик  bl-mchf.bin   30 488 байт  = 93,0 % области 32 КиБ (0x08000000..0x08007FFF), запас 2 280
```

Проверить после сборки:

```powershell
E:\tools\gcc-arm\bin\arm-none-eabi-size build\fw-mchf.elf
#  text    data     bss     dec     hex filename
# 434232    2168  103436  539836   83cbc build\fw-mchf.elf
```

(`bss` в выводе `size` включает CCM-секцию, поэтому больше, чем «RAM» в отчёте сборки.)

---

## 4. Как это устроено (и чем отличается от `make`)

`tools/build.py` — маленький драйвер сборки на Python:

1. парсит `mchf-eclipse/include.mak`, `files.mak`, `bootloader.mak`, `f4-include.mak`,
   `f4-files.mak`, `f4-bootloader.mak` (переменные `SUBDIRS`, `HAL_SUBDIRS`, `SRC`, `HAL_SRC`,
   `DSPLIB_SRC`, `BL_SRC`, `BL_HAL_SRC`);
2. компилирует каждый файл теми же флагами, что и Makefile:
   - общие: `-mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -mthumb -DARM_MATH_CM4
     -DCORTEX_M4 -DSTM32F407xx -D__FPU_PRESENT=1U -DUSE_HAL_DRIVER -DDEBUG -DUSE_FULL_ASSERT
     -DTRACE -D_GNU_SOURCE -DTRX_ID="mchf" -DTRX_NAME="mcHF QRP" -ffunction-sections
     -fdata-sections -flto -Wall -Wno-unused-function -g3`;
   - прошивка: `-O2`;  загрузчик: `-Os -DBOOTLOADER_BUILD`;
   - все `-I` из `SUBDIRS` + `HAL_SUBDIRS`, `-std=gnu11` (C) / `-std=gnu++11` (C++);
3. линкует `g++` с `-flto --specs=nano.specs -g3 -Xlinker --gc-sections` и скриптом
   `linker/arm-gcc-link_f4_flash1024k.ld` (прошивка) или
   `linker/arm-gcc-link-bootloader_f4.ld` (загрузчик), библиотеки `-lm -lc -lnosys`;
4. делает `.bin`/`.hex` через `objcopy` и печатает расход flash/RAM.

Отличия от `make all` (осознанные, ничего не меняют в коде прошивки):

* целевые объекты строятся в `build/`, а не рядом с исходниками;
* `g++` при линковке получает список объектных файлов через response-файл `@build\firmware.link.rsp`
  (в Windows командная строка ограничена 32 767 символами, 527 путей туда не влезают) —
  результат линковки идентичен;
* добавлен `-fcommon` (совместимость с GCC ≥ 10, иначе LTO-линковка падает с
  `multiple definition of bm_set_normal / USBD_COMP_CfgDesc`);
* добавлены `-MMD -MP` — только для корректной инкрементальной сборки (файлы `.d`);
* `-DTRX_NAME="mcHF QRP"` — без одинарных кавычек. Makefile задаёт `TRX_NAME='mcHF QRP'`,
  и после подстановки в строку получается `'mcHF QRP'` **вместе с кавычками** (они видны в
  строке версии); здесь строка чистая;
* не генерируются `.dfu` (скрипт `support/hex2dfu/hex2dfu.py` требует Python 2 и модуль
  `intelhex`) и handbook (`support/ui/menu/mk-menu-handbook`, bash) — для прошивки они не нужны.

---

## 5. Как прошивать

### 5.1 Прошивка (firmware) — через загрузчик по USB, без программатора

Загрузчик UHSDR представляется компьютеру как USB-флешка и умеет сам обновлять прошивку
(см. `mchf-eclipse/src/bootloader/bootloader_main.c`, `BL_MSC_Application`).

1. Выключить трансивер.
2. Нажать и **удерживать** кнопку **BAND−**, включить питание. На экране появится
   информация загрузчика (`Firmware Name: fw-mchf`). Кнопку BAND− держать.
3. Подключить USB-кабель к трансиверу — на компьютере появится съёмный диск.
4. Пока BAND− удерживается, загрузчик:
   * сохранит текущую прошивку в файл `fw-mchf.old` на этом диске;
   * если на диске лежит **`fw-mchf.bin`**, запишет его в flash (на экране
     `Updating firmware using "fw-mchf.bin"...`).
   Поэтому: скопируйте `build\fw-mchf.bin` в корень появившегося диска **до** того, как
   загрузчик начнёт обновление, либо положите файл заранее на SD-карту/диск.
5. Дождаться `Finished.`, отсоединить диск (или нажать BAND− для перезагрузки) — трансивер
   перезапустится с новой прошивкой.

Если BAND− **не** удерживать, загрузчик просто сохранит дамп flash и загрузит текущую
прошивку — это безопасный режим «ничего не менять».

### 5.2 Загрузчик (bootloader) — только программатором

Собранный загрузчик — **UHSDR bootloader 4.1.2** (`UHSDR_BOOT_VERS` в `src/uhsdr_version.h`);
на экране при входе в режим обновления он печатает `Version: 4.1.2`. Эта версия соответствует
прошивке 2.9.48 — менять загрузчик нужно только если он старее.

Загрузчик занимает первые 32 КиБ flash (`0x08000000`), поэтому обновляется либо по SWD, либо
через встроенный DFU STM32.

**Вариант A — ST-Link / SWD (рекомендуется):**

```powershell
# любым из инструментов, например STM32CubeProgrammer (GUI/CLI) или openocd:
STM32_Programmer_CLI.exe -c port=SWD -w build\bl-mchf.bin 0x08000000 -v -rst
```

**Вариант B — DFU (встроенный загрузчик STM32):**

1. Нажать и удерживать **BAND+**, включить питание → на экране `DFU`.
   Загрузчик установит признак и перезагрузится в системный DFU STM32
   (`mchfBl_CheckAndGoForDfuBoot`, адрес `0x1FFF0000`).
2. На компьютере появится устройство `STM32 BOOTLOADER` (DFU). Залить `build\bl-mchf.bin`
   по адресу `0x08000000` (STM32CubeProgrammer: `-c port=USB1 -w build\bl-mchf.bin 0x08000000 -v -rst`).
   Для DfuSe нужен `.dfu` — этот скрипт его не делает (см. §4), используйте CubeProgrammer или `.hex`.

**Важно:** не отключайте питание во время записи загрузчика. Если загрузчик «окирпичен»,
трансивер всё равно поднимается через DFU (BAND+ при включении), т.к. DFU — это ROM STM32.

---

## 6. Диагностика

| Симптом | Что делать |
|---|---|
| `arm-none-eabi-gcc not found` | указать `--toolchain` или `ARM_GCC_BIN` (§1) |
| `compilation failed for ...` | в выводе будет полная команда и текст ошибки компилятора |
| `link failed for fw-mchf.elf` | то же; частые причины — забытый исходник в `*.mak` или дубликат глобального символа в заголовке |
| предупреждения | все они пишутся в `build\warnings.log`; разбор — в `_analysis/build-report.md` |
| непонятно, что собралось | `build\build-summary.json` (размеры), `build\fw-mchf.map` (карта), `build\compile_commands.json` |
| хочется повторить «как в Eclipse» | `python tools\build.py all --force -v` |

Требования к ресурсам: на полную пересборку нужно ~1 ГБ на диске (объектные файлы + ELF с
отладочной информацией) и ~1,5 ГБ RAM при `-j16`.

---

## 7. Ограничения

* `.dfu` не собирается (нужен Python 2 + `intelhex`); `.hex` собирается — его достаточно для
  STM32CubeProgrammer.
* Handbook/меню-граф (`make ...handbook`) не генерируются.
* Прошивка собирается **только под F4** (RS-918/mcHF): скрипт читает `f4-*.mak`.
* Поддержка F7 (OVI40) и H7 (OVI40H7) **удалена из этого дерева** на этапе чистки (партия c):
  vendor-каталоги плат F7/H7 в `basesw/` (`ovi40`, `ovi40-h7`), их `*-files.mak`/`*-include.mak`/
  `*-bootloader.mak` и скрипты линковки F7/H7 перенесены в `_archive/deleted/`
  (см. `_analysis/cleanup-log.md`). В `Makefile` ветки `BUILDFOR=F7/H7` остались только как явные
  `$(error …)`; `.cproject` содержит три F4-конфигурации.
* CCM-память занята на 99,7 % (204 байта свободно) — при добавлении буферов в CCM сборка
  упадёт на линковке; свободная RAM (128 КиБ) при этом ещё есть.
* Байт-в-байт воспроизводимость между полными пересборками не гарантируется: в прошивку
  зашивается `fwt-<__DATE__ __TIME__>` (`UHSDR_BUILD_DAT`). Повторная сборка без изменения
  исходников (только перелинковка) даёт идентичный `.bin`.

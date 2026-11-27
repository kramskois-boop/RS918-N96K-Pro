# Host-side regression tests (N96K P2)

No compiler, no firmware, no third-party Python packages: these tests parse the
C sources of the RS918 N96K-Pro firmware tree and check the invariants that are
invisible to the compiler.

Current state (v1.2 tree + B1 test tooling): **1754 checks passed, 0 failed, 3 known, 0 skipped**.
The three KNOWN entries are documented audit findings (see `known_issues.py`);
they do not fail the run.

```bash
python3 tests/run_all.py          # exit 0 = pass, 1 = failures
python3 tests/run_all.py --verbose
```

Layout:

| File | Purpose |
|---|---|
| `run_all.py` | entry point, runs every check module and aggregates the result |
| `checkbase.py` | PASS/FAIL/KNOWN/SKIP bookkeeping |
| `cparse.py` | small dependency-free C helpers (comments, `#if 0`, `#define`, initializers, arrays) |
| `check_filters.py` | FIR/IIR-lattice/biquad descriptor and coefficient tables |
| `check_iq.py` | Nizar IQ coefficient tables, descriptors and `FilterPathInfo` references |
| `check_files_mak.py` | every filter driver is in `files.mak` (P0-2 regression) |
| `check_versions.py` | UHSDR 2.9.48 / product / bootloader identity; the bootloader must be exactly **5.0.4** (a 4.1.2 rollback is a hard failure, audit F5) |
| `check_eeprom.py` | EEPROM address map consistency **and** the `ConfigEntryInfo[]` descriptor binding (F11, see below) |
| `eeprom_descriptor_map.txt` | frozen EEPROM descriptor binding snapshot used by `check_eeprom.py` (data file, not a check module) |
| `check_vswr.py` | VSWR/PA foldback safety invariants (audit P0-4/P1-1/A1): state machine, threshold EEPROM+menu, A1 power clamp, calculation guard/caps, minimal-drive recovery, `SWR!` indication, out-of-band TX interlock |
| `check_audio.py` | static biquad instances in `audio_driver.c` |
| `known_issues.py` | documented defects accepted as KNOWN (XFAIL); delete an entry once fixed |

## EEPROM descriptor binding snapshot (F11)

`check_eeprom.py` parses `ConfigEntryInfo[]` from
`mchf-eclipse/drivers/ui/ui_configuration.c` (including the
`UI_C_EEPROM_BAND_*_PF` token-pasting rows) and verifies that every descriptor
still binds the same `EEPROM_*` address to the same `ts.*` / `swrm.*`
variable and `ConfigEntry` type.  That binding — not just the `#define` map —
is the persistence contract for the settings a user already has in flash, so it
is frozen in `tests/eeprom_descriptor_map.txt`.  The check also verifies the
table structure (one trailing `ConfigEntry_Stop`, six positional descriptor
fields, no two descriptors on one address, `ConfigEntry_Int32` occupying two
words), that the `UiConfiguration_ReadConfigEntries/WriteConfigEntries` loops
still use the table, and that `UiConfiguration_GetEntry()` keeps its sparse-map
lookup (linear search by the `.id` field).

A swap, deletion or re-addressing of a descriptor row (e.g. moving the VSWR
threshold from 426 to `EEPROM_EXPFLAGS1` = 427) fails the test.  Adding a new
setting at the end of the map is a legitimate change but also changes the
contract: review the diff and regenerate the snapshot explicitly:

```bash
python3 tests/check_eeprom.py --update-golden   # writes tests/eeprom_descriptor_map.txt
```

Never regenerate the snapshot just to make a red CI green.

Adding a check: create `check_<name>.py` with `run(repo) -> Results`, add it to
`CHECK_MODULES` in `run_all.py`.  Failures must be actionable: print
`file:line` and the expected/actual values.  Do not fail on things that cannot
be resolved statically - use `res.skip()` instead.

# Host-side regression tests (N96K P2)

No compiler, no firmware, no third-party Python packages: these tests parse the
C sources of the RS918 N96K-Pro firmware tree and check the invariants that are
invisible to the compiler.

`BT`bash
python3 tests/run_all.py          # exit 0 = pass, 1 = failures
python3 tests/run_all.py --verbose
`BT`

Layout:

| File | Purpose |
|---|---|
| `run_all.py` | entry point, runs every check module and aggregates the result |
| `checkbase.py` | PASS/FAIL/KNOWN/SKIP bookkeeping |
| `cparse.py` | small dependency-free C helpers (comments, `#if 0`, `#define`, initializers, arrays) |
| `check_filters.py` | FIR/IIR-lattice/biquad descriptor and coefficient tables |
| `check_iq.py` | Nizar IQ coefficient tables, descriptors and `FilterPathInfo` references |
| `check_files_mak.py` | every filter driver is in `files.mak` (P0-2 regression) |
| `check_versions.py` | UHSDR 2.9.48 / product / bootloader identity |
| `check_eeprom.py` | EEPROM address map consistency |
| `check_audio.py` | static biquad instances in `audio_driver.c` |
| `known_issues.py` | documented defects accepted as KNOWN (XFAIL); delete an entry once fixed |

Adding a check: create `check_<name>.py` with `run(repo) -> Results`, add it to
`CHECK_MODULES` in `run_all.py`.  Failures must be actionable: print
`file:line` and the expected/actual values.  Do not fail on things that cannot
be resolved statically - use `res.skip()` instead.

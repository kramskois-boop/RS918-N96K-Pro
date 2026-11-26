#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""run_all.py - host-side regression tests for the RS918 N96K-Pro firmware.

These tests run on any machine with Python 3.8+ and no third-party packages.
They do not build or execute the firmware: they parse the C sources and check
the invariants that a compiler cannot check (table sizes versus descriptor
fields, product/version identity, EEPROM map, build source lists).

Usage (from the repository root, or from anywhere):

    python3 tests/run_all.py            # all checks, exit code 0/1
    python3 tests/run_all.py --verbose  # also print the check names as they run

Exit code:
    0  every check passed (KNOWN issues and SKIPs do not fail the run)
    1  at least one FAIL
    2  the test harness itself could not run (missing tree, bad import)

Check modules:
    check_filters   CIS filter tables in drivers/audio/filters/*.c  (3a, 3b)
    check_iq        Nizar IQ tables and their descriptors            (3c)
    check_files_mak every filter driver is in files.mak             (3d)
    check_versions  UHSDR/product/bootloader identity               (3e)
    check_eeprom    EEPROM address map consistency                  (3f)
    check_audio     static biquad instances in audio_driver.c       (bonus)
"""

from __future__ import annotations

import os
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

from checkbase import Results  # noqa: E402

CHECK_MODULES = (
    "check_filters",
    "check_iq",
    "check_files_mak",
    "check_versions",
    "check_eeprom",
    "check_audio",
)


def main(argv):
    verbose = "--verbose" in argv or "-v" in argv
    repo = os.path.dirname(HERE)
    if not os.path.isdir(os.path.join(repo, "mchf-eclipse")):
        sys.stderr.write("run_all.py: %s does not look like the firmware "
                         "repository (mchf-eclipse/ is missing)\n" % repo)
        return 2
    out = sys.stdout
    out.write("N96K host tests\n")
    out.write("repository: %s\n" % repo)
    out.write("python    : %s\n\n" % sys.version.split()[0])

    total = Results("total")
    failed = False
    for module_name in CHECK_MODULES:
        if verbose:
            out.write("running %s ...\n" % module_name)
        try:
            module = __import__(module_name)
            results = module.run(repo)
        except Exception:               # noqa: BLE001 - report, never crash CI
            out.write("[FAIL] %-16s harness exception in %s:\n"
                      % (module_name, module_name))
            traceback.print_exc(file=out)
            failed = True
            total.failed.append((module_name, "harness exception"))
            continue
        if not isinstance(results, Results):
            out.write("[FAIL] %-16s %s.run() returned %r\n"
                      % (module_name, module_name, type(results)))
            failed = True
            total.failed.append((module_name, "bad run() result"))
            continue
        for code, message in results.failed:
            total.failed.append(("%s/%s" % (module_name, code), message))
        for code, message, reason in results.known:
            total.known.append(("%s/%s" % (module_name, code), message, reason))
        for code, message in results.skipped:
            total.skipped.append(("%s/%s" % (module_name, code), message))
        total.passed += results.passed
        if not results.report(out):
            failed = True
        out.write("\n")

    if total.known:
        out.write("KNOWN issues (accepted, see tests/known_issues.py):\n")
        for code, message, reason in total.known:
            out.write("  - %s: %s\n" % (code, message))
    out.write("=" * 78 + "\n")
    out.write("%d checks passed, %d failed, %d known, %d skipped\n"
              % (total.passed, len(total.failed), len(total.known),
                 len(total.skipped)))
    if failed:
        out.write("RESULT: FAIL\n")
        return 1
    out.write("RESULT: PASS\n")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        sys.exit(130)

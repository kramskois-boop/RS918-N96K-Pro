#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""verify_artifacts.py - CI gate for the RS918 N96K-Pro firmware artifacts.

Reads <build-dir>/build-summary.json written by tools/build.py, checks the
firmware/bootloader against the flash/RAM/CCM budgets and writes SHA256SUMS.txt.
The reference size is the verified v1.0 firmware (fw-mchf.bin = 320_024 bytes);
small deviations caused by the P0/P2 patches are accepted, only a real budget
violation fails the build.

Usage:
    python3 docker/verify_artifacts.py build [--expected-fw-size N]
                                             [--fw-tolerance N]

Exit code: 0 = artifacts are good, 1 = verification failed, 2 = usage error.
No third-party packages are required (runs inside the Docker image and on the
GitHub Actions runner).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys

FIRMWARE = "fw-mchf"
BOOTLOADER = "bl-mchf"
SUFFIXES = (".elf", ".bin", ".hex", ".map")

# flash/RAM/CCM budgets of the STM32F407 target (see tools/build.py)
FW_FLASH_BUDGET = 983040        # 0x08010000..0x080FFFFF = 960 KiB
BL_FLASH_BUDGET = 32 * 1024     # 0x08000000..0x08007FFF
RAM_BUDGET = 128 * 1024
CCM_BUDGET = 64 * 1024

REFERENCE_FW_BIN = 320024       # verified v1.0 build (GCC 15.2.1, -O2)
DEFAULT_TOLERANCE = 8192        # patch-level size drift accepted with a warning


def sha256sum(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def check_file(build_dir, name, errors):
    path = os.path.join(build_dir, name)
    if not os.path.isfile(path):
        errors.append("missing artifact: %s" % name)
        return None
    if os.path.getsize(path) == 0:
        errors.append("empty artifact: %s" % name)
        return None
    return path


def main(argv):
    parser = argparse.ArgumentParser(description="verify N96K firmware artifacts")
    parser.add_argument("build_dir", help="directory with the build artifacts")
    parser.add_argument("--expected-fw-size", type=int, default=REFERENCE_FW_BIN,
                        help="reference fw-mchf.bin size (default: %(default)s)")
    parser.add_argument("--fw-tolerance", type=int, default=DEFAULT_TOLERANCE,
                        help="accepted fw size drift before a warning")
    args = parser.parse_args(argv)

    build_dir = os.path.abspath(args.build_dir)
    errors = []
    warnings = []
    print("[verify] artifacts: %s" % build_dir)

    # -- every expected artifact exists ------------------------------------
    files = {}
    for prefix in (FIRMWARE, BOOTLOADER):
        for suffix in SUFFIXES:
            name = prefix + suffix
            path = check_file(build_dir, name, errors)
            if path:
                files[name] = path
    summary_path = check_file(build_dir, "build-summary.json", errors)
    if summary_path:
        files["build-summary.json"] = summary_path

    # -- sizes from build-summary.json -------------------------------------
    summary = {}
    if summary_path:
        try:
            with open(summary_path, "r", encoding="utf-8") as handle:
                summary = json.load(handle)
        except (OSError, ValueError) as exc:
            errors.append("cannot read build-summary.json: %s" % exc)

    firmware = summary.get("firmware") or {}
    bootloader = summary.get("bootloader") or {}
    if not firmware:
        errors.append("build-summary.json has no firmware entry")
    if not bootloader:
        errors.append("build-summary.json has no bootloader entry")

    if firmware:
        flash_used = firmware.get("flash_used", -1)
        if not 0 < flash_used <= FW_FLASH_BUDGET:
            errors.append("firmware flash_used=%r outside 1..%d"
                          % (flash_used, FW_FLASH_BUDGET))
        ram_used = firmware.get("ram_used", -1)
        if not 0 < ram_used <= RAM_BUDGET:
            errors.append("firmware ram_used=%r outside 1..%d"
                          % (ram_used, RAM_BUDGET))
        ccm_used = firmware.get("ccm_used", -1)
        if not 0 <= ccm_used <= CCM_BUDGET:
            errors.append("firmware ccm_used=%r outside 0..%d"
                          % (ccm_used, CCM_BUDGET))
        ccm_free = CCM_BUDGET - ccm_used if ccm_used >= 0 else 0
        if 0 <= ccm_used and ccm_free < 1024:
            warnings.append("CCM is nearly full: %d bytes free" % ccm_free)

    if bootloader:
        flash_used = bootloader.get("flash_used", -1)
        if not 0 < flash_used <= BL_FLASH_BUDGET:
            errors.append("bootloader flash_used=%r outside 1..%d"
                          % (flash_used, BL_FLASH_BUDGET))

    fw_bin = os.path.join(build_dir, FIRMWARE + ".bin")
    if os.path.isfile(fw_bin):
        fw_size = os.path.getsize(fw_bin)
        delta = fw_size - args.expected_fw_size
        print("[verify] fw-mchf.bin  : %d bytes (%+d vs reference %d)"
              % (fw_size, delta, args.expected_fw_size))
        if abs(delta) > args.fw_tolerance:
            warnings.append(
                "fw-mchf.bin size %d deviates from the reference %d by %+d "
                "bytes (tolerance +/-%d)" % (fw_size, args.expected_fw_size,
                                             delta, args.fw_tolerance))
    bl_bin = os.path.join(build_dir, BOOTLOADER + ".bin")
    if os.path.isfile(bl_bin):
        print("[verify] bl-mchf.bin  : %d bytes (budget %d)"
              % (os.path.getsize(bl_bin), BL_FLASH_BUDGET))
        # NOTE: the 5.0.4 bootloader is ported by a parallel workstream, so only
        # presence and the flash budget are enforced here.

    if firmware:
        print("[verify] firmware     : flash %d/%d, RAM %d/%d, CCM %d/%d"
              % (firmware.get("flash_used", -1), FW_FLASH_BUDGET,
                 firmware.get("ram_used", -1), RAM_BUDGET,
                 firmware.get("ccm_used", -1), CCM_BUDGET))
    if bootloader:
        print("[verify] bootloader   : flash %d/%d"
              % (bootloader.get("flash_used", -1), BL_FLASH_BUDGET))

    # -- SHA256SUMS.txt ----------------------------------------------------
    checksum_lines = []
    for name in sorted(files):
        checksum_lines.append("%s  %s" % (sha256sum(files[name]), name))
    sums_path = os.path.join(build_dir, "SHA256SUMS.txt")
    try:
        with open(sums_path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("\n".join(checksum_lines) + "\n")
        print("[verify] wrote %s (%d entries)" % (sums_path, len(checksum_lines)))
    except OSError as exc:
        errors.append("cannot write SHA256SUMS.txt: %s" % exc)

    for warning in warnings:
        print("[verify] WARNING: " + warning)
    if errors:
        for error in errors:
            print("[verify] ERROR: " + error, file=sys.stderr)
        print("[verify] FAILED")
        return 1
    print("[verify] OK")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        sys.exit(130)

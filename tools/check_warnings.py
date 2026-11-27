#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_warnings.py - compiler warning gate for the RS918 N96K-Pro firmware.

tools/build.py writes every compiler/linker warning to <build-dir>/warnings.log.
This script normalizes those lines and compares them with the checked-in
baseline tests/warnings-baseline.txt:

    * a warning text that is not in the baseline ("new warning type")   -> FAIL
    * more occurrences of a known warning than in the baseline          -> FAIL
    * fewer occurrences / warnings that disappeared                     -> OK,
      the baseline should be lowered with --update-baseline.

Normalization drops everything that legitimately differs between the Windows
development host and the pinned Linux Docker image (absolute paths, line and
column numbers, the ".exe" suffix of the LTO wrapper) and incidental numbers
in linker diagnostics, so one baseline works for both.

Usage:
    python3 tools/check_warnings.py build                    # build/warnings.log
    python3 tools/check_warnings.py artifacts/warnings.log
    python3 tools/check_warnings.py build --update-baseline  # accept current log
    python3 tools/check_warnings.py --help

Exit code:
    0  no new warnings (known warnings may have disappeared; see the report)
    1  at least one new/increased warning - the build must not pass
    2  the check itself could not run (missing log/baseline, unreadable file)
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
DEFAULT_BASELINE = os.path.join(REPO, "tests", "warnings-baseline.txt")
DEFAULT_BUILD_DIR = "build"

# "<path>:<line>:<col>: warning: <message>" - the GCC diagnostics build.py logs
LOCATION_RE = re.compile(
    r"^(?P<path>.+?):(?P<line>\d+):(?P<col>\d+): "
    r"(?P<sev>warning|error|note): (?P<msg>.*)$")
# absolute paths of both flavours (the toolchain paths differ between the
# Windows xPack install and /opt/xpack in the Docker image)
ABS_WIN_RE = re.compile(r"[A-Za-z]:[\\/][^:\n]*")
ABS_POSIX_RE = re.compile(r"(?<![\w.])/(?:[\w.+-]+/)+[\w.+-]+")
DIGITS_RE = re.compile(r"\d+")
SEVERITIES = ("warning:", "error:", "note:")


def _strip_abs_paths(text):
    """Replace absolute Windows/POSIX paths, keeping the file name visible."""
    text = ABS_WIN_RE.sub("<path>", text)
    text = ABS_POSIX_RE.sub("<path>", text)
    return text


def _normalize_message(text):
    """Normalize one diagnostic message (no "<source>: " prefix)."""
    text = _strip_abs_paths(text)
    # lto-wrapper.exe on Windows, lto-wrapper in the Linux image
    text = text.replace(".exe:", ":")
    return text.strip()


def _link_key(text):
    """Normalize a linker message logged by build.py as "link: <text>"."""
    start = -1
    for marker in SEVERITIES:
        pos = text.find(marker)
        if pos >= 0 and (start < 0 or pos < start):
            start = pos
    if start > 0:
        text = text[start:]        # drop ld's file/in-function prefix
    text = _normalize_message(text)
    # LTRANS job counts and other incidental numbers depend on the host
    text = DIGITS_RE.sub("<N>", text)
    return "link: " + text


def normalize_line(raw):
    """Turn one warnings.log line into a comparable <count>-keyed string."""
    raw = raw.rstrip("\r\n")
    src, sep, text = raw.partition(": ")
    if not sep or not src.strip():
        return _link_key(raw)
    src = src.strip()
    if src in ("link", "build"):
        return _link_key(text)
    match = LOCATION_RE.match(text)
    if match:
        # the source file is known from build.py, drop path/line/column so
        # that edits moving code around do not touch the baseline
        return "%s: %s: %s" % (src, match.group("sev"), match.group("msg").strip())
    # unexpected shape: keep it but make it path/line independent
    return "%s: %s" % (src, _normalize_message(text))


def read_warnings_log(path):
    """Return (Counter of normalized warnings, number of raw log lines)."""
    counts = Counter()
    raw_lines = 0
    with open(path, "r", encoding="utf-8-sig") as handle:
        for raw in handle:
            if not raw.strip():
                continue
            raw_lines += 1
            counts[normalize_line(raw)] += 1
    return counts, raw_lines


def load_baseline(path):
    """Return Counter of the baseline, or None when the file does not exist."""
    if not os.path.exists(path):
        return None
    counts = Counter()
    with open(path, "r", encoding="utf-8-sig") as handle:
        for lineno, raw in enumerate(handle, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            count_text, sep, key = line.partition(" ")
            if not sep or not count_text.isdigit() or not key:
                raise ValueError("%s:%d: expected '<count> <warning>': %r"
                                 % (path, lineno, line))
            counts[key] = int(count_text)
    return counts


def write_baseline(path, counts, log_path):
    lines = [
        "# RS918 N96K-Pro firmware warning baseline.",
        "#",
        "# Used by tools/check_warnings.py to stop NEW compiler/linker warnings",
        "# from being added unnoticed.  Warnings that disappear are fine (the",
        "# baseline should then be lowered).",
        "#",
        "# Regenerate after intentional warning fixes:",
        "#   python3 tools/build.py all --force",
        "#   python3 tools/check_warnings.py build --update-baseline",
        "#",
        "# Format: <occurrences> <normalized warning text>",
        "# (source warnings: '<file>: warning: <text> [-Wflag]'; linker messages",
        "#  start with 'link: ' and have paths/addresses/numbers normalized.)",
        "#",
        "# Generated from: %s" % os.path.basename(log_path),
        "",
    ]
    for key in sorted(counts):
        lines.append("%d %s" % (counts[key], key))
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")


def _print_keys(keys, current, baseline, prefix):
    for key in keys:
        if key in baseline and key in current:
            print("  %s %s (baseline %d, now %d)"
                  % (prefix, key, baseline[key], current[key]))
        elif key in baseline:
            print("  %s %s (baseline %d, now 0)" % (prefix, key, baseline[key]))
        else:
            print("  %s %s (new, now %d)" % (prefix, key, current[key]))


def compare(current, baseline):
    new = sorted(key for key in current if key not in baseline)
    increased = sorted(key for key in current
                       if key in baseline and current[key] > baseline[key])
    decreased = sorted(key for key in current
                       if key in baseline and current[key] < baseline[key])
    gone = sorted(key for key in baseline if key not in current)
    return new, increased, decreased, gone


def _default_baseline(args):
    if args.baseline:
        return args.baseline
    return DEFAULT_BASELINE


def parse_args(argv):
    parser = argparse.ArgumentParser(
        prog="check_warnings.py",
        description="Compare build/warnings.log with tests/warnings-baseline.txt "
                    "and fail on new compiler warnings.")
    parser.add_argument("log", nargs="?", default=DEFAULT_BUILD_DIR,
                        help="warnings.log file or build directory "
                             "(default: %s)" % DEFAULT_BUILD_DIR)
    parser.add_argument("--baseline", default=None,
                        help="baseline file (default: %s)" % DEFAULT_BASELINE)
    parser.add_argument("--update-baseline", action="store_true",
                        help="rewrite the baseline from the given warnings.log")
    return parser.parse_args(argv)


def _resolve_log(path):
    if os.path.isdir(path):
        return os.path.join(path, "warnings.log")
    return path


def main(argv):
    args = parse_args(argv)
    log_path = _resolve_log(args.log)
    baseline_path = _default_baseline(args)

    if not os.path.isfile(log_path):
        sys.stderr.write("check_warnings.py: no warnings log at %s\n"
                         "  build the firmware first: python3 tools/build.py all --force\n"
                         % log_path)
        return 2
    try:
        current, raw_lines = read_warnings_log(log_path)
    except (OSError, ValueError) as exc:
        sys.stderr.write("check_warnings.py: cannot read %s: %s\n" % (log_path, exc))
        return 2

    print("[warnings] log      : %s (%d line(s), %d warning(s))"
          % (log_path, raw_lines, sum(current.values())))

    if args.update_baseline:
        try:
            write_baseline(baseline_path, current, log_path)
        except OSError as exc:
            sys.stderr.write("check_warnings.py: cannot write %s: %s\n"
                             % (baseline_path, exc))
            return 2
        print("[warnings] baseline : %s (%d distinct warning(s), updated)"
              % (baseline_path, len(current)))
        print("[warnings] RESULT: BASELINE UPDATED")
        return 0

    try:
        baseline = load_baseline(baseline_path)
    except (OSError, ValueError) as exc:
        sys.stderr.write("check_warnings.py: cannot read baseline: %s\n" % exc)
        return 2
    if baseline is None:
        sys.stderr.write("check_warnings.py: no baseline at %s\n"
                         "  create it from a clean build: "
                         "python3 tools/check_warnings.py build --update-baseline\n"
                         % baseline_path)
        return 2

    print("[warnings] baseline : %s (%d distinct warning(s))"
          % (baseline_path, len(baseline)))
    current_total = sum(current.values())
    baseline_total = sum(baseline.values())

    new, increased, decreased, gone = compare(current, baseline)
    failed = False
    if new:
        failed = True
        print("[warnings] NEW warning types (not in baseline):")
        _print_keys(new, current, baseline, "[new]")
    if increased:
        failed = True
        print("[warnings] MORE occurrences than in the baseline:")
        _print_keys(increased, current, baseline, "[more]")
    if decreased:
        print("[warnings] fewer occurrences than in the baseline (OK):")
        _print_keys(decreased, current, baseline, "[less]")
    if gone:
        print("[warnings] warnings that disappeared (OK):")
        _print_keys(gone, current, baseline, "[gone]")

    print("[warnings] total: %d now, %d in baseline (limit %d)"
          % (current_total, baseline_total, baseline_total))
    if failed:
        print("[warnings] RESULT: FAIL - new warnings, see the list above")
        print("[warnings] if they are intentional, update the baseline:")
        print("[warnings]   python3 tools/check_warnings.py build --update-baseline")
        return 1
    if current_total > baseline_total:       # redundant with the checks above
        print("[warnings] RESULT: FAIL - warning count grew")
        return 1
    if decreased or gone:
        print("[warnings] RESULT: OK (known warnings disappeared; you may lower "
              "the baseline with --update-baseline)")
    else:
        print("[warnings] RESULT: OK (no new warnings)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        sys.exit(130)

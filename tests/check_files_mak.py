#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_files_mak.py - every filter driver must be compiled (P0-2 regression).

Audit P2 item 3(d): the P0-2 defect was that iir_2_8k.c and iir_16k5_96ks.c
existed, were referenced by the DSP code, but were missing from files.mak, so
the linker failed with "undefined reference".  A host test cannot link, but it
can prove that the source list of the build covers every file in
drivers/audio/filters/ (and that no stale entry points at a missing file).

Checks:
  * files.mak "SRC" list contains every *.c from drivers/audio/filters/;
  * the two files of the P0-2 regression are explicitly listed;
  * every driver path listed in files.mak exists on disk;
  * no duplicate entries in the SRC list.
"""

from __future__ import annotations

import glob
import os
import re

import cparse
from checkbase import Results

CHECK = "files.mak"
FILTERS_DIR = "drivers/audio/filters"
P0_2_FILES = ("iir_2_8k.c", "iir_16k5_96ks.c")


def _logical_lines(text):
    """Join make continuation lines (trailing backslash)."""
    pending = ""
    for raw in text.splitlines():
        line = (pending + " " + raw.strip()) if pending else raw.strip()
        if line.endswith("\\"):
            pending = line[:-1]
            continue
        pending = ""
        yield line
    if pending:
        yield pending


def _make_var(text, name):
    """Return the value of a simple "NAME := ..." / "NAME = ..." assignment."""
    pattern = re.compile(r"^\s*%s\s*(?::=|\?=|\+=|=)\s*(.*)$" % re.escape(name))
    pending = ""
    collecting = False
    value = ""
    for line in _logical_lines(text):
        if pending:
            if collecting:
                value += " " + line
            pending = ""
            continue
        match = pattern.match(line)
        if match:
            collecting = True
            value = match.group(1)
            continue
        if collecting:
            # value continues on following lines only when the previous logical
            # line was continued; logical lines already collapse that, so a new
            # assignment or an empty line ends the value
            break
    return value.split()


def run(repo):
    res = Results(CHECK)
    eclipse = os.path.join(repo, "mchf-eclipse")
    files_mak = os.path.join(eclipse, "files.mak")
    if not os.path.isfile(files_mak):
        res.fail("missing", "files.mak not found: %s" % files_mak)
        return res
    text = cparse.strip_comments(cparse.read_text(files_mak))
    src = _make_var(text, "SRC")
    if not src:
        res.fail("src", "cannot parse the SRC list from files.mak")
        return res
    res.ok()

    normalized = [entry.replace("\\", "/") for entry in src]
    seen = set()
    for entry in normalized:
        if entry in seen:
            res.fail("dup", "duplicate SRC entry: %s" % entry)
        seen.add(entry)
        if not os.path.isfile(os.path.join(eclipse, entry)):
            res.fail("stale", "SRC entry does not exist: %s" % entry)

    listed = {entry for entry in normalized
              if entry.startswith(FILTERS_DIR + "/")}
    on_disk = {FILTERS_DIR + "/" + os.path.basename(path)
               for path in glob.glob(os.path.join(eclipse,
                                                  *FILTERS_DIR.split("/"),
                                                  "*.c"))}
    missing = sorted(on_disk - listed)
    for entry in missing:
        res.fail("missing-filter", "%s exists but is not in files.mak SRC "
                                    "(undefined reference at link time)" % entry)
    if not missing:
        res.ok()

    for filename in P0_2_FILES:
        entry = FILTERS_DIR + "/" + filename
        if entry not in listed:
            res.fail("p0-2", "%s missing from files.mak (P0-2 regression!)"
                     % entry)
        else:
            res.ok()

    for entry in sorted(listed - on_disk):
        res.fail("stale-filter", "files.mak lists a missing filter driver: %s"
                 % entry)

    if len(listed) != len(on_disk):
        res.skip("count", "filters: %d listed, %d on disk"
                 % (len(listed), len(on_disk)))
    return res

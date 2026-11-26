#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_eeprom.py - EEPROM configuration map consistency (audit P2 item 3f).

The EEPROM layout is the compatibility contract between firmware releases and
between the firmware and a user's stored settings.  The audit (P0-1) found a
dead range check (&& instead of &) in ui_configuration.c; this host test checks
the static side of the map without a target and without touching the code:

  * MAX_VAR_ADDR (= EEPROM_FIRST_UNUSED - 1) must stay below NB_OF_VAR from
    misc/v_eprom/eeprom.h (the actual EEPROM word count);
  * every EEPROM_* configuration address in ui_configuration.h resolves to an
    integer in [0, MAX_VAR_ADDR] and no two ids share an address;
  * EEPROM_FIRST_UNUSED must be exactly MAX_VAR_ADDR + 1;
  * the "#if (MAX_VAR_ADDR > NB_OF_VAR) #error" compile-time guard is present;
  * every EEPROM_* identifier referenced by ui_configuration.c is defined (a
    typo here silently maps a setting to the wrong slot or does not compile at
    all once a name is removed from the header).

The parsing is deliberately "rough": the descriptor table is data, the ids are
#defines, so no preprocessor or C parser is needed.  ui_configuration.c is also
being edited by the P0-1 EEPROM patch; the test only reads it.
"""

from __future__ import annotations

import os
import re

import cparse
from checkbase import Results

CHECK = "eeprom"
UI_H = os.path.join("mchf-eclipse", "drivers", "ui", "ui_configuration.h")
UI_C = os.path.join("mchf-eclipse", "drivers", "ui", "ui_configuration.c")
EEPROM_H = os.path.join("mchf-eclipse", "misc", "v_eprom", "eeprom.h")

# names that are not configuration-address ids
SPECIAL = {"EEPROM_FIRST_UNUSED", "EEPROM_KEYER_MEMORY_ADDRESS"}
# ids that are intentionally reserved for future use
_REF_RE = re.compile(r"\b(EEPROM_\w+)\b")

# headers with helper constants used inside the EEPROM map expressions
HELPER_HEADERS = (
    os.path.join("mchf-eclipse", "hardware", "uhsdr_board.h"),
    os.path.join("mchf-eclipse", "drivers", "audio", "audio_filter.h"),
)


def _enum_defines(text):
    """Collect simple "enum { A, B, C }" member values as integer defines.

    Some EEPROM map expressions use enum constants (e.g. FILTER_MODE_MAX in
    EEPROM_FILTER_PATH_MAP_END), which are not #defines and therefore invisible
    to the preprocessor-style parser.  Members without an explicit value get the
    usual C auto-increment value.
    """
    values = {}
    for match in re.finditer(r"\benum\b[^{;]*\{", text):
        try:
            body, _end = cparse.brace_body(text, match.end() - 1)
        except ValueError:
            continue
        value = 0
        for chunk in cparse.split_top_level(body):
            chunk = chunk.strip()
            if not chunk:
                continue
            if "=" in chunk:
                name, expr = chunk.split("=", 1)
                name = name.strip()
                explicit = cparse.eval_int(expr, values)
                value = explicit
            else:
                name = chunk
            if not re.match(r"^[A-Za-z_]\w*$", name) or value is None:
                continue
            values.setdefault(name, str(value))
            value += 1
    return values


def _clean(path, apply_pp=True):
    text = cparse.strip_comments(cparse.read_text(path))
    return cparse.apply_preprocessor(text) if apply_pp else text


def run(repo):
    res = Results(CHECK)
    for path in (UI_H, UI_C, EEPROM_H):
        if not os.path.isfile(os.path.join(repo, path)):
            res.fail("missing", "required file not found: %s" % path)
    if res.failed:
        return res

    # --- header: ids / MAX_VAR_ADDR ---------------------------------------
    header_text = _clean(os.path.join(repo, UI_H))
    header_raw = cparse.strip_comments(
        cparse.read_text(os.path.join(repo, UI_H)))
    defines = cparse.parse_defines(header_text)
    ui_defines = dict(defines)      # only ids defined in ui_configuration.h
    for helper in HELPER_HEADERS:
        helper_path = os.path.join(repo, helper)
        if os.path.isfile(helper_path):
            helper_text = cparse.strip_comments(cparse.read_text(helper_path))
            defines.update(cparse.parse_defines(helper_text))
            defines.update(_enum_defines(helper_text))
    first_unused = cparse.eval_int(defines.get("EEPROM_FIRST_UNUSED", ""), defines)
    max_var = cparse.eval_int(defines.get("MAX_VAR_ADDR", ""), defines)
    if max_var is None:
        res.fail("MAX_VAR_ADDR", "MAX_VAR_ADDR is missing/unresolvable in %s"
                 % UI_H)
        return res
    if first_unused is None:
        res.fail("EEPROM_FIRST_UNUSED", "EEPROM_FIRST_UNUSED is unresolvable")
        return res
    if first_unused - 1 != max_var:
        res.fail("MAX_VAR_ADDR", "MAX_VAR_ADDR=%d != EEPROM_FIRST_UNUSED-1=%d"
                 % (max_var, first_unused - 1))
    else:
        res.ok()

    # NB_OF_VAR lives in the virtual EEPROM driver
    eeprom_text = _clean(os.path.join(repo, EEPROM_H))
    eeprom_defines = cparse.parse_defines(eeprom_text)
    nb_of_var = cparse.eval_int(eeprom_defines.get("NB_OF_VAR", ""),
                                eeprom_defines)
    if nb_of_var is None:
        res.fail("NB_OF_VAR", "NB_OF_VAR is missing/unresolvable in %s"
                 % EEPROM_H)
        return res
    if max_var < nb_of_var:
        res.ok()
    else:
        res.fail("MAX_VAR_ADDR",
                 "MAX_VAR_ADDR=%d must be < NB_OF_VAR=%d (EEPROM overflow)"
                 % (max_var, nb_of_var))

    # every EEPROM_* address: resolvable, in range, unique
    addresses = {}
    for name, value_text in sorted(ui_defines.items()):
        if not name.startswith("EEPROM_") or name in SPECIAL:
            continue
        value = cparse.eval_int(value_text, defines)
        if value is None:
            # constants that are not addresses (e.g. keyer memory) are skipped
            # only when they are clearly not an id; otherwise this is a bug
            res.fail(name, "%s does not resolve to an integer: %r"
                     % (name, value_text.strip()))
            continue
        if value < 0 or value > max_var:
            res.fail(name, "%s=%d is outside [0, MAX_VAR_ADDR=%d]"
                     % (name, value, max_var))
            continue
        addresses.setdefault(value, []).append(name)
        res.ok()
    for value, names in sorted(addresses.items()):
        if len(names) > 1:
            res.fail("duplicate-address",
                     "EEPROM address %d is used by: %s"
                     % (value, ", ".join(names)))

    # the compile-time guard must stay in the header
    if re.search(r"MAX_VAR_ADDR\s*>\s*NB_OF_VAR", header_raw) and \
            "#error" in header_raw:
        res.ok()
    else:
        res.fail("range-guard",
                 "%s no longer contains the #if (MAX_VAR_ADDR > NB_OF_VAR) "
                 "#error guard" % UI_H)

    # --- source: every referenced id exists -------------------------------
    source_text = _clean(os.path.join(repo, UI_C))
    # drop preprocessor lines: they are macro definitions whose body may
    # contain token-pasting fragments such as EEPROM_BAND##bandNo##_5W
    source_code = "\n".join("" if line.lstrip().startswith("#") else line
                            for line in source_text.split("\n"))
    refs = {}
    undefined = set()
    for match in _REF_RE.finditer(source_code):
        name = match.group(1)
        existing = defines.get(name)
        if existing is None:
            undefined.add(name)
            continue
        refs.setdefault(name, []).append(cparse.line_of(source_code,
                                                        match.start()))
    for name in sorted(undefined):
        res.fail("undefined-ref",
                 "%s references undefined EEPROM id %s (typo or stale header)"
                 % (UI_C, name))
    if not undefined:
        res.ok()

    # rough sanity of the descriptor table: it must exist and be non-empty
    entries = source_text.count("ConfigEntry_")
    if entries > 20:
        res.ok()
    else:
        res.fail("descriptor-table",
                 "only %d ConfigEntry_ references found in %s - the config "
                 "descriptor table seems missing/mangled"
                 % (entries, UI_C))
    return res

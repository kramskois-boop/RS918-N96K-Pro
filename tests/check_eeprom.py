#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_eeprom.py - EEPROM configuration map consistency (audit P2 item 3f, F11).

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

F11 (audit p-verify2.md:159): the #define map alone is not enough.  The actual
persistence contract is the ConfigEntryInfo[] descriptor table in
ui_configuration.c: every row binds an EEPROM_* id to a RAM variable, and
UiConfiguration_ReadConfigEntries()/WriteConfigEntries() persist exactly the
rows of that table.  A mutation such as moving the VSWR threshold descriptor
from its id (426) to a neighbouring one (EEPROM_EXPFLAGS1 = 427), swapping two
ids or deleting a row used to leave the test suite green.  Therefore the table
is parsed here (including the UI_C_EEPROM_BAND_*_PF rows, which are generated
by token pasting) and checked against a frozen binding snapshot,
tests/eeprom_descriptor_map.txt:

  * the table must parse completely: exactly one ConfigEntry_Stop, it must be
    the last row, every row has the six positional ConfigEntryDescriptor fields;
  * every row id must be an EEPROM_* symbol defined in ui_configuration.h, its
    value must be in [0, MAX_VAR_ADDR], and no two descriptors may write the
    same EEPROM address (ConfigEntry_Int32 occupies id and id+1);
  * ids handled explicitly by the Ui*SettingEEPROM_*() helpers may not also be
    owned by a descriptor row (double persistence of one address);
  * UiConfiguration_GetEntry()/ReadConfigEntries()/WriteConfigEntries() must
    still use the table (lookup by the .id field for this sparse map, and the
    read/write loops over rows up to ConfigEntry_Stop);
  * the id -> (typeId, val_ptr) binding must be identical to the frozen
    snapshot.  This is the EEPROM compatibility contract: an intentional table
    change (new setting at the end of the map) requires a deliberate snapshot
    update and review:

        python3 tests/check_eeprom.py --update-golden

The parsing is deliberately "rough" (no C preprocessor): the table is data and
the ids are #defines.  #ifdef regions of ui_configuration.c are evaluated
against the macros defined by the real headers (OBSOLETE_NR is commented out,
USE_LMS_AUTONOTCH is active), otherwise two alternative copies of the notch
descriptors would appear.  ui_configuration.c is also edited by the EEPROM
patches; the test only reads it.
"""

from __future__ import annotations

import os
import re
import sys

import cparse
from checkbase import Results

CHECK = "eeprom"
UI_H = os.path.join("mchf-eclipse", "drivers", "ui", "ui_configuration.h")
UI_C = os.path.join("mchf-eclipse", "drivers", "ui", "ui_configuration.c")
EEPROM_H = os.path.join("mchf-eclipse", "misc", "v_eprom", "eeprom.h")
# headers with helper constants used inside the EEPROM map expressions and for
# the #ifdef evaluation of ui_configuration.c
HELPER_HEADERS = (
    os.path.join("mchf-eclipse", "hardware", "uhsdr_board.h"),
    os.path.join("mchf-eclipse", "drivers", "audio", "audio_filter.h"),
    os.path.join("mchf-eclipse", "drivers", "ui", "radio_management.h"),
)
GOLDEN = os.path.join("tests", "eeprom_descriptor_map.txt")

CONFIG_ENTRY_TYPES = (
    "ConfigEntry_Stop",
    "ConfigEntry_UInt8",
    "ConfigEntry_UInt16",
    "ConfigEntry_UInt32_16",
    "ConfigEntry_Int32_16",
    "ConfigEntry_Int16",
    "ConfigEntry_Int32",
)
# ConfigEntry_Int32 saves/reads two subsequent 16 bit words: id and id+1
TYPE_SPAN = {"ConfigEntry_Int32": 2}

# names that are not configuration-address ids
SPECIAL = {"EEPROM_FIRST_UNUSED", "EEPROM_KEYER_MEMORY_ADDRESS"}
# ids that are intentionally reserved for future use
_REF_RE = re.compile(r"\b(EEPROM_\w+)\b")
_ROW_ID_RE = re.compile(r"^EEPROM_[A-Za-z0-9_]+$")
_ROW_MACRO_RE = re.compile(
    r"^[ \t]*#\s*define\s+([A-Za-z_]\w*)\(([^)]*)\)[ \t]*(.*)$", re.M)
_TABLE_RE = re.compile(r"\bConfigEntryInfo\s*\[\s*\]\s*=\s*\{")
_GETENTRY_BY_ID_RE = re.compile(r"ConfigEntryInfo\s*\[\s*\w+\s*\]\s*\.\s*id\s*==")
_GETENTRY_BY_INDEX_RE = re.compile(
    r"ConfigEntryInfo\s*\[\s*(?:\(\s*u?int\d*_t\s*\)\s*)?id\s*\]")
_ROW_LOOP_RE = re.compile(
    r"ConfigEntryInfo\[\w+\]\.typeId\s*!=\s*ConfigEntry_Stop")
_EXPLICIT_ACCESS_RE = re.compile(r"\bUi(?:Read|Write)SettingEEPROM_\w+\s*\(")
_NUMBER_OF_ENTRIES = "EEPROM_NUMBER_OF_ENTRIES"
_GOLDEN_UPDATE_HINT = ("intentional table change? review the diff and run "
                       "python3 tests/check_eeprom.py --update-golden")


# ---------------------------------------------------------------------------
# parsing helpers
# ---------------------------------------------------------------------------


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


def _clean(path, apply_pp=True, defined_names=None):
    text = cparse.strip_comments(cparse.read_text(path))
    return cparse.apply_preprocessor(text,
                                     defined_names=defined_names) if apply_pp \
        else text


def _read_headers(repo):
    """Parse #defines/enums of ui_configuration.h and its helper headers."""
    defines = {}
    for rel in (UI_H, EEPROM_H) + HELPER_HEADERS:
        path = os.path.join(repo, rel)
        if not os.path.isfile(path):
            continue
        text = cparse.strip_comments(cparse.read_text(path))
        defines.update(cparse.parse_defines(text))
        defines.update(_enum_defines(text))
    return defines


def _defined_names(defines):
    return set(defines.keys())


def _table_text(repo, defines):
    """Comment-stripped ui_configuration.c with #if regions evaluated."""
    return _clean(os.path.join(repo, UI_C), defined_names=_defined_names(defines))


def _row_macros(table_text):
    """Function-like macros that generate ConfigEntryInfo[] rows."""
    macros = {}
    for match in _ROW_MACRO_RE.finditer(table_text):
        name, params, value = match.group(1), match.group(2), match.group(3)
        if not name.startswith("UI_C_"):
            continue
        params = [p.strip() for p in params.split(",") if p.strip()]
        macros[name] = (params, value.strip())
    return macros


def _expand_row_macro(name, args, params, value, line):
    """Expand one UI_C_* row macro (parameters substituted, ## pasted)."""
    if len(args) != len(params):
        return None, ("%s:%d: macro %s called with %d argument(s), expected %d"
                      % (UI_C, line, name, len(args), len(params)))
    expanded = value
    for param, arg in zip(params, args):
        expanded = re.sub(r"\b%s\b" % re.escape(param), arg, expanded)
    while "##" in expanded:
        pasted = re.sub(r"([A-Za-z0-9_]+)\s*##\s*([A-Za-z0-9_]+)", r"\1\2",
                        expanded)
        if pasted == expanded:
            return None, ("%s:%d: cannot paste tokens in macro %s: %r"
                          % (UI_C, line, name, value))
        expanded = pasted
    match = re.search(r"\{(.*)\}", expanded, re.S)
    if not match:
        return None, ("%s:%d: macro %s does not expand to a descriptor row: %r"
                      % (UI_C, line, name, expanded))
    fields = [f.strip() for f in cparse.split_top_level(match.group(1))
              if f.strip()]
    return {"line": line, "fields": fields, "macro": name}, None


def _parse_descriptor_table(table_text):
    """Parse ConfigEntryInfo[] into rows, expanding the row-generator macros.

    Returns (rows, errors): rows are dicts {line, fields, macro}, errors are
    (code, message) tuples.  Comments must already be stripped and #if regions
    evaluated by the caller (line numbers are preserved).
    """
    match = _TABLE_RE.search(table_text)
    if not match:
        return [], [("descriptor-table",
                     "%s has no ConfigEntryInfo[] initializer" % UI_C)]
    body, _end = cparse.brace_body(table_text, match.end() - 1)
    macros = _row_macros(table_text)
    rows = []
    errors = []
    index = 0
    length = len(body)
    while index < length:
        char = body[index]
        if char in " \t\r\n,":
            index += 1
            continue
        line = cparse.line_of(table_text, match.end() + index)
        if char == "{":
            try:
                row_body, next_index = cparse.brace_body(body, index)
            except ValueError:
                errors.append(("descriptor-table",
                               "%s:%d: unbalanced braces in descriptor row"
                               % (UI_C, line)))
                break
            fields = [f.strip() for f in cparse.split_top_level(row_body)
                      if f.strip()]
            rows.append({"line": line, "fields": fields, "macro": None})
            index = next_index
            continue
        macro_match = re.match(r"([A-Za-z_]\w*)\s*\(", body[index:])
        if macro_match and macro_match.group(1) in macros:
            name = macro_match.group(1)
            params, value = macros[name]
            open_index = index + macro_match.end() - 1
            depth = 0
            close_index = open_index
            while close_index < length:
                if body[close_index] == "(":
                    depth += 1
                elif body[close_index] == ")":
                    depth -= 1
                    if depth == 0:
                        break
                close_index += 1
            if depth != 0:
                errors.append(("descriptor-table",
                               "%s:%d: unbalanced call to %s"
                               % (UI_C, line, name)))
                break
            args = [a.strip() for a in
                    cparse.split_top_level(body[open_index + 1:close_index])]
            row, error = _expand_row_macro(name, args, params, value, line)
            if error is not None:
                errors.append(("descriptor-table", error))
            else:
                rows.append(row)
            index = close_index + 1
            continue
        errors.append(("descriptor-table",
                       "%s:%d: cannot parse ConfigEntryInfo[] element starting "
                       "with %r" % (UI_C, line, body[index:index + 40])))
        break
    return rows, errors


def _descriptor_spans(row):
    """Yield all EEPROM addresses a descriptor writes."""
    type_id = row["fields"][0]
    addr = row.get("addr")
    if addr is None:
        return
    for step in range(TYPE_SPAN.get(type_id, 1)):
        yield addr + step


def _normalise_expression(text):
    return re.sub(r"\s+", "", text or "")


def _explicit_helper_ids(source_code, defines):
    """EEPROM ids passed explicitly to the Ui*SettingEEPROM_*() helpers."""
    found = {}
    for match in _EXPLICIT_ACCESS_RE.finditer(source_code):
        open_index = match.end() - 1
        depth = 0
        close_index = open_index
        while close_index < len(source_code):
            if source_code[close_index] == "(":
                depth += 1
            elif source_code[close_index] == ")":
                depth -= 1
                if depth == 0:
                    break
            close_index += 1
        args = source_code[open_index + 1:close_index]
        for id_match in re.finditer(r"\b(EEPROM_\w+)\b", args):
            name = id_match.group(1)
            if name in SPECIAL:
                continue
            value = cparse.eval_int(name, defines)
            if value is None:
                continue
            found.setdefault(value, []).append(
                cparse.line_of(source_code, match.start()))
    return found


def _function_body(text, name):
    """Return the body of the (first, uncommented) definition of a C function."""
    pattern = re.compile(r"\b%s\s*\([^;{}]*\)\s*\{" % re.escape(name))
    match = pattern.search(text)
    if not match:
        return None
    try:
        body, _end = cparse.brace_body(text, match.end() - 1)
    except ValueError:
        return None
    return body


def _load_golden(path):
    """Read the frozen descriptor map: {addr: (symbol, type, val_ptr)}."""
    entries = {}
    errors = []
    try:
        with open(path, "r", encoding="utf-8") as handle:
            lines = handle.readlines()
    except OSError as exc:
        return None, ["cannot read %s: %s" % (GOLDEN, exc)]
    for number, line in enumerate(lines, 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) != 4:
            errors.append("%s:%d: expected 4 tab separated columns, got %d"
                          % (GOLDEN, number, len(fields)))
            continue
        address, symbol, type_id, value_ptr = fields
        try:
            address = int(address)
        except ValueError:
            errors.append("%s:%d: %r is not an EEPROM address"
                          % (GOLDEN, number, fields[0]))
            continue
        if address in entries:
            errors.append("%s:%d: duplicate address %d in the snapshot"
                          % (GOLDEN, number, address))
            continue
        entries[address] = (symbol, type_id, value_ptr)
    return entries, errors


def _format_golden(entries):
    lines = [
        "# N96K-Pro EEPROM descriptor binding snapshot - owner: tests/check_eeprom.py",
        "#",
        "# One row per ConfigEntryInfo[] descriptor from",
        "# mchf-eclipse/drivers/ui/ui_configuration.c, sorted by EEPROM address:",
        "#   <address>\t<EEPROM_* id symbol>\t<ConfigEntry type>\t<bound variable>",
        "#",
        "# This file freezes the persistence contract (which setting lives in which",
        "# EEPROM word).  Stored user settings are keyed by these addresses, so a",
        "# change here is a migration and must be reviewed, not silently accepted.",
        "# Regenerate only for an intentional, reviewed map change:",
        "#   python3 tests/check_eeprom.py --update-golden",
        "",
    ]
    for address in sorted(entries):
        symbol, type_id, value_ptr = entries[address]
        lines.append("%d\t%s\t%s\t%s"
                     % (address, symbol, type_id, value_ptr))
    lines.append("")
    return "\n".join(lines)


def _write_golden(repo, entries):
    path = os.path.join(repo, GOLDEN)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(_format_golden(entries))
    return path


# ---------------------------------------------------------------------------
# checks
# ---------------------------------------------------------------------------


def _check_descriptor_table(repo, res, defines, ui_defines, max_var):
    """F11: parse ConfigEntryInfo[] and verify its structure and binding."""
    if not os.path.isfile(os.path.join(repo, UI_C)):
        res.fail("missing", "required file not found: %s" % UI_C)
        return None
    table_text = _table_text(repo, defines)
    rows, errors = _parse_descriptor_table(table_text)
    for _code, message in errors:
        res.fail("descriptor-table", message)
    if not rows:
        return None

    stop_rows = [row for row in rows if row["fields"] and
                 row["fields"][0] == "ConfigEntry_Stop"]
    if len(stop_rows) != 1:
        res.fail("descriptor-stop",
                 "%s: expected exactly one ConfigEntry_Stop row, found %d"
                 % (UI_C, len(stop_rows)))
    elif rows[-1] is not stop_rows[0]:
        res.fail("descriptor-stop",
                 "%s:%d: ConfigEntry_Stop must be the last row (rows after it "
                 "are silently never persisted)"
                 % (UI_C, stop_rows[0]["line"]))
    else:
        res.ok()

    data_rows = [row for row in rows if row["fields"] and
                 row["fields"][0] != "ConfigEntry_Stop"]
    if not data_rows:
        res.fail("descriptor-table",
                 "%s: ConfigEntryInfo[] contains no descriptors" % UI_C)
        return None

    occupied = {}
    entries = {}
    for row in data_rows:
        fields = row["fields"]
        line = row["line"]
        loc = "%s:%d" % (UI_C, line)
        if len(fields) != 6:
            res.fail("descriptor-row",
                     "%s: expected 6 positional ConfigEntryDescriptor fields "
                     "(type, id, val_ptr, default, min, max), got %d: %s"
                     % (loc, len(fields), ", ".join(fields)))
            continue
        type_id, id_expr, value_ptr, default, minimum, maximum = fields
        if type_id not in CONFIG_ENTRY_TYPES or type_id == "ConfigEntry_Stop":
            res.fail("descriptor-type",
                     "%s: unknown ConfigEntry typeId %r" % (loc, type_id))
            continue
        if not _ROW_ID_RE.match(id_expr):
            res.fail("descriptor-id",
                     "%s: descriptor id %r is not an EEPROM_* symbol"
                     % (loc, id_expr))
            continue
        if id_expr not in ui_defines:
            res.fail("descriptor-id",
                     "%s: descriptor id %s is not defined in %s"
                     % (loc, id_expr, UI_H))
            continue
        if id_expr in SPECIAL:
            res.fail("descriptor-id",
                     "%s: %s is not a configuration address"
                     % (loc, id_expr))
            continue
        address = cparse.eval_int(id_expr, defines)
        if address is None:
            res.fail("descriptor-id",
                     "%s: cannot resolve %s to an integer" % (loc, id_expr))
            continue
        if address < 0 or address > max_var:
            res.fail("descriptor-id",
                     "%s: %s=%d is outside [0, MAX_VAR_ADDR=%d]"
                     % (loc, id_expr, address, max_var))
            continue
        row["addr"] = address
        res.ok()
        for step, addr in enumerate(_descriptor_spans(row)):
            if addr in occupied:
                res.fail("descriptor-address",
                         "%s: %s writes EEPROM address %d, already owned by "
                         "%s at line %d"
                         % (loc, id_expr, addr, occupied[addr][0],
                            occupied[addr][1]))
                continue
            occupied[addr] = (id_expr, line)
        entries[address] = (id_expr, type_id,
                            _normalise_expression(value_ptr))

    # the helper functions persist by explicit id: those addresses may not also
    # be owned by a table descriptor (double bookkeeping of one EEPROM word)
    source_text = _clean(os.path.join(repo, UI_C),
                         defined_names=_defined_names(defines))
    table_match = _TABLE_RE.search(source_text)
    if table_match:
        _body, table_end = cparse.brace_body(source_text, table_match.end() - 1)
        outside = (source_text[:table_match.start()] +
                   " " * (table_end - table_match.start()) +
                   source_text[table_end:])
    else:
        outside = source_text
    explicit = _explicit_helper_ids(outside, defines)
    for address, lines in sorted(explicit.items()):
        if address in occupied:
            res.fail("descriptor-conflict",
                     "%s: address %d is persisted explicitly by "
                     "Ui*SettingEEPROM_*() (line %s) and by the descriptor for "
                     "%s (line %d) - one of them double-writes the setting"
                     % (UI_C, address, ",".join(str(n) for n in lines),
                        occupied[address][0], occupied[address][1]))
        else:
            res.ok()

    # the table must actually be used by the load/save path
    for function in ("UiConfiguration_ReadConfigEntries",
                     "UiConfiguration_WriteConfigEntries"):
        body = _function_body(source_text, function)
        if body is None:
            res.fail("descriptor-unused",
                     "%s: %s() is missing" % (UI_C, function))
        elif _ROW_LOOP_RE.search(body) is None:
            res.fail("descriptor-unused",
                     "%s: %s() no longer iterates ConfigEntryInfo[] rows up to "
                     "ConfigEntry_Stop" % (UI_C, function))
        else:
            res.ok()

    # UiConfiguration_GetEntry(): this map is sparse (the table row order is
    # not the EEPROM address order), so a linear search by the .id field is the
    # only correct lookup.  If it is ever replaced by ConfigEntryInfo[id], the
    # table must become dense first.
    get_entry = _function_body(source_text, "UiConfiguration_GetEntry")
    if get_entry is None:
        res.fail("getentry", "%s: UiConfiguration_GetEntry() is missing"
                 % UI_C)
    elif _GETENTRY_BY_ID_RE.search(get_entry):
        res.ok()
    elif _GETENTRY_BY_INDEX_RE.search(get_entry):
        gaps = [addr for addr in range(0, max_var + 1)
                if addr not in occupied]
        if gaps:
            res.fail("getentry",
                     "%s: UiConfiguration_GetEntry() indexes ConfigEntryInfo[] "
                     "directly, but the table is sparse (%d of %d EEPROM "
                     "addresses have no descriptor, first gap: %d) - the lookup "
                     "must compare the .id field (or the table must be made "
                     "dense)" % (UI_C, len(gaps), max_var + 1, gaps[0]))
        else:
            res.ok()
    else:
        res.fail("getentry",
                 "%s: UiConfiguration_GetEntry() neither searches by the .id "
                 "field nor indexes ConfigEntryInfo[] directly" % UI_C)

    # the self-describing "number of entries" word must still announce the map
    # size from the header, not the number of table rows
    count_entries = [row for row in data_rows
                     if row["fields"][1] == _NUMBER_OF_ENTRIES]
    if len(count_entries) != 1:
        res.fail("entries-count",
                 "%s: expected exactly one descriptor for %s, found %d"
                 % (UI_C, _NUMBER_OF_ENTRIES, len(count_entries)))
    else:
        default = count_entries[0]["fields"][3]
        declared = cparse.eval_int(default, defines)
        if declared != cparse.eval_int("EEPROM_FIRST_UNUSED", defines):
            res.fail("entries-count",
                     "%s:%d: %s default is %r, expected EEPROM_FIRST_UNUSED "
                     "(%d)" % (UI_C, count_entries[0]["line"], _NUMBER_OF_ENTRIES,
                               default, cparse.eval_int("EEPROM_FIRST_UNUSED",
                                                        defines)))
        else:
            res.ok()

    # frozen binding snapshot
    golden_path = os.path.join(repo, GOLDEN)
    golden, golden_errors = _load_golden(golden_path)
    if golden is None:
        for message in golden_errors:
            res.fail("descriptor-golden", message)
        res.fail("descriptor-golden",
                 "missing %s - cannot verify the EEPROM binding" % GOLDEN)
        return entries
    for message in golden_errors:
        res.fail("descriptor-golden", message)
    changed = []
    for address in sorted(set(golden) | set(entries)):
        want = golden.get(address)
        have = entries.get(address)
        if want == have:
            res.ok()
            continue
        if want is None:
            changed.append("%s: new descriptor %s (addr %d) is not in the "
                           "snapshot" % (UI_C, have[0], address))
        elif have is None:
            changed.append("%s: descriptor %s (addr %d, %s, %s) disappeared"
                           % (UI_C, want[0], address, want[1], want[2]))
        else:
            changed.append("%s: addr %d binding changed: %s/%s/%s -> %s/%s/%s"
                           % (UI_C, address, want[0], want[1], want[2],
                              have[0], have[1], have[2]))
    for message in changed[:10]:
        res.fail("descriptor-golden", message)
    if changed:
        res.fail("descriptor-golden",
                 "%d EEPROM descriptor binding(s) differ from %s; %s"
                 % (len(changed), GOLDEN, _GOLDEN_UPDATE_HINT))
    return entries


def run(repo):
    res = Results(CHECK)
    for path in (UI_H, UI_C, EEPROM_H):
        if not os.path.isfile(os.path.join(repo, path)):
            res.fail("missing", "required file not found: %s" % path)
    if res.failed:
        return res

    defines = _read_headers(repo)

    # --- header: ids / MAX_VAR_ADDR ---------------------------------------
    # The header keeps its historical parsing: the include guard would blank
    # the whole file if it were evaluated against the macros it defines itself.
    header_text = _clean(os.path.join(repo, UI_H))
    header_raw = cparse.strip_comments(
        cparse.read_text(os.path.join(repo, UI_H)))
    ui_defines = dict(cparse.parse_defines(header_text))   # ids of this header
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
    source_text = _clean(os.path.join(repo, UI_C),
                         defined_names=_defined_names(defines))
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

    # --- F11: ConfigEntryInfo[] <-> EEPROM_* binding ----------------------
    _check_descriptor_table(repo, res, defines, ui_defines, max_var)
    return res


def _main(argv):
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if "--update-golden" not in argv:
        sys.stderr.write("usage: python3 tests/check_eeprom.py "
                         "[--update-golden]\n")
        return 2
    defines = _read_headers(repo)
    table_text = _table_text(repo, defines)
    rows, errors = _parse_descriptor_table(table_text)
    if errors:
        for _code, message in errors:
            sys.stderr.write("%s\n" % message)
        return 1
    entries = {}
    for row in rows:
        if not row["fields"] or row["fields"][0] == "ConfigEntry_Stop":
            continue
        if len(row["fields"]) != 6:
            sys.stderr.write("%s:%d: unexpected descriptor row\n"
                             % (UI_C, row["line"]))
            return 1
        type_id, id_expr, value_ptr = row["fields"][:3]
        address = cparse.eval_int(id_expr, defines)
        if address is None:
            sys.stderr.write("%s:%d: cannot resolve %s\n"
                             % (UI_C, row["line"], id_expr))
            return 1
        entries[address] = (id_expr, type_id, _normalise_expression(value_ptr))
    path = _write_golden(repo, entries)
    sys.stdout.write("wrote %s (%d descriptors)\n" % (path, len(entries)))
    return 0


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))

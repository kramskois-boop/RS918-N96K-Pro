#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_vswr.py - VSWR/PA foldback protection invariants (audit P0-4/P1-1/A1).

The VSWR protection is safety code: it is the only thing between a badly
matched antenna and the PA.  Host tests cannot execute it (no target, no ADC),
but every invariant that matters is visible in the C sources, so this module
locks them down statically.  Coverage:

  (a) arm/retry/recovery state machine constants and the hysteresis;
  (b) the threshold is a run-time configuration value stored in the EEPROM
      descriptor EEPROM_VSWR_PROTECTION_THRESHOLD (default 3, min 1 = OFF,
      max 10) and registered as the "VSWR Protection" configuration menu item;
  (c) A1 invariant: while the foldback is engaged (including a recovery trial)
      RadioManagement_SetBandPowerFactor() forces PA_LEVEL_MINIMAL, and
      RadioManagement_IsVswrFoldbackActive() is declared, defined and used;
  (d) VSWR calculation: fwd_pwr >= SWR_MIN_CALC_POWER guard before the
      division, NaN/negative/ratio>1 clamps and the VSWR_MAX_CALC_VALUE (49)
      cap for a (nearly) infinite mismatch;
  (e) recovery happens only at minimal drive: the PA bias is re-enabled for a
      trial only after the drive has been reduced to PA_LEVEL_MINIMAL, and the
      user power is restored only after RECOVERY_CNT good measurements below
      (threshold - hysteresis);
  (f) the "SWR!" indication in ui_driver.c is driven by
      swrm.high_vswr_detected and the normal power display is restored;
  (g) TX outside the amateur bands is blocked unless
      FLAGS1_TX_OUTSIDE_BANDS (CONFIG_TX_OUT_ENABLE) is set, both in
      RadioManagement_ValidateFrequencyForTX() (interlock) and in
      RadioManagement_SetBandPowerFactor() (second barrier, drive = 0).

The checks are textual but mutation sensitive: removing a safety call, moving
the bias enable in front of the drive reduction, dropping the fwd_power guard
or the VSWR cap, changing the threshold/defaults or deleting the menu item /
"SWR!" indication makes run() fail with a pointed message.  The sources are
only read, never modified.
"""

from __future__ import annotations

import os
import re

import cparse
from checkbase import Results

CHECK = "vswr"

RM_C = os.path.join("mchf-eclipse", "drivers", "ui", "radio_management.c")
RM_H = os.path.join("mchf-eclipse", "drivers", "ui", "radio_management.h")
UI_DRIVER_C = os.path.join("mchf-eclipse", "drivers", "ui", "ui_driver.c")
UI_DRIVER_H = os.path.join("mchf-eclipse", "drivers", "ui", "ui_driver.h")
UI_CONF_C = os.path.join("mchf-eclipse", "drivers", "ui", "ui_configuration.c")
UI_CONF_H = os.path.join("mchf-eclipse", "drivers", "ui", "ui_configuration.h")
UI_MENU_C = os.path.join("mchf-eclipse", "drivers", "ui", "menu", "ui_menu.c")
UI_MENU_STRUCT = os.path.join("mchf-eclipse", "drivers", "ui", "menu",
                              "ui_menu_structure.c")
UI_MENU_INT = os.path.join("mchf-eclipse", "drivers", "ui", "menu",
                           "ui_menu_internal.h")
BOARD_H = os.path.join("mchf-eclipse", "hardware", "uhsdr_board.h")

SOURCES = (RM_C, RM_H, UI_DRIVER_C, UI_DRIVER_H, UI_CONF_C, UI_CONF_H,
           UI_MENU_C, UI_MENU_STRUCT, UI_MENU_INT, BOARD_H)


# ---------------------------------------------------------------------------
# small parsing helpers
# ---------------------------------------------------------------------------


def _clean(path):
    """Comment stripped, disabled #if regions blanked; line numbers preserved."""
    return cparse.apply_preprocessor(cparse.strip_comments(cparse.read_text(path)))


def _enum_values(text, marker):
    """Values of the enum block that contains marker (e.g. PA_LEVEL_FULL)."""
    index = text.find(marker)
    while index >= 0:
        start = text.rfind("enum", 0, index)
        if start < 0:
            return {}
        brace = text.find("{", start, index + len(marker))
        if brace < 0:
            return {}
        try:
            body, _end = cparse.brace_body(text, brace)
        except ValueError:
            return {}
        values = {}
        value = 0
        for chunk in cparse.split_top_level(body):
            chunk = chunk.strip()
            if not chunk or chunk.startswith("#"):
                continue
            if "=" in chunk:
                name, expr = chunk.split("=", 1)
                name = name.strip()
                explicit = cparse.eval_int(expr, {})
                if explicit is None:
                    continue
                value = explicit
            else:
                name = chunk
            name = re.sub(r"\[.*", "", name).strip()
            if re.match(r"^[A-Za-z_]\w*$", name):
                values.setdefault(name, value)
            value += 1
        if values:
            return values
        index = text.find(marker, index + 1)
    return {}


def _define_number(text, name):
    """Numeric value of a #define (integer or float literal, trailing f/L)."""
    match = re.search(r"^[ \t]*#\s*define\s+%s\s+([-+]?[0-9.]+)[fFlL]?[ \t]*$"
                      % re.escape(name), text, re.M)
    if not match:
        return None
    token = match.group(1)
    try:
        return float(token) if "." in token else int(token)
    except ValueError:
        return None


def _find(text, pattern):
    return re.search(pattern, text, re.S)


def _has(text, pattern):
    return re.search(pattern, text, re.S) is not None


def _count(text, pattern):
    return len(re.findall(pattern, text, re.S))


def _block_after(text, match):
    """Body of the { ... } block that follows a regex match, or None."""
    brace = text.find("{", match.end())
    if brace < 0:
        return None
    try:
        body, _end = cparse.brace_body(text, brace)
    except ValueError:
        return None
    return body


def _function(text, name):
    """(body, line) of the first definition of name, or (None, None).

    A plain search for "name (...)" would also match calls: e.g.
    RadioManagement_IsVswrFoldbackActive() is called in the condition of an
    "if (...) {" statement, so the call would be taken for the definition.
    A definition is recognised by requiring a C declaration prefix (return
    type token(s)) between the start of the line and the name; calls inside
    expressions or as statements at the start of a line have none.
    """
    for match in re.finditer(r"\b%s\s*\(" % re.escape(name), text):
        line_start = text.rfind("\n", 0, match.start()) + 1
        prefix = text[line_start:match.start()]
        if not re.match(r"^[ \t]*(?:[A-Za-z_]\w*[ \t]+)+\**$", prefix):
            continue
        depth = 0
        index = match.end() - 1
        while index < len(text):
            char = text[index]
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0:
                    break
            index += 1
        if depth != 0:
            continue
        index += 1
        while index < len(text) and text[index] in " \t\r\n":
            index += 1
        if index >= len(text) or text[index] != "{":
            continue
        try:
            body, _end = cparse.brace_body(text, index)
        except ValueError:
            continue
        return body, cparse.line_of(text, match.start())
    return None, None


def _expect(res, code, condition, message):
    if condition:
        res.ok()
    else:
        res.fail(code, message)


def _loc(rel, line, function=None):
    if line is None:
        return rel
    if function:
        return "%s:%d %s()" % (rel.replace("\\", "/"), line, function)
    return "%s:%d" % (rel.replace("\\", "/"), line)


# ---------------------------------------------------------------------------
# (a) state machine constants
# ---------------------------------------------------------------------------


def _check_constants(res, rm_text):
    expected = {
        "VSWR_MAX_CALC_VALUE": 49.0,
        "VSWR_PROTECTION_HYSTERESIS": 0.5,
        "VSWR_PROTECTION_ARM_CNT": 3,
        "VSWR_PROTECTION_RECOVERY_CNT": 10,
        "VSWR_PROTECTION_RETRY_CNT": 8,
    }
    values = {}
    for name, want in sorted(expected.items()):
        value = _define_number(rm_text, name)
        values[name] = value
        _expect(res, "const-" + name, value == want,
                "%s: %s is %r, expected %r (the PA safety state machine "
                "constants are a reviewed contract)" % (RM_C, name, value, want))
    if None in values.values():
        return
    _expect(res, "const-cap",
            values["VSWR_MAX_CALC_VALUE"] >= 10
            and values["VSWR_MAX_CALC_VALUE"] <= 100,
            "%s: VSWR_MAX_CALC_VALUE=%r is not a sensible cap for a (nearly) "
            "infinite mismatch (expected a finite value in 10..100)"
            % (RM_C, values["VSWR_MAX_CALC_VALUE"]))
    _expect(res, "const-hysteresis",
            0 < values["VSWR_PROTECTION_HYSTERESIS"] < 5,
            "%s: VSWR_PROTECTION_HYSTERESIS=%r must be a small positive "
            "window (foldback release is below threshold - hysteresis)"
            % (RM_C, values["VSWR_PROTECTION_HYSTERESIS"]))
    _expect(res, "const-arm",
            1 < values["VSWR_PROTECTION_ARM_CNT"] <= 5,
            "%s: VSWR_PROTECTION_ARM_CNT=%r must keep the arm window short "
            "(2..5 measurement ticks ~20..50 ms, audit R1)"
            % (RM_C, values["VSWR_PROTECTION_ARM_CNT"]))
    _expect(res, "const-recovery",
            values["VSWR_PROTECTION_RECOVERY_CNT"] >= 4
            and values["VSWR_PROTECTION_RETRY_CNT"] >= 1,
            "%s: recovery/retry counters are degenerate "
            "(RECOVERY_CNT=%r, RETRY_CNT=%r)"
            % (RM_C, values["VSWR_PROTECTION_RECOVERY_CNT"],
               values["VSWR_PROTECTION_RETRY_CNT"]))
    _expect(res, "const-hysteresis",
            values["VSWR_PROTECTION_HYSTERESIS"] < 2.0,
            "%s: VSWR_PROTECTION_HYSTERESIS=%r is >= the smallest active "
            "threshold (2), the protection could never release"
            % (RM_C, values["VSWR_PROTECTION_HYSTERESIS"]))


# ---------------------------------------------------------------------------
# (b) threshold configuration: EEPROM descriptor + menu
# ---------------------------------------------------------------------------


def _normalise(text):
    return re.sub(r"\s+", "", text)


def _check_threshold_config(res, conf_h, conf_c, board_h, menu_struct,
                            menu_int, menu_c, header_defs):
    first_unused = cparse.eval_int(header_defs.get("EEPROM_FIRST_UNUSED", ""),
                                   header_defs)
    max_var = cparse.eval_int(header_defs.get("MAX_VAR_ADDR", ""), header_defs)
    slot = cparse.eval_int(header_defs.get("EEPROM_VSWR_PROTECTION_THRESHOLD",
                                          ""), header_defs)
    _expect(res, "eeprom-slot", slot is not None,
            "%s: EEPROM_VSWR_PROTECTION_THRESHOLD is not defined"
            % UI_CONF_H)
    if slot is not None:
        _expect(res, "eeprom-slot", slot == 426,
                "%s: EEPROM_VSWR_PROTECTION_THRESHOLD=%r, expected 426 "
                "(the reserved slot of the fork/upstream 2.12.2 map; moving it "
                "would change the EEPROM layout)" % (UI_CONF_H, slot))
        _expect(res, "eeprom-slot", max_var is not None
                and 0 <= slot <= max_var,
                "%s: EEPROM slot %r is outside [0, MAX_VAR_ADDR=%r]"
                % (UI_CONF_H, slot, max_var))
        if first_unused is not None:
            _expect(res, "eeprom-slot", slot < first_unused,
                    "%s: EEPROM slot %r is not below EEPROM_FIRST_UNUSED=%r"
                    % (UI_CONF_H, slot, first_unused))

    # thresholds: 1 = OFF, 2..10 = trip value, default 3
    off = cparse.eval_int(header_defs.get("VSWR_PROTECTION_THRESHOLD_OFF", ""),
                          header_defs)
    minimum = cparse.eval_int(header_defs.get("VSWR_PROTECTION_THRESHOLD_MIN", ""),
                              header_defs)
    maximum = cparse.eval_int(header_defs.get("VSWR_PROTECTION_THRESHOLD_MAX", ""),
                              header_defs)
    default = cparse.eval_int(
        header_defs.get("VSWR_PROTECTION_THRESHOLD_DEFAULT", ""), header_defs)
    _expect(res, "threshold-off", off == 1,
            "%s: VSWR_PROTECTION_THRESHOLD_OFF=%r, expected 1 (= protection "
            "OFF, upstream 2.12.2 compatible)" % (RM_H, off))
    _expect(res, "threshold-min", minimum == off,
            "%s: VSWR_PROTECTION_THRESHOLD_MIN=%r, expected OFF=%r"
            % (RM_H, minimum, off))
    _expect(res, "threshold-max", maximum == 10,
            "%s: VSWR_PROTECTION_THRESHOLD_MAX=%r, expected 10 (menu range "
            "1..10)" % (RM_H, maximum))
    _expect(res, "threshold-default", default == 3,
            "%s: VSWR_PROTECTION_THRESHOLD_DEFAULT=%r, expected 3"
            % (RM_H, default))
    if None not in (off, minimum, maximum, default):
        _expect(res, "threshold-range",
                minimum <= default <= maximum and off < maximum,
                "%s: threshold default %r is outside [%r, %r]"
                % (RM_H, default, minimum, maximum))

    # ConfigEntryDescriptor: id -> &ts.vswr_protection_threshold with the macros
    descriptor = _find(_normalise(conf_c),
                       r"\{ConfigEntry_UInt8,EEPROM_VSWR_PROTECTION_THRESHOLD,"
                       r"&ts\.vswr_protection_threshold,"
                       r"VSWR_PROTECTION_THRESHOLD_DEFAULT,"
                       r"VSWR_PROTECTION_THRESHOLD_MIN,"
                       r"VSWR_PROTECTION_THRESHOLD_MAX\}")
    _expect(res, "descriptor", descriptor is not None,
            "%s: no ConfigEntryDescriptor binds EEPROM_VSWR_PROTECTION_"
            "THRESHOLD to &ts.vswr_protection_threshold with the "
            "VSWR_PROTECTION_THRESHOLD_* macros - the menu value would not be "
            "persisted (or not restored) across a power cycle" % UI_CONF_C)

    # the RAM field must be a uint8_t in ts (the descriptor type is UInt8)
    field = _find(board_h, r"uint8_t\s+vswr_protection_threshold\s*;")
    _expect(res, "ts-field", field is not None,
            "%s: ts.vswr_protection_threshold is not declared as uint8_t "
            "(mismatch with ConfigEntry_UInt8)" % BOARD_H)

    # menu registration: enum value, configuration menu item, handler
    _expect(res, "menu-enum",
            _has(menu_int,
                 r"CONFIG_VSWR_PROTECTION_THRESHOLD\s*,\s*[^\n]*\n"
                 r"[^\n]*MAX_RADIO_CONFIG_ITEM"),
            "%s: CONFIG_VSWR_PROTECTION_THRESHOLD is not in the radio config "
            "item enum before MAX_RADIO_CONFIG_ITEM" % UI_MENU_INT)
    _expect(res, "menu-item",
            _has(menu_struct,
                 r"\{\s*MENU_CONF\s*,\s*MENU_ITEM\s*,\s*"
                 r"CONFIG_VSWR_PROTECTION_THRESHOLD\s*,\s*NULL\s*,\s*"
                 r"\"VSWR Protection\""),
            "%s: the \"VSWR Protection\" configuration menu item is not "
            "registered (a stored threshold would be invisible/unreachable)"
            % UI_MENU_STRUCT)

    case = re.search(r"case\s+CONFIG_VSWR_PROTECTION_THRESHOLD\s*:", menu_c)
    handler = ""
    if case is not None:
        end = re.search(r"\b(break|case\s+\w+|default)\s*:", menu_c[case.end():])
        handler = menu_c[case.end():case.end() + end.start()] if end else \
            menu_c[case.end():]
    _expect(res, "menu-handler", case is not None,
            "%s: no case CONFIG_VSWR_PROTECTION_THRESHOLD handler - the menu "
            "item would do nothing" % UI_MENU_C)
    if case is not None:
        _expect(res, "menu-handler-var",
                _has(handler, r"&ts\.vswr_protection_threshold"),
                "%s: the VSWR Protection menu handler does not edit "
                "ts.vswr_protection_threshold" % UI_MENU_C)
        _expect(res, "menu-handler-limits",
                _has(handler, r"UiDriverMenuItemChangeUInt8") and
                _has(handler, r"VSWR_PROTECTION_THRESHOLD_MIN") and
                _has(handler, r"VSWR_PROTECTION_THRESHOLD_MAX") and
                _has(handler, r"VSWR_PROTECTION_THRESHOLD_DEFAULT"),
                "%s: the VSWR Protection menu handler does not use "
                "UiDriverMenuItemChangeUInt8 with the VSWR_PROTECTION_"
                "THRESHOLD_MIN/MAX/DEFAULT limits" % UI_MENU_C)
        _expect(res, "menu-handler-off",
                _has(handler, r"\"OFF\""),
                "%s: the VSWR Protection menu handler does not display \"OFF\" "
                "for threshold <= VSWR_PROTECTION_THRESHOLD_OFF" % UI_MENU_C)


# ---------------------------------------------------------------------------
# (c) A1: foldback forces minimal drive, power factor clamp
# ---------------------------------------------------------------------------


def _check_power_factor_clamp(res, rm_text, rm_h, board_h):
    body, line = _function(rm_text, "RadioManagement_SetBandPowerFactor")
    loc = _loc(RM_C, line, "RadioManagement_SetBandPowerFactor")
    _expect(res, "clamp-function", body is not None,
            "%s: RadioManagement_SetBandPowerFactor() is missing" % loc)
    if body is None:
        return
    _expect(res, "clamp-active-flag",
            _has(body, r"RadioManagement_IsVswrFoldbackActive\s*\(\)"),
            "%s: does not consult RadioManagement_IsVswrFoldbackActive()"
            % loc)
    _expect(res, "clamp-tx",
            _has(body, r"ts\.txrx_mode\s*==\s*TRX_MODE_TX"),
            "%s: the foldback clamp is not restricted to TX" % loc)
    clamp = _find(body, r"if\s*\(\s*\(\s*ts\.txrx_mode\s*==\s*TRX_MODE_TX\s*\)"
                        r"\s*&&\s*RadioManagement_IsVswrFoldbackActive\s*\(\s*\)"
                        r"\s*\)")
    clamp_block = _block_after(body, clamp) if clamp is not None else None
    _expect(res, "clamp-level", clamp_block is not None and
            _has(clamp_block, r"effective_power_level\s*=\s*PA_LEVEL_MINIMAL\s*;"),
            "%s: while the foldback is active the user power level is not "
            "forced to PA_LEVEL_MINIMAL (A1 invariant: no full drive into a "
            "bad load)" % loc)
    _expect(res, "clamp-used",
            _has(body, r"ts\.pwr_adj\[effective_power_level\s*==\s*"
                       r"PA_LEVEL_FULL") and
            _has(body, r"switch\s*\(\s*effective_power_level\s*\)"),
            "%s: effective_power_level is computed but the pwr_adj lookup/"
            "level scaling still uses ts.power_level (the clamp has no "
            "effect)" % loc)
    _expect(res, "clamp-user-level",
            _has(body, r"ts\.power_level\s*=") is False,
            "%s: the clamp modifies ts.power_level, which is the user's "
            "persisted level - the A1 fix must only override the applied "
            "factor" % loc)

    body, line = _function(rm_text, "RadioManagement_SetMinimalTxPowerFactor")
    loc = _loc(RM_C, line, "RadioManagement_SetMinimalTxPowerFactor")
    _expect(res, "minimal-function", body is not None,
            "%s: RadioManagement_SetMinimalTxPowerFactor() is missing" % loc)
    if body is not None:
        _expect(res, "minimal-function-body",
                _has(body, r"ts\.power_level\s*=\s*PA_LEVEL_MINIMAL\s*;") and
                _has(body, r"RadioManagement_SetBandPowerFactor\s*\(") and
                _has(body, r"ts\.power_level\s*=\s*saved_level\s*;"),
                "%s: must set PA_LEVEL_MINIMAL, apply the power factor and "
                "restore the user level" % loc)
        _expect(res, "minimal-function-no-bias",
                _has(body, r"RadioManagement_SetPaBias\s*\(") is False,
                "%s: must not re-apply the PA bias (bias stays off on "
                "foldback)" % loc)

    body, line = _function(rm_text, "RadioManagement_IsVswrFoldbackActive")
    loc = _loc(RM_C, line, "RadioManagement_IsVswrFoldbackActive")
    _expect(res, "is-active", body is not None and
            _has(body, r"return\s+vswr_foldback_active\s*;"),
            "%s: RadioManagement_IsVswrFoldbackActive() must report "
            "vswr_foldback_active" % loc)
    _expect(res, "is-active-decl",
            _has(rm_h, r"bool\s+RadioManagement_IsVswrFoldbackActive\s*"
                       r"\(\s*void\s*\)\s*;"),
            "%s: RadioManagement_IsVswrFoldbackActive() is not declared"
            % RM_H)

    # PA_LEVEL_MINIMAL must be the minimum output level, not full power
    levels = _enum_values(board_h, "PA_LEVEL_FULL")
    match = re.search(r"#\s*define\s+PA_LEVEL_MINIMAL\s+(\w+)", rm_h)
    minimal = levels.get(match.group(1)) if match is not None else None
    _expect(res, "pa-level-minimal",
            match is not None and minimal is not None and
            levels.get("PA_LEVEL_FULL") == 0 and minimal > 0,
            "%s: PA_LEVEL_MINIMAL must alias a low PA_LEVEL_* (full drive is "
            "PA_LEVEL_FULL=0); levels=%r" % (RM_H, levels))


# ---------------------------------------------------------------------------
# (d) VSWR calculation: guard, clamps, cap
# ---------------------------------------------------------------------------


def _check_vswr_math(res, rm_text, ui_driver_h):
    min_power = _define_number(ui_driver_h, "SWR_MIN_CALC_POWER")
    _expect(res, "swr-min-power", min_power is not None and 0 < min_power < 10,
            "%s: SWR_MIN_CALC_POWER is missing/degenerate (%r)" %
            (UI_DRIVER_H, min_power))

    body, line = _function(rm_text, "RadioManagement_UpdatePowerAndVSWR")
    loc = _loc(RM_C, line, "RadioManagement_UpdatePowerAndVSWR")
    _expect(res, "math-function", body is not None,
            "%s: RadioManagement_UpdatePowerAndVSWR() is missing" % loc)
    if body is None:
        return
    guard = _find(body, r"if\s*\(\s*swrm\.fwd_pwr\s*>=\s*SWR_MIN_CALC_POWER"
                       r"\s*\)")
    guard_block = _block_after(body, guard) if guard is not None else None
    _expect(res, "math-guard", guard_block is not None,
            "%s: the VSWR calculation is no longer guarded by "
            "fwd_pwr >= SWR_MIN_CALC_POWER (division by ~0 -> NaN/Inf)"
            % loc)
    if guard_block is not None:
        _expect(res, "math-ratio",
                _has(guard_block, r"swrm\.rev_pwr\s*/\s*swrm\.fwd_pwr"),
                "%s: the guarded block does not compute the reflected/forward "
                "ratio" % loc)
        _expect(res, "math-nan",
                _has(guard_block, r"!\s*\(\s*vswr_ratio\s*>=\s*0\.0f\s*\)") and
                _has(guard_block, r"vswr_ratio\s*=\s*0\.0f\s*;"),
                "%s: no NaN/negative ratio clamp inside the guarded block "
                "(detector offset can make rev > fwd)" % loc)
        _expect(res, "math-ratio-clamp",
                _has(guard_block, r"if\s*\(\s*vswr_ratio\s*>\s*1\.0f\s*\)") and
                _has(guard_block, r"vswr_ratio\s*=\s*1\.0f\s*;"),
                "%s: reflection ratio is not clamped to 1.0 (negative "
                "denominator in the VSWR formula)" % loc)
        _expect(res, "math-cap",
                _has(guard_block, r"if\s*\(\s*vswr_ratio\s*>\s*0\.9801f\s*\)") and
                _has(guard_block, r"swrm\.vswr\s*=\s*VSWR_MAX_CALC_VALUE\s*;"),
                "%s: the (nearly) infinite mismatch cap VSWR_MAX_CALC_VALUE "
                "is missing (sqrt of 1.0 -> Inf)" % loc)
        formula = re.search(r"swrm\.vswr\s*=\s*\(\s*1\.0f\s*\+\s*vswr_refl\s*\)"
                            r"\s*/\s*\(\s*1\.0f\s*-\s*vswr_refl\s*\)", guard_block)
        _expect(res, "math-formula", formula is not None,
                "%s: VSWR formula (1+r)/(1-r) is missing/changed" % loc)
        _expect(res, "math-sqrt", _has(guard_block, r"sqrtf\s*\("),
                "%s: reflection coefficient is not derived with sqrtf()" % loc)

    # all swrm.vswr writes must be inside the guarded block
    writes_total = _count(body, r"swrm\.vswr\s*=")
    writes_guarded = _count(guard_block, r"swrm\.vswr\s*=") \
        if guard_block is not None else 0
    _expect(res, "math-guard-writes", writes_total == writes_guarded and
            writes_total > 0,
            "%s: %d of %d swrm.vswr writes are outside the fwd_pwr guard"
            % (loc, writes_total - writes_guarded, writes_total))

    # measurement settle tick: counted only while collecting samples
    samples = _find(body, r"if\s*\(\s*swrm\.p_curr\s*<\s*SWR_SAMPLES_CNT\s*\)")
    samples_block = _block_after(body, samples) if samples is not None else None
    _expect(res, "math-settle", samples_block is not None and
            _has(samples_block, r"vswr_settle_cnt\s*\+\+") and
            _has(samples_block, r"vswr_settle_cnt\s*<\s*255"),
            "%s: vswr_settle_cnt is not incremented (with a cap) per sample "
            "tick inside the SWR_SAMPLES_CNT branch" % loc)

    # the protection must run after a fresh measurement
    check_call = _find(body, r"RadioManagement_CheckVswrProtection\s*\(\s*\)")
    _expect(res, "math-check-call", check_call is not None,
            "%s: RadioManagement_CheckVswrProtection() is not called after "
            "the measurement (the protection never runs)" % loc)
    if check_call is not None and guard is not None:
        _expect(res, "math-check-order", check_call.start() > guard.start(),
                "%s: the protection runs before the fwd_pwr guard/measurement"
                % loc)
    if check_call is not None:
        ratio = _find(body, r"swrm\.rev_pwr\s*/\s*swrm\.fwd_pwr")
        _expect(res, "math-check-order2",
                ratio is not None and check_call.start() > ratio.start(),
                "%s: the protection runs before the VSWR is computed"
                % loc)


# ---------------------------------------------------------------------------
# (e) recovery only at minimal drive
# ---------------------------------------------------------------------------


def _check_state_machine(res, rm_text):
    body, line = _function(rm_text, "RadioManagement_CheckVswrProtection")
    loc = _loc(RM_C, line, "RadioManagement_CheckVswrProtection")
    _expect(res, "sm-function", body is not None,
            "%s: RadioManagement_CheckVswrProtection() is missing" % loc)
    if body is None:
        return

    # TX-only, never on a pending TX/RX transition
    tx_guard = _find(body, r"if\s*\(\s*ts\.txrx_mode\s*!=\s*TRX_MODE_TX\s*\)")
    tx_block = _block_after(body, tx_guard) if tx_guard is not None else None
    _expect(res, "sm-tx-only", tx_block is not None and
            _has(tx_block, r"return\s*;"),
            "%s: no early return for ts.txrx_mode != TRX_MODE_TX" % loc)
    transition = _find(body, r"if\s*\(\s*ts\.ptt_req\s*\|\|\s*"
                            r"ts\.tx_immediate_stop_req\s*\)")
    transition_block = _block_after(body, transition) \
        if transition is not None else None
    _expect(res, "sm-transition", transition_block is not None and
            _has(transition_block, r"return\s*;"),
            "%s: power/bias must not be switched on a pending TX/RX transition "
            "(ts.ptt_req || ts.tx_immediate_stop_req)" % loc)

    # protection disabled at run time -> release the PA, do not leave it muted
    disable = _find(body, r"if\s*\(\s*ts\.vswr_protection_threshold\s*<=\s*"
                          r"VSWR_PROTECTION_THRESHOLD_OFF\s*\)")
    disable_block = _block_after(body, disable) if disable is not None else None
    _expect(res, "sm-disable", disable_block is not None and
            _has(disable_block, r"RadioManagement_VswrFoldbackRelease\s*\(\s*\)"),
            "%s: switching the protection OFF at run time does not release an "
            "active foldback (PA left muted)" % loc)

    # own arm window, not the dynamic tune/display settle counter
    arm = _find(body, r"if\s*\(\s*vswr_settle_cnt\s*<\s*"
                      r"VSWR_PROTECTION_ARM_CNT\s*\)")
    arm_block = _block_after(body, arm) if arm is not None else None
    _expect(res, "sm-arm", arm_block is not None and
            _has(arm_block, r"return\s*;"),
            "%s: no arm window (vswr_settle_cnt < VSWR_PROTECTION_ARM_CNT)"
            % loc)
    _expect(res, "sm-no-dynamic-tune",
            _has(body, r"dynamic_tune") is False,
            "%s: the protection depends on the dynamic tune/display settle "
            "counter again (audit R5: a tuning action must not suppress the "
            "protection)" % loc)
    _expect(res, "sm-valid", _has(body, r"swrm\.fwd_pwr\s*>=\s*"
                                        r"SWR_MIN_CALC_POWER"),
            "%s: no fwd_pwr >= SWR_MIN_CALC_POWER validity check" % loc)

    # fast attack / slow decay damping
    _expect(res, "sm-damping",
            _has(body, r"vswr_foldback_avg\s*<\s*1\.0f") and
            _has(body, r"swrm\.vswr\s*>\s*vswr_foldback_avg") and
            _has(body, r"vswr_foldback_avg\s*\*\s*\(\s*1\s*-\s*"
                       r"VSWR_DAMPENING_FACTOR\s*\)"),
            "%s: the dampened VSWR integrator (fast attack / slow decay) is "
            "missing" % loc)

    # normal path: only a valid measurement may trip the protection
    normal = _find(body, r"if\s*\(\s*vswr_foldback_active\s*==\s*false\s*\)")
    normal_block = _block_after(body, normal) if normal is not None else None
    _expect(res, "sm-normal", normal_block is not None and
            _has(normal_block, r"if\s*\(\s*valid_vswr\s*==\s*false\s*\)") and
            _has(normal_block, r"vswr_foldback_avg\s*>\s*\(\s*float\s*\)\s*"
                              r"ts\.vswr_protection_threshold"),
            "%s: the normal path does not require a valid measurement before "
            "comparing with the threshold" % loc)

    # trip: PA muted AND drive minimal AND indication flag set
    trip = _find(body, r"if\s*\(\s*vswr_foldback_avg\s*>\s*\(\s*float\s*\)\s*"
                       r"ts\.vswr_protection_threshold\s*\)")
    trip_block = _block_after(body, trip) if trip is not None else None
    _expect(res, "sm-trip", trip_block is not None and
            _has(trip_block, r"RadioManagement_DisablePaBias\s*\(\s*\)") and
            _has(trip_block, r"RadioManagement_SetMinimalTxPowerFactor\s*\(") and
            _has(trip_block, r"swrm\.high_vswr_detected\s*=\s*true") and
            _has(trip_block, r"vswr_foldback_active\s*=\s*true"),
            "%s: the trip path must mute the PA bias, reduce the drive to the "
            "minimum, set swrm.high_vswr_detected and vswr_foldback_active"
            % loc)

    # foldback hold: minimal drive is re-applied on every measurement, so a
    # PWR change by the user cannot restore full drive while folded back
    minimal_calls = [m.start() for m in re.finditer(
        r"RadioManagement_SetMinimalTxPowerFactor\s*\(", body)]
    retry_block = _find(body, r"if\s*\(\s*vswr_foldback_trial\s*==\s*false\s*\)")
    _expect(res, "sm-hold-drive",
            len(minimal_calls) >= 3 and retry_block is not None and
            any(position < retry_block.start() and
                (trip is None or position > trip.start())
                for position in minimal_calls),
            "%s: while folded back the minimal drive is not re-applied on "
            "every measurement (a PWR/menu/CAT change could restore full "
            "drive)" % loc)

    # recovery trial: PA bias may only be re-enabled at MINIMAL drive
    bias_calls = [m.start() for m in re.finditer(
        r"RadioManagement_SetPaBias\s*\(\s*\)", body)]
    _expect(res, "sm-trial-bias-count", len(bias_calls) == 1,
            "%s: expected exactly one RadioManagement_SetPaBias() call (the "
            "recovery trial), found %d" % (loc, len(bias_calls)))
    trial_true = _find(body, r"vswr_foldback_trial\s*=\s*true\s*;")
    if bias_calls and trial_true is not None:
        after_true = [position for position in minimal_calls
                      if position > trial_true.start()]
        _expect(res, "sm-trial-order",
                bool(after_true) and after_true[0] < bias_calls[0] and
                bias_calls[0] > trial_true.start(),
                "%s: the PA bias is re-enabled for a recovery trial before the "
                "drive is forced to PA_LEVEL_MINIMAL (audit R3: no recovery "
                "burst at full power)" % loc)
    else:
        _expect(res, "sm-trial-order", False,
                "%s: recovery trial start (vswr_foldback_trial = true / "
                "SetPaBias) is missing" % loc)
    _expect(res, "sm-trial-retry",
            _has(body, r"if\s*\(\s*\+\+vswr_foldback_retry\s*<\s*"
                       r"VSWR_PROTECTION_RETRY_CNT\s*\)"),
            "%s: no retry counter on the trial start path" % loc)

    # recovery counting: only good measurements below threshold - hysteresis
    recovery = _find(body, r"if\s*\(\s*vswr_foldback_avg\s*<\s*\(\s*\(\s*float"
                          r"\s*\)\s*ts\.vswr_protection_threshold\s*-\s*"
                          r"VSWR_PROTECTION_HYSTERESIS\s*\)\s*\)")
    recovery_block = _block_after(body, recovery) if recovery is not None \
        else None
    _expect(res, "sm-recovery", recovery_block is not None and
            _has(recovery_block, r"\+\+vswr_foldback_recovery\s*>=\s*"
                                 r"VSWR_PROTECTION_RECOVERY_CNT") and
            _has(recovery_block,
                 r"RadioManagement_VswrFoldbackRelease\s*\(\s*\)"),
            "%s: the release is not gated by RECOVERY_CNT consecutive "
            "measurements below (threshold - hysteresis)" % loc)
    # inside the hysteresis band the trial must be aborted (PA muted again)
    if recovery is not None:
        tail = body[recovery.end():]
        _expect(res, "sm-hysteresis-band",
                _has(tail, r"RadioManagement_DisablePaBias\s*\(\s*\)") and
                _has(tail, r"vswr_foldback_trial\s*=\s*false") and
                _has(tail, r"vswr_foldback_recovery\s*=\s*0"),
                "%s: a reading between (threshold - hysteresis) and threshold "
                "does not abort the trial (PA bias would stay on)" % loc)

    # bad trial: mute again and retry later
    if trip is not None:
        second = re.search(r"if\s*\(\s*vswr_foldback_avg\s*>\s*\(\s*float\s*\)"
                           r"\s*ts\.vswr_protection_threshold\s*\)",
                           body[trip.end():])
        bad_block = _block_after(body[trip.end():], second) \
            if second is not None else None
        _expect(res, "sm-bad-trial", bad_block is not None and
                _has(bad_block, r"RadioManagement_DisablePaBias\s*\(\s*\)") and
                _has(bad_block, r"vswr_foldback_trial\s*=\s*false"),
                "%s: a recovery trial that still sees a bad VSWR does not mute "
                "the PA again" % loc)
    _expect(res, "sm-bias-off-hold",
            _count(body, r"RadioManagement_DisablePaBias\s*\(\s*\)") >= 3,
            "%s: too few RadioManagement_DisablePaBias() calls - the PA is "
            "not held muted while waiting for / aborting a trial" % loc)

    # release / reset helpers
    body, line = _function(rm_text, "RadioManagement_VswrFoldbackRelease")
    loc = _loc(RM_C, line, "RadioManagement_VswrFoldbackRelease")
    _expect(res, "sm-release", body is not None and
            _has(body, r"vswr_foldback_active\s*=\s*false") and
            _has(body, r"swrm\.high_vswr_detected\s*=\s*false") and
            _has(body, r"RadioManagement_SetBandPowerFactor\s*\(") and
            _has(body, r"if\s*\(\s*ts\.txrx_mode\s*==\s*TRX_MODE_TX\s*\)") and
            _has(body, r"RadioManagement_SetPaBias\s*\(\s*\)"),
            "%s: release must clear the foldback/indication, restore the user "
            "power factor and only re-enable the PA bias while transmitting"
            % loc)

    body, line = _function(rm_text, "RadioManagement_VswrFoldbackReset")
    loc = _loc(RM_C, line, "RadioManagement_VswrFoldbackReset")
    _expect(res, "sm-reset", body is not None and
            _has(body, r"vswr_foldback_active\s*=\s*false") and
            _has(body, r"vswr_settle_cnt\s*=\s*0") and
            _has(body, r"vswr_foldback_recovery\s*=\s*0") and
            _has(body, r"swrm\.high_vswr_detected\s*=\s*false"),
            "%s: reset must clear the foldback state, the settle counter, the "
            "recovery counter and the indication flag" % loc)

    switch_body, line = _function(rm_text, "RadioManagement_SwitchTxRx")
    loc = _loc(RM_C, line, "RadioManagement_SwitchTxRx")
    rx = _find(switch_body or "", r"if\s*\(\s*txrx_mode_final\s*==\s*"
                                   r"TRX_MODE_RX\s*\)")
    rx_block = _block_after(switch_body, rx) \
        if (rx is not None and switch_body is not None) else None
    _expect(res, "sm-reset-on-rx", switch_body is not None and
            rx_block is not None and
            _has(rx_block, r"RadioManagement_VswrFoldbackReset\s*\(\s*\)"),
            "%s: the foldback state is not reset when returning to RX (a new "
            "TX would start with stale protection state)" % loc)


# ---------------------------------------------------------------------------
# (f) "SWR!" indication
# ---------------------------------------------------------------------------


def _check_indication(res, driver_c):
    _expect(res, "indicator-decl",
            _has(driver_c, r"SWR_Indicator_Drawn"),
            "%s: the SWR_Indicator_Drawn state flag is missing" % UI_DRIVER_C)
    body, line = _function(driver_c, "UiDriver_HandleTXMeters")
    loc = _loc(UI_DRIVER_C, line, "UiDriver_HandleTXMeters")
    _expect(res, "indicator-function", body is not None,
            "%s: UiDriver_HandleTXMeters() is missing" % loc)
    if body is None:
        return
    gate = _find(body, r"if\s*\(\s*swrm\.high_vswr_detected\s*\)")
    _expect(res, "indicator-gate", gate is not None,
            "%s: the \"SWR!\" indication is not driven by "
            "swrm.high_vswr_detected" % loc)
    gate_block = _block_after(body, gate) if gate is not None else None
    _expect(res, "indicator-text",
            gate_block is not None and
            _has(gate_block, r"\"SWR!\""),
            "%s: the tripped protection does not show the \"SWR!\" warning"
            % loc)
    _expect(res, "indicator-print",
            _has(body, r"UiLcdHy28_PrintTextCentered\s*\([^;]*\"SWR!\"") and
            _has(body, r"\"SWR!\"\s*,\s*Red"),
            "%s: \"SWR!\" is not drawn in red in the power indicator area"
            % loc)
    _expect(res, "indicator-blink",
            _has(body, r"vswr_ind_cnt\s*>=\s*4") and
            _has(body, r"vswr_ind_phase\s*\^=\s*1"),
            "%s: the \"SWR!\" indication is not a controlled blink (phase "
            "toggle) - it could flicker or stay on" % loc)
    _expect(res, "indicator-restore",
            _has(body, r"else if\s*\(\s*vswr_ind_drawn\s*\)") and
            _has(body, r"UiDriver_DisplayPowerLevel\s*\(\s*\)"),
            "%s: the normal power level display is not restored when the "
            "foldback releases" % loc)
    _expect(res, "indicator-flag",
            _has(body, r"SWR_Indicator_Drawn\s*=\s*\(\s*vswr_ind_drawn\s*!=\s*0"
                       r"\s*\)"),
            "%s: SWR_Indicator_Drawn is not updated from the blink phase"
            % loc)
    smeter, smeter_line = _function(driver_c, "UiDriver_HandleSMeter")
    _expect(res, "indicator-rx-restore", smeter is not None and
            _has(smeter, r"if\s*\(\s*SWR_Indicator_Drawn\s*\)") and
            _has(smeter, r"UiDriver_DisplayPowerLevel\s*\(\s*\)"),
            "%s:%s: the RX screen does not restore the power level box after a "
            "TX \"SWR!\" indication" % (UI_DRIVER_C, smeter_line))


# ---------------------------------------------------------------------------
# (g) TX outside the amateur bands is blocked
# ---------------------------------------------------------------------------


def _check_tx_interlock(res, rm_text, board_h, menu_struct, menu_c):
    flags = None
    match = re.search(r"#\s*define\s+FLAGS1_TX_OUTSIDE_BANDS\s+(\S+)", board_h)
    if match is not None:
        flags = cparse.eval_int(match.group(1), {})
    _expect(res, "interlock-flag", flags == 0x2000,
            "%s: FLAGS1_TX_OUTSIDE_BANDS is missing or no longer 0x2000 (%r)"
            % (BOARD_H, flags))

    body, line = _function(rm_text, "RadioManagement_ValidateFrequencyForTX")
    loc = _loc(RM_C, line, "RadioManagement_ValidateFrequencyForTX")
    _expect(res, "interlock-function", body is not None,
            "%s: RadioManagement_ValidateFrequencyForTX() is missing" % loc)
    if body is not None:
        block = _find(body, r"if\s*\(\s*\(\s*RadioManagement_GetBand\s*\(\s*"
                            r"dial_freq\s*\)\s*>=\s*MAX_BANDS\s*\)\s*&&\s*"
                            r"\(\s*\(\s*ts\.flags1\s*&\s*"
                            r"FLAGS1_TX_OUTSIDE_BANDS\s*\)\s*==\s*0\s*\)\s*\)")
        block_body = _block_after(body, block) if block is not None else None
        _expect(res, "interlock-block", block_body is not None and
                _has(block_body, r"return\s+OSC_TUNE_IMPOSSIBLE\s*;"),
                "%s: out-of-band frequencies are not refused with "
                "OSC_TUNE_IMPOSSIBLE when FLAGS1_TX_OUTSIDE_BANDS is clear"
                % loc)
        _expect(res, "interlock-tunable",
                _has(body, r"prepareNextFrequency\s*\("),
                "%s: the frequency is no longer checked for tunability" % loc)

    body, line = _function(rm_text, "RadioManagement_SwitchTxRx")
    loc = _loc(RM_C, line, "RadioManagement_SwitchTxRx")
    _expect(res, "interlock-switchtx", body is not None and
            _has(body, r"RadioManagement_ValidateFrequencyForTX\s*\(") and
            _has(body, r"!=\s*OSC_TUNE_IMPOSSIBLE") and
            _has(body, r"txrx_mode_final\s*=\s*tx_ok\s*\?") and
            _has(body, r"txrx_mode_final\s*!=\s*ts\.txrx_mode"),
            "%s: the main TX switch no longer gates the mode on the "
            "RadioManagement_ValidateFrequencyForTX() result" % loc)

    body, line = _function(rm_text, "RadioManagement_SetBandPowerFactor")
    loc = _loc(RM_C, line, "RadioManagement_SetBandPowerFactor")
    if body is not None:
        oob = _find(body, r"if\s*\(\s*band\s*>=\s*MAX_BANDS\s*\)")
        oob_block = _block_after(body, oob) if oob is not None else None
        _expect(res, "interlock-drive-zero", oob_block is not None and
                _has(oob_block, r"\(\s*ts\.flags1\s*&\s*FLAGS1_TX_OUTSIDE_BANDS"
                                r"\s*\)\s*==\s*0") and
                _has(oob_block, r"pf_bandvalue\s*=\s*0\s*;"),
                "%s: out-of-band TX with FLAGS1_TX_OUTSIDE_BANDS clear does not "
                "force the PA drive to zero (second barrier)" % loc)

    _expect(res, "interlock-menu",
            _has(menu_struct,
                 r"\{\s*MENU_CONF\s*,\s*MENU_ITEM\s*,\s*CONFIG_TX_OUT_ENABLE"
                 r"\s*,\s*NULL\s*,\s*\"TX on Out-Of-Band Freq\.\""),
            "%s: the CONFIG_TX_OUT_ENABLE menu item is not registered (the "
            "out-of-band TX permission could not be enabled at all)"
            % UI_MENU_STRUCT)
    case = re.search(r"case\s+CONFIG_TX_OUT_ENABLE\s*:", menu_c)
    handler = ""
    if case is not None:
        end = re.search(r"\b(break|case\s+\w+|default)\s*:", menu_c[case.end():])
        handler = menu_c[case.end():case.end() + end.start()] if end else \
            menu_c[case.end():]
    _expect(res, "interlock-menu-handler", case is not None and
            _has(handler, r"FLAGS1_TX_OUTSIDE_BANDS"),
            "%s: the CONFIG_TX_OUT_ENABLE handler does not toggle "
            "FLAGS1_TX_OUTSIDE_BANDS" % UI_MENU_C)

    # Tune keys the transmitter through SwitchTxRx, so the interlock covers it
    tbody, tline = _function(rm_text, "RadioManagement_Tune")
    _expect(res, "interlock-tune", tbody is not None and
            _has(tbody, r"RadioManagement_SwitchTxRx\s*\("),
            "%s:%r: Tune does not go through the interlocked SwitchTxRx()"
            % (RM_C, tline))


# ---------------------------------------------------------------------------
# run
# ---------------------------------------------------------------------------


def run(repo):
    res = Results(CHECK)
    missing = [rel for rel in SOURCES
               if not os.path.isfile(os.path.join(repo, rel))]
    for rel in missing:
        res.fail("missing", "required file not found: %s" % rel)
    if missing:
        return res

    rm_c = _clean(os.path.join(repo, RM_C))
    rm_h_text = cparse.strip_comments(cparse.read_text(os.path.join(repo, RM_H)))
    driver_c = _clean(os.path.join(repo, UI_DRIVER_C))
    driver_h = cparse.strip_comments(
        cparse.read_text(os.path.join(repo, UI_DRIVER_H)))
    conf_c = _clean(os.path.join(repo, UI_CONF_C))
    conf_h = cparse.strip_comments(
        cparse.read_text(os.path.join(repo, UI_CONF_H)))
    menu_c = _clean(os.path.join(repo, UI_MENU_C))
    menu_struct = _clean(os.path.join(repo, UI_MENU_STRUCT))
    menu_int = cparse.strip_comments(
        cparse.read_text(os.path.join(repo, UI_MENU_INT)))
    board_h = cparse.strip_comments(
        cparse.read_text(os.path.join(repo, BOARD_H)))

    header_defs = cparse.parse_defines(rm_h_text)
    header_defs.update(cparse.parse_defines(conf_h))
    header_defs.update(cparse.parse_defines(board_h))

    _check_constants(res, rm_c)
    _check_threshold_config(res, conf_h, conf_c, board_h, menu_struct,
                            menu_int, menu_c, header_defs)
    _check_power_factor_clamp(res, rm_c, rm_h_text, board_h)
    _check_vswr_math(res, rm_c, driver_h)
    _check_state_machine(res, rm_c)
    _check_indication(res, driver_c)
    _check_tx_interlock(res, rm_c, board_h, menu_struct, menu_c)
    return res

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_filters.py - host-side invariants of the UHSDR/Nizar DSP filter tables.

Covers audit task P2 item 3(a)+(b): every filter driver in
mchf-eclipse/drivers/audio/filters/*.c is parsed and checked without a compiler:

  * arm_fir_instance_f32            : numTaps == len(pCoeffs), numTaps > 0
  * arm_fir_decimate_instance_f32   : numTaps == len(pCoeffs), numTaps > 0,
                                      M (decimation) sane when resolvable
  * arm_fir_interpolate_instance_f32: phaseLength == len(pCoeffs), > 0
  * arm_iir_lattice_instance_f32    : numStages == len(pkCoeffs),
                                      len(pvCoeffs) == numStages + 1,
                                      inline pState (if present) == 2*numStages
  * arrays of the above (e.g. FirZoomFFTDecimate[]) : validated element by
    element, count must match the "extern ...[N]" declaration
  * inline / named coefficient tables: all values finite (no nan/inf), no
    float32 overflow, not an all-zero table; int tables must fit their type
  * when a descriptor points at a named array, the array is resolved and its
    size must agree with numTaps/phaseLength

The checks are intentionally conservative: anything that cannot be resolved is
reported (so a removed #define does not silently disable a check) but is not
counted as a DSP defect.
"""

from __future__ import annotations

import glob
import math
import os
import re
import struct

import cparse
import known_issues
from checkbase import Results

CHECK = "filters"

FIR_TYPES = ("arm_fir_instance_f32", "arm_fir_decimate_instance_f32",
             "arm_fir_interpolate_instance_f32")
IIR_TYPES = ("arm_iir_lattice_instance_f32",)
BIQUAD_TYPES = ("arm_biquad_casd_df1_inst_f32",)
DESCRIPTOR_TYPES = FIR_TYPES + IIR_TYPES + BIQUAD_TYPES

_FLOAT_TYPES = {"float", "float32_t", "double"}
_INT_RANGES = {
    "int8_t": (-128, 127), "uint8_t": (0, 255),
    "int16_t": (-32768, 32767), "uint16_t": (0, 65535),
    "int32_t": (-2147483648, 2147483647), "uint32_t": (0, 4294967295),
    "q15_t": (-32768, 32767), "q31_t": (-2147483648, 2147483647),
    "q7_t": (-128, 127),
}

_EXTERN_ARRAY_RE = re.compile(
    r"extern\s+const\s+(?P<type>[\w\s]+?)\s+(?P<name>[A-Za-z_]\w*)\s*"
    r"\[\s*(?P<dim>[^\]]*)\]\s*;")


def _clean(path):
    raw = cparse.read_text(path)
    return cparse.apply_preprocessor(cparse.strip_comments(raw))


def _defines_of(path):
    text = _clean(path)
    return cparse.parse_defines(text)


def _extern_arrays(path):
    """name -> (type, dim_expr, lineno) from filters.h extern declarations."""
    text = _clean(path)
    result = {}
    for match in _EXTERN_ARRAY_RE.finditer(text):
        result[match.group("name")] = (
            match.group("type").strip(), match.group("dim").strip(),
            cparse.line_of(text, match.start()))
    return result


def _float32_ok(value):
    try:
        struct.pack("<f", value)
        return True
    except (OverflowError, struct.error):
        return False


def check_float_values(res, key, loc, values, unresolved, what, allow_all_zero=False):
    """Common coefficient-table sanity checks."""
    if unresolved:
        res.fail(key, "%s: unresolved token(s) in %s: %s"
                 % (loc, what, ", ".join(unresolved[:4])))
        return
    if not values:
        res.fail(key, "%s: %s is empty" % (loc, what))
        return
    bad = [v for v in values if not math.isfinite(v)]
    if bad:
        res.fail(key, "%s: %s contains nan/inf (%d values)" % (loc, what, len(bad)))
        return
    too_big = [v for v in values if not _float32_ok(v)]
    if too_big:
        res.fail(key, "%s: %s has %d value(s) that overflow float32"
                 % (loc, what, len(too_big)))
        return
    if not allow_all_zero and all(v == 0.0 for v in values):
        res.fail(key, "%s: %s is all zeros" % (loc, what))
        return
    res.ok()


def _array_values(res, key, loc, body, what, type_name):
    """Validate one brace-initialised table (float or integer)."""
    if type_name in _FLOAT_TYPES:
        values, unresolved = cparse.parse_float_tokens(body)
        check_float_values(res, key, loc, values, unresolved, what)
        return
    low, high = _INT_RANGES.get(type_name, (None, None))
    values, unresolved = cparse.parse_float_tokens(body)
    if unresolved:
        res.fail(key, "%s: unresolved token(s) in %s: %s"
                 % (loc, what, ", ".join(unresolved[:4])))
        return
    if low is not None:
        out_of_range = [v for v in values
                        if not float(v).is_integer() or not (low <= v <= high)]
        if out_of_range:
            res.fail(key, "%s: %s: %d value(s) do not fit %s (e.g. %r)"
                     % (loc, what, len(out_of_range), type_name,
                        out_of_range[0]))
            return
    res.ok()


class _Context:
    """Per-run lookups: named arrays and extern declarations."""

    def __init__(self, repo):
        self.repo = repo
        self.filters_dir = os.path.join(
            repo, "mchf-eclipse", "drivers", "audio", "filters")
        self.header_defines = {}
        for path in sorted(glob.glob(os.path.join(self.filters_dir, "*.h"))):
            self.header_defines.update(_defines_of(path))
        self.extern_arrays = {}
        filters_h = os.path.join(self.filters_dir, "filters.h")
        if os.path.isfile(filters_h):
            self.extern_arrays = _extern_arrays(filters_h)
        self.sources = sorted(glob.glob(os.path.join(self.filters_dir, "*.c")))
        self.arrays = {}        # symbol -> (path, dim, count, line)
        self.descriptors = {}   # symbol -> (path, type, numStages/numTaps, line)
        self._scan()

    def _scan(self):
        for path in self.sources:
            text = _clean(path)
            defines = dict(self.header_defines)
            defines.update(cparse.parse_defines(text))
            for arr in cparse.find_array_definitions(text):
                dim = cparse.eval_int(arr["dim"], defines)
                count = cparse.count_elements(arr["body"])
                self.arrays.setdefault(arr["name"], (
                    path, dim, count, cparse.line_of(text, arr["offset"])))
            for init in cparse.find_initializers(text, DESCRIPTOR_TYPES):
                fields = cparse.parse_designated(init["body"])
                if fields is None:
                    continue
                if init["type"] in FIR_TYPES:
                    num = cparse.eval_int(
                        fields.get("numTaps", fields.get("phaseLength", "")),
                        defines)
                elif init["type"] in IIR_TYPES:
                    num = cparse.eval_int(fields.get("numStages", ""), defines)
                else:
                    num = cparse.eval_int(fields.get("numStages", ""), defines)
                self.descriptors.setdefault(init["name"], (
                    path, init["type"], num,
                    cparse.line_of(text, init["offset"])))

    def array_size(self, name):
        entry = self.arrays.get(name)
        if not entry:
            return None
        _path, dim, count, _line = entry
        return dim if dim is not None else count


def _resolve_descriptor_number(res, key, loc, what, fields, defines):
    raw = fields.get("numTaps", fields.get("phaseLength", fields.get("numStages", "")))
    value = cparse.eval_int(raw, defines)
    if value is None:
        res.fail(key, "%s: cannot resolve %s (%r)" % (loc, what, raw.strip()))
        return None
    return value


def _check_pointer_table(res, key, loc, what, fields, field, ctx, expected):
    """If a pointer field names a table, its size must match expected."""
    raw = fields.get(field, "").strip()
    if not raw:
        res.fail(key, "%s: missing .%s" % (loc, field))
        return
    target = cparse.pointer_target(raw)
    if target is None:
        return          # compound literal / NULL handled by the caller
    size = ctx.array_size(target)
    if size is None:
        res.fail(key, "%s: .%s points at unknown table '%s'"
                 % (loc, field, target))
        return
    if expected is not None and size != expected:
        res.fail(key, "%s: .%s -> %s has %d elements, expected %d"
                 % (loc, field, target, size, expected))
        return
    res.ok()


def _validate_descriptor(res, path, text, init, defines, ctx, loc_suffix=""):
    """Validate one FIR / IIR lattice / biquad descriptor initializer."""
    name = init["name"]
    is_array = init.get("is_array")
    basename = os.path.basename(path)
    key = "%s:%s" % (basename, name)
    loc = "%s:%d" % (basename, cparse.line_of(text, init["offset"]))
    fields = cparse.parse_designated(init["body"])

    if fields is None:
        # array of descriptors: descend into each { ... } element
        elements = [chunk.strip() for chunk in cparse.split_top_level(init["body"])
                    if chunk.strip()]
        good = 0
        for index, chunk in enumerate(elements):
            if not chunk.startswith("{"):
                res.fail(key, "%s: descriptor array element %d is not a struct "
                               "initializer: %r" % (loc, index, chunk[:40]))
                continue
            try:
                body, end = cparse.brace_body(chunk, 0)
            except ValueError:
                res.fail(key, "%s: unbalanced braces in array element %d"
                         % (loc, index))
                continue
            element = {"type": init["type"], "name": "%s[%d]" % (name, index),
                       "is_array": False,
                       "offset": init["offset"] + 1,
                       "body": body, "end": init["offset"] + end}
            if _validate_descriptor(res, path, text, element, defines, ctx):
                good += 1
        expected = None
        extern = ctx.extern_arrays.get(name)
        if extern:
            expected = cparse.eval_int(extern[1], ctx.header_defines)
        if expected is None:
            res.fail(key, "%s: descriptor array has no resolvable extern "
                           "dimension in filters.h" % loc)
        elif expected != len(elements):
            res.fail(key, "%s: descriptor array has %d elements, filters.h "
                           "declares [%d]" % (loc, len(elements), expected))
        elif good == len(elements):
            res.ok()
        return good == len(elements)

    descriptor_type = init["type"]

    if descriptor_type in FIR_TYPES:
        num = _resolve_descriptor_number(res, key, loc, "numTaps", fields, defines)
        if num is None or num <= 0:
            if num is not None:
                res.fail(key, "%s: numTaps must be > 0 (got %d)" % (loc, num))
            return False
        body = cparse.compound_literal_body(fields.get("pCoeffs", ""))
        count = None
        if body is not None:
            values, unresolved = cparse.parse_float_tokens(body)
            check_float_values(res, key, loc, values, unresolved, "pCoeffs")
            count = len(values) + len(unresolved)
        _check_pointer_table(res, key, loc, "FIR", fields, "pCoeffs", ctx, num)
        if count is not None and count != num:
            res.fail(key, "%s: numTaps=%d but pCoeffs has %d elements"
                     % (loc, num, count))
            return False
        if descriptor_type == "arm_fir_decimate_instance_f32":
            m = cparse.eval_int(fields.get("M", ""), defines)
            if m is not None and (m <= 0 or m > num):
                res.fail(key, "%s: decimation factor M=%d is not in 1..numTaps"
                         % (loc, m))
                return False
        pstate = cparse.compound_literal_body(fields.get("pState", ""))
        if pstate is not None:
            n_state = cparse.count_elements(pstate)
            if n_state < num:
                res.fail(key, "%s: inline pState is too short for numTaps=%d "
                               "(%d floats)" % (loc, num, n_state))
                return False
        res.ok()
        return True

    if descriptor_type in IIR_TYPES:
        num = _resolve_descriptor_number(res, key, loc, "numStages", fields, defines)
        if num is None or num <= 0:
            if num is not None:
                res.fail(key, "%s: numStages must be > 0 (got %d)" % (loc, num))
            return False
        ok = True
        for field, expected in (("pkCoeffs", num), ("pvCoeffs", num + 1)):
            body = cparse.compound_literal_body(fields.get(field, ""))
            if body is None:
                if cparse.pointer_target(fields.get(field, "")) is not None:
                    _check_pointer_table(res, key, loc, "IIR", fields, field,
                                         ctx, expected)
                else:
                    res.fail(key, "%s: .%s is neither a table literal nor a "
                                   "named array" % (loc, field))
                    ok = False
                continue
            values, unresolved = cparse.parse_float_tokens(body)
            check_float_values(res, key, loc, values, unresolved, field,
                               allow_all_zero=False)
            if len(values) + len(unresolved) != expected:
                reason = known_issues.KNOWN_ISSUES.get((CHECK, key))
                message = ("%s: numStages=%d but %s has %d elements (expected %d)"
                           % (loc, num, field, len(values) + len(unresolved),
                              expected))
                if reason is not None and field == "pkCoeffs":
                    res.known_issue(key, message, reason)
                else:
                    res.fail(key, message)
                ok = False
        pstate = cparse.compound_literal_body(fields.get("pState", ""))
        if pstate is not None:
            n_state = cparse.count_elements(pstate)
            if n_state != 2 * num:
                res.fail(key, "%s: inline pState has %d elements, IIR lattice "
                               "needs 2*numStages=%d" % (loc, n_state, 2 * num))
                ok = False
            else:
                res.ok()
        if ok:
            res.ok()
        return ok

    if descriptor_type in BIQUAD_TYPES:
        num = _resolve_descriptor_number(res, key, loc, "numStages", fields, defines)
        if num is None or num <= 0:
            if num is not None:
                res.fail(key, "%s: numStages must be > 0 (got %d)" % (loc, num))
            return False
        ok = True
        for field, expected, what in (("pCoeffs", 5 * num, "pCoeffs"),
                                      ("pState", 4 * num, "pState")):
            body = cparse.compound_literal_body(fields.get(field, ""))
            if body is None:
                continue
            count = cparse.count_elements(body)
            if count != expected:
                message = ("%s: numStages=%d but %s has %d elements (expected %d)"
                           % (loc, num, what, count, expected))
                reason = known_issues.KNOWN_ISSUES.get((CHECK, key))
                if reason is not None and what == "pCoeffs":
                    res.known_issue(key, message, reason)
                else:
                    res.fail(key, message)
                ok = False
            else:
                res.ok()
        if ok:
            res.ok()
        return ok

    return True


def run(repo):
    res = Results(CHECK)
    ctx = _Context(repo)
    if not ctx.sources:
        res.fail("filters-dir", "no filter drivers found under %s"
                 % ctx.filters_dir)
        return res
    res.ok()        # filter directory present

    # --- descriptors ------------------------------------------------------
    for path in ctx.sources:
        text = _clean(path)
        defines = dict(ctx.header_defines)
        defines.update(cparse.parse_defines(text))
        for init in cparse.find_initializers(text, DESCRIPTOR_TYPES):
            _validate_descriptor(res, path, text, init, defines, ctx)

    # --- tables (named arrays / compound literals) -------------------------
    for path in ctx.sources:
        text = _clean(path)
        defines = dict(ctx.header_defines)
        defines.update(cparse.parse_defines(text))
        basename = os.path.basename(path)
        # extern declarations from filters.h must agree with definitions
        for arr in cparse.find_array_definitions(text):
            name = arr["name"]
            loc = "%s:%d" % (basename, cparse.line_of(text, arr["offset"]))
            key = "%s:%s" % (basename, name)
            dim = cparse.eval_int(arr["dim"], defines)
            count = cparse.count_elements(arr["body"])
            type_name = "float32_t"
            # recover the declared type from the source text
            prefix = text[max(0, arr["offset"] - 160):arr["offset"] + 80]
            m = re.search(r"(float32_t|float|double|int8_t|uint8_t|int16_t|"
                          r"uint16_t|int32_t|uint32_t|q15_t|q31_t|q7_t)\s+$" , prefix)
            if m:
                type_name = m.group(1)
            if dim is None:
                res.fail(key, "%s: cannot resolve array dimension %r"
                         % (loc, arr["dim"]))
            elif dim != count:
                res.fail(key, "%s: declared [%s] = %d but initializer has %d "
                               "elements" % (loc, arr["dim"], dim, count))
            else:
                res.ok()
            extern = ctx.extern_arrays.get(name)
            if extern:
                extern_dim = cparse.eval_int(extern[1], ctx.header_defines)
                if extern_dim is not None and dim is not None \
                        and extern_dim != dim:
                    res.fail(key, "%s: definition [%d] disagrees with filters.h"
                                   " extern [%d] (line %d)"
                             % (loc, dim, extern_dim, extern[2]))
                else:
                    res.ok()
            _array_values(res, key, loc, arr["body"], name, type_name)

    # --- stale known-issue bookkeeping ------------------------------------
    for (check_name, key), _reason in sorted(known_issues.KNOWN_ISSUES.items()):
        if check_name != CHECK:
            continue
        if not any(code == key for code, _m, _r in res.known):
            res.skip("stale-known-issue", 
                     "%s no longer reproduces; remove it from known_issues.py"
                     % key)
    return res

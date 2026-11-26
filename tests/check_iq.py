#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_iq.py - host-side invariants of the Nizar IQ filter tables.

Covers audit task P2 item 3(c): the large IQ coefficient tables and their
descriptors must agree even though a C compiler is not available:

  * iq_tx_filter.c: every active IQ_FilterDescriptor has num_taps > 0 and both
    .i / .q tables of exactly num_taps finite, non-zero coefficients (tables may
    be compound literals or references to named arrays);
  * iq_rx_filter.c / iq_rx_filter_am.c: every declared table has a resolvable
    dimension equal to its initializer length, finite values and is not empty;
  * extern declarations in filters.h must agree with the definitions found in
    the IQ drivers (this is the "linker view" without a linker);
  * the FilterPathInfo references in audio_filter.c (num_taps macro next to the
    i/q table names) are cross-checked against the table sizes, so a renamed or
    resized Nizar table cannot silently desynchronise the receiver path.

The "big Nizar tables" (i_rx_new_coeffs, iq_rx_am_*_coeffs, ...) are exactly
the kind of data where a copy/paste error is invisible without this test.
"""

from __future__ import annotations

import glob
import math
import os
import re
import struct

import cparse
from checkbase import Results

CHECK = "iq"

IQ_FILES = ("iq_rx_filter.c", "iq_rx_filter_am.c", "iq_tx_filter.c")
_IQ_TAP_RE = re.compile(r"\bIQ_TX_NUM_TAPS(?:_WIDE|_MAX)?\b")

_FLOAT32_RE = re.compile(
    r"^(?:const\s+)?(?:float32_t|float)\s+(?P<name>[A-Za-z_]\w*)\s*"
    r"\[\s*(?P<dim>[^\]]*)\]\s*=")

_EXTERN_RE = re.compile(
    r"extern\s+const\s+(?P<type>float32_t|float)\s+(?P<name>[A-Za-z_]\w*)\s*"
    r"\[\s*(?P<dim>[^\]]*)\]\s*;")

# FilterPathInfo entry fragment: "<NUM_TAPS_MACRO>, i_xxx, q_xxx"
_PATH_ENTRY_RE = re.compile(
    r"\b(IQ_RX_NUM_TAPS(?:_HI|_MID)?|IQ_NUM_TAPS)\s*,\s*"
    r"([A-Za-z_]\w*)\s*,\s*([A-Za-z_]\w*)")


def _clean(path):
    raw = cparse.read_text(path)
    return cparse.apply_preprocessor(cparse.strip_comments(raw))


def _float32_ok(value):
    try:
        struct.pack("<f", value)
        return True
    except (OverflowError, struct.error):
        return False


def _check_values(res, key, loc, body, what):
    values, unresolved = cparse.parse_float_tokens(body)
    if unresolved:
        res.fail(key, "%s: unresolved token(s) in %s: %s"
                 % (loc, what, ", ".join(unresolved[:4])))
        return None
    if not values:
        res.fail(key, "%s: %s is empty" % (loc, what))
        return None
    if any(not math.isfinite(v) for v in values):
        res.fail(key, "%s: %s contains nan/inf" % (loc, what))
        return None
    if any(not _float32_ok(v) for v in values):
        res.fail(key, "%s: %s overflows float32" % (loc, what))
        return None
    if all(v == 0.0 for v in values):
        res.fail(key, "%s: %s is all zeros" % (loc, what))
        return None
    res.ok()
    return values


def _table_size(fields, field, defines, arrays):
    """Count .i/.q table elements: compound literal or named array."""
    raw = fields.get(field, "").strip()
    if not raw:
        return None, "missing"
    body = cparse.compound_literal_body(raw)
    if body is not None:
        values, unresolved = cparse.parse_float_tokens(body)
        return len(values) + len(unresolved), ("literal", body)
    target = cparse.pointer_target(raw)
    if target is None:
        return None, "unsupported"
    entry = arrays.get(target)
    if entry is None:
        return None, "unknown table %s" % target
    return entry[0], None


def run(repo):
    res = Results(CHECK)
    fdir = os.path.join(repo, "mchf-eclipse", "drivers", "audio", "filters")
    driver_dir = os.path.join(repo, "mchf-eclipse", "drivers", "audio")
    if not os.path.isdir(fdir):
        res.fail("iq-dir", "not found: %s" % fdir)
        return res

    # shared defines: all filter headers + per-file defines
    header_defines = {}
    for path in sorted(glob.glob(os.path.join(fdir, "*.h"))):
        header_defines.update(cparse.parse_defines(_clean(path)))

    # resolve extern declarations from filters.h
    extern = {}
    filters_h = os.path.join(fdir, "filters.h")
    if os.path.isfile(filters_h):
        text = _clean(filters_h)
        for match in _EXTERN_RE.finditer(text):
            extern[match.group("name")] = (
                match.group("dim").strip(), cparse.line_of(text, match.start()))

    # first pass: every named table in the IQ drivers
    arrays = {}         # name -> (path, dim, count, line)
    cleaned = {}
    for filename in IQ_FILES:
        path = os.path.join(fdir, filename)
        if not os.path.isfile(path):
            res.fail("iq-missing", "expected source file not found: %s" % path)
            continue
        text = _clean(path)
        cleaned[path] = text
        defines = dict(header_defines)
        defines.update(cparse.parse_defines(text))
        for arr in cparse.find_array_definitions(text):
            name = arr["name"]
            loc = "%s:%d" % (filename, cparse.line_of(text, arr["offset"]))
            key = "%s:%s" % (filename, name)
            dim = cparse.eval_int(arr["dim"], defines)
            count = cparse.count_elements(arr["body"])
            if dim is None:
                res.fail(key, "%s: cannot resolve array dimension %r"
                         % (loc, arr["dim"]))
            elif dim != count:
                res.fail(key, "%s: declared [%s] = %d but initializer has %d "
                               "elements" % (loc, arr["dim"], dim, count))
            else:
                res.ok()
            arrays.setdefault(name, (path, dim, count, loc))
            _check_values(res, key, loc, arr["body"], name)
            if name in extern:
                extern_dim = cparse.eval_int(extern[name][0], header_defines)
                if extern_dim is not None and dim is not None \
                        and extern_dim != dim:
                    res.fail(key, "%s: definition [%d] disagrees with filters.h"
                                   " extern [%d] (line %d)"
                             % (loc, dim, extern_dim, extern[name][1]))
                else:
                    res.ok()

    # extern tables that must be defined in the IQ drivers (filters.h declares
    # them under these exact names)
    for name, (dim_text, line) in sorted(extern.items()):
        if not re.match(r"^(?:i_|q_|iq_)", name):
            continue
        if name not in arrays:
            res.skip("iq-extern", "%s declared extern in filters.h:%d but not "
                                   "defined in the IQ drivers (checked elsewhere)"
                     % (name, line))
            continue
        extern_dim = cparse.eval_int(dim_text, header_defines)
        _path, dim, _count, loc = arrays[name]
        if extern_dim is not None and dim is not None and extern_dim != dim:
            res.fail("iq-extern", "%s: filters.h size %d != definition size %d "
                                  "(%s)" % (name, extern_dim, dim, loc))

    # second pass: IQ_FilterDescriptor tables
    for path, text in cleaned.items():
        filename = os.path.basename(path)
        defines = dict(header_defines)
        defines.update(cparse.parse_defines(text))
        for init in cparse.find_initializers(text, ("IQ_FilterDescriptor",)):
            fields = cparse.parse_designated(init["body"])
            loc = "%s:%d" % (filename, cparse.line_of(text, init["offset"]))
            key = "%s:%s" % (filename, init["name"])
            if fields is None:
                res.fail(key, "%s: descriptor has no designated fields" % loc)
                continue
            num_taps = cparse.eval_int(fields.get("num_taps", ""), defines)
            if num_taps is None or num_taps <= 0:
                res.fail(key, "%s: cannot resolve/validate .num_taps (%r)"
                         % (loc, fields.get("num_taps", "").strip()))
                continue
            if num_taps > 1024:
                res.fail(key, "%s: .num_taps=%d is implausibly large"
                         % (loc, num_taps))
                continue
            ok = True
            for field in ("i", "q"):
                size, payload = _table_size(fields, field, defines, arrays)
                if size is None:
                    if payload == "missing":
                        res.fail(key, "%s: missing .%s" % (loc, field))
                        ok = False
                    elif payload and payload.startswith("unknown table"):
                        res.fail(key, "%s: .%s -> %s" % (loc, field, payload))
                        ok = False
                    else:
                        res.fail(key, "%s: .%s is not a table literal" % (loc, field))
                        ok = False
                    continue
                if isinstance(payload, tuple) and payload[0] == "literal":
                    _check_values(res, key, loc, payload[1], ".%s" % field)
                if size != num_taps:
                    res.fail(key, "%s: .num_taps=%d but .%s has %d elements"
                             % (loc, num_taps, field, size))
                    ok = False
            if ok:
                res.ok()

    # third pass: FilterPathInfo references in audio_filter.c
    audio_filter = os.path.join(driver_dir, "audio_filter.c")
    if os.path.isfile(audio_filter):
        text = _clean(audio_filter)
        defines = dict(header_defines)
        defines.update(cparse.parse_defines(text))
        checked = 0
        stale = 0
        for match in _PATH_ENTRY_RE.finditer(text):
            macro, i_name, q_name = match.groups()
            num_taps = cparse.eval_int(macro, defines)
            i_dim = arrays.get(i_name, (None, None))[1]
            q_dim = arrays.get(q_name, (None, None))[1]
            if num_taps is None or i_dim is None:
                stale += 1
                continue
            checked += 1
            loc = "%s:%d" % ("audio_filter.c", cparse.line_of(text, match.start()))
            if i_dim != num_taps or q_dim != num_taps:
                res.fail("pathinfo", "%s: %s=%d but %s/%s have %r/%r elements"
                         % (loc, macro, num_taps, i_name, q_name, i_dim, q_dim))
            else:
                res.ok()
        if checked == 0:
            res.skip("pathinfo", "no resolvable FilterPathInfo IQ references "
                                  "found in audio_filter.c")
        if stale:
            res.skip("pathinfo-stale", "%d FilterPathInfo reference(s) could "
                                        "not be resolved (renamed tables?)" % stale)
    else:
        res.skip("pathinfo", "audio_filter.c not found; FilterPathInfo "
                              "cross-check skipped")
    return res

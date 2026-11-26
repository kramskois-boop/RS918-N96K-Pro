#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_audio.py - static filter instances in audio_driver.c (bonus, host-only).

Audit P2 item 3(a)/(b) focuses on drivers/audio/filters/*.c, but the DSP core
also initialises static cascaded-biquad instances in audio_driver.c.  Those are
plain C initializers as well, and two of them are known to be placeholders, so
checking them here catches a whole class of "wrong number of coefficient/
state words" defects that only the ARM CMSIS code would otherwise trip over at
runtime (buffer over-read).

CMSIS contract (basesw/mcHF/Drivers/CMSIS/DSP_Lib):
  arm_biquad_casd_df1_inst_f32:
      numStages  sections in a cascade
      pCoeffs    5 * numStages floats   (b0,b1,b2,a1,a2 per section)
      pState     4 * numStages floats
The check does not touch the source: audio_driver.c is owned by the audio patch.
"""

from __future__ import annotations

import glob
import math
import os

import cparse
import known_issues
from checkbase import Results

CHECK = "audio_instances"
AUDIO_DRIVER = os.path.join("mchf-eclipse", "drivers", "audio", "audio_driver.c")
BIQUAD = "arm_biquad_casd_df1_inst_f32"


def _clean(path):
    raw = cparse.read_text(path)
    return cparse.apply_preprocessor(cparse.strip_comments(raw))


def run(repo):
    res = Results(CHECK)
    path = os.path.join(repo, AUDIO_DRIVER)
    if not os.path.isfile(path):
        res.fail("missing", "audio_driver.c not found: %s" % path)
        return res
    text = _clean(path)
    defines = cparse.parse_defines(text)
    header_defines = {}
    for header in glob.glob(os.path.join(
            repo, "mchf-eclipse", "drivers", "audio", "*.h")):
        header_defines.update(
            cparse.parse_defines(_clean(header)))
    merged = dict(header_defines)
    merged.update(defines)

    for init in cparse.find_initializers(text, (BIQUAD,)):
        name = init["name"]
        loc = "%s:%d" % ("audio_driver.c",
                         cparse.line_of(text, init["offset"]))
        key = "audio_driver.c:%s" % name
        fields = cparse.parse_designated(init["body"])
        if fields is None:
            res.fail(key, "%s: cannot parse biquad initializer" % loc)
            continue
        num = cparse.eval_int(fields.get("numStages", ""), merged)
        if num is None or num <= 0:
            res.fail(key, "%s: cannot resolve/validate numStages (%r)"
                     % (loc, fields.get("numStages", "").strip()))
            continue
        ok = True
        for field, expected in (("pCoeffs", 5 * num), ("pState", 4 * num)):
            body = cparse.compound_literal_body(fields.get(field, ""))
            if body is None:
                res.fail(key, "%s: .%s is not an inline table" % (loc, field))
                ok = False
                continue
            values, unresolved = cparse.parse_float_tokens(body)
            if unresolved:
                res.fail(key, "%s: unresolved token(s) in %s: %s"
                         % (loc, field, ", ".join(unresolved[:4])))
                ok = False
                continue
            if any(not math.isfinite(v) for v in values):
                res.fail(key, "%s: %s contains nan/inf" % (loc, field))
                ok = False
                continue
            count = len(values)
            if count != expected:
                message = ("%s: numStages=%d but %s has %d elements "
                           "(expected %d)" % (loc, num, field, count, expected))
                reason = known_issues.KNOWN_ISSUES.get((CHECK, key))
                if reason is not None and field == "pCoeffs":
                    res.known_issue(key, message, reason)
                else:
                    res.fail(key, message)
                ok = False
            else:
                res.ok()
        if ok:
            res.ok()

    for (check_name, key), _reason in sorted(known_issues.KNOWN_ISSUES.items()):
        if check_name != CHECK:
            continue
        if not any(code == key for code, _m, _r in res.known):
            res.skip("stale-known-issue",
                     "%s no longer reproduces; remove it from known_issues.py"
                     % key)
    return res

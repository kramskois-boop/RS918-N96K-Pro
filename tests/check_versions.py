#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_versions.py - product identity invariants (audit P2 item 3e).

The numeric UHSDR version 2.9.48 must not change: the EEPROM/config
compatibility logic and the menu code parse it.  The product version string,
origin attribution and licence are the visible N96K branding.

Bootloader: the product targets UHSDR bootloader 5.0.4 (P2 workstream run in
parallel with this one).  Until that port lands, 4.1.2 is accepted as KNOWN so
the pre-integration tree stays green; any other value is a hard failure.
When the bootloader patch is merged the KNOWN entry simply stops appearing.
"""

from __future__ import annotations

import os
import re

import cparse
from checkbase import Results

CHECK = "version"
VERSION_HEADER = os.path.join("mchf-eclipse", "src", "uhsdr_version.h")

EXPECTED_UHSDR = ("2", "9", "48")
EXPECTED_PRODUCT_NAME = "RS918 N96K-Pro"
EXPECTED_PRODUCT_VERSION = "2.9.48-n96kp3"
EXPECTED_BOOT_VERS = "5.0.4"
ACCEPTED_BOOT_VERS = {
    "4.1.2": "P2 bootloader 5.0.4 port is a separate, parallel workstream; "
            "4.1.2 is the v1.0 baseline value",
}


def _string_literal(raw):
    """Return the C string literal content of a define value, or None."""
    match = re.search(r'"((?:[^"\\]|\\.)*)"', raw or "")
    return match.group(1) if match else None


def _check_string(res, defines, macro, expected):
    raw = defines.get(macro)
    if raw is None:
        res.fail("define", "%s is not defined" % macro)
        return None
    value = _string_literal(raw)
    if value is None:
        res.fail("define", "%s is not a string literal: %r" % (macro, raw))
        return None
    if value != expected:
        res.fail(macro, "%s is %r, expected %r" % (macro, value, expected))
        return value
    res.ok()
    return value


def run(repo):
    res = Results(CHECK)
    path = os.path.join(repo, VERSION_HEADER)
    if not os.path.isfile(path):
        res.fail("missing", "version header not found: %s" % path)
        return res
    text = cparse.strip_comments(cparse.read_text(path))
    defines = cparse.parse_defines(text)
    res.ok()

    # numeric version must stay 2.9.48 (EEPROM/config compatibility)
    major = _check_string(res, defines, "UHSDR_VER_MAJOR", EXPECTED_UHSDR[0])
    minor = _check_string(res, defines, "UHSDR_VER_MINOR", EXPECTED_UHSDR[1])
    release = _check_string(res, defines, "UHSDR_VER_RELEASE", EXPECTED_UHSDR[2])
    if None not in (major, minor, release):
        res.ok()

    # visible product identity
    product_name = _check_string(res, defines, "UHSDR_PRODUCT_NAME",
                                 EXPECTED_PRODUCT_NAME)
    product_version = _check_string(res, defines, "UHSDR_PRODUCT_VERSION",
                                    EXPECTED_PRODUCT_VERSION)
    if product_version is not None:
        joined = major + "." + minor + "." + release
        if not product_version.startswith(joined):
            res.fail("product-version",
                     "UHSDR_PRODUCT_VERSION %r does not start with the numeric "
                     "version %s" % (product_version, joined))
        else:
            res.ok()

    # derived version string must embed the product version
    raw_version = defines.get("UHSDR_VERSION", "")
    if "UHSDR_PRODUCT_VERSION" not in raw_version and \
            EXPECTED_PRODUCT_VERSION not in raw_version:
        res.fail("UHSDR_VERSION", "UHSDR_VERSION does not embed the product "
                                  "version: %r" % raw_version)
    else:
        res.ok()

    # licence / origin attribution (P0-6)
    licence = _string_literal(defines.get("UHSDR_LICENCE", ""))
    if licence is None or "GPLv3" not in licence:
        res.fail("UHSDR_LICENCE", "licence is not GPLv3: %r" % licence)
    else:
        res.ok()
    repo_url = _string_literal(defines.get("UHSDR_REPO", ""))
    if repo_url is None or "df8oe/UHSDR" not in repo_url:
        res.fail("UHSDR_REPO", "UHSDR origin attribution is absent: %r"
                 % repo_url)
    else:
        res.ok()

    # bootloader version: target 5.0.4, current 4.1.2 accepted as transition
    boot = _string_literal(defines.get("UHSDR_BOOT_VERS", ""))
    if boot is None:
        res.fail("UHSDR_BOOT_VERS", "UHSDR_BOOT_VERS is not defined")
    elif boot == EXPECTED_BOOT_VERS:
        res.ok()
    elif boot in ACCEPTED_BOOT_VERS:
        res.known_issue(
            "UHSDR_BOOT_VERS",
            "UHSDR_BOOT_VERS=%r, product target is %r"
            % (boot, EXPECTED_BOOT_VERS),
            ACCEPTED_BOOT_VERS[boot])
    else:
        res.fail("UHSDR_BOOT_VERS",
                 "UHSDR_BOOT_VERS=%r, expected %r"
                 % (boot, EXPECTED_BOOT_VERS))
    return res

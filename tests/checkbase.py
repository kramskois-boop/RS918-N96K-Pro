#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""checkbase.py - result bookkeeping shared by the N96K host checks.

Each check module exposes run(repo) and returns a Results instance:
    res.ok()                      one more passing assertion
    res.fail(code, message)       hard failure, makes run_all.py exit 1
    res.known(code, message, why) known/accepted defect (XFAIL, does not fail CI)
    res.skip(code, message)       not checkable in this tree/environment

A known issue is authoritative only while it actually reproduces: if a fix
lands, the matching KNOWN_ISSUES entry becomes "stale" and run_all.py reports
it, prompting the owner to delete the entry.
"""

from __future__ import annotations


class Results:
    def __init__(self, name):
        self.name = name
        self.passed = 0
        self.failed = []
        self.known = []
        self.skipped = []

    # -- recording ---------------------------------------------------------
    def ok(self, count=1):
        self.passed += count

    def fail(self, code, message):
        self.failed.append((code, message))

    def known_issue(self, code, message, reason):
        self.known.append((code, message, reason))

    def skip(self, code, message):
        self.skipped.append((code, message))

    # -- reporting ---------------------------------------------------------
    def report(self, out):
        if self.failed:
            for code, message in self.failed:
                out.write("[FAIL] %-16s %s: %s\n" % (self.name, code, message))
        for code, message, reason in self.known:
            out.write("[KNOWN] %-15s %s: %s\n" % (self.name, code, message))
            out.write("        known issue: %s\n" % reason)
        for code, message in self.skipped:
            out.write("[SKIP] %-16s %s: %s\n" % (self.name, code, message))
        out.write("[%s] %s: %d checks passed, %d failed, %d known\n" % (
            "FAIL" if self.failed else "PASS",
            self.name, self.passed, len(self.failed), len(self.known)))
        return not self.failed

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_workflow.py - lightweight GitHub Actions workflow linter (stdlib only).

PyYAML is not installed in the development environment used for this project,
so this script parses the block-style YAML that Actions workflows are written
in with a small indentation-aware scanner.  It catches the class of defect that
a real YAML parser would silently accept and truncate: **a duplicated mapping
key** (YAML forbids duplicate keys; GitHub keeps only the last one, which is how
the F12 regression - two top-level "env:"/"jobs:" blocks in build.yml - stayed
invisible until the workflow was read line by line).

What is checked:
  * duplicated keys in the same YAML mapping, at any nesting level, including
    duplicated job names under "jobs:";
  * optionally (--expect-top / --expect-jobs) that the expected top-level keys
    and job ids are present.

The scanner is not a full YAML parser.  It understands what this project's
workflows use: block mappings and sequences, comments, flow collections and
block scalars ("run: |", "with: >-").  Anchors/aliases and multi-document
files are not supported.

Usage:
    python tools/check_workflow.py .github/workflows/build.yml
    python tools/check_workflow.py --expect-jobs host-tests,docker-build,release\
        --expect-top name,on,permissions,concurrency,env,jobs build.yml

Exit code: 0 = clean, 1 = lint error (duplicate/missing key), 2 = usage error.
"""

from __future__ import annotations

import argparse
import re
import sys

# line that opens a block mapping entry: indentation, key, optional value
KEY_RE = re.compile(r"^(?P<indent> *)(?P<key>[A-Za-z0-9_.-]+|\"[^\"]+\"|'[^']+'):(?P<rest>.*)$")
# value that starts a block scalar (|, >, |-, >+, |2, ...)
BLOCK_SCALAR_RE = re.compile(r"^[|>][-+]?[0-9]*[ \t]*(?:#.*)?$")


class DuplicateKey(object):
    """A repeated mapping key: YAML/GitHub would keep only the last value."""

    def __init__(self, key, line, first_line):
        self.key = key
        self.line = line
        self.first_line = first_line

    def __str__(self):
        return ("duplicate key %r at line %d (first defined at line %d)"
                % (self.key, self.line, self.first_line))


class WorkflowScanner(object):
    """Indentation-aware scanner for the mapping keys of one workflow file."""

    def __init__(self, text):
        self.lines = text.splitlines()
        self.stack = []                 # [(indent, key)] - key ancestors
        self.seen = {}                  # (indent, parent_path) -> {key: line}
        self.duplicates = []            # [DuplicateKey] - all repeated keys
        self.top_level = []             # top-level keys in file order
        self.job_keys = []              # keys of the "jobs:" mapping
        self._item = 0
        self._block_indent = None       # current block scalar (key indentation)

    # -- helpers ---------------------------------------------------------
    def _parent_path(self):
        return tuple(key for _, key in self.stack)

    def _add_key(self, key, indent, lineno):
        # ancestors are the stack entries with a smaller indentation
        while self.stack and self.stack[-1][0] >= indent:
            self.stack.pop()
        parent = self._parent_path()
        mapping = self.seen.setdefault((indent, parent), {})
        if key in mapping:
            self.duplicates.append(DuplicateKey(key, lineno, mapping[key]))
        else:
            mapping[key] = lineno
        if indent == 0:
            self.top_level.append(key)
        # children of "jobs:" are the job names (indent 2 below a top-level key)
        if indent == 2 and parent == ("jobs",):
            self.job_keys.append(key)
        self.stack.append((indent, key))

    # -- main loop -------------------------------------------------------
    def scan(self):
        for lineno, raw in enumerate(self.lines, 1):
            line = raw.rstrip("\r")
            if self._block_indent is not None:
                if not line.strip():
                    continue
                indent = len(line) - len(line.lstrip(" "))
                if indent > self._block_indent:
                    continue            # scalar content of "run: |" etc.
                self._block_indent = None
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            indent = len(line) - len(line.lstrip(" "))
            content = line[indent:]
            # a sequence item opens a fresh mapping scope: "- name: ..."
            if content == "-" or content.startswith("- "):
                self._item += 1
                while self.stack and self.stack[-1][0] >= indent:
                    self.stack.pop()
                self.stack.append((indent, "[item %d]" % self._item))
                rest = content[2:].strip() if content.startswith("- ") else ""
                if rest and not rest.startswith("#"):
                    match = KEY_RE.match(rest)
                    if match:
                        key = _unquote(match.group("key"))
                        rest_value = match.group("rest").strip()
                        self._add_key(key, indent + 2, lineno)
                        if BLOCK_SCALAR_RE.match(rest_value):
                            self._block_indent = indent + 2
                continue
            match = KEY_RE.match(line)
            if not match:
                continue                    # plain scalar line / flow continuation
            key = _unquote(match.group("key"))
            value = match.group("rest").strip()
            self._add_key(key, indent, lineno)
            if BLOCK_SCALAR_RE.match(value):
                self._block_indent = indent
        return self

    # -- results ---------------------------------------------------------
    def top_level_duplicates(self):
        """Return [(key, occurrence_index)] for keys repeated at indent 0."""
        seen = {}
        result = []
        for key in self.top_level:
            seen[key] = seen.get(key, 0) + 1
            if seen[key] > 1:
                result.append((key, seen[key]))
        return result


def _unquote(key):
    if len(key) >= 2 and key[0] == key[-1] and key[0] in "\"'":
        return key[1:-1]
    return key


def check_file(path, expect_top, expect_jobs):
    errors = []
    warnings = []
    try:
        with open(path, "r", encoding="utf-8-sig") as handle:
            text = handle.read()
    except OSError as exc:
        return ["cannot read %s: %s" % (path, exc)], warnings, None
    scanner = WorkflowScanner(text).scan()
    for duplicate in scanner.duplicates:
        errors.append("%s:%d: %s" % (path, duplicate.line, duplicate))
    print("[workflow] %s: %d top-level keys, %d job(s)"
          % (path, len(scanner.top_level), len(scanner.job_keys)))
    print("[workflow]   top-level: %s" % ", ".join(scanner.top_level))
    print("[workflow]   jobs     : %s" % ", ".join(scanner.job_keys))
    for key in expect_top:
        if key not in scanner.top_level:
            errors.append("%s: expected top-level key %r is missing" % (path, key))
    for job in expect_jobs:
        if job not in scanner.job_keys:
            errors.append("%s: expected job %r is missing (jobs: %s)"
                          % (path, job, ", ".join(scanner.job_keys) or "none"))
    return errors, warnings, scanner


def parse_args(argv):
    parser = argparse.ArgumentParser(
        prog="check_workflow.py",
        description="stdlib lint for .github/workflows/*.yml: duplicate keys/jobs.")
    parser.add_argument("workflow", nargs="+", help="workflow file(s) to check")
    parser.add_argument("--expect-top", default="",
                        help="comma-separated top-level keys that must exist")
    parser.add_argument("--expect-jobs", default="",
                        help="comma-separated job ids that must exist under jobs:")
    args = parser.parse_args(argv)
    args.expect_top = [k.strip() for k in args.expect_top.split(",") if k.strip()]
    args.expect_jobs = [j.strip() for j in args.expect_jobs.split(",") if j.strip()]
    return args


def main(argv):
    args = parse_args(argv)
    failed = False
    for path in args.workflow:
        errors, warnings, _scanner = check_file(path, args.expect_top, args.expect_jobs)
        for warning in warnings:
            print("[workflow] WARNING: " + warning)
        for error in errors:
            print("[workflow] ERROR: " + error)
        if errors:
            failed = True
    if failed:
        print("[workflow] FAILED")
        return 1
    print("[workflow] OK: no duplicate keys" +
          (" and all expected keys/jobs are present" if
           (args.expect_top or args.expect_jobs) else ""))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        sys.exit(130)

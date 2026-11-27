#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""release.py - assemble the release/vX directory for the RS918 N96K-Pro.

Takes the artifacts produced by "tools/build.py all" (or downloaded from the
CI "docker-build" job) and builds the release directory that is attached to a
GitHub Release:

    fw-mchf.{bin,hex,elf,map}
    bl-mchf-<bootloader version>.{bin,hex,elf,map}   (from build/bl-mchf.*)
    bl-mchf-4.1.2-known-good.bin                     (optional --known-good)
    build-summary.json, compile_commands.json, warnings.log
    RELEASE-NOTES.md
    SHA256SUMS.txt   (upper-case SHA-256, two spaces, case-insensitive
                      sort by file name - the format of release/v1.0-v1.2)

Before packing, the flash/RAM/CCM budgets are re-checked with
"docker/verify_artifacts.py" (a subprocess).  The check is skipped
automatically when the input directory does not contain the canonical
fw-mchf/bl-mchf artifact names (e.g. when a release/ directory is used as a
fixture for testing), or with --no-verify.

RELEASE-NOTES.md is taken from --notes, from docs/release-notes/<version>.md
when that file exists in the repository, or generated from a template (review
it before publishing - the template contains TODO markers).

Usage (from the repository root, or from anywhere; relative paths are resolved
against the current directory):

    python tools/release.py --version v1.3 --build-dir build --out release/v1.3
    python tools/release.py --version v1.3 --build-dir artifacts \
        --out release/v1.3 --notes docs/release-notes/v1.3.md
    python tools/release.py --version v1.3 --build-dir build --out release/v1.3 \
        --known-good release/v1.2/bl-mchf-4.1.2-known-good.bin --force

Exit code: 0 = release directory is complete and verified, 1 = packaging or
verification failed, 2 = usage error.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

SCRIPT_PATH = Path(__file__).resolve()
PROJECT_ROOT = SCRIPT_PATH.parent.parent
DEFAULT_BUILD_DIR = PROJECT_ROOT / "build"
DEFAULT_RELEASE_ROOT = PROJECT_ROOT / "release"
VERIFY_SCRIPT = PROJECT_ROOT / "docker" / "verify_artifacts.py"
VERSION_HEADER = PROJECT_ROOT / "mchf-eclipse" / "src" / "uhsdr_version.h"
NOTES_DIR = PROJECT_ROOT / "docs" / "release-notes"

FIRMWARE = "fw-mchf"
BOOTLOADER = "bl-mchf"
SUFFIXES = (".bin", ".hex", ".elf", ".map")
BUILD_SUMMARY = "build-summary.json"
COMPILE_DB = "compile_commands.json"
WARNINGS_LOG = "warnings.log"
RELEASE_NOTES = "RELEASE-NOTES.md"
CHECKSUMS = "SHA256SUMS.txt"

KNOWN_GOOD_NAME = "bl-mchf-4.1.2-known-good.bin"
KNOWN_GOOD_SIZE = 31496
KNOWN_GOOD_SHA256 = (
    "933249535DD5CACB871708802C4D09549AB84E2D4DA7AE53A69BB6E2FF3B8CA1")

DEFAULT_BOOTLOADER_VERSION = "5.0.4"
VERSION_RE = re.compile(r"^v[0-9][0-9A-Za-z._+-]*$")


class ReleaseError(Exception):
    """Raised when the release directory cannot be produced."""


# ---------------------------------------------------------------------------
# small helpers
# ---------------------------------------------------------------------------

def sha256_file(path):
    digest = hashlib.sha256()
    with open(str(path), "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def write_text_lf(path, text):
    """Write UTF-8 text with LF line endings regardless of the host OS."""
    with open(str(path), "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def human_size(size):
    return "{:,}".format(int(size)).replace(",", " ")


def percent(used, total):
    try:
        value = 100.0 * float(used) / float(total)
    except (TypeError, ValueError, ZeroDivisionError):
        return "-"
    return ("%.1f" % value).replace(".", ",")


def count_lines(path):
    try:
        with open(str(path), "r", encoding="utf-8", errors="replace") as handle:
            return sum(1 for _ in handle)
    except OSError:
        return 0


def read_version_strings():
    """Best-effort parse of the version header (bootloader + product version)."""
    info = {"bootloader": DEFAULT_BOOTLOADER_VERSION, "product": None}
    try:
        text = VERSION_HEADER.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return info
    match = re.search(r'#\s*define\s+UHSDR_BOOT_VERS\s+"([^"]+)"', text)
    if match:
        info["bootloader"] = match.group(1).strip()
    match = re.search(r'#\s*define\s+UHSDR_PRODUCT_VERSION\s+"([^"]+)"', text)
    if match:
        info["product"] = match.group(1).strip()
    return info


def git_info():
    """Return {'commit': ..., 'branch': ...} or {} when git is not usable."""
    info = {}
    for key, args in (("commit", ("rev-parse", "--short", "HEAD")),
                      ("branch", ("rev-parse", "--abbrev-ref", "HEAD"))):
        try:
            proc = subprocess.run(["git"] + list(args), cwd=str(PROJECT_ROOT),
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                  timeout=10, check=False)
        except (OSError, subprocess.SubprocessError):
            return {}
        if proc.returncode != 0:
            return {}
        value = proc.stdout.decode("utf-8", "replace").strip()
        if value:
            info[key] = value
    if info.get("branch") == "HEAD":
        info["branch"] = None
    return info


def _is_within(child, parent):
    try:
        child.relative_to(parent)
        return True
    except ValueError:
        return False


# ---------------------------------------------------------------------------
# inputs
# ---------------------------------------------------------------------------

def locate_artifacts(build_dir, bootloader_version):
    """Map output release file name -> source path; raise on missing inputs."""
    sources = {}
    missing = []

    for suffix in SUFFIXES:
        name = FIRMWARE + suffix
        path = build_dir / name
        if path.is_file():
            sources[name] = path
        else:
            missing.append(name)

    versioned = BOOTLOADER + "-" + bootloader_version
    for suffix in SUFFIXES:
        raw_name = BOOTLOADER + suffix
        raw_path = build_dir / raw_name
        versioned_name = versioned + suffix
        versioned_path = build_dir / versioned_name
        if raw_path.is_file():
            sources[versioned_name] = raw_path
        elif versioned_path.is_file():
            sources[versioned_name] = versioned_path
        else:
            missing.append("%s (or %s)" % (raw_name, versioned_name))

    for name in (BUILD_SUMMARY, COMPILE_DB):
        path = build_dir / name
        if path.is_file():
            sources[name] = path
        else:
            missing.append(name)

    # warnings.log is optional: tools/build.py only creates it when there were
    # warnings, so an empty log is written by pack() when it is absent.
    warnings_path = build_dir / WARNINGS_LOG
    if warnings_path.is_file():
        sources[WARNINGS_LOG] = warnings_path

    if missing:
        raise ReleaseError(
            "missing required artifact(s) in %s:\n  %s"
            % (build_dir, "\n  ".join(sorted(missing))))
    return sources


def load_build_summary(build_dir):
    path = build_dir / BUILD_SUMMARY
    try:
        with open(str(path), "r", encoding="utf-8") as handle:
            summary = json.load(handle)
    except OSError as exc:
        raise ReleaseError("cannot read %s: %s" % (path, exc))
    except ValueError as exc:
        raise ReleaseError("%s is not valid JSON: %s" % (path, exc))
    for key in ("firmware", "bootloader"):
        if not isinstance(summary.get(key), dict):
            raise ReleaseError("%s has no %r section" % (BUILD_SUMMARY, key))
    return summary


def inspect_known_good(path):
    if not path.is_file():
        raise ReleaseError("--known-good file not found: %s" % path)
    size = path.stat().st_size
    digest = sha256_file(path)
    warnings = []
    if size != KNOWN_GOOD_SIZE:
        warnings.append("known-good size %d != documented %d bytes"
                        % (size, KNOWN_GOOD_SIZE))
    if digest != KNOWN_GOOD_SHA256:
        warnings.append("known-good SHA256 does not match the verified 4.1.2 "
                        "digest (%s...)" % KNOWN_GOOD_SHA256[:16])
    return {"path": path, "size": size, "sha256": digest, "warnings": warnings}


def resolve_notes(version, explicit):
    """Return (path or None, description of the source)."""
    if explicit:
        path = Path(explicit).expanduser().resolve()
        if not path.is_file():
            raise ReleaseError("--notes file not found: %s" % path)
        return path, "explicit --notes"
    candidate = NOTES_DIR / (version + ".md")
    if candidate.is_file():
        return candidate, "docs/release-notes/%s.md" % version
    return None, "template"


# ---------------------------------------------------------------------------
# verification
# ---------------------------------------------------------------------------

def run_verification(build_dir, no_verify):
    """Re-check sizes/budgets with docker/verify_artifacts.py."""
    if no_verify:
        print("[release] artifact verification skipped (--no-verify)")
        return "skipped (--no-verify)"
    if not VERIFY_SCRIPT.is_file():
        print("[release] WARNING: %s not found - verification skipped"
              % VERIFY_SCRIPT)
        return "skipped (verifier not found)"
    canonical = [BUILD_SUMMARY, FIRMWARE + ".elf", BOOTLOADER + ".elf"]
    if not all((build_dir / name).is_file() for name in canonical):
        print("[release] artifact verification skipped: %s does not look like "
              "a raw build directory (canonical fw-mchf/bl-mchf names)"
              % build_dir)
        return "skipped (not a raw build directory)"
    print("[release] running docker/verify_artifacts.py %s" % build_dir)
    try:
        proc = subprocess.run([sys.executable, str(VERIFY_SCRIPT), str(build_dir)],
                              cwd=str(PROJECT_ROOT), stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=120, check=False)
    except (OSError, subprocess.SubprocessError) as exc:
        raise ReleaseError("cannot run %s: %s" % (VERIFY_SCRIPT.name, exc))
    output = proc.stdout.decode("utf-8", "replace").rstrip()
    if output:
        print(output)
    if proc.returncode != 0:
        raise ReleaseError("artifact verification failed (%s exit code %d)"
                           % (VERIFY_SCRIPT.name, proc.returncode))
    return "PASS (docker/verify_artifacts.py)"


# ---------------------------------------------------------------------------
# RELEASE-NOTES.md template
# ---------------------------------------------------------------------------

def render_template(version, summary, files, known_good, warnings_lines,
                    bootloader_version, product_version, git):
    fw = summary["firmware"]
    bl = summary["bootloader"]
    lines = []
    add = lines.append

    add("# RS918 N96K-Pro %s — release" % version)
    add("")
    add("> Сгенерировано `tools/release.py`; TODO-пункты шаблона нужно "
        "заполнить перед публикацией. Цифры взяты из `%s`."
        % BUILD_SUMMARY)
    add("")

    build = []
    if git.get("commit"):
        build.append("commit `%s`" % git["commit"])
    if git.get("branch"):
        build.append("ветка `%s`" % git["branch"])
    build.append("тег `%s`" % version)
    if product_version:
        build.append("версия прошивки `fwv-%s`" % product_version)
    add("Сборка: " + ", ".join(build) + ".")
    add("Лицензия: GNU GPLv3; исходники — тег `%s` (см. "
        "`LICENCE.md`)." % version)
    add("")

    add("## Артефакты")
    add("")
    add("| Файл | Размер, Б |")
    add("|---|---:|")
    for name, size in files:
        add("| `%s` | %s |" % (name, human_size(size)))
    add("")
    if known_good is not None:
        add("- `%s` — прежний загрузчик 4.1.2 known-good для отката "
            "(%s Б)." % (KNOWN_GOOD_NAME, human_size(known_good["size"])))
    else:
        add("- Загрузчик 4.1.2 known-good в автоматический каталог не входит: "
            "в CI-артефактах его нет; добавьте вручную через "
            "`--known-good` при локальной упаковке.")
    add("")
    add("Контрольные суммы — в `%s`." % CHECKSUMS)
    add("")

    add("## Бюджет")
    add("")
    add("- Flash: %s / %s (%s %%), запас %s Б."
        % (human_size(fw.get("flash_used", 0)),
           human_size(fw.get("flash_size", 0)),
           percent(fw.get("flash_used"), fw.get("flash_size")),
           human_size(fw.get("flash_free", 0))))
    add("- RAM: %s / %s; CCM: %s / %s (%s %%)."
        % (human_size(fw.get("ram_used", 0)),
           human_size(fw.get("ram_size", 0)),
           human_size(fw.get("ccm_used", 0)),
           human_size(fw.get("ccm_size", 0)),
           percent(fw.get("ccm_used"), fw.get("ccm_size"))))
    add("- Загрузчик %s: %s / %s (%s %%)."
        % (bootloader_version,
           human_size(bl.get("flash_used", 0)),
           human_size(bl.get("flash_size", 0)),
           percent(bl.get("flash_used"), bl.get("flash_size"))))
    add("- Предупреждения компилятора: %d строк (`%s`)."
        % (warnings_lines, WARNINGS_LOG))
    add("- Host-тесты: см. job `host-tests` CI-прогона (или "
        "`python tests/run_all.py`); тесты проверяют ЦОС-таблицы, "
        "версии и EEPROM, но не железо.")
    add("")
    add("## Что нового")
    add("")
    add("- TODO: краткая сводка изменений релиза (для автоматической "
        "публикации положите её в `docs/release-notes/%s.md`)." % version)
    add("- Полный список — `docs/CHANGELOG.md`, "
        "`docs/PATCHES.md`.")
    add("")
    add("## Прошивка")
    add("")
    add("1. Сделайте бэкап текущей прошивки и настроек.")
    add("2. `fw-mchf.bin` — в корень FAT-флешки, флешку в USB-порт "
        "трансивера, выключить и включить питание с зажатой BAND−, дождаться "
        "`Finished.` (на флешке останется `fw-mchf.old` для отката).")
    add("3. Альтернатива — DFU (BAND+ при включении + STM32CubeProgrammer).")
    add("4. После смены прошивки возможен сброс настроек: раскладка EEPROM "
        "может отличаться.")
    add("")
    add("Подробно — `docs/TESTPLAN.md`, `docs/BOOTLOADER.md`.")
    add("")
    add("## Ограничения")
    add("")
    add("- TODO: перечислить непроверенное на железе (см. "
        "`docs/TESTPLAN.md`); если сборка на железе не испытывалась — "
        "указать это явно.")
    add("- Известные риски и незакрытые пункты — `docs/CHANGELOG.md`, "
        "`docs/PATCHES.md`.")
    add("")
    add("## Лицензия")
    add("")
    add("GNU GPLv3 (`LICENCE.md`); исходники доступны по тегу "
        "`%s`." % version)
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# output directory
# ---------------------------------------------------------------------------

def prepare_out_dir(out_dir, build_dir, force):
    out_abs = out_dir.expanduser().resolve()
    build_abs = build_dir.expanduser().resolve()
    release_root = DEFAULT_RELEASE_ROOT.resolve()

    if out_abs == PROJECT_ROOT:
        raise ReleaseError("refusing to use the repository root as --out")
    if out_abs == release_root:
        raise ReleaseError("refusing to use the release/ root as --out; "
                           "pass release/<version> instead")
    if _is_within(out_abs, build_abs) or _is_within(build_abs, out_abs):
        raise ReleaseError("--out and --build-dir must not contain each other")
    if (out_abs / ".git").exists():
        raise ReleaseError("refusing to remove a git repository at %s" % out_abs)
    if out_abs.exists():
        if not out_abs.is_dir():
            raise ReleaseError("--out exists and is not a directory: %s" % out_abs)
        entries = list(out_abs.iterdir())
        if entries and not force:
            raise ReleaseError("output directory is not empty: %s (use --force "
                               "to recreate it)" % out_abs)
        if entries:
            force_allowed = (_is_within(out_abs, release_root)
                             or not _is_within(out_abs, PROJECT_ROOT))
            if not force_allowed:
                raise ReleaseError(
                    "refusing to delete %s: --force only recreates directories "
                    "outside the repository or under release/" % out_abs)
            shutil.rmtree(str(out_abs))
    out_abs.mkdir(parents=True, exist_ok=True)
    return out_abs


# ---------------------------------------------------------------------------
# packing
# ---------------------------------------------------------------------------

def pack(args, build_dir, out_dir, version, bootloader_version):
    build_dir = build_dir.expanduser().resolve()
    if not build_dir.is_dir():
        raise ReleaseError("build directory not found: %s" % build_dir)
    sources = locate_artifacts(build_dir, bootloader_version)
    summary = load_build_summary(build_dir)
    notes_path, notes_source = resolve_notes(version, args.notes)
    known_good = None
    if args.known_good:
        known_good = inspect_known_good(Path(args.known_good).expanduser().resolve())
        for warning in known_good["warnings"]:
            print("[release] WARNING: " + warning)

    verification = run_verification(build_dir, args.no_verify)

    out_dir = prepare_out_dir(out_dir, build_dir, args.force)
    print("[release] packing %s -> %s" % (build_dir, out_dir))

    copied = []
    for name, source in sorted(sources.items()):
        shutil.copyfile(str(source), str(out_dir / name))
        copied.append(name)
    if WARNINGS_LOG not in sources:
        print("[release] WARNING: %s is missing in the build directory; "
              "writing an empty one" % WARNINGS_LOG)
        write_text_lf(out_dir / WARNINGS_LOG, "")
        copied.append(WARNINGS_LOG)
    if known_good is not None:
        shutil.copyfile(str(known_good["path"]), str(out_dir / KNOWN_GOOD_NAME))
        copied.append(KNOWN_GOOD_NAME)

    if notes_path is not None:
        shutil.copyfile(str(notes_path), str(out_dir / RELEASE_NOTES))
    else:
        files = [(name, (out_dir / name).stat().st_size) for name in copied]
        files.sort()
        text = render_template(version, summary, files, known_good,
                               count_lines(out_dir / WARNINGS_LOG),
                               bootloader_version,
                               read_version_strings().get("product"),
                               git_info())
        write_text_lf(out_dir / RELEASE_NOTES, text)
    copied.append(RELEASE_NOTES)

    # release/v1.0-v1.2 sort case-insensitively (PowerShell Sort-Object Name)
    names = sorted((p.name for p in out_dir.iterdir()
                    if p.is_file() and p.name != CHECKSUMS), key=str.lower)
    lines = ["%s  %s" % (sha256_file(out_dir / name), name) for name in names]
    write_text_lf(out_dir / CHECKSUMS, "\n".join(lines) + "\n")

    # self-check: every listed checksum must match the file that was written
    written = {}
    with open(str(out_dir / CHECKSUMS), "r", encoding="utf-8") as handle:
        for lineno, raw in enumerate(handle, 1):
            line = raw.rstrip("\n")
            if not line:
                continue
            parts = line.split("  ", 1)
            if len(parts) != 2 or len(parts[0]) != 64:
                raise ReleaseError("internal error: bad line %d in %s"
                                   % (lineno, CHECKSUMS))
            written[parts[1]] = parts[0]
    actual = sorted((p.name for p in out_dir.iterdir()
                     if p.is_file() and p.name != CHECKSUMS), key=str.lower)
    if sorted(written, key=str.lower) != actual:
        raise ReleaseError("internal error: %s does not list every file" % CHECKSUMS)
    for name in actual:
        if sha256_file(out_dir / name) != written[name]:
            raise ReleaseError("internal error: checksum mismatch for %s" % name)

    return {
        "version": version,
        "build_dir": build_dir,
        "out_dir": out_dir,
        "bootloader_version": bootloader_version,
        "summary": summary,
        "files": sorted(copied, key=str.lower),
        "checksums": len(lines),
        "warnings_lines": count_lines(out_dir / WARNINGS_LOG),
        "verification": verification,
        "notes_source": notes_source,
        "known_good": known_good,
    }


def print_summary(result):
    fw = result["summary"]["firmware"]
    bl = result["summary"]["bootloader"]
    print("[release] version       : %s" % result["version"])
    print("[release] build dir     : %s" % result["build_dir"])
    print("[release] output dir    : %s" % result["out_dir"])
    print("[release] bootloader    : %s" % result["bootloader_version"])
    print("[release] firmware      : fw-mchf.bin %s B (flash %s/%s, RAM %s/%s, "
          "CCM %s/%s)" % (human_size(fw.get("bin_size", 0)),
                          human_size(fw.get("flash_used", 0)),
                          human_size(fw.get("flash_size", 0)),
                          human_size(fw.get("ram_used", 0)),
                          human_size(fw.get("ram_size", 0)),
                          human_size(fw.get("ccm_used", 0)),
                          human_size(fw.get("ccm_size", 0))))
    print("[release] bootloader    : bl-mchf.bin %s B (flash %s/%s)"
          % (human_size(bl.get("bin_size", 0)),
             human_size(bl.get("flash_used", 0)),
             human_size(bl.get("flash_size", 0))))
    print("[release] warnings      : %d lines" % result["warnings_lines"])
    print("[release] verification  : %s" % result["verification"])
    print("[release] notes         : %s" % result["notes_source"])
    if result["known_good"] is not None:
        print("[release] known-good    : %s (%s B)"
              % (KNOWN_GOOD_NAME, human_size(result["known_good"]["size"])))
    else:
        print("[release] known-good    : not requested (CI artifacts do not "
              "contain it; pass --known-good locally)")
    print("[release] files         : %d payload (+ %s = %d total), "
          "%d checksums" % (len(result["files"]), CHECKSUMS,
                            len(result["files"]) + 1, result["checksums"]))
    for name in result["files"]:
        path = result["out_dir"] / name
        print("[release]   %-40s %s B" % (name, human_size(path.stat().st_size)))
    print("[release] OK")


# ---------------------------------------------------------------------------
# command line
# ---------------------------------------------------------------------------

def parse_args(argv):
    parser = argparse.ArgumentParser(
        prog="release.py",
        description="Pack the RS918 N96K-Pro release directory from build "
                    "artifacts (stdlib only).",
        epilog="see docs/RELEASE.md for the release procedure")
    parser.add_argument("--version", required=True,
                        help="release version / tag, e.g. v1.3")
    parser.add_argument("--build-dir", default=None,
                        help="artifact directory (default: <repo>/build)")
    parser.add_argument("--out", default=None,
                        help="output release directory (default: "
                             "<repo>/release/<version>)")
    parser.add_argument("--notes", default=None,
                        help="RELEASE-NOTES.md source (default: "
                             "docs/release-notes/<version>.md if it exists, "
                             "otherwise a generated template)")
    parser.add_argument("--known-good", default=None,
                        help="optional 4.1.2 known-good bootloader .bin to "
                             "include as %s" % KNOWN_GOOD_NAME)
    parser.add_argument("--force", "-f", action="store_true",
                        help="recreate a non-empty output directory")
    parser.add_argument("--no-verify", action="store_true",
                        help="skip the docker/verify_artifacts.py size check")
    return parser.parse_args(argv)


def main(argv):
    args = parse_args(argv)
    if not VERSION_RE.match(args.version):
        print("[release] ERROR: --version must look like a release tag, "
              "e.g. v1.3 (got %r)" % args.version)
        return 2
    build_dir = Path(args.build_dir) if args.build_dir else DEFAULT_BUILD_DIR
    out_dir = Path(args.out) if args.out else DEFAULT_RELEASE_ROOT / args.version
    bootloader_version = read_version_strings()["bootloader"]
    try:
        result = pack(args, build_dir, out_dir, args.version, bootloader_version)
    except ReleaseError as exc:
        print("[release] ERROR: %s" % exc, file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("[release] interrupted", file=sys.stderr)
        return 130
    print_summary(result)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

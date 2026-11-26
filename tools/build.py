#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
build.py - reproducible command line build driver for the UHSDR / RS-918 firmware.

This script replaces GNU make (which is not installed on the target machine) by
reading the very same make include files that the original Makefile uses:

    include.mak      SUBDIRS
    files.mak        SRC
    bootloader.mak   BL_SRC
    f4-include.mak   HAL_SUBDIRS
    f4-files.mak     HAL_SRC, DSPLIB_SRC
    f4-bootloader.mak BL_HAL_SRC

and by using the same compiler/linker flags as Makefile for BUILDFOR=F4:

  firmware   : MACHFLAGS_F4 + COMPILEFLAGS + -O2
               linker/arm-gcc-link_f4_flash1024k.ld
  bootloader : MACHFLAGS_F4 + COMPILEFLAGS + -Os -DBOOTLOADER_BUILD
               linker/arm-gcc-link-bootloader_f4.ld

Usage (from the repository root or from anywhere):

    python tools/build.py all          # firmware + bootloader
    python tools/build.py firmware     # fw-mchf.elf/.bin/.map only
    python tools/build.py bootloader   # bl-mchf.elf/.bin/.map only
    python tools/build.py clean        # remove the build/ directory

All artifacts are written to <repo>/build/ (git-ignored), sources are never
touched.  Exit code is 0 on success and non-zero on the first failure.

The toolchain is looked up in this order:
  1. --toolchain <dir>            (directory that contains arm-none-eabi-gcc)
  2. $ARM_GCC_BIN                 (same)
  3. E:\\tools\\gcc-arm\\bin       (default of this project)
  4. PATH
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

# ---------------------------------------------------------------------------
# project layout
# ---------------------------------------------------------------------------

SCRIPT_PATH = Path(__file__).resolve()
PROJECT_ROOT = SCRIPT_PATH.parent.parent           # .../Mchf_RS918
ECLIPSE_DIR = PROJECT_ROOT / "mchf-eclipse"
DEFAULT_BUILD_DIR = PROJECT_ROOT / "build"

DEFAULT_TOOLCHAIN_DIRS = [
    Path(r"E:\tools\gcc-arm\bin"),
]

FIRMWARE_NAME = "fw-mchf"          # fw-$(TRX_ID)
BOOTLOADER_NAME = "bl-mchf"        # bl-$(TRX_ID)

# ---------------------------------------------------------------------------
# flags, copied verbatim from mchf-eclipse/Makefile (BUILDFOR=F4)
# ---------------------------------------------------------------------------

MACHFLAGS_F4 = [
    "-mcpu=cortex-m4",
    "-mfloat-abi=hard",
    "-mfpu=fpv4-sp-d16",
    "-mthumb",
    "-DARM_MATH_CM4",
    "-DCORTEX_M4",
    "-DSTM32F407xx",
    "-D__FPU_PRESENT=1U",
]

# COMPILEFLAGS := -DUSE_HAL_DRIVER -DDEBUG -DUSE_FULL_ASSERT -DTRACE -D_GNU_SOURCE \
#                 -DTRX_ID=\"$(TRX_ID)\" -DTRX_NAME=\"$(TRX_NAME)\" $(CONFIGFLAGS) \
#                 -ffunction-sections -fdata-sections -flto -Wall -Wno-unused-function -g3
COMPILEFLAGS_BASE = [
    "-DUSE_HAL_DRIVER",
    "-DDEBUG",
    "-DUSE_FULL_ASSERT",
    "-DTRACE",
    "-D_GNU_SOURCE",
    # NOTE: Makefile sets TRX_NAME='mcHF QRP' (make keeps the single quotes) and
    # then -DTRX_NAME=\"$(TRX_NAME)\".  That produces the C string literal
    # 'mcHF QRP' *including* the single quotes.  We use the clean form
    # "mcHF QRP" instead (this is what the menu/LCD strings expect); see
    # BUILD.md and _analysis/build-report.md.
    "-DTRX_ID=\"mchf\"",
    "-DTRX_NAME=\"mcHF QRP\"",
    "-ffunction-sections",
    "-fdata-sections",
    "-flto",
    "-Wall",
    "-Wno-unused-function",
    "-g3",
    # [GCC >= 10 compatibility] since GCC 10 the default is -fno-common.  The project
    # was written for GCC < 10 and has several tentative definitions in headers, e.g.
    #   hardware/uhsdr_board.h   : bm_set_normal / bm_set_rtc   (button maps)
    #   drivers/usb/device/...   : USBD_COMP_CfgDesc            (USB descriptors)
    # Without -fcommon the -flto link stops with "multiple definition of ...".
    # One more definition (the HAL tick counter `uwTick`) is not repaired by -fcommon
    # (LTO promotes it), it is declared extern in stm32f4xx_hal.h and defined in
    # stm32f4xx_hal.c instead - see the comments in those files.
    "-fcommon",
]

LINKER_DIR = ECLIPSE_DIR / "linker"
LINKER_SCRIPT_FIRMWARE = "arm-gcc-link_f4_flash1024k.ld"
LINKER_SCRIPT_BOOTLOADER = "arm-gcc-link-bootloader_f4.ld"

# LDFLAGS_BASE := -L$(LINKERLOC) -flto --specs=nano.specs -g3
LDFLAGS_BASE = ["-L" + str(LINKER_DIR), "-flto", "--specs=nano.specs", "-g3"]
LIBS = ["-lm", "-lc", "-lnosys"]

DSPLIB_EXTRA_CFLAGS = ["-Wno-strict-aliasing"]   # Makefile: $(DSPLIB_OBJS): EXTRA_CFLAGS:= -Wno-strict-aliasing

# memory map of the target (linker scripts)
FW_FLASH_ORIGIN = 0x08010000
FW_FLASH_LENGTH = 1024 * 1024 - 64 * 1024
FW_RAM_ORIGIN = 0x20000000
FW_RAM_LENGTH = 128 * 1024
FW_CCM_ORIGIN = 0x10000000
FW_CCM_LENGTH = 64 * 1024
BL_FLASH_ORIGIN = 0x08000000
BL_FLASH_LENGTH = 32 * 1024


class BuildError(Exception):
    """fatal build error, message is shown to the user"""


# ---------------------------------------------------------------------------
# make include parser
# ---------------------------------------------------------------------------

_ASSIGN_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*(:=|\+=|\?=|=)\s*(.*)$")


def _logical_lines(path: Path):
    """join make continuation lines (trailing backslash) into one line"""
    buf = ""
    text = path.read_text(encoding="utf-8", errors="replace")
    for raw in text.splitlines():
        line = (buf + " " + raw.strip()) if buf else raw.strip()
        if line.endswith("\\"):
            buf = line[:-1]
            continue
        buf = ""
        yield line
    if buf:
        yield buf


def read_make_vars(paths) -> dict:
    """parse simple `NAME := value` assignments of the UHSDR *.mak files"""
    values = {}
    for path in paths:
        path = Path(path)
        if not path.is_file():
            raise BuildError("make include not found: %s" % path)
        for line in _logical_lines(path):
            if not line or line.startswith("#"):
                continue
            match = _ASSIGN_RE.match(line)
            if not match:
                raise BuildError("unsupported make syntax in %s: %r" % (path.name, line))
            name, op, value = match.group(1), match.group(2), match.group(3).strip()
            if op == "+=":
                values[name] = (values.get(name, "") + " " + value).strip()
            elif op == "?=":
                values.setdefault(name, value)
            else:
                values[name] = value
    return values


# ---------------------------------------------------------------------------
# toolchain
# ---------------------------------------------------------------------------

class Toolchain:
    def __init__(self, bindir: Path):
        self.bindir = bindir
        suffix = ".exe" if os.name == "nt" else ""

        def tool(name: str) -> Path:
            path = bindir / (name + suffix)
            if path.is_file():
                return path
            # tolerate the other spelling (e.g. a non-suffixed toolchain on Windows)
            alt = bindir / (name + ("" if suffix else ".exe"))
            return alt if alt.is_file() else path

        self.gcc = tool("arm-none-eabi-gcc")
        self.gxx = tool("arm-none-eabi-g++")
        self.objcopy = tool("arm-none-eabi-objcopy")
        self.size = tool("arm-none-eabi-size")
        self.nm = tool("arm-none-eabi-nm")
        missing = [str(p) for p in (self.gcc, self.gxx, self.objcopy, self.size, self.nm)
                   if not p.is_file()]
        if missing:
            raise BuildError("toolchain is incomplete, missing: %s" % ", ".join(missing))

    def version(self) -> str:
        out = subprocess.run([str(self.gcc), "--version"], capture_output=True, text=True,
                             errors="replace", check=True)
        return out.stdout.splitlines()[0].strip() if out.stdout else "unknown"


def find_toolchain(explicit: str | None) -> Toolchain:
    candidates = []
    if explicit:
        candidates.append(Path(explicit))
    if os.environ.get("ARM_GCC_BIN"):
        candidates.append(Path(os.environ["ARM_GCC_BIN"]))
    candidates.extend(DEFAULT_TOOLCHAIN_DIRS)
    for cand in candidates:
        for name in ("arm-none-eabi-gcc.exe", "arm-none-eabi-gcc"):
            if (cand / name).is_file():
                return Toolchain(cand)
    # last resort: PATH
    exe = shutil.which("arm-none-eabi-gcc")
    if exe:
        return Toolchain(Path(exe).parent)
    raise BuildError(
        "arm-none-eabi-gcc not found. Install the ARM toolchain and pass "
        "--toolchain <dir-with-arm-none-eabi-gcc> or set ARM_GCC_BIN.\n"
        "Tried: %s" % ", ".join(str(c) for c in candidates + [Path("<PATH>")]))


# ---------------------------------------------------------------------------
# build description
# ---------------------------------------------------------------------------

class CompileUnit:
    """one object file to produce"""

    __slots__ = ("src", "src_rel", "obj", "dep", "kind", "extra", "target")

    def __init__(self, src: Path, src_rel: str, obj: Path, kind: str, extra, target: str):
        self.src = src
        self.src_rel = src_rel
        self.obj = obj
        self.dep = obj.with_suffix(".d")
        self.kind = kind            # 'cc' or 'cxx'
        self.extra = list(extra)
        self.target = target


class BuildPlan:
    """everything that is needed for one target (firmware or bootloader)"""

    def __init__(self, name: str, linker_script: str, optflags, defines, objects, includes):
        self.name = name
        self.linker_script = linker_script
        self.optflags = list(optflags)
        self.defines = list(defines)
        self.objects = list(objects)      # ordered exactly as in the Makefile
        self.includes = list(includes)


def _abs_sources(root: Path, rel_paths, what: str) -> list:
    result = []
    for rel in rel_paths:
        rel = rel.strip()
        if not rel:
            continue
        abs_path = (root / rel).resolve()
        if not abs_path.is_file():
            raise BuildError("%s: source file listed in the *.mak files does not exist: %s"
                             % (what, rel))
        result.append(abs_path)
    return result


def make_units(build_dir: Path, target: str, sources, extra_for_dsplib=None) -> list:
    units = []
    extra_for_dsplib = extra_for_dsplib or {}
    for src in sources:
        src_rel = src.relative_to(ECLIPSE_DIR).as_posix()
        suffix = src.suffix.lower()
        if suffix == ".c":
            kind = "cc"
        elif suffix in (".cpp", ".cxx"):
            kind = "cxx"
        elif suffix == ".s":
            kind = "cc"           # .S / .s -> assembler with preprocessor, compiled by gcc
        else:
            raise BuildError("unsupported source type: %s" % src_rel)
        obj = build_dir / "obj" / target / (src_rel + ".o")
        units.append(CompileUnit(src, src_rel, obj, kind, extra_for_dsplib.get(src_rel, ()), target))
    return units


# ---------------------------------------------------------------------------
# builder
# ---------------------------------------------------------------------------

class Builder:
    def __init__(self, args):
        self.args = args
        self.build_dir = Path(args.build_dir).resolve() if args.build_dir else DEFAULT_BUILD_DIR
        self.toolchain = find_toolchain(args.toolchain)
        self.jobs = args.jobs or min(32, (os.cpu_count() or 4))
        self.verbose = args.verbose
        self.tty = sys.stdout.isatty()
        self.warnings = []           # (target, source, line, text)
        self._compile_commands = []

    # -- console ------------------------------------------------------------
    def info(self, message: str):
        print("[build] " + message, flush=True)

    def target_info(self, target: str, message: str):
        print("[%s] %s" % (target, message), flush=True)

    def error(self, message: str):
        print("[build] ERROR: " + message, file=sys.stderr, flush=True)

    # -- description --------------------------------------------------------
    def read_descriptions(self):
        vars_f4 = read_make_vars([
            ECLIPSE_DIR / "include.mak",
            ECLIPSE_DIR / "files.mak",
            ECLIPSE_DIR / "bootloader.mak",
            ECLIPSE_DIR / "f4-include.mak",
            ECLIPSE_DIR / "f4-files.mak",
            ECLIPSE_DIR / "f4-bootloader.mak",
        ])
        self.mak_vars = vars_f4
        self.subdirs = vars_f4.get("SUBDIRS", "").split()
        self.hal_subdirs = vars_f4.get("HAL_SUBDIRS", "").split()
        self.include_flags = []
        for d in self.subdirs + self.hal_subdirs:
            self.include_flags.append("-I" + str((ECLIPSE_DIR / d)))

        def sources(name):
            return _abs_sources(ECLIPSE_DIR, vars_f4.get(name, "").split(), name)

        self.src_main = sources("SRC")
        self.src_hal = sources("HAL_SRC")
        self.src_dsplib = sources("DSPLIB_SRC")
        self.src_bl = sources("BL_SRC")
        self.src_bl_hal = sources("BL_HAL_SRC")

        dsplib_extra = {u: DSPLIB_EXTRA_CFLAGS for u in
                        [s.relative_to(ECLIPSE_DIR).as_posix() for s in self.src_dsplib]}

        # firmware: order matters, the Makefile links $(HAL_OBJS) $(DSPLIB_OBJS) $(OBJS),
        # startup_stm32f407xx.S must be the very first object
        fw_units = (make_units(self.build_dir, "firmware", self.src_hal, {})
                    + make_units(self.build_dir, "firmware", self.src_dsplib, dsplib_extra)
                    + make_units(self.build_dir, "firmware", self.src_main, {}))
        bl_units = (make_units(self.build_dir, "bootloader", self.src_bl_hal, {})
                    + make_units(self.build_dir, "bootloader", self.src_bl, {}))

        self.plans = {
            "firmware": BuildPlan(
                name="firmware",
                linker_script=LINKER_SCRIPT_FIRMWARE,
                optflags=["-O2"],
                defines=[],
                objects=fw_units,
                includes=self.include_flags),
            "bootloader": BuildPlan(
                name="bootloader",
                linker_script=LINKER_SCRIPT_BOOTLOADER,
                optflags=["-Os", "-DBOOTLOADER_BUILD"],
                defines=[],
                objects=bl_units,
                includes=self.include_flags),
        }
        return self.plans

    # -- flags --------------------------------------------------------------
    def compile_flags(self, plan: BuildPlan) -> list:
        return (MACHFLAGS_F4 + COMPILEFLAGS_BASE + plan.optflags + plan.defines)

    def link_flags(self, plan: BuildPlan) -> list:
        flags = list(LDFLAGS_BASE) + MACHFLAGS_F4
        flags.append("-Xlinker")
        flags.append("--gc-sections")
        libs_dir = ECLIPSE_DIR / "libs"
        flags.append("-L" + str(libs_dir))
        flags.append("-T" + str(LINKER_DIR / plan.linker_script))
        return flags

    def compile_cmd(self, plan: BuildPlan, unit: CompileUnit) -> list:
        compiler = self.toolchain.gcc if unit.kind == "cc" else self.toolchain.gxx
        std = "-std=gnu11" if unit.kind == "cc" else "-std=gnu++11"
        cmd = [str(compiler)] + self.compile_flags(plan) + unit.extra + [std] + \
              plan.includes + ["-c", str(unit.src), "-o", str(unit.obj),
                               "-MMD", "-MP", "-MF", str(unit.dep)]
        return cmd

    # -- incremental build helpers -----------------------------------------
    @staticmethod
    def _make_words(text: str) -> list:
        r"""split a make dependency list into words

        gcc writes bare '\' as a path separator but escapes a space inside a
        path as '\ ' (e.g. "E:\harnet\sdr\ 11\..."), so a plain .split()
        tears Windows paths apart."""
        words = []
        cur = ""
        i = 0
        while i < len(text):
            ch = text[i]
            if ch == "\\" and i + 1 < len(text):
                nxt = text[i + 1]
                if nxt.isspace() or nxt in "\\#":
                    cur += nxt          # escaped character, e.g. a space in a path
                else:
                    cur += ch + nxt     # ordinary Windows path separator
                i += 2
                continue
            if ch.isspace():
                if cur:
                    words.append(cur)
                    cur = ""
            else:
                cur += ch
            i += 1
        if cur:
            words.append(cur)
        return words

    def _depfile_deps(self, dep: Path) -> list:
        if not dep.is_file():
            return []
        try:
            text = dep.read_text(encoding="utf-8", errors="replace")
        except OSError:
            return []
        # [build fix] the dependency file starts with a Windows path
        # ("E:\...\foo.c.o: ..."), so a simple partition(":") would cut inside
        # the drive letter and silently lose every header dependency.  The
        # target always ends with the object suffix, cut there instead.
        # The file may use CRLF, and -MP appends "header.h:" phony target
        # lines after the real rule - normalise the EOLs, join the continuation
        # lines and keep only the first (dependency) line.
        text = text.replace("\r\n", "\n").replace("\r", "\n")
        text = text.replace("\\\n", " ")
        marker = ".o:"
        pos = text.find(marker)
        if pos < 0:
            return []
        rhs = text[pos + len(marker):]
        nl = rhs.find("\n")
        if nl >= 0:
            rhs = rhs[:nl]
        result = []
        for token in self._make_words(rhs):
            token = token.strip().replace("\\", "/")
            if not token:
                continue
            result.append(Path(token))
        return result

    def _needs_rebuild(self, unit: CompileUnit) -> bool:
        if not unit.obj.is_file():
            return True
        obj_mtime = unit.obj.stat().st_mtime
        if unit.src.stat().st_mtime > obj_mtime:
            return True
        for dep in self._depfile_deps(unit.dep):
            try:
                if dep.is_file() and dep.stat().st_mtime > obj_mtime:
                    return True
            except OSError:
                return True
        return False

    def _flags_stamp(self, plan: BuildPlan) -> Path:
        return self.build_dir / ("%s.flags.json" % plan.name)

    def _check_flags_stamp(self, plan: BuildPlan) -> bool:
        """returns True when the object files were built with other flags"""
        stamp = self._flags_stamp(plan)
        payload = {
            "compile": self.compile_flags(plan),
            "link": self.link_flags(plan),
            "libs": LIBS,
            "includes": self.include_flags,
            "toolchain": str(self.toolchain.bindir),
            "compiler_version": self.toolchain.version(),
        }
        current = json.dumps(payload, sort_keys=True)
        if stamp.is_file() and stamp.read_text(encoding="utf-8") == current:
            return False
        stamp.parent.mkdir(parents=True, exist_ok=True)
        stamp.write_text(current, encoding="utf-8")
        return True

    # -- compilation --------------------------------------------------------
    def _run(self, cmd: list) -> subprocess.CompletedProcess:
        return subprocess.run(cmd, capture_output=True, text=True, errors="replace")

    def compile_all(self, plan: BuildPlan, force: bool):
        units = plan.objects
        todo = [u for u in units if force or self._needs_rebuild(u)]
        for unit in units:
            unit.obj.parent.mkdir(parents=True, exist_ok=True)
            self._compile_commands.append({
                "directory": str(ECLIPSE_DIR),
                "arguments": self.compile_cmd(plan, unit),
                "file": str(unit.src),
                "output": str(unit.obj),
            })
        done = 0
        skip = len(units) - len(todo)
        self.target_info(plan.name, "compiling %d units (%d already up to date)"
                         % (len(todo), skip))
        if not todo:
            return
        start = time.time()
        errors = []
        lock_tty = self.tty and not self.verbose

        def worker(unit: CompileUnit):
            proc = self._run(self.compile_cmd(plan, unit))
            return unit, proc

        with ThreadPoolExecutor(max_workers=self.jobs) as pool:
            futures = [pool.submit(worker, u) for u in todo]
            for future in futures:
                unit, proc = future.result()
                done += 1
                if proc.returncode != 0:
                    errors.append((unit, proc))
                else:
                    for line in (proc.stderr or "").splitlines():
                        if ": warning:" in line or ": error:" in line:
                            self.warnings.append((plan.name, unit.src_rel, line.strip()))
                if self.verbose:
                    self.target_info(plan.name, "[%4d/%4d] %s %s"
                                     % (done, len(todo), "CC" if unit.kind == "cc" else "CXX",
                                        unit.src_rel))
                elif lock_tty:
                    sys.stdout.write("\r[%s] compiling %d/%d ..." % (plan.name, done, len(todo)))
                    sys.stdout.flush()
                if errors:
                    for other in futures:
                        other.cancel()
                    break
        if lock_tty:
            sys.stdout.write("\r")
            sys.stdout.flush()
        elapsed = time.time() - start
        if errors:
            unit, proc = errors[0]
            cmd = " ".join(self.compile_cmd(plan, unit))
            raise BuildError("compilation failed for %s\n  command: %s\n%s"
                             % (unit.src_rel, cmd, (proc.stderr or proc.stdout).strip()))
        self.target_info(plan.name, "compiled %d units in %.1f s" % (len(todo), elapsed))

    # -- linking ------------------------------------------------------------
    def _response_file(self, path: Path, arguments) -> Path:
        """write a GCC response file (@file) - a Windows command line is limited
        to 32767 characters and 526 object files do not fit into it.

        NOTE: GCC expands @files like a shell, i.e. a backslash is an escape
        character, therefore all Windows paths are written with forward slashes
        (which gcc/ld accept on Windows as well)."""
        path.parent.mkdir(parents=True, exist_ok=True)
        lines = []
        for arg in arguments:
            arg = arg.replace("\\", "/")
            if not arg or re.search(r'[\s"]', arg):
                lines.append('"' + arg.replace('"', '\\"') + '"')
            else:
                lines.append(arg)
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return path

    def link(self, plan: BuildPlan) -> Path:
        elf = self.build_dir / (FIRMWARE_NAME + ".elf") if plan.name == "firmware" \
            else self.build_dir / (BOOTLOADER_NAME + ".elf")
        mapfile = elf.with_suffix(".map")
        objects = [str(u.obj) for u in plan.objects]
        prefix = [str(self.toolchain.gxx)] + self.link_flags(plan) + \
                 ["-Wl,-Map," + str(mapfile), "-o", str(elf)]
        if sum(len(a) + 1 for a in prefix + objects + LIBS) > 20000:
            # Windows: "The filename or extension is too long" above ~32k characters
            rsp = self._response_file(self.build_dir / (plan.name + ".link.rsp"),
                                      objects + LIBS)
            cmd = prefix + ["@" + str(rsp)]
        else:
            cmd = prefix + objects + LIBS
        self.target_info(plan.name, "linking %s" % elf.name)
        proc = self._run(cmd)
        for line in (proc.stderr or "").splitlines():
            if line.strip():
                print("  " + line.strip())
                if "warning:" in line:
                    self.warnings.append((plan.name, "link", line.strip()))
        if proc.returncode != 0:
            raise BuildError("link failed for %s\n  command: %s\n%s"
                             % (elf.name, " ".join(cmd), proc.stderr.strip()))
        return elf

    def make_binary(self, plan: BuildPlan, elf: Path) -> Path:
        binary = elf.with_suffix(".bin")
        cmd = [str(self.toolchain.objcopy), "-O", "binary", str(elf), str(binary)]
        self.target_info(plan.name, "objcopy -> %s" % binary.name)
        proc = self._run(cmd)
        if proc.returncode != 0:
            raise BuildError("objcopy failed for %s\n%s" % (elf.name, proc.stderr.strip()))
        # the Makefile also produces .hex (and a .dfu derived from it); the .dfu step
        # needs python2 + intelhex, so only the Intel HEX file is generated here
        hexfile = elf.with_suffix(".hex")
        proc = self._run([str(self.toolchain.objcopy), "-O", "ihex", str(elf), str(hexfile)])
        if proc.returncode == 0:
            self.target_info(plan.name, "objcopy -> %s" % hexfile.name)
        return binary

    # -- size report --------------------------------------------------------
    def elf_symbols(self, elf: Path) -> dict:
        proc = self._run([str(self.toolchain.nm), str(elf)])
        symbols = {}
        if proc.returncode != 0:
            return symbols
        for line in proc.stdout.splitlines():
            parts = line.split()
            if len(parts) == 3:
                addr, kind, name = parts
                try:
                    symbols[name] = int(addr, 16)
                except ValueError:
                    pass
        return symbols

    def size_report(self, plan: BuildPlan, elf: Path) -> dict:
        proc = self._run([str(self.toolchain.size), str(elf)])
        if proc.stdout:
            lines = proc.stdout.strip().splitlines()
            for line in lines:
                print("  " + line)
        sym = self.elf_symbols(elf)
        info = {"elf": str(elf)}
        if plan.name == "firmware":
            flash_origin, flash_len = FW_FLASH_ORIGIN, FW_FLASH_LENGTH
            ram_origin, ram_len = FW_RAM_ORIGIN, FW_RAM_LENGTH
            ccm_origin, ccm_len = FW_CCM_ORIGIN, FW_CCM_LENGTH
        else:
            flash_origin, flash_len = BL_FLASH_ORIGIN, BL_FLASH_LENGTH
            ram_origin, ram_len = FW_RAM_ORIGIN, FW_RAM_LENGTH
            ccm_origin, ccm_len = FW_CCM_ORIGIN, FW_CCM_LENGTH
        try:
            etext = sym["__etext"]
            data_start = sym["__data_start__"]
            data_end = sym["__data_end__"]
            bss_start = sym["__bss_start__"]
            bss_end = sym["__bss_end__"]
            flash_used = (etext - flash_origin) + (data_end - data_start)
            # .data is located at the very start of RAM, so __bss_end__ - origin
            # already covers .data + .bss (do not add .data a second time)
            ram_used = bss_end - ram_origin
            ccm_used = sym.get("_eccmram", ccm_origin) - sym.get("_sccmram", ccm_origin)
            info.update({
                "flash_origin": flash_origin,
                "flash_size": flash_len,
                "flash_used": flash_used,
                "flash_free": flash_len - flash_used,
                "ram_origin": ram_origin,
                "ram_size": ram_len,
                "ram_used": ram_used,
                "ram_free": ram_len - ram_used,
                "ccm_origin": ccm_origin,
                "ccm_size": ccm_len,
                "ccm_used": ccm_used,
                "stack_top": sym.get("_estack"),
                "text": etext - flash_origin,
                "data": data_end - data_start,
                "bss": sym["__bss_end__"] - sym["__bss_start__"],
            })
            self.target_info(plan.name, "FLASH %d / %d bytes used (%.1f%%), %d bytes free"
                             % (flash_used, flash_len, 100.0 * flash_used / flash_len,
                                flash_len - flash_used))
            self.target_info(plan.name, "RAM   %d / %d bytes static (%.1f%%), %d bytes left "
                             "for stack (CCM %d/%d)"
                             % (ram_used, ram_len, 100.0 * ram_used / ram_len,
                                ram_len - ram_used, ccm_used, ccm_len))
            if flash_used > flash_len:
                raise BuildError("%s does not fit into flash (%d > %d bytes)"
                                 % (plan.name, flash_used, flash_len))
        except KeyError as exc:
            self.warning_note("cannot compute size for %s: missing symbol %s" % (elf.name, exc))
        return info

    def warning_note(self, message: str):
        self.warnings.append(("build", "", "note: " + message))

    # -- targets ------------------------------------------------------------
    def build_target(self, name: str) -> dict:
        plan = self.plans[name]
        force = self.args.force or self._check_flags_stamp(plan)
        if force and not self.args.force:
            self.target_info(name, "build flags changed -> full rebuild")
        self.compile_all(plan, force)
        elf = self.link(plan)
        binary = self.make_binary(plan, elf)
        sizes = self.size_report(plan, elf)
        sizes["bin"] = str(binary)
        sizes["bin_size"] = binary.stat().st_size
        self.target_info(name, "binary %s: %d bytes" % (binary.name, binary.stat().st_size))
        return sizes

    def write_compile_commands(self):
        path = self.build_dir / "compile_commands.json"
        self.build_dir.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(self._compile_commands, indent=2), encoding="utf-8")
        self.info("compile_commands.json written (%d entries)" % len(self._compile_commands))

    def write_warnings_log(self):
        if not self.warnings:
            return
        path = self.build_dir / "warnings.log"
        lines = ["%s: %s" % (src, text) if src else text for _, src, text in self.warnings]
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        self.info("%d warning(s), see %s" % (len(self.warnings), path))

    def clean(self):
        if self.build_dir.exists():
            shutil.rmtree(self.build_dir)
            self.info("removed %s" % self.build_dir)
        else:
            self.info("nothing to clean (%s does not exist)" % self.build_dir)

    def run(self) -> int:
        self.info("project root : %s" % PROJECT_ROOT)
        self.info("toolchain    : %s (%s)" % (self.toolchain.bindir, self.toolchain.version()))
        self.info("build dir    : %s" % self.build_dir)
        self.read_descriptions()
        self.info("sources      : firmware %d objects (HAL %d + DSP %d + src %d), "
                   "bootloader %d objects"
                   % (len(self.plans["firmware"].objects), len(self.src_hal), len(self.src_dsplib),
                      len(self.src_main), len(self.plans["bootloader"].objects)))
        self.build_dir.mkdir(parents=True, exist_ok=True)

        targets = self.args.target
        results = {}
        start = time.time()
        for target in targets:
            if target == "firmware":
                self.info("=== firmware (BUILDFOR=F4, -O2, %s) ===" % LINKER_SCRIPT_FIRMWARE)
                results["firmware"] = self.build_target("firmware")
            elif target == "bootloader":
                self.info("=== bootloader (BUILDFOR=F4, -Os -DBOOTLOADER_BUILD, %s) ==="
                          % LINKER_SCRIPT_BOOTLOADER)
                results["bootloader"] = self.build_target("bootloader")
            elif target == "clean":
                self.clean()
                return 0
            else:
                raise BuildError("unknown target: %s" % target)
        self.write_compile_commands()
        self.write_warnings_log()
        elapsed = time.time() - start
        self.info("done in %.1f s" % elapsed)
        summary_path = self.build_dir / "build-summary.json"
        summary_path.write_text(json.dumps(results, indent=2), encoding="utf-8")
        return 0


# ---------------------------------------------------------------------------
# command line
# ---------------------------------------------------------------------------

def parse_args(argv):
    parser = argparse.ArgumentParser(
        prog="build.py",
        description="Reproducible CLI build of the UHSDR/RS-918 firmware (F4, 1 MB flash).",
        epilog="targets: all | firmware | bootloader | clean")
    parser.add_argument("target", nargs="*", default=["all"],
                        choices=["all", "firmware", "bootloader", "clean"],
                        help="what to build (default: all)")
    parser.add_argument("--jobs", "-j", type=int, default=0,
                        help="parallel compiler jobs (default: CPU count)")
    parser.add_argument("--toolchain", "-t", default=None,
                        help=r"directory containing arm-none-eabi-gcc (default: E:\tools\gcc-arm\bin)")
    parser.add_argument("--build-dir", default=None, help="artifact directory (default: <repo>/build)")
    parser.add_argument("--force", "-f", action="store_true", help="rebuild everything")
    parser.add_argument("--verbose", "-v", action="store_true", help="show every compiler call")
    args = parser.parse_args(argv)
    targets = []
    for t in args.target:
        if t == "all":
            targets.extend(["firmware", "bootloader"])
        else:
            targets.append(t)
    # keep the order firmware -> bootloader, drop duplicates
    seen = set()
    args.target = [t for t in targets if not (t in seen or seen.add(t))]
    return args


def main(argv) -> int:
    args = parse_args(argv)
    try:
        builder = Builder(args)
        return builder.run()
    except BuildError as exc:
        print("[build] ERROR: %s" % exc, file=sys.stderr, flush=True)
        return 2
    except KeyboardInterrupt:
        print("[build] interrupted", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

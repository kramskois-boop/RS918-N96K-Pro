#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""cparse.py - tiny, dependency-free C source helpers used by the host tests.

N96K P2 (CI/host tests): the firmware DSP tables are plain C initializers, so a
full C parser is overkill.  This module provides just enough:

    strip_comments()        remove // and /* */ comments, keep line numbers
    apply_preprocessor()    blank out disabled "#if 0" regions, keep line numbers
    parse_defines()         collect object-like #define NAME value
    eval_int()              evaluate a small integer constant expression
    find_initializers()     locate "TYPE name = { ... }" / "TYPE name[] = { ... }"
    brace_body()            return the text between matching braces
    split_top_level()       split an initializer body on top-level commas
    parse_designated()      turn ".field = value, ..." into a dict
    count_elements()        count top-level elements of an initializer body
    parse_float_tokens()    extract float literals, flag unresolved tokens
    find_array_definitions() locate "const float32_t name[dim] = {...}"

The helpers deliberately ignore anything they do not understand; the checks in
check_*.py report it, so a new exotic construct shows up as a test failure with
a file:line pointer instead of silently passing.
"""

from __future__ import annotations

import ast
import re

# ---------------------------------------------------------------------------
# source text helpers
# ---------------------------------------------------------------------------


def read_text(path):
    """Read a C source file.  The tree is mixed UTF-8/Latin-1 (Nizar comments)."""
    with open(path, "rb") as fh:
        raw = fh.read()
    return raw.decode("utf-8", "replace")


def strip_comments(text):
    """Replace C comments with spaces, preserving newlines (=> line numbers)."""
    out = []
    i = 0
    n = len(text)
    state = "code"
    while i < n:
        ch = text[i]
        if state == "code":
            if ch == "/" and i + 1 < n and text[i + 1] == "/":
                state = "line"
                out.append(" ")
                i += 1
                continue
            if ch == "/" and i + 1 < n and text[i + 1] == "*":
                state = "block"
                out.append(" ")
                i += 1
                continue
            if ch == '"':
                state = "string"
            elif ch == "'":
                state = "char"
            out.append(ch)
            i += 1
            continue
        if state == "line":
            if ch == "\n":
                state = "code"
                out.append("\n")
            else:
                out.append(" ")
            i += 1
            continue
        if state == "block":
            if ch == "*" and i + 1 < n and text[i + 1] == "/":
                state = "code"
                out.append(" ")
                i += 1
            elif ch == "\n":
                out.append("\n")
            else:
                out.append(" ")
            i += 1
            continue
        # string / char literal
        out.append(ch)
        if ch == "\\" and i + 1 < n:
            out.append(text[i + 1])
            i += 2
            continue
        if (state == "string" and ch == '"') or (state == "char" and ch == "'"):
            state = "code"
        i += 1
    return "".join(out)


_COND_RE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")


def _blank(line):
    return "" if not line.strip() else " " * len(line)


def apply_preprocessor(text, defines=None):
    """Blank out disabled preprocessor regions, preserving newlines.

    Only the simple forms used by this firmware are evaluated:
      #if 0 / #if 1          -> known
      #ifdef KNOWN_MACRO     -> known True when the macro is defined
      anything else          -> treated as enabled (the build defines USE_* etc.)
    The result keeps #define lines, so parse_defines() can still use them.
    """
    defines = defines or {}
    lines = text.split("\n")
    stack = []          # level: {"parent": bool, "cond": bool, "else": bool}
    out = []
    for line in lines:
        m = _COND_RE.match(line)
        if not m:
            active = all(level["parent"] and level["cond"] for level in stack)
            out.append(line if active else _blank(line))
            continue
        directive, rest = m.group(1), m.group(2).strip()
        if directive in ("if", "ifdef", "ifndef"):
            parent = all(level["parent"] and level["cond"] for level in stack)
            if directive == "if":
                value = eval_int(rest, defines)
                cond = True if value is None else bool(value)
            elif directive == "ifdef":
                # both known and unknown macros are assumed enabled: the firmware
                # build defines its feature switches (USE_FREEDV, ...), so the
                # guarded table is the one that gets compiled
                cond = True
            else:  # ifndef
                cond = False if rest in defines else True
            stack.append({"parent": parent, "cond": cond, "else": False})
        elif directive in ("else", "elif"):
            if not stack:
                out.append(_blank(line))
                continue
            level = stack[-1]
            level["else"] = True
            if directive == "elif":
                value = eval_int(rest, defines)
                level["cond"] = not level["cond"] and (True if value is None else bool(value))
            else:
                level["cond"] = not level["cond"]
        else:  # endif
            if stack:
                stack.pop()
        out.append(_blank(line))
    return "\n".join(out)


# ---------------------------------------------------------------------------
# preprocessor defines / integer expressions
# ---------------------------------------------------------------------------

_DEFINE_RE = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)(\([^)]*\))?[ \t]*(.*)$")


def parse_defines(text):
    """Collect object-like #define NAME value from (comment stripped) text."""
    defines = {}
    pending = ""
    for line in text.split("\n"):
        if pending:
            line = pending + " " + line.strip()
            pending = ""
        if line.rstrip().endswith("\\"):
            pending = line.rstrip()[:-1]
            continue
        m = _DEFINE_RE.match(line)
        if not m or m.group(2):     # skip function-like macros
            continue
        defines[m.group(1)] = m.group(3).strip()
    return defines


_CAST_RE = re.compile(r"\(\s*(?:const\s+)?(?:u?int\d+_t|float32_t|float|double|int|long|unsigned|signed|char|size_t|bool)\s*\*?\s*\)")
_HEX_RE = re.compile(r"^[+-]?0[xX][0-9a-fA-F]+[uUlL]*$")
_DEC_RE = re.compile(r"^[+-]?\d+[uUlL]*$")
_FLOAT_RE = re.compile(r"^[+-]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?[fFlL]?$")
_SAFE_EXPR = re.compile(r"^[0-9a-fA-FxXbBuUlL\s+\-*/%()<>|&~^=!:.,]*$")


def _strip_int_suffix(token):
    token = token.strip()
    return re.sub(r"[uUlL]+$", "", token)


def eval_int(expr, defines, depth=0):
    """Evaluate a small integer constant expression; returns int or None."""
    if depth > 12:
        return None
    expr = (expr or "").strip()
    if not expr:
        return None
    # drop comments that survived (defines may carry them)
    expr = strip_comments(expr).strip()
    if not expr:
        return None
    expr = _CAST_RE.sub(" ", expr).strip()
    while expr.startswith("(") and expr.endswith(")"):
        inner = expr[1:-1]
        if inner.count("(") != inner.count(")"):
            break
        expr = inner.strip()
    if not expr:
        return None
    if _HEX_RE.match(expr):
        try:
            return int(_strip_int_suffix(expr), 16)
        except ValueError:
            return None
    if _DEC_RE.match(expr):
        try:
            return int(_strip_int_suffix(expr))
        except ValueError:
            return None
    ident = re.match(r"^([A-Za-z_]\w*)$", expr)
    if ident:
        name = ident.group(1)
        if name in defines:
            return eval_int(defines[name], defines, depth + 1)
        return None
    # substitute all known object-like macros, then evaluate arithmetic
    def repl(match):
        name = match.group(0)
        if name in defines:
            value = eval_int(defines[name], defines, depth + 1)
            if value is None:
                raise ValueError(name)
            return str(value)
        raise ValueError(name)

    try:
        substituted = re.sub(r"\b[A-Za-z_]\w*\b", repl, expr)
    except ValueError:
        return None
    if not _SAFE_EXPR.match(substituted):
        return None
    try:
        node = ast.parse(substituted, mode="eval")
        return int(_eval_ast(node))
    except (SyntaxError, ValueError, TypeError, ZeroDivisionError):
        return None


def _eval_ast(node):
    """Evaluate a whitelisted integer arithmetic AST (no names, no calls)."""
    if isinstance(node, ast.Expression):
        return _eval_ast(node.body)
    if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)):
        return int(node.value)
    if isinstance(node, ast.UnaryOp):
        value = _eval_ast(node.operand)
        if isinstance(node.op, ast.USub):
            return -value
        if isinstance(node.op, ast.UAdd):
            return value
        if isinstance(node.op, ast.Invert):
            return ~value
        raise ValueError("unary operator")
    if isinstance(node, ast.BinOp):
        left = _eval_ast(node.left)
        right = _eval_ast(node.right)
        op = node.op
        if isinstance(op, ast.Add):
            return left + right
        if isinstance(op, ast.Sub):
            return left - right
        if isinstance(op, ast.Mult):
            return left * right
        if isinstance(op, ast.Div):
            return int(left / right)
        if isinstance(op, ast.FloorDiv):
            return left // right
        if isinstance(op, ast.Mod):
            return left % right
        if isinstance(op, ast.LShift):
            return left << right
        if isinstance(op, ast.RShift):
            return left >> right
        if isinstance(op, ast.BitOr):
            return left | right
        if isinstance(op, ast.BitAnd):
            return left & right
        if isinstance(op, ast.BitXor):
            return left ^ right
        raise ValueError("binary operator")
    raise ValueError("unsupported expression")


# ---------------------------------------------------------------------------
# brace matching / initializers
# ---------------------------------------------------------------------------


def brace_body(text, open_index):
    """Given the index of a '{', return (body, index_after_closing_brace)."""
    assert text[open_index] == "{"
    depth = 0
    i = open_index
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return text[open_index + 1:i], i + 1
        i += 1
    raise ValueError("unbalanced braces")


def split_top_level(body, separator=","):
    """Split an initializer body on separators that are not nested."""
    parts = []
    start = 0
    depth = 0
    i = 0
    n = len(body)
    while i < n:
        ch = body[i]
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == separator and depth == 0:
            parts.append(body[start:i])
            start = i + 1
        i += 1
    parts.append(body[start:])
    return parts


def count_elements(body):
    """Count top-level comma separated elements; None for an empty initializer."""
    body = body.strip()
    if not body:
        return 0
    parts = [p.strip() for p in split_top_level(body)]
    parts = [p for p in parts if p]
    return len(parts)


_INIT_RE = re.compile(
    r"\b(?P<type>arm_fir_instance_f32|arm_fir_decimate_instance_f32|"
    r"arm_fir_interpolate_instance_f32|arm_iir_lattice_instance_f32|"
    r"arm_biquad_casd_df1_inst_f32|IQ_FilterDescriptor)\s+"
    r"(?P<name>[A-Za-z_]\w*)\s*(?P<array>\[[^\]]*\])?\s*=\s*\{"
)


def find_initializers(text, type_names=None):
    """Find all "TYPE name = { ... }" initializers (designated or aggregate)."""
    result = []
    for match in _INIT_RE.finditer(text):
        type_name = match.group("type")
        if type_names is not None and type_name not in type_names:
            continue
        try:
            body, end = brace_body(text, match.end() - 1)
        except ValueError:
            continue
        result.append({
            "type": type_name,
            "name": match.group("name"),
            "is_array": bool(match.group("array")),
            "offset": match.start(),
            "body": body,
            "end": end,
        })
    return result


_DESIGNATOR_RE = re.compile(r"^\s*\.\s*([A-Za-z_]\w*)\s*=\s*(.*)$", re.S)


def parse_designated(body):
    """Parse ".field = value, ..." into {field: value_text}."""
    fields = {}
    generic = []
    for chunk in split_top_level(body):
        chunk = chunk.strip()
        if not chunk:
            continue
        m = _DESIGNATOR_RE.match(chunk)
        if m:
            fields[m.group(1)] = m.group(2).strip()
        else:
            generic.append(chunk)
    if generic and not fields:
        return None
    fields["__generic__"] = generic
    return fields


_COMPOUND_LITERAL_RE = re.compile(r"\{")
_ARRAY_CAST_RE = re.compile(r"^\(\s*(?:const\s+)?[A-Za-z_][\w\s*\]]*\[\s*\]\s*\)\s*")
_PTR_CAST_RE = re.compile(r"^\(\s*(?:const\s+)?[A-Za-z_][\w\s*]*\*?\s*\)\s*")


def _strip_casts(value):
    value = value.strip()
    while True:
        new = _ARRAY_CAST_RE.sub("", value, count=1)
        if new != value:
            value = new.strip()
            continue
        new = _PTR_CAST_RE.sub("", value, count=1)
        if new != value:
            value = new.strip()
            continue
        return value


def compound_literal_body(value):
    """Return the { ... } body of a compound literal, or None."""
    value = value.strip()
    idx = value.find("{")
    if idx < 0:
        return None
    try:
        body, end = brace_body(value, idx)
    except ValueError:
        return None
    if value[end:].strip():
        return None         # trailing junk, not a plain compound literal
    return body


def pointer_target(value):
    """Best-effort extraction of the array name a pointer field points at."""
    value = _strip_casts(value)
    if value in ("NULL", "0", "{}"):
        return None
    m = re.match(r"^&?\s*([A-Za-z_]\w*)\s*$", value)
    if m:
        return m.group(1)
    return None


_FLOAT_TOKEN_RE = re.compile(r"^[+-]?(?:(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?)[fFlL]?$")


def parse_float_tokens(body):
    """Return (values, unresolved) for an initializer body of float literals."""
    values = []
    unresolved = []
    for chunk in split_top_level(body):
        token = chunk.strip()
        if not token:
            continue
        token = _CAST_RE.sub(" ", token).strip()
        if not token:
            continue
        m = _FLOAT_TOKEN_RE.match(token)
        if not m:
            unresolved.append(token)
            continue
        text = re.sub(r"[fFlL]$", "", token)
        try:
            values.append(float(text))
        except ValueError:
            unresolved.append(token)
    return values, unresolved


# ---------------------------------------------------------------------------
# array definitions
# ---------------------------------------------------------------------------

_ARRAY_DEF_RE = re.compile(
    r"\b(?:const\s+|static\s+|volatile\s+|__MCHF_SPECIALMEM\s+)*"
    r"(?:float32_t|float|double|int32_t|uint32_t|int16_t|uint16_t|int8_t|uint8_t|q15_t|q31_t|q7_t)\s+"
    r"(?:__MCHF_SPECIALMEM\s+)?(?P<name>[A-Za-z_]\w*)\s*\[\s*(?P<dim>[^\]]*)\]\s*=\s*\{"
)


def find_array_definitions(text):
    """Find "type name[dim] = { ... }" definitions."""
    arrays = []
    for match in _ARRAY_DEF_RE.finditer(text):
        try:
            body, end = brace_body(text, match.end() - 1)
        except ValueError:
            continue
        arrays.append({
            "name": match.group("name"),
            "dim": match.group("dim").strip(),
            "offset": match.start(),
            "body": body,
            "end": end,
        })
    return arrays


_ARRAY_DECL_RE = re.compile(
    r"\b(?:const\s+|static\s+|volatile\s+)*"
    r"(?:float32_t|float|int32_t|uint32_t|int16_t|uint16_t|uint8_t)\s+"
    r"(?:__MCHF_SPECIALMEM\s+)?(?P<name>[A-Za-z_]\w*)\s*\[\s*(?P<dim>[^\]]*)\]\s*;"
)


def find_array_declarations(text):
    """Find "type name[dim];" declarations without initializer."""
    return [{"name": m.group("name"), "dim": m.group("dim").strip(), "offset": m.start()}
            for m in _ARRAY_DECL_RE.finditer(text)]


def line_of(text, offset):
    """1-based line number of a character offset."""
    return text.count("\n", 0, offset) + 1


def describe(path, text, offset):
    """'file:line' for messages."""
    return "%s:%d" % (path.replace("\\", "/"), line_of(text, offset))

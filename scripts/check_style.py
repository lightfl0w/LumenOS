#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKIP_TOP = {"third_party", "build", ".git", "__pycache__"}

MAX_DEPTH = 3
MAX_FUNC_LINES = 120
MAX_COLUMNS = 100

STRICT = "--strict" in sys.argv
TOP = 10
if "--top" in sys.argv:
    try:
        TOP = int(sys.argv[sys.argv.index("--top") + 1])
    except (IndexError, ValueError):
        pass


def iter_c_sources():
    for p in sorted(ROOT.rglob("*.c")):
        if any(part in SKIP_TOP for part in p.relative_to(ROOT).parts):
            continue
        yield p


def strip_code(line: str) -> str:
    out = []
    in_str = None
    i = 0
    while i < len(line):
        c = line[i]
        if in_str:
            if c == "\\":
                i += 2
                continue
            if c == in_str:
                in_str = None
            i += 1
            continue
        if c in "\"'":
            in_str = c
            i += 1
            continue
        if c == "/" and i + 1 < len(line) and line[i + 1] == "/":
            break
        out.append(c)
        i += 1
    return "".join(out)


def strip_strings(text: str) -> str:
    return "\n".join(strip_code(l) for l in text.splitlines())


def find_functions(text: str):
    lines = text.splitlines()
    i = 0
    n = len(lines)
    while i < n:
        m = re.match(r"^[A-Za-z_].*\b(\w+)\s*\(", lines[i])
        if not m:
            i += 1
            continue

        j = i
        while j < n and "{" not in lines[j] and ";" not in lines[j]:
            j += 1
        if j >= n or ";" in lines[j]:
            i += 1
            continue
        name = m.group(1)
        depth = 0
        started = False
        maxd = 0
        k = j
        while k < n:
            for ch in lines[k]:
                if ch == "{":
                    depth += 1
                    started = True
                    maxd = max(maxd, depth)
                elif ch == "}":
                    depth -= 1
            if started and depth == 0:
                break
            k += 1
        yield name, i + 1, k + 1, maxd
        i = k + 1


def main() -> int:
    deep, longfn, longline, longcond = [], [], [], []

    for p in iter_c_sources():
        rel = str(p.relative_to(ROOT))
        raw = p.read_text(errors="replace")
        clean = strip_strings(raw)

        for i, line in enumerate(clean.splitlines(), 1):
            if len(line) > MAX_COLUMNS:
                longline.append((rel, i, len(line)))

        for name, start, end, depth in find_functions(clean):
            span = end - start + 1
            if depth > MAX_DEPTH:
                deep.append((rel, start, name, depth, span))
            if span > MAX_FUNC_LINES:
                longfn.append((rel, start, name, span))

        cond_open = 0
        for i, line in enumerate(raw.splitlines(), 1):
            t = strip_code(line)
            if not t.strip():
                continue
            opens = t.count("(")
            closes = t.count(")")
            if opens > closes and re.match(r"^\s*(if|while|for)\b", line):
                longcond.append((rel, i, line.strip()[:60]))

    total = len(deep) + len(longfn) + len(longline) + len(longcond)
    if total == 0:
        print("[style] OK: no readability backlogs")
        return 0

    def report(title, rows, fmt):
        if not rows:
            return
        print(f"[style] {len(rows)} {title}:")
        for r in rows[:TOP]:
            print("    " + fmt(r))
        if len(rows) > TOP:
            print(f"    ... and {len(rows) - TOP} more")

    report(
        "functions nested deeper than %d" % MAX_DEPTH,
        sorted(deep, key=lambda r: -r[3]),
        lambda r: f"{r[0]}:{r[1]} {r[2]}()  depth={r[3]} len={r[4]}",
    )
    report(
        "functions longer than %d lines" % MAX_FUNC_LINES,
        sorted(longfn, key=lambda r: -r[3]),
        lambda r: f"{r[0]}:{r[1]} {r[2]}()  {r[3]} lines",
    )
    report(
        "lines wider than %d columns" % MAX_COLUMNS,
        sorted(longline, key=lambda r: -r[2]),
        lambda r: f"{r[0]}:{r[1]}  {r[2]} cols",
    )
    report(
        "conditions wrapped across lines",
        sorted(longcond, key=lambda r: r[0]),
        lambda r: f"{r[0]}:{r[1]}  {r[2]}...",
    )

    print(f"[style] backlog total {total}; fix order: nesting, function length, columns")
    if STRICT:
        print("[style] --strict: failing on readability backlog")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
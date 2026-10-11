#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

LAYER = {
    "lib": 0,       
    "arch": 1,
    "mm": 2,
    "kernel": 3,
    "drivers": 4,
    "fs": 4,
    "net": 4,
    "user": 5,
}

ALLOW_LIST = {
    ("lib/rand/rand.c", "arch/cpu.h"),
    ("arch/x86_64/irq/interrupt.c", "kernel/sched/thread.h"),
    ("arch/x86_64/mm/paging.c", "kernel/sched/thread.h"),
    ("arch/x86_64/cpu/smp/smp.c", "kernel/sched/thread.h"),
    ("arch/x86_64/cpu/tss/tss.c", "kernel/sched/thread.h"),
}


def layer_of(inc: str) -> int:
    top = inc.split("/", 1)[0]
    return LAYER.get(top, -1) 


def file_layer(path: Path) -> int:
    for part in path.parts:
        if part in LAYER:
            return LAYER[part]
    return -1


INCLUDE_RE = re.compile(r'#\s*include\s*"([^"]+)"')


def main() -> int:
    errors = []
    warns = []
    scanned = 0
    for path in ROOT.rglob("*.c"):
        if "third_party" in path.parts or "build" in path.parts:
            continue
        fl = file_layer(path)
        if fl < 0:
            continue
        scanned += 1
        try:
            text = path.read_text(errors="replace")
        except OSError:
            continue
        for line in text.splitlines():
            m = INCLUDE_RE.match(line.strip())
            if not m:
                continue
            inc = m.group(1)
            il = layer_of(inc)
            if il < 0:
                continue
            if (str(path.relative_to(ROOT)), inc) in ALLOW_LIST:
                continue
            if fl == 0 and il > 0:
                errors.append((str(path), inc))
            elif fl == 1 and il >= 3:
                warns.append((str(path), inc))

    print(f"[layers] scanned {scanned} C files")
    if errors:
        print(f"[layers] ERROR: {len(errors)} base-layer (lib) upward includes "
              f"(must be cleaned; fatal only with --strict):")
        for p, inc in errors[:50]:
            print(f"    {p}  ->  {inc}")
        if len(errors) > 50:
            print(f"    ... and {len(errors) - 50} more")
    if warns:
        print(f"[layers] WARN: {len(warns)} arch->core/service/user includes "
              f"(cleanup backlog, non-fatal):")
        for p, inc in warns[:50]:
            print(f"    {p}  ->  {inc}")
        if len(warns) > 50:
            print(f"    ... and {len(warns) - 50} more")

    if "--strict" in sys.argv:
        if errors or warns:
            print("[layers] --strict: failing on layering violations")
            return 1
        print("[layers] OK (strict): no layering violations")
        return 0

    if errors:
        print("[layers] OK: violations present but non-fatal (use --strict to fail)")
    else:
        print("[layers] OK: no base-layer upward dependencies")
    return 0


if __name__ == "__main__":
    sys.exit(main())

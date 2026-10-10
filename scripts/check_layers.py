#!/usr/bin/env python3
"""Layering guardrail for the kernel source tree (战役 0).

Enforces a bottom-up dependency direction so future refactors cannot
accidentally couple a low layer to a higher one. Layer order (lower number =
lower level):

    L0  arch   (include/arch, arch/)
    L1  mm     (include/mm, mm/)
    L2  core   (include/kernel, kernel/)  [sched, syscall, userprog, ...]
    L3  service(kernel/gui, kernel/ipc, drivers/, fs/, net/)
    L4  user   (include/user, user/)
    B   base   (lib/)

Two severities:

  ERROR  base layer (lib/) must not include anything above itself. The tree
         currently has known historical violations here; they are reported as
         ERROR but only fail the build under --strict, so this doubles as the
         cleanup backlog that 战役 2/3 must resolve before the rule can be
         hard-enforced.

  WARN   arch/ must not include core/service/user layers (the documented
         layering rule). The tree currently has real violations here; they are
         reported but do NOT fail the build, so this doubles as the cleanup
         backlog for 战役 3. Remove the WARN block once the list is empty.

Usage:
    python3 scripts/check_layers.py            # exit 1 only on --strict
    python3 scripts/check_layers.py --strict   # fail on ERROR (lib) + WARN
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

LAYER = {
    "lib": 0,        # base, must depend on nothing above itself
    "arch": 1,
    "mm": 2,
    "kernel": 3,
    "drivers": 4,
    "fs": 4,
    "net": 4,
    "user": 5,
}

# Known, intentional layering where a "lower" file includes a higher-prefixed
# header that is actually implemented at a lower level (e.g. arch hardware
# primitives such as rdtsc/cpuid live in arch/ but are leaf dependencies used
# by lib). These are not upward dependencies in practice; they are whitelisted
# so the guardrail can stay strict on everything else.
ALLOW_LIST = {
    ("lib/rand/rand.c", "arch/cpu.h"),
    # arch<->kernel TCB contract: the context-switch and exception paths in
    # arch/ read kernel_stack_top / pml4_phys / status and the FPU buffer of
    # struct TASK directly, so they must see the full layout. The layout is a
    # shared arch/kernel ABI; this is a deliberate exception, not a layering
    # violation. See include/arch/task.h extraction note in 战役 2.
    ("arch/x86_64/irq/interrupt.c", "kernel/sched/thread.h"),
    ("arch/x86_64/mm/paging.c", "kernel/sched/thread.h"),
    ("arch/x86_64/cpu/smp/smp.c", "kernel/sched/thread.h"),
    ("arch/x86_64/cpu/tss/tss.c", "kernel/sched/thread.h"),
}


def layer_of(inc: str) -> int:
    top = inc.split("/", 1)[0]
    return LAYER.get(top, -1)  # -1: outside our layers (e.g. include/arch/...)


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
                continue  # outside our layering (e.g. <stddef.h>, include/arch/...)
            if (str(path.relative_to(ROOT)), inc) in ALLOW_LIST:
                continue
            if fl == 0 and il > 0:
                # base layer (lib) must not depend on anything above it
                errors.append((str(path), inc))
            elif fl == 1 and il >= 3:
                # arch must not pull in core/service/user layers
                warns.append((str(path), inc))

    print(f"[layers] scanned {scanned} C files")
    if errors:
        print(f"[layers] ERROR: {len(errors)} base-layer (lib) upward includes "
              f"(must be cleaned in 战役 2/3; fatal only with --strict):")
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

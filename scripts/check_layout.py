#!/usr/bin/env python3
"""Directory hygiene guardrail.

The layering check (check_layers.py) answers "may this file include that
header". This one answers "is this tree laid out the way the docs promise",
which is the question a new contributor actually has when they go looking for
a file.

Checks, all of which have actually been violated at some point in this repo:

  empty-dirs     Empty directories left behind by removed or never-written
                 code. git cannot track them, so they show up as untracked
                 noise and make `find` output misleading.

  stray-artifacts Build output and editor droppings accidentally committed.

  orphan-headers A header under include/ that nothing includes. Dead weight,
                 and worse, a newcomer reasonably assumes it is the contract
                 for something. Headers reachable only from other headers are
                 fine and not reported.

  shadow-headers Two headers with the same basename in include/. Because
                 sources include them by basename in some places and by path
                 in others, this silently picks the wrong one.

  unmirrored-hdr An include/arch/<name>.h that is neither an architecture
                 dispatch shim (the CONFIG_ARCH_X86_64 pattern) nor an
                 arch-generic interface, so its placement is ambiguous.

Usage:
    python3 scripts/check_layout.py
    python3 scripts/check_layout.py --strict   # fail instead of warn
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKIP_DIRS = {"third_party", "build", ".git", "__pycache__", ".cache",
             ".zig-cache", ".zig-tmp"}
INCLUDE = ROOT / "include"

STRICT = "--strict" in sys.argv

# Headers user programs reach by basename. USER_INCS puts include/user/libc
# ahead of include/, so these resolve to the userland copy on purpose even
# though kernel/ carries same-named files.
USER_BASENAMES = {"assert.h", "signal.h", "syscall.h"}


def log(msg: str) -> None:
    print(msg, flush=True)


def _skipped(rel: Path) -> bool:
    return any(part in SKIP_DIRS for part in rel.parts)


def iter_files(pattern: str):
    for p in ROOT.rglob(pattern):
        if _skipped(p.relative_to(ROOT)):
            continue
        yield p


def check_empty_dirs() -> list[str]:
    out = []
    for p in sorted(ROOT.rglob("*")):
        if not p.is_dir():
            continue
        rel = p.relative_to(ROOT)
        if _skipped(rel):
            continue
        try:
            next(p.iterdir())
        except StopIteration:
            out.append(str(rel))
        except OSError:
            pass
    return out


def check_stray_artifacts() -> list[str]:
    out = []
    for pat in ("*.o", "*.d", "*.pyc", "*.orig", "*.rej", "*.swp"):
        for p in iter_files(pat):
            out.append(str(p.relative_to(ROOT)))
    return sorted(out)


def _sources() -> list[Path]:
    return [p for p in iter_files("*.c")] + [p for p in iter_files("*.h")]


def check_orphan_headers() -> list[str]:
    blob = []
    for p in _sources():
        try:
            blob.append(p.read_text(errors="replace"))
        except OSError:
            pass
    text = "\n".join(blob)
    out = []
    for h in sorted(INCLUDE.rglob("*.h")):
        rel = h.relative_to(INCLUDE).as_posix()   # e.g. arch/x86_64/irq.h
        name = h.name
        if f'"{rel}"' in text:
            continue
        # Headers pulled in by bare basename (common for the arch/ shims).
        if re.search(r'"%s"' % re.escape(name), text):
            continue
        out.append(str(h.relative_to(ROOT)))
    return out


def _referenced() -> set[str]:
    """Every include path spelled anywhere in the tree, in include/ terms."""
    refs: set[str] = set()
    for p in _sources():
        try:
            text = p.read_text(errors="replace")
        except OSError:
            continue
        for m in re.finditer(r'#\s*include\s+[<"]([^>"]+)[>"]', text):
            refs.add(m.group(1))
    return refs


def check_shadow_headers() -> list[str]:
    """A basename reachable by two different paths is a real collision.

    include/arch/<n>.h dispatching to include/arch/x86_64/<n>.h is the intended
    portability pattern and always spells the full path, so it is safe. The
    dangerous case is a header included *by basename* while another header of
    the same name exists: which one wins depends on -I order.
    """
    refs = _referenced()
    by_basename: dict[str, list[str]] = {}
    for h in INCLUDE.rglob("*.h"):
        by_basename.setdefault(h.name, []).append(h.relative_to(INCLUDE).as_posix())
    out = []
    for name, paths in sorted(by_basename.items()):
        if len(paths) < 2:
            continue
        if name not in refs:
            continue  # only ever included by full path -> shim pattern
        # User programs build with -I include/user/libc first, so a basename
        # like "syscall.h" or "signal.h" resolves there deterministically even
        # though the kernel has a same-named header. That is intended layering,
        # not a collision.
        if name in USER_BASENAMES and any(p.startswith("user/") for p in paths):
            continue
        out.append(f"{name}: included by basename, also exists at "
                   + ", ".join(sorted(paths)))
    return out


def check_unmirrored_headers() -> list[str]:
    """include/arch/ should hold either a dispatch shim or a generic interface."""
    out = []
    arch = INCLUDE / "arch"
    if not arch.is_dir():
        return out
    for h in sorted(arch.rglob("*.h")):
        rel = h.relative_to(INCLUDE)
        if rel.parts[1:2] and rel.parts[1] == "x86_64":
            continue  # concrete implementation, correct place for it
        text = h.read_text(errors="replace")
        if "CONFIG_ARCH_X86_64" in text:
            continue  # dispatch shim
        # arch-generic interfaces are fine; flag only the ones that declare
        # x86-only inline asm without going through a shim.
        if "__asm__" in text or "asm volatile" in text:
            out.append(f"{rel} (declares inline asm outside the shim layer)")
    return out


def main() -> int:
    checks = [
        ("empty dirs", check_empty_dirs, "remove them, or add the code you meant to put there"),
        ("stray build artifacts", check_stray_artifacts, "delete them; they belong in build/"),
        ("orphan headers", check_orphan_headers,
         "delete the header, or include it where it belongs"),
        ("shadowed header basenames", check_shadow_headers,
         "give one of them a distinct name"),
        ("headers outside the arch shim convention", check_unmirrored_headers,
         "move the implementation under include/arch/x86_64/ and keep a shim"),
    ]
    total = 0
    for title, fn, fix in checks:
        found = fn()
        if not found:
            continue
        total += len(found)
        log(f"[layout] {len(found)} {title}:")
        for item in found:
            log(f"    {item}")
        log(f"[layout]   -> {fix}")

    if total == 0:
        log("[layout] OK: tree layout is clean")
        return 0
    if STRICT:
        log("[layout] --strict: failing on layout problems")
        return 1
    log("[layout] OK: layout problems present but non-fatal (use --strict to fail)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
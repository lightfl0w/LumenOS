#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKIP_DIRS = {"third_party", "build", ".git", "__pycache__", ".cache",
             ".zig-cache", ".zig-tmp"}
INCLUDE = ROOT / "include"

STRICT = "--strict" in sys.argv

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
        rel = h.relative_to(INCLUDE).as_posix()  
        name = h.name
        if f'"{rel}"' in text:
            continue

        if re.search(r'"%s"' % re.escape(name), text):
            continue
        out.append(str(h.relative_to(ROOT)))
    return out


def _referenced() -> set[str]:
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
    refs = _referenced()
    by_basename: dict[str, list[str]] = {}
    for h in INCLUDE.rglob("*.h"):
        by_basename.setdefault(h.name, []).append(h.relative_to(INCLUDE).as_posix())
    out = []
    for name, paths in sorted(by_basename.items()):
        if len(paths) < 2:
            continue
        if name not in refs:
            continue  
        if name in USER_BASENAMES and any(p.startswith("user/") for p in paths):
            continue
        out.append(f"{name}: included by basename, also exists at "
                   + ", ".join(sorted(paths)))
    return out


def check_unmirrored_headers() -> list[str]:
    out = []
    arch = INCLUDE / "arch"
    if not arch.is_dir():
        return out
    for h in sorted(arch.rglob("*.h")):
        rel = h.relative_to(INCLUDE)
        if rel.parts[1:2] and rel.parts[1] == "x86_64":
            continue  
        text = h.read_text(errors="replace")
        if "CONFIG_ARCH_X86_64" in text:
            continue 

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
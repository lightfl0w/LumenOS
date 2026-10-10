#!/usr/bin/env python3
"""Headless QEMU smoke test: build artefacts boot to a shell.

Used as a regression gate for refactors. Boots the UEFI image the same way
build.py's `run` does, captures the debugcon serial output to a file, and fails
if the kernel does not reach the interactive shell prompt ("Welcome to fish")
within a timeout.

This is a guardrail (战役 0), not a functional test: it only asserts that the
system still boots end-to-end. Reading the debugcon log from a file (rather than
a pipe) avoids QEMU's stdio chardev buffering when stdout is not a tty.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"

OVMF_DIRS = [
    "/usr/share/edk2-ovmf/x64",
    "/usr/share/OVMF",
    "/usr/share/edk2/x64",
    "/usr/share/ovmf/x64",
]


def find_ovmf():
    for d in OVMF_DIRS:
        base = Path(d)
        if not base.exists():
            continue
        for cn in ("OVMF_CODE.4m.fd", "OVMF_CODE.fd"):
            for vn in ("OVMF_VARS.4m.fd", "OVMF_VARS.fd"):
                code = base / cn
                if code.exists() and (base / vn).exists():
                    return code, base / vn
    return None


def log(msg: str) -> None:
    print(msg, flush=True)


def main() -> int:
    missing = []
    if not (BUILD / "esp.img").exists():
        missing.append(str(BUILD / "esp.img"))
    if not (BUILD / "test_hd.img").exists():
        missing.append(str(BUILD / "test_hd.img"))
    ovmf = find_ovmf()
    if ovmf is None:
        missing.append("OVMF firmware (searched: " + ", ".join(OVMF_DIRS) + ")")
    if missing:
        log("[smoke] missing build artefacts, run `python3 build.py` first:")
        for m in missing:
            log("  - " + m)
        return 2

    code, var_src = ovmf
    (BUILD / "OVMF_VARS.fd").write_bytes(var_src.read_bytes())

    with tempfile.NamedTemporaryFile(mode="r", suffix=".log", delete=False) as tf:
        dbg_log = Path(tf.name)

    qemu = "qemu-system-x86_64"
    cmd = [
        qemu, "-machine", "pc", "-accel", "tcg,tb-size=256", "-m", "1G",
        "-smp", "1", "-vga", "std",
        "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={code}",
        "-drive", f"if=pflash,format=raw,unit=1,file={BUILD / 'OVMF_VARS.fd'}",
        "-hda", str(BUILD / "esp.img"),
        "-hdb", str(BUILD / "test_hd.img"),
        "-debugcon", f"file:{dbg_log}",
        "-display", "none", "-monitor", "none",
    ]

    timeout_s = 90
    log(f"[smoke] launching {qemu} (timeout {timeout_s}s) ...")
    proc = subprocess.Popen(cmd)
    deadline = time.time() + timeout_s
    saw_shell = False
    boot_lines: list[str] = []
    while time.time() < deadline:
        if proc.poll() is not None:
            break
        try:
            lines = dbg_log.read_text(errors="replace").splitlines()
        except OSError:
            lines = []
        boot_lines = lines
        if any("Welcome to fish" in ln for ln in lines):
            saw_shell = True
            break
        time.sleep(0.5)

    if proc.poll() is None:
        proc.kill()
    proc.wait()

    if saw_shell:
        log("[smoke] PASS: kernel reached interactive shell prompt")
        return 0

    log("[smoke] FAIL: shell prompt not reached within timeout")
    log("[smoke] ---- last boot output ----")
    for l in boot_lines[-25:]:
        log("  " + l)
    return 1


if __name__ == "__main__":
    sys.exit(main())

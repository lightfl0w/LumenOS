from __future__ import annotations

import socket
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

KEYMAP = {
    ":": "shift-semicolon",
    "/": "slash", ".": "dot", "-": "minus", "_": "shift-minus",
    " ": "spc", "\n": "ret",
}
for c, k in (("0", "0"), ("1", "1"), ("2", "2"), ("3", "3"), ("4", "4"),
             ("5", "5"), ("6", "6"), ("7", "7"), ("8", "8"), ("9", "9")):
    KEYMAP[c] = k
for c in "abcdefghijklmnopqrstuvwxyz":
    KEYMAP[c] = c


def find_ovmf():
    for d in OVMF_DIRS:
        base = Path(d)
        if not base.exists():
            continue
        for cn in ("OVMF_CODE.4m.fd", "OVMF_CODE.fd"):
            for vn in ("OVMF_VARS.4m.fd", "OVMF_VARS.fd"):
                if (base / cn).exists() and (base / vn).exists():
                    return base / cn, base / vn
    return None


def log(msg: str) -> None:
    print(msg, flush=True)


def mon_cmd(sock: socket.socket, cmd: str) -> None:
    sock.sendall((cmd + "\n").encode())


def send_text(sock: socket.socket, text: str) -> None:
    for ch in text:
        key = KEYMAP.get(ch)
        if key is None:
            raise ValueError(f"no key mapping for {ch!r}")
        mon_cmd(sock, f"sendkey {key}")
        time.sleep(0.03)


def main() -> int:
    missing = []
    if not (BUILD / "esp.img").exists():
        missing.append(str(BUILD / "esp.img"))
    if not (BUILD / "test_hd.img").exists():
        missing.append(str(BUILD / "test_hd.img"))
    if not (BUILD / "seatprobe.elf").exists():
        missing.append(str(BUILD / "seatprobe.elf"))
    ovmf = find_ovmf()
    if ovmf is None:
        missing.append("OVMF firmware")
    if missing:
        for m in missing:
            log(f"[seatprobe-run] missing: {m}")
        return 2

    code, var_src = ovmf
    (BUILD / "OVMF_VARS.fd").write_bytes(var_src.read_bytes())

    with tempfile.NamedTemporaryFile(mode="r", suffix=".log", delete=False) as tf:
        dbg_log = Path(tf.name)
    mon_sock_path = BUILD / "monitor.sock"

    qemu = "qemu-system-x86_64"
    cmd = [
        qemu, "-machine", "pc", "-accel", "tcg,tb-size=256", "-m", "1G",
        "-smp", "1", "-vga", "std",
        "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={code}",
        "-drive", f"if=pflash,format=raw,unit=1,file={BUILD / 'OVMF_VARS.fd'}",
        "-hda", str(BUILD / "esp.img"),
        "-hdb", str(BUILD / "test_hd.img"),
        "-debugcon", f"file:{dbg_log}",
        "-display", "none",
        "-monitor", f"unix:{mon_sock_path},server=on,wait=off",
    ]
    if mon_sock_path.exists():
        mon_sock_path.unlink()

    log(f"[seatprobe-run] launching {qemu} ...")
    proc = subprocess.Popen(cmd)

    sock = None
    deadline = time.time() + 30
    while time.time() < deadline and sock is None:
        try:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.connect(str(mon_sock_path))
            sock.settimeout(0.2)
        except OSError:
            if sock:
                sock.close()
                sock = None
            if proc.poll() is not None:
                log("[seatprobe-run] FAIL: qemu exited early")
                return 1
            time.sleep(0.5)
    if sock is None:
        proc.kill()
        proc.wait()
        log("[seatprobe-run] FAIL: monitor socket never appeared")
        return 1

    saw_shell = False
    deadline = time.time() + 90
    while time.time() < deadline:
        if proc.poll() is not None:
            break
        try:
            lines = dbg_log.read_text(errors="replace").splitlines()
        except OSError:
            lines = []
        if any(("Welcome to fish" in ln) or ("[lumen@lumen " in ln) for ln in lines):
            saw_shell = True
            break
        time.sleep(0.5)

    if not saw_shell:
        log("[seatprobe-run] FAIL: shell prompt not reached within timeout")
        log("[seatprobe-run] ---- last boot output ----")
        for l in dbg_log.read_text(errors="replace").splitlines()[-25:]:
            log("  " + l)
        proc.kill()
        proc.wait()
        return 1

    time.sleep(1.0)
    probe = sys.argv[1] if len(sys.argv) > 1 else "/bin/seatprobe.elf"
    marker = Path(probe).stem.replace(".", "_")
    log(f"[seatprobe-run] shell up, sending {probe} ...")
    try:
        send_text(sock, probe + "\n")
    except (OSError, ValueError) as e:
        log(f"[seatprobe-run] FAIL: sendkey: {e}")
        proc.kill()
        proc.wait()
        return 1

    result = "timeout"
    deadline = time.time() + 60
    while time.time() < deadline:
        if proc.poll() is not None:
            result = "qemu-exited"
            break
        try:
            text = dbg_log.read_text(errors="replace")
        except OSError:
            text = ""
        stem = Path(probe).stem
        if (f"[{marker}] PASS" in text or f"[{marker}] OK" in text
                or f"{stem}: PASS" in text or f"{stem} PASS" in text
                or f"{stem}: done" in text):
            result = "pass"
            break
        if f"[{marker}] FAIL" in text or f"{stem}: FAIL" in text:
            result = "fail"
            break
        time.sleep(0.5)

    if sock:
        try:
            sock.close()
        except OSError:
            pass
    proc.kill()
    proc.wait()

    tail = dbg_log.read_text(errors="replace").splitlines()
    keep = [l for l in tail if "[seatprobe]" in l or "seatd" in l.lower() or f"[{Path(probe).stem}" in l]
    for l in keep[-30:]:
        log("  " + l)

    if result == "pass":
        log("[seatprobe-run] PASS: real libseat + embedded seatd works in guest")
        return 0
    log(f"[seatprobe-run] FAIL: {result}")
    for l in tail[-30:]:
        log("  " + l)
    return 1


if __name__ == "__main__":
    sys.exit(main())

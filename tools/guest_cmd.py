#!/usr/bin/env python3

import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
OVMF = Path("/usr/share/edk2-ovmf/x64")
KEYS = {":": "shift-semicolon", ";": "shift-semicolon", "/": "slash",
        ".": "dot", "-": "minus",
        " ": "spc", "\n": "ret", "_": "shift-minus", "=": "equal",
        "|": "shift-backslash", "<": "shift-comma", ">": "shift-dot"}


def send(mon, text):
    for ch in text:
        key = KEYS.get(ch, ch)
        if ch.isupper():
            key = f"shift-{ch.lower()}"
        mon.sendall(f"sendkey {key}\n".encode())
        time.sleep(0.03)


def main():
    command = sys.argv[1]
    wait = float(sys.argv[2]) if len(sys.argv) > 2 else 8.0
    tail = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    keys = sys.argv[4] if len(sys.argv) > 4 else None
    key_delay = float(sys.argv[5]) if len(sys.argv) > 5 else 4.0
    (BUILD / "OVMF_VARS.fd").write_bytes((OVMF / "OVMF_VARS.4m.fd").read_bytes())
    log = Path(tempfile.mkstemp(suffix=".log")[1])
    mon_path = BUILD / "monitor.sock"
    mon_path.unlink(missing_ok=True)
    qemu = [
        "qemu-system-x86_64", "-machine", "pc", "-accel", "tcg,tb-size=256",
        "-m", "1G", "-smp", "1", "-vga", "std",
        "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={OVMF / 'OVMF_CODE.4m.fd'}",
        "-drive", f"if=pflash,format=raw,unit=1,file={BUILD / 'OVMF_VARS.fd'}",
        "-hda", str(BUILD / "esp.img"), "-hdb", str(BUILD / "test_hd.img"),
        "-debugcon", f"file:{log}", "-display", "none",
        "-monitor", f"unix:{mon_path},server=on,wait=off",
    ]
    qemu_err = BUILD / "qemu-guest.log"
    err_fp = open(qemu_err, "w")
    proc = subprocess.Popen(qemu, stdout=subprocess.DEVNULL,
                            stderr=err_fp)
    mon = None
    for _ in range(120):
        if mon_path.exists():
            try:
                mon = socket.socket(socket.AF_UNIX)
                mon.connect(str(mon_path))
                break
            except OSError:
                mon = None
        time.sleep(0.5)
    if mon is None:
        proc.kill()
        proc.wait()
        err_fp.close()
        tail = qemu_err.read_text(errors="replace")[-2000:]
        raise SystemExit(f"monitor socket unavailable\nqemu stderr:\n{tail}")
    logged_in = False
    sent_password = False
    for _ in range(300):
        text = log.read_text(errors="replace")
        if "lumen@lumen" in text:
            break
        if "lumen login:" in text and not logged_in:
            send(mon, "root\n")
            logged_in = True
        if "Password:" in text and logged_in and not sent_password:
            send(mon, "\n")
            sent_password = True
        time.sleep(0.5)
    time.sleep(1)
    send(mon, command + "\n")
    if keys:
        events = []
        for part in keys.split(","):
            if ":" in part:
                delay, key = part.split(":", 1)
                events.append((float(delay), key))
            else:
                events.append((key_delay, part))
        previous = 0.0
        for delay, key in events:
            time.sleep(max(0.0, delay - previous))
            previous = delay
            mon.sendall(f"sendkey {key}\n".encode())
    time.sleep(wait)
    proc.kill()
    proc.wait()
    print("\n".join(log.read_text(errors="replace").splitlines()[-tail:]))


if __name__ == "__main__":
    main()

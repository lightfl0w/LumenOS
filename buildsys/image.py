from __future__ import annotations
import argparse
import glob
import io
import os
import shlex
import shutil
import subprocess
import sys
import time
from contextlib import contextmanager
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Iterator, List, Optional, Sequence, Tuple

from . import (ROOT, USER_DIR, MM_DIR, FS_DIR, NET_DIR, KERNEL_DIR, BUILD_DIR,
              SCRIPTS, TOOLS, TESTS, PROBES, MUSL_SRC, CONFIG_FILE,
              CONFIG_SCRIPT, AUTOCONF_H, ARCH_PROFILES, DEFAULT_ARCH_KEY,
              ARCH_KEYS, CONFIG, ARCH, MUSL_ARCH, RUST_TRIPLE,
              FREESTANDING_KEY, CLANG_TRIPLES, CLANG_DEFAULT_TRIPLE, ARCH_DIR,
              BOOT_DIR, CLANG_FREESTANDING_FLAGS, CC_CLANG_CANDIDATES,
              LLVM_BIN_GLOBS, FREE, INCS, USER_INCS, KERNEL_CFLAGS,
              UP_CFLAGS_64, UP_LDFLAGS_64, MUSL64_BASE, UEFI_CFLAGS, MUSL_PREFIX,
              MUSL_INC, MUSL_LIB, PCRE2_SRC, PCRE2_PREFIX, PCRE2_INC, PCRE2_LIB,
              FISH_SRC, FISH_CARGO_TOML, FISH_TARGET, FISH_BIN, MUSL_DEMO_CFLAGS,
              LC_CFLAGS, Ansi, Tools, CmdResult, run, Console, KNOWN_VA_WARNINGS,
              SCRIPTS)

def _find_ovmf() -> Optional[Tuple[Path, Path]]:
    dirs = [ARCH.get("ovmf_dir"), "/usr/share/OVMF", "/usr/share/edk2/x64",
            "/usr/share/edk2-ovmf/x64", "/usr/share/ovmf/x64"]
    for d in dirs:
        if not d:
            continue
        base = Path(d)
        for cn in ("OVMF_CODE.4m.fd", "OVMF_CODE.fd"):
            for vn in ("OVMF_VARS.4m.fd", "OVMF_VARS.fd"):
                code, vars_src = base / cn, base / vn
                if code.exists() and vars_src.exists():
                    return code, vars_src
    return None
def do_run(console: Console, stats: BuildStats,
           smp: int, gdb: bool, no_net: bool, boot_floppy: bool,
           kvm: bool, uefi: bool = False) -> None:
    qemu = shutil.which(ARCH["qemu_system"])
    if qemu is None:
        console.warn(f"{ARCH['qemu_system']} not found on PATH; build is up-to-date.")
        return
    if kvm and not os.path.exists("/dev/kvm"):
        console.warn("未找到 /dev/kvm, KVM 不可用 (需 Linux/WSL2 且开启嵌套虚拟化); "
                     "回退 TCG 软件模拟")
        kvm = False
    if uefi:
        ovmf = _find_ovmf()
        if ovmf is None:
            console.warn("未找到 OVMF 固件 (OVMF_CODE/OVMF_VARS); "
                         "无法以 UEFI 引导")
            return
        code, vars_src = ovmf
        esp = BUILD_DIR / "esp.img"
        if not esp.exists():
            console.warn(f"{esp} 不存在; 请确认 CONFIG_UEFI=y 且构建成功")
            return
        hd_img = BUILD_DIR / "test_hd.img"
        if not hd_img.exists():
            console.warn(f"{hd_img} 不存在; 请先运行构建 (build.py) 生成 test_hd.img")
            return
        vars_dst = BUILD_DIR / "OVMF_VARS.fd"
        shutil.copyfile(vars_src, vars_dst)
        cmd = [qemu, "-machine", "pc", "-accel", "tcg,tb-size=256", "-m", "1G",
               "-smp", str(max(1, smp)), "-vga", "std",
               "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={code}",
               "-drive", f"if=pflash,format=raw,unit=1,file={vars_dst}",
               "-hda", str(esp), "-hdb", str(hd_img),
               "-debugcon", "stdio", "-display", "gtk,zoom-to-fit=off"]
        if gdb:
            cmd += ["-s", "-S"]
        console.writeln()
        console.writeln(f"  {console._c(Ansi.BR_GRN)}▶ launching qemu..."
                        f"{'（SMP' if smp > 1 else ''}"
                        f"{' · OVMF/UEFI' if uefi else ''}"
                        f"{' · GDB 等待' if gdb else ''}"
                        f"{console._c(Ansi.RESET)}")
        console.writeln()
        subprocess.run(cmd)
        return
    if kvm:
        console.info("KVM 加速已启用 (-enable-kvm -cpu host), 可在 ring0 测 MWAIT")
        cmd = [qemu, "-enable-kvm", "-cpu", "host", "-m", "1G",
               "-smp", str(max(1, smp)),
               "-device", "virtio-vga,edid=on,xres=1024,yres=768"]
    else:
        cmd = [qemu, "-accel", "tcg,tb-size=256", "-m", "1G",
               "-smp", str(max(1, smp)), "-device", "virtio-vga,edid=on,xres=1024,yres=768"]
    if boot_floppy:
        cmd += ["-fda", str(BUILD_DIR / "floppy.img")]
    else:
        hd_img = BUILD_DIR / "test_hd.img"
        if not hd_img.exists():
            console.warn(f"{hd_img} 不存在; 请先运行构建 (build.py) 生成 test_hd.img")
            return
        cmd += ["-hda", str(hd_img)]
    cmd += ["-debugcon", "stdio", "-display", "gtk,zoom-to-fit=off"]
    if not no_net:
        cmd += ["-netdev", "user,id=net0,hostfwd=tcp::8765-:8765",
                "-device", "e1000,netdev=net0"]
    if gdb:
        cmd += ["-s", "-S"]
    console.writeln()
    console.writeln(f"  {console._c(Ansi.BR_GRN)}▶ launching qemu..."
                    f"{'（SMP' if smp > 1 else ''}"
                    f"{' · KVM' if kvm else ''}"
                    f"{' · GDB 等待' if gdb else ''}"
                    f"{' · floppy 引导' if boot_floppy else ''}"
                    f"{console._c(Ansi.RESET)}")
    console.writeln()
    subprocess.run(cmd)
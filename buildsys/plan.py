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
              LC_CFLAGS, Ansi, Tools, CmdResult, run, Console,
              KNOWN_VA_WARNINGS, SCRIPTS)
from .tasks import (Task, BuildStats, task_assemble_bin, task_assemble_elf,
                   task_assemble_elf64, task_cc, task_link, task_objcopy_binary,
                   task_python, task_nasm_uefi, task_cc_uefi, task_link_uefi,
                   _musl_buildenv)
from .toolchain import task_config

# Directories whose .c files belong to the kernel link. A file in one of these
# trees only gets compiled if it is wired into kernel_c_sources.
KERNEL_SOURCE_DIRS = ("arch", "kernel", "mm", "lib", "drivers", "fs", "net")

# .c files under these trees that are deliberately NOT part of the kernel link
# (separate target: the freestanding EFI loader is linked into BOOTX64.EFI).
KERNEL_SOURCE_EXEMPT = (
    "arch/x86_64/boot/uefi/",
)


def check_kernel_sources_wired(*source_lists) -> None:
    """Fail loudly if a kernel .c file was never wired into the build.

    Adding a new .c under arch/ kernel/ mm/ lib/ drivers/ fs/ net/ does NOT
    compile it by itself -- it must be added to kernel_c_sources. That is the
    single most common way a new contributor loses an afternoon: the file
    compiles fine standalone, the linker never mentions it, and the feature is
    simply absent at runtime. So we cross-check the tree against the wiring and
    stop the build with an actionable message.
    """
    wired = set()
    for sources in source_lists:
        for _stem, src in sources:
            try:
                wired.add(Path(src).resolve())
            except OSError:
                continue
    missing = []
    for d in KERNEL_SOURCE_DIRS:
        base = ROOT / d
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*.c")):
            if path.resolve() in wired:
                continue
            rel = path.relative_to(ROOT).as_posix()
            if any(rel.startswith(prefix) for prefix in KERNEL_SOURCE_EXEMPT):
                continue
            missing.append(rel)
    if not missing:
        return
    lines = [
        "build.py: kernel source files are not wired into the build:",
        "",
    ]
    lines += [f"  {rel}" for rel in missing]
    lines += [
        "",
        "Each of these is a .c file in a kernel source directory but is not",
        "listed in kernel_c_sources (buildsys/plan.py), so it is never",
        "compiled or linked -- the build stays green while the code is simply",
        "missing. Add each one, for example:",
        "",
        '    ("myname.o",    KERNEL_DIR / "subdir" / "myname.c"),',
        "",
        "in the kernel_c_sources list. If a file is intentionally excluded,",
        "add its directory to KERNEL_SOURCE_EXEMPT in buildsys/plan.py.",
    ]
    raise SystemExit("\n".join(lines))


def make_plan(tools: Tools, with_musl_lib: bool = False,
             with_tests: bool = False):
    tasks: List[Task] = []
    user_elves: List[Task] = []
    tasks.append(task_assemble_bin("boot.bin",
                          BOOT_DIR / "boot.asm", BUILD_DIR / "boot.bin", tools))
    tasks.append(task_assemble_bin("vbr.bin",
                          BOOT_DIR / "vbr.asm", BUILD_DIR / "vbr.bin", tools))
    tasks.append(task_assemble_bin("loader.bin",
                          BOOT_DIR / "loader.asm", BUILD_DIR / "loader.bin", tools))
    for stem in ("func", "io", "stub", "entry", "switch", "idle", "mb2_entry"):
        tasks.append(task_assemble_elf64(
            f"{stem}.o",
            ARCH_DIR / "cpu" / f"{stem}.asm",
            BUILD_DIR / f"{stem}.o", tools,
        ))
    tasks.append(task_assemble_elf64(
        "syscall_entry.o",
        ARCH_DIR / "syscall" / "entry.asm",
        BUILD_DIR / "syscall_entry.o", tools,
    ))
    tasks.append(task_assemble_bin(
        "ap_trampoline.bin",
        ARCH_DIR / "cpu" / "ap_trampoline.asm",
        BUILD_DIR / "ap_trampoline.bin", tools))
    tasks.append(task_objcopy_binary(
        "ap_tramp.o", BUILD_DIR / "ap_trampoline.bin",
        BUILD_DIR / "ap_tramp.o", tools,
        "_binary_ap_trampoline_bin_start",
        out_fmt=ARCH["objcopy_tramp_fmt"], out_arch=ARCH["objcopy_tramp_arch"]))
    tasks.append(task_assemble_elf64(
        "up_start.o", USER_DIR / "apps" / "start.asm", BUILD_DIR / "up_start.o", tools,
    ))
    kernel_c_sources = [
        ("ioc.o",        ROOT / "drivers" / "char" / "serial" / "console" / "io.c"),
        ("pit.o",        KERNEL_DIR / "time" / "pit" / "pit.c"),
        ("pic.o",        ARCH_DIR / "irq" / "pic" / "pic.c"),
        ("apic.o",       ARCH_DIR / "irq" / "apic" / "apic.c"),
        ("acpi.o",     ARCH_DIR / "irq" / "acpi" / "acpi.c"),
        ("syscall_arch.o", ARCH_DIR / "syscall" / "init.c"),
        ("arch_paging.o", ARCH_DIR / "mm" / "paging.c"),
        ("syscall_entry_c.o", ARCH_DIR / "syscall" / "entry.c"),
        ("idt.o",        ARCH_DIR / "irq" / "idt.c"),
        ("interrupt.o",  ARCH_DIR / "irq" / "interrupt.c"),
        ("early.o",      ARCH_DIR / "boot" / "early.c"),
        ("kernel.o",     KERNEL_DIR / "main.c"),
        ("cmdline.o",    KERNEL_DIR / "boot" / "cmdline.c"),
        ("mb2.o",        ARCH_DIR / "boot" / "mb2.c"),
        ("boot_info.o",  ARCH_DIR / "boot" / "boot_info.c"),
        ("assert.o",     KERNEL_DIR / "panic.c"),
        ("ssp.o",        KERNEL_DIR / "ssp.c"),
        ("str.o",        ROOT / "lib" / "string" / "str.c"),
        ("rand.o",       ROOT / "lib" / "rand" / "rand.c"),
        ("rbtree.o",     ROOT / "lib" / "rbtree" / "rbtree.c"),
        ("png.o",        ROOT / "lib" / "png" / "png.c"),
        ("ttf.o",        ROOT / "lib" / "ttf" / "ttf.c"),
        ("bitmap.o",     MM_DIR / "bitmap" / "bitmap.c"),
        ("pool.o",       MM_DIR / "pool" / "pool.c"),
        ("access.o",     MM_DIR / "access.c"),
        ("kheap.o",      ROOT / "lib" / "malloc" / "kmalloc.c"),
        ("list.o",       ROOT / "lib" / "list" / "list.c"),
        ("kprintf.o",    ROOT / "lib" / "printf" / "printf.c"),
        ("thread.o",     KERNEL_DIR / "sched" / "thread.c"),
        ("sync.o",       KERNEL_DIR / "sync" / "sync.c"),
        ("percpu.o",     KERNEL_DIR / "sched" / "percpu.c"),
        ("smp.o",        ARCH_DIR / "cpu" / "smp" / "smp.c"),
        ("ioqueue.o",    ROOT / "drivers" / "char" / "serial" / "ioqueue.c"),
        ("tty.o",        ROOT / "drivers" / "char" / "serial" / "tty.c"),
        ("pty.o",        ROOT / "drivers" / "char" / "serial" / "pty.c"),
        ("keyboard.o",   ROOT / "drivers" / "input" / "keyboard" / "keyboard.c"),
        ("rtc.o",        ROOT / "drivers" / "char" / "serial" / "rtc.c"),
        ("ide.o",        ROOT / "drivers" / "block" / "ata" / "ide.c"),
        ("block.o",      ROOT / "drivers" / "block" / "ata" / "block.c"),
        ("nvme.o",       ROOT / "drivers" / "block" / "ata" / "nvme.c"),
        ("pci.o",        ROOT / "drivers" / "bus" / "pci" / "pci.c"),
        ("ext2.o",       FS_DIR / "ext2.c"),
        ("ext4.o",       FS_DIR / "ext4.c"),
        ("vfs.o",        FS_DIR / "vfs" / "vfs.c"),
        ("pbcache.o",    FS_DIR / "pbcache.c"),
        ("fs.o",         FS_DIR / "fs.c"),
        ("inode.o",      FS_DIR / "inode.c"),
        ("dir.o",        FS_DIR / "dir.c"),
        ("file.o",       FS_DIR / "file.c"),
        ("proc.o",       FS_DIR / "proc.c"),
        ("gdt.o",        ARCH_DIR / "cpu" / "gdt" / "gdt.c"),
        ("tss.o",        ARCH_DIR / "cpu" / "tss" / "tss.c"),
        ("process.o",    KERNEL_DIR / "userprog" / "process.c"),
        ("exec.o",       KERNEL_DIR / "userprog" / "exec.c"),
        ("pe.o",         KERNEL_DIR / "abi" / "win32" / "pe.c"),
        ("pipe.o",       KERNEL_DIR / "ipc" / "pipe.c"),
        ("ksyscall.o",   KERNEL_DIR / "syscall" / "syscall.c"),
        ("signal.o",     KERNEL_DIR / "syscall" / "signal.c"),
        ("file_syscall.o",KERNEL_DIR / "syscall" / "file_syscall.c"),
        ("mmap.o",       KERNEL_DIR / "syscall" / "mmap.c"),
        ("futex.o",      KERNEL_DIR / "syscall" / "futex.c"),
        ("linux_compat.o", KERNEL_DIR / "abi" / "linux" / "linux_compat.c"),
        ("linux_compat_io.o", KERNEL_DIR / "abi" / "linux" / "linux_compat_io.c"),
        ("linux_compat_fs.o", KERNEL_DIR / "abi" / "linux" / "linux_compat_fs.c"),
        ("linux_compat_proc.o", KERNEL_DIR / "abi" / "linux" / "linux_compat_proc.c"),
        ("win32.o",      KERNEL_DIR / "abi" / "win32" / "win32.c"),
        ("win32_gdi.o",  KERNEL_DIR / "abi" / "win32" / "win32_gdi.c"),
        ("usyscall.o",   USER_DIR / "libc" / "syscall.c"),
        ("ustdio.o",     USER_DIR / "libc" / "stdio.c"),
        ("wait_exit.o",  KERNEL_DIR / "userprog" / "wait_exit.c"),
        ("fork.o",       KERNEL_DIR / "userprog" / "fork.c"),
        ("clone.o",      KERNEL_DIR / "userprog" / "clone.c"),
        ("mouse.o",      ROOT / "drivers" / "char" / "serial" / "mouse.c"),
        ("gfx.o",        KERNEL_DIR / "gui" / "gfx.c"),
        ("gpu.o",        ROOT / "drivers" / "video" / "framebuffer" / "gpu.c"),
        ("display.o",    ROOT / "drivers" / "video" / "framebuffer" / "display.c"),
        ("input.o",      KERNEL_DIR / "gui" / "input.c"),
        ("udi.o",        ROOT / "drivers" / "video" / "framebuffer" / "udi.c"),
        ("udi_virtio.o", ROOT / "drivers" / "video" / "framebuffer" / "udi_virtio.c"),
        ("udi_vmware.o", ROOT / "drivers" / "video" / "framebuffer" / "udi_vmware.c"),
        ("font.o",       KERNEL_DIR / "gui" / "font.c"),
        ("theme.o",      KERNEL_DIR / "gui" / "theme.c"),
        ("shm.o",        KERNEL_DIR / "gui" / "shm.c"),
        ("guiserver.o",  KERNEL_DIR / "gui" / "server.c"),
        ("wm.o", KERNEL_DIR / "gui" / "wm" / "wm.c"),
        ("wm_anim.o", KERNEL_DIR / "gui" / "wm" / "wm_anim.c"),
        ("wm_bar.o", KERNEL_DIR / "gui" / "wm" / "wm_bar.c"),
        ("guiclients.o", KERNEL_DIR / "gui" / "clients.c"),
        ("guiclients_term.o", KERNEL_DIR / "gui" / "clients_term.c"),
        ("guiclients_monitor.o", KERNEL_DIR / "gui" / "clients_monitor.c"),
        ("guiclients_png.o", KERNEL_DIR / "gui" / "clients_png.c"),
        ("guiclients_files.o", KERNEL_DIR / "gui" / "clients_files.c"),
        ("guiclients_edit.o", KERNEL_DIR / "gui" / "clients_edit.c"),
        ("gui.o",        KERNEL_DIR / "gui" / "gui.c"),
        ("x11.o",        KERNEL_DIR / "gui" / "x11.c"),
        ("x11_render.o", KERNEL_DIR / "gui" / "x11_render.c"),
        ("x11_window.o", KERNEL_DIR / "gui" / "x11_window.c"),
        ("x11_server.o", KERNEL_DIR / "gui" / "x11_server.c"),
        ("sha256.o",     ROOT / "lib" / "crypto" / "sha256.c"),
        ("chacha20.o",   ROOT / "lib" / "crypto" / "chacha20.c"),
        ("aes_gcm.o",    ROOT / "lib" / "crypto" / "aes_gcm.c"),
        ("x25519.o",     ROOT / "lib" / "crypto" / "x25519.c"),
        ("p256.o",       ROOT / "lib" / "crypto" / "p256.c"),
        ("rsa.o",        ROOT / "lib" / "crypto" / "rsa.c"),
        ("x509.o",       ROOT / "lib" / "tls" / "x509.c"),
        ("tls.o",        ROOT / "lib" / "tls" / "tls.c"),
        ("tls_roots.o",  ROOT / "lib" / "tls" / "roots.c"),
        ("tls_net.o",    NET_DIR / "tls.c"),
        ("dns.o",        NET_DIR / "dns.c"),
    ]
    tasks.append(Task(
        name="musl-headers",
        cmd=[sys.executable, str(SCRIPTS / "gen_musl_headers.py"),
             "--src", str(MUSL_SRC), "--out", str(MUSL_INC), "--arch", MUSL_ARCH],
        out=MUSL_INC / "bits" / "alltypes.h",
        group="musl-headers",
        description="assemble musl headers into build/musl/include (cross-platform)"))
    for stem, src in kernel_c_sources:
        tasks.append(task_cc(stem, src, BUILD_DIR / stem, tools, KERNEL_CFLAGS,
                             cc=tools.kcc, kind=tools.kcc_kind))
    user_programs = [
        ("prog_no_arg", "prog_no_arg.c", "_start", []),
        ("prog_arg",    "prog_arg.c",    "_start", []),
        ("cat",         "cat.c",         "_start", []),
        ("fork_demo",   "fork_demo.c",   "_start", []),
        ("orphan",      "orphan_demo.c", "_start", []),
        ("tls_test",    "tls_test.c",    "_start", []),
        ("sig_test",    "sig_test.c",    "_start", []),
        ("badptr_test", "badptr_test.c", "_start", []),
        ("cwd_test",    "cwd_test.c",    "_start", []),
        ("echocat",     "echocat.c",     "_start", []),
        ("prog_pipe",   "prog_pipe.c",   "_start", []),
        ("heap_demo",   "heap_demo.c",   "_start", []),
        ("signal_demo", "signal_demo.c", "_start", []),
        ("mmap_demo",   "mmap_demo.c",   "_start", []),
        ("mmap2_demo",  "mmap2_demo.c",  "_start", []),
        ("dev_demo",    "dev_demo.c",    "_start", []),
        ("dev_demo",    "dev_demo.c",    "_start", []),
        ("futex_demo",  "futex_demo.c",  "_start", []),
        ("fsyscall_demo","fsyscall_demo.c","_start", []),
        ("clone_demo",  "clone_demo.c",  "_start", []),
        ("clone_stress","clone_stress.c","_start", []),
        ("path_probe", "path_probe.c", "_start", []),
        ("kaddr_probe", "kaddr_probe.c", "_start", []),
        ("sock_probe", "sock_probe.c", "_start", []),
        ("init_sh",    "init_sh.c",    "_start", []),
        ("shell",      "shell.c",      "_start", []),
        ("futex_probe", "futex_probe.c", "_start", []),
        ("eintr_probe", "eintr_probe.c", "_start", []),
        ("wait_probe", "wait_probe.c", "_start", []),
        ("sel_probe",  "sel_probe.c",  "_start", []),
        ("gs_probe", "gs_probe.c", "_start", []),
        ("ping",        "ping.c",        "_start", []),
        ("udp_echo",    "udp_echo.c",    "_start", []),
        ("tlsclient",   "tlsclient.c",   "_start", []),
        ("apktls",      "apktls.c",      "_start", []),
        ("cow_stress",  "cow_stress.c",  "_start", []),
        ("canary_test", "canary_test.c", "_start", []),
    ]
    user_lib_sources = [
        (USER_DIR / "libc", "stdio.c",   "up_stdio.o"),
        (USER_DIR / "libc", "syscall.c", "up_syscall.o"),
        (ROOT / "lib" / "string",  "str.c",     "up_str.o"),
        (ROOT / "lib" / "rbtree", "rbtree.c",  "up_rbtree.o"),
        (USER_DIR / "libc", "stdlib.c",  "up_stdlib.o"),
        (USER_DIR / "libc", "ssp.c",     "up_ssp.o"),
    ]
    def compile_user_lib(out_dir: Path) -> List[Path]:
        outs = []
        for _dir, fname, oname in user_lib_sources:
            src = _dir / fname
            obj = out_dir / oname
            tasks.append(task_cc(oname, src, obj, tools, UP_CFLAGS_64))
            outs.append(obj)
        return outs
    lib_objs = compile_user_lib(BUILD_DIR)
    tls_lib_sources = [
        (ROOT / "lib" / "crypto", "sha256.c",   "up_sha256.o"),
        (ROOT / "lib" / "crypto", "chacha20.c", "up_chacha20.o"),
        (ROOT / "lib" / "crypto", "aes_gcm.c",  "up_aes_gcm.o"),
        (ROOT / "lib" / "crypto", "x25519.c",   "up_x25519.o"),
        (ROOT / "lib" / "crypto", "p256.c",     "up_p256.o"),
        (ROOT / "lib" / "crypto", "rsa.c",      "up_rsa.o"),
        (ROOT / "lib" / "tls",    "x509.c",     "up_x509.o"),
        (ROOT / "lib" / "tls",    "tls.c",      "up_tls.o"),
        (ROOT / "lib" / "tls",    "roots.c",    "up_roots.o"),
    ]
    tls_objs = []
    for _dir, fname, oname in tls_lib_sources:
        src = _dir / fname
        obj = BUILD_DIR / oname
        tasks.append(task_cc(oname, src, obj, tools, UP_CFLAGS_64))
        tls_objs.append(obj)
    tls_programs = ("apktls",)
    for prog_name, src_c, entry_flag, opt_flags in user_programs:
        if not with_tests and (PROBES / src_c).exists():
            continue
        nick_map = {"prog_no_arg": "up_no_arg", "prog_arg": "up_arg",
                    "cat": "up_cat", "fork_demo": "up_fork",
                    "prog_pipe": "up_pipe",
                    "heap_demo": "up_heap_demo", "signal_demo": "up_signal",
                    "mmap_demo": "up_mmap_demo", "mmap2_demo": "up_mmap2_demo",
                    "futex_demo": "up_futex_demo", "fsyscall_demo": "up_fsyscall_demo",
                    "clone_demo": "up_clone_demo"}
        nick = nick_map.get(prog_name, f"up_{prog_name}")
        prog_obj = BUILD_DIR / f"{nick}.o"
        src_path = USER_DIR / "apps" / src_c
        if not src_path.exists() and (USER_DIR / "init" / src_c).exists():
            src_path = USER_DIR / "init" / src_c
        if not src_path.exists() and (TESTS / "probes" / src_c).exists():
            src_path = TESTS / "probes" / src_c
        tasks.append(task_cc(nick, src_path, prog_obj, tools,
                             UP_CFLAGS_64 + opt_flags))
        elf_flags = list(UP_LDFLAGS_64)
        if entry_flag:
            elf_flags[elf_flags.index("-e") + 1] = entry_flag
        elf = BUILD_DIR / f"{prog_name}.elf"
        extra_objs = list(tls_objs) if prog_name in tls_programs else []
        elf_task = task_link(
            f"{prog_name}.elf", elf, tools,
            [BUILD_DIR / "up_start.o", BUILD_DIR / "lc_clone.o", prog_obj,
             *lib_objs, *extra_objs],
            flags=elf_flags,
        )
        tasks.append(elf_task)
        user_elves.append(elf_task)
    tasks.append(task_assemble_elf64(
        "lc_start.o", USER_DIR / "apps" / "lc_crt0.asm", BUILD_DIR / "lc_start.o", tools,
    ))
    tasks.append(task_assemble_elf64(
        "lc_clone.o", USER_DIR / "apps" / "lc_clone.asm", BUILD_DIR / "lc_clone.o",
        tools,
    ))
    lc_libc = task_cc("lc_libc.o", USER_DIR / "libc" / "compat" / "lc_libc.c",
                      BUILD_DIR / "lc_libc.o", tools, LC_CFLAGS,
                      cc=tools.kcc, kind=tools.kcc_kind)
    tasks.append(lc_libc)
    lc_demo_c = task_cc("lc_demo.o", USER_DIR / "apps" / "lc_demo.c",
                        BUILD_DIR / "lc_demo.o", tools, LC_CFLAGS,
                        cc=tools.kcc, kind=tools.kcc_kind)
    tasks.append(lc_demo_c)
    lc_elf = task_link("lc_demo.elf", BUILD_DIR / "lc_demo.elf", tools,
                       [BUILD_DIR / "lc_start.o", BUILD_DIR / "lc_demo.o",
                        BUILD_DIR / "lc_libc.o"],
       flags=["-s", "-m", ARCH["ld_emulation"],
              "-e", "_lc_start", "-static", "-pie", "--no-dynamic-linker",
              "-z", "pack-relative-relocs"])
    tasks.append(lc_elf)
    user_elves.append(lc_elf)
    gui_launch_c = task_cc("gui_launch.o", USER_DIR / "apps" / "gui_launch.c",
                           BUILD_DIR / "gui_launch.o", tools, LC_CFLAGS,
                           cc=tools.kcc, kind=tools.kcc_kind)
    tasks.append(gui_launch_c)
    gui_elf = task_link(
        "gui.elf", BUILD_DIR / "gui.elf", tools,
        [BUILD_DIR / "lc_start.o", BUILD_DIR / "gui_launch.o",
         BUILD_DIR / "lc_libc.o"],
        flags=["-s", "-m", ARCH["ld_emulation"],
               "-e", "_lc_start", "-static", "-pie", "--no-dynamic-linker",
               "-z", "pack-relative-relocs"])
    tasks.append(gui_elf)
    user_elves.append(gui_elf)
    mingw = shutil.which("x86_64-w64-mingw32-gcc")
    if mingw:
        win_src = USER_DIR / "apps" / "win_main.c"
        win_exe = BUILD_DIR / "win_main.exe"
        win_task = Task(
            name="win_main.exe",
            cmd=[mingw, str(win_src), "-o", str(win_exe)],
            out=win_exe, deps=[win_src],
            optional=True, group="link",
            description="link win_main.exe (mingw PE32+ win32)",
        )
        tasks.append(win_task)
        user_elves.append(win_task)
        gui_src = USER_DIR / "apps" / "win_gui.c"
        gui_exe = BUILD_DIR / "win_gui.exe"
        gui_task = Task(
            name="win_gui.exe",
            cmd=[mingw, str(gui_src), "-o", str(gui_exe), "-lgdi32", "-luser32"],
            out=gui_exe, deps=[gui_src],
            optional=True, group="link",
            description="link win_gui.exe (mingw PE32+ win32 gdi)",
        )
        tasks.append(gui_task)
        user_elves.append(gui_task)
    zig = shutil.which("zig")
    cxx = shutil.which("clang++")
    if zig or cxx:
        cpp_src = USER_DIR / "apps" / "cpp_hello.cc"
        cpp_elf = BUILD_DIR / "cpp_hello.elf"
        if zig:
            cpp_cmd = [zig, "c++", str(cpp_src),
                       "-target", ARCH["clang_musl_target"], "-static", "-pie", "-Os",
                       "-Wno-nullability-completeness", "-o", str(cpp_elf)]
            cpp_desc = "link cpp_hello.elf (zig libc++ for musl, static-pie)"
        else:
            cpp_cmd = [cxx, str(cpp_src),
                       "--target=" + ARCH["clang_musl_target"], "-nostdlibinc",
                       "-I", str(MUSL_INC), "-L", str(MUSL_LIB),
                       "-fPIE", "-static", "-pie", "-Os",
                       "-Wno-nullability-completeness", "-o", str(cpp_elf)]
            cpp_desc = "link cpp_hello.elf (clang++ musl, static-pie)"
        cpp_task = Task(
            name="cpp_hello.elf",
            cmd=cpp_cmd,
            out=cpp_elf, deps=[cpp_src],
            optional=True, group="link",
            description=cpp_desc,
        )
        tasks.append(cpp_task)
        user_elves.append(cpp_task)
    net_dir = NET_DIR
    net_cflags = KERNEL_CFLAGS + ["-I", str(net_dir)]
    net_c_sources = [
        ("rtl8139.o", ROOT / "drivers" / "net" / "rtl8139" / "rtl8139.c"),
        ("e1000.o", ROOT / "drivers" / "net" / "e1000" / "e1000.c"),
        ("virtio_net.o", ROOT / "drivers" / "net" / "virtio" / "virtio_net.c"),
        ("arp.o", net_dir / "arp.c"),
        ("ip.o", net_dir / "ip.c"),
        ("eth.o", net_dir / "eth.c"),
        ("icmp.o", net_dir / "icmp.c"),
        ("tcp.o", net_dir / "tcp.c"),
        ("udp.o", net_dir / "udp.c"),
        ("socket.o", net_dir / "socket.c"),
        ("net.o", net_dir / "net.c"),
    ]
    for stem, src in net_c_sources:
        tasks.append(task_cc(stem, src, BUILD_DIR / stem, tools, net_cflags,
                             cc=tools.kcc, kind=tools.kcc_kind))
    # Every kernel .c must be wired into one of the source lists above; a file
    # that is not would be silently absent from the link.
    check_kernel_sources_wired(kernel_c_sources, net_c_sources)
    font_src = ROOT / "lib" / "assets" / "font.ttf"
    font_kernel_ttf = BUILD_DIR / "font_kernel.ttf"
    tasks.append(task_python(
        "font_kernel.ttf",
        TOOLS / "make_font_subset.py",
        [str(font_src), str(font_kernel_ttf), "--charset=latin"],
        out=font_kernel_ttf,
    ))
    font_subset = BUILD_DIR / "font_subset.ttf"
    tasks.append(task_python(
        "font_subset.ttf",
        TOOLS / "make_font_subset.py",
        [str(font_src), str(font_subset), "--charset=gb2312-l1"],
        out=font_subset,
    ))
    tasks.append(Task(
        name="font_kernel.o",
        cmd=[tools.objcopy, "-I", "binary", "-O", "elf64-x86-64",
             "-B", "i386:x86-64", "--set-section-alignment", ".data=64",
             "font_kernel.ttf", "font_kernel.o"],
        cwd=BUILD_DIR, out=BUILD_DIR / "font_kernel.o", deps=[],
        description="embed font_kernel.ttf", group="objcopy",
    ))
    asset_specs = (("wallpaper.png", 1024, 768, 0),
                   ("pic1.png", 768, 512, 1),
                   ("pic2.png", 640, 480, 2))
    for nm, ww, hh, var in asset_specs:
        asset = task_python(
            nm, TOOLS / "make_wallpaper.py",
            [str(BUILD_DIR / nm), str(ww), str(hh), str(var)],
            out=BUILD_DIR / nm,
        )
        asset.group = "asset"
        asset.description = f"make_wallpaper.py → {nm}"
        tasks.append(asset)
    tasks.append(Task(
        name="wallpaper_kernel.o",
        cmd=[tools.objcopy, "-I", "binary", "-O", "elf64-x86-64",
             "-B", "i386:x86-64", "--set-section-alignment", ".data=64",
             "wallpaper.png", "wallpaper_kernel.o"],
        cwd=BUILD_DIR, out=BUILD_DIR / "wallpaper_kernel.o", deps=[],
        description="embed wallpaper.png", group="objcopy",
    ))
    if (ROOT / "logo.png").exists():
        tasks.append(Task(
            name="logo_kernel.o",
            cmd=[tools.objcopy, "-I", "binary", "-O", "elf64-x86-64",
                 "-B", "i386:x86-64", "--set-section-alignment", ".data=64",
                 str(ROOT / "logo.png"), "logo_kernel.o"],
            cwd=BUILD_DIR, out=BUILD_DIR / "logo_kernel.o", deps=[],
            description="embed logo.png", group="objcopy",
        ))
    kernel_objs_names = [
        "mb2_entry.o", "entry.o", "kernel.o", "cmdline.o", "mb2.o", "boot_info.o", "func.o", "ioc.o", "io.o", "idle.o", "acpi.o",
        "apic.o", "pit.o", "stub.o", "syscall_entry.o", "syscall_arch.o", "syscall_entry_c.o", "arch_paging.o", "idt.o", "interrupt.o", "early.o", "pic.o",
        "assert.o", "ssp.o", "str.o", "kprintf.o", "rand.o", "rbtree.o", "png.o", "ttf.o", "bitmap.o", "pool.o", "access.o", "kheap.o", "list.o",
        "switch.o", "thread.o", "sync.o", "percpu.o", "smp.o",
        "ap_tramp.o", "ioqueue.o", "tty.o", "pty.o", "keyboard.o", "rtc.o",
        "ide.o", "block.o", "nvme.o", "pci.o", "pbcache.o", "ext2.o", "ext4.o", "vfs.o", "fs.o", "inode.o",
        "dir.o", "file.o", "proc.o",
        "gdt.o", "tss.o", "process.o", "exec.o", "pe.o",
        "pipe.o", "ksyscall.o", "mmap.o", "futex.o",
        "linux_compat.o", "linux_compat_io.o", "linux_compat_fs.o", "linux_compat_proc.o", "win32.o", "win32_gdi.o", "signal.o", "file_syscall.o",
        "usyscall.o", "ustdio.o", "wait_exit.o", "fork.o", "clone.o",
        "lc_clone.o",
        "mouse.o", "gfx.o", "gpu.o", "display.o", "input.o", "udi.o",
        "udi_virtio.o", "udi_vmware.o", "font.o",
        "theme.o", "font_kernel.o", "wallpaper_kernel.o", "logo_kernel.o", "shm.o", "guiserver.o",
        "wm.o", "wm_anim.o", "wm_bar.o", "guiclients.o", "guiclients_term.o", "guiclients_monitor.o",
                "guiclients_png.o", "guiclients_files.o", "guiclients_edit.o", "gui.o", "x11.o", "x11_render.o", "x11_window.o", "x11_server.o",
        "rtl8139.o", "e1000.o", "virtio_net.o", "arp.o", "ip.o", "eth.o", "icmp.o",
        "tcp.o", "udp.o", "socket.o", "net.o",
        "sha256.o", "chacha20.o", "aes_gcm.o", "x25519.o", "p256.o", "rsa.o",
        "x509.o", "tls.o", "tls_roots.o", "tls_net.o", "dns.o",
    ]
    kernel_link_objs = [BUILD_DIR / n for n in kernel_objs_names]
    kernel_elf = BUILD_DIR / "kernel.elf"
    kernel_link_task = task_link(
        "kernel.elf", kernel_elf, tools, kernel_link_objs,
        script=ROOT / ARCH["kernel_linker_script"],
    )
    tasks.append(kernel_link_task)
    kernel_bin = BUILD_DIR / "kernel.bin"
    kernel_bin_task = Task(
        name="kernel.bin",
        cmd=[tools.objcopy, "-O", "binary", str(kernel_elf), str(kernel_bin)],
        out=kernel_bin, deps=[], description="strip ELF → flat binary",
        group="objcopy",
    )
    tasks.append(kernel_bin_task)
    floppy_img = BUILD_DIR / "floppy.img"
    tasks.append(task_python(
        "floppy.img",
        TOOLS / "mkfloppy.py",
        [str(BUILD_DIR / "boot.bin"),
         str(BUILD_DIR / "loader.bin"),
         str(BUILD_DIR / "kernel.bin"),
         str(floppy_img)],
        out=floppy_img,
    ))
    uefi_enabled = False
    uefi_esp = None
    if CONFIG.get("CONFIG_UEFI") == "y":
        uefi_dir = ARCH_DIR / "boot" / "uefi"
        bootx64 = BUILD_DIR / "BOOTX64.EFI"
        esp_img = BUILD_DIR / "esp.img"
        uefi_enabled = tools.kind == "clang" and bool(tools.lld_link)
        uefi_entry = task_nasm_uefi("uefi_entry.obj", uefi_dir / "entry.asm",
                                    BUILD_DIR / "uefi_entry.obj", tools)
        uefi_main = task_cc_uefi("uefi_main.obj", uefi_dir / "main.c",
                                 BUILD_DIR / "uefi_main.obj", tools, UEFI_CFLAGS)
        uefi_efi = task_link_uefi("BOOTX64.EFI", bootx64, tools,
                                  [BUILD_DIR / "uefi_entry.obj",
                                   BUILD_DIR / "uefi_main.obj"])
        uefi_esp = task_python("esp.img", TOOLS / "mkesp.py",
                               [str(BUILD_DIR), str(esp_img)], out=esp_img)
        uefi_esp.description = "mkesp.py → esp.img"
        uefi_esp.deps = [bootx64, kernel_elf]
        for t in (uefi_entry, uefi_main, uefi_efi, uefi_esp):
            t.optional = not uefi_enabled
        tasks.extend((uefi_entry, uefi_main, uefi_efi, uefi_esp))
    musl_env = _musl_buildenv(tools) if with_musl_lib else None
    plan_musl_enabled = musl_env is not None
    if plan_musl_enabled:
        sh, make, cc = musl_env["sh"], musl_env["make"], musl_env["cc"]
        cc_str = " ".join(cc)
        musl_script = (
            "set -e; set -o pipefail; "
            f"rm -rf {shlex.quote(str(MUSL_PREFIX))}; "
            f"mkdir -p {shlex.quote(str(MUSL_PREFIX))}; "
            f"cd {shlex.quote(str(MUSL_SRC))}; "
            f"rm -rf obj config.mak; "
            f"mkdir -p obj/include/bits; "
            f"sed -f tools/mkalltypes.sed arch/x86_64/bits/alltypes.h.in "
            f"include/alltypes.h.in > obj/include/bits/alltypes.h; "
            f"CC={shlex.quote(cc_str)} "
            f"./configure --prefix={shlex.quote(str(MUSL_PREFIX))} "
            f"--enable-shared --enable-static --disable-option-checking; "
            f"make -j\"$(nproc 2>/dev/null || echo 4)\" "
            f"2>&1 | tee {shlex.quote(str(MUSL_PREFIX / 'build.log'))}; "
            f"make install"
        )
        tasks.append(Task(
            name="musl-native-lib",
            cmd=[sh, "-c", musl_script],
            out=MUSL_PREFIX / "lib" / "libc.a",
            deps=[ROOT / "build.py"],
            optional=True,
            group="musl-lib",
            description="configure+make+install native musl 1.2.6",
        ))
        BUSYBOX_DIR = ROOT / "third_party" / "busybox"
        _musl_gcc = MUSL_PREFIX / "bin" / "musl-gcc"
        _musl_clang = MUSL_PREFIX / "bin" / "musl-clang"
        _musl_pick = (
            f"MC={shlex.quote(str(_musl_clang))}; "
            f"[ -x \"$MC\" ] || MC={shlex.quote(str(_musl_gcc))}; ")
        _busybox_pre = (f"test -x {shlex.quote(str(_musl_gcc))} || "
                        f"test -x {shlex.quote(str(_musl_clang))} || exit 0; " +
                        _musl_pick)
        _linux_inc = BUILD_DIR / "linux-include"
        tasks.append(Task(
            name="busybox-config",
            cmd=[sh, "-c",
                 _busybox_pre +
                 f"cd {shlex.quote(str(BUSYBOX_DIR))} && "
                 f"[ -f .config ] || {{ make defconfig >/dev/null 2>&1; "
                 f"sed -i 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config; "
                 f"yes '' | make oldconfig >/dev/null 2>&1; }}; true"],
            out=BUSYBOX_DIR / ".config",
            deps=[BUSYBOX_DIR / "Makefile"],
            optional=True, group="busybox",
            description="busybox defconfig (static)",
        ))
        tasks.append(Task(
            name="busybox-build",
            cmd=[sh, "-c",
                 _busybox_pre +
                 f"cd {shlex.quote(str(BUSYBOX_DIR))} && "
                 f"[ -d {shlex.quote(str(_linux_inc / 'linux'))} ] || "
                 f"{{ mkdir -p {shlex.quote(str(_linux_inc))} && "
                 f"cp -rL /usr/include/linux {shlex.quote(str(_linux_inc / 'linux'))} && "
                 f"cp -rL /usr/include/asm {shlex.quote(str(_linux_inc / 'asm'))} && "
                 f"cp -rL /usr/include/asm-generic {shlex.quote(str(_linux_inc / 'asm-generic'))} && "
                 f"cp -rL /usr/include/mtd {shlex.quote(str(_linux_inc / 'mtd'))}; }} && "
                 f"rm -f busybox && "
                 f"make -j4 CC=\"$MC\" "
                 f"CFLAGS={shlex.quote('-static -Os -I' + str(_linux_inc))} "
                 f"LDFLAGS={shlex.quote('-static -Wl,-Ttext-segment=0x8048000')} > bb.log 2>&1 || "
                 f"{{ grep -iE 'error:' bb.log | head -10; tail -5 bb.log; false; }} && "
                 f"cp busybox {shlex.quote(str(BUILD_DIR / 'busybox'))} && "
                 f"cp busybox {shlex.quote(str(BUILD_DIR / 'suidsh'))}"],
            out=BUILD_DIR / "busybox",
            deps=[BUSYBOX_DIR / ".config", BUSYBOX_DIR / "Makefile"],
            optional=True, group="busybox",
            description="build busybox (static musl)",
        ))
        musl_demo_c = task_cc("musl_demo.o", USER_DIR / "apps" / "musl_demo.c",
                              BUILD_DIR / "musl_demo.o", tools, MUSL_DEMO_CFLAGS)
        musl_demo_c.optional = True
        tasks.append(musl_demo_c)
        if with_tests:
            libc_tests_main = task_cc(
                "libc_tests_main.o", TESTS / "probes" / "libc_tests_main.c",
                BUILD_DIR / "libc_tests_main.o", tools, MUSL_DEMO_CFLAGS)
            libc_tests_main.optional = True
            tasks.append(libc_tests_main)
        tests_sources = [
            ("test_string.o", ROOT / "third_party" / "libc-testsuite" / "string.c"),
            ("test_qsort.o", ROOT / "third_party" / "libc-testsuite" / "qsort.c"),
            ("test_strtol.o", ROOT / "third_party" / "libc-testsuite" / "strtol.c"),
            ("test_strtod.o", ROOT / "third_party" / "libc-testsuite" / "strtod.c"),
            ("test_basename.o", ROOT / "third_party" / "libc-testsuite" / "basename.c"),
            ("test_dirname.o", ROOT / "third_party" / "libc-testsuite" / "dirname.c"),
            ("test_fnmatch.o", ROOT / "third_party" / "libc-testsuite" / "fnmatch.c"),
        ]
        if with_tests:
            for stem, src in tests_sources:
                t = task_cc(stem, src, BUILD_DIR / stem, tools,
                            MUSL_DEMO_CFLAGS)
                t.optional = True
                tasks.append(t)
        def link_musl_user(name, elf, objs):
            ld = tools.ld
            crt1 = MUSL_LIB / "crt1.o"
            crti = MUSL_LIB / "crti.o"
            crtn = MUSL_LIB / "crtn.o"
            cmd = [ld, "-nostdlib", "-static", "-pie", "--no-dynamic-linker",
                   "-z", "pack-relative-relocs",
                   "-e", "_start",
                   str(crt1), str(crti), *map(str, objs), str(crtn),
                   "-L", str(MUSL_LIB), "--start-group", "-lc", "--end-group",
                   "-o", str(elf)]
            return Task(name=name, cmd=cmd, out=elf, optional=True,
                        group="musl", description=f"link {elf.name} (native musl)",
                        cwd=BUILD_DIR)
        musl_demo_elf = link_musl_user(
            "musl_demo.elf", BUILD_DIR / "musl_demo.elf",
            [BUILD_DIR / "musl_demo.o"])
        tasks.append(musl_demo_elf)
        if with_tests:
            musl_abi_test_c = task_cc(
                "musl_abi_test.o", TESTS / "probes" / "musl_abi_test.c",
                BUILD_DIR / "musl_abi_test.o", tools, MUSL_DEMO_CFLAGS)
            musl_abi_test_c.optional = True
            tasks.append(musl_abi_test_c)
            tasks.append(link_musl_user(
                "musl_abi_test.elf", BUILD_DIR / "musl_abi_test.elf",
                [BUILD_DIR / "musl_abi_test.o"]))
            py_probe_c = task_cc(
                "musl_py_compat_probe.o", TESTS / "probes" / "py_compat_probe.c",
                BUILD_DIR / "py_compat_probe.o", tools, MUSL_DEMO_CFLAGS)
            py_probe_c.optional = True
            py_probe_c.group = "musl"
            tasks.append(py_probe_c)
            tasks.append(link_musl_user(
                "musl_py_compat_probe.elf", BUILD_DIR / "py_compat_probe.elf",
                [BUILD_DIR / "py_compat_probe.o"]))
        sh_c = task_cc("musl_sh.o", USER_DIR / "apps" / "sh.c", BUILD_DIR / "sh.o",
                       tools, MUSL_DEMO_CFLAGS)
        sh_c.optional = True
        sh_c.group = "musl"
        tasks.append(sh_c)
        sh_elf = link_musl_user("musl_sh.elf", BUILD_DIR / "sh.elf",
                                [BUILD_DIR / "sh.o"])
        tasks.append(sh_elf)
        if with_tests:
            libc_tests_elf = link_musl_user(
                "libc_testsuite.elf", BUILD_DIR / "libc_testsuite.elf",
                [BUILD_DIR / "libc_tests_main.o",
                 BUILD_DIR / "test_string.o", BUILD_DIR / "test_qsort.o",
                 BUILD_DIR / "test_strtol.o", BUILD_DIR / "test_strtod.o",
                 BUILD_DIR / "test_basename.o", BUILD_DIR / "test_dirname.o",
                 BUILD_DIR / "test_fnmatch.o"])
            tasks.append(libc_tests_elf)
        for stem in ("termios_probe", "pty_demo", "jc_demo", "at_probe",
                     "futex_bs_probe", "compat_stub_probe"):
            csrc = TESTS / "probes" / (stem + ".c")
            if not csrc.exists():
                csrc = USER_DIR / "apps" / (stem + ".c")
            if not with_tests and csrc.parent == PROBES:
                continue
            cobj = BUILD_DIR / (stem + ".o")
            ct = task_cc("musl_" + stem + ".o", csrc, cobj, tools,
                         MUSL_DEMO_CFLAGS + ["-fno-stack-protector", "-Os"])
            ct.optional = True
            tasks.append(ct)
            app_elf = link_musl_user("musl_" + stem + ".elf",
                                     BUILD_DIR / (stem + ".elf"), [cobj])
            tasks.append(app_elf)
        rustc = Path("/opt/cargo/bin/rustc")
        if rustc.exists():
            for stem in ("rust_hello", "rust_probe", "rust_probe2"):
                rs = TESTS / "probes" / (stem + ".rs")
                if not rs.exists():
                    rs = USER_DIR / "apps" / (stem + ".rs")
                if not with_tests and rs.parent == PROBES:
                    continue
                tasks.append(Task(
                    name="musl_rust_" + stem,
                    cmd=[str(rustc), "--target", RUST_TRIPLE,
                         "-O", "-C", "panic=abort", "-C", "debuginfo=0",
                         "-o", str(BUILD_DIR / (stem + ".elf")), str(rs)],
                    out=BUILD_DIR / (stem + ".elf"),
                    env={"CARGO_HOME": "/opt/cargo", "RUSTUP_HOME": "/opt/rustup"},
                    optional=True, group="musl",
                    description="rustc " + stem))
            cargo = Path("/opt/cargo/bin/cargo")
            if (FISH_CARGO_TOML.exists() and cargo.exists()
                    and CONFIG.get("CONFIG_FISH") == "y"):
                fish_script = (
                    "set -e; "
                    f"cd {shlex.quote(str(FISH_SRC))}; "
                    f"{shlex.quote(str(cargo))} build --release "
                    f"--target {RUST_TRIPLE} --no-default-features; "
                    f"cp {shlex.quote(str(FISH_BIN))} "
                    f"{shlex.quote(str(BUILD_DIR / 'fish.elf'))}"
                )
                tasks.append(Task(
                    name="musl_fish",
                    cmd=[sh, "-c", fish_script],
                    out=BUILD_DIR / "fish.elf",
                    deps=[FISH_CARGO_TOML],
                    env={"CARGO_HOME": "/opt/cargo", "RUSTUP_HOME": "/opt/rustup",
                         "CARGO_TARGET_DIR": str(FISH_TARGET),
                         ARCH["cargo_cc_var"]: str(_musl_clang)},
                    optional=True, group="musl",
                    description="cargo build fish.elf"))
        if (PCRE2_SRC / "configure").exists():
            pcre2_script = (
                "set -e; "
                f"cd {shlex.quote(str(PCRE2_SRC))}; " +
                _musl_pick +
                "rm -f config.status; "
                "CC=\"$MC\" ./configure "
                f"--prefix={shlex.quote(str(PCRE2_PREFIX))} "
                "--enable-static --disable-shared --disable-stack-protector "
                "--disable-pcre2grep-libz --disable-pcre2grep-libbz2 "
                "--disable-pcre2test-libedit --disable-pcre2test-libreadline "
                "CFLAGS='-Os -fPIC' >/dev/null 2>&1; "
                "make -j\"$(nproc 2>/dev/null || echo 4)\" >/dev/null; "
                "make install >/dev/null"
            )
            tasks.append(Task(
                name="pcre2-static-lib",
                cmd=[sh, "-c", pcre2_script],
                out=PCRE2_LIB / "libpcre2-8.a",
                deps=[MUSL_LIB / "libc.a"],
                optional=True, group="musl-lib",
                description="build static libpcre2-8 against musl"))
            p2_cflags = MUSL_DEMO_CFLAGS + ["-fno-stack-protector", "-Os",
                                             "-DPCRE2_STATIC",
                                             "-I", str(PCRE2_INC)]
            p2_obj = BUILD_DIR / "pcre2_demo.o"
            p2_c = task_cc("musl_pcre2_demo.o", USER_DIR / "apps" / "pcre2_demo.c",
                           p2_obj, tools, p2_cflags)
            p2_c.optional = True
            tasks.append(p2_c)
            ld = tools.ld
            p2_elf = Task(
                name="musl_pcre2_demo.elf",
                cmd=[ld, "-nostdlib", "-static", "-pie", "--no-dynamic-linker",
                     "-z", "pack-relative-relocs",
                     "-e", "_start",
                     str(MUSL_LIB / "crt1.o"), str(MUSL_LIB / "crti.o"),
                     str(p2_obj), str(MUSL_LIB / "crtn.o"),
                     "-L", str(MUSL_LIB), "-L", str(PCRE2_LIB),
                     "--start-group", "-lc", "-lpcre2-8", "--end-group",
                     "-o", str(BUILD_DIR / "pcre2_demo.elf")],
                out=BUILD_DIR / "pcre2_demo.elf",
                deps=[p2_obj, PCRE2_LIB / "libpcre2-8.a"],
                optional=True, group="musl", cwd=BUILD_DIR,
                description="link pcre2_demo.elf (musl + libpcre2-8)")
            tasks.append(p2_elf)
        for dst_name in ("libc.so", ARCH["musl_loader"]):
            tasks.append(Task(
                name=f"musl-dyn-{dst_name}",
                cmd=[sh, "-c",
                     f"cp {shlex.quote(str(MUSL_LIB / 'libc.so'))} "
                     f"{shlex.quote(str(BUILD_DIR / dst_name))}"],
                out=BUILD_DIR / dst_name,
                deps=[MUSL_LIB / "libc.so"],
                optional=True, group="musl-dyn",
                description=f"install {dst_name} into build/",
            ))
        dyn_lib_c = task_cc("dyn_lib.o", USER_DIR / "apps" / "dyn_lib.c",
                            BUILD_DIR / "dyn_lib.o", tools, MUSL_DEMO_CFLAGS)
        dyn_lib_c.optional = True
        dyn_lib_c.group = "musl-dyn"
        tasks.append(dyn_lib_c)
        dyn_lib_so = Task(
            name="libdyndemo.so",
            cmd=[tools.ld,
                 "-m", ARCH["ld_emulation"], "-shared", "-nostdlib", "-e", "0",
                 "-T", str(ROOT / ARCH["user_dyn_linker_script"]),
                 str(BUILD_DIR / "dyn_lib.o"),
                 "-L", str(MUSL_LIB), "-lc",
                 "-o", str(BUILD_DIR / "libdyndemo.so")],
            out=BUILD_DIR / "libdyndemo.so",
            deps=[BUILD_DIR / "dyn_lib.o"],
            optional=True, group="musl-dyn",
            description="link libdyndemo.so (shared)",
        )
        tasks.append(dyn_lib_so)
        dyn_demo_c = task_cc("dyn_demo.o", USER_DIR / "apps" / "dyn_demo.c",
                             BUILD_DIR / "dyn_demo.o", tools, MUSL_DEMO_CFLAGS)
        dyn_demo_c.optional = True
        dyn_demo_c.group = "musl-dyn"
        tasks.append(dyn_demo_c)
        def link_musl_dynamic(name, elf, objs, needed_dir):
            ld = tools.ld
            cmd = [ld, "-m", ARCH["ld_emulation"], "-nostdlib", "-pie",
                   "--dynamic-linker", "/lib/" + ARCH["musl_loader"],
                   "-T", str(ROOT / ARCH["user_dyn_linker_script"]), "-e", "_start",
                   str(MUSL_LIB / "Scrt1.o"), str(MUSL_LIB / "crti.o"),
                   *map(str, objs), str(MUSL_LIB / "crtn.o"),
                   "-L", str(MUSL_LIB), "-L", str(needed_dir),
                   "-lc", "-ldyndemo",
                   "-o", str(elf)]
            return Task(name=name, cmd=cmd, out=elf, optional=True,
                        group="musl-dyn",
                        description=f"link {elf.name} (dynamic, PT_INTERP)",
                        cwd=BUILD_DIR)
        dyn_demo_elf = link_musl_dynamic(
            "dyn_demo.elf", BUILD_DIR / "dyn_demo.elf",
            [BUILD_DIR / "dyn_demo.o"], BUILD_DIR)
        dyn_demo_elf.deps = [BUILD_DIR / "dyn_demo.o",
                             BUILD_DIR / "libdyndemo.so"]
        tasks.append(dyn_demo_elf)
        sysroot_dir = BUILD_DIR / "sysroot"
        sysroot_setup = (
            f"mkdir -p {shlex.quote(str(sysroot_dir / 'usr' / 'lib'))} "
            f"{shlex.quote(str(sysroot_dir / 'lib'))} && "
            f"cp -r {shlex.quote(str(MUSL_INC))}/. "
            f"{shlex.quote(str(sysroot_dir / 'usr' / 'include'))}/ && "
            f"cp {shlex.quote(str(MUSL_LIB / 'crt1.o'))} "
            f"{shlex.quote(str(MUSL_LIB / 'Scrt1.o'))} "
            f"{shlex.quote(str(MUSL_LIB / 'rcrt1.o'))} "
            f"{shlex.quote(str(MUSL_LIB / 'crti.o'))} "
            f"{shlex.quote(str(MUSL_LIB / 'crtn.o'))} "
            f"{shlex.quote(str(sysroot_dir / 'usr' / 'lib'))}/ && "
            f"cp {shlex.quote(str(MUSL_LIB / 'libc.so'))} "
            f"{shlex.quote(str(sysroot_dir / 'usr' / 'lib' / 'libc.so'))} && "
            f"cp {shlex.quote(str(MUSL_LIB / 'libc.so'))} "
            f"{shlex.quote(str(sysroot_dir / 'lib' / ARCH['musl_loader']))}"
        )
        tasks.append(Task(
            name="musl-sysroot",
            cmd=[sh, "-c", sysroot_setup],
            out=sysroot_dir / "usr" / "lib" / "libc.so",
            deps=[MUSL_LIB / "libc.so", MUSL_LIB / "Scrt1.o",
                  MUSL_LIB / "rcrt1.o", MUSL_LIB / "crti.o",
                  MUSL_LIB / "crtn.o", MUSL_LIB / "crt1.o"],
            optional=True, group="musl-dyn",
            description="assemble musl sysroot (headers+crt+libc.so+ldso)"))
        dyn_hello_elf = Task(
            name="dyn_hello.elf",
            cmd=[*tools.cc, "--target=x86_64-unknown-linux-musl",
                 f"--sysroot={sysroot_dir}",
                 "-O2", "-fstack-protector-strong", "-fPIE", "-pie",
                 str(USER_DIR / "apps" / "dyn_hello.c"),
                 "-o", str(BUILD_DIR / "dyn_hello.elf")],
            out=BUILD_DIR / "dyn_hello.elf",
            deps=[USER_DIR / "apps" / "dyn_hello.c",
                  sysroot_dir / "usr" / "lib" / "libc.so"],
            optional=True, group="musl-dyn",
            description="compile dyn_hello.elf (clang driver + sysroot, no manual ld)")
        tasks.append(dyn_hello_elf)
    tasks.append(task_config())
    test_hd_img = BUILD_DIR / "test_hd.img"
    test_hd_task = task_python(
        "test_hd.img",
        TOOLS / "make_ext4.py",
        [str(BUILD_DIR), str(test_hd_img)],
        out=test_hd_img,
    )
    test_hd_task.deps = [kernel_link_task, kernel_bin_task]
    if CONFIG.get("CONFIG_UEFI") == "y" and uefi_enabled:
        test_hd_task.deps.append(uefi_esp)
    test_hd_task.deps.extend(user_elves)
    test_hd_task.description = "make_ext4.py → test_hd.img (+nvme.img)"
    test_hd_task.group = "image"
    tasks.append(test_hd_task)
    return BuildPlan(tasks=tasks, user_elves=user_elves,
                     musl_enabled=plan_musl_enabled)
@dataclass
class BuildPlan:
    tasks: List[Task]
    user_elves: List[Task]
    musl_enabled: bool = False
    def all(self) -> List[Task]:
        return list(self.tasks) + list(self.user_elves)
def execute_plan(plan: BuildPlan, tools: Tools, console: Console,
                 jobs: int = 1) -> BuildStats:
    stats = BuildStats()
    overall_t0 = time.perf_counter()
    def run_task(task: Task) -> None:
        t0 = time.perf_counter()
        if task.out and task.out.exists():
            out_mtime = task.out.stat().st_mtime
            stale = task.group in ("link", "objcopy")
            for dep_path in task.dep_paths():
                if dep_path.exists() and dep_path.stat().st_mtime > out_mtime:
                    stale = True
                    break
            if not stale:
                stats.cache_hit += 1
                stats.timings[task.name] = (time.perf_counter() - t0, "cached")
                return
        try:
            res = run(task.cmd, cwd=task.cwd, env=task.env)
        except FileNotFoundError as exc:
            if task.optional:
                console.warn(f"{task.name} 跳过: {exc}")
                return
            stats.failed += 1
            console.fail(f"{task.name}: {exc}")
            raise
        if not res.ok:
            if task.optional:
                console.warn(f"{task.name} 跳过 (exit {res.returncode}, "
                             f"原生 musl 不可用不影响其它构建)")
                return
            stats.failed += 1
            console.fail(f"{task.name} (exit {res.returncode})")
            for line in (res.stderr or res.stdout).splitlines()[-30:]:
                console.writeln(f"          {line}")
            raise SystemExit(res.returncode)
        stats.compiled += 1
        stats.timings[task.name] = (time.perf_counter() - t0, "built")
        for line in (res.stderr or "").splitlines():
            if "warning:" not in line:
                continue
            entry = f"{task.name}: {line.strip()}"
            stats.warn_list.append(entry)
            if any(k in line for k in KNOWN_VA_WARNINGS):
                stats.known_warnings += 1
            else:
                stats.warnings += 1
                stats.real_warn_list.append(entry)
    def fmt_dur(seconds: float) -> str:
        if seconds < 0.05:
            return "(<0.1s)"
        if seconds < 10:
            return f"({seconds:.2f}s)"
        return f"({seconds/60:.1f}min)"
    def c_dim(s: str) -> str:
        return f"{console._c(Ansi.DIM)}{console._c(Ansi.GRAY)}{s}{console._c(Ansi.RESET)}"
    total_steps = 13 + (1 if any(t.group == "uefi" for t in plan.tasks) else 0) \
        + (1 if any(t.group == "image" for t in plan.tasks) else 0)
    s = 1
    console.step_header(s, total_steps, "Resolving Kconfig")
    for t in (t for t in plan.tasks if t.name == "kconfig"):
        run_task(t)
        console.ok(f"{t.description}  {c_dim(fmt_dur(stats.timings[t.name][0]))}")
    s += 1
    console.step_header(s, total_steps, "Assembling boot sectors")
    for t in plan.tasks[:3]:
        run_task(t)
        console.ok(f"{t.description}  {c_dim(fmt_dur(stats.timings[t.name][0]))}")
    s += 1
    console.step_header(s, total_steps, "Compiling kernel assembly")
    asm_kern = [t for t in plan.tasks if t.group == "asm" and t.name in
                ("func.o", "io.o", "stub.o", "entry.o", "switch.o", "idle.o",
                 "mb2_entry.o", "syscall_entry.o", "ap_trampoline.bin")]
    for t in asm_kern:
        run_task(t)
        console.ok(f"{t.description}  {c_dim(fmt_dur(stats.timings[t.name][0]))}")
    for t in (t for t in plan.tasks if t.name == "ap_tramp.o"):
        run_task(t)
    s += 1
    console.step_header(s, total_steps, "Compiling kernel C objects")
    for t in (t for t in plan.tasks if t.group == "musl-headers"):
        run_task(t)
        console.ok(f"{t.description}  {c_dim(fmt_dur(stats.timings[t.name][0]))}")
    cc_kern = [t for t in plan.tasks if t.group == "cc" and t.name.endswith(".o")
               and not t.name.startswith("up_") and not t.name.startswith("lc_")
               and not t.name.startswith("musl_") and not t.name.startswith("test_")
               and not t.name.startswith("libc_tests_main")]
    with console.progress(len(cc_kern), "kernel C", Ansi.BR_CYN) as update:
        for i, t in enumerate(cc_kern, 1):
            run_task(t)
            update(i, t.description)
    s += 1
    console.step_header(s, total_steps, "Compiling user & lib objects")
    user_lib = [t for t in plan.tasks if t.group == "cc" and t.name.startswith("up_")]
    prog_obj = [t for t in plan.tasks if t.group == "cc" and t.name.startswith("up_")
                and t.name not in ("up_start.o",)]
    up_start = [t for t in plan.tasks if t.name == "up_start.o"]
    with console.progress(len(user_lib) + len(prog_obj) + len(up_start),
                          "user C", Ansi.BR_YEL) as update:
        i = 0
        for grp in (up_start, user_lib, prog_obj):
            for t in grp:
                i += 1
                run_task(t)
                update(i, t.description)
    s += 1
    console.step_header(s, total_steps, "Building lc_demo")
    lc_tasks = [t for t in plan.tasks if t.group in ("asm","cc","link","objcopy") and "lc_" in t.name]
    with console.progress(len(lc_tasks), "lc_demo", Ansi.BR_MAG) as update:
        for i, t in enumerate(lc_tasks, 1):
            run_task(t)
            update(i, t.description)
    s += 1
    console.step_header(s, total_steps, "Building native musl (configure+make)")
    if not plan.musl_enabled:
        console.info("原生 musl 编译已跳过 (未开 --with-musl-lib 或缺少 sh/make/cc)")
    else:
        musl_lib_tasks = [t for t in plan.tasks if t.group == "musl-lib"]
        with console.progress(len(musl_lib_tasks), "musl lib", Ansi.BR_BLU) as update:
            for i, t in enumerate(musl_lib_tasks, 1):
                run_task(t)
                update(i, t.description)
    s += 1
    console.step_header(s, total_steps, "Building musl demos & libc testsuite")
    musl_lib_ok = (MUSL_PREFIX / "lib" / "libc.a").exists()
    if not plan.musl_enabled:
        console.info("musl demo/testsuite 跳过 (原生 musl 未启用)")
    elif not musl_lib_ok:
        console.info("musl 库未生成 (configure/make 失败或被跳过), "
                     "demo/testsuite 跳过 — 不影响内核/用户程序")
    else:
        musl_tasks = [t for t in plan.tasks if
                      t.group == "musl-dyn" or
                      (t.group in ("cc", "link", "musl") and
                       ("musl_" in t.name or "test_" in t.name or
                        "libc_tests" in t.name))]
        with console.progress(len(musl_tasks), "musl", Ansi.BR_BLU) as update:
            for i, t in enumerate(musl_tasks, 1):
                run_task(t)
                update(i, t.description)
    s += 1
    console.step_header(s, total_steps, "Building busybox")
    busybox_tasks = [t for t in plan.tasks if t.group == "busybox"]
    with console.progress(len(busybox_tasks), "busybox", Ansi.BR_BLU) as update:
        for i, t in enumerate(busybox_tasks, 1):
            run_task(t)
            update(i, t.description)
    s += 1
    console.step_header(s, total_steps, "Generating font & image assets")
    font_py = [t for t in plan.tasks
               if (t.group == "python" and "font" in t.name) or
               t.group == "asset"]
    with console.progress(len(font_py), "assets", Ansi.BR_MAG) as update:
        i = 0
        for t in font_py:
            i += 1
            run_task(t)
            update(i, t.description)
    s += 1
    console.step_header(s, total_steps, "Linking user ELFs (remaining)")
    user_elf_tasks = list(plan.user_elves)
    with console.progress(len(user_elf_tasks), "user ELFs", Ansi.BR_GRN) as update:
        for i, t in enumerate(user_elf_tasks, 1):
            run_task(t)
            update(i, t.description)
    s += 1
    console.step_header(s, total_steps, "Linking kernel image")
    kernel_link = [t for t in plan.tasks
                   if t.group in ("link", "objcopy") and "kernel" in t.name]
    with console.progress(len(kernel_link), "kernel link", Ansi.BR_CYN) as update:
        for i, t in enumerate(kernel_link, 1):
            run_task(t)
            update(i, t.description)
    s += 1
    console.step_header(s, total_steps, "Packing floppy image")
    floppy_task = [t for t in plan.tasks if t.name == "floppy.img"][0]
    t0 = time.perf_counter()
    res = run(floppy_task.cmd)
    if not res.ok:
        console.fail(f"mkfloppy.py failed (exit {res.returncode})")
        for line in (res.stderr or res.stdout).splitlines()[-20:]:
            console.writeln(f"          {line}")
        raise SystemExit(res.returncode)
    dur = time.perf_counter() - t0
    console.ok(f"build/floppy.img  {c_dim(fmt_dur(dur))}")
    s += 1
    uefi_tasks = [t for t in plan.tasks
                  if t.group == "uefi" or t.name == "esp.img"]
    if uefi_tasks:
        console.step_header(s, total_steps, "Building UEFI bootloader")
        with console.progress(len(uefi_tasks), "UEFI", Ansi.BR_MAG) as update:
            for i, t in enumerate(uefi_tasks, 1):
                run_task(t)
                update(i, t.description)
    s += 1
    image_tasks = [t for t in plan.tasks if t.group == "image"]
    if image_tasks:
        console.step_header(s, total_steps, "Building disk images")
        with console.progress(len(image_tasks), "images", Ansi.BR_WHT) as update:
            for i, t in enumerate(image_tasks, 1):
                run_task(t)
                update(i, t.description)
    stats.timings["__total__"] = (time.perf_counter() - overall_t0, "overall")
    return stats
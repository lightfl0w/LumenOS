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
              LC_CFLAGS, COMPILE_COMMANDS)

class Ansi:
    RESET   = "\x1b[0m"
    BOLD    = "\x1b[1m"
    DIM     = "\x1b[2m"
    ITALIC  = "\x1b[3m"
    UNDER   = "\x1b[4m"
    REV     = "\x1b[7m"
    RED     = "\x1b[31m"
    GREEN   = "\x1b[32m"
    YELLOW  = "\x1b[33m"
    BLUE    = "\x1b[34m"
    MAGENTA = "\x1b[35m"
    CYAN    = "\x1b[36m"
    GRAY    = "\x1b[90m"
    BR_RED  = "\x1b[91m"
    BR_GRN  = "\x1b[92m"
    BR_YEL  = "\x1b[93m"
    BR_BLU  = "\x1b[94m"
    BR_MAG  = "\x1b[95m"
    BR_CYN  = "\x1b[96m"
    BR_WHT  = "\x1b[97m"
    CURSOR_HIDE = "\x1b[?25l"
    CURSOR_SHOW = "\x1b[?25h"
    ERASE_LINE  = "\x1b[2K"
    CR          = "\r"
def _enable_vt_on_windows() -> None:
    if os.name != "nt":
        return
    try:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        for handle_id in (-11, -12):
            handle = kernel32.GetStdHandle(handle_id)
            mode = ctypes.c_uint32()
            if kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
                kernel32.SetConsoleMode(handle, mode.value | 0x4)
    except Exception:
        pass
def color_enabled(no_color_flag: bool, stream=sys.stdout) -> bool:
    if no_color_flag:
        return False
    if not hasattr(stream, "isatty") or not stream.isatty():
        return True
    return True
@dataclass
class Tools:
    nasm:    str
    cc:      List[str]
    ld:      str
    objcopy: str
    python:  str
    kind:    str = "clang"
    kcc:     List[str] = field(default_factory=list)
    kcc_kind: str = "clang"
    lld_link: str = ""
_LLVM_BIN_DIR_CACHE: Optional[List[str]] = None
def _llvm_bin_dirs() -> List[str]:
    global _LLVM_BIN_DIR_CACHE
    if _LLVM_BIN_DIR_CACHE is None:
        dirs: List[str] = []
        for pat in LLVM_BIN_GLOBS:
            dirs.extend(sorted(glob.glob(pat), reverse=True))
        _LLVM_BIN_DIR_CACHE = dirs
    return _LLVM_BIN_DIR_CACHE
def _find(name: str, candidates: Sequence[str]) -> str:
    llvm_dirs = _llvm_bin_dirs()
    for c in candidates:
        path = shutil.which(c)
        if path is not None:
            if os.name == "nt":
                base, ext = os.path.splitext(path)
                if ext.lower() != ".exe":
                    exe_path = base + ".exe"
                    if os.path.exists(exe_path):
                        path = exe_path
            return path
        for d in llvm_dirs:
            p = Path(d) / c
            if p.is_file() and os.access(p, os.X_OK):
                return str(p)
    raise FileNotFoundError(
        f"Could not find any of: {', '.join(candidates)} (needed for `{name}`)."
    )
def _resolve_cc() -> Tuple[List[str], str]:
    for c in CC_CLANG_CANDIDATES:
        path = shutil.which(c)
        if path:
            return [path], "clang"
    zig = shutil.which("zig")
    if zig:
        return [zig, "cc"], "zig"
    for c in ("gcc", "cc"):
        path = shutil.which(c)
        if path:
            return [path], "gcc"
    raise FileNotFoundError(
        "Could not find any C compiler (clang, zig cc, gcc)."
    )
def detect_tools() -> Tools:
    cc, kind = _resolve_cc()
    try:
        lld_link = _find("lld-link", [ARCH["uefi_linker"], "lld-link"])
    except FileNotFoundError:
        lld_link = ""
    return Tools(
        nasm    = _find("nasm",    ["nasm"]),
        cc      = cc,
        ld      = _find("ld",      ["ld.lld", "lld-link", "x86_64-elf-ld", "ld"]),
        objcopy = _find("objcopy", ["llvm-objcopy", "objcopy", "x86_64-elf-objcopy"]),
        python  = sys.executable,
        kind    = kind,
        kcc     = cc,
        kcc_kind = kind,
        lld_link = lld_link,
    )
@dataclass
class CmdResult:
    returncode: int
    stdout: str
    stderr: str
    duration: float
    @property
    def ok(self) -> bool:
        return self.returncode == 0
CMD_TIMEOUT = 600


def run(cmd: Sequence[str], cwd: Optional[Path] = None,
        env: Optional[dict] = None, quiet: bool = False) -> CmdResult:
    start = time.perf_counter()
    try:
        proc = subprocess.run(
            list(cmd),
            cwd=str(cwd) if cwd else None,
            env={**os.environ, **(env or {})},
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=CMD_TIMEOUT,
        )
    except subprocess.TimeoutExpired:
        raise SystemExit(f"命令超时 ({CMD_TIMEOUT}s): {' '.join(map(str, cmd))}")
    dur = time.perf_counter() - start
    return CmdResult(proc.returncode, proc.stdout, proc.stderr, dur)
class Console:
    def __init__(self, use_color: bool):
        self.use_color = use_color
    def _c(self, code: str) -> str:
        return code if self.use_color else ""
    def write(self, s: str) -> None:
        sys.stdout.write(s)
        sys.stdout.flush()
    def writeln(self, s: str = "") -> None:
        self.write(s + "\n")
    def banner(self, version: str) -> None:
        c = self
        bar = "═" * 70
        c.writeln()
        c.writeln(f"{c._c(Ansi.CYAN)}{c._c(Ansi.BOLD)}{bar}{c._c(Ansi.RESET)}")
        c.writeln(f"{c._c(Ansi.BR_CYN)}{c._c(Ansi.BOLD)}  LumenOS   "
                  f"{c._c(Ansi.DIM)}{c._c(Ansi.GRAY)}build system  "
                  f"{c._c(Ansi.RESET)}{c._c(Ansi.DIM)}·  {version}{c._c(Ansi.RESET)}")
        c.writeln(f"{c._c(Ansi.CYAN)}{c._c(Ansi.BOLD)}{bar}{c._c(Ansi.RESET)}")
        c.writeln()
    def step_header(self, idx: int, total: int, title: str, hint: str = "") -> None:
        c = self
        bullet = f"{c._c(Ansi.BR_YEL)}▶{c._c(Ansi.RESET)}"
        idx_str = f"{c._c(Ansi.GRAY)}[{idx}/{total}]{c._c(Ansi.RESET)}"
        title_str = f"{c._c(Ansi.BOLD)}{c._c(Ansi.BR_WHT)}{title}{c._c(Ansi.RESET)}"
        hint_str = (f"  {c._c(Ansi.DIM)}{c._c(Ansi.GRAY)}{hint}{c._c(Ansi.RESET)}" if hint else "")
        c.writeln(f"  {bullet} {idx_str}  {title_str}{hint_str}")
        c.writeln(f"  {c._c(Ansi.GRAY)}{'─' * 66}{c._c(Ansi.RESET)}")
    def ok(self, msg: str) -> None:
        self.writeln(f"      {self._c(Ansi.GREEN)}✓{self._c(Ansi.RESET)}  {msg}")
    def info(self, msg: str) -> None:
        self.writeln(f"      {self._c(Ansi.CYAN)}·{self._c(Ansi.RESET)}  {msg}")
    def warn(self, msg: str) -> None:
        self.writeln(f"      {self._c(Ansi.YELLOW)}!{self._c(Ansi.RESET)}  {msg}")
    def fail(self, msg: str) -> None:
        self.writeln(f"      {self._c(Ansi.RED)}✗{self._c(Ansi.RESET)}  {msg}")
    @contextmanager
    def progress(self, total: int, label: str, color: str = Ansi.BR_CYN):
        c = self
        width = 38
        started = time.perf_counter()
        state = {"done": 0, "msg": ""}
        def render() -> None:
            pct = (state["done"] / total) if total else 1.0
            fill = int(round(width * pct))
            bar = "█" * fill + "░" * (width - fill)
            elapsed = time.perf_counter() - started
            eta = (elapsed / state["done"]) * (total - state["done"]) if state["done"] else 0.0
            tail = state["msg"][:40]
            sys.stdout.write(
                f"\r      {c._c(color)}▐{bar}▌{c._c(Ansi.RESET)} "
                f"{c._c(Ansi.BOLD)}{pct*100:5.1f}%{c._c(Ansi.RESET)} "
                f"{c._c(Ansi.GRAY)}{state['done']:>3}/{total:<3} "
                f"{c._c(Ansi.DIM)}{tail:<40} "
                f"{c._c(Ansi.GRAY)}elap {elapsed:5.1f}s eta {eta:4.1f}s{c._c(Ansi.RESET)}"
            )
            sys.stdout.flush()
        def update(done: int, msg: str = "") -> None:
            state["done"] = done
            state["msg"] = msg
            render()
        c.write(c._c(Ansi.CURSOR_HIDE))
        try:
            update(0, label)
            yield update
        finally:
            update(total, label)
            sys.stdout.write("\n")
            sys.stdout.flush()
            c.write(c._c(Ansi.CURSOR_SHOW))
    def summary(self, rows: Sequence[Tuple[str, str, str]]) -> None:
        c = self
        c.writeln()
        import json
        with open(ROOT / "compile_commands.json", "w", encoding="utf-8") as f:
            json.dump(COMPILE_COMMANDS, f, indent=1)
        c.writeln(f"  {c._c(Ansi.BOLD)}{c._c(Ansi.BR_WHT)}Summary{c._c(Ansi.RESET)}")
        c.writeln(f"  {c._c(Ansi.GRAY)}{'─' * 66}{c._c(Ansi.RESET)}")
        for label, value, color in rows:
            c.writeln(f"      {c._c(Ansi.GRAY)}{label:<14}{c._c(Ansi.RESET)}"
                      f" {c._c(color)}{c._c(Ansi.BOLD)}{value}{c._c(Ansi.RESET)}")
        c.writeln()
def show_failure_hint(console: Console, missing: List[str]) -> None:
    console.writeln()
    console.fail("Missing required toolchain components:")
    for m in missing:
        console.writeln(f"          • {m}")
    console.writeln()
    console.info("Install one of the supported toolchains:")
    console.writeln("          • Linux   : apt install nasm clang lld  OR  apt install nasm zig")
    console.writeln("          • Arch    : pacman -S nasm clang lld")
    console.writeln("          • macOS   : brew install nasm llvm")
    console.writeln("          • Windows : install llvm, nasm, lld (winget/choco/scoop)")
    console.writeln()
    console.info("Compiler priority: clang > zig cc > gcc")
    console.writeln()
def ensure_config() -> None:
    if CONFIG_FILE.exists():
        return
    res = run([sys.executable, str(CONFIG_SCRIPT), "defconfig",
               f"{DEFAULT_ARCH_KEY}_defconfig"])
    if not res.ok:
        sys.stderr.write((res.stderr or res.stdout or "kconfig defconfig failed") + "\n")
        raise SystemExit(res.returncode)
def task_config() -> Task:
    return Task(
        name="kconfig",
        cmd=[sys.executable, str(CONFIG_SCRIPT), "syncconfig"],
        out=AUTOCONF_H, deps=[CONFIG_FILE], group="config",
        description="resolve Kconfig → build/config/autoconf.h",
    )
def do_clean(console: Console) -> None:
    if BUILD_DIR.exists():
        shutil.rmtree(BUILD_DIR)
        console.ok(f"removed {BUILD_DIR}")
    else:
        console.info(f"{BUILD_DIR} already absent")
BUILD_ENV_STAMP = BUILD_DIR / ".buildenv"
def _build_env_fingerprint(tools: Tools) -> str:
    flags = " ".join(KERNEL_CFLAGS + UP_CFLAGS_64 + MUSL64_BASE)
    parts = [" ".join(tools.cc), tools.kind, tools.ld, tools.objcopy, tools.nasm,
             flags]
    try:
        res = run([*tools.cc, "--version"])
        line = (res.stdout or res.stderr).splitlines()
        if line:
            parts.append(line[0].strip())
    except OSError:
        pass
    return " | ".join(parts)
def restamp_build_env(tools: Tools, console: Console) -> None:
    fingerprint = _build_env_fingerprint(tools)
    old = ""
    if BUILD_ENV_STAMP.exists():
        try:
            old = BUILD_ENV_STAMP.read_text(encoding="utf-8").strip()
        except OSError:
            old = ""
    if old == fingerprint:
        return
    stale_objs = BUILD_DIR.exists() and any(BUILD_DIR.glob("*.o"))
    if old or stale_objs:
        console.warn("build environment changed, forcing rebuild:")
        if old:
            console.warn(f"  was: {old}")
        console.warn(f"  now: {fingerprint}")
        do_clean(console)
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    BUILD_ENV_STAMP.write_text(fingerprint + "\n", encoding="utf-8")

from .tasks import Task

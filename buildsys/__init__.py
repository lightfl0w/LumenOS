from __future__ import annotations
import io, sys
from pathlib import Path
from typing import Sequence

def _force_utf8_stdout() -> None:
    for stream_name in ("stdout", "stderr"):
        stream = getattr(sys, stream_name, None)
        if stream is None:
            continue
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, io.UnsupportedOperation):
            try:
                setattr(sys, stream_name,
                        io.TextIOWrapper(stream.buffer, encoding="utf-8",
                                         errors="replace"))
            except Exception:
                pass
_force_utf8_stdout()
ROOT       = Path(__file__).resolve().parent.parent
USER_DIR   = ROOT / "user"
MM_DIR     = ROOT / "mm"
FS_DIR     = ROOT / "fs"
NET_DIR    = ROOT / "net"
KERNEL_DIR = ROOT / "kernel"
BUILD_DIR  = ROOT / "build"
SCRIPTS    = ROOT / "scripts"
TOOLS      = ROOT / "tools"
TESTS      = ROOT / "tests"
PROBES     = TESTS / "probes"
MUSL_SRC   = ROOT / "third_party" / "musl"
CONFIG_FILE   = ROOT / ".config"
CONFIG_SCRIPT = SCRIPTS / "kconfig.py"
AUTOCONF_H    = BUILD_DIR / "config" / "autoconf.h"
ARCH_PROFILES = {
    "x86_64": {
        "arch_dir": "arch/x86_64",
        "musl_arch": "x86_64",
        "freestanding_key": "x86_64-freestanding",
        "freestanding_triple": "x86_64-unknown-none",
        "rust_triple": "x86_64-unknown-linux-musl",
        "kernel_arch_cflags": ["-mcmodel=large", "-mno-red-zone", "-mstackrealign"],
        "ld_emulation": "elf_x86_64",
        "kernel_linker_script": "arch/x86_64/linker.ld",
        "user_dyn_linker_script": "arch/x86_64/user_dyn.ld",
        "nasm_elf_format": "elf64",
        "uefi_clang_target": "x86_64-unknown-windows",
        "uefi_nasm_format": "win64",
        "uefi_linker": "lld-link",
        "ovmf_dir": "/usr/share/edk2-ovmf/x64",
        "objcopy_tramp_fmt": "elf64-x86-64",
        "objcopy_tramp_arch": "i386:x86-64",
        "qemu_system": "qemu-system-x86_64",
        "clang_musl_target": "x86_64-linux-musl",
        "musl_loader": "ld-musl-x86_64.so.1",
        "cargo_cc_var": "CC_x86_64_unknown_linux_musl",
        "clang_triples": {
            "x86_64-freestanding": "x86_64-unknown-none",
            "i386-freestanding":   "i386-unknown-none",
            "x86_64-linux-musl":   "x86_64-linux-musl",
            "x86_64-linux-gnu":    "x86_64-unknown-linux-gnu",
        },
    },
    "aarch64": {
        "arch_dir": "arch/arm64",
        "musl_arch": "aarch64",
        "freestanding_key": "aarch64-freestanding",
        "freestanding_triple": "aarch64-unknown-none",
        "rust_triple": "aarch64-unknown-linux-musl",
        "kernel_arch_cflags": ["-mgeneral-regs-only", "-mno-outline-atomics"],
        "ld_emulation": "aarch64elf",
        "kernel_linker_script": "arch/x86_64/linker.ld",
        "user_dyn_linker_script": "arch/x86_64/user_dyn.ld",
        "nasm_elf_format": "elf64",
        "uefi_clang_target": "aarch64-unknown-windows",
        "uefi_nasm_format": "win64",
        "uefi_linker": "lld-link",
        "ovmf_dir": "/usr/share/edk2-ovmf/aarch64",
        "objcopy_tramp_fmt": "elf64-littleaarch64",
        "objcopy_tramp_arch": "aarch64",
        "qemu_system": "qemu-system-aarch64",
        "clang_musl_target": "aarch64-linux-musl",
        "musl_loader": "ld-musl-aarch64.so.1",
        "cargo_cc_var": "CC_aarch64_unknown_linux_musl",
        "clang_triples": {
            "aarch64-freestanding": "aarch64-unknown-none",
            "aarch64-linux-musl":   "aarch64-linux-musl",
            "aarch64-linux-gnu":    "aarch64-unknown-linux-gnu",
        },
    },
}
DEFAULT_ARCH_KEY = "x86_64"
ARCH_KEYS = {
    "CONFIG_ARCH_X86_64": "x86_64",
    "CONFIG_ARCH_AARCH64": "aarch64",
}
def read_config(path: Path = CONFIG_FILE) -> dict:
    cfg: dict = {}
    if not path.exists():
        return cfg
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, val = line.partition("=")
        val = val.strip()
        if len(val) >= 2 and val[0] == '"' and val[-1] == '"':
            val = val[1:-1]
        cfg[key.strip()] = val
    return cfg
def active_arch(cfg: dict) -> dict:
    for sym, key in ARCH_KEYS.items():
        if cfg.get(sym) == "y":
            return {"key": key, **ARCH_PROFILES[key]}
    return {"key": DEFAULT_ARCH_KEY, **ARCH_PROFILES[DEFAULT_ARCH_KEY]}
CONFIG = read_config()
ARCH   = active_arch(CONFIG)
MUSL_ARCH  = ARCH["musl_arch"]
RUST_TRIPLE = ARCH["rust_triple"]
FREESTANDING_KEY = ARCH["freestanding_key"]
CLANG_TRIPLES = ARCH["clang_triples"]
CLANG_DEFAULT_TRIPLE = ARCH["freestanding_triple"]
ARCH_DIR   = ROOT / ARCH["arch_dir"]
MM_DIR     = ROOT / "mm"
FS_DIR     = ROOT / "fs"
USER_DIR   = ROOT / "user"
BOOT_DIR   = ARCH_DIR / "boot"
CLANG_FREESTANDING_FLAGS = ["-nostdlibinc"]
CC_CLANG_CANDIDATES = ("clang", "clang-22", "clang-21", "clang-20", "clang-19",
                       "clang-18", "clang-17")
LLVM_BIN_GLOBS = (
    "/usr/lib/llvm*/bin",
    "/usr/local/lib/llvm*/bin",
    "/usr/local/opt/llvm*/bin",
    "/opt/homebrew/opt/llvm*/bin",
)
FREE      = ["-ffreestanding", "-fno-builtin", "-fno-sanitize=all", "-O2"]
INCS      = ["-I", str(ROOT / "include")]
USER_INCS = ["-I", str(ROOT / "include" / "user" / "libc"),
             "-I", str(ROOT / "include" / "lib"), *INCS]
KERNEL_CFLAGS = FREE + INCS + [
    *ARCH["kernel_arch_cflags"],
    "-fstack-protector-strong",
    "-Wall", "-Wunused-function", "-Wunused-variable",
] + ["-include", str(AUTOCONF_H)]
UP_CFLAGS_64 = FREE + ["-fPIE", "-fno-stack-protector", *USER_INCS,
                       "-include", str(AUTOCONF_H)]
UP_LDFLAGS_64 = ["-s", "-m", ARCH["ld_emulation"],
                 "-e", "_start", "-static", "-pie", "--no-dynamic-linker",
                 "-z", "pack-relative-relocs"]
MUSL64_BASE = FREE + ["-fPIE"]
UEFI_CFLAGS = FREE + ["-fno-stack-protector", "-fshort-wchar", "-mno-red-zone",
                      "-nostdlibinc", "-Wall", "-Wextra", "-Wconversion",
                      "-Wno-unused-parameter", *INCS]
MUSL_PREFIX = BUILD_DIR / "musl"
MUSL_INC   = MUSL_PREFIX / "include"
MUSL_LIB   = MUSL_PREFIX / "lib"
PCRE2_SRC    = ROOT / "third_party" / "pcre2"
PCRE2_PREFIX = BUILD_DIR / "pcre2"
PCRE2_INC    = PCRE2_PREFIX / "include"
PCRE2_LIB    = PCRE2_PREFIX / "lib"
FISH_SRC       = ROOT / "third_party" / "fish"
FISH_CARGO_TOML = FISH_SRC / "Cargo.toml"
FISH_TARGET    = BUILD_DIR / "fish-target"
FISH_BIN       = FISH_TARGET / RUST_TRIPLE / "release" / "fish"
MUSL_DEMO_CFLAGS = MUSL64_BASE + ["-fstack-protector-strong", "-I", str(MUSL_INC)]
LC_CFLAGS = MUSL64_BASE + ["-fno-stack-protector", "-I", str(ROOT / "include"),
                           "-include", str(AUTOCONF_H)]

COMPILE_COMMANDS: list = []

# Re-export the public build-system API implemented across submodules so that
# `build.py`, `buildsys.cli`, and any other importer can pull names from the
# top-level package.
from .toolchain import (Ansi, Tools, CmdResult, run, Console, detect_tools,
                       color_enabled, _enable_vt_on_windows, show_failure_hint,
                       ensure_config, task_config, do_clean, restamp_build_env,
                       BUILD_ENV_STAMP)
from .tasks import (Task, BuildStats, KNOWN_VA_WARNINGS, task_assemble_bin,
                   task_assemble_elf, task_assemble_elf64, task_cc, task_link,
                   task_objcopy_binary, task_python, task_nasm_uefi, task_cc_uefi,
                   task_link_uefi, _musl_buildenv)
from .plan import make_plan, BuildPlan, execute_plan
from .image import _find_ovmf, do_run

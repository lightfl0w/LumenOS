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
from .toolchain import (Ansi, Tools, CmdResult, run, Console, detect_tools,
                       color_enabled)

@dataclass
class Task:
    name: str
    cmd:  List[str]
    cwd:  Optional[Path] = None
    out:  Optional[Path] = None
    deps: List[Task] = field(default_factory=list)
    description: str = ""
    group: str = ""
    env:  Optional[dict] = None
    optional: bool = False
    def is_stale(self) -> bool:
        if not self.out:
            return True
        if not self.out.exists():
            return True
        out_mtime = self.out.stat().st_mtime
        for d in self.deps:
            if d.is_stale():
                return True
            for path in d.dep_paths():
                if path.exists() and path.stat().st_mtime > out_mtime:
                    return True
        return False
    def dep_paths(self) -> Iterable[Path]:
        base = self.cwd or Path.cwd()
        for d in self.deps:
            if isinstance(d, Path):
                if d.exists():
                    yield d
                continue
        for tok in self.cmd:
            if tok.startswith("-"):
                continue
            p = Path(tok)
            if not p.is_absolute():
                p = base / p
            if p.exists() and p.is_file():
                yield p
        if self.out is not None and any(t == "-c" for t in self.cmd):
            dfile = self.out.with_suffix(".d")
            if dfile.exists():
                text = dfile.read_text(errors="replace")
                joined = text.replace("\\\r\n", " ").replace("\\\n", " ")
                for line in joined.splitlines():
                    if ":" not in line:
                        continue
                    for tok in line.split(":", 1)[1].split():
                        if not tok:
                            continue
                        p = Path(tok)
                        if not p.is_absolute():
                            p = base / p
                        if p.exists() and p.is_file():
                            yield p
KNOWN_VA_WARNINGS = (
    "-Wpointer-to-int-cast",
    "-Wint-to-pointer-cast",
    "-Wint-to-void-pointer-cast",
    "-Wvoid-pointer-to-int-cast",
)

@dataclass
class BuildStats:
    compiled: int = 0
    cache_hit: int = 0
    failed:   int = 0
    warnings: int = 0
    known_warnings: int = 0
    warn_list: list = field(default_factory=list)
    real_warn_list: list = field(default_factory=list)
    timings: dict = field(default_factory=dict)
def task_assemble_bin(name: str, src: Path, out: Path, tools: Tools) -> Task:
    return Task(
        name=name, cmd=[tools.nasm, "-f", "bin", str(src), "-o", str(out)],
        out=out, deps=[], description=str(src.relative_to(ROOT)),
        group="asm",
    )
def task_assemble_elf(name: str, src: Path, out: Path, tools: Tools) -> Task:
    return Task(
        name=name, cmd=[tools.nasm, "-f", "elf32", str(src), "-o", str(out)],
        out=out, deps=[], description=str(src.relative_to(ROOT)),
        group="asm",
    )
def task_assemble_elf64(name: str, src: Path, out: Path, tools: Tools) -> Task:
    return Task(
        name=name, cmd=[tools.nasm, "-f", ARCH["nasm_elf_format"],
                        str(src), "-o", str(out)],
        out=out, deps=[], description=str(src.relative_to(ROOT)),
        group="asm",
    )

def task_cc(name: str, src: Path, out: Path, tools: Tools, flags: List[str], target: str = FREESTANDING_KEY, cc: Optional[List[str]] = None, kind: Optional[str] = None) -> Task:
    cc = tools.cc if cc is None else cc
    kind = tools.kind if kind is None else kind
    cmd = [*cc]
    if kind == "clang":
        cmd.append(f"--target={CLANG_TRIPLES.get(target, CLANG_DEFAULT_TRIPLE)}")
        cmd += CLANG_FREESTANDING_FLAGS
    elif kind == "zig" and target:
        cmd.append(f"--target={target}")
        if any(f.startswith("-fstack-protector") for f in flags):
            cmd += ["-nostdinc", "-lc"]
    cmd += ["-c", str(src), *flags, "-MMD", "-MP", "-o", str(out)]
    COMPILE_COMMANDS.append({
        "directory": str(ROOT),
        "arguments": cmd,
        "file": str(src),
    })
    return Task(
        name=name,
        cmd=cmd,
        out=out, deps=[], description=str(src.relative_to(ROOT)),
        group="cc",
    )
def task_link(name: str, out: Path, tools: Tools,
              objs: Sequence[Path], script: Optional[Path] = None,
              flags: Sequence[str] = ()) -> Task:
    cmd = [tools.ld]
    if script:
        cmd += ["-T", str(script)]
    cmd += [*flags, "-o", str(out), *map(str, objs)]
    return Task(name=name, cmd=cmd, cwd=BUILD_DIR, out=out,
                deps=[], description=f"link → {out.name}", group="link")
def task_objcopy_binary(name: str, src_elf: Path, out: Path, tools: Tools,
                        symbol: str, elf_arch: str = "i386",
                        out_fmt: str = "elf32-i386", out_arch: str = "i386") -> Task:
    return Task(
        name=name,
        cmd=[tools.objcopy, "-I", "binary", "-O", out_fmt, "-B", out_arch,
             src_elf.name, out.name],
        cwd=BUILD_DIR, out=out, deps=[],
        description=f"objcopy {symbol}", group="objcopy",
    )
def task_python(name: str, script: Path, args: Sequence[str],
                out: Optional[Path] = None) -> Task:
    return Task(
        name=name,
        cmd=[sys.executable, str(script), *args],
        out=out, deps=[], description=str(script.name), group="python",
    )
def task_nasm_uefi(name: str, src: Path, out: Path, tools: Tools) -> Task:
    return Task(
        name=name,
        cmd=[tools.nasm, "-f", ARCH["uefi_nasm_format"], str(src), "-o", str(out)],
        out=out, deps=[], description=str(src.relative_to(ROOT)), group="uefi",
    )
def task_cc_uefi(name: str, src: Path, out: Path, tools: Tools,
                 flags: List[str]) -> Task:
    cmd = [*tools.cc, f"--target={ARCH['uefi_clang_target']}", *flags,
           "-c", str(src), "-o", str(out)]
    COMPILE_COMMANDS.append({
        "directory": str(ROOT),
        "arguments": cmd,
        "file": str(src),
    })
    return Task(
        name=name, cmd=cmd, out=out, deps=[],
        description=str(src.relative_to(ROOT)), group="uefi",
    )
def task_link_uefi(name: str, out: Path, tools: Tools,
                   objs: Sequence[Path]) -> Task:
    cmd = [tools.lld_link, "/subsystem:efi_application", "/entry:efi_entry",
           "/nodefaultlib", f"/out:{out}", *map(str, objs)]
    return Task(name=name, cmd=cmd, cwd=BUILD_DIR, out=out, deps=[],
                description=f"link → {out.name} (UEFI PE)", group="uefi")
def _musl_buildenv(tools: Tools) -> Optional[dict]:
    if os.name == "nt":
        return None
    sh = shutil.which("sh") or shutil.which("bash")
    make = shutil.which("make") or "/usr/bin/make"
    if not sh or not os.path.exists(make):
        return None
    cc = list(tools.cc)
    if len(cc) > 1:
        wrapper = BUILD_DIR / "musl-cc"
        wrapper.parent.mkdir(parents=True, exist_ok=True)
        body = "#!/bin/sh\nexec " + " ".join(shlex.quote(c) for c in cc) + ' "$@"\n'
        wrapper.write_text(body, encoding="utf-8")
        try:
            os.chmod(wrapper, 0o755)
        except OSError:
            pass
        cc = [str(wrapper)]
    return {"sh": sh, "make": make, "cc": cc}
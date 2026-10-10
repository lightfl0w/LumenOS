from __future__ import annotations
import argparse
import subprocess
import sys
from pathlib import Path
from typing import Sequence

from . import (ROOT, BUILD_DIR, CONFIG, ARCH, SCRIPTS, detect_tools,
              ensure_config, make_plan, execute_plan, do_run, do_clean,
              restamp_build_env, show_failure_hint, BuildStats,
              _enable_vt_on_windows, color_enabled, CLANG_DEFAULT_TRIPLE,
              FREESTANDING_KEY)
from .toolchain import Console, Ansi
from .plan import BuildPlan

def run_layout_check(console: "Console") -> int:
    """Directory hygiene guardrail: no empty dirs, stray artifacts, orphan
    headers, ambiguous header basenames, or headers sitting outside the arch
    shim convention. Cheap to run and catches the kind of rot that makes a
    tree hard to navigate.
    """
    script = SCRIPTS / "check_layout.py"
    if not script.exists():
        return 0
    proc = subprocess.run([sys.executable, str(script)],
                          capture_output=True, text=True)
    out = (proc.stdout or "") + (proc.stderr or "")
    lines = [l.strip() for l in out.splitlines() if l.strip()]
    if proc.returncode != 0 or (lines and "non-fatal" not in lines[-1]):
        for line in lines:
            console.writeln("  " + line)
        return proc.returncode
    return 0


def run_style_check(console: "Console") -> int:
    """Readability backlog: over-deep nesting, huge functions, wide lines,
    wrapped conditions. Informational by design -- it prints the worst
    offenders so the fix order is explicit instead of a vague sense that the
    code is hard to read. Only --strict makes it fatal."""
    script = SCRIPTS / "check_style.py"
    if not script.exists():
        return 0
    proc = subprocess.run([sys.executable, str(script), "--top", "5"],
                          capture_output=True, text=True)
    out = (proc.stdout or "") + (proc.stderr or "")
    lines = [l.rstrip() for l in out.splitlines() if l.strip()]
    for line in lines:
        console.writeln("  " + line)
    return proc.returncode


def run_layer_check(console: "Console") -> int:
    """Layering guardrail (战役 0): fail the build on any upward dependency.

    Runs check_layers.py in --strict mode so both ERROR (lib reaching up) and
    WARN (arch reaching into kernel/drivers) break the build unless the pair is
    an explicit, commented entry in ALLOW_LIST.
    """
    script = SCRIPTS / "check_layers.py"
    if not script.exists():
        return 0
    proc = subprocess.run([sys.executable, str(script), "--strict"],
                          capture_output=True, text=True)
    out = (proc.stdout or "") + (proc.stderr or "")
    for line in out.splitlines():
        if line.strip():
            console.writeln("  " + line.strip())
    if proc.returncode != 0:
        console.fail("layering guardrail failed: see [layers] output above")
        return proc.returncode
    return 0


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="build.py",
        description="Cross-platform LumenOS build system",
    )
    parser.add_argument("target", nargs="?", default="floppy",
                        choices=["all", "floppy", "run", "clean", "smoke", "lint"],
                        help="build target (default: floppy)")
    parser.add_argument("--no-color", action="store_true",
                        help="disable ANSI colour output")
    parser.add_argument("--jobs", "-j", type=int, default=1,
                        help="parallel compile jobs (default: 1)")
    parser.add_argument("--version", action="version", version="lumen-build 1.0")
    parser.add_argument("--sm", type=int,
                        default=4 if CONFIG.get("CONFIG_SMP") == "y" else 1,
                        metavar="N",
                        help="SMP CPU 数(qemu -smp N, 多核启动验证; "
                             "默认取自 CONFIG_SMP)")
    parser.add_argument("--gdb", action="store_true",
                        help="QEMU GDB stub(-s -S, 等待 gdb 连接 kernel.elf)")
    parser.add_argument("--no-net", action="store_true",
                        help="不挂虚拟网卡/后端(默认挂 e1000 + user 后端)")
    parser.add_argument("--boot-floppy", action="store_true",
                        help="以 floppy.img 作为引导软盘(-fda); 默认用 test_hd.img(-hda)")
    parser.add_argument("--bios", action="store_true",
                        help="强制走传统 BIOS 链(test_hd.img + boot.bin/loader.bin); "
                             "默认在 CONFIG_UEFI=y 且 build/esp.img 存在时走 UEFI")
    parser.add_argument("--test", action="store_true",
                        help="同时编译 tests/ 下的探针（默认不编译，避免拖慢构建）")
    parser.add_argument("--kvm", action="store_true",
                        help="启用 KVM 硬件加速(-enable-kvm -cpu host), 用于测试 "
                             "需在 ring0 执行的 MWAIT 等真实 CPU 特性(需 Linux/WSL2)")
    args = parser.parse_args(argv)
    _enable_vt_on_windows()
    console = Console(use_color=color_enabled(args.no_color))
    console.banner("v1.0  ·  Windows / Linux / macOS")
    if args.target == "clean":
        do_clean(console)
        return 0
    if args.target == "lint":
        return run_layer_check(console) or run_layout_check(console)
    ensure_config()
    try:
        tools = detect_tools()
    except FileNotFoundError as exc:
        show_failure_hint(console, [str(exc)])
        return 2
    console.info(f"arch    = {ARCH['key']}  (CONFIG_ARCH)")
    console.info(f"cc      = {' '.join(tools.cc)}  ({tools.kind})")
    console.info(f"target  = {CLANG_DEFAULT_TRIPLE if tools.kind == 'clang' else FREESTANDING_KEY}")
    console.info(f"ld      = {tools.ld}")
    console.info(f"nasm    = {tools.nasm}")
    console.info(f"objcopy = {tools.objcopy}")
    console.writeln()
    restamp_build_env(tools, console)
    plan = make_plan(tools,
                     with_musl_lib=(CONFIG.get("CONFIG_MUSL_LIB") == "y"),
                     with_tests=args.test)
    try:
        stats = execute_plan(plan, tools, console, jobs=args.jobs)
    except SystemExit as exc:
        return int(exc.code) if exc.code is not None else 1
    floppy = BUILD_DIR / "floppy.img"
    kernel = BUILD_DIR / "kernel.bin"
    elf    = BUILD_DIR / "kernel.elf"
    def human_size(p: Path) -> str:
        try:
            n = p.stat().st_size
        except FileNotFoundError:
            return "—"
        for unit in ("B", "KB", "MB", "GB"):
            if n < 1024 or unit == "GB":
                return f"{n:.1f} {unit}" if unit != "B" else f"{n} {unit}"
            n /= 1024
        return f"{n:.1f} GB"
    total_dur = stats.timings.get("__total__", (0.0, ""))[0]
    def fmt_dur(seconds: float) -> str:
        if seconds < 0.05:
            return "(<0.1s)"
        if seconds < 10:
            return f"({seconds:.2f}s)"
        return f"({seconds/60:.1f}min)"
    uefi_esp = BUILD_DIR / "esp.img"
    artefacts = "floppy.img · kernel.bin · kernel.elf"
    if uefi_esp.exists():
        artefacts += " · BOOTX64.EFI · esp.img"
    rows = [
        ("artefacts",   artefacts,                                Ansi.BR_WHT),
        ("floppy size", human_size(floppy),                       Ansi.BR_CYN),
        ("kernel size", human_size(kernel),                       Ansi.BR_CYN),
        ("kernel.elf",  human_size(elf),                          Ansi.BR_CYN),
    ]
    if uefi_esp.exists():
        rows.append(("esp size", human_size(uefi_esp), Ansi.BR_CYN))
    rows += [
        ("compiled",    str(stats.compiled),                      Ansi.BR_GRN),
        ("cached",      str(stats.cache_hit),                     Ansi.BR_YEL),
        ("failed",      str(stats.failed),                        Ansi.BR_RED if stats.failed else Ansi.GRAY),
        ("warnings",    str(stats.warnings),                      Ansi.BR_YEL if stats.warnings else Ansi.GRAY),
        ("known noise", str(stats.known_warnings),                Ansi.GRAY),
        ("total",       fmt_dur(total_dur),                       Ansi.BR_WHT),
    ]
    console.summary(rows)
    if stats.warn_list:
        wf = BUILD_DIR / "warnings.txt"
        wf.write_text("\n".join(stats.warn_list) + "\n", encoding="utf-8")
        console.writeln(f"  {console._c(Ansi.DIM)}{console._c(Ansi.GRAY)}"
                        f"  警告明细: {wf.relative_to(ROOT)}"
                        f"{console._c(Ansi.RESET)}")
    if stats.real_warn_list:
        for entry in stats.real_warn_list[:10]:
            console.writeln(f"  {console._c(Ansi.BR_YEL)}⚠ {entry}"
                            f"{console._c(Ansi.RESET)}")
    console.writeln(f"  {console._c(Ansi.GREEN)}{console._c(Ansi.BOLD)}"
                    f"✔  build complete{console._c(Ansi.RESET)}")
    console.writeln()
    if args.target != "lint":
        rc = run_layout_check(console) or run_layer_check(console)
        if rc != 0:
            return rc
        run_style_check(console)
    if args.target == "run":
        esp_img = BUILD_DIR / "esp.img"
        use_uefi = (
            not args.bios
            and not args.boot_floppy
            and CONFIG.get("CONFIG_UEFI") == "y"
            and esp_img.exists()
        )
        mode = "UEFI (OVMF + esp.img)" if use_uefi else "BIOS (boot.bin/loader.bin)"
        console.info(f"boot mode: {mode}")
        do_run(console, stats, args.sm, args.gdb, args.no_net, args.boot_floppy,
               args.kvm, use_uefi)
    elif args.target == "smoke":
        rc = subprocess.run([sys.executable, str(SCRIPTS / "smoke_qemu.py")]).returncode
        if rc != 0:
            console.fail("smoke test failed: kernel did not boot to shell")
            return rc
    return 0
if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
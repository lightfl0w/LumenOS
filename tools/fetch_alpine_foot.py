#!/usr/bin/env python3

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEST = ROOT / "third_party" / "alpine-foot"
MIRROR = "https://mirrors.tuna.tsinghua.edu.cn/alpine"
BRANCH = "v3.22"
ARCH = "x86_64"
REPOS = [f"{MIRROR}/{BRANCH}/main", f"{MIRROR}/{BRANCH}/community"]
APK_TOOLS = "apk-tools-static-2.14.13-r0.apk"
KEYS_PKG = "alpine-keys-2.5-r0.apk"
PACKAGES = [
    "foot",
    "foot-extra-terminfo",
    "ncurses-terminfo",
    "fontconfig",
    "ttf-dejavu",
]
MARKER = DEST / ".fetched"


def download(url: str, dst: Path) -> None:
    req = urllib.request.Request(url, headers={"User-Agent": "lumen-fetch/1"})
    with urllib.request.urlopen(req, timeout=120) as resp:
        dst.write_bytes(resp.read())


def extract_member(apk: Path, member: str, dst: Path) -> None:
    with tarfile.open(apk, "r:*") as tf:
        src = tf.extractfile(member)
        if src is None:
            raise SystemExit(f"{apk.name}: missing {member}")
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(src.read())


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true",
                    help="reinstall even if the tree already looks complete")
    args = ap.parse_args()

    if MARKER.exists() and not args.force:
        print(f"[alpine-foot] already fetched: {DEST.relative_to(ROOT)}")
        print("[alpine-foot] use --force to reinstall")
        return 0

    DEST.mkdir(parents=True, exist_ok=True)
    tools_dir = DEST / "tools"
    keys_dir = DEST / "keys"
    tools_dir.mkdir(exist_ok=True)
    keys_dir.mkdir(exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="lumen-alpine-") as td:
        tmp = Path(td)
        dl = tmp / "dl"
        dl.mkdir()

        print(f"[alpine-foot] downloading {APK_TOOLS}")
        apk_tools = dl / APK_TOOLS
        download(f"{MIRROR}/{BRANCH}/main/{ARCH}/{APK_TOOLS}", apk_tools)
        apk_static = tools_dir / "apk.static"
        extract_member(apk_tools, "sbin/apk.static", apk_static)
        apk_static.chmod(0o755)

        print(f"[alpine-foot] downloading {KEYS_PKG}")
        keys_pkg = dl / KEYS_PKG
        download(f"{MIRROR}/{BRANCH}/main/{ARCH}/{KEYS_PKG}", keys_pkg)
        with tarfile.open(keys_pkg, "r:*") as tf:
            for name in tf.getnames():
                if name.startswith("etc/apk/keys/") and name.endswith(".pub"):
                    out = keys_dir / Path(name).name
                    extract_member(keys_pkg, name, out)

        cache = tmp / "cache"
        cache.mkdir()
        cmd = [
            str(apk_static),
            "--root", str(DEST),
            "--arch", ARCH,
            "--keys-dir", str(keys_dir),
            "--cache-dir", str(cache),
            "--no-progress",
            "--no-scripts",
            "--allow-untrusted",
            "--usermode",
            "--initdb",
        ]
        for repo in REPOS:
            cmd += ["--repository", repo]
        cmd += ["add", *PACKAGES]

        print("[alpine-foot] installing:", " ".join(PACKAGES))
        proc = subprocess.run(cmd, text=True)
        if proc.returncode != 0:
            print("[alpine-foot] apk failed", file=sys.stderr)
            return proc.returncode

    foot = DEST / "usr" / "bin" / "foot"
    if not foot.exists():
        print(f"[alpine-foot] missing {foot}", file=sys.stderr)
        return 1

    MARKER.write_text("\n".join(PACKAGES) + "\n", encoding="utf-8")
    libs = sorted((DEST / "usr" / "lib").glob("*.so*")) \
        if (DEST / "usr" / "lib").is_dir() else []
    print(f"[alpine-foot] foot: {foot.relative_to(ROOT)}")
    print(f"[alpine-foot] shared libs: {len(libs)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

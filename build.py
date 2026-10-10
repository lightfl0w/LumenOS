#!/usr/bin/env python3
"""LumenOS build system entry point.

The implementation lives in the `buildsys` package; this file is a thin
launcher so `python3 build.py` keeps working from the repository root.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

from buildsys.cli import main

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

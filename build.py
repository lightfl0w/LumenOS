#!/usr/bin/env python3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

from buildsys.cli import main

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

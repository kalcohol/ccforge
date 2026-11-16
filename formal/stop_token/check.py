#!/usr/bin/env python3
"""Repository entry point; Python 3.9+, standard library only."""

import sys

sys.dont_write_bytecode = True
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "formal"))
from engine.runner import main

if __name__ == "__main__":
    raise SystemExit(main(ROOT, "stop_token", "backport/cpp26/execution/stop_token.hpp"))

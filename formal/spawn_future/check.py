#!/usr/bin/env python3
"""Repository entry point; Python 3.9+, standard library only."""
import sys
sys.dont_write_bytecode = True
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'formal'))
from engine.runner import main

if __name__ == '__main__':
    from spawn_future.self_tests import run
    run()
    raise SystemExit(main(ROOT, 'spawn_future', 'backport/cpp26/execution/spawn_future.hpp'))

#!/usr/bin/env python3
"""Repository entry point; standard library, Python 3.9+."""
import sys
sys.dont_write_bytecode=True
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'formal'))
from engine.runner import main
if __name__=='__main__':
    from run_loop.self_tests import run
    run()
    raise SystemExit(main(ROOT,'run_loop','backport/cpp26/execution/run_loop.hpp'))

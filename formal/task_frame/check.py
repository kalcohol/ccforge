#!/usr/bin/env python3
"""Bounded task frame-ownership checker, Python 3.9+."""
import sys
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "formal"))
from engine.runner import main
from task_frame.mutants import validate_sensitivity

if __name__ == "__main__":
    # Source and step inspection stay exploration-free. Tier runs validate the
    # named failures before the shared runner can write expected data or a lock.
    if not {"--check-source", "--steps"}.intersection(sys.argv[1:]):
        try:
            validate_sensitivity()
        except ValueError as exc:
            print(exc, file=sys.stderr)
            raise SystemExit(1)
    raise SystemExit(main(ROOT, "task_frame",
                          "backport/cpp26/execution/detail/task_terminal.hpp"))

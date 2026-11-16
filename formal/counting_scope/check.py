#!/usr/bin/env python3
"""Repository entry point; Python 3.9+, standard library only."""

import sys

sys.dont_write_bytecode = True
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "formal"))
from engine.runner import main

if __name__ == "__main__":
    from counting_scope.model import check_duplicates
    from counting_scope.self_tests import run

    run()

    try:
        check_duplicates((ROOT / "backport/cpp26/execution/counting_scope.hpp").read_text())
    except ValueError as exc:
        raise SystemExit(str(exc))
    raise SystemExit(main(ROOT, "counting_scope", "backport/cpp26/execution/counting_scope.hpp"))

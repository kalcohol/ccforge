#!/usr/bin/env python3
"""Bounded stop-adapter checker, Python 3.9+."""
import sys
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "formal"))
from engine.runner import main
from proxy_stop_adapter.mutants import validate_records, validate_selection, validate_sensitivity

if __name__ == "__main__":
    # Source and step inspection stay exploration-free. Tier runs validate the
    # named failures before the shared runner can write expected data or a lock.
    if not {"--check-source", "--steps"}.intersection(sys.argv[1:]):
        try:
            # The exhaustive selection check runs first, before any tier.
            validate_selection()
            validate_sensitivity()
            # A regenerating run rewrites the records, so it is not held to
            # the old names. Every other run is.
            if "--update-expected" not in sys.argv[1:]:
                validate_records()
        except ValueError as exc:
            print(exc, file=sys.stderr)
            raise SystemExit(1)
    raise SystemExit(main(ROOT, "proxy_stop_adapter",
                          "backport/cpp26/execution/detail/proxy_stop_adapter.hpp"))

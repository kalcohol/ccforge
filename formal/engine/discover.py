#!/usr/bin/env python3
"""Fail-closed discovery shared by repository audit and verification floor."""

import sys

sys.dont_write_bytecode = True
from pathlib import Path
import tempfile


def discover(root):
    checkers = sorted(root.glob("*/check.py"))
    if not checkers:
        raise ValueError("formal directory contains no model check.py")
    for lock in root.glob("*/source.lock"):
        if not (lock.parent / "check.py").is_file():
            raise ValueError(str(lock.parent) + ": source.lock without check.py")
    return checkers


def self_test():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for stage in range(3):
            try:
                found = discover(root)
            except ValueError:
                assert stage != 2
            else:
                assert stage == 2 and len(found) == 1
            if stage == 0:
                (root / "model").mkdir()
                (root / "model/source.lock").touch()
            elif stage == 1:
                (root / "model/check.py").touch()
        (root / "orphan").mkdir()
        (root / "orphan/source.lock").touch()
        try:
            discover(root)
        except ValueError as exc:
            assert "source.lock without check.py" in str(exc)
        else:
            raise AssertionError("orphan source lock accepted")


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        self_test()
    else:
        try:
            print("\n".join(str(p) for p in discover(Path("formal"))))
        except ValueError as exc:
            raise SystemExit(str(exc))

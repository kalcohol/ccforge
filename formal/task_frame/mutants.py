"""Manually declared negative outcomes, independent of generated records."""
from functools import lru_cache
from pathlib import Path
import re

from engine.search import explore
from . import model, tiers

CATALOG = {
    "omit_exchange": {"outcome": "killed",
                      "reason": "the terminal destroys the frame but the operation still owns it"},
    "deliver_before_destroy": {"outcome": "killed",
                               "reason": "the receiver is entered while the frame is alive"},
    "read_after_callback": {"outcome": "killed",
                            "reason": "the terminal routine reads the operation after the receiver returned"},
}


@lru_cache(maxsize=1)
def failure_manifest():
    expected = {}
    for row in Path(__file__).with_name("sensitivity.txt").read_text().splitlines():
        fields = row.partition("#")[0].split()
        if not fields:
            continue
        if len(fields) != 2 or not re.fullmatch(r"TF[1-7]", fields[1]) or fields[0] in expected:
            raise ValueError("invalid sensitivity manifest row: " + row)
        expected[fields[0]] = fields[1]
    return expected


def declared(task):
    return task["name"] in failure_manifest()


def validate_sensitivity():
    expected = failure_manifest()
    for tier in ("quick", "full"):
        names = [task["name"] for task in tiers.select(tier) if task["mut"] or task["control"]]
        if sorted(names) != sorted(expected):
            raise ValueError(tier + ": negative cases do not match the sensitivity manifest")
    for task in tiers.sensitivity():
        name = task["name"]
        result = explore(model, tiers.configuration(task), stop_on_first=task["stop"],
                         max_states=task["max"])
        actual = {violation["category"].partition(":")[0] for violation in result["violations"]}
        if not result["complete"] or actual != {expected[name]}:
            raise ValueError("%s: expected only %s, got %s"
                             % (name, expected[name], sorted(actual)))

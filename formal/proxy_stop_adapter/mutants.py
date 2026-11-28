"""Manually declared negative outcomes, independent of generated records."""
from functools import lru_cache
from pathlib import Path
import re

from engine.search import explore
from . import model, tiers

CATALOG = {
    "lose_request": {"outcome": "killed",
                     "reason": "the adapter forwarder never calls request_stop"},
    "duplicate_registration": {"outcome": "killed",
                               "reason": "a competing initializer also registers"},
}


@lru_cache(maxsize=1)
def failure_manifest():
    expected = {}
    for row in Path(__file__).with_name("sensitivity.txt").read_text().splitlines():
        fields = row.partition("#")[0].split()
        if not fields:
            continue
        if len(fields) != 2 or not re.fullmatch(r"PA(?:[1-9]|10)", fields[1]) or fields[0] in expected:
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


def validate_records():
    """The expected file names exactly the configurations of the full tier."""
    path = Path(__file__).with_name("expected") / "results.tsv"
    recorded = sorted(row.split("\t")[0] for row in path.read_text().splitlines()[1:] if row)
    selected = sorted(task["name"] for task in tiers.select("full"))
    if recorded != selected:
        extra = sorted(set(recorded) - set(selected))
        missing = sorted(set(selected) - set(recorded))
        raise ValueError("expected records do not match the full tier: obsolete %s, missing %s"
                         % (extra, missing))


def validate_selection():
    """Compare the model's search with the independent oracle on every candidate combination.

    One statically classified query runs alone, for each combination of the
    three candidates (absent, impossible, eligible, requested, and a default
    token known to be impossible) and of the two shortcuts. The all-absent
    combination is the static rejection X3 and has no execution.
    """
    from types import SimpleNamespace
    outcomes = (None, "impossible", "eligible", "requested")
    checked = 0
    for same_type in (False, True):
        for inplace in (False, True):
            for tokdef in outcomes + ("sure",):
                for nostop in outcomes:
                    for default in outcomes:
                        cfg = SimpleNamespace(
                            name="selection", queries=1, mut="", control="", possible=False, static=True,
                            same_type=same_type, tokdef="inplace" if inplace else tokdef, nostop=nostop,
                            default=default, loss=False, before=False, requests=0, timing="arbitrary",
                            completion="ordinary", required=set())
                        answer, made, survivors = model.oracle(cfg)
                        label = "%s/%s/%s sample=%s inplace=%s" % (tokdef, nostop, default, same_type, inplace)
                        if answer == "X3":
                            continue
                        model.configure(cfg)
                        w = model.initial()
                        for _ in range(64):
                            if w.aborted or w.phase == model.READY:
                                break
                            steps = [new for (step, actor), new in model.successors(w) if actor == 0]
                            if len(steps) != 1:
                                raise ValueError("selection %s: the search is not deterministic" % label)
                            w = steps[0]
                            if model.state_violations(w):
                                raise ValueError("selection %s: %s" % (label, model.state_violations(w)))
                        if w.aborted != (answer == "T3"):
                            raise ValueError("selection %s: termination disagrees with the oracle" % label)
                        if not w.aborted and (w.token[0] != answer or w.made != made or
                                              model.live_sources(w) != survivors or
                                              w.slots != model.expected_slots(cfg)):
                            raise ValueError("selection %s: got %s, made %d, kept %d; the oracle says %s, %d, %d"
                                             % (label, w.token[0], w.made, model.live_sources(w), answer, made,
                                                survivors))
                        if w.aborted and (w.made != made or w.factory != made or w.commits):
                            raise ValueError("selection %s: ended after %d constructions and %d factory calls; "
                                             "the oracle says %d" % (label, w.made, w.factory, made))
                        if w.aborted and model.terminal_violations(w):
                            raise ValueError("selection %s: %s" % (label, model.terminal_violations(w)))
                        checked += 1
    return checked

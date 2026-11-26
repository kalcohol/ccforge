"""Quick representatives and the complete predeclared finite scenario suite."""
from types import SimpleNamespace
from .scenarios import COVERAGE, SCENARIOS


def case(scenario, queries=2, mut="", control=""):
    name = ".".join((mut or control or "base", scenario, str(queries)))
    return dict(name=name, model="sc", scenario=scenario, queries=queries, mut=mut,
                control=control, stop=bool(mut or control), max=4000)


def sensitivity():
    return [
        case("already_requested", mut="lose_request"),
        case("request_race", mut="duplicate_registration"),
        case("same_thread_cleanup", control="no_source_pin"),
        case("request_race", control="terminal_before_cleanup"),
        case("request_race", control="publish_before_registration"),
        case("already_requested", control="publish_before_immediate_forward"),
    ]


def select(tier):
    base = [case(name, 0 if name == "unused" else 2) for name in SCENARIOS]
    if tier == "quick":
        return [t for t in base if t["scenario"] in
                ("impossible", "request_race", "cross_thread_cleanup", "same_thread_cleanup")] + sensitivity()
    return base + [case(name, 1) for name in SCENARIOS if name != "unused"] + sensitivity()


def identity(task):
    return {k: task[k] for k in ("model", "scenario", "queries", "mut", "control")}


def configuration(task):
    name = task["scenario"]
    fields = dict(possible=True, before=False, requests=1, timing="arbitrary",
                  completion="ordinary")
    fields.update(SCENARIOS[name])
    fields.update(identity(task), name=task["name"])
    required = set(COVERAGE[name])
    if fields["queries"] < 2:
        required.discard("query.wait")
        required.discard("query.park")
    return SimpleNamespace(**fields, required=required)

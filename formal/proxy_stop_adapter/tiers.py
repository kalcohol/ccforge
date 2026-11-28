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
        case("held_nostop_impossible_default", control="engaged_where_representable"),
        case("requested_default", control="fabricated_request"),
        case("held_nostop_requested_default", control="source_released_first"),
        case("held_nostop_impossible_default", control="wrong_survivor"),
        case("requested_default", control="early_termination"),
        case("request_race", control="reader_issues"),
        case("request_race", control="unserialized_copy"),
        case("request_race", control="sequential_reconstruction"),
        case("stateless_default", control="bypassed_fresh_check"),
    ]


QUICK = ("impossible", "impossible_sampled", "static_sampled", "impossible_token_default",
         "impossible_nostop", "tag_only_engaged", "impossible_engaged", "held_nostop_impossible_default",
         "held_nostop_requested_default", "requested_nostop_usable_default", "requested_default",
         "stateless_default", "request_race", "cross_thread_cleanup", "same_thread_cleanup",
         "loss_during_initialization")


def select(tier):
    base = [case(name, 0 if name == "unused" else 2) for name in SCENARIOS]
    if tier == "quick":
        return [t for t in base if t["scenario"] in QUICK] + sensitivity()
    return base + [case(name, 1) for name in SCENARIOS if name != "unused"] + sensitivity()


def identity(task):
    return {k: task[k] for k in ("model", "scenario", "queries", "mut", "control")}


def configuration(task):
    name = task["scenario"]
    fields = dict(possible=True, tokdef="inplace", nostop=None, default="eligible", same_type=False,
                  static=False, loss=False, before=False, requests=1, timing="arbitrary",
                  completion="ordinary")
    fields.update(SCENARIOS[name])
    fields.update(identity(task), name=task["name"])
    required = set(COVERAGE[name])
    if fields["queries"] < 2:
        required.discard("query.wait")
        required.discard("query.park")
        required.discard("copy.wait")
    return SimpleNamespace(**fields, required=required)

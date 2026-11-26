"""Quick representatives and the complete predeclared finite scenario suite."""
from types import SimpleNamespace
from .scenarios import COVERAGE, DESTROYERS, SCENARIOS, SINGLE


def case(scenario, destroy="receiver", mut="", control=""):
    if scenario in SINGLE:
        destroy = "none"
    name = ".".join((mut or control or "base", scenario, destroy))
    return dict(name=name, model="sc", scenario=scenario, destroy=destroy, mut=mut,
                control=control, stop=bool(mut or control), max=20000)


def sensitivity():
    return [
        case("inline_result", "owner", mut="omit_exchange"),
        case("worker_stopped", "receiver", mut="omit_exchange"),
        case("inline_result", "owner", mut="deliver_before_destroy"),
        case("child_inline_stopped", "receiver", mut="deliver_before_destroy"),
        case("inline_result", "receiver", mut="read_after_callback"),
        case("worker_stopped", "owner", mut="read_after_callback"),
        case("worker_result", "owner", mut="read_after_callback"),
        case("moved", control="copying_move"),
        case("failed_own_env", control="raw_handle_state"),
        case("inline_result", "owner", control="drop_on_connect"),
        case("unstarted", control="drop_on_connect"),
        case("inline_result", "receiver", control="touch_after_resume"),
        case("worker_stopped", "receiver", control="touch_after_resume"),
        case("inline_error", "owner", control="convert_before_destroy"),
        case("inline_error", "owner", control="stage_after_destroy"),
    ]


def base():
    return [case(name, destroy) for name in SCENARIOS
            for destroy in (("none",) if name in SINGLE else DESTROYERS)]


def select(tier):
    if tier == "quick":
        chosen = ("moved", "failed_own_env", "unstarted", "inline_result", "inline_error",
                  "child_inline_stopped", "worker_stopped")
        return [t for t in base() if t["scenario"] in chosen] + sensitivity()
    return base() + sensitivity()


def identity(task):
    return {k: task[k] for k in ("model", "scenario", "destroy", "mut", "control")}


def configuration(task):
    name = task["scenario"]
    fields = dict(move=False, fail_at=0, where="", kind="")
    fields.update(SCENARIOS[name])
    fields.update(identity(task), name=task["name"])
    # The scenario's own kind of ownership path, not its catalog name.
    fields["scenario"] = SCENARIOS[name]["scenario"]
    required = set(COVERAGE[name])
    if task["destroy"] == "receiver":
        required.add("terminal.receiver_destroys")
    elif task["destroy"] == "owner":
        required.add("operation.empty_destroy")
    return SimpleNamespace(**fields, required=required)

"""Ownership paths of one task: unconnected, failed connect, unstarted and started."""
SCENARIOS = {
    # name: scenario fields. Started scenarios add where the terminal runs and its kind.
    "unconnected": dict(scenario="unconnected", move=False),
    "moved": dict(scenario="unconnected", move=True),
    "failed_receiver": dict(scenario="failed_connect", fail_at=1),
    "failed_own_env": dict(scenario="failed_connect", fail_at=2),
    "failed_environment": dict(scenario="failed_connect", fail_at=3),
    "unstarted": dict(scenario="unstarted"),
    "inline_result": dict(scenario="started", where="inline", kind="result"),
    "inline_exception": dict(scenario="started", where="inline", kind="exception"),
    "inline_error": dict(scenario="started", where="inline", kind="error"),
    "inline_staging_throw": dict(scenario="started", where="inline", kind="staging_throw"),
    "inline_conversion_routed": dict(scenario="started", where="inline", kind="conversion_routed"),
    "inline_conversion_terminates": dict(scenario="started", where="inline", kind="conversion_terminates"),
    "child_inline_stopped": dict(scenario="started", where="child_inline", kind="stopped"),
    "child_inline_result": dict(scenario="started", where="child_inline", kind="result"),
    "worker_stopped": dict(scenario="started", where="worker", kind="stopped"),
    "worker_result": dict(scenario="started", where="worker", kind="result"),
}

# Scenarios whose outcome depends on who destroys the operation.
DESTROYERS = ("receiver", "owner")
SINGLE = ("unconnected", "moved", "failed_receiver", "failed_own_env", "failed_environment",
          "unstarted", "inline_conversion_terminates")

COVERAGE = {
    "unconnected": {"task.owned_destroy"},
    "moved": {"task.move", "task.owned_destroy"},
    "failed_receiver": {"connect.unwind", "task.empty_destroy"},
    "failed_own_env": {"connect.unwind", "task.empty_destroy"},
    "failed_environment": {"connect.unwind", "task.empty_destroy"},
    "unstarted": {"operation.owned_destroy", "task.empty_destroy"},
    "inline_result": {"start.after_terminal"},
    "inline_exception": {"start.after_terminal"},
    "inline_error": {"start.after_terminal"},
    "inline_staging_throw": {"error.staging_throw"},
    "inline_conversion_routed": {"error.conversion_routed"},
    "inline_conversion_terminates": {"error.conversion_terminates"},
    "child_inline_stopped": {"return.after_terminal", "start.after_terminal"},
    "child_inline_result": {"return.before_terminal", "start.after_terminal"},
    "worker_stopped": {"worker.before_start_returned", "worker.after_start_returned",
                       "start.before_terminal", "start.after_terminal",
                       "return.before_terminal", "return.after_terminal"},
    "worker_result": {"worker.before_start_returned", "worker.after_start_returned",
                      "start.before_terminal", "start.after_terminal",
                      "return.before_terminal", "return.after_terminal"},
}

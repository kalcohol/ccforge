"""The backend joins its own queries before calling a terminal operation."""
SCENARIOS = {
    "unused": dict(queries=0),
    "impossible": dict(possible=False),
    "already_requested": dict(before=True),
    "request_race": dict(),
    "cross_thread_cleanup": dict(timing="after_queries", completion="cross"),
    "same_thread_cleanup": dict(timing="after_queries", completion="self"),
    "repeated_request": dict(requests=2),
}

COVERAGE = {
    "unused": {"terminal.entry", "request.unregistered"},
    "impossible": {"query.wait", "query.park", "query.disengaged", "terminal.entry"},
    "already_requested": {"registration.immediate", "request.delivered", "query.translated"},
    "request_race": {"query.wait", "registration.immediate", "request.in_registration",
                     "request.registered", "request.unregistered", "terminal.entry"},
    "cross_thread_cleanup": {"query.wait", "request.registered", "request.delivered",
                             "terminal.wait", "request.resume", "terminal.unregister"},
    "same_thread_cleanup": {"terminal.self_remove", "request.resume_after_destroy"},
    "repeated_request": {"request.repeat", "registration.immediate", "request.registered"},
}

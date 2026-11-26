"""Finite SC interleavings of lazy proxy stop adaptation and terminal cleanup."""
from dataclasses import dataclass, replace
from types import SimpleNamespace

STEPS = {
    k: SimpleNamespace(lines=reference, desc=description)
    for k, (reference, description) in {
        "claim": ("__proxy_stop_adapter::__query | __phase_.compare_exchange_strong",
                  "single initializer by SC CAS"),
        "wait": ("__proxy_stop_adapter::__query | __phase_.wait(__initializing);",
                 "busy observations wait for publication"),
        "source": ("__proxy_stop_adapter::__query | __source_->emplace();",
                   "construct an in-place source only for a possible token"),
        "register": ("__proxy_stop_adapter::__query | __callback_.emplace(",
                     "upstream registration linearization"),
        "publish": ("__proxy_stop_adapter::__query | __phase_.store(__ready);",
                    "publish completed initialization"),
        "notify": ("__proxy_stop_adapter::__query | __phase_.notify_all();",
                   "wake observers after ready publication"),
        "read": ("__proxy_stop_adapter::__query | return *__source_ ?",
                 "observe the published token or a disengaged value"),
        "request": ("model-only: upstream stop-token library contract",
                    "latch the request and commit a registered invocation if present"),
        "forward": ("__proxy_stop_forwarder::operator() | (*__pin)->request_stop();",
                    "pin the source locally and deliver the stop request"),
        "resume": ("__proxy_stop_forwarder::operator() | auto __pin = __source;",
                   "return through the source request after any nested callback"),
        "terminal": ("__proxy_stop_adapter::__finish | __observe_(__proxy_stop_event::terminal_commit);",
                     "commit the unique terminal after the backend joins its queries"),
        "unregister": ("__proxy_stop_adapter::__finish | __callback_.reset();",
                       "remove the registration, waiting on foreign invocation only"),
        "outward": ("model-only: caller dispatches its terminal after __finish returns",
                    "outward completion can immediately destroy the operation"),
    }.items()
}

FRESH, INITIALIZING, READY = range(3)
ENTER, SOURCE, REGISTER, PUBLISH, NOTIFY, WAIT, PARK, READ, DONE = range(9)
NONE, SELECTED, RUNNING = range(3)
LIVE, TERMINAL, UNREGISTERED, OUTWARD = range(4)
C = None
COV = set()


@dataclass(frozen=True)
class State:
    pcs: tuple
    phase: int = FRESH
    notified: bool = False
    source: bool = False
    registrations: int = 0
    registered: bool = False
    callback_live: bool = False
    requests: int = 0
    invocation: int = NONE
    immediate: int = -1
    pending_at_registration: bool = False  # Only synchronous delivery is due at query return.
    owed: bool = False  # Contract ghost, not used to enable delivery.
    forwarded: int = 0
    terminal: int = LIVE
    pin: bool = False
    answers: tuple = ()


def configure(cfg):
    global C
    C = cfg
    COV.clear()


def initial():
    return State((ENTER,) * C.queries, requests=int(C.before))


def key(w):
    return w


def state(k):
    return k


def put(values, i, x):
    return values[:i] + (x,) + values[i+1:]


def successors(w):
    out = []

    def add(step, actor, new, *cover):
        COV.update(cover)
        out.append(((step, actor), new))

    for i, pc in enumerate(w.pcs):
        def query(step, next_pc, *cover, **update):
            add(step, i, replace(w, pcs=put(w.pcs, i, next_pc), **update), *cover)

        if pc == ENTER:
            if w.phase == FRESH or (w.phase == INITIALIZING and C.mut == "duplicate_registration"):
                query("claim", SOURCE, "query.claim", phase=INITIALIZING)
            elif w.phase == INITIALIZING:
                query("wait", WAIT, "query.wait")
            else:
                query("claim", READ, "query.ready")
        elif pc == SOURCE:
            query("source", REGISTER if C.possible else PUBLISH,
                  "source.construct" if C.possible else "source.skip", source=C.possible,
                  phase=READY if C.control == "publish_before_registration" else w.phase)
        elif pc == REGISTER:
            immediate = w.requests != 0
            # The library always commits synchronous invocation when stop is
            # already latched. Only the adapter's delivery can be mutated.
            query("register", PUBLISH, "registration.immediate" if immediate else "registration.linked",
                  registrations=w.registrations + 1, registered=not immediate, callback_live=True,
                  invocation=SELECTED if immediate else w.invocation,
                  immediate=i if immediate else w.immediate, owed=w.owed or immediate,
                  pending_at_registration=w.pending_at_registration or immediate)
        elif pc == PUBLISH and (w.immediate != i or C.control == "publish_before_immediate_forward"):
            query("publish", NOTIFY, "query.publish", phase=READY)
        elif pc == NOTIFY:
            query("notify", READ, "query.notify", notified=True)
        elif pc == WAIT and not w.notified:
            query("wait", PARK, "query.park")
        if (pc in (WAIT, READ) and w.phase == READY) or (pc == PARK and w.notified):
            # A waiter may observe ready before parking. The parked branch
            # represents a waiter that requires the notify to return.
            # Keep the return-time facts even if later steps register or forward.
            answer = (w.source, w.phase, w.registrations,
                      w.pending_at_registration and not w.forwarded)
            query("read", DONE, "query.translated" if w.source else "query.disengaged",
                  answers=w.answers + (answer,))

    joined = all(pc == DONE for pc in w.pcs)
    if (C.possible and w.requests < C.requests and
            (C.timing == "arbitrary" or joined)):
        if w.requests == 0 and w.registered:
            add("request", "upstream", replace(w, requests=1, owed=True, invocation=SELECTED),
                "request.registered")
        else:
            where = ("request.repeat" if w.requests else "request.in_registration"
                     if REGISTER in w.pcs else "request.unregistered")
            add("request", "upstream", replace(w, requests=w.requests + 1), where)

    if w.invocation == SELECTED:
        delivered = C.mut != "lose_request"
        add("forward", "callback", replace(w, invocation=RUNNING, forwarded=w.forwarded + int(delivered),
                                          pin=C.control != "no_source_pin"),
            "request.pin", "request.delivered" if delivered else "request.dropped")
    if w.invocation == RUNNING and (C.completion != "self" or w.terminal == OUTWARD):
        add("resume", "callback", replace(w, invocation=NONE, immediate=-1, pin=False),
            "request.resume_after_destroy" if w.terminal == OUTWARD else "request.resume")

    if w.terminal == LIVE and joined:
        if C.completion == "ordinary" or (C.completion == "self" and w.invocation == RUNNING) or (
                C.completion == "cross" and w.owed):
            add("terminal", "terminal", replace(w, terminal=TERMINAL), "terminal.commit")
    elif w.terminal == TERMINAL:
        if w.invocation != NONE and C.completion != "self":
            COV.add("terminal.wait")
        else:
            removal = "terminal.self_remove" if w.invocation != NONE else "terminal.unregister"
            add("unregister", "terminal", replace(w, terminal=UNREGISTERED, registered=False,
                                                  callback_live=False), removal)
        if C.control == "terminal_before_cleanup" and w.callback_live:
            add("outward", "terminal", replace(w, terminal=OUTWARD), "terminal.entry")
    elif w.terminal == UNREGISTERED:
        add("outward", "terminal", replace(w, terminal=OUTWARD), "terminal.entry")
    return out


def state_violations(w):
    errors = []
    if w.owed and w.invocation == NONE and w.forwarded == 0:
        errors.append("PA1: committed stop request was lost")
    if w.forwarded > 1 or w.registrations > 1:
        errors.append("PA2: duplicate callback registration or forwarding")
    if w.terminal == OUTWARD and w.callback_live:
        errors.append("PA3: outward completion still owns a registration")
    if w.terminal == OUTWARD and w.invocation == RUNNING and not w.pin:
        errors.append("PA4: forwarding resumes after source destruction")
    published = (C.possible, READY, int(C.possible), False)
    if any(answer != published for answer in w.answers):
        errors.append("PA5: token returned without stable publication, registration or pending-stop delivery")
    return errors


def terminal_violations(w):
    errors = state_violations(w)
    if w.terminal != OUTWARD or len(w.answers) != C.queries or w.invocation != NONE:
        errors.append("PA6: terminal, query or callback has not retired")
    if w.registrations != int(C.possible and bool(C.queries)):
        errors.append("PA2: wrong number of registrations")
    return errors


def is_blocked(w):
    return (PARK in w.pcs and not w.notified) or (
        w.terminal == TERMINAL and w.invocation != NONE and C.completion != "self")


def is_aborted(w):
    return False


def metrics():
    return {"max_store_buffer": 0}


def coverage():
    if not C.mut and not C.control:
        assert C.required <= COV, C.required - COV
    return sorted(COV)


def step_table():
    return [("protocol", name, step.lines, "SC / library / caller", step.desc)
            for name, step in STEPS.items()]


def describe(meta, w):
    step, actor = meta
    return "%s %s: %s" % (actor, step, w)

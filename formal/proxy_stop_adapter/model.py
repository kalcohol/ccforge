"""Finite SC interleavings of lazy proxy stop adaptation and terminal cleanup."""
from dataclasses import dataclass, replace
from types import SimpleNamespace

A = "__proxy_stop_adapter::"
STEPS = {
    k: SimpleNamespace(lines=reference, desc=description)
    for k, (reference, description) in {
        "claim": (A + "__query | __phase_.compare_exchange_strong",
                  "single initializer by SC CAS"),
        "wait": (A + "__query | __phase_.wait(__initializing);",
                 "busy observations wait for publication"),
        "observe": (A + "__initialize | if (__upstream.stop_possible()) {",
                    "the initializer's one observation of whether the upstream can stop"),
        "source": (A + "__initialize | __cell.emplace();",
                   "construct the live source only for a token observed possible"),
        "fresh": (A + "__initialize | std::terminate();",
                  "the live source reports a fresh stop state, or the program ends (T1, T2)"),
        "issue": (A + "__initialize | __token_.__token.emplace(__cell->get_token());",
                  "issue and commit the live token once, before the forwarder can reach the source"),
        "sample": (A + "__issue_impossible | __token_.__token.emplace(*__sampled);",
                   "impossible upstream of the issued type: the receiver's own token is the answer"),
        "value": (A + "__issue_impossible | __token_.__token.emplace(std::inplace_stop_token());",
                  "impossible upstream, in-place token: the specified disengaged value"),
        "sure": (A + "__search | __token_.__token.emplace();",
                 "K2 known to be impossible without observing it; nothing after it exists"),
        "tokdef": (A + "__search | const __issued_t __value = __issued_t();",
                   "K2: the token type's nonthrowing default, observed"),
        "nostop": (A + "__examine_sources | __proxy_emplace_no_stop_state(__cell);",
                   "K3: a source made from nostopstate in the cell, and its token"),
        "hold": (A + "__examine_sources | __held.emplace(*__candidate);",
                 "K3 is engaged and unrequested: kept while K4 is examined"),
        "drop_token": (A + "__examine_sources | __candidate.reset();",
                       "a requested K3 releases its token"),
        "drop_source": (A + "__examine_sources | __cell.reset();",
                        "and then its source"),
        "default": (A + "__examine_default_source | __slot.emplace();",
                    "K4: a default constructed source, in the spare slot if K3 is held"),
        "drop_held_token": (A + "__examine_default_source | __held.reset();",
                            "an impossible K4 wins: the held K3 releases its token"),
        "drop_held_source": (A + "__examine_default_source | __source_.__get().reset();",
                             "and then its source"),
        "drop_candidate_token": (A + "__examine_default_source | __candidate.reset();",
                                 "a K4 that is not chosen releases its token"),
        "drop_candidate_source": (A + "__examine_default_source | __slot.reset();",
                                  "and then its source"),
        "commit": (A + "__commit | __token_.__token.emplace(__answer);",
                   "the one committed answer"),
        "abort": (A + "__examine_default_source | std::terminate();",
                  "T3: no candidate is usable without reporting a request"),
        "abort_sources": (A + "__exhausted | std::terminate();",
                          "T3 where no default source exists"),
        "register": (A + "__initialize | __callback_.__registration.emplace(",
                     "construct the upstream callback, which links it unless the upstream is requested or gone"),
        "publish": (A + "__query | __phase_.store(__ready);",
                    "publish completed initialization"),
        "notify": (A + "__query | __phase_.notify_all();",
                   "wake observers after ready publication"),
        "copy": ("__proxy_token_slot::__copy | __forge_sync::unique_lock __lock{__copying};",
                 "enter the copy of the committed answer, one reader at a time"),
        "read": (A + "__query | return __token_.__copy();",
                 "leave the copy and return it"),
        "lose": ("model-only: the upstream loses its last source without a request",
                 "the upstream token stops being possible"),
        "request": ("model-only: upstream stop-token library contract",
                    "latch the request and commit a registered invocation if present"),
        "forward": ("__proxy_stop_forwarder::operator() | (*__pin)->request_stop();",
                    "pin the source locally and deliver the stop request"),
        "resume": ("__proxy_stop_forwarder::operator() | auto __pin = __source;",
                   "return through the source request after any nested callback"),
        "terminal": (A + "__finish | __observe_(__proxy_stop_event::terminal_commit);",
                     "commit the unique terminal after the backend joins its queries"),
        "unregister": ("__proxy_callback_slot::__remove | __registration.reset();",
                       "remove the registration, waiting on foreign invocation only"),
        "outward": ("model-only: caller dispatches its terminal after __finish returns",
                    "outward completion can immediately destroy the operation"),
    }.items()
}

FRESH, INITIALIZING, READY = range(3)
(ENTER, OBSERVE, SOURCE, CHECK, ISSUE, REGISTER, SEARCH, PUBLISH, NOTIFY, WAIT, PARK, READ, COPYING,
 DONE) = range(14)
NONE, SELECTED, RUNNING = range(3)
LIVE, TERMINAL, UNREGISTERED, OUTWARD = range(4)
# What a candidate's token reports: (stop_possible, stop_requested).
OBSERVED = {"impossible": (False, False), "sure": (False, False), "eligible": (True, False),
            "requested": (True, True)}
# A slot: (candidate, source alive, adapter-owned candidate token alive).
EMPTY = ("", False, False)
C = None
COV = set()


@dataclass(frozen=True)
class State:
    pcs: tuple
    phase: int = FRESH
    notified: bool = False
    upstream: bool = True  # Whether the upstream token can still stop.
    sampled: object = None  # The initializer's observation of that, once made.
    source: bool = False  # The live source, the forwarder's target.
    slots: tuple = (EMPTY, EMPTY)  # The cell and the spare slot during and after the search.
    todo: tuple = ()  # The initializer's remaining steps of the search.
    made: int = 0  # Sources ever constructed.
    factory: int = 0  # Calls of a source's token factory.
    commits: int = 0  # Answers committed to the cache.
    misordered: bool = False  # A source was released while its token was alive.
    token: tuple = ()  # The committed answer: (candidate, possible, requested).
    copying: int = 0  # Readers inside the copy of the answer.
    registrations: int = 0  # Upstream callback objects constructed.
    registered: bool = False  # The callback is linked for a later request.
    callback_live: bool = False
    requests: int = 0
    invocation: int = NONE
    immediate: int = -1
    pending_at_registration: bool = False  # Only synchronous delivery is due at query return.
    owed: bool = False  # Contract ghost, not used to enable delivery.
    forwarded: int = 0
    terminal: int = LIVE
    pin: bool = False
    aborted: bool = False
    answers: tuple = ()


def configure(cfg):
    global C
    C = cfg
    COV.clear()


def initial():
    return State((ENTER,) * C.queries, requests=int(C.before), upstream=C.possible)


def key(w):
    return w


def state(k):
    return k


def put(values, i, x):
    return values[:i] + (x,) + values[i+1:]


def oracle(cfg=None):
    """The answer for an upstream that cannot stop, from the candidates alone.

    Written from the three selection rules of the decision table, and not
    from the steps below. Returns (answer, sources constructed, sources that
    survive). The answer is a candidate name, "T3" or "X3".
    """
    c = cfg or C
    if c.same_type:
        return "sample", 0, 0
    if c.tokdef == "inplace":
        return "value", 0, 0
    order = [("tokdef", c.tokdef), ("nostop", c.nostop), ("default", c.default)]
    if not any(result for _, result in order):
        return "X3", 0, 0
    made = 0
    for name, result in order:
        if not result:
            continue
        made += name != "tokdef"
        if result in ("impossible", "sure"):
            return name, made, int(name != "tokdef")
    for name, result in order[1:]:
        if result == "eligible":
            return name, made, 1
    return "T3", made, 0


def expected_slots(cfg=None):
    """Which source survives, and in which slot, from the candidates alone.

    A source made from nostopstate lives in the cell. A default constructed
    source lives in the cell too, unless an eligible nostopstate source was
    being held when it was examined: then it lives in the spare slot.
    """
    c = cfg or C
    answer = oracle(c)[0]
    if answer == "nostop":
        return (("nostop", True, False), EMPTY)
    if answer == "default":
        kept = ("default", True, False)
        return (EMPTY, kept) if c.nostop == "eligible" else (kept, EMPTY)
    return (EMPTY, EMPTY)


def plan():
    """The initializer's steps for the search, as the source performs them."""
    steps = []
    if C.control == "early_termination":
        return [("abort", "abort")]
    if C.same_type:
        return [("sample", "commit", "sample")]
    if C.tokdef == "inplace":
        return [("value", "commit", "value")]
    if C.tokdef == "sure":
        return [("sure", "commit", "tokdef")]
    if C.tokdef:
        steps.append(("tokdef", "poll", "tokdef"))
        if C.tokdef == "impossible":
            return steps + [("commit", "commit", "tokdef")]
    held = False
    if C.nostop:
        steps.append(("nostop", "make", 0, "nostop"))
        if C.nostop == "impossible":
            return steps + [("commit", "commit", "nostop")]
        if C.nostop == "requested":
            steps += [("drop_token", "token", 0), ("drop_source", "source", 0)]
        elif C.control == "engaged_where_representable":
            return steps + [("commit", "commit", "nostop")]
        else:
            steps.append(("hold", "hold", 0))
            held = True
    if C.default:
        slot = 1 if held else 0
        steps.append(("default", "make", slot, "default"))
        release = [("drop_candidate_token", "token", slot), ("drop_candidate_source", "source", slot)]
        if C.control == "source_released_first":
            release.reverse()
        if C.default == "impossible":
            if held and C.control == "wrong_survivor":
                # The right number of sources, but the wrong one is kept.
                return steps + [("commit", "commit", "default"), ("drop_candidate_source", "source", slot)]
            if held:
                steps += [("drop_held_token", "token", 0), ("drop_held_source", "source", 0)]
            return steps + [("commit", "commit", "default")]
        if held:
            return steps + release + [("commit", "commit", "nostop")]
        if C.default == "eligible" or C.control == "fabricated_request":
            return steps + [("commit", "commit", "default")]
        return steps + release + [("abort", "abort")]
    if held:
        return steps + [("commit", "commit", "nostop")]
    return steps + [("abort_sources", "abort")]


def successors(w):
    out = []
    if w.aborted:
        return out

    def add(step, actor, new, *cover):
        COV.update(cover)
        out.append(((step, actor), new))

    for i, pc in enumerate(w.pcs):
        def query(step, next_pc, *cover, **update):
            add(step, i, replace(w, pcs=put(w.pcs, i, next_pc), **update), *cover)

        def search(step, *cover, **update):
            # Enter the search for an upstream that cannot stop.
            first = tuple(plan())
            query(step, SEARCH, *cover, todo=first, **update)

        if pc == ENTER:
            if w.phase == FRESH or (w.phase == INITIALIZING and C.mut == "duplicate_registration"):
                if C.static:
                    # A statically unstoppable upstream is not polled.
                    search("claim", "query.claim", phase=INITIALIZING, sampled=False)
                else:
                    query("claim", OBSERVE, "query.claim", phase=INITIALIZING)
            elif w.phase == INITIALIZING:
                query("wait", WAIT, "query.wait")
            else:
                query("claim", READ, "query.ready")
        elif pc == OBSERVE:
            # The observation point. A later loss does not change the answer.
            if w.upstream:
                query("observe", SOURCE, "observe.possible", sampled=True)
            else:
                search("observe", "observe.impossible", sampled=False)
        elif pc == SOURCE:
            rebuilt = int(C.control == "sequential_reconstruction")
            query("source", CHECK, "source.construct", source=True, slots=(("live", True, False), EMPTY),
                  made=w.made + 1 + rebuilt,
                  phase=READY if C.control == "publish_before_registration" else w.phase)
        elif pc == CHECK:
            if C.default == "eligible" or C.control == "bypassed_fresh_check":
                query("fresh", ISSUE, "source.fresh")
            else:
                add("fresh", i, replace(w, aborted=True), "abort", "source.stale")
        elif pc == ISSUE:
            query("issue", REGISTER, "token.issue", token=("live", True, False), factory=w.factory + 1,
                  commits=w.commits + 1)
        elif pc == SEARCH:
            op = w.todo[0]
            rest = w.todo[1:]
            step, kind = op[0], op[1]
            if kind == "poll":
                query(step, SEARCH, "search." + op[2], todo=rest)
            elif kind == "make":
                slot, name = op[2], op[3]
                query(step, SEARCH, "search." + name, todo=rest, made=w.made + 1, factory=w.factory + 1,
                      slots=put(w.slots, slot, (name, True, True)))
            elif kind == "hold":
                query(step, SEARCH, "search.held", todo=rest)
            elif kind == "token":
                name, alive, _ = w.slots[op[2]]
                query(step, SEARCH, "release.token", todo=rest, slots=put(w.slots, op[2], (name, alive, False)))
            elif kind == "source":
                name, _, token_alive = w.slots[op[2]]
                query(step, SEARCH if rest else PUBLISH, "release.source", todo=rest,
                      misordered=w.misordered or token_alive,
                      slots=put(w.slots, op[2], EMPTY if not token_alive else (name, False, True)))
            elif kind == "commit":
                name = op[2]
                seen = OBSERVED[{"sample": "impossible", "value": "impossible",
                                 "tokdef": C.tokdef or "impossible", "nostop": C.nostop or "impossible",
                                 "default": C.default or "impossible"}[name]]
                # The candidate token is copied into the cache and released.
                slots = tuple((n, alive, False) if n == name else (n, alive, token)
                              for n, alive, token in w.slots)
                query(step, PUBLISH if not rest else SEARCH, "answer." + name, todo=rest, token=(name,) + seen,
                      commits=w.commits + 1, slots=slots)
            else:
                add(step, i, replace(w, aborted=True, todo=()), "abort")
        elif pc == REGISTER:
            immediate = w.requests != 0
            # The library always commits synchronous invocation when stop is
            # already latched. Only the adapter's delivery can be mutated. A
            # callback constructed after the upstream lost its last source
            # is an object that nothing will ever invoke.
            linked = not immediate and w.upstream
            query("register", PUBLISH, "registration.immediate" if immediate else
                  "registration.linked" if linked else "registration.unlinked",
                  registrations=w.registrations + 1, registered=linked, callback_live=True,
                  invocation=SELECTED if immediate else w.invocation,
                  immediate=i if immediate else w.immediate, owed=w.owed or immediate,
                  pending_at_registration=w.pending_at_registration or immediate)
        elif pc == PUBLISH and (w.immediate != i or C.control == "publish_before_immediate_forward"):
            query("publish", NOTIFY, "query.publish", phase=READY)
        elif pc == NOTIFY:
            query("notify", READ, "query.notify", notified=True)
        elif pc == WAIT and not w.notified:
            query("wait", PARK, "query.park")
        elif pc == COPYING:
            # Keep the return-time facts even if later steps register or forward.
            reissued = int(C.control == "reader_issues")
            answer = (w.token, w.phase, w.registrations,
                      w.pending_at_registration and not w.forwarded)
            query("read", DONE, "query.translated" if w.source else
                  "query.stored" if any(alive for _, alive, _ in w.slots) else "query.disengaged",
                  answers=w.answers + (answer,), factory=w.factory + reissued, copying=w.copying - 1)
        if (pc in (WAIT, READ) and w.phase == READY) or (pc == PARK and w.notified):
            # A waiter may observe ready before parking. The parked branch
            # represents a waiter that requires the notify to return. Readers
            # take the copy lock one at a time.
            if w.copying == 0 or C.control == "unserialized_copy":
                query("copy", COPYING, "copy.enter", copying=w.copying + 1)
            else:
                COV.add("copy.wait")

    joined = all(pc == DONE for pc in w.pcs)
    if C.loss and w.upstream and w.requests == 0:
        add("lose", "upstream", replace(w, upstream=False),
            "loss.before_observation" if w.sampled is None else
            "loss.before_registration" if REGISTER in w.pcs or ISSUE in w.pcs or CHECK in w.pcs or SOURCE in w.pcs
            else "loss.after_registration")

    if ((w.upstream or w.requests) and w.requests < C.requests and
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


def live_sources(w):
    return sum(alive for _, alive, _ in w.slots)


def state_violations(w):
    errors = []
    if w.owed and w.invocation == NONE and w.forwarded == 0:
        errors.append("PA1: committed stop request was lost")
    # Two sources are alive together only while a held nostopstate source and
    # a default constructed one are both under examination.
    names = sorted(name for name, alive, _ in w.slots if alive)
    searching = SEARCH in w.pcs
    if (w.forwarded > 1 or w.registrations > 1 or
            (len(names) == 2 and not (searching and names == ["default", "nostop"])) or
            w.made > (1 if w.sampled else 2)):
        errors.append("PA2: duplicate source, callback registration or forwarding")
    if w.terminal == OUTWARD and w.callback_live:
        errors.append("PA3: outward completion still owns a registration")
    if w.terminal == OUTWARD and w.invocation == RUNNING and not w.pin:
        errors.append("PA4: forwarding resumes after source destruction")
    if w.copying > 1:
        errors.append("PA9: two readers copy the committed answer at once")
    if w.misordered:
        errors.append("PA10: a source was released while a token issued from it was alive")
    if w.phase == READY and not w.aborted:
        answer, made, survivors = ("live", 1, 1) if w.sampled else oracle()
        # At publication, before any reader returns. Exactly one committed
        # answer; it is the candidate the decision table selects; the sources
        # ever made and the sources that remain are the table's, with no
        # candidate token left over; one factory call per source examined.
        # Copies returned to readers are counted apart.
        if w.commits != 1:
            errors.append("PA8: the answer was not issued and committed exactly once before publication")
        elif not w.token or w.token[0] != answer:
            errors.append("PA7: the committed answer is not the candidate the decision table selects")
        elif (w.made != made or live_sources(w) != survivors or any(token for _, _, token in w.slots) or
              (not w.sampled and w.slots != expected_slots())):
            # The count is not enough: the source that survives must be the
            # one the committed answer was issued from, in its slot.
            errors.append("PA2: duplicate source, callback registration or forwarding")
        elif w.factory != made:
            errors.append("PA8: the answer was not issued and committed exactly once before publication")
        elif not w.sampled:
            # W2, from the committed token's own observers. A request is
            # never fabricated. Possible is reported only by an eligible
            # source that is the selected answer (ST-07.2).
            _, possible, requested = w.token
            expected = OBSERVED[{"sample": "impossible", "value": "impossible"}.get(
                answer, getattr(C, answer, None) or "impossible")]
            if requested or (possible, requested) != expected:
                errors.append("PA7: the committed answer does not report what the selected candidate observes")
    for token, phase, registrations, pending in w.answers:
        if not token or phase != READY or registrations != int(bool(w.sampled)) or pending:
            errors.append("PA5: token returned without stable publication, registration or pending-stop delivery")
    return errors


def expected_abort(w):
    """Whether the program must end for the observation made, from the candidates alone."""
    if w.sampled:
        return C.default != "eligible"
    return oracle()[0] == "T3"


def terminal_violations(w):
    errors = state_violations(w)
    if w.aborted:
        if not expected_abort(w):
            errors.append("PA7: the program ended although a permitted answer exists")
        # Nothing is committed. T1 and T2 end after one construction and
        # before any factory call. T3 ends after every available source
        # candidate was constructed and its token issued.
        made = 1 if w.sampled else oracle()[1]
        factory = 0 if w.sampled else made
        if w.commits or w.made != made or w.factory != factory:
            errors.append("PA8: the answer was not issued and committed exactly once before publication")
        return errors
    if w.sampled is not None and expected_abort(w):
        errors.append("PA7: an answer was published although no permitted answer exists")
    if w.sampled is None and (w.commits or w.factory or w.made):
        errors.append("PA8: the answer was not issued and committed exactly once before publication")
    if w.terminal != OUTWARD or len(w.answers) != C.queries or w.invocation != NONE:
        errors.append("PA6: terminal, query or callback has not retired")
    if w.registrations != int(bool(w.sampled)):
        errors.append("PA2: wrong number of registrations")
    return errors


def is_blocked(w):
    return (PARK in w.pcs and not w.notified) or (
        w.terminal == TERMINAL and w.invocation != NONE and C.completion != "self")


def is_aborted(w):
    return w.aborted


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

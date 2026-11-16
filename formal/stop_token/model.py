#!/usr/bin/env python3
WAIT_CALL_SITES = (
    "model-only; call site inplace_stop_source::wait_for_unlock | state_.wait(state, "
    "std::memory_order_relaxed); || model-only; call site "
    "inplace_stop_source::remove_callback | cb->completion.wait(0, "
    "std::memory_order_acquire);"
)
NOTIFY_CALL_SITES = (
    "model-only; call site inplace_stop_source::unlock | state_.notify_all(); || "
    "model-only; call site inplace_stop_source::request_stop | "
    "node->completion.notify_all(); || model-only; call site "
    "inplace_stop_source::~inplace_stop_source | state_.notify_all();"
)
INVOKE_CALLBACK_REF = "inplace_stop_source::request_stop | node->invoke_fn(node);"
FINAL_HANDOFF_REF = "inplace_stop_source::remove_callback | unlock(stop_site::remove_handoff, lock());"
"""Explicit-state model checker for the inplace_stop_source lock / completion
protocol in backport/cpp26/execution/stop_token.hpp.

Standard library only.  Run with: python3 -B formal/stop_token/check.py --help

Every thread is a stack of frames; each frame is a program counter into a
routine.  A routine is a table of steps (see the @step decorators); each step
is exactly one atomic access (load, CAS, exchange, fetch_or, store, wait
compare-and-block, notify) or one block of plain accesses made under the
source lock (kind 'plain', which asserts lock ownership), or a purely
thread-local continuation (kind 'local', chained to the preceding step).
Mutants are single `if mut(...)` lines inside the affected step.
"""

S_, L_, W_, DEAD = 1, 2, 4, 8  # kStopRequested, kLocked, kLockWaiters, kDestroyed(model)
RUN, DESTROYED, AWAITED = 0, 1, 2  # invocation_status
RID, PC, RV, X, Y, Z = range(6)  # frame slots


class Cfg:
    def __init__(self, **kw):
        self.name = "cfg"
        self.threads = []  # list of scripts (list of ops)
        self.ncb = 0
        self.bodies = {}  # callback -> script
        self.reg = ()  # initially registered callbacks, head first
        self.Y = 1  # yield-phase re-reads before arming the waiter bit
        self.mem = "sc"  # 'sc' | 'x86' | 'arm'
        self.lib = "abs"  # 'abs' | 'cnt_sc' | 'cnt_plain'
        self.elig = "own"  # abstract notify eligibility: 'own' | 'any'
        self.spurious = False  # spurious wakeups of blocked waits
        self.weakfail = False  # spurious failure of the waiter-bit compare_exchange_weak
        self.strict = False  # FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME probes
        self.check_inv = True  # lock word invariants (inplace_stop_source protocol)
        self.precond_violation = False  # scenario g: source destroyed while in use
        self.arm_rmw_early = True
        # Model: relaxed/acquire loads of state_ may return any value that is
        # not older (in modification order) than the last one this thread
        # observed or wrote (read-read / write-read coherence only).  RMWs and
        # the futex/abstract compare-and-block read the latest value.
        self.stale = False
        # Model: vector-clock happens-before tracking (release sequences of
        # RMWs included) with race checks on ghost plain locations.
        self.hb = False
        self.muts = frozenset()
        self.__dict__.update(kw)


C = None
NT = 0
STEPS = {}
COV = set()
STATS = {"maxbuf": 0}


class Step:
    __slots__ = ("fn", "lines", "at", "local", "nalts", "guard", "desc")


def step(rid, pc, lines, at, desc, nalts=None, guard=None):
    def deco(fn):
        s = Step()
        s.fn, s.lines, s.at, s.nalts, s.guard, s.desc = fn, lines, at, nalts, guard, desc
        s.local = at == "local"
        STEPS[(rid, pc)] = s
        return fn

    return deco


def mut(m):
    return m in C.muts


def setx(tup, i, v):
    return tup[:i] + (v,) + tup[i + 1 :]


FIELDS = (
    "word",
    "owner",
    "lst",
    "status",
    "reqthr",
    "comp",
    "inv",
    "exe",
    "freed",
    "assoc",
    "ctord",
    "rec",
    "recl",
    "blk",
    "cnt",
    "bufs",
    "winner",
    "rtrue",
    "stopseen",
    "early",
    "abort",
    "thr",
    "hist",
    "view",
    "vc",
    "sync",
    "accs",
    "cclk",
)


class World:
    __slots__ = FIELDS + ("v", "cur")

    def key(s):
        return (
            s.word,
            s.owner,
            s.lst,
            s.status,
            s.reqthr,
            s.comp,
            s.inv,
            s.exe,
            s.freed,
            s.assoc,
            s.ctord,
            s.rec,
            s.recl,
            s.blk,
            s.cnt,
            s.bufs,
            s.winner,
            s.rtrue,
            s.stopseen,
            s.early,
            s.abort,
            s.thr,
            s.hist,
            s.view,
            s.vc,
            s.sync,
            s.accs,
            s.cclk,
        )

    @staticmethod
    def of(k):
        w = World.__new__(World)
        (
            w.word,
            w.owner,
            w.lst,
            w.status,
            w.reqthr,
            w.comp,
            w.inv,
            w.exe,
            w.freed,
            w.assoc,
            w.ctord,
            w.rec,
            w.recl,
            w.blk,
            w.cnt,
            w.bufs,
            w.winner,
            w.rtrue,
            w.stopseen,
            w.early,
            w.abort,
            w.thr,
            w.hist,
            w.view,
            w.vc,
            w.sync,
            w.accs,
            w.cclk,
        ) = k
        w.v = None
        w.cur = None
        return w


# ---------------------------------------------------------------- Model: happens-before tracking
# Atomic objects: 0 = state_, 1 + c = node c's completion.  sync[obj] is the
# clock an acquire read of the current value synchronizes with (the join of
# every release that heads a release sequence the current value belongs to;
# every RMW continues release sequences, a non-RMW store restarts them).
def vjoin(a, b):
    return tuple(x if x >= y else y for x, y in zip(a, b))


def hb_acquire(w, t, obj):
    if not C.hb:
        return
    s = w.sync[obj]
    if s is not None:
        w.vc = setx(w.vc, t, vjoin(w.vc[t], s))


def hb_release(w, t, obj, rmw):
    if not C.hb:
        return
    v = w.vc[t]
    s = w.sync[obj]
    w.sync = setx(w.sync, obj, vjoin(s, v) if (rmw and s is not None) else v)
    w.vc = setx(w.vc, t, setx(v, t, v[t] + 1))


def hb_plain_store(w, obj):
    """A relaxed non-RMW store ends every release sequence on obj."""
    if C.hb:
        w.sync = setx(w.sync, obj, None)


def L_LIST():
    return 0


def L_ST(c):
    return 1 + c


def L_REC(t):
    return 1 + C.ncb + t


def L_FN(c):
    return 1 + C.ncb + NT + c


def L_UD(c):
    return 1 + 2 * C.ncb + NT + c


def L_PRE():
    return 1 + 3 * C.ncb + NT


LOCNAME = None


def hb_access(w, t, loc, write, what):
    """Plain access to a ghost location; reports accesses not ordered by happens-before."""
    if not C.hb:
        return
    rec = w.accs[loc]
    v = w.vc[t]
    if rec is None:
        wt, we, reads = -1, 0, (0,) * NT
    else:
        wt, we, reads = rec
        if wt != -1 and wt != t and v[wt] < we:
            viol(w, "data race on %s: %s by T%d vs write by T%d" % (what, "write" if write else "read", t, wt))
        if write:
            for u in range(NT):
                if u != t and reads[u] and v[u] < reads[u]:
                    viol(w, "data race on %s: write by T%d vs read by T%d" % (what, t, u))
    if write:
        w.accs = setx(w.accs, loc, (t, v[t], (0,) * NT))
    else:
        w.accs = setx(w.accs, loc, (wt, we, setx(reads, t, v[t])))


# ---------------------------------------------------------------- Model: stale reads of state_
def stale_alts(w, t, f):
    """Number of values a plain/acquire load of state_ by thread t may return."""
    if not C.stale:
        return 1
    return len(w.hist) - w.view[t]


def sload(w, t, a):
    """Load of state_: alternative a picks the a-th value at or after this thread's view."""
    if not C.stale:
        return load(w, t, 0)
    i = w.view[t] + a
    w.view = setx(w.view, t, i)
    return w.hist[i]


def see_latest(w, t):
    """A read of state_ that returns the latest value (RMW, failed CAS, futex compare)."""
    if C.stale:
        w.view = setx(w.view, t, len(w.hist) - 1)


def normalize_hist(w):
    """Drop history no unfinished thread can still read; keep keys canonical."""
    if not C.stale:
        return
    live = [u for u in range(NT) if w.thr[u]]
    last = len(w.hist) - 1
    m = min((w.view[u] for u in live), default=last)
    if m:
        w.hist = w.hist[m:]
        last -= m
    w.view = tuple((w.view[u] - m) if w.thr[u] else last for u in range(NT))


def viol(w, cat):
    if w.v is None:
        w.v = []
    w.v.append(cat)


def cov(tag):
    COV.add(tag)


# ---------------------------------------------------------------- frames
def call(w, rid, pc, x=0, y=0, z=0):
    w.cur.append([rid, pc, 0, x, y, z])


def ret(w, val=0):
    w.cur.pop()
    if w.cur:
        w.cur[-1][RV] = val


# ---------------------------------------------------------------- memory
def bufferable(order):
    if C.mem == "sc":
        return False
    if C.mem == "x86":
        return order != "sc"
    return True  # arm: STR / STLR without a trailing barrier


def mem_get(w, obj):
    return w.word if obj == 0 else w.comp[obj - 1]


def write(w, t, obj, val):
    """Commit a modification of atomic object obj to memory (modification order)."""
    if obj == 0:
        if (w.word & S_) and not (val & S_) and not (val & DEAD):
            viol(w, "stop_requested monotonicity: a write cleared kStopRequested")
        w.word = val
        if C.stale:
            w.hist = w.hist + (val,)
            w.view = setx(w.view, t, len(w.hist) - 1)
    else:
        c = obj - 1
        if w.freed[c]:
            viol(w, "node access after reclamation: completion store")
        w.comp = setx(w.comp, c, val)
    if C.lib == "abs":
        blk = w.blk
        for u in range(NT):
            b = blk[u]
            if b >= 0 and (b >> 8) == obj:
                blk = setx(blk, u, b | (1 << t))
        w.blk = blk


def load(w, t, obj):
    for o, v, _k in reversed(w.bufs[t]):
        if o == obj:
            return v
    return mem_get(w, obj)


def store(w, t, obj, val, order):
    if bufferable(order):
        b = w.bufs[t] + ((obj, val, 1 if order in ("rel", "sc") else 0),)
        w.bufs = setx(w.bufs, t, b)
        if len(b) > STATS["maxbuf"]:
            STATS["maxbuf"] = len(b)
    else:
        write(w, t, obj, val)


def acc(w, c, what):
    """Plain or atomic access to callback node storage."""
    if w.freed[c]:
        viol(w, "node access after reclamation: " + what)


def own(w, t, what):
    if w.owner != t:
        viol(w, "protected access without the source lock: " + what)


def observe_stop(w, v):
    if C.stale:
        return  # cross-thread order of plain loads is not total; per-thread coherence holds by view
    if v & S_:
        w.stopseen = 1
    elif w.stopseen:
        viol(w, "stop_requested monotonicity: false observed after true")


# ================================================================ lock()
def loop_head(w, f):
    """Protocol: examine the local word and pick the next atomic op."""
    st = f[X]
    if C.strict and (st & DEAD):
        cov("lock.check_lifetime.abort")
        w.abort = 1
        return
    if not (st & L_):
        f[Y] = 0
        f[PC] = "L2"
    elif C.Y > 0:
        f[Y] = 0
        f[PC] = "W1"
    else:
        arm_or_park(w, f)


def arm_or_park(w, f):
    """Protocol: skip the CAS when the observed word already carries the bit."""
    f[Y] = 0
    if f[X] & W_:
        cov("lock.park.bit_already_set")
        park(w, f)
    else:
        f[PC] = "W2"


def wait_entry():
    """Model: first step of state_.wait / completion.wait for the selected library."""
    if C.lib == "abs":
        return "C"
    if C.lib.startswith("cnt16"):
        return "G"
    return "E"


def park(w, f):
    """Protocol: state_.wait(state); continuation is the reload at source operation."""
    f[PC] = "W4"
    call(w, "wait", wait_entry(), 0, f[X])


@step(
    "lock",
    "L0",
    "inplace_stop_source::lock | auto state = state_.load(std::memory_order_relaxed);",
    "load_rlx",
    "state = state_.load(relaxed)",
    nalts=stale_alts,
)
def _(w, t, f, a):
    f[X] = sload(w, t, a)
    loop_head(w, f)


@step(
    "lock",
    "L2",
    "inplace_stop_source::lock | state_.compare_exchange_weak(state, state | kLocked,",
    "rmw",
    "compare_exchange_weak(state, state|kLocked, acquire, relaxed)",
)
def _(w, t, f, a):
    cur = w.word
    if cur == f[X]:
        if w.owner != -1:
            viol(w, "mutual exclusion: lock acquired while held")
        write(w, t, 0, cur | L_)
        w.owner = t
        cov("lock.cas.ok")
        if not mut("H1_lock_cas_relaxed"):  # HB mutant: success order relaxed
            hb_acquire(w, t, 0)  # acquire; as an RMW it continues release sequences
        ret(w, f[X])
    else:
        cov("lock.cas.fail")
        see_latest(w, t)
        f[X] = cur
        loop_head(w, f)
    # A spurious weak failure leaves `state` unchanged and re-enters L2: a pure
    # self-loop of the global state, so it is not generated.


@step(
    "lock",
    "W1",
    "inplace_stop_source::wait_for_unlock | std::this_thread::yield();",
    "load_rlx",
    "yield(); state = state_.load(relaxed) (yield phase)",
    nalts=stale_alts,
)
def _(w, t, f, a):
    f[X] = sload(w, t, a)
    if not (f[X] & L_) and not mut("M8b_no_unlocked_return_in_yield"):  # MUTANT: delete source operation
        cov("lock.yield.saw_unlocked")
        loop_head(w, f)
        return
    f[Y] += 1
    if f[Y] >= C.Y:
        arm_or_park(w, f)


def w2_alts(w, t, f):
    return 2 if C.weakfail else 1


@step(
    "lock",
    "W2",
    "inplace_stop_source::wait_for_unlock | state_.compare_exchange_weak(state, state | kLockWaiters,",
    "rmw",
    "compare_exchange_weak(state, state|kLockWaiters, relaxed, relaxed)",
    nalts=w2_alts,
)
def _(w, t, f, a):
    cur = w.word
    if a == 1:  # spurious failure of the weak CAS
        cov("lock.arm.spurious_fail")
        see_latest(w, t)
        loop_head(w, f)
        return
    if mut("M8_arm_fetch_or"):  # MUTANT: arming succeeds on any word
        write(w, t, 0, cur | W_)
        f[X] |= W_
        park(w, f)
        return
    if cur == f[X]:
        write(w, t, 0, cur | W_)
        cov("lock.arm.ok")
        if mut("M37_wait_on_reload_after_arming"):  # Mutant: wait on a fresh load
            f[PC] = "W3"
            return
        if not mut("M15_wait_without_bit_in_expected"):  # MUTANT: forget state |= kLockWaiters
            f[X] |= W_
        park(w, f)
    else:
        cov("lock.arm.fail")
        see_latest(w, t)
        f[X] = cur
        if mut("M2_wait_unarmed_after_failed_cas"):  # MUTANT: wait anyway
            park(w, f)
            return
        loop_head(w, f)  # source operation: examine again, never wait unarmed


@step(
    "lock",
    "W3",
    "model-only: script, test hook, or mutant continuation",
    "load_rlx",
    "state = state_.load(relaxed) after arming; wait on it",
    nalts=stale_alts,
)
def _(w, t, f, a):
    f[X] = sload(w, t, a)
    park(w, f)


@step(
    "lock",
    "W4",
    "inplace_stop_source::wait_for_unlock | return state_.load(std::memory_order_relaxed);",
    "load_rlx",
    "return state_.load(relaxed) after wait",
    nalts=stale_alts,
)
def _(w, t, f, a):
    f[X] = sload(w, t, a)
    loop_head(w, f)


# ================================================================ unlock()
def call_unlock(w, known):
    entry = "U0"
    if mut("M3_notify_from_stale_load"):
        entry = "Us"
    if mut("M14_notify_before_release"):
        entry = "Un"
    if C.mem == "arm" and C.lib in ("cnt_plain", "cnt16_plain") and C.arm_rmw_early:
        entry = "Ue"
    call(w, "unlock", entry, known)


@step(
    "unlock",
    "Us",
    "model-only: script, test hook, or mutant continuation",
    "load_rlx",
    "stale = state_.load() before the exchange",
)
def _(w, t, f, a):
    f[Y] = load(w, t, 0)
    f[PC] = "U0"


@step("unlock", "Un", "model-only: script, test hook, or mutant continuation", "local", "notify_all before the release")
def _(w, t, f, a):
    f[PC] = "U0"
    call_notify(w, 0, 1)


def ue_alts(w, t, f):
    return 2


@step(
    "unlock",
    "Ue",
    "model-only: script, test hook, or mutant continuation",
    "load_rlx",
    "pessimistic ARMv8 control: the plain waiter-count load of the coming notify is satisfied before the exchange",
    nalts=ue_alts,
)
def _(w, t, f, a):
    if a == 1:
        w.early = setx(w.early, t, w.cnt[0])
    f[PC] = "U0"


@step(
    "unlock",
    "U0",
    "inplace_stop_source::unlock | state_.exchange(known, std::memory_order_seq_cst)",
    "rmw",
    "previous = state_.exchange(known, seq_cst); decide wake",
)
def _(w, t, f, a):
    known = f[X]
    prev = w.word
    if w.owner != t:
        viol(w, "mutual exclusion: unlock by a thread that does not hold the lock")
    if C.strict and (prev & DEAD):
        cov("unlock.check_lifetime.abort")
        w.abort = 1  # source operation
        return
    if C.check_inv and (prev & ~W_) != (known | L_):
        viol(
            w,
            (
                "lock word invariant: word changed while held [source: inplace_stop_source::unlock | "
                "state_.exchange(known, std::memory_order_seq_cst)]"
            ),
        )
    write(w, t, 0, known)
    w.owner = -1
    # seq_cst exchange = acquire + release RMW (HB mutants H2: relaxed, H7: acquire only)
    if not mut("H2_unlock_exchange_relaxed"):
        hb_acquire(w, t, 0)
        if not mut("H7_unlock_exchange_acquire_only"):
            hb_release(w, t, 0, rmw=True)
    decide = f[Y] if mut("M3_notify_from_stale_load") else prev
    f[Y] = 0
    if (decide & W_) and not mut("M14_notify_before_release"):
        cov("unlock.notify")
        f[PC] = "U2"
        call_notify(w, 0, 0 if mut("M1_notify_one") else 1)  # source operation
    else:
        cov("unlock.no_notify")
        if w.early[t] >= 0:
            w.early = setx(w.early, t, -1)
        ret(w)


@step(
    "unlock", "U2", "inplace_stop_source::unlock | if ((previous & kLockWaiters) != 0)", "local", "return from unlock"
)
def _(w, t, f, a):
    ret(w)


# ================================================================ atomic wait / notify
#  abstract library ([atomics.wait]):   wait.C ; notify.N
#  count library (libstdc++-like):      wait.E ; wait.C ; wait.X      notify.N0 ; notify.N1
@step("wait", "E", WAIT_CALL_SITES, "rmw", "library: waiter count fetch_add(1, seq_cst)")
def _(w, t, f, a):
    w.cnt = setx(w.cnt, f[X], w.cnt[f[X]] + 1)
    f[PC] = "C"


@step("wait", "C", WAIT_CALL_SITES, "sys", "wait(old): atomically compare the value and block if equal")
def _(w, t, f, a):
    obj = f[X]
    if obj > 0:
        acc(w, obj - 1, "completion.wait")
    else:
        see_latest(w, t)
    if mem_get(w, obj) != f[Y]:
        cov("wait.%s.value_changed" % ("state" if obj == 0 else "completion"))
        if C.lib == "abs":
            if obj > 0 and not mut("H4_completion_load_relaxed"):
                hb_acquire(w, t, obj)  # completion.wait(0, acquire) load
            ret(w)
        else:
            f[PC] = "X"
    else:
        cov("wait.%s.blocked" % ("state" if obj == 0 else "completion"))
        w.blk = setx(w.blk, t, obj << 8)
        if C.lib.startswith("cnt16"):
            # libstdc++ 16 __wait_impl: futex returns -> waiter count fetch_sub ->
            # return to __atomic_wait_address, which reloads (step G).
            f[PC] = "X"


@step("wait", "X", WAIT_CALL_SITES, "rmw", "library: waiter count fetch_sub(1)")
def _(w, t, f, a):
    w.cnt = setx(w.cnt, f[X], w.cnt[f[X]] - 1)
    if C.lib.startswith("cnt16"):
        f[PC] = "G"
    else:
        ret(w)


def g_at(f):
    return "load_rlx" if f[X] == 0 else "load_acq"


def g_alts(w, t, f):
    return stale_alts(w, t, f) if f[X] == 0 else 1


@step(
    "wait",
    "G",
    WAIT_CALL_SITES,
    g_at,
    "libstdc++ 16: load the value with the caller order (_M_setup_wait / _M_on_wake / spin) and compare with old",
    nalts=g_alts,
)
def _(w, t, f, a):
    obj = f[X]
    if obj > 0:
        acc(w, obj - 1, "completion.wait (load)")
        v = load(w, t, obj)
        if v != f[Y] and not mut("H4_completion_load_relaxed"):
            hb_acquire(w, t, obj)
    else:
        v = sload(w, t, a)
    if v != f[Y]:
        cov("wait16.%s.value_changed" % ("state" if obj == 0 else "completion"))
        ret(w)
    else:
        f[PC] = "E"


def call_notify(w, obj, allw):
    if mut("M18_no_completion_notify") and obj > 0:
        return
    if mut("Mg_destructor_no_notify") and obj == 0 and w.cur[-1][RID] == "dsrc":
        return
    call(w, "notify", "N" if C.lib == "abs" else "N0", obj, allw)


def eligible(w, t, obj):
    out = []
    for u in range(NT):
        b = w.blk[u]
        if b >= 0 and (b >> 8) == obj:
            m = b & 255
            if (m >> t) & 1 if C.elig == "own" else m != 0:
                out.append(u)
    return out


def n_alts(w, t, f):
    if f[Y]:
        return 1
    return max(1, len(eligible(w, t, f[X])))


@step(
    "notify", "N", NOTIFY_CALL_SITES, "sys", "notify_all(): unblock every eligible wait ([atomics.wait])", nalts=n_alts
)
def _(w, t, f, a):
    obj = f[X]
    if obj > 0:
        acc(w, obj - 1, "completion.notify_all")
    el = eligible(w, t, obj)
    if not f[Y]:
        el = el[a : a + 1]
    if el:
        cov("notify.woke")
    else:
        cov("notify.nobody_eligible")
    for u in el:
        w.blk = setx(w.blk, u, -1)
    ret(w)


def n0_at(f):
    return "load_acq" if C.lib in ("cnt_sc", "cnt16_sc") else "load_rlx"


@step("notify", "N0", NOTIFY_CALL_SITES, n0_at, "library: load the waiter count, skip the wake when zero")
def _(w, t, f, a):
    obj = f[X]
    if obj > 0:
        acc(w, obj - 1, "completion.notify_all")
    c = w.cnt[obj]
    if obj == 0 and w.early[t] >= 0:
        c = w.early[t]
    if w.early[t] >= 0:
        w.early = setx(w.early, t, -1)
    if c == 0:
        cov("notify.count_zero_skip")
        ret(w)
    else:
        f[PC] = "N1"


def sleepers(w, obj):
    return [u for u in range(NT) if w.blk[u] >= 0 and (w.blk[u] >> 8) == obj]


def n1_alts(w, t, f):
    if f[Y]:
        return 1
    return max(1, len(sleepers(w, f[X])))


@step("notify", "N1", NOTIFY_CALL_SITES, "sys", "library: futex wake of the sleepers on this address", nalts=n1_alts)
def _(w, t, f, a):
    sl = sleepers(w, f[X])
    if not f[Y]:
        sl = sl[a : a + 1]
    if sl:
        cov("notify.woke")
    else:
        cov("notify.futex_wake_no_sleeper")
    for u in sl:
        w.blk = setx(w.blk, u, -1)
    ret(w)


# ================================================================ request_stop()
@step(
    "req",
    "R0",
    "inplace_stop_source::request_stop | if (stop_requested()) return false;",
    "load_acq",
    "if (stop_requested()) return false   (acquire load)",
    nalts=stale_alts,
)
def _(w, t, f, a):
    v = sload(w, t, a)
    if C.strict and (v & DEAD):
        w.abort = 1
        return
    observe_stop(w, v)
    if not mut("H6_stop_requested_relaxed"):  # HB mutant
        hb_acquire(w, t, 0)
    if v & S_:
        cov("req.fast_false")
        ret(w, 0)
    else:
        f[PC] = "R2"
        call(w, "lock", "L0")


@step(
    "req",
    "R2",
    "inplace_stop_source::request_stop | if ((state & kStopRequested) != 0)",
    "local",
    "if (state & kStopRequested) { unlock(request_lost); return false; }",
)
def _(w, t, f, a):
    f[X] = f[RV]
    f[RV] = 0
    if (f[X] & S_) and not mut("M12_no_loser_check"):
        cov("req.lost_under_lock")
        f[PC] = "Rf"
        call_unlock(w, f[X])
    else:
        f[PC] = "R3"


@step(
    "req", "Rf", "inplace_stop_source::request_stop | unlock(stop_site::request_lost, state);", "local", "return false"
)
def _(w, t, f, a):
    ret(w, 0)


@step(
    "req",
    "R3",
    "inplace_stop_source::request_stop | state_.fetch_or(kStopRequested, std::memory_order_release);",
    "rmw",
    "state_.fetch_or(kStopRequested, release); state |= kStopRequested",
)
def _(w, t, f, a):
    if w.winner != -1:
        viol(w, "request_stop: two winners")
    w.winner = t
    hb_access(w, t, L_PRE(), True, "data written before request_stop")
    if mut("M10_stop_bit_plain_store"):  # MUTANT: non-RMW store of the stale local word
        write(w, t, 0, f[X] | L_ | S_)
    else:
        write(w, t, 0, w.word | S_)
    if not mut("H3_stop_fetch_or_relaxed"):  # HB mutant
        hb_release(w, t, 0, rmw=True)
    if not mut("M9_forget_stop_bit_in_known"):  # MUTANT: omit `state |= kStopRequested`
        f[X] |= S_
    f[PC] = "R4"


@step(
    "req",
    "R4",
    "inplace_stop_source::request_stop | unlink(node);",
    "plain",
    "loop head under lock: select+unlink head, set status/request_thread; or finish",
)
def _(w, t, f, a):
    own(w, t, "callbacks_ list")
    deferred = f[Z]
    hb_access(w, t, L_LIST(), False, "callbacks_ / links")
    if not w.lst:
        cov("req.final")
        f[PC] = "Rtn" if deferred else "Rt"
        call_unlock(w, f[X])  # source operation
        return
    c = w.lst[0]
    acc(w, c, "request_stop unlink")
    hb_access(w, t, L_LIST(), True, "callbacks_ / links")
    if not mut("M41_unlink_after_invoke"):  # Mutant: node stays linked while invoked
        if len(w.lst) > 1:
            acc(w, w.lst[1], "request_stop unlink (next->prev)")
        w.lst = w.lst[1:]
    f[Y] = c
    cov("req.select")
    if mut("M23_status_after_unlock"):  # Mutant: publish status after unlocking
        f[PC] = "R4b"
        call_unlock(w, f[X])
        return
    w.rec = setx(w.rec, t, RUN)
    w.recl = setx(w.recl, t, 1)
    w.status = setx(w.status, c, t)
    w.reqthr = setx(w.reqthr, c, t)
    hb_access(w, t, L_ST(c), True, "node status / request_thread")
    hb_access(w, t, L_REC(t), True, "requester status record")
    if mut("M30_invoke_under_lock"):  # Mutant: invoke while holding the lock
        f[PC] = "R5"
        return
    f[PC] = "R5n" if deferred else "R5"
    call_unlock(w, f[X])  # source operation


@step(
    "req",
    "R4b",
    "model-only: script, test hook, or mutant continuation",
    "plain_unlocked",
    "status / request_thread / record published after the unlock at the source operation",
)
def _(w, t, f, a):
    c = f[Y]
    acc(w, c, "request_stop status publication")
    w.rec = setx(w.rec, t, RUN)
    w.recl = setx(w.recl, t, 1)
    w.status = setx(w.status, c, t)
    w.reqthr = setx(w.reqthr, c, t)
    hb_access(w, t, L_ST(c), True, "node status / request_thread")
    hb_access(w, t, L_REC(t), True, "requester status record")
    f[PC] = "R5"


@step("req", "R5", INVOKE_CALLBACK_REF, "local", "node->invoke_fn(node)")
def _(w, t, f, a):
    f[PC] = "R6"
    call(w, "invoke", "I0", f[Y], 0)


@step(
    "req",
    "R5n",
    "model-only: script, test hook, or mutant continuation",
    "local",
    "deferred completion notify after the unlock",
)
def _(w, t, f, a):
    z = f[Z]
    f[Z] = 0
    f[PC] = "R5"
    call_notify(w, z, 1)


@step(
    "req",
    "Rtn",
    "model-only: script, test hook, or mutant continuation",
    "local",
    "deferred completion notify after the final unlock",
)
def _(w, t, f, a):
    z = f[Z]
    f[Z] = 0
    f[PC] = "Rt"
    call_notify(w, z, 1)


@step(
    "req",
    "R6",
    "inplace_stop_source::request_stop | state = lock(); if (status != invocation_status::destroyed_on_requester)",
    "local",
    "state = lock()",
)
def _(w, t, f, a):
    if mut("M20_status_read_before_relock"):  # Mutant: read the record before lock()
        f[Z] = w.rec[t] + 1
    f[PC] = "R7"
    if mut("M30_invoke_under_lock"):
        f[RV] = f[X]  # still the owner; no lock()
        return
    call(w, "lock", "L0")


@step(
    "req",
    "R7",
    "inplace_stop_source::request_stop | node->status = nullptr;",
    "plain",
    "under lock: if (status != destroyed_on_requester) { node->status = nullptr; awaited? }",
)
def _(w, t, f, a):
    own(w, t, "invocation status record")
    f[X] = f[RV]
    f[RV] = 0
    c = f[Y]
    r = w.rec[t]
    if mut("M20_status_read_before_relock"):
        r = f[Z] - 1
        f[Z] = 0
    hb_access(w, t, L_REC(t), False, "requester status record")
    w.recl = setx(w.recl, t, 0)
    if mut("M41_unlink_after_invoke") and c in w.lst:
        i = w.lst.index(c)
        acc(w, c, "request_stop late unlink")
        w.lst = w.lst[:i] + w.lst[i + 1 :]
    if r != DESTROYED or mut("M7_touch_node_after_self_destruction"):
        acc(w, c, "request_stop node->status = nullptr")
        if not mut("M21_status_not_cleared"):  # Mutant: keep node->status set
            w.status = setx(w.status, c, -1)
            hb_access(w, t, L_ST(c), True, "node status / request_thread")
        if r == AWAITED:
            cov("req.awaited")
            f[PC] = "R8"
            return
        cov("req.running" if r == RUN else "req.M7")
    else:
        cov("req.destroyed_on_requester")
    f[Y] = 0
    f[PC] = "R4"


def r8_at(f):
    return "store_rel" if mut("M4_completion_store_release") else "store_sc"


@step(
    "req",
    "R8",
    "inplace_stop_source::request_stop | node->completion.store(1, std::memory_order_seq_cst);",
    r8_at,
    "node->completion.store(1, seq_cst)",
)
def _(w, t, f, a):
    c = f[Y]
    if mut("M22_completion_notify_before_store"):  # Mutant: notify, then store
        f[PC] = "R8b"
        call_notify(w, 1 + c, 1)
        return
    acc(w, c, "completion.store")
    store(w, t, 1 + c, 1, "rel" if mut("M4_completion_store_release") else "sc")
    if mut("H5_completion_store_relaxed"):  # HB mutant
        hb_plain_store(w, 1 + c)
    else:
        hb_release(w, t, 1 + c, rmw=False)
    if mut("M6_completion_notify_after_unlock"):
        f[Z] = 1 + c
        f[Y] = 0
        f[PC] = "R4"
        return
    f[PC] = "R9"


@step(
    "req",
    "R9",
    "inplace_stop_source::request_stop | node->completion.notify_all();",
    "local",
    "node->completion.notify_all() (still under the lock)",
)
def _(w, t, f, a):
    c = f[Y]
    if C.strict:
        f[PC] = "R9s"
    else:
        f[Y] = 0
        f[PC] = "R4"
    call_notify(w, 1 + c, 1)


@step(
    "req",
    "R8b",
    "model-only: script, test hook, or mutant continuation",
    "store_sc",
    "completion.store(1, seq_cst) after the notify",
)
def _(w, t, f, a):
    c = f[Y]
    acc(w, c, "completion.store")
    store(w, t, 1 + c, 1, "sc")
    hb_release(w, t, 1 + c, rmw=False)
    if C.strict:
        f[PC] = "R9s"
    else:
        f[Y] = 0
        f[PC] = "R4"


@step(
    "req",
    "R9s",
    "inplace_stop_source::request_stop | const auto notified_function = live_node->invoke_fn;",
    "plain",
    "strict probe: read live_node->invoke_fn after the notify",
)
def _(w, t, f, a):
    own(w, t, "strict probe")
    acc(w, f[Y], "strict probe after completion notify")
    f[Y] = 0
    f[PC] = "R4"


@step("req", "Rt", "inplace_stop_source::request_stop | return true;", "local", "return true")
def _(w, t, f, a):
    w.rtrue += 1
    ret(w, 1)


# ================================================================ invocation
@step("invoke", "I0", INVOKE_CALLBACK_REF, "user", "read invoke_fn and enter the user callback")
def _(w, t, f, a):
    c = f[X]
    if w.freed[c]:
        viol(w, "callback invoked after its destructor returned")
    n = w.inv[c] + 1
    w.inv = setx(w.inv, c, n)
    if n > 1:
        viol(w, "callback invoked more than once")
    if not f[Y] and w.winner != t:
        viol(w, "request_stop loser invoked a callback")
    w.exe = setx(w.exe, c, t)
    hb_access(w, t, L_FN(c), False, "callback function object / invoke_fn")
    hb_access(w, t, L_PRE(), False, "data written before request_stop (read by the callback)")
    cov("invoke.inline" if f[Y] else "invoke.requester")
    f[PC] = "I1"
    call(w, "script", "S", NT + c, 0)


@step("invoke", "I1", INVOKE_CALLBACK_REF, "user", "user callback returns")
def _(w, t, f, a):
    hb_access(w, t, L_UD(f[X]), True, "callback side effects")
    w.exe = setx(w.exe, f[X], -1)
    ret(w)


# ================================================================ constructor / try_add_callback  [212-228, 430-445]
@step(
    "ctor",
    "A0",
    "inplace_stop_source::try_add_callback | const auto state = lock();",
    "local",
    "source_ = token.source_; try_add_callback: lock()",
)
def _(w, t, f, a):
    w.assoc = setx(w.assoc, f[X], 1)
    hb_access(w, t, L_FN(f[X]), True, "callback function object / invoke_fn")
    f[PC] = "A1"
    call(w, "lock", "L0")


@step(
    "ctor",
    "A1",
    "inplace_stop_source::try_add_callback | cb->next = callbacks_;",
    "plain",
    "under lock: stop bit set -> unlock(add_stopped), else link at head",
)
def _(w, t, f, a):
    own(w, t, "callbacks_ list")
    st = f[RV]
    f[RV] = 0
    c = f[X]
    if (st & S_) and mut("M29_add_ignores_stop_bit"):  # Mutant: link although stopped
        hb_access(w, t, L_LIST(), True, "callbacks_ / links")
        if w.lst:
            acc(w, w.lst[0], "try_add_callback callbacks_->prev")
        w.lst = (c,) + w.lst
        w.assoc = setx(w.assoc, c, 3)
        cov("ctor.add_after_stop")
        f[PC] = "A2"
    elif st & S_:
        cov("ctor.add_stopped")
        f[PC] = "A2s"
    else:
        hb_access(w, t, L_LIST(), False, "callbacks_ / links")
        hb_access(w, t, L_LIST(), True, "callbacks_ / links")
        if w.lst:
            acc(w, w.lst[0], "try_add_callback callbacks_->prev")
        w.lst = (c,) + w.lst
        cov("ctor.add")
        f[PC] = "A2"
    call_unlock(w, st)


def hb_handoff_release(w, t, c):
    """The finished object is handed to the thread that will destroy it (scenario-level sync)."""
    if C.hb:
        v = w.vc[t]
        w.cclk = setx(w.cclk, c, v)
        w.vc = setx(w.vc, t, setx(v, t, v[t] + 1))


@step("ctor", "A2", "inplace_stop_source::try_add_callback | return true;", "local", "return true; constructor returns")
def _(w, t, f, a):
    w.ctord = setx(w.ctord, f[X], 1)
    hb_handoff_release(w, t, f[X])
    ret(w)


@step(
    "ctor",
    "A2s",
    "inplace_stop_callback::inplace_stop_callback | this->invoke_fn(this);",
    "local",
    "source_ = nullptr; this->invoke_fn(this)",
)
def _(w, t, f, a):
    w.assoc = setx(w.assoc, f[X], 2)
    f[PC] = "A3"
    call(w, "invoke", "I0", f[X], 1)


@step(
    "ctor",
    "A3",
    "inplace_stop_callback::inplace_stop_callback | source_ = nullptr;",
    "local",
    "constructor returns after the inline invocation",
)
def _(w, t, f, a):
    w.ctord = setx(w.ctord, f[X], 1)
    hb_handoff_release(w, t, f[X])
    ret(w)


# ================================================================ destructor / remove_callback  [230-267, 447-451]
@step(
    "dtor",
    "D0",
    "inplace_stop_source::remove_callback | const auto state = lock();",
    "local",
    "if (source_) remove_callback: lock()",
)
def _(w, t, f, a):
    if w.assoc[f[X]] not in (1, 3):
        cov("dtor.not_associated")
        if w.exe[f[X]] == t:
            cov("dtor.self_inside_own_inline_invocation")
        f[PC] = "Dz"
        return
    f[PC] = "D1"
    call(w, "lock", "L0")


@step(
    "dtor",
    "D1",
    "inplace_stop_source::remove_callback | *cb->status = invocation_status::awaited;",
    "plain",
    "under lock: pending / completed / on requester / awaited",
)
def _(w, t, f, a):
    own(w, t, "callback node")
    st = f[RV]
    f[RV] = 0
    c = f[X]
    acc(w, c, "remove_callback")
    hb_access(w, t, L_LIST(), False, "callbacks_ / links")  # if (cb->prev)
    if c not in w.lst or mut("M28_completed_check_before_pending"):
        hb_access(w, t, L_ST(c), False, "node status / request_thread")
    if mut("M28_completed_check_before_pending") and w.status[c] == -1:  # Mutant
        cov("dtor.completed")
        f[PC] = "Dz"
    elif c in w.lst:
        i = w.lst.index(c)
        if i + 1 < len(w.lst):
            acc(w, w.lst[i + 1], "unlink next->prev")
        if i > 0:
            acc(w, w.lst[i - 1], "unlink *prev")
        w.lst = w.lst[:i] + w.lst[i + 1 :]
        hb_access(w, t, L_LIST(), True, "callbacks_ / links")
        cov("dtor.pending")
        f[PC] = "Dz"
    elif w.status[c] == -1:
        cov("dtor.completed")
        f[PC] = "Dz"
    else:
        r = w.status[c]
        if not w.recl[r]:
            viol(w, "status record accessed outside its lifetime")
        if w.reqthr[c] == t and not mut("M17_no_same_thread_check"):
            w.rec = setx(w.rec, r, DESTROYED)
            hb_access(w, t, L_REC(r), True, "requester status record")
            cov("dtor.on_requester")
            if any(fr[RID] == "invoke" and fr[Y] for fr in w.cur):
                cov("dtor.on_requester.inside_inline_invocation")
            f[PC] = "Dz"
        else:
            cov("dtor.awaited")
            if any(fr[RID] == "invoke" and fr[Y] for fr in w.cur):
                cov("dtor.awaited.inside_inline_invocation")
            if mut("M19_awaited_after_unlock"):  # Mutant: record after leaving the lock
                f[Y] = r
                f[PC] = "D1w"
            else:
                w.rec = setx(w.rec, r, AWAITED)
                hb_access(w, t, L_REC(r), True, "requester status record")
                f[PC] = "D4" if (mut("M11_no_completion_wait") or mut("M25_handoff_before_wait")) else "D2"
    call_unlock(w, st)


@step(
    "dtor",
    "D1w",
    "model-only: script, test hook, or mutant continuation",
    "plain_unlocked",
    "*cb->status = awaited after remove_callback unlock",
)
def _(w, t, f, a):
    r = f[Y]
    if not w.recl[r]:
        viol(w, "status record accessed outside its lifetime")
    w.rec = setx(w.rec, r, AWAITED)
    hb_access(w, t, L_REC(r), True, "requester status record")
    f[Y] = 0
    f[PC] = "D2"


@step(
    "dtor",
    "D2",
    "inplace_stop_source::remove_callback | cb->completion.load(std::memory_order_acquire)",
    "load_acq",
    "while (cb->completion.load(acquire) == 0)",
)
def _(w, t, f, a):
    c = f[X]
    acc(w, c, "completion.load")
    if load(w, t, 1 + c) != 0:
        if not mut("H4_completion_load_relaxed"):  # HB mutant
            hb_acquire(w, t, 1 + c)
        cov("dtor.completion_seen")
        f[PC] = "Dz" if (mut("M25_handoff_before_wait") and f[Z] == 1) else "D4"
    else:
        f[PC] = "D2"
        call(w, "wait", wait_entry(), 1 + c, 0)  # source operation


@step("dtor", "D4", FINAL_HANDOFF_REF, "local", "final handoff: lock()")
def _(w, t, f, a):
    if mut("M5_no_final_handoff"):
        f[PC] = "Dz"
        return
    f[PC] = "D5"
    call(w, "lock", "L0")


@step("dtor", "D5", FINAL_HANDOFF_REF, "local", "final handoff: unlock(remove_handoff, lock())")
def _(w, t, f, a):
    st = f[RV]
    f[RV] = 0
    cov("dtor.handoff")
    if mut("M25_handoff_before_wait") and f[Z] == 0:  # Mutant: handoff, then wait
        f[Z] = 1
        f[PC] = "D2"
    else:
        f[PC] = "Dz"
    call_unlock(w, st)


@step(
    "dtor",
    "Dz",
    "inplace_stop_source::remove_callback | unlock(stop_site::remove_completed, state);",
    "local",
    "destructor returns: callback function and node storage are reclaimed",
)
def _(w, t, f, a):
    c = f[X]
    e = w.exe[c]
    if e != -1 and e != t:
        viol(w, "destructor returned on another thread while the callback is executing")
    if w.inv[c] == 1 and e == -1:
        hb_access(w, t, L_UD(c), False, "callback side effects (read after destruction)")
    hb_access(w, t, L_FN(c), True, "callback function object / invoke_fn (destroyed)")
    w.freed = setx(w.freed, c, 1)
    ret(w)


# ================================================================ helpers for scripts
@step("lu", "K0", "model-only: script, test hook, or mutant continuation", "local", "lock()")
def _(w, t, f, a):
    f[PC] = "K1"
    call(w, "lock", "L0")


@step(
    "lu",
    "K1",
    "model-only: script, test hook, or mutant continuation",
    "plain",
    "critical section body, then unlock(known)",
)
def _(w, t, f, a):
    own(w, t, "critical section")
    st = f[RV]
    f[RV] = 0
    f[PC] = "K2"
    call_unlock(w, st)


@step("lu", "K2", "model-only: script, test hook, or mutant continuation", "local", "done")
def _(w, t, f, a):
    ret(w)


@step(
    "lh",
    "H0",
    "model-only: script, test hook, or mutant continuation",
    "local",
    "lock() by a holder that then stays inside the critical section (test hook hold)",
)
def _(w, t, f, a):
    f[PC] = "H1"
    call(w, "lock", "L0")


@step(
    "lh",
    "H1",
    "model-only: script, test hook, or mutant continuation",
    "local",
    "holder stays in the critical section forever",
)
def _(w, t, f, a):
    f[RV] = 0
    ret(w)


@step(
    "sr",
    "Q0",
    "inplace_stop_source::stop_requested | state_.load(std::memory_order_acquire)",
    "load_acq",
    "stop_requested(): state_.load(acquire)",
    nalts=stale_alts,
)
def _(w, t, f, a):
    v = sload(w, t, a)
    if C.strict and (v & DEAD):
        w.abort = 1
        return
    observe_stop(w, v)
    if not mut("H6_stop_requested_relaxed"):  # HB mutant
        hb_acquire(w, t, 0)
    if v & S_:
        hb_access(w, t, L_PRE(), False, "data written before request_stop (read after stop_requested())")
    ret(w)


@step(
    "dsrc",
    "X0",
    "inplace_stop_source::~inplace_stop_source | state_.exchange(kDestroyed, std::memory_order_release)",
    "rmw",
    "strict ~inplace_stop_source: state_.exchange(kDestroyed, release)",
)
def _(w, t, f, a):
    prev = w.word
    write(w, t, 0, DEAD)
    if prev & W_:
        f[PC] = "X1"
        call_notify(w, 0, 1)  # source operation
    else:
        ret(w)


@step("dsrc", "X1", "inplace_stop_source::~inplace_stop_source | state_.notify_all();", "local", "destructor returns")
def _(w, t, f, a):
    ret(w)


def script_guard(w, t, f):
    ops = SCRIPTS[f[X]]
    i = f[Y]
    if i < len(ops) and ops[i][0] == "dtor":
        c = ops[i][1]
        # handed over only after construction; Model: a callback may also
        # destroy itself from inside its own invocation, including the inline
        # invocation in its constructor (inplace_stop_source protocol).
        return w.ctord[c] == 1 or w.exe[c] == t
    return True


@step(
    "script",
    "S",
    "model-only: script, test hook, or mutant continuation",
    "local",
    "next scripted operation",
    guard=script_guard,
)
def _(w, t, f, a):
    ops = SCRIPTS[f[X]]
    i = f[Y]
    f[RV] = 0
    if i == len(ops):
        ret(w)
        return
    f[Y] = i + 1
    op = ops[i]
    k = op[0]
    if k == "lu":
        call(w, "lu", "K0")
    elif k == "req":
        call(w, "req", "R0")
    elif k == "ctor":
        call(w, "ctor", "A0", op[1])
    elif k == "dtor":
        if C.hb and w.cclk[op[1]] is not None:
            w.vc = setx(w.vc, t, vjoin(w.vc[t], w.cclk[op[1]]))
        call(w, "dtor", "D0", op[1])
    elif k == "sr":
        call(w, "sr", "Q0")
    elif k == "lockhold":
        call(w, "lh", "H0")
    elif k == "destroy":
        call(w, "dsrc", "X0")
    else:
        raise ValueError(op)


SCRIPTS = []


# ================================================================ engine
def at_of(st, f):
    return st.at(f) if callable(st.at) else st.at


def enabled(w, t, st, f):
    if st.guard and not st.guard(w, t, f):
        return False
    b = w.bufs[t]
    if b:
        at = at_of(st, f)
        if at in ("rmw", "sys"):
            return False
        if at == "store_sc" and not bufferable("sc"):
            return False
        if at == "load_acq" and C.mem == "arm":
            for e in b:
                if e[2]:
                    return False  # STLR ... LDAR stay in program order
    return True


def run_thread(w, t, st, f, a):
    """Execute one step of thread t (alternative a) plus chained local steps."""
    w.cur = [list(fr) for fr in w.thr[t]]
    f = w.cur[-1]
    COV.add((f[RID], f[PC]))
    st.fn(w, t, f, a)
    while w.cur and not w.v and not w.abort:
        f = w.cur[-1]
        s2 = STEPS[(f[RID], f[PC])]
        if not s2.local or (s2.guard and not s2.guard(w, t, f)):
            break
        COV.add((f[RID], f[PC]))
        s2.fn(w, t, f, 0)
    w.thr = setx(w.thr, t, tuple(tuple(fr) for fr in w.cur))
    w.cur = None
    normalize_hist(w)


def successors(k):
    """Return [(meta, World)] for state key k."""
    w0 = World.of(k)
    out = []
    if w0.abort:
        return out
    for t in range(NT):
        if w0.bufs[t]:
            w = World.of(k)
            (obj, val, _kind) = w.bufs[t][0]
            w.bufs = setx(w.bufs, t, w.bufs[t][1:])
            write(w, t, obj, val)
            out.append(((t, "flush", "", 0), w))
        th = w0.thr[t]
        if not th:
            continue
        if w0.blk[t] != -1:
            if C.spurious:
                w = World.of(k)
                w.blk = setx(w.blk, t, -1)
                out.append(((t, "spurious-wake", "", 0), w))
            continue
        f = th[-1]
        st = STEPS[(f[RID], f[PC])]
        if not enabled(w0, t, st, f):
            continue
        n = st.nalts(w0, t, f) if st.nalts else 1
        for a in range(n):
            w = World.of(k)
            run_thread(w, t, st, f, a)
            out.append(((t, f[RID], f[PC], a), w))
    return out


def check_state(w):
    """Per-state invariants (lock word invariants of inplace_stop_source protocol)."""
    if C.precond_violation:
        return
    if ((w.word & L_) != 0) != (w.owner != -1):
        viol(w, "mutual exclusion: lock bit and owner disagree")
    if C.check_inv and (w.word & W_) and not (w.word & L_):
        viol(
            w,
            (
                "lock word invariant: kLockWaiters set on an unlocked word [source: "
                "inplace_stop_source::lock | state_.compare_exchange_weak(state, state | kLocked,]"
            ),
        )


def check_terminal(w):
    """Called on states without successors."""
    if w.abort:
        return
    unfinished = [t for t in range(NT) if w.thr[t]]
    if unfinished:
        blocked = [t for t in unfinished if w.blk[t] != -1]
        if blocked:
            objs = sorted({("state_" if (w.blk[t] >> 8) == 0 else "completion") for t in blocked})
            viol(w, "lost wakeup / deadlock: terminal state with thread(s) blocked in wait on " + "+".join(objs))
        else:
            viol(w, "deadlock: terminal state with unfinished threads")
        return
    if C.precond_violation:
        return
    if (w.word & L_) or (C.check_inv and (w.word & W_)):
        viol(w, "final state: lock word not released")
    if any(w.cnt):
        viol(w, "final state: library waiter count not zero")
    if w.winner != -1 and w.rtrue != 1:
        viol(w, "request_stop: winner did not return true exactly once")
    if w.winner == -1 and C.has_req:
        viol(w, "request_stop: nobody won")
    for c in range(C.ncb):
        if w.assoc[c] == 1 and not w.freed[c] and (w.word & S_) and w.inv[c] != 1:
            viol(w, "registered live callback not invoked by request_stop")
        if w.assoc[c] == 2 and w.inv[c] != 1:
            viol(w, "unassociated callback not invoked inline")
        if w.assoc[c] == 3 and w.inv[c] != 1:
            viol(w, "callback registered after the stop request was never invoked")
    for c in w.lst:
        if w.freed[c]:
            viol(w, "final state: freed node still linked")


def initial():
    global NT, SCRIPTS
    NT = len(C.threads)
    SCRIPTS = [list(s) for s in C.threads] + [list(C.bodies.get(c, [])) for c in range(C.ncb)]
    C.has_req = any(op[0] == "req" for s in C.threads for op in s)
    n = C.ncb
    w = World.__new__(World)
    w.word = 0
    w.owner = -1
    w.lst = tuple(C.reg)
    w.status = (-1,) * n
    w.reqthr = (-1,) * n
    w.comp = (0,) * n
    w.inv = (0,) * n
    w.exe = (-1,) * n
    w.freed = (0,) * n
    w.assoc = tuple(1 if c in C.reg else 0 for c in range(n))
    w.ctord = tuple(1 if c in C.reg else 0 for c in range(n))
    w.rec = (0,) * NT
    w.recl = (0,) * NT
    w.blk = (-1,) * NT
    w.cnt = (0,) * (1 + n)
    w.bufs = ((),) * NT
    w.winner = -1
    w.rtrue = 0
    w.stopseen = 0
    w.early = (-1,) * NT
    w.abort = 0
    if C.stale:
        if C.mem != "sc":
            raise ValueError("stale reads are modeled on top of SC only")
        w.hist = (0,)
        w.view = (0,) * NT
    else:
        w.hist = ()
        w.view = ()
    if C.hb:
        if C.mem != "sc" or C.stale:
            raise ValueError("happens-before tracking is modeled on SC interleavings only")
        w.vc = tuple(tuple(1 if u == t else 0 for u in range(NT)) for t in range(NT))
        w.sync = (None,) * (1 + n)
        w.accs = (None,) * (2 + 3 * n + NT)
        w.cclk = (None,) * n
    else:
        w.vc = ()
        w.sync = ()
        w.accs = ()
        w.cclk = ()
    w.thr = tuple((("script", "S", 0, t, 0, 0),) for t in range(NT))
    w.v = None
    w.cur = None
    # run the initial local dispatch of every thread (no shared effect)
    for t in range(NT):
        f = w.thr[t][-1]
        st = STEPS[(f[RID], f[PC])]
        if enabled(w, t, st, f):
            run_thread(w, t, st, f, 0)
    return w


def describe_initial(w):
    return describe(None, w)


def describe(meta, w):
    t = meta[0] if meta else None
    if meta is None:
        head = "(initial)"
    elif meta[1] in ("flush", "spurious-wake"):
        head = "T%d %s" % (t, meta[1])
    else:
        s = STEPS[(meta[1], meta[2])]
        from pathlib import Path
        from engine.source_lock import resolve

        reference = resolve(
            (Path(__file__).resolve().parents[2] / "backport/cpp26/execution/stop_token.hpp").read_text(), s.lines
        )
        head = "T%d %s.%s%s [%s] %s" % (t, meta[1], meta[2], ("/alt%d" % meta[3]) if meta[3] else "", reference, s.desc)
    blk = ",".join(
        "T%d:%s" % (u, "state_" if (b >> 8) == 0 else "comp%d" % ((b >> 8) - 1)) for u, b in enumerate(w.blk) if b >= 0
    )
    s = "%-110s | word=%s%s%s%s owner=%s list=%s comp=%s cnt=%s blocked=[%s] buf=%s rec=%s freed=%s" % (
        head,
        "D" if w.word & DEAD else "",
        "W" if w.word & W_ else "-",
        "L" if w.word & L_ else "-",
        "S" if w.word & S_ else "-",
        w.owner,
        list(w.lst),
        list(w.comp),
        list(w.cnt),
        blk,
        [len(b) for b in w.bufs],
        list(w.rec),
        list(w.freed),
    )
    if w.hist:
        s += " hist=%s view=%s" % (list(w.hist), list(w.view))
    return s


def cfg_summary(c):
    return (
        "threads=%s ncb=%d bodies=%s reg=%s Y=%d mem=%s lib=%s elig=%s spurious=%s "
        "weakfail=%s strict=%s inv=%s stale=%s muts=%s"
    ) % (
        c.threads,
        c.ncb,
        c.bodies,
        list(c.reg),
        c.Y,
        c.mem,
        c.lib,
        c.elig,
        c.spurious,
        c.weakfail,
        c.strict,
        c.check_inv,
        c.stale,
        sorted(c.muts),
    )


def step_table():
    rows = []
    for (rid, pc), s in STEPS.items():
        at = (
            s.at
            if not callable(s.at)
            else {
                "N0": "load (seq_cst | plain)",
                "R8": "store (seq_cst)",
                "G": "load (relaxed for state_, acquire for completion)",
            }[pc]
        )
        rows.append((rid, pc, s.lines, at, s.desc))
    return rows


def configure(cfg):
    """Reset per-task globals; workers configure themselves on every task."""
    global C
    C = cfg
    COV.clear()
    STATS["maxbuf"] = 0


def key(world):
    return world.key()


state = World.of


def state_violations(world):
    check_state(world)
    return world.v or ()


def terminal_violations(world):
    check_terminal(world)
    return world.v or ()


def is_blocked(encoded):
    return any(b != -1 for b in encoded[13])


def is_aborted(world):
    return bool(world.abort)


def metrics():
    return {"max_store_buffer": STATS["maxbuf"]}


def coverage():
    return sorted("%s.%s" % c if isinstance(c, tuple) else c for c in COV)

"""Explicit-state J/S/F model. Immutable fields make state interning deterministic.

Observable atomic boundaries and unlocks remain interleavable; the mutex
excludes other lockers throughout each critical section. Stop sources implement
the contract documented in README.md.
Mutants change one source-level protocol choice, never a property predicate.
"""

import copy
from pathlib import Path
from types import SimpleNamespace
from engine.memory import Memory, put, change, merge, synchronization, merge_sync, empty_sync

SOURCE = Path(__file__).resolve().parents[2] / 'backport/cpp26/execution/counting_scope.hpp'
C = None
COV = set()


def both(function, fragment):
    return ' || '.join(kind + '::' + function.replace('$', kind) + ' | ' + fragment
                       for kind in ('simple_counting_scope', 'counting_scope'))


REFS = {
    'associate': both('__try_associate', '__count_.fetch_add(1, std::memory_order_relaxed);'),
    'close': both('close', 'std::lock_guard lk{__mtx_};'),
    'close_store': both('close', '__state_.store('),
    'release_holder': both('scope_association::__release', 'scope->__decrement();'),
    'join': both('__start_join', 'joiner->__next = __joiners_;'),
    'release': both('__decrement', '__count_.fetch_sub(1, std::memory_order_acq_rel)'),
    'destroy_scope': both('~$', 'state = __state_.load(std::memory_order_relaxed);'),
    'count': both('__count', 'return __count_.load(std::memory_order_acquire);'),
    'closed': both('__is_closed', '__state_.load(std::memory_order_acquire)'),
    'drain': '::__complete_joiners | head = head->__next;',
    'drain_late': 'model-only; call site ::__complete_joiners | current->__complete(current);',
    'start_tail': '__join_sender::__op::start | return;',
    'schedule': '__join_sender::__op::__complete_join | self->__schedule_op',
    'receive': '__join_scheduler_receiver::set_value | std::execution::set_value(std::move(*__rcvr));',
    'request': 'counting_scope::request_stop | __stop_source_.request_stop();',
    'request_loop': 'model-only; call site counting_scope::request_stop | __stop_source_.request_stop();',
    'request_end': 'model-only; call site counting_scope::request_stop | __stop_source_.request_stop();',
    'construct': 'model-only: implicit Fn/member initialization in callback_type constructor',
    'prepare': '__fused_stop_token::callback_type::__prepare_input | if (token.stop_requested())',
    'prepare_downstream': '__fused_stop_token::stop_requested | __downstream.stop_requested();',
    'register': 'model-only; call site __fused_stop_token::callback_type::__prepare_input | return token.__input;',
    'invoke': '__fused_stop_token::callback_type::__invoke::operator() | owner->__invoked.exchange(true, std::memory_order_relaxed)',
    'fn': '__fused_stop_token::callback_type::__invoke::operator() | std::forward<Fn>(owner->__fn)();',
    'deregister': 'model-only: implicit reverse member destruction; abstract stop callback contract',
}
REFS.update({
    'after_close': '::__scope_after_close | return __scope_state::closed;',
    'after_association': '::__scope_after_association | return state == __scope_state::unused ? __scope_state::open : state;',
    'after_join_start': '::__scope_after_join_start | std::terminate();',
    'is_joining': '::__scope_is_joining | return state == __scope_state::open_and_joining',
    'rejects': '::__scope_rejects_association | return state != __scope_state::unused',
    'try_result': '::__scope_try_associate | if (count >= limit || __scope_rejects_association(state))',
})
REFS.update({
    'unlock': 'model-only: lock_guard release, last mutex access',
    'associate_read': both('__try_associate', 'const auto next ='),
    'increment': REFS['associate'],
    'close_read': both('close', '__state_.load(std::memory_order_relaxed)'),
    'state_store': both('__decrement', '__state_.store(') + ' || ' + REFS['close_store'] +
                   ' || ' + both('__try_associate', '__state_.store(') +
                   ' || ' + both('__start_join', '__scope_state::joined,') +
                   ' || ' + both('__start_join', '__forge_counting_scope::__scope_after_join_start('),
    'join_count': both('__start_join', '__count_.load(std::memory_order_acquire)'),
    'join_state': both('__start_join', '__state_.load(std::memory_order_relaxed)'),
    'list_read': REFS['join'],
    'list_push': both('__start_join', '__joiners_ = joiner;'),
    'join_return': 'model-only: saved __start_join result',
    'decrement': REFS['release'],
    'decrement_state': both('__decrement', 'auto state = __state_.load(std::memory_order_relaxed);'),
    'list_detach': both('__decrement', 'ready = std::exchange(__joiners_, nullptr);'),
    'list_clear': both('__decrement', 'ready = std::exchange(__joiners_, nullptr);'),
    'association_forward': both('scope_association::try_associate', 'return __scope_->__try_associate();'),
    'token_forward': both('scope_token::try_associate', 'return __scope_->__try_associate();'),
    'destructor_read': REFS['destroy_scope'],
    'scope_dead': 'model-only: destructor check and implicit member teardown',
    'wait_zero': REFS['count'],
    'wait_closed': REFS['closed'],
})
# Model-only scripts and abstract library returns have no C++ body of their own.
ACTIONS = ('associate close join release destroy_scope count closed drain drain_late start_tail schedule receive '
           'request request_loop request_end construct prepare register invoke fn deregister '
           'ctor_end destroy_cb cb_dead fn_end invoke_end holder_done release_holder '
           'write destroy_join token signal await wait_ctor wait_join wait_all wait_idle '
           'drop_fn plain_store prepare_downstream').split()
ACTIONS += ('unlock associate_read increment close_read state_store join_count join_state '
            'list_read list_push join_return decrement decrement_state list_detach list_clear '
            'destructor_read scope_dead wait_zero wait_closed').split()
STEPS = {a: SimpleNamespace(lines=REFS.get(a, 'model-only: finite script / library contract'), desc=a)
         for a in ACTIONS}


class Cfg:
    def __init__(self, **kw):
        self.name = 'cfg'
        self.scope_kind = 'counting_scope'
        self.scheduler = 'inline'
        self.threads = ()
        self.nholders = 1
        self.njoiners = 1
        self.ncallbacks = 0
        self.initial_holders = (0,)
        self.initial_callbacks = ()
        self.inputs = ()
        self.bodies = {}
        self.continuations = {}
        self.stopped = (False, False)
        self.strict = False
        self.keep_association = False
        self.hb = False
        self.stale = False
        self.mut = ''
        self.control = ''
        self.limit = None
        self.required_coverage = ()
        self.__dict__.update(kw)


# All persistent fields contain only immutable tuples/scalars.
FIELDS = ('scope count alive owner joiners holders ever released holder_alive started completed op_alive links '
          'cb_alive constructed flag calls running regs active obligated fn_thread ctor_thread pre_stopped '
          'stop requests source_alive threads queue signals abort errors vc mutex accesses construction handoff '
          'reg_clocks end_clocks request_clocks hist views retained commitments').split()


class World:
    pass


def key(w):
    return tuple(getattr(w, f) for f in FIELDS)


def state(k):
    w = World()
    for f, v in zip(FIELDS, k):
        setattr(w, f, v)
    return w


def error(w, message):
    w.errors += (message,)


def detect(w, message):
    if C.strict:
        w.abort = message
        COV.add('strict.' + message)
    else:
        error(w, message)


def scope_access(w, t):
    if not w.alive:
        detect(w, 'J4/S1: scope access after destruction')
    access(w, t, 'scope storage')


def cb_access(w, t, c):
    if not w.cb_alive[c]:
        error(w, 'F4: callback storage access after destruction')


def op_access(w, t, j):
    if not w.op_alive[j] or w.completed[j]:
        error(w, 'J4: operation access after receiver completion')


def unlocked(action):
    return C.mut == {'release': 'J_decrement_unlocked', 'join': 'J_join_unlocked',
                     'close': 'J_close_unlocked', 'destroy_scope': 'J_destructor_unlocked'}.get(action)


def mutex_enter(w, t, action):
    outside = unlocked(action)
    if not outside:
        acquire(w, t, w.mutex)
        w.owner = t
    scope_access(w, t)
    return outside


def prepend(w, t, *ops):
    change(w, 'threads', t, tuple(ops) + w.threads[t])


def initial():
    n = len(C.threads) + 1  # Last thread is the queued start scheduler.
    z = (0,) * n
    w = World()
    w.scope = 'open' if C.initial_holders else 'unused'
    w.count = len(C.initial_holders)
    w.alive, w.owner, w.joiners = True, -1, ()
    w.holders = tuple(h in C.initial_holders for h in range(C.nholders))
    w.ever, w.released = w.holders, (False,) * C.nholders
    w.holder_alive = (True,) * C.nholders
    w.started = w.completed = (0,) * C.njoiners
    w.op_alive, w.links = (True,) * C.njoiners, (-1,) * C.njoiners
    nc = C.ncallbacks
    w.cb_alive = (True,) * nc
    w.constructed = tuple(c in C.initial_callbacks for c in range(nc))
    w.flag, w.calls, w.running = (False,) * nc, (0,) * nc, (-1,) * nc
    w.regs = tuple(1 if c in C.initial_callbacks and s < len(C.inputs[c]) else 0
                   for c in range(nc) for s in range(2))
    w.active = (-1,) * (nc * 2)
    w.obligated = (False,) * nc
    w.fn_thread, w.ctor_thread = (-1,) * nc, (-1,) * nc
    w.pre_stopped = (False,) * nc
    w.stop, w.requests, w.source_alive = C.stopped, (0, 0), (True, True)
    w.threads = tuple(tuple(s) for s in C.threads) + ((),)
    w.queue, w.signals, w.abort, w.errors = (), (), '', ()
    w.vc = tuple(tuple(1 if u == t else 0 for u in range(n)) for t in range(n))
    zero_sync = (z, (0, 0, 0, 0))
    w.mutex, w.accesses = zero_sync, ()
    w.construction = (z,) * nc
    w.handoff = (zero_sync,) * nc
    w.reg_clocks = w.end_clocks = (zero_sync,) * (nc * 2)
    w.request_clocks = (zero_sync, zero_sync)
    w.hist = tuple(((v, z, zero_sync),) for v in (w.scope, w.count, *w.stop))
    w.retained = ((), ())
    w.commitments = ()  # (callback identity, exchange winner, Fn entries).
    w.views = ((0, 0, 0, 0),) * n
    return w


def enabled(w, t, op):
    a, *args = op
    if a == 'release' and not w.holders[args[0]]:
        return True
    if a in ('associate', 'close', 'join', 'release', 'destroy_scope') and not unlocked(a) and w.owner != -1:
        return False
    if a == 'wait_zero':
        return w.count == 0
    if a == 'wait_closed':
        return w.scope not in ('unused', 'open', 'open_and_joining')
    if a == 'await':
        return args[0] in dict(w.signals)
    if a == 'wait_ctor':
        return w.constructed[args[0]]
    if a == 'wait_join':
        return bool(w.completed[args[0]])
    if a == 'wait_all':
        return all(w.completed[j] for j in range(C.njoiners))
    if a == 'wait_idle':
        return not any(w.requests)
    if a in ('destroy_cb', 'holder_done'):
        c = args[0]
        return w.constructed[c]
    if a == 'deregister':
        c, s = args
        u = w.active[2*c+s]
        if u not in (-1, t):
            COV.add('deregister.wait')
        return u in (-1, t)
    return True


def execute(w, t, op, alt=0):
    a, *args = op
    COV.add(a)
    if a in ('signal', 'await'):
        signals = dict(w.signals)
        if a == 'signal':
            signals[args[0]] = synchronization(w, t)
            w.signals = tuple(sorted(signals.items()))
        else:
            acquire(w, t, signals[args[0]])
    elif a in ('wait_ctor', 'wait_join', 'wait_all', 'wait_idle'):
        # These waits are script-level release/acquire handoffs.
        if a == 'wait_ctor':
            acquire(w, t, w.handoff[args[0]])
        elif a == 'wait_join':
            acquire(w, t, dict(w.signals)['join.' + str(args[0])])
        elif a == 'wait_all':
            for name, clock in w.signals:
                if name.startswith('join.'):
                    acquire(w, t, clock)
        else:
            for clock in w.request_clocks:
                acquire(w, t, clock)
    elif a in ('associate', 'close', 'join', 'release', 'destroy_scope'):
        if a == 'release' and not w.holders[args[0]]:
            return  # Disengaged scope_association::__release never touches scope.
        outside = mutex_enter(w, t, a)
        tail = () if outside else (('unlock',),)
        if a == 'associate':
            prepend(w, t, ('associate_read', args[0]), *tail)
        elif a == 'close':
            prepend(w, t, ('close_read',), *tail)
        elif a == 'join':
            j = args[0]
            op_access(w, t, j)
            change(w, 'started', j, w.started[j] + 1)
            prepend(w, t, ('join_count', j), *tail, ('join_return', j))
        elif a == 'release':
            h = args[0]
            if w.holders[h]:
                prepend(w, t, ('decrement', h), *tail)
            else:
                prepend(w, t, *tail)
        else:
            prepend(w, t, ('destructor_read',), *tail)
    elif a == 'unlock':
        # The release is the last scope/mutex access on this thread.
        scope_access(w, t)
        w.mutex, w.owner = synchronization(w, t), -1
    elif a == 'associate_read':
        scope_access(w, t)
        h = args[0]
        rejected = w.scope not in ('unused', 'open', 'open_and_joining')
        accept = (not rejected or C.mut == 'J_accept_joined') and (C.limit is None or w.count < C.limit)
        COV.add('associate.accept' if accept else 'associate.reject')
        if accept:
            if rejected:
                error(w, 'J3: closed or joined accepted association')
            if w.holders[h] or w.ever[h]:
                error(w, 'script: holder identity reused')
            target = 'open' if w.scope == 'unused' else w.scope
            prepend(w, t, ('state_store', target), ('increment', h))
    elif a == 'increment':
        scope_access(w, t)
        h = args[0]
        change(w, 'holders', h, True)
        change(w, 'ever', h, True)
        w.count += 1
        publish(w, t, 1, w.count, 'relaxed', rmw=True)
    elif a == 'close_read':
        scope_access(w, t)
        target = dict(unused='unused_and_closed', open='closed', open_and_joining='closed_and_joining').get(w.scope, w.scope)
        prepend(w, t, ('state_store', target))
    elif a == 'state_store':
        scope_access(w, t)
        w.scope = args[0]
        COV.add('state.' + w.scope)
        publish(w, t, 0, w.scope)
    elif a == 'join_count':
        scope_access(w, t)
        j = args[0]
        count = load(w, t, 1, len(w.hist[1])-1)
        if count == 0:
            prepend(w, t, ('state_store', 'joined'))
            # Return value is carried on the stack, never re-read from scope.
            w.threads = put(w.threads, t, tuple(('receive', j) if op == ('join_return', j) else op
                                                for op in w.threads[t]))
            COV.add('join.empty')
        else:
            prepend(w, t, ('join_state', j))
            COV.add('join.register')
    elif a == 'join_state':
        scope_access(w, t)
        j = args[0]
        if w.scope not in ('open', 'closed', 'open_and_joining', 'closed_and_joining'):
            error(w, 'J3: scope_after_join_start terminate default')
        target = w.scope if C.mut == 'J_no_joining' else (
            'closed_and_joining' if w.scope.startswith('closed') else 'open_and_joining')
        prepend(w, t, ('state_store', target), ('list_read', j))
    elif a == 'list_read':
        scope_access(w, t)
        j = args[0]
        access(w, t, 'joiner list')
        access(w, t, 'link.' + str(j), True)
        change(w, 'links', j, w.joiners[0] if w.joiners else -1)
        # Merge only while both list participants respect the mutex. Either
        # unlocked participant makes both read/write pairs interleavable.
        if unlocked('join') or unlocked('release'):
            prepend(w, t, ('list_push', j))
        else:
            execute(w, t, ('list_push', j))
    elif a == 'list_push':
        scope_access(w, t)
        access(w, t, 'joiner list', True)
        j = args[0]
        chain, head = (), w.links[j]
        while head != -1 and head not in chain:
            chain += (head,)
            head = w.links[head]
        w.joiners = (j,) + chain
    elif a == 'join_return':
        prepend(w, t, ('start_tail', args[0]))
    elif a == 'decrement':
        scope_access(w, t)
        h = args[0]
        access(w, t, 'holder.' + str(h), True)
        change(w, 'holders', h, False)
        change(w, 'released', h, True)
        old = w.count
        w.count -= 1
        publish(w, t, 1, w.count, 'acq_rel', rmw=True)
        if old == (2 if C.mut == 'J_wrong_decrement' else 1):
            prepend(w, t, ('decrement_state',))
    elif a == 'decrement_state':
        scope_access(w, t)
        if w.scope.endswith('_and_joining'):
            prepend(w, t, ('state_store', 'joined'), ('list_detach',))
    elif a == 'list_detach':
        scope_access(w, t)
        access(w, t, 'joiner list')
        head = w.joiners[0] if w.joiners else -1
        if unlocked('release') or unlocked('join'):
            prepend(w, t, ('list_clear', head))
        else:
            execute(w, t, ('list_clear', head))
    elif a == 'list_clear':
        scope_access(w, t)
        access(w, t, 'joiner list', True)
        w.joiners = ()
        head = args[0]
        if C.mut == 'J_complete_locked':
            prepend(w, t, ('drain', head))
        else:
            # Detachment is the last body access, but unlock remains separate.
            tail = w.threads[t]
            if tail and tail[0] == ('unlock',):
                change(w, 'threads', t, tail[:1] + (('drain', head),) + tail[1:])
            else:
                prepend(w, t, ('drain', head))
    elif a == 'destructor_read':
        scope_access(w, t)
        allowed = w.scope in ('joined', 'unused', 'unused_and_closed')
        COV.add('destructor.accept' if allowed else 'destructor.terminate')
        tail = w.threads[t]
        finish = ('scope_dead', allowed)
        if tail and tail[0] == ('unlock',):
            change(w, 'threads', t, tail[:1] + (finish,) + tail[1:])
        else:
            prepend(w, t, finish)
    elif a == 'scope_dead':
        if not args[0]:
            detect(w, 'J5: destructor terminates')
        else:
            if C.scope_kind == 'counting_scope':
                if any(w.regs[2*c+i] in (1, 2, 3) for c in range(C.ncallbacks)
                       for i, src in enumerate(C.inputs[c]) if src == 0):
                    detect(w, 'S1: scope destroyed with registration')
                access(w, t, 'source.0', True)
                w.source_alive = (False, w.source_alive[1])
            access(w, t, 'scope storage', True)
            w.alive = False
    elif a in ('count', 'closed', 'token', 'wait_zero', 'wait_closed'):
        scope_access(w, t)
        if a != 'token':
            obj = 1 if a in ('count', 'wait_zero') else 0
            value = load(w, t, obj, alt)
            COV.add('observer.' + ({'wait_zero': 'count', 'wait_closed': 'closed'}.get(a, a)) + '.' + str(value))
    elif a == 'drain':
        j = args[0]
        if j != -1:
            op_access(w, t, j)
            access(w, t, 'link.' + str(j), True)
            nxt = w.links[j]
            change(w, 'links', j, -1)
            tail = ('drain_late', j) if C.mut == 'J_read_next_late' else ('drain', nxt)
            prepend(w, t, ('schedule', j), tail)
    elif a == 'drain_late':
        j = args[0]
        op_access(w, t, j)
        prepend(w, t, ('drain', w.links[j]))
    elif a == 'start_tail':
        if C.mut == 'J_touch_start':
            op_access(w, t, args[0])
    elif a == 'schedule':
        j = args[0]
        op_access(w, t, j)
        if C.scheduler == 'inline':
            prepend(w, t, ('receive', j))
        else:
            w.queue += ((j, empty_sync(w) if C.control == 'no_queue_release' else synchronization(w, t)),)
    elif a == 'receive':
        j = args[0]
        op_access(w, t, j)
        if any(w.requests):
            COV.add('join.inside_request')
        if w.scope != 'joined' or w.count != 0 or any(w.holders):
            error(w, 'J1: premature join completion')
        for h in range(C.nholders):
            if w.ever[h] and not w.released[h]:
                error(w, 'J1: engaged association unreleased at completion')
            if w.released[h]:
                access(w, t, 'holder.' + str(h))
        change(w, 'completed', j, w.completed[j] + 1)
        signals = dict(w.signals)
        signals['join.' + str(j)] = synchronization(w, t)
        w.signals = tuple(sorted(signals.items()))
        prepend(w, t, *C.continuations.get(j, ()))
    elif a == 'destroy_join':
        change(w, 'op_alive', args[0], False)
    elif a == 'write':
        h = args[0]
        if not w.holder_alive[h]:
            error(w, 'J4: holder storage access after destruction')
        access(w, t, 'holder.' + str(h), True)
    elif a == 'release_holder':
        h = args[0]
        change(w, 'holder_alive', h, False)
        prepend(w, t, ('release', h))
    elif a == 'holder_done':
        c, h = args
        sequence = (('destroy_cb', c), ('release_holder', h))
        if C.mut == 'S_release_first':
            sequence = tuple(reversed(sequence))
        prepend(w, t, *sequence)
    elif a == 'request':
        s = args[0]
        access(w, t, 'source.' + str(s))
        if not w.source_alive[s]:
            detect(w, 'S1: stop source access after destruction')
        change(w, 'requests', s, w.requests[s] + 1)
        if not w.stop[s]:
            change(w, 'stop', s, True)
            publish(w, t, 2+s, True)
            change(w, 'retained', s, tuple(r for r in range(len(w.regs))
                if w.regs[r] == 1 and C.inputs[r//2][r%2] == s))
            if C.control == 'literal_f3':
                for c in range(C.ncallbacks):
                    if any(src == s and w.regs[2*c+slot] == 1 for slot, src in enumerate(C.inputs[c])):
                        change(w, 'obligated', c, True)
            prepend(w, t, ('request_loop', s), ('request_end', s, True))
        else:
            prepend(w, t, ('request_end', s, False))
    elif a == 'request_loop':
        s = args[0]
        access(w, t, 'source.' + str(s))
        if not w.source_alive[s]:
            detect(w, 'S1: request_stop still accessing destroyed source')
        pending = [(c, slot) for c in range(C.ncallbacks) for slot, src in enumerate(C.inputs[c])
                   if src == s and w.regs[2*c+slot] == 1]
        if pending:
            c, slot = pending[alt]
            if C.control == 'skip_retained':
                return
            r = 2*c+slot
            COV.add('request.select.%d.%d' % (c, slot))
            change(w, 'regs', r, 2)
            change(w, 'active', r, t)
            change(w, 'obligated', c, True)
            acquire(w, t, w.reg_clocks[r])
            prepend(w, t, ('invoke', c, slot), ('invoke_end', c, slot), ('request_loop', s))
    elif a == 'request_end':
        s, winning = args
        if winning:
            for r in w.retained[s]:
                c = r // 2
                if not w.calls[c] and (C.control == 'entry_before_return' or not committed(w, c)):
                    error(w, 'F3: retained registration lost delivery at winning request return')
            change(w, 'retained', s, ())
        access(w, t, 'source.' + str(s))
        if not w.source_alive[s]:
            detect(w, 'S1: request_stop return on destroyed source')
        change(w, 'requests', s, w.requests[s] - 1)
        # All request calls, including losing requests, publish their return.
        change(w, 'request_clocks', s, merge_sync(w.request_clocks[s], synchronization(w, t)))
    elif a == 'construct':
        c = args[0]
        access(w, t, 'Fn.' + str(c), True)
        change(w, 'construction', c, w.vc[t])
        change(w, 'ctor_thread', c, t)
        change(w, 'pre_stopped', c, any(w.stop[s] and all(x <= y for x, y in zip(w.hist[2+s][-1][1], w.vc[t]))
                                         for s in C.inputs[c]))
        if len(C.inputs[c]) == 2:
            actions = ((('register', c, 1), ('prepare', c), ('register', c, 0))
                       if C.mut == 'F_reverse_registration' else
                       (('prepare', c), ('register', c, 0), ('register', c, 1)))
        else:
            actions = (('register', c, 0),)
        prepend(w, t, *actions, ('ctor_end', c))
    elif a in ('prepare', 'prepare_downstream'):
        c = args[0]
        slot = 0 if a == 'prepare' else 1
        src = C.inputs[c][slot]
        if C.mut != 'F_no_prepare':
            stopped = load(w, t, 2+src, alt)
            if stopped:
                prepend(w, t, ('invoke', c, -1))
            elif slot == 0:
                prepend(w, t, ('prepare_downstream', c))
    elif a == 'register':
        c, s = args
        cb_access(w, t, c)
        src = C.inputs[c][s]
        access(w, t, 'source.' + str(src))
        if not w.source_alive[src]:
            detect(w, 'S1: registration on destroyed source')
        r = 2*c+s
        change(w, 'reg_clocks', r, synchronization(w, t))
        if w.stop[src]:
            change(w, 'regs', r, 5)  # Inline-only: never borrowed source storage.
            change(w, 'obligated', c, True)
            prepend(w, t, ('invoke', c, s))
            COV.add('register.inline')
        else:
            change(w, 'regs', r, 1)
            COV.add('register.pending')
    elif a == 'ctor_end':
        c = args[0]
        if w.pre_stopped[c] and (w.calls[c] != 1 or w.fn_thread[c] != t):
            error(w, 'F2: pre-stopped input did not invoke on constructing thread')
        change(w, 'constructed', c, True)
        change(w, 'handoff', c, synchronization(w, t))
    elif a == 'invoke':
        c, s = args
        cb_access(w, t, c)
        access(w, t, 'Fn.' + str(c))
        if not w.flag[c]:
            if C.mut == 'F_plain_arbitration':
                prepend(w, t, ('plain_store', c))
            else:
                change(w, 'flag', c, True)
                w.commitments += ((c, t, 0),)
                if C.control != 'drop_dispatch':
                    prepend(w, t, ('fn', c))
        COV.add('trampoline.exchange')
    elif a == 'plain_store':
        c = args[0]
        change(w, 'flag', c, True)
        w.commitments += ((c, t, 0),)
        prepend(w, t, ('fn', c))
    elif a == 'fn':
        c = args[0]
        cb_access(w, t, c)
        access(w, t, 'Fn.' + str(c))
        change(w, 'calls', c, w.calls[c] + 1)
        change(w, 'fn_thread', c, t)
        change(w, 'running', c, t)
        for i, (identity, winner, entries) in enumerate(w.commitments):
            if (identity, winner) == (c, t):
                w.commitments = put(w.commitments, i, (c, t, entries + 1))
                break
        prepend(w, t, *C.bodies.get(c, ()), ('fn_end', c))
    elif a == 'fn_end':
        # A stack-only return: no callback storage is touched after Fn.
        change(w, 'running', args[0], -1)
    elif a == 'invoke_end':
        c, s = args
        r = 2*c+s
        change(w, 'active', r, -1)
        if w.regs[r] == 2:
            change(w, 'regs', r, 3)
        change(w, 'end_clocks', r, synchronization(w, t))
    elif a == 'destroy_cb':
        c = args[0]
        acquire(w, t, w.handoff[c])
        order = tuple(reversed(range(len(C.inputs[c]))))
        if C.mut == 'F_reverse_registration':
            order = tuple(range(len(C.inputs[c])))
        actions = tuple(('deregister', c, s) for s in order)
        if C.mut == 'F_destroy_fn_first':
            actions = (('drop_fn', c),) + actions
        prepend(w, t, *actions, ('cb_dead', c))
    elif a == 'deregister':
        c, s = args
        r = 2*c+s
        src = C.inputs[c][s]
        if w.regs[r] == 5:
            COV.add('deregister.inline_only')
        if w.regs[r] != 5:
            access(w, t, 'source.' + str(src))
            if not w.source_alive[src]:
                detect(w, 'S1: deregistration after source destruction')
        # Cancel only this registration's retention, independently of selection.
        change(w, 'retained', src, tuple(x for x in w.retained[src] if x != r))
        if w.active[r] == -1:
            acquire(w, t, w.end_clocks[r])
        change(w, 'regs', r, 4)
        COV.add('deregister.self' if w.active[r] == t else 'deregister.external')
    elif a in ('drop_fn', 'cb_dead'):
        c = args[0]
        access(w, t, 'Fn.' + str(c), True)
        if w.running[c] not in (-1, t):
            error(w, 'F4: destructor returned while Fn running elsewhere')
        if w.obligated[c] and not w.calls[c]:
            error(w, 'F3: lost stop before deregistration')
        change(w, 'cb_alive', c, False)
    else:
        raise ValueError(op)


def successors(k):
    original = state(k)
    if original.abort or original.errors:
        return []
    out = []
    for t, script in enumerate(original.threads):
        if t == len(original.threads)-1 and original.queue:
            w = copy.copy(original)
            j, clock = w.queue[0]
            w.queue = w.queue[1:]
            acquire(w, t, clock)
            change(w, 'threads', t, (('receive', j),) + script)
            out.append(((t, 'dequeue', (j,)), w))
        if not script or not enabled(original, t, script[0]):
            continue
        op = script[0]
        if op[0] in ('count', 'closed', 'wait_zero', 'wait_closed'):
            alternatives = load_choices(original, t, 1 if op[0] in ('count', 'wait_zero') else 0)
            if op[0] == 'wait_zero':
                alternatives = tuple(i for i in alternatives if original.hist[1][i][0] == 0)
            elif op[0] == 'wait_closed':
                alternatives = tuple(i for i in alternatives if original.hist[0][i][0]
                                     not in ('unused', 'open', 'open_and_joining'))
        elif op[0] in ('prepare', 'prepare_downstream'):
            slot = 0 if op[0] == 'prepare' else 1
            alternatives = load_choices(original, t, 2+C.inputs[op[1]][slot])
        elif op[0] == 'request_loop':
            s = op[1]
            pending = sum(original.regs[2*c+slot] == 1
                for c in range(C.ncallbacks) for slot, src in enumerate(C.inputs[c]) if src == s)
            alternatives = range(max(1, pending))
            if pending > 1:
                COV.add('request.multiple_pending')
        else:
            alternatives = (0,)
        for alt in alternatives:
            w = copy.copy(original)
            change(w, 'threads', t, script[1:])
            if C.hb or C.stale:
                change(w, 'vc', t, put(w.vc[t], t, w.vc[t][t] + 1))
            execute(w, t, op, alt)
            out.append(((t, op[0], tuple(op[1:])), w))
    return out


def committed(w, c):
    return any(identity == c and entries == 0 and
               w.threads[winner][:1] == (('fn', c),)
               for identity, winner, entries in w.commitments)


def state_violations(w):
    if w.abort:
        return ()
    if w.count < 0:
        error(w, 'J3: count underflow')
    if w.count != sum(w.holders):
        error(w, 'J3: count differs from engaged associations')
    if any(w.completed) and w.scope != 'joined':
        error(w, 'J3: transition out of joined state')
    if w.scope == 'joined' and w.count != 0:
        error(w, 'J3: joined with nonzero count')
    if w.count > 0 and w.scope not in ('open', 'closed', 'open_and_joining', 'closed_and_joining'):
        error(w, 'J3: positive count in invalid state')
    if any(n > 1 for n in w.completed):
        error(w, 'J1: join completed more than once')
    if any(n > 1 for n in w.calls):
        error(w, 'F1/S2: Fn invoked more than once')
    for c, winner, entries in w.commitments:
        if entries == 0 and w.threads[winner][:1] != (('fn', c),):
            error(w, 'F3: commitment abandoned or protocol wait before Fn entry')
        if entries not in (0, 1):
            error(w, 'F3: commitment entered Fn more than once')
    return w.errors


def terminal_violations(w):
    if w.abort:
        return ()
    if any(w.threads) or w.queue or any(x != -1 for x in w.active):
        error(w, 'F4/J2: wait cycle or unfinished script')
    if not C.keep_association and any(s != d for s, d in zip(w.started, w.completed)):
        error(w, 'J2: lost started join')
    for c in range(C.ncallbacks):
        if w.obligated[c] and not w.calls[c]:
            error(w, 'F3/S2: registered callback lost stop')
    if any(entries != 1 for _, _, entries in w.commitments):
        error(w, 'F3: commitment did not enter Fn exactly once')
    return w.errors


def is_blocked(k):
    w = state(k)
    return any(script and not enabled(w, t, script[0]) for t, script in enumerate(w.threads))


def is_aborted(w):
    return bool(w.abort)


def metrics():
    return {'max_store_buffer': 0}


def coverage():
    if not C.mut and not C.control:
        missing = set(C.required_coverage) - COV
        assert not missing, 'missing required coverage: ' + repr(sorted(missing))
    return sorted(COV)


def step_table():
    extra = [("helper", a, ref, "pure function", a) for a, ref in REFS.items() if a not in STEPS]
    return extra + [('protocol', a, s.lines, 'atomic / protected plain / script', s.desc)
            for a, s in STEPS.items()]


def describe_initial(w):
    return 'scope=%s count=%s holders=%s stop=%s' % (w.scope, w.count, w.holders, w.stop)


def describe(meta, w):
    from engine.source_lock import resolve
    t, a, args = meta
    reference = resolve(SOURCE.read_text(), STEPS[a].lines) if a in STEPS else 'model-only: scheduler dequeue acquire'
    return ('T%d %s%s [%s] | scope=%s count=%d holders=%s joins=%s/%s regs=%s active=%s calls=%s commitments=%s alive=%s abort=%s'
            % (t, a, args, reference, w.scope, w.count, w.holders, w.started, w.completed,
               w.regs, w.active, w.calls, w.commitments, w.alive, w.abort))


def cfg_summary(cfg):
    return repr(cfg.__dict__)


def check_duplicates(text):
    """Fail closed on any token difference in every duplicated modeled routine."""
    import re
    from engine.source_lock import (analyze, definition_candidates, TOKEN,
                                    CPP_TOKEN, IDENTIFIER, PP_NUMBER, spliced)
    text = text.replace('\r\n', '\n')
    stream, _, pairs, *_ = analyze(text)
    # The fragment locator intentionally hides directives and literal contents.
    # Re-lex spelling here, retaining directives as well as opaque literals.
    code, offsets = spliced(text)
    masked = list(code)
    opaque = []
    for match in TOKEN.finditer(code):
        if re.fullmatch(IDENTIFIER + '|' + PP_NUMBER, match[0]):
            continue
        header = match.group('header') is not None
        start = match.start('header') if header else match.start()
        if header or not match[0].startswith(('//', '/*')):
            opaque.append((code[start:match.end()], offsets[start]))
        for i in range(start, match.end()):
            if masked[i] != '\n':
                masked[i] = ' '
    spelling = sorted([(m[0], offsets[m.start()]) for m in CPP_TOKEN.finditer(''.join(masked))]
                      + opaque, key=lambda item: item[1])
    for function in ('~$', 'close', '__try_associate', '__start_join', '__decrement',
                     'scope_association::__release', '__count', '__is_closed',
                     'scope_association::try_associate', 'scope_token::try_associate'):
        bodies = []
        for kind in ('simple_counting_scope', 'counting_scope'):
            name = kind + '::' + function.replace('$', kind)
            candidates = definition_candidates(text, name)
            if len(candidates) != 1:
                raise ValueError('duplicate protocol: expected exactly one definition for ' + name)
            body = next(iter(candidates))
            start, end = stream[body][1], stream[pairs[body]][1]
            bodies.append(tuple('$scope' if v == kind else v
                                for v, offset in spelling if start <= offset <= end))
        if bodies[0] != bodies[1]:
            raise ValueError('duplicate protocol bodies differ: ' + function)


def configure(cfg):
    global C, acquire, access, publish, load_choices, load
    C = cfg
    memory = Memory(cfg, detect)
    acquire, access, publish = memory.acquire, memory.access, memory.publish
    load_choices, load = memory.load_choices, memory.load
    COV.clear()

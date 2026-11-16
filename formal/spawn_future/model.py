"""Bounded spawn_future ownership and completion protocol.

Script expansion preserves atomic refcount operations, lock acquisition/unlock,
active arbitration and callback wait boundaries. Ghost obligations are created
by launch/start/producer completion, independently of their discharge paths.
"""
import copy
from pathlib import Path
from types import SimpleNamespace
from engine.memory import Memory, put, change, synchronization, empty_sync

SOURCE = Path(__file__).resolve().parents[2] / 'backport/cpp26/execution/spawn_future.hpp'
C = None
COV = set()
REFS = {
    'launch': '::__spawn_future | auto resource_owner =',
    'associate': '__shared_state::__shared_state | token.try_associate();',
    'start_producer': '__shared_state::__start | std::execution::start(__op);',
    'inner': '__inner_recv::set_value | auto state = __state->shared_from_this();',
    'complete': '__shared_state::__complete_with | if (__phase != __phase_t::done)',
    'complete_tail': '__shared_state::__complete_with | keepalive = std::move(__keepalive);',
    'request': '__shared_state::__request_stop | auto keepalive = this->shared_from_this();',
    'request_body': '__shared_state::__request_stop | __stop_source.request_stop();',
    'cancel': '__shared_state::__try_cancel | __request_stop();',
    'cancel_body': '__shared_state::__try_cancel | if (__phase == __phase_t::done)',
    'unconsumed': '__shared_state::__abandon_unconsumed | if (!__consumer_taken)',
    'consume': '__shared_state::__consume | if (__consumer_taken)',
    'detach': '__shared_state::__detach_consumer | __consumer_cb.reset();',
    'connect': '__sender::connect | auto consumer =',
    'owner_init': '__op::__op | __consumer_state->__owner = this;',
    'start': '__op::start | auto state = std::move(__state);',
    'install': '__consumer::__install_stop_callback | __state = state;',
    'query_stop': '__consumer::__install_stop_callback | if (token.stop_requested())',
    'register': '__consumer::__install_stop_callback | __stop_callback.emplace(',
    'deliver': '__consumer::__deliver | __active.exchange(false, std::memory_order_acq_rel)',
    'deliver_stopped': '__consumer::__deliver_stopped | __active.exchange(false, std::memory_order_acq_rel)',
    'deliver_error': '__consumer::__deliver_error | __active.exchange(false, std::memory_order_acq_rel)',
    'abandon': '__consumer::__abandon | __release_owner();',
    'abandon_active': '__consumer::__abandon | __active.exchange(false, std::memory_order_acq_rel)',
    'release_owner': '__consumer::__release_owner | std::lock_guard lk{__owner_mtx};',
    'owner_take': '__consumer::__release_owner | std::exchange(__owner, nullptr)',
    'owner_reset': '__consumer::__release_owner | owner->__consumer_state.reset();',
    'destroy_op': '__op::~__op | if (__state)',
    'op_take': '__op::~__op | consumer = std::move(__consumer_state);',
    'receiver': '__shared_state::__deliver_to | std::visit(',
    'resource_retain': '__allocation_resource::__retain | __refs.fetch_add(1, std::memory_order_relaxed);',
    'resource_release': '__allocation_resource::__release | __refs.fetch_sub(1, std::memory_order_acq_rel)',
    'resource_free': '__allocation_resource::__release | traits_t::deallocate(alloc, this, 1);',
    'state_free': 'model-only; call site ::__make_state | state_traits_t::allocate(state_alloc, 1);',
    'weak_lock': '__consumer::__stop_callback_fn::operator() | __state.lock()',
}
ACTIONS = ('''launch associate start_producer produce inner complete complete_body complete_tail keep_take
request request_body stop_source request_end request_return cancel cancel_body unconsumed unconsumed_body consume consume_body detach detach_body
connect owner_init start start_take install query_stop register register_done deregister callback weak_lock
invoke_end deliver deliver_stopped deliver_error abandon abandon_active active_store delivery_claim receiver release_owner
owner_take owner_commit owner_reset destroy_op op_take op_commit op_abandon op_dead drop_future move_future
lock unlock copy move drop weak_copy weak_drop state_dead state_free cb_free consumer_dead source_dead producer_deregister
resource_retain resource_release resource_free association_release join scope_stop scope_request_end signal await wait_done wait_stop
producer_construct producer_prepare producer_register producer_invoke producer_fn producer_invoke_end consumer_end state_end consume_return defensive_take
fail_install consume_fail discard_cb unrelated no_op''').split()
STEPS = {a: SimpleNamespace(lines=REFS.get(a, 'model-only: library contract / finite script / implicit member teardown'), desc=a)
         for a in ACTIONS}
EXTRA_REFS = (
    '__inner_recv::set_error | auto state = __state->shared_from_this();',
    '__inner_recv::set_stopped | auto state = __state->shared_from_this();',
    '__sender::~__sender | __state->__abandon_unconsumed();',
    '__sender::operator= | __state->__abandon_unconsumed();',
    '__sender::connect | state->__abandon_unconsumed();',
    '::__make_state | return std::shared_ptr<State>',
    '::__make_resource | resource_traits_t::allocate(resource_alloc, 1);',
    '__resource_allocator::~__resource_allocator | __resource->__release();',
    '__resource_allocator::operator= | old->__release();',
    'spawn_future_t::operator() | auto wrapped = token.wrap',
)
# Atomic indices: resource, state strong/weak, consumer strong/weak, active,
# downstream stop, state stop and scope stop (distinct publications).
RESOURCE, SS, SW, CS, CW, ACTIVE, DOWN, STOP, SCOPE_STOP = range(9)
FIELDS = '''threads errors refs weak edges wedges allocated alive cb_alive resource_refs allocators association releases
scope_alive phase taken result stores transitions consumer_cb keepalive owner op_alive destroying started received expected consumer_expected receiver_expected
producer_started producer_done producer_claimed stop_requested callback_status callback_thread callback_inline callback_pub callback_end
pregs pthreads ppub pend fused_winner fused_calls stop_obligation producer_stop_seen prestart_notified notice_pub request_owners consume_calls
requesters locks mutex vc views hist accesses signals obligation cancellation delivery_claims destructor_won'''.split()


class Cfg:
    def __init__(self, **kw):
        self.name = 'cfg'
        self.threads = ()
        self.mode = 'own'
        self.registration = 'start'
        self.payload = 'value'
        self.token = 'inplace'
        self.closed = False
        self.failure = ''
        self.inline_destroy = False
        self.inline_unrelated = False
        self.hb = False
        self.stale = False
        self.mut = ''
        self.control = ''
        self.required_coverage = ()
        self.__dict__.update(kw)


class World:
    pass


def key(w):
    return tuple(getattr(w, f) for f in FIELDS)


def state(k):
    w = World()
    for f, v in zip(FIELDS, k):
        setattr(w, f, v)
    return w


def error(w, msg):
    w.errors += (msg,)


def prepend(w, t, *ops):
    change(w, 'threads', t, tuple(ops) + w.threads[t])


def signal(w, t, name):
    d = dict(w.signals)
    d[name] = synchronization(w, t)
    w.signals = tuple(sorted(d.items()))


def edge(w, name):
    return dict(w.edges).get(name, -1)


def setedge(w, name, obj, weak=False):
    field = 'wedges' if weak else 'edges'
    d = dict(getattr(w, field))
    if obj == -1:
        d.pop(name, None)
    else:
        d[name] = obj
    setattr(w, field, tuple(sorted(d.items())))


def touch(w, t, obj, write=False):
    if not w.alive[obj]:
        error(w, 'C3: object access after destruction')
    mem.access(w, t, ('state', 'consumer')[obj] + ' storage', write)


def op_access(w, t, write=False):
    if not w.op_alive:
        error(w, 'C3: operation storage after destruction')
    mem.access(w, t, 'operation storage', write)


def scope_access(w, t, write=False):
    if not w.scope_alive:
        error(w, 'C3: scope storage after destruction')
    mem.access(w, t, 'scope storage', write)


def plain(w, t, name, write=False):
    if name in ('operation.__state', 'consumer_state'):
        op_access(w, t)
    elif name in ('phase', 'consumer_taken', 'result', 'consumer_cb', 'keepalive', 'producer completion'):
        touch(w, t, 0)
    elif name in ('owner', 'consumer.__state', 'callback optional'):
        touch(w, t, 1)
    mem.access(w, t, name, write)


def producer_select(w, t, slot):
    # Atomic registration/request selection; invocation ownership is per input.
    if w.pregs[slot] == 'pending':
        COV.add('producer.select.' + str(slot))
        change(w, 'pregs', slot, 'executing')
        change(w, 'pthreads', slot, t)
        mem.acquire(w, t, w.ppub[slot])
        w.stop_obligation = True
        prepend(w, t, ('producer_invoke', slot), ('producer_invoke_end', slot))


def producer_registration_violations(w):
    # Both registrations are members of the child operation in state storage,
    # even though input 1 borrows a different source. Invocation return may
    # remain on the self-removing thread's stack after removal/storage end.
    return tuple('C6: producer registration %d survives containing storage' % slot
                 for slot, status in enumerate(w.pregs) if status not in ('absent', 'removed'))


def producer_entry(w, t):
    if not w.alive[0]:
        error(w, 'C6: producer callback enters after containing storage lifetime')
    touch(w, t, 0)


def caller_owners(w, t):
    # Strong owners held by the actual caller, not the redundant callee pin.
    names = ('future', 'op_state', 'start_state.' + str(t), 'weak_pin.' + str(t),
             'abandon_state.' + str(t), 'delivery_state.' + str(t))
    return tuple(n for n in names if edge(w, n) == 0)



def initial():
    w = World()
    n = len(C.threads)
    z, bounds = (0,) * n, (0,) * 9
    pub = (z, bounds)
    w.threads, w.errors = C.threads, ()
    w.refs = w.weak = (0, 0)
    w.edges = w.wedges = ()
    w.allocated = (False,) * 4  # resource, state, state CB, combined consumer CB
    w.alive = w.cb_alive = (False, False)
    w.resource_refs, w.allocators = 0, ()
    w.association, w.releases, w.scope_alive = False, 0, True
    w.phase, w.taken, w.result, w.stores, w.transitions = 'running', False, '', 0, 0
    w.consumer_cb, w.keepalive, w.owner = False, False, False
    w.op_alive, w.destroying, w.started = False, False, False
    w.received, w.expected, w.consumer_expected, w.receiver_expected = (), '', '', ''
    w.producer_started = w.producer_done = w.producer_claimed = False
    w.stop_requested = False
    w.callback_status, w.callback_thread, w.callback_inline = 'absent', -1, False
    w.callback_pub = w.callback_end = pub
    w.pregs, w.pthreads = ('absent',) * 2, (-1,) * 2
    w.ppub = w.pend = (pub,) * 2
    w.fused_winner, w.fused_calls, w.stop_obligation = -1, 0, False
    w.producer_stop_seen = False
    w.prestart_notified, w.notice_pub = False, pub
    w.request_owners, w.consume_calls, w.requesters = (), 0, 0
    w.locks, w.mutex = (-1, -1, -1), (pub,) * 3
    w.vc = tuple(tuple(1 if t == u else 0 for u in range(n)) for t in range(n))
    w.views = (bounds,) * n
    w.hist = tuple(((v, z, pub),) for v in (0, 0, 0, 0, 0, True, False, False, False))
    w.accesses, w.signals = (), ()
    w.obligation, w.cancellation, w.delivery_claims, w.destructor_won = False, False, 0, False
    return w


def enabled(w, t, op):
    a, *args = op
    if a == 'lock':
        return w.locks[args[0]] == -1
    if a == 'await':
        return args[0] in dict(w.signals)
    if a == 'wait_done':
        return 'producer_done' in dict(w.signals)
    if a == 'wait_stop':
        return w.producer_stop_seen or 'producer_done' in dict(w.signals)
    if a == 'deregister' and w.callback_thread not in (-1, t):
        COV.add('deregister.wait')
        return False
    if a == 'producer_deregister' and args and w.pthreads[args[0]] not in (-1, t):
        COV.add('producer.deregister.wait')
        return False
    return True


def locked(w, t, body, *args):
    prepend(w, t, ('lock', 0), (body, *args), ('unlock', 0))


def deliver_ops(kind, pin):
    return ((kind, pin), ('drop', pin))


def execute(w, t, op, alt=0):
    a, *args = op
    COV.add(a)
    local = lambda s: s + '.' + str(t)
    if a == 'signal':
        signal(w, t, args[0])
    elif a == 'await':
        if not ((C.control == 'operation_no_handoff' and args[0] == 'connected') or
                (C.control == 'scope_no_handoff' and args[0] == 'scope_stop_returned')):
            mem.acquire(w, t, dict(w.signals)[args[0]])
    elif a in ('wait_done', 'wait_stop'):
        if a == 'wait_done' or 'producer_done' in dict(w.signals):
            mem.acquire(w, t, dict(w.signals)['producer_done'])
        else:
            src = STOP if w.stop_requested else SCOPE_STOP
            mem.load(w, t, src, len(w.hist[src])-1)
    elif a == 'lock':
        k = args[0]
        mem.acquire(w, t, w.mutex[k])
        change(w, 'locks', k, t)
        if k == 0:
            touch(w, t, 0)
        elif k == 1:
            touch(w, t, 1)
        else:
            op_access(w, t)
    elif a == 'unlock':
        k = args[0]
        if w.locks[k] != t:
            error(w, 'C8: unlocking unowned mutex')
        if k == 0:
            touch(w, t, 0)
        elif k == 1:
            touch(w, t, 1)
        else:
            op_access(w, t)
        if C.control != 'no_mutex_release' or k != 0:
            change(w, 'mutex', k, synchronization(w, t))
        change(w, 'locks', k, -1)
    elif a == 'launch':
        if w.hist[SCOPE_STOP][-1][0]:
            COV.add('scope.stop_before_launch')
        w.allocated = (True, True, True, False)
        w.alive, w.cb_alive = (True, False), (True, False)
        w.resource_refs = 4
        w.allocators = ('launch', 'state_allocator', 'deleter', 'state_cb')
        w.refs, w.weak = (1, 0), (2, 0)
        plain(w, t, 'resource storage', True)
        plain(w, t, 'state CB storage', True)
        setedge(w, 'future', 0)
        setedge(w, 'esft', 0, True)
        setedge(w, 'implicit.state', 0, True)
        # Construction is single threaded before start; initialize, do not
        # invent release publications for these relaxed reference counts.
        for idx, value in ((RESOURCE, 4), (SS, 1), (SW, 2)):
            mem.publish(w, t, idx, value, 'relaxed', rmw=True)
        construction = (('producer_construct',),) if C.registration == 'connect' else ()
        prepend(w, t, *construction, ('associate',), ('resource_release', 'launch'), ('signal', 'launched'))
    elif a == 'associate':
        scope_access(w, t)
        w.association = not C.closed
        signal(w, t, 'before_start')
        if C.mut == 'member_order':
            COV.add('member_order.initializers_moved')
            # __op now precedes the source whose token connect queries.
            error(w, 'C3: producer connect queries unconstructed stop source')
        if C.closed:
            COV.add('launch.not_started')
            w.expected = 'stopped'
            locked(w, t, 'complete_body', 'stopped', False)
        else:
            prepend(w, t, ('copy', 'keepalive', 0), ('start_producer',))
    elif a == 'start_producer':
        w.keepalive = True
        w.producer_started = True
        if w.hist[SCOPE_STOP][-1][0]:
            COV.add('scope.stop_before_start')
        registration = (('producer_prepare',), ('producer_register', 0), ('producer_register', 1)) if C.registration == 'start' else ()
        if C.registration == 'start':
            COV.add('producer.registration.start')
        completion = ()
        if w.producer_stop_seen:
            # The child contract preserves notification across its start gate.
            mem.acquire(w, t, w.notice_pub)
        if C.mode == 'inline' or (C.mode == 'sync' and w.producer_stop_seen and C.control != 'skip_start_stop'):
            completion = (('produce',),)
        prepend(w, t, *registration, ('signal', 'producer_started'), *completion)
    elif a == 'producer_construct':
        COV.add('producer.registration.connect')
        prepend(w, t, ('producer_prepare',), ('producer_register', 0), ('producer_register', 1),
                ('signal', 'producer_connected'))
    elif a == 'producer_prepare':
        # Logical fused-token precheck precedes either input registration.
        for slot, src in enumerate((STOP, SCOPE_STOP)):
            if mem.load(w, t, src, len(w.hist[src])-1):
                w.stop_obligation = True
                COV.add('producer.pre_stopped')
                prepend(w, t, ('producer_invoke', -1))
                break
    elif a == 'producer_register':
        slot = args[0]
        (touch(w, t, 0) if slot == 0 else scope_access(w, t))
        change(w, 'ppub', slot, synchronization(w, t))
        src = (STOP, SCOPE_STOP)[slot]
        if mem.load(w, t, src, len(w.hist[src])-1):
            change(w, 'pregs', slot, 'inline')
            change(w, 'pthreads', slot, t)
            w.stop_obligation = True
            COV.add('producer.register.inline')
            prepend(w, t, ('producer_invoke', slot), ('producer_invoke_end', slot))
        else:
            change(w, 'pregs', slot, 'pending')
            COV.add('producer.register.pending')
    elif a == 'producer_invoke':
        producer_entry(w, t)
        # Imported fused committed-delivery contract: the winner's next action
        # is Fn, with no protocol wait. A losing input never waits for Fn.
        if w.fused_winner == -1:
            COV.add('producer.winner.' + str(args[0]))
            w.fused_winner = t
            if not C.control.startswith('drop_fused_'):
                prepend(w, t, ('producer_fn',))
        else:
            COV.add('producer.loser')
    elif a == 'producer_fn':
        producer_entry(w, t)
        w.fused_calls += 1
        COV.add('producer.stop_delivered')
        if not w.producer_started:
            w.prestart_notified = True
            COV.add('producer.notification.before_start')
        if C.control != 'drop_prestart_notice' or w.producer_started:
            w.producer_stop_seen = True
            w.notice_pub = synchronization(w, t)
        if ((C.mode == 'sync' and w.producer_started) or C.control == 'complete_before_start') and not w.producer_claimed:
            prepend(w, t, ('produce',))
    elif a == 'producer_invoke_end':
        slot = args[0]
        change(w, 'pthreads', slot, -1)
        if w.pregs[slot] == 'executing':
            change(w, 'pregs', slot, 'completed')
        change(w, 'pend', slot, synchronization(w, t))
    elif a == 'resource_retain':
        name = args[0]
        if not w.allocated[0] or not w.scope_alive:
            error(w, 'C3: allocator retain after resource/scope destruction')
        plain(w, t, 'resource storage')
        w.resource_refs += 1
        w.allocators += (name,)
        mem.publish(w, t, RESOURCE, w.resource_refs, 'relaxed', rmw=True)
    elif a == 'resource_release':
        name = args[0]
        if name not in w.allocators:
            error(w, 'C3: allocator reference underflow')
            return
        if not w.allocated[0] or not w.scope_alive:
            error(w, 'C3: allocator release after resource/scope destruction')
        plain(w, t, 'resource storage')
        w.allocators = tuple(x for x in w.allocators if x != name)
        w.resource_refs -= 1
        mem.publish(w, t, RESOURCE, w.resource_refs, 'acq_rel', rmw=True)
        if w.resource_refs == 0:
            ops = (('resource_free',), ('association_release',))
            if C.mut == 'association_first':
                ops = tuple(reversed(ops))
            prepend(w, t, *ops)
    elif a == 'resource_free':
        if not w.scope_alive:
            error(w, 'C5: allocator uses destroyed scope')
        change(w, 'allocated', 0, False)
        plain(w, t, 'resource storage', True)
    elif a == 'association_release':
        if w.association:
            scope_access(w, t)
            if any(w.allocated):
                error(w, 'C5: allocation live at association release')
            w.association, w.releases = False, w.releases + 1
            signal(w, t, 'released')
        else:
            signal(w, t, 'released')
    elif a == 'copy':
        name, obj = args
        if not w.cb_alive[obj] or w.refs[obj] == 0:
            error(w, 'C3: strong increment resurrects dead object')
            return
        plain(w, t, ('state', 'consumer')[obj] + ' CB storage')
        setedge(w, name, obj)
        change(w, 'refs', obj, w.refs[obj]+1)
        mem.publish(w, t, SS if obj == 0 else CS, w.refs[obj], 'relaxed', rmw=True)
    elif a == 'move':
        src, dst = args
        obj = edge(w, src)
        for name in (src,dst):
            field = {'op_state':'operation.__state', 'consumer_state':'consumer.__state', 'op_consumer':'consumer_state'}.get(name)
            if field:
                plain(w, t, field, True)
        setedge(w, src, -1)
        if obj != -1:
            setedge(w, dst, obj)
    elif a == 'drop':
        name = args[0]
        field = {'op_state': 'operation.__state', 'op_consumer': 'consumer_state', 'consumer_state': 'consumer.__state'}.get(name)
        if field:
            plain(w, t, field, True)
        obj = edge(w, name)
        if obj == -1:
            return
        if not w.cb_alive[obj]:
            error(w, 'C3: strong release after control block destruction')
        plain(w, t, ('state', 'consumer')[obj] + ' CB storage')
        setedge(w, name, -1)
        change(w, 'refs', obj, w.refs[obj]-1)
        mem.publish(w, t, SS if obj == 0 else CS, w.refs[obj], 'acq_rel', rmw=True)
        if obj == 1 and not w.refs[obj] and w.callback_status not in ('absent', 'removed'):
            error(w, 'I4: consumer released with callback installed')
        if not w.refs[obj]:
            prepend(w, t, *( (('resource_retain', local('deleter_local')), ('state_dead',)) if obj == 0 else (('consumer_dead',),)))
    elif a == 'weak_copy':
        name, obj = args
        if C.mut == 'strong_callback' and name == 'callback':
            execute(w, t, ('copy', 'callback_strong', obj))
            return
        if not w.cb_alive[obj]:
            error(w, 'C3: weak copy after control block destruction')
        plain(w, t, ('state', 'consumer')[obj] + ' CB storage')
        setedge(w, name, obj, True)
        change(w, 'weak', obj, w.weak[obj]+1)
        mem.publish(w, t, SW if obj == 0 else CW, w.weak[obj], 'relaxed', rmw=True)
    elif a == 'weak_drop':
        name = args[0]
        obj = dict(w.wedges).get(name, -1)
        if obj == -1:
            return
        if not w.cb_alive[obj]:
            error(w, 'C3: weak release after control block destruction')
        plain(w, t, ('state', 'consumer')[obj] + ' CB storage')
        setedge(w, name, -1, True)
        change(w, 'weak', obj, w.weak[obj]-1)
        mem.publish(w, t, SW if obj == 0 else CW, w.weak[obj], 'acq_rel', rmw=True)
        if w.weak[obj] == 0:
            prepend(w, t, ('cb_free', obj))
    elif a == 'state_dead':
        touch(w, t, 0)
        if w.producer_started:
            plain(w, t, 'producer completion')
        if w.producer_started and not w.producer_done:
            error(w, 'C7: state destroyed before producer completion')
        if C.mut == 'member_order':
            prepend(w, t, ('resource_release', 'state_allocator'), ('source_dead',),
                    ('producer_deregister', 1), ('producer_deregister', 0), ('weak_drop', 'esft'), ('state_end',), ('state_free',))
        else:
            prepend(w, t, ('producer_deregister', 1), ('producer_deregister', 0), ('resource_release', 'state_allocator'),
                    ('source_dead',), ('weak_drop', 'esft'), ('state_end',), ('state_free',))
    elif a == 'state_end':
        for violation in producer_registration_violations(w):
            error(w, violation)
        COV.add('producer.storage_end.unstarted' if not w.producer_started else 'producer.storage_end.started')
        touch(w, t, 0, True)
        change(w, 'alive', 0, False)
    elif a == 'producer_deregister':
        touch(w, t, 0)
        slot = args[0]
        skipped = C.control in ('skip_registration_teardown', 'skip_unstarted_teardown') or C.control in (
            ('skip_state_teardown', 'skip_state_teardown_late_stop') if slot == 0 else
            ('skip_scope_teardown', 'skip_scope_teardown_late_stop'))
        if not skipped:
            if w.pregs[slot] not in ('absent', 'removed', 'inline'):
                (touch(w, t, 0) if slot == 0 else scope_access(w, t))
            if w.pthreads[slot] == -1:
                mem.acquire(w, t, w.pend[slot])
            COV.add('producer.deregister.self' if w.pthreads[slot] == t else 'producer.deregister.external')
            if not w.producer_started and w.pregs[slot] != 'absent':
                COV.add('producer.deregister.unstarted.' + str(slot))
            change(w, 'pregs', slot, 'removed')
    elif a == 'source_dead':
        touch(w, t, 0)
        if w.pregs[0] not in ('absent', 'removed'):
            error(w, 'C6: source destroyed before producer deregistration')
        if w.requesters:
            error(w, 'C3: borrowed source destroyed inside request')
    elif a == 'state_free':
        change(w, 'allocated', 1, False)
        # The deleter's local allocator covers destruction and deallocation.
        # Its increment is separate and takes place before member destruction.
        prepend(w, t, ('resource_release', local('deleter_local')), ('weak_drop', 'implicit.state'))
    elif a == 'consumer_dead':
        touch(w, t, 1)
        prepend(w, t, ('deregister',), ('drop', 'consumer_state'), ('consumer_end',),
                ('weak_drop', 'implicit.consumer'))
    elif a == 'consumer_end':
        if w.callback_status not in ('absent', 'removed'):
            error(w, 'I4: consumer released with callback installed')
        touch(w, t, 1, True)
        change(w, 'alive', 1, False)
    elif a == 'cb_free':
        obj = args[0]
        if w.refs[obj] or w.weak[obj]:
            error(w, 'C3: premature control block destruction')
        plain(w, t, ('state', 'consumer')[obj] + ' CB storage', True)
        change(w, 'cb_alive', obj, False)
        if obj == 0:
            change(w, 'allocated', 2, False)
            prepend(w, t, ('resource_release', 'deleter'), ('resource_release', 'state_cb'))
        else:
            change(w, 'allocated', 3, False)
            prepend(w, t, ('resource_release', 'consumer_cb'))
    elif a == 'produce':
        if not w.producer_started:
            error(w, 'C6: completion of unstarted producer')
        if not w.producer_claimed:
            w.producer_claimed = True
            # Producer completion obligation is recorded before dispatch.
            if C.control != 'skip_producer':
                prepend(w, t, ('inner',))
    elif a == 'inner':
        ops = (('complete', C.payload), ('signal', 'producer_done'))
        w.producer_done = True
        plain(w, t, 'producer completion', True)
        COV.add('producer.' + C.mode)
        if w.prestart_notified:
            COV.add('producer.completion.after_prestart')
        if (C.mut != 'drop_inner_pin' or C.payload != 'value') and C.control != 'keep_cover_removed':
            ops = (('copy', local('entry'), 0),) + ops + (('drop', local('entry')),)
        prepend(w, t, *ops)
    elif a == 'complete':
        locked(w, t, 'complete_body', args[0], True)
    elif a == 'complete_body':
        value, producer = args
        touch(w, t, 0)
        plain(w, t, 'phase')
        if w.phase != 'done':
            plain(w, t, 'result', True)
            plain(w, t, 'phase', True)
            plain(w, t, 'consumer_cb', True)
            if producer:
                w.expected = 'error' if C.failure == 'result' else C.payload
            w.result = 'error' if C.failure == 'result' else value
            if (producer and C.control == 'wrong_producer') or (not producer and C.control == 'wrong_closed'):
                w.result = 'stopped' if producer else 'value'
            if C.failure == 'result':
                COV.add('failure.result')
            w.phase, w.stores, w.transitions = 'done', w.stores+1, w.transitions+1
            signal(w, t, 'result_written')
            if w.consumer_cb:
                w.consumer_cb = False
                execute(w, t, ('move', 'registered', local('delivery')))
                # Delivery must follow unlock; the mutant moves the call only.
                seq = (('deliver', local('delivery')),)
                tail = w.threads[t]
                if C.mut == 'deliver_locked':
                    change(w, 'threads', t, seq + tail[:1] + (('complete_tail',),) + tail[1:])
                elif C.mut == 'keepalive_first' or C.control == 'inner_cover_removed':
                    change(w, 'threads', t, tail[:1] + (('complete_tail',),) + seq + tail[1:])
                else:
                    change(w, 'threads', t, tail[:1] + seq + (('complete_tail',),) + tail[1:])
                # consumer_cb was declared before keepalive: it dies last.
                tail = w.threads[t]
                pos = next(i+1 for i,x in enumerate(tail) if x == ('complete_tail',))
                if C.mut == 'keepalive_first' or C.control == 'inner_cover_removed':
                    pos += 1
                change(w, 'threads', t, tail[:pos] + (('drop', local('delivery')),) + tail[pos:])
                return
        tail = w.threads[t]
        change(w, 'threads', t, tail[:1] + (('complete_tail',),) + tail[1:])
    elif a == 'complete_tail':
        locked(w, t, 'keep_take')
    elif a == 'keep_take':
        touch(w, t, 0)
        plain(w, t, 'keepalive', True)
        if C.control != 'retain_keepalive':
            execute(w, t, ('move', 'keepalive', local('keep')))
        w.keepalive = False
        tail = w.threads[t]
        change(w, 'threads', t, tail[:1] + (('drop', local('keep')),) + tail[1:])
    elif a == 'connect':
        if C.failure == 'allocation':
            COV.add('failure.allocation')
            prepend(w, t, ('unconsumed',), ('drop', 'future'), ('signal', 'connected'))
        else:
            w.op_alive = True
            op_access(w, t, True)
            plain(w, t, 'resource storage')
            plain(w, t, 'consumer CB storage', True)
            change(w, 'allocated', 3, True)
            change(w, 'alive', 1, True)
            plain(w, t, 'consumer storage', True)
            change(w, 'cb_alive', 1, True)
            change(w, 'refs', 1, 1)
            change(w, 'weak', 1, 1)
            setedge(w, 'op_consumer', 1)
            setedge(w, 'implicit.consumer', 1, True)
            for idx in (CS, CW):
                mem.publish(w, t, idx, 1, 'relaxed', rmw=True)
            prepend(w, t, ('resource_retain', 'consumer_cb'), ('move', 'future', 'op_state'), ('owner_init',), ('signal', 'connected'))
    elif a == 'owner_init':
        w.owner, w.op_alive = True, True
        op_access(w, t, True)
        plain(w, t, 'owner', True)
    elif a == 'start':
        op_access(w, t)
        w.started, w.obligation = True, True
        prepend(w, t, ('move', 'op_state', local('start_state')), ('lock', 2), ('start_take',), ('unlock', 2))
    elif a == 'start_take':
        plain(w, t, 'consumer_state')
        # Copy under the lock: its atomic increment is a distinct step.
        ops = (('copy', local('start_consumer'), 1),)
        tail = (('drop', local('start_consumer')), ('drop', local('start_state')), ('signal', 'started'))
        order = (('install',), ('consume',))
        if C.mut == 'consume_first':
            order = tuple(reversed(order))
        rest = w.threads[t]
        change(w, 'threads', t, ops + rest[:1] + order + tail + rest[1:])
    elif a == 'install':
        touch(w, t, 1)
        plain(w, t, 'consumer.__state', True)
        prepend(w, t, ('copy', 'consumer_state', 0), ('query_stop',))
    elif a == 'query_stop':
        if C.failure == 'install':
            w.consumer_expected = 'error'
            COV.add('failure.install')
            prepend(w, t, ('fail_install',))
            return
        stopped = mem.load(w, t, DOWN, alt) if C.token == 'inplace' else False
        if stopped:
            prepend(w, t, ('cancel',))
        if C.token == 'inplace':
            # Registration follows the optional inline __try_cancel call.
            tail = w.threads[t]
            pos = 1 if stopped else 0
            change(w, 'threads', t, tail[:pos] + (('weak_copy', 'callback', 0), ('register',)) + tail[pos:])
    elif a == 'fail_install':
        prepend(w, t, (('deliver_stopped' if C.control == 'wrong_install' else 'deliver_error'), local('start_consumer')), ('unconsumed',))
        change(w, 'threads', t, tuple(x for x in w.threads[t] if x != ('consume',)))
    elif a == 'register':
        plain(w, t, 'callback optional', True)
        w.callback_pub = synchronization(w, t)
        stopped = mem.load(w, t, DOWN, len(w.hist[DOWN])-1)
        w.callback_status = 'inline' if stopped else 'pending'
        if stopped:
            w.callback_inline, w.callback_thread = True, t
            COV.add('register.inline')
            prepend(w, t, ('callback',), ('invoke_end',), ('register_done',))
        else:
            COV.add('register.pending')
            prepend(w, t, ('register_done',))
    elif a == 'register_done':
        signal(w, t, 'installed')
    elif a == 'deregister':
        if C.control in ('callback_cover_removed', 'callback_abandon_cover_removed'):
            return
        touch(w, t, 1)
        plain(w, t, 'callback optional', True)
        if w.callback_status not in ('absent', 'removed'):
            if w.callback_thread == -1:
                mem.acquire(w, t, w.callback_end)
            COV.add('deregister.self' if w.callback_thread == t else 'deregister.external')
            w.callback_status = 'removed'
            prepend(w, t, ('weak_drop', 'callback'), ('drop', 'callback_strong'))
    elif a == 'callback':
        touch(w, t, 1)
        prepend(w, t, ('weak_lock',))
    elif a == 'weak_lock':
        # Increment-if-nonzero is a single RMW; no resurrecting zero.
        if w.refs[0]:
            execute(w, t, ('copy', local('weak_pin'), 0))
            prepend(w, t, ('cancel',), ('drop', local('weak_pin')))
        else:
            COV.add('weak_lock.expired')
    elif a == 'invoke_end':
        w.callback_thread = -1
        if w.callback_status != 'removed':
            w.callback_status = 'completed'
        w.callback_end = synchronization(w, t)
    elif a == 'request':
        owners = caller_owners(w, t)
        w.request_owners += ((t, owners),)
        ops = () if C.control == 'skip_stop_request' else (('request_body',),)
        if C.mut != 'drop_request_pin':
            ops = (('copy', local('request_pin'), 0),) + ops + (('drop', local('request_pin')),)
        if C.control == 'request_cover_removed':
            ops = tuple(('drop', n) for n in owners) + ops
        prepend(w, t, *ops, ('request_return',))
    elif a == 'request_body':
        if edge(w, local('abandon_state')) != -1 and w.hist[ACTIVE][-1][0]:
            error(w, 'I6: abandonment requests stop before active is cleared')
        touch(w, t, 0)
        w.requesters += 1
        if not w.stop_requested:
            w.stop_requested = True
            mem.publish(w, t, STOP, True)
            prepend(w, t, ('request_end',))
            producer_select(w, t, 0)
        else:
            prepend(w, t, ('request_end',))
    elif a == 'request_end':
        touch(w, t, 0)
        w.requesters -= 1
    elif a == 'request_return':
        w.request_owners = tuple(x for x in w.request_owners if x[0] != t)
    elif a == 'stop_source':
        if not w.hist[DOWN][-1][0]:
            mem.publish(w, t, DOWN, True)
            if w.callback_status == 'pending':
                w.callback_status, w.callback_thread = 'executing', t
                mem.acquire(w, t, w.callback_pub)
                prepend(w, t, ('callback',), ('invoke_end',))
        signal(w, t, 'downstream_stop')
    elif a == 'cancel':
        prepend(w, t, ('request',), ('lock', 0), ('cancel_body',), ('unlock', 0))
    elif a == 'cancel_body':
        touch(w, t, 0)
        plain(w, t, 'phase')
        # Contract arbitration is independent of the implementation done guard.
        if w.phase != 'done':
            w.expected = 'stopped'
            w.cancellation = True
        if w.phase != 'done' or C.mut == 'cancel_done':
            plain(w, t, 'result', True)
            plain(w, t, 'phase', True)
            w.phase, w.result = 'done', ('value' if C.control == 'wrong_cancel' else 'stopped')
            w.stores, w.transitions = w.stores+1, w.transitions+1
            if not w.stop_requested:
                error(w, 'C6: successful cancellation without stop request')
            if w.consumer_cb:
                w.consumer_cb = False
                execute(w, t, ('move', 'registered', local('cancel_delivery')))
                tail = w.threads[t]
                change(w, 'threads', t, tail[:1] + deliver_ops('deliver', local('cancel_delivery')) + tail[1:])
            COV.add('cancel.success')
        else:
            COV.add('cancel.done')
    elif a == 'unconsumed':
        locked(w, t, 'unconsumed_body')
    elif a == 'unconsumed_body':
        if w.consume_calls:
            error(w, 'I3: abandon_unconsumed after consume')
        touch(w, t, 0)
        plain(w, t, 'consumer_taken', True)
        if not w.taken or C.mut == 'abandon_taken':
            w.taken = True
            if w.phase != 'done':
                tail = w.threads[t]
                change(w, 'threads', t, tail[:1] + (('request',),) + tail[1:])
    elif a == 'defensive_take':
        w.taken = True
        w.consumer_expected = 'stopped'
    elif a == 'consume':
        w.consume_calls += 1
        if C.failure == 'consume':
            w.consumer_expected = 'error'
            COV.add('failure.consume')
            prepend(w, t, ('copy', local('consume_arg'), 1), ('drop', local('consume_arg')),
                    (('deliver_stopped' if C.control == 'wrong_consume_failure' else 'deliver_error'), local('start_consumer')), ('request',))
        else:
            # Passing by value copies the strong consumer argument.
            prepend(w, t, ('copy', local('consume_arg'), 1), ('lock', 0), ('consume_body',), ('unlock', 0))
    elif a == 'consume_body':
        touch(w, t, 0)
        plain(w, t, 'consumer_taken', True)
        plain(w, t, 'phase')
        tail = w.threads[t]
        after = ()
        # A pre-existing taken bit creates the stopped contract even when the
        # implementation guard is removed. Delivery kind cannot set the oracle.
        if w.taken:
            w.consumer_expected = 'stopped'
        if w.taken and C.mut != 'consume_taken':
            COV.add('consume.already_consumed')
            after = ((('deliver' if C.control == 'wrong_consumed' else 'deliver_stopped'), local('start_consumer')),)
        else:
            w.taken = True
            if w.phase == 'done':
                COV.add('consume.deliver_now')
                after = (('deliver', local('start_consumer')),)
            else:
                COV.add('consume.registered')
                plain(w, t, 'consumer_cb', True)
                execute(w, t, ('move', local('consume_arg'), 'registered'))
                w.consumer_cb = True
        change(w, 'threads', t, tail[:1] + (('drop', local('consume_arg')), ('consume_return',)) + after + tail[1:])
    elif a == 'consume_return':
        if C.control == 'repeat_consume' and w.consume_calls == 1:
            prepend(w, t, ('consume',))
    elif a in ('deliver', 'deliver_stopped', 'deliver_error', 'abandon_active'):
        touch(w, t, 1)
        if a == 'abandon_active' and C.control == 'abandon_cover_removed':
            prepend(w, t, ('delivery_claim', a))
            return
        if w.hist[ACTIVE][-1][0]:
            if C.mut == 'plain_active' and a == 'deliver':
                mem.load(w, t, ACTIVE, len(w.hist[ACTIVE])-1)
                prepend(w, t, ('active_store', a))
            else:
                mem.publish(w, t, ACTIVE, False, 'acq_rel', rmw=True)
                prepend(w, t, ('delivery_claim', a))
        elif C.mut == 'plain_active' and a == 'deliver':
            mem.load(w, t, ACTIVE, len(w.hist[ACTIVE])-1)
        else:
            # exchange writes even when the old value was false.
            mem.publish(w, t, ACTIVE, False, 'acq_rel', rmw=True)
    elif a == 'active_store':
        mem.publish(w, t, ACTIVE, False, 'release')
        prepend(w, t, ('delivery_claim', args[0]))
    elif a == 'delivery_claim':
        kind = args[0]
        w.delivery_claims += 1
        if kind == 'abandon_active':
            w.destructor_won = True
            has_state = edge(w, 'consumer_state') != -1
            seq = (('move', 'consumer_state', local('abandon_state')), ('detach',), ('deregister',))
            if has_state:
                if C.mut == 'abandon_stop_first':
                    seq = (seq[0], ('request',), seq[1], seq[2])
                else:
                    seq += (('request',),)
            seq += (('drop', local('abandon_state')),)
        else:
            w.receiver_expected = w.consumer_expected or w.expected
            seq = (('move', 'consumer_state', local('delivery_state')), ('deregister',), ('release_owner',), ('receiver', kind), ('drop', local('delivery_state')))
            if C.mut == 'owner_after' and kind == 'deliver':
                seq = (seq[0], seq[1], seq[3], seq[2], seq[4])
            if C.mut == 'callback_after' and kind == 'deliver':
                seq = (seq[0], seq[2], seq[3], seq[1], seq[4])
        if C.control in ('keep_cover_removed', 'inner_cover_removed') and kind != 'abandon_active':
            seq = seq[:1] + (('drop', local('delivery_state')),) + seq[1:]
        prepend(w, t, *seq)
    elif a == 'release_owner':
        if C.mut == 'owner_unlocked':
            prepend(w, t, ('owner_take',))
        else:
            prepend(w, t, ('lock', 1), ('owner_take',), ('unlock', 1))
    elif a == 'owner_take':
        touch(w, t, 1)
        plain(w, t, 'owner', C.mut != 'owner_unlocked')
        had = w.owner
        if C.mut == 'owner_unlocked':
            prepend(w, t, ('owner_commit', had))
        else:
            execute(w, t, ('owner_commit', had))
    elif a == 'owner_commit':
        if C.mut != 'owner_unlocked' and w.locks[1] != t:
            error(w, 'I5: owner clear without owner mutex')
        plain(w, t, 'owner', True)
        w.owner = False
        if args[0]:
            prepend(w, t, ('lock', 2), ('owner_reset',), ('unlock', 2))
    elif a == 'owner_reset':
        plain(w, t, 'consumer_state', True)
        execute(w, t, ('move', 'op_consumer', local('reset_consumer')))
        prepend(w, t, ('drop', local('reset_consumer')))
    elif a == 'detach':
        if edge(w, local('abandon_state')) != -1:
            locked(w, t, 'detach_body')
    elif a == 'detach_body':
        plain(w, t, 'consumer_cb', True)
        if w.consumer_cb:
            w.consumer_cb = False
            prepend(w, t, ('drop', 'registered'))
    elif a == 'receiver':
        owners = tuple(n for n, obj in w.edges if obj == 0 and not n.startswith(('entry.', 'keepalive', 'keep.')))
        if not owners:
            error(w, 'I8: delivery lacks consumer or caller state owner')
        touch(w, t, 1)
        kind = args[0]
        if w.destructor_won or (w.destroying and C.control == 'literal_C1'):
            error(w, 'C1: receiver after destruction begins')
        if w.callback_status not in ('absent', 'removed'):
            error(w, 'C6: receiver before callback deregistration')
        if kind == 'deliver':
            touch(w, t, 0)
            plain(w, t, 'result')
        value = {'deliver_stopped': 'stopped', 'deliver_error': 'error'}.get(kind, w.result)
        if C.control == 'corrupt_result':
            value = 'corrupted'
        w.received += (value,)
        if value != w.receiver_expected:
            error(w, 'C2: receiver result differs from winning result')
        if C.control == 'drop_receiver':
            w.received = ()
        if C.inline_unrelated:
            prepend(w, t, ('unrelated',))
        if C.inline_destroy:
            prepend(w, t, ('destroy_op',))
        if C.control == 'wait_cycle':
            prepend(w, t, ('lock', 0), ('lock', 0))
        if C.control == 'receiver_reenter':
            prepend(w, t, ('request',))
        if C.control == 'post_consume_abandon':
            prepend(w, t, ('unconsumed',))
        signal(w, t, 'received')
    elif a == 'destroy_op':
        if not w.op_alive or w.destroying:
            return
        op_access(w, t)
        w.destroying = True
        plain(w, t, 'operation.__state')
        if edge(w, 'op_state') != -1:
            prepend(w, t, ('unconsumed',))
        rest = w.threads[t]
        ops = (('lock', 2), ('op_take',), ('unlock', 2), ('op_abandon',), ('drop', 'op_state'), ('op_dead',))
        if C.mut == 'op_unlocked':
            ops = (('op_take',),) + ops[3:]
        change(w, 'threads', t, rest[:1] + ops + rest[1:] if rest[:1] == (('unconsumed',),) else ops + rest)
    elif a == 'op_take':
        plain(w, t, 'consumer_state', C.mut != 'op_unlocked')
        obj = edge(w, 'op_consumer')
        if C.mut == 'op_unlocked':
            prepend(w, t, ('op_commit', obj))
        else:
            execute(w, t, ('op_commit', obj))
    elif a == 'op_commit':
        plain(w, t, 'consumer_state', True)
        setedge(w, 'op_consumer', -1)
        if args[0] != -1:
            setedge(w, local('destructor_consumer'), args[0])
    elif a == 'op_abandon':
        if edge(w, local('destructor_consumer')) != -1:
            prepend(w, t, ('abandon',), ('drop', local('destructor_consumer')))
    elif a == 'abandon':
        ops = () if C.control == 'owner_cover_removed' else (('release_owner',),)
        prepend(w, t, *ops, ('abandon_active',))
    elif a == 'op_dead':
        if any(x != -1 for x in w.locks[2:]):
            error(w, 'C3: operation destroyed with locked mutex')
        if w.owner:
            error(w, 'I5: owner link survives operation storage')
        op_access(w, t, True)
        w.op_alive = False
    elif a in ('drop_future', 'move_future'):
        prepend(w, t, ('unconsumed',), ('drop', 'future'))
    elif a == 'join':
        if C.control != 'scope_no_handoff':
            mem.acquire(w, t, dict(w.signals)['released'])
        if w.association:
            error(w, 'C5: join while associated')
        scope_access(w, t, True)
        if w.pregs[1] not in ('absent', 'removed', 'inline'):
            error(w, 'C6: scope destroyed before producer deregistration')
        w.scope_alive = False
    elif a == 'scope_stop':
        scope_access(w, t)
        if 'launched' in dict(w.signals) and not w.alive[0]:
            COV.add('scope.stop_after_state_end')
        if not w.hist[SCOPE_STOP][-1][0]:
            mem.publish(w, t, SCOPE_STOP, True)
            prepend(w, t, ('scope_request_end',))
            producer_select(w, t, 1)
        else:
            prepend(w, t, ('scope_request_end',))
    elif a == 'scope_request_end':
        scope_access(w, t)
        signal(w, t, 'scope_stop_returned')
    elif a == 'unrelated':
        COV.add('receiver.unrelated_destruction')
    elif a == 'no_op':
        pass
    else:
        raise ValueError(op)


def successors(k):
    w = state(k)
    if w.errors:
        return []
    out = []
    for t, script in enumerate(w.threads):
        if not script or not enabled(w, t, script[0]):
            continue
        op = script[0]
        choices = mem.load_choices(w, t, DOWN) if op[0] == 'query_stop' and C.token == 'inplace' else (0,)
        for alt in choices:
            child = copy.copy(w)
            change(child, 'threads', t, script[1:])
            if C.hb or C.stale:
                change(child, 'vc', t, put(child.vc[t], t, child.vc[t][t]+1))
            execute(child, t, op, alt)
            out.append(((t, op[0], tuple(op[1:])), child))
    return out


def state_violations(w):
    errors = list(w.errors)
    for obj in range(2):
        if w.refs[obj] != sum(v == obj for _,v in w.edges) or w.weak[obj] != sum(v == obj for _,v in w.wedges):
            errors.append('C3: reference count differs from ownership graph')
    if any(v < 0 for v in w.refs + w.weak) or w.resource_refs < 0:
        errors.append('C3: count underflow')
    if w.stores > 1 or w.transitions > 1:
        errors.append('C2: result stored or phase transitioned twice')
    if len(w.received) > 1 or w.delivery_claims > 1:
        errors.append('C1: duplicate receiver/arbitration winner')
    # I2 bounds invocation count only. A preset taken bit is separately covered
    # by the contract oracle and C2, even on the first invocation.
    if w.consume_calls > 1:
        errors.append('I2: consume invocation bound exceeded')
    for t, owners in w.request_owners:
        if not owners or not any(edge(w, n) == 0 for n in owners):
            errors.append('I1: request caller lost its strong owner')
    if w.fused_calls > 1:
        errors.append('C6: fused stop invoked twice')
    if w.prestart_notified and not w.producer_stop_seen:
        errors.append('C6: pre-start notification not preserved')
    if (w.producer_claimed or w.producer_done) and not w.producer_started:
        errors.append('C6: completion of unstarted producer')
    if w.stop_obligation and not w.fused_calls:
        winner = w.fused_winner
        if winner != -1 and (not w.threads[winner] or w.threads[winner][0] != ('producer_fn',)):
            errors.append('C6: fused commitment lost dispatch')
    if w.cancellation and not w.stop_requested:
        errors.append('C6: cancellation lost stop obligation')
    return tuple(errors)


def terminal_violations(w):
    errors = list(state_violations(w))
    errors.extend(producer_registration_violations(w))
    if any(w.threads) or any(v != -1 for v in w.locks) or w.callback_thread != -1 or any(t != -1 for t in w.pthreads):
        errors.append('C8: wait cycle or unfinished thread')
    if w.obligation and not w.destructor_won and len(w.received) != 1:
        errors.append('C1: lost started receiver obligation')
    if w.producer_started and not w.producer_done:
        errors.append('C7: lost producer completion obligation')
    if w.stop_obligation and w.fused_calls != 1:
        errors.append('C6: lost fused stop obligation')
    if any(w.allocated) or any(w.refs + w.weak) or w.edges or w.wedges or w.resource_refs:
        errors.append('C4: allocation or ownership leak')
    if not C.closed and w.releases != 1:
        errors.append('C4: association not released exactly once')
    return tuple(errors)


def is_blocked(k):
    w = state(k)
    return any(s and not enabled(w, t, s[0]) for t, s in enumerate(w.threads))


def is_aborted(w):
    return False


def metrics():
    return {'max_store_buffer': 0}


def coverage():
    if not C.mut and not C.control:
        missing = set(C.required_coverage) - COV
        assert not missing, 'missing required coverage: ' + repr(sorted(missing))
    return sorted(COV)


def step_table():
    extra = [('source', str(i), ref, 'call site / allocation / construction / forwarding', ref) for i,ref in enumerate(EXTRA_REFS)]
    return extra + [('protocol', a, s.lines, 'atomic / protected plain / library / script', s.desc) for a, s in STEPS.items()]


def describe_initial(w):
    return 'allocations=%s refs=%s threads=%s' % (w.allocated, w.refs, w.threads)


def describe(meta, w):
    from engine.source_lock import resolve
    t, a, args = meta
    return ('T%d %s%s [%s] | phase=%s result=%s refs=%s weak=%s resource=%s edges=%s cb=%s locks=%s received=%s threads=%s'
            % (t, a, args, resolve(SOURCE.read_text(), STEPS[a].lines), w.phase, w.result,
               w.refs, w.weak, w.resource_refs, w.edges, w.callback_status, w.locks, w.received, w.threads))


def configure(cfg):
    global C, mem
    C = cfg
    COV.clear()
    mem = Memory(cfg, error)

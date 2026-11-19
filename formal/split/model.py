"""split.hpp: explicit owners, intrusive shared links and three mutex sections."""
import copy
from pathlib import Path
from types import SimpleNamespace
from engine.memory import Memory, put, change, synchronization, empty_sync

SOURCE = Path(__file__).resolve().parents[2] / 'include/forge/split.hpp'
C = None
COV = set()
REFS = {
    'make': '::split | auto shared = std::allocate_shared<ST>(alloc,',
    'connect': '__sender::connect | return __op<S, R>{__shared, std::move(r)};',
    'copy_sh': '__op::start | auto sh = __shared;',
    'copy_sub': '__op::start | auto sub = __sub;',
    'start_lock': '__op::start | std::unique_lock lk{st.mtx};',
    'check_done': '__op::start | if (st.phase == __shared_state<S>::Phase::done)',
    'link_read': '__op::start | if (st.tail)',
    'link_copy': '__op::start | else st.on_done = sub;',
    'tail': '__op::start | st.tail = sub.get();',
    'running': '__op::start | st.phase = __shared_state<S>::Phase::running;',
    'keep': '__op::start | st.keepalive = sh;',
    'source_start': '__op::start | std::execution::start(st.op);',
    'completion': '__inner_recv::set_value | auto st = __st->shared_from_this();',
    'store_lock': '__inner_recv::set_value | std::lock_guard lk{st->mtx};',
    'store': '__inner_recv::set_value | st->result.template emplace<1>(static_cast<Vs&&>(vs)...);',
    'done': '__inner_recv::set_value | st->phase = __shared_state<S>::Phase::done;',
    'notify': '__inner_recv::set_value | st->notify_all();',
    'detach_lock': '__shared_state::notify_all | std::lock_guard lk{mtx};',
    'detach': '__shared_state::notify_all | callbacks = std::move(on_done);',
    'clear_tail': '__shared_state::notify_all | tail = nullptr;',
    'pin': '__shared_state::notify_all | pin = std::move(keepalive);',
    'walk': '__shared_state::notify_all | auto next = std::move(callbacks->next);',
    'advance': '__shared_state::notify_all | callbacks = std::move(next);',
    'exchange': '__subscriber::deliver | active.exchange(false, std::memory_order_acq_rel)',
    'late_exchange': '::deliver_to_subscriber | sub->active.exchange(false, std::memory_order_acq_rel)',
    'destroy_op': '__op::~__op | __sub->active.store(false, std::memory_order_release);',
    'receive': 'model-only: deliver_result visit/apply lambda; cached const references',
    'node_alloc': 'model-only: __op constructor allocator read and allocate_shared mem-initializer',
}
ACTIONS = '''make copy_sender connect param_move node_alloc publish await drop_sender start copy_sh copy_sub start_lock check_done link_read link_copy tail idle running keep start_unlock source_start start_return completion store_lock store done store_unlock notify detach_lock detach clear_tail pin detach_unlock walk advance walk_end exchange late_exchange receive receiver_return destroy_op op_end release destroy state_op_end allocator_end state_end receiver_end node_end free signal after_op assign_ref after_operation'''.split()
STEPS = {a: SimpleNamespace(lines=REFS.get(a, 'model-only: shared ownership contract, mutex unlock, script or implicit teardown'), desc=a) for a in ACTIONS}
EXTRA_REFS = (
    '__inner_recv::set_error | auto st = __st->shared_from_this();',
    '__inner_recv::set_error | st->phase = __shared_state<S>::Phase::done;',
    '__inner_recv::set_stopped | st->result.template emplace<3>(__stopped_tag{});',
    '__inner_recv::set_stopped | st->phase = __shared_state<S>::Phase::done;',
    '__sender::connect | return __op<S, R>{std::move(__shared), std::move(r)};',
    '::split | auto alloc = __split_detail::__allocator_for_source<source_t>::get(sndr);',
)
FIELDS = '''threads errors owners refs alive allocated made destroyed freed receiver_alive receiver_ends op_alive op_made op_ends phase result head tail next mutex mutex_pub signals started source_starts source_running source_events expected received abandoned dtor_entry suppress_required exchange_won suppressed calls walking linking link_target launch vc views hist accesses allocator_alive allocator_ends source_alive source_ends'''.split()

class Cfg:
    def __init__(self, **kw):
        self.name = 'cfg'; self.threads = (); self.nodes = 1
        self.mode = 'own'; self.payload = 'value'; self.failure = ''
        self.actions = (); self.abandon = (); self.hb = False; self.stale = False
        self.mut = ''; self.control = ''; self.required_coverage = ()
        self.pause_destructor = False; self.required_evidence = ()
        self.__dict__.update(kw)

class World:
    pass

def key(w): return tuple(getattr(w, f) for f in FIELDS)
def state(k):
    w = World()
    for f, v in zip(FIELDS, k): setattr(w, f, v)
    return w

def error(w, e): w.errors += (e,)
def prepend(w, t, *ops): change(w, 'threads', t, tuple(ops) + w.threads[t])
def signal(w, t, name):
    d = dict(w.signals); d[name] = synchronization(w, t); w.signals = tuple(sorted(d.items()))
def edge(w, name): return dict(w.owners).get(name, -1)
def assign(w, name, obj):
    d = dict(w.owners)
    if obj == -1: d.pop(name, None)
    else:
        if name in d: error(w, 'SP4/5: overwriting an owned reference ' + name)
        d[name] = obj
    w.owners = tuple(sorted(d.items()))
def transfer(w, src, dst):
    obj = edge(w, src); assign(w, src, -1)
    if obj != -1: assign(w, dst, obj)
def inc(w, t, obj, name):
    if not w.alive[obj] or not w.refs[obj]: error(w, 'SP4/5: increment without live owner')
    assign(w, name, obj); change(w, 'refs', obj, w.refs[obj] + 1)
    mem.publish(w, t, obj, w.refs[obj], 'relaxed', rmw=True)
def touch(w, t, obj=0, write=False):
    if not w.alive[obj]: error(w, 'SP4: dead state' if obj == 0 else 'SP5: dead node ' + str(obj))
    mem.access(w, t, 'storage.' + str(obj), write)
def plain(w, t, name, write=False): touch(w, t); mem.access(w, t, name, write)
def optouch(w, t, i, write=False):
    if not w.op_alive[i]: error(w, 'SP6: operation access after destruction ' + str(i))
    mem.access(w, t, 'operation.' + str(i), write)
def rcvrtouch(w, t, i, write=False):
    touch(w, t, i + 1)
    if not w.receiver_alive[i]: error(w, 'SP5: dead receiver ' + str(i))
    mem.access(w, t, 'receiver.' + str(i), write)
def sh(i): return 'sh.' + str(i)
def sub(i): return 'sub.' + str(i)
def os(i): return 'op.state.' + str(i)
def on(i): return 'op.node.' + str(i)
def cb(t): return 'callbacks.' + str(t)
def nx(t): return 'walknext.' + str(t)
def st(t): return 'st.' + str(t)
def pin(t): return 'pin.' + str(t)
def link(i): return 'next.' + str(i)
def releases(*names): return tuple(('release', name) for name in names)

def initial():
    w = World(); n = len(C.threads); N = C.nodes; z = (0,) * n; b = (0,) * (1 + 2 * N)
    w.threads = C.threads; w.errors = (); w.owners = (); w.refs = (0,) * (N + 1)
    w.alive = w.allocated = w.made = (False,) * (N + 1)
    w.destroyed = w.freed = (0,) * (N + 1)
    w.receiver_alive = (False,) * N; w.receiver_ends = (0,) * N
    w.op_alive = w.op_made = (False,) * N; w.op_ends = (0,) * N
    w.phase = 'idle'; w.result = 'empty'; w.head = w.tail = -1; w.next = (-1,) * N
    w.mutex = -1; w.mutex_pub = (z, b); w.signals = (); w.accesses = ()
    w.started = (); w.source_starts = 0; w.source_running = False; w.source_events = 0; w.expected = ''
    w.received = ((),) * N; w.abandoned = w.dtor_entry = w.suppress_required = w.exchange_won = w.suppressed = (False,) * N
    w.calls = (); w.walking = (); w.linking = (); w.link_target = (-1,) * N; w.launch = (False,) * N
    w.allocator_alive = w.source_alive = False; w.allocator_ends = w.source_ends = 0
    w.vc = tuple(tuple(int(t == u) for u in range(n)) for t in range(n)); w.views = (b,) * n
    w.hist = tuple(((v, z, (z, b)),) for v in (0,) * (N + 1) + (True,) * N)
    return w

def lock(w, t):
    touch(w, t); mem.acquire(w, t, w.mutex_pub); w.mutex = t

def unlock(w, t):
    touch(w, t)
    if w.mutex != t: error(w, 'SP8: unlock without lock'); return
    w.mutex_pub = synchronization(w, t); w.mutex = -1

def enabled(w, t, op):
    a = op[0]
    if a == 'await': return op[1] in dict(w.signals)
    if a == 'start_lock' and len(op) == 2 and (C.mut == 'phase_before_lock' or C.control == 'detach_without_phase_guard'): return True
    if a in ('start_lock', 'store_lock'): return w.mutex == -1
    if a == 'detach_lock' and C.mut != 'detach_unlocked': return w.mutex == -1
    return True

def execute(w, t, op):
    a, *args = op; COV.add(a)
    if a in ('publish', 'signal'): signal(w, t, args[0] if args else 'ready')
    elif a == 'await': mem.acquire(w, t, dict(w.signals)[args[0]])
    elif a == 'make':
        # Construction is private until publish; no synthetic atomic release.
        change(w, 'allocated', 0, True)
        if C.failure == 'source_connect':
            change(w, 'freed', 0, 1); change(w, 'allocated', 0, False); COV.add('failure.source_connect')
        else:
            change(w, 'made', 0, True); change(w, 'alive', 0, True)
            change(w, 'refs', 0, 1); assign(w, 'sender.0', 0); w.allocator_alive = w.source_alive = True
    elif a == 'copy_sender': inc(w, t, 0, 'sender.' + str(args[0]))
    elif a == 'drop_sender':
        for i, u, path in w.calls:
            if u == t: COV.add('drop_sender_inline.' + path)
        prepend(w, t, ('release', 'sender.' + str(args[0])))
    elif a == 'connect':
        i, mode, sender = args; name = 'sender.' + str(sender)
        touch(w, t)
        if mode == 'move': transfer(w, name, 'param.' + str(i)); COV.add('connect.move')
        else: inc(w, t, 0, 'param.' + str(i)); COV.add('connect.copy')
        prepend(w, t, ('param_move', i))
    elif a == 'param_move':
        i = args[0]; transfer(w, 'param.' + str(i), os(i))
        prepend(w, t, ('node_alloc', i))
    elif a == 'node_alloc':
        i = args[0]; touch(w, t); mem.access(w, t, 'allocator')
        if edge(w, os(i)) != 0 or not w.allocator_alive: error(w, 'SP4: allocator read without state owner')
        if C.failure == 'node':
            COV.add('failure.node'); prepend(w, t, ('release', os(i)))
        else:
            obj = i + 1
            change(w, 'allocated', obj, True); change(w, 'made', obj, True); change(w, 'alive', obj, True)
            change(w, 'refs', obj, 1); assign(w, on(i), obj)
            change(w, 'receiver_alive', i, True); change(w, 'op_alive', i, True); change(w, 'op_made', i, True)
    elif a == 'start':
        i = args[0]; optouch(w, t, i); w.started += (i,)
        for j, u, path in w.calls:
            if u == t: COV.add('subscribe_inline.' + path)
        if C.control == 'drop_start': return
        if C.mut == 'member_start': prepend(w, t, ('start_lock', i))
        else: prepend(w, t, ('copy_sh', i), ('copy_sub', i), ('start_lock', i))
    elif a == 'copy_sh':
        i = args[0]; optouch(w, t, i); inc(w, t, 0, sh(i))
    elif a == 'copy_sub':
        i = args[0]; optouch(w, t, i); inc(w, t, i + 1, sub(i))
    elif a == 'start_lock':
        i = args[0]
        if (C.mut == 'phase_before_lock' or C.control == 'detach_without_phase_guard') and not args[1:]:
            plain(w, t, 'phase'); prepend(w, t, ('start_lock', i, w.phase)); return
        lock(w, t)
        prepend(w, t, ('check_done', i, *args[1:]))
    elif a == 'check_done':
        i = args[0]; observed = args[1] if args[1:] else w.phase
        if C.mut != 'phase_before_lock' and C.control != 'detach_without_phase_guard': plain(w, t, 'phase')
        if observed == 'done':
            COV.add('path.late')
            COV.add('late.after_detach' if 'detached' in dict(w.signals) else 'late.before_detach')
            ops = (('late_exchange', i), ('start_unlock', i)) if C.mut == 'late_locked' else (('start_unlock', i), ('late_exchange', i))
            prepend(w, t, *ops, ('start_return', i))
        else:
            COV.add('path.subscribe'); w.linking += (i,)
            COV.add('subscribe.after_event' if w.source_events else 'subscribe.before_event')
            if w.result != 'empty': COV.add('subscribe.after_store')
            prepend(w, t, *( (('idle', i),) if C.mut == 'link_after_unlock' else (('link_read', i), ('link_copy', i), ('tail', i), ('idle', i)) ))
    elif a == 'link_read':
        i = args[0]; plain(w, t, 'tail'); change(w, 'link_target', i, w.tail)
    elif a == 'link_copy':
        i = args[0]; target = w.link_target[i]
        if target == -1:
            plain(w, t, 'on_done', True); w.head = i
            inc(w, t, i + 1, 'assignment.link.' + str(i)); prepend(w, t, ('assign_ref', 'assignment.link.' + str(i), 'head'))
        else:
            touch(w, t, target + 1); mem.access(w, t, 'next.' + str(target), True)
            change(w, 'next', target, i); inc(w, t, i + 1, 'assignment.link.' + str(i))
            prepend(w, t, ('assign_ref', 'assignment.link.' + str(i), link(target)))
    elif a == 'tail':
        plain(w, t, 'tail', True); w.tail = args[0]; w.linking = tuple(i for i in w.linking if i != args[0])
    elif a == 'idle':
        i = args[0]; plain(w, t, 'phase'); change(w, 'launch', i, w.phase == 'idle')
        if w.phase == 'idle': prepend(w, t, ('running', i), ('keep', i))
        suffix = (('link_read', i), ('link_copy', i), ('tail', i)) if C.mut == 'link_after_unlock' else ()
        tail = (('source_start', i),) if w.launch[i] else ()
        rest = (('start_unlock', i),) + suffix + tail + (('start_return', i),)
        if C.mut == 'source_locked' and tail: rest = suffix + tail + (('start_unlock', i), ('start_return', i))
        # Append after running/keep, before the caller's continuation.
        offset = 2 if w.launch[i] else 0
        change(w, 'threads', t, w.threads[t][:offset] + rest + w.threads[t][offset:])
    elif a == 'running':
        if C.mut != 'no_running': plain(w, t, 'phase', True); w.phase = 'running'
    elif a == 'keep':
        if C.mut != 'no_keepalive':
            plain(w, t, 'keepalive', True); inc(w, t, 0, 'assignment.keep.' + str(args[0]))
            prepend(w, t, ('assign_ref', 'assignment.keep.' + str(args[0]), 'keepalive'))
    elif a == 'start_unlock': unlock(w, t)
    elif a == 'source_start':
        touch(w, t)
        if not w.source_alive: error(w, 'SP4: dead source operation')
        mem.access(w, t, 'source operation'); w.source_starts += 1; w.source_running = True
        COV.add('launcher.' + str(args[0])); signal(w, t, 'source_ready')
        if C.mode == 'inline': prepend(w, t, ('completion',))
    elif a == 'start_return':
        i = args[0]; signal(w, t, 'start_return.' + str(i))
        if C.mut != 'member_start': prepend(w, t, *releases(sub(i), sh(i)))
        # No access to operation members after a completion call.
    elif a == 'completion':
        # Independent source event: even drop_completion keeps outcome/obligation.
        w.source_events += 1; w.expected = 'error' if C.payload == 'throw' else C.payload
        w.source_running = False; COV.add('completion.' + C.mode)
        if tuple(name for name, obj in w.owners if obj == 0) == ('keepalive',): COV.add('keepalive.only')
        touch(w, t)
        if not w.source_alive: error(w, 'SP4: dead source operation')
        mem.access(w, t, 'source operation')
        if C.control == 'drop_completion': return
        if C.mut != 'no_completion_pin' or C.payload not in ('value', 'throw'): inc(w, t, 0, st(t))
        else:
            if not w.refs[0]: error(w, 'SP4: shared_from_this without owner')
        prepend(w, t, ('store_lock',), ('store',), ('done',), ('store_unlock',), ('notify',), ('release', st(t)))
    elif a == 'store_lock': lock(w, t)
    elif a == 'store':
        plain(w, t, 'result', True)
        if C.payload == 'throw' and C.mut == 'no_catch': error(w, 'SP2: noexcept value caching terminates')
        if C.control != 'drop_store': w.result = 'error' if C.payload == 'throw' else C.payload
    elif a == 'done':
        if C.mut == 'done_unlocked' and C.payload in ('value', 'throw') and w.mutex == t:
            # Move only the phase write after the original unlock.
            assert w.threads[t][0] == ('store_unlock',)
            change(w, 'threads', t, w.threads[t][1:]); prepend(w, t, ('store_unlock',), ('done',)); return
        plain(w, t, 'phase', True); w.phase = 'done'
        signal(w, t, 'stored')
    elif a == 'store_unlock':
        if C.mut == 'notify_in_store' and C.payload in ('value', 'throw') and w.mutex == t:
            assert w.threads[t][0] == ('notify',)
            change(w, 'threads', t, w.threads[t][1:]); prepend(w, t, ('notify',), ('detach_unlock',)); return
        unlock(w, t)
    elif a == 'notify':
        touch(w, t); w.walking += (t,)
        prepend(w, t, ('detach_lock',), ('detach',), ('clear_tail',), ('pin',), ('detach_unlock',), ('walk',), ('walk_end',), ('release', pin(t)))
    elif a == 'detach_lock':
        if C.mut != 'detach_unlocked': lock(w, t)
    elif a == 'detach':
        plain(w, t, 'on_done', True)
        count = 0; cursor = w.head; seen = set()
        while cursor != -1 and cursor not in seen:
            seen.add(cursor); count += 1; cursor = w.next[cursor]
        COV.add('detach.size' + str(count))
        transfer(w, 'head', cb(t)); w.head = -1
        signal(w, t, 'detached')
    elif a == 'clear_tail': plain(w, t, 'tail', True); w.tail = -1
    elif a == 'pin':
        if C.mut != 'retain_keepalive':
            plain(w, t, 'keepalive', True); transfer(w, 'keepalive', pin(t))
        if C.control == 'pin_cover_removed': prepend(w, t, ('release', pin(t)))
    elif a == 'detach_unlock':
        if C.mut == 'deliver_locked' and w.mutex == t:
            # Delay just this unlock until the notify walk has returned.
            script = w.threads[t]; idx = next(i for i, v in enumerate(script) if v[0] == 'walk_end')
            change(w, 'threads', t, script[:idx + 1] + (('start_unlock', -1),) + script[idx + 1:])
        elif C.mut != 'detach_unlocked': unlock(w, t)
    elif a == 'walk':
        obj = edge(w, cb(t))
        if obj != -1:
            i = obj - 1; touch(w, t, obj); mem.access(w, t, 'next.' + str(i), True)
            transfer(w, link(i), nx(t)); change(w, 'next', i, -1)
            prepend(w, t, ('exchange', i), ('advance',), ('walk',))
    elif a == 'advance':
        # shared_ptr move-assignment swaps in next before releasing the old node.
        transfer(w, cb(t), 'old.callbacks.' + str(t)); transfer(w, nx(t), cb(t))
        prepend(w, t, ('release', 'old.callbacks.' + str(t)))
    elif a == 'walk_end': w.walking = tuple(u for u in w.walking if u != t)
    elif a in ('exchange', 'late_exchange'):
        i = args[0]; obj = i + 1; touch(w, t, obj)
        # Independent obligation at the destructor store's position in MO,
        # including when the store is omitted and op_end has not happened.
        change(w, 'suppressed', i, w.suppress_required[i])
        active = w.hist[1 + C.nodes + i][-1][0]
        mem.publish(w, t, 1 + C.nodes + i, False, 'acq_rel', rmw=True)
        if w.suppressed[i] and active:
            error(w, 'AB: exchange admits delivery after destructor store position')
            if not w.abandoned[i]: COV.add('failure.suppression_in_destructor')
        if w.suppressed[i] and not w.abandoned[i]: COV.add('exchange.during_destructor')
        if C.pause_destructor: signal(w, t, 'exchanged.' + str(i))
        if active:
            path = ('nested_late' if any(v[1] == t for v in w.calls) else 'late') if a == 'late_exchange' else ('sync' if C.mode == 'inline' else 'notify')
            change(w, 'exchange_won', i, True); prepend(w, t, ('receive', i, path), ('receiver_return', i))
        else: COV.add('suppressed.' + str(i))
    elif a == 'receive':
        i = args[0]; touch(w, t); rcvrtouch(w, t, i); mem.access(w, t, 'result')
        if w.result != w.expected or not w.source_events: error(w, 'SP2: cached result differs from source event')
        if w.suppressed[i]: error(w, 'AB: delivery after destructor won')
        if C.control == 'literal_abandonment' and w.dtor_entry[i]: error(w, 'AB: destructor entry forbids later receiver entry')
        path = args[1]; COV.add('delivery.' + path)
        for j, u, parent_path in w.calls:
            if u == t: COV.add('reentry.' + parent_path + '.' + path)
        w.calls += ((i, t, path),)
        if C.control != 'drop_receiver': change(w, 'received', i, w.received[i] + (w.result,))
        signal(w, t, 'receiver.' + str(i))
        actions = C.actions[i] if i < len(C.actions) else ('destroy',)
        ops = []
        for action in actions:
            if action == 'destroy': ops.append(('destroy_op', i))
            elif action.startswith('subscribe:'):
                j = int(action.split(':')[1]); ops += [('connect', j, 'copy', 0), ('start', j)]
            elif action.startswith('drop:'): ops.append(('drop_sender', int(action.split(':')[1])))
        prepend(w, t, *ops)
    elif a == 'receiver_return':
        i = args[0]; touch(w, t); rcvrtouch(w, t, i); mem.access(w, t, 'result')
        w.calls = tuple(v for v in w.calls if v[:2] != (i, t))
        if C.control == 'after_op_access': prepend(w, t, ('after_operation', i))
        signal(w, t, 'receiver_return.' + str(i))
    elif a == 'destroy_op':
        i = args[0]
        for j, u, path in w.calls:
            if j == i and u == t: COV.add('self_destroy.' + path)
        optouch(w, t, i); change(w, 'dtor_entry', i, True)
        change(w, 'suppress_required', i, True)
        if C.mut != 'no_active_store': mem.publish(w, t, 1 + C.nodes + i, False, 'release')
        pause = ()
        if C.pause_destructor:
            signal(w, t, 'store_position.' + str(i)); pause = (('await', 'exchanged.' + str(i)),)
        prepend(w, t, *pause, *releases(on(i), os(i)), ('op_end', i))
    elif a == 'op_end':
        i = args[0]; optouch(w, t, i, True)
        change(w, 'abandoned', i, True)
        if edge(w, os(i)) != -1 or edge(w, on(i)) != -1: error(w, 'SP6: destroyed operation retains member reference')
        if C.control != 'skip_op_end_' + str(i): change(w, 'op_alive', i, False); change(w, 'op_ends', i, w.op_ends[i] + 1)
        signal(w, t, 'destroyed.' + str(i)); COV.add('op_end.' + str(t))
    elif a == 'release':
        name = args[0]; obj = edge(w, name)
        if obj == -1: return
        if C.control == 'skip_release_' + name: return
        assign(w, name, -1); change(w, 'refs', obj, w.refs[obj] - 1)
        order = 'relaxed' if C.control == 'relaxed_release' else 'acq_rel'
        mem.publish(w, t, obj, w.refs[obj], order, rmw=True)
        if not w.refs[obj]:
            if name.startswith('st.'): COV.add('state_destroy.completion')
            prepend(w, t, ('destroy', obj))
    elif a == 'destroy':
        obj = args[0]
        if w.refs[obj] or any(v == obj for _, v in w.owners): error(w, 'SP4/5: destruction with owner')
        if obj == 0:
            if w.source_running or w.walking or w.calls: error(w, 'SP4: state destruction during protected call')
            if w.head != -1 or edge(w, 'head') != -1: error(w, 'SP4: state destruction with subscriber list')
            prepend(w, t, ('state_op_end',), ('allocator_end',), ('state_end',), ('free', 0))
            COV.add('state_destroy.T' + str(t))
        else:
            i = obj - 1
            if any(v[0] == i for v in w.calls): error(w, 'SP5: node destruction inside receiver call')
            prepend(w, t, ('receiver_end', i), ('release', link(i)), ('node_end', i), ('free', obj))
    elif a == 'state_op_end':
        touch(w, t); mem.access(w, t, 'source operation', True)
        if w.source_running: error(w, 'SP4: live inner operation destroyed')
        if not w.source_alive: error(w, 'SP4: repeated source storage end')
        if C.control != 'skip_source_end': w.source_alive = False; w.source_ends += 1
    elif a == 'allocator_end':
        touch(w, t); mem.access(w, t, 'allocator', True)
        if C.control != 'skip_allocator_end': w.allocator_alive = False; w.allocator_ends += 1
    elif a == 'state_end':
        if w.refs[0] or any(obj == 0 for _, obj in w.owners): error(w, 'SP4: state storage ends with owners')
        if w.source_alive or w.allocator_alive: error(w, 'SP4: state storage ends before members')
        if w.head != -1 or w.tail != -1: error(w, 'SP4: state storage ends with list')
        touch(w, t, 0, True)
        for name in ('result', 'phase', 'on_done', 'tail', 'keepalive'): mem.access(w, t, name, True)
        if C.control != 'skip_state_end': change(w, 'alive', 0, False); change(w, 'destroyed', 0, w.destroyed[0] + 1)
    elif a == 'receiver_end':
        i = args[0]; rcvrtouch(w, t, i, True); COV.add('receiver_end.T' + str(t))
        if C.control != 'skip_receiver_end_' + str(i):
            change(w, 'receiver_alive', i, False); change(w, 'receiver_ends', i, w.receiver_ends[i] + 1)
    elif a == 'node_end':
        i = args[0]
        if w.refs[i + 1] or any(obj == i + 1 for _, obj in w.owners): error(w, 'SP5: node storage ends with owners')
        if w.receiver_alive[i] or edge(w, link(i)) != -1: error(w, 'SP5: node storage ends before members')
        touch(w, t, i + 1, True); mem.access(w, t, 'next.' + str(i), True)
        if C.control != 'skip_node_end_' + str(i):
            change(w, 'alive', i + 1, False); change(w, 'destroyed', i + 1, w.destroyed[i + 1] + 1)
    elif a == 'free':
        obj = args[0]
        if w.alive[obj] or obj and w.receiver_alive[obj - 1]: error(w, 'SP4/5: deallocation before storage end')
        if C.control != 'skip_free_' + str(obj): change(w, 'allocated', obj, False); change(w, 'freed', obj, w.freed[obj] + 1)
    elif a == 'assign_ref':
        src, dst = args
        trash = 'old.' + src
        transfer(w, dst, trash); transfer(w, src, dst)
        prepend(w, t, ('release', trash))
    elif a == 'after_operation': optouch(w, t, args[0])
    elif a == 'after_op': transfer(w, nx(t), cb(t))
    else: raise ValueError(op)

def successors(k):
    w = state(k)
    if w.errors: return []
    out = []
    for t, script in enumerate(w.threads):
        if not script or not enabled(w, t, script[0]): continue
        v = copy.copy(w); change(v, 'threads', t, script[1:])
        if C.hb: change(v, 'vc', t, put(v.vc[t], t, v.vc[t][t] + 1))
        execute(v, t, script[0]); out.append(((t, script[0]), v))
    return out

def state_violations(w):
    e = list(w.errors)
    for obj, count in enumerate(w.refs):
        if count < 0 or count != sum(v == obj for _, v in w.owners): e.append('SP4/5: count differs from owners ' + str(obj))
        if w.destroyed[obj] > 1 or w.freed[obj] > 1: e.append('SP4/5: repeated destruction ' + str(obj))
        if not w.alive[obj] and count: e.append('SP4/5: owners of dead object ' + str(obj))
    if w.phase == 'done' and w.linking:
        e.append('SP2: done gate covering invariant violated'); COV.add('failure.done_gate')
    if w.source_starts > 1 or w.source_starts and not w.started: e.append('SP1: source is not lazy single start')
    if w.source_events > 1: e.append('SP2: repeated source completion')
    for i in range(C.nodes):
        if len(w.received[i]) > 1: e.append('SP2: duplicate receiver ' + str(i))
        if w.op_ends[i] > 1 or w.receiver_ends[i] > 1: e.append('SP5/6: repeated storage end')
    # Cover the lifetime, not a particular owner kind. Even an operation or
    # sender is sufficient while it remains alive; recheck after every release.
    if w.walking or w.calls:
        if not w.alive[0] or not any(obj == 0 for name, obj in w.owners):
            e.append('SP4: state lifetime unowned during protected call')
    for i, t, path in w.calls:
        if not w.alive[i + 1] or not w.receiver_alive[i] or not any(obj == i + 1 for name, obj in w.owners):
            e.append('SP5: receiver node lifetime unowned during call ' + str(i))
    return tuple(e)

def terminal_violations(w):
    e = list(state_violations(w))
    if any(w.threads): e.append('SP8: self-deadlock or unfinished script')
    if w.source_starts != int(bool(w.started)): e.append('SP1: lost source start obligation')
    if w.source_starts and w.source_events != 1: e.append('SP2: lost source completion obligation')
    for i in w.started:
        if not w.received[i] and not (i in C.abandon and w.abandoned[i] and not w.exchange_won[i]): e.append('SP2: lost receiver obligation ' + str(i))
    for obj in range(C.nodes + 1):
        if w.alive[obj] or w.allocated[obj] or w.refs[obj] or any(v == obj for _, v in w.owners): e.append('SP7: leaked object ' + str(obj))
        if w.destroyed[obj] != int(w.made[obj]): e.append('SP4/5: storage end obligation ' + str(obj))
        expected_free = int(w.made[obj]) + int(obj == 0 and C.failure == 'source_connect')
        if w.freed[obj] != expected_free: e.append('SP7: allocation obligation ' + str(obj))
    for i in range(C.nodes):
        if w.op_alive[i] or w.op_ends[i] != int(w.op_made[i]): e.append('SP6: operation storage obligation ' + str(i))
        if w.receiver_alive[i] or w.receiver_ends[i] != int(w.made[i + 1]): e.append('SP5: receiver storage obligation ' + str(i))
    if w.source_alive or w.source_ends != int(w.made[0]): e.append('SP4: source storage obligation')
    if w.allocator_alive or w.allocator_ends != int(w.made[0]): e.append('SP4: allocator storage obligation')
    if edge(w, 'keepalive') != -1: e.append('SP7: lingering keepalive')
    return tuple(e)

def is_blocked(k):
    w = state(k); return any(s and not enabled(w, t, s[0]) for t, s in enumerate(w.threads))
def is_aborted(w): return False
def metrics(): return {'max_store_buffer': 0}
def coverage():
    if not C.mut and not C.control: assert set(C.required_coverage) <= COV, set(C.required_coverage) - COV
    assert set(C.required_evidence) <= COV, set(C.required_evidence) - COV
    return sorted(COV)
def step_table():
    return [('protocol', a, s.lines, 'atomic / plain / library / script', s.desc) for a, s in STEPS.items()] + [('source', str(i), r, 'forwarding', r) for i, r in enumerate(EXTRA_REFS)]
def describe_initial(w): return 'threads=' + repr(w.threads)
def describe(meta, w):
    from engine.source_lock import resolve
    return '%s [%s] phase=%s result=%s refs=%s owners=%s list=%s/%s/%s alive=%s scripts=%s' % (meta, resolve(SOURCE.read_text(), STEPS[meta[1][0]].lines), w.phase, w.result, w.refs, w.owners, w.head, w.tail, w.next, w.alive, w.threads)
def configure(cfg):
    global C, mem
    C = cfg; COV.clear(); mem = Memory(cfg, error)

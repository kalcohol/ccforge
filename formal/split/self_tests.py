"""Independent oracle, metadata and ownership regressions."""
import ast
from pathlib import Path
from engine.oracle import check, fixtures
from . import model as m, tiers
from .mutants import CATALOG, CONTROLS, failure_manifest, KILLERS
from .scenarios import SCENARIOS

def run():
    protected = {'started', 'source_events', 'expected', 'made', 'op_made', 'abandoned', 'dtor_entry', 'suppress_required', 'suppressed'}
    check(ast.parse(Path(m.__file__).read_text()), protected); fixtures(protected)
    tasks = tiers.full(); names = {t['name'] for t in tasks}
    assert len(names) == len(tasks) and failure_manifest() <= names
    assert all(1 <= len(tiers.configuration(t).threads) <= 5 for t in tasks)
    for mid in CATALOG:
        assert {(t['scen'], t['model']) for t in tasks if t['mut'] == mid} == {(s, mode) for s in SCENARIOS for mode in ('sc', 'sc_hb')}
    assert {t['mut'] for t in tiers.select('quick')} >= set(CATALOG)
    assert {t['control'] for t in tiers.select('quick')} >= set(CONTROLS)
    assert set(KILLERS) == set(CATALOG)
    for mid in CATALOG:
        assert {t['name'] for t in tasks if t['mut'] == mid and t['name'] in failure_manifest()} == {mid + '.' + s for s in KILLERS[mid]}
        assert bool(KILLERS[mid]) == (CATALOG[mid]['outcome'] == 'killed')
    from engine.source_lock import resolve
    for row in m.step_table(): resolve(m.SOURCE.read_text(), row[2])
    m.configure(m.Cfg(threads=((), ())))
    w = m.initial()
    for a in m.STEPS: assert a in m.describe((0, (a,)), w)
    # Every executable action has metadata, including library and teardown steps.
    tree = ast.parse(Path(m.__file__).read_text())
    actions = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Compare) and isinstance(node.left, ast.Name) and node.left.id == 'a':
            for rhs in node.comparators:
                values = rhs.elts if isinstance(rhs, (ast.Tuple, ast.List, ast.Set)) else (rhs,)
                actions.update(v.value for v in values if isinstance(v, ast.Constant) and isinstance(v.value, str))
    assert actions <= set(m.STEPS), actions - set(m.STEPS)
    # Obligations cannot be discharged by removing the transition that creates
    # the actual result, start, receiver call, storage end or deallocation.
    w = m.initial(); w.started = (0,); w.source_events = 1; w.expected = 'value'
    errors = m.terminal_violations(w)
    assert 'SP1: lost source start obligation' in errors
    assert 'SP2: lost receiver obligation 0' in errors
    for obj in range(2):
        w = m.initial(); m.change(w, 'made', obj, True)
        assert 'SP4/5: storage end obligation ' + str(obj) in m.terminal_violations(w)
        assert 'SP7: allocation obligation ' + str(obj) in m.terminal_violations(w)
    # Moves do not modify either atomic history. Copies are relaxed RMWs;
    # releases acquire only their object's release sequence and run locally.
    m.configure(m.Cfg(threads=((), ()), stale=True))
    w = m.initial(); m.execute(w, 0, ('make',))
    before = w.hist; m.transfer(w, 'sender.0', 'moved'); assert w.hist == before
    m.inc(w, 0, 0, 'copy')
    assert w.hist[0][-1][2] == m.empty_sync(w)
    m.execute(w, 0, ('release', 'copy'))
    assert w.hist[0][-1][2][0] == w.vc[0]
    assert w.refs[0] == 1 and not any(op[0] == 'destroy' for op in w.threads[0])
    m.execute(w, 1, ('release', 'moved'))
    assert w.threads[1][0] == ('destroy', 0)
    # Count and owner checks are independent of the decrement implementation.
    w = m.initial(); m.change(w, 'refs', 0, 1)
    assert 'SP4/5: count differs from owners 0' in m.state_violations(w)
    # shared_from_this cannot resurrect a zero-count state, even in the window
    # before its destructor has been scheduled.
    w = m.initial(); m.change(w, 'alive', 0, True); m.inc(w, 0, 0, 'st.0')
    assert 'SP4/5: increment without live owner' in w.errors
    # Each managed storage has its own lifetime write, not just a global flag.
    for obj in (0, 1, 2, 3):
        m.configure(m.Cfg(threads=((), ()), nodes=3, hb=True))
        w = m.initial(); m.change(w, 'alive', obj, True)
        if obj: m.change(w, 'receiver_alive', obj - 1, True)
        m.touch(w, 0, obj)
        m.execute(w, 1, ('state_end',) if obj == 0 else ('node_end', obj - 1))
        assert 'HB: unordered access to storage.' + str(obj) in w.errors
        assert not w.alive[obj]
    # Every reference is checked separately, per managed identity. A phantom
    # owner cannot hide a missing per-object destruction at terminal states.
    for i in range(3):
        for name, obj in ((m.os(i), 0), (m.on(i), i + 1), (m.sh(i), 0), (m.sub(i), i + 1), (m.link(i), (i + 1) % 3 + 1)):
            w = m.initial(); m.assign(w, name, obj)
            assert 'SP4/5: count differs from owners ' + str(obj) in m.state_violations(w)
            assert 'SP7: leaked object ' + str(obj) in m.terminal_violations(w)
    # Strong abandonment is intentionally false: an exchange can win first,
    # then the destructor can store false, then receiver entry is still legal.
    for control in ('', 'literal_abandonment'):
        m.configure(m.Cfg(threads=((),), control=control, actions=((),)))
        w = m.initial(); m.execute(w, 0, ('make',)); m.execute(w, 0, ('connect', 0, 'copy', 0))
        m.execute(w, 0, ('param_move', 0)); m.execute(w, 0, ('node_alloc', 0))
        w.source_events = 1; w.expected = w.result = 'value'
        m.execute(w, 0, ('exchange', 0)); m.execute(w, 0, ('destroy_op', 0)); m.execute(w, 0, ('receive', 0, 'notify'))
        assert bool(w.errors) == bool(control)
    # The obligation is recorded at the store position, independently of whether
    # the mutant performs the store, and is checked before destructor return.
    for mutant in ('', 'no_active_store'):
        m.configure(m.Cfg(threads=((),), mut=mutant))
        w = m.initial(); m.execute(w, 0, ('make',)); m.execute(w, 0, ('connect', 0, 'copy', 0))
        m.execute(w, 0, ('param_move', 0)); m.execute(w, 0, ('node_alloc', 0))
        m.execute(w, 0, ('destroy_op', 0))
        assert w.suppress_required[0] and not w.abandoned[0]
        m.execute(w, 0, ('exchange', 0))
        assert bool(w.errors) == bool(mutant)
        assert w.suppressed[0]
    # The destructor is a release store, not a synthetic exchange/acquire.
    m.configure(m.Cfg(threads=((), ()), stale=True))
    w = m.initial(); m.execute(w, 0, ('destroy_op', 0))
    assert len(w.hist[2]) == 2 and w.hist[2][-1][0] is False
    assert w.vc[0] == (1, 0)
    # Declaration/unwind order: parameter is consumed before allocation, and
    # __sub is released before __shared; separate decrements are retained.
    for mode in ('move', 'copy'):
        m.configure(m.Cfg(threads=((),), failure='node'))
        w = m.initial(); m.execute(w, 0, ('make',)); m.execute(w, 0, ('connect', 0, mode, 0))
        assert (m.edge(w, 'sender.0') == -1) == (mode == 'move')
        m.execute(w, 0, ('param_move', 0)); m.execute(w, 0, ('node_alloc', 0))
        assert w.threads[0][0] == ('release', m.os(0))
        assert not w.op_made[0] and not w.made[1]
    m.configure(m.Cfg(threads=((),)))
    w = m.initial(); m.execute(w, 0, ('destroy_op', 0))
    assert w.threads[0][:3] == (('release', m.on(0)), ('release', m.os(0)), ('op_end', 0))

    # The outcome is a source-event oracle even when dispatch/storage is omitted.
    for variant in ('', 'drop_completion', 'drop_store'):
        m.configure(m.Cfg(threads=((),), control=variant, payload='throw'))
        w = m.initial(); m.execute(w, 0, ('make',)); m.execute(w, 0, ('completion',))
        assert w.expected == 'error' and w.source_events == 1 and w.result == 'empty'
    # Both survivor protections are invariants, not special passing branches.
    for mutant in ('', 'no_completion_pin', 'detach_unlocked'):
        m.configure(m.Cfg(threads=((),), mut=mutant))
        w = m.initial(); w.walking = (0,)
        assert 'SP4: state lifetime unowned during protected call' in m.state_violations(w)
        w.walking = (); w.phase = 'done'; w.linking = (0,)
        assert 'SP2: done gate covering invariant violated' in m.state_violations(w)
    # Cover actual ownership for the state AND the receiver node, regardless of
    # kind. An operation or sender can protect a call; losing either last owner
    # must fail even before the last-releaser destructor is scheduled.
    for owner in ('op.state.0', 'sender.0'):
        m.configure(m.Cfg(threads=((),)))
        w = m.initial(); w.calls = ((0, 0, 'late'),)
        for obj in (0, 1): m.change(w, 'alive', obj, True); m.change(w, 'refs', obj, 1)
        m.change(w, 'receiver_alive', 0, True)
        m.assign(w, owner, 0); m.assign(w, 'op.node.0', 1)
        assert not m.state_violations(w)
        m.assign(w, owner, -1); m.change(w, 'refs', 0, 0)
        assert 'SP4: state lifetime unowned during protected call' in m.state_violations(w)
        m.assign(w, owner, 0); m.change(w, 'refs', 0, 1)
        m.assign(w, 'op.node.0', -1); m.change(w, 'refs', 1, 0)
        assert 'SP5: receiver node lifetime unowned during call 0' in m.state_violations(w)
    # Omitting the keepalive assignment removes its memory access as well.
    for mutant in ('', 'retain_keepalive'):
        m.configure(m.Cfg(threads=((),), hb=True, mut=mutant))
        w = m.initial(); m.execute(w, 0, ('make',)); m.execute(w, 0, ('pin',))
        assert any(name == 'keepalive' for name, _ in w.accesses) == (not mutant)
    # The after-store label requires an actual result store, not source entry.
    m.configure(m.Cfg(threads=((),)))
    w = m.initial(); m.execute(w, 0, ('make',)); w.source_events = 1
    m.execute(w, 0, ('check_done', 0))
    assert 'subscribe.after_event' in m.COV and 'subscribe.after_store' not in m.COV
    m.execute(w, 0, ('store',)); m.execute(w, 0, ('check_done', 0))
    assert 'subscribe.after_store' in m.COV
    # State/member/node ends independently reject live references and members.
    m.configure(m.Cfg(threads=((),)))
    w = m.initial(); m.execute(w, 0, ('make',)); m.execute(w, 0, ('state_end',))
    assert 'SP4: state storage ends with owners' in w.errors
    assert 'SP4: state storage ends before members' in w.errors
    w = m.initial(); m.execute(w, 0, ('make',)); m.execute(w, 0, ('connect', 0, 'copy', 0))
    m.execute(w, 0, ('param_move', 0)); m.execute(w, 0, ('node_alloc', 0)); m.execute(w, 0, ('node_end', 0))
    assert 'SP5: node storage ends with owners' in w.errors
    assert 'SP5: node storage ends before members' in w.errors
    w = m.initial(); w.source_alive = True
    assert 'SP4: source storage obligation' in m.terminal_violations(w)
    w.source_alive = False; w.allocator_alive = True
    assert 'SP4: allocator storage obligation' in m.terminal_violations(w)
    # Completion mutants edit set_value only. The independently written error
    # and stopped functions retain their own pins, mutex and notify placement.
    for payload in ('value', 'throw', 'error', 'stopped'):
        value_path = payload in ('value', 'throw')
        m.configure(m.Cfg(threads=((),), payload=payload, mut='no_completion_pin'))
        w = m.initial(); m.execute(w, 0, ('make',)); m.execute(w, 0, ('completion',))
        assert (m.edge(w, m.st(0)) == -1) == value_path
        m.configure(m.Cfg(threads=((),), payload=payload, mut='done_unlocked'))
        w = m.initial(); m.execute(w, 0, ('make',)); w.mutex = 0
        w.threads = ((('store_unlock',), ('notify',)),)
        m.execute(w, 0, ('done',))
        assert (w.phase == 'idle') == value_path
        m.configure(m.Cfg(threads=((),), payload=payload, mut='notify_in_store'))
        w = m.initial(); m.execute(w, 0, ('make',)); w.mutex = 0
        w.threads = ((('notify',),),)
        m.execute(w, 0, ('store_unlock',))
        assert (w.mutex == 0) == value_path

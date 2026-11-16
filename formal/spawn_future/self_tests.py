"""Fast protocol regressions and independent obligation checks."""
import ast
from . import model as m
from . import tiers
from .mutants import CATALOG, CONTROLS, failure_manifest
from .scenarios import SCENARIOS


# Direct w.<field> writes only: this is a syntax regression, not dataflow or
# alias analysis. Discharge fields are deliberately outside this protected set.
ORACLE_FIELDS = frozenset(('expected', 'consumer_expected', 'receiver_expected',
    'obligation', 'producer_started', 'stop_obligation', 'cancellation',
    'prestart_notified', 'request_owners'))


def check_oracle_assignments(tree):
    def variant(node):
        return any(isinstance(n, ast.Attribute) and isinstance(n.value, ast.Name)
                   and n.value.id == 'C' and n.attr in ('mut', 'control') for n in ast.walk(node))

    def protected(node):
        return any(isinstance(n, ast.Attribute) and isinstance(n.ctx, ast.Store)
                   and isinstance(n.value, ast.Name) and n.value.id == 'w'
                   and n.attr in ORACLE_FIELDS for n in ast.walk(node))

    for node in ast.walk(tree):
        if isinstance(node, ast.If) and variant(node.test):
            assert not any(protected(child) for child in node.body + node.orelse), node.lineno
        if isinstance(node, (ast.Assign, ast.AnnAssign, ast.AugAssign, ast.NamedExpr)) and protected(node):
            assert node.value is None or not variant(node.value), node.lineno


def run():
    tasks = tiers.full()
    names = {t['name'] for t in tasks}
    assert len(names) == len(tasks)
    assert failure_manifest() <= names
    assert {t['mut'] for t in tiers.select('quick')} >= set(CATALOG)
    assert {t['control'] for t in tiers.select('quick')} >= set(CONTROLS)
    for item in tasks:
        assert 1 <= len(tiers.configuration(item).threads) <= 5
    assert {s[0] for s in SCENARIOS} >= set('abcdefghrx')
    # Start creates a completion obligation even if no delivery transition
    # ever runs. Terminal checking must not depend on receive code executing.
    m.configure(m.Cfg(threads=((),)))
    w = m.initial()
    w.obligation = w.started = True
    assert any('lost started' in e for e in m.terminal_violations(w))
    w.destructor_won = True
    assert not any('lost started' in e for e in m.terminal_violations(w))
    w.producer_started = True
    assert any('lost producer' in e for e in m.terminal_violations(w))
    # Expired weak lock does not create an owner or revive an object.
    m.configure(m.Cfg(threads=((),)))
    w = m.initial()
    m.execute(w, 0, ('weak_lock',))
    assert not w.edges and w.refs == (0,0)
    assert 'weak_lock.expired' in m.COV
    # Last strong destruction leaves the CB alive while explicit weak owners
    # exist. The implicit and enable_shared_from_this weak owners are distinct.
    m.execute(w, 0, ('launch',))
    assert w.weak == (2,0)
    assert dict(w.wedges) == {'esft': 0, 'implicit.state': 0}
    m.execute(w, 0, ('weak_copy', 'external', 0))
    assert w.weak[0] == 3
    m.execute(w, 0, ('drop', 'future'))
    assert w.refs[0] == 0 and w.cb_alive[0] and w.allocated[1]
    assert not m.state_violations(w)
    # A live enable_shared_from_this copy is relaxed, a decrement acq_rel.
    m.configure(m.Cfg(threads=((),), stale=True))
    w = m.initial()
    m.execute(w, 0, ('launch',))
    m.change(w, 'vc', 0, (3,))
    m.execute(w, 0, ('copy', 'pin', 0))
    assert w.hist[m.SS][-1][1] == (3,)
    assert not any(w.hist[m.SS][-1][2][0])
    m.execute(w, 0, ('drop', 'pin'))
    assert w.hist[m.SS][-1][2][0] == (3,)
    # Moves do not introduce atomic modifications.
    hist = w.hist
    m.execute(w, 0, ('move', 'future', 'moved'))
    assert w.hist == hist and m.edge(w, 'moved') == 0
    # Three mutexes are independent; nested owner -> operation acquisition is
    # retained in the scripts. Unlock is never merged with protected writes.
    m.configure(m.Cfg(threads=((),())))
    w = m.initial()
    m.execute(w, 0, ('launch',))
    m.execute(w, 0, ('lock', 0))
    assert not m.enabled(w, 1, ('lock', 0))
    assert m.enabled(w, 1, ('lock', 1))
    m.execute(w, 0, ('unlock', 0))
    assert m.enabled(w, 1, ('lock', 0))
    # Deregistration waits for another thread, but not for self; an inline
    # registration is also removable without a subsequent source dependency.
    w.callback_thread, w.callback_status = 0, 'executing'
    assert not m.enabled(w, 1, ('deregister',))
    assert m.enabled(w, 0, ('deregister',))
    # Already-consumed is a defensive internal branch: public move-only sender
    # scripts never start twice. Exercise the branch without inventing a legal
    # second public consumer operation.
    w.taken = True
    w.threads = ((('unlock', 0),), ())
    m.execute(w, 0, ('consume_body',))
    assert ('deliver_stopped', 'start_consumer.0') in w.threads[0]
    assert 'consume.already_consumed' in m.COV
    # The stop query permits coherent stale reads, then registration checks
    # stop atomically and invokes inline even when the query read false.
    m.configure(m.Cfg(threads=((),()), stale=True))
    w = m.initial()
    m.change(w, 'vc', 0, (2,0))
    m.execute(w, 0, ('stop_source',))
    assert m.mem.load_choices(w, 1, m.DOWN) == (0,1)
    m.mem.load(w, 1, m.DOWN, 0)
    m.execute(w, 1, ('register',))
    assert w.callback_inline and w.callback_thread == 1
    assert w.vc[1][0] == 2 and w.views[1][m.DOWN] == 1
    m.mem.acquire(w, 1, m.synchronization(w, 0))
    assert m.mem.load_choices(w, 1, m.DOWN) == (1,)
    # A pending registration is canceled by deregistration; request cannot
    # invoke it later. An executing callback keeps its return on the stack.
    m.configure(m.Cfg(threads=((),())))
    w = m.initial()
    w.alive = (True,True)
    w.callback_status = 'removed'
    m.execute(w, 1, ('stop_source',))
    assert not w.threads[1] and w.callback_thread == -1
    # Lock-removal mutants split the ordinary accesses that lock merging hid.
    for mutant, action, continuation in (('owner_unlocked','owner_take','owner_commit'),
                                         ('op_unlocked','op_take','op_commit')):
        m.configure(m.Cfg(threads=((),()), mut=mutant))
        w=m.initial(); w.alive=(True,True); w.owner=True
        m.execute(w,0,(action,))
        assert w.threads[0][0][0] == continuation
    # Every concurrent refcount equals the explicit ownership graph, checked
    # independently of copy/drop transitions.
    m.configure(m.Cfg(threads=((),)))
    w=m.initial(); w.refs=(1,0)
    assert any('ownership graph' in e for e in m.state_violations(w))

    # Scope stop is a different input to the fused token. It must not invent a
    # modification or publication on the state's own stop source.
    m.configure(m.Cfg(threads=((),), stale=True))
    w=m.initial(); before=w.hist[m.STOP]
    m.execute(w,0,('scope_stop',))
    assert w.hist[m.STOP] == before and not w.stop_requested
    assert w.hist[m.SCOPE_STOP][-1][0]

    # Unstarted consumers have no __state edge. Their abandon path must not
    # invent a second request_stop call (the operation owns the state instead).
    m.configure(m.Cfg(threads=((),)))
    w=m.initial(); w.alive=(True,True)
    m.execute(w,0,('delivery_claim','abandon_active'))
    assert ('request',) not in w.threads[0]

    # A false load/store branch performs only an acquire load, including when
    # the old false carries a release sequence. It must not extend MO history.
    m.configure(m.Cfg(threads=((),), stale=True, mut='plain_active'))
    w=m.initial(); w.alive=(True,True)
    m.mem.publish(w,0,m.ACTIVE,False,'release')
    before=w.hist
    m.execute(w,0,('deliver','test'))
    assert w.hist == before and not w.threads[0]

    # __complete_with's consumer_cb is declared before local keepalive.
    # Expand the function and check the separate atomic destruction points.
    for mutant in ('', 'keepalive_first'):
        m.configure(m.Cfg(threads=((),), mut=mutant))
        w=m.initial(); m.execute(w,0,('launch',))
        w.alive=(True,True); w.consumer_cb=True
        m.setedge(w,'registered',1)
        w.threads=((('unlock',0),),)
        m.execute(w,0,('complete_body','value',True))
        script=w.threads[0]
        assert script.index(('complete_tail',)) < script.index(('drop','delivery.0'))
        assert (script.index(('deliver','delivery.0')) < script.index(('complete_tail',))) == (not mutant)
        w.threads=(script[script.index(('complete_tail',))+1:],)
        m.execute(w,0,('complete_tail',))
        assert w.threads[0][:3] == (('lock',0),('keep_take',),('unlock',0))
        w.threads=((('unlock',0),('drop','delivery.0')),)
        m.execute(w,0,('keep_take',))
        assert w.threads[0] == (('unlock',0),('drop','keep.0'),('drop','delivery.0'))

    # __consume destroys its by-value parameter before delivery in the caller.
    for taken in (False,True):
        m.configure(m.Cfg(threads=((),)))
        w=m.initial(); w.alive=(True,True); w.phase='done'; w.taken=taken
        w.threads=((('unlock',0),),)
        m.execute(w,0,('consume_body',))
        script=w.threads[0]
        assert script[1:3] == (('drop','consume_arg.0'),('consume_return',))
        assert script[3][0] == ('deliver_stopped' if taken else 'deliver')

    # Implicit consumer member destruction: callback, __state, object lifetime
    # end, implicit weak release. Combined storage is available at every point.
    m.configure(m.Cfg(threads=((),)))
    w=m.initial(); w.alive=(True,True); w.cb_alive=(True,True); w.allocated=(True,True,True,True)
    m.execute(w,0,('consumer_dead',))
    assert w.alive[1] and w.cb_alive[1] and w.allocated[3]
    assert w.threads[0] == (('deregister',),('drop','consumer_state'),('consumer_end',),('weak_drop','implicit.consumer'))
    m.execute(w,0,('consumer_end',))
    assert not w.alive[1] and w.cb_alive[1] and w.allocated[3]

    # Producer waits refer to the selected input registration, never to the
    # receiver's registration; self deregistration cannot wait on its stack.
    m.configure(m.Cfg(threads=((),())))
    w=m.initial(); w.pthreads=(0,-1); w.callback_thread=1
    assert not m.enabled(w,1,('producer_deregister',0))
    assert m.enabled(w,0,('producer_deregister',0))
    assert m.enabled(w,1,('producer_deregister',1))
    assert m.enabled(w,0,('state_dead',))
    # Pre-stopped inline registration no longer borrows its source on removal.
    w.pregs=('inline','inline'); w.pthreads=(-1,-1); w.scope_alive=False; w.alive=(True,False)
    m.execute(w,0,('producer_deregister',1))
    assert not w.errors and w.pregs[1] == 'removed'

    # Lifetime end must conflict with ordinary storage use, independently of
    # fields and tombstones. Member-destructor access remains legitimate.
    for name, read, end in (('operation storage',m.op_access,'op_dead'),
                            ('scope storage',m.scope_access,'join')):
        m.configure(m.Cfg(threads=((),()),hb=True))
        w=m.initial(); w.op_alive=True
        read(w,0)
        if end=='join':
            m.signal(w,1,'released')
        m.execute(w,1,(end,))
        assert any('HB: unordered access to '+name == e for e in w.errors)

    # Every action the interpreter can execute has metadata and renders. This
    # static exhaustiveness check also covers actions absent in quick traces.
    from pathlib import Path
    tree=ast.parse(Path(m.__file__).read_text(),feature_version=(3,9))
    actions=set()
    for node in ast.walk(tree):
        if isinstance(node,ast.Compare) and isinstance(node.left,ast.Name) and node.left.id=='a':
            for rhs in node.comparators:
                values=rhs.elts if isinstance(rhs,(ast.Tuple,ast.List,ast.Set)) else (rhs,)
                actions.update(v.value for v in values if isinstance(v,ast.Constant) and isinstance(v.value,str))
    assert actions <= set(m.STEPS), actions-set(m.STEPS)
    m.configure(m.Cfg(threads=((),)))
    w=m.initial()
    for action in actions:
        assert action in m.describe((0,action,()),w)
    assert 'scope_request_end' in actions

    # Inspect direct protected writes under variant-dependent if statements,
    # and direct C.mut/C.control references in their assignment values (including
    # conditional expressions). No claim of transitive/alias analysis is made.
    check_oracle_assignments(tree)
    for field in ORACLE_FIELDS:
        for variant in ('mut', 'control'):
            for source in (
                    "if C.%s == 'x':\n    w.%s = True" % (variant, field),
                    "if C.%s == 'x':\n    pass\nelse:\n    w.%s = True" % (variant, field),
                    "w.%s = True if C.%s == 'x' else False" % (field, variant),
                    "w.%s: bool = False if C.%s else True" % (field, variant),
                    "w.%s += 1 if C.%s else 0" % (field, variant),
                    "w.%s = bool(C.%s)" % (field, variant)):
                try:
                    check_oracle_assignments(ast.parse(source))
                except AssertionError:
                    pass
                else:
                    raise AssertionError('accepted dependent oracle: ' + source)
    check_oracle_assignments(ast.parse(
        "w.producer_started = True\nw.request_owners += ((t, owners),)\n"
        "if C.control == 'skip':\n    w.producer_done = False\n"
        "w.expected = 'error' if C.failure else C.payload"))

    # Contract expectations are identical under changed implementation guards
    # and dispatch kinds. A preset bit is independent of invocation cardinality.
    for mutant in ('', 'consume_taken'):
        m.configure(m.Cfg(threads=((),), mut=mutant))
        w=m.initial(); w.alive=(True,True); w.phase='done'; w.expected='value'
        m.execute(w,0,('defensive_take',))
        assert w.consumer_expected == 'stopped'
        w.threads=((('unlock',0),),)
        m.execute(w,0,('consume_body',))
        m.execute(w,0,('delivery_claim','deliver'))
        assert w.receiver_expected == 'stopped'
    for mutant in ('', 'cancel_done'):
        m.configure(m.Cfg(threads=((),), mut=mutant))
        w=m.initial(); w.alive=(True,True); w.phase='done'; w.expected='error'
        m.execute(w,0,('cancel_body',))
        assert w.expected == 'error'
    for failure, action in (('install','query_stop'), ('consume','consume')):
        m.configure(m.Cfg(threads=((),), failure=failure))
        w=m.initial(); w.alive=(True,True)
        m.execute(w,0,(action,))
        m.execute(w,0,('delivery_claim','deliver_stopped'))
        assert w.receiver_expected == 'error'

    # Fn notification is legal before start in every child mode, preserves its
    # state, and never claims or schedules completion. Start gates dispatch.
    for mode in ('own','inline','sync','later','ignore'):
        m.configure(m.Cfg(threads=((),), mode=mode, registration='connect'))
        w=m.initial(); w.alive=(True,False)
        m.execute(w,0,('producer_fn',))
        assert w.prestart_notified and w.producer_stop_seen
        assert not w.producer_claimed and not w.producer_done and not w.errors
        assert ('produce',) not in w.threads[0]
        m.execute(w,0,('start_producer',))
        assert (('produce',) in w.threads[0]) == (mode in ('sync','inline'))
    # The completion prohibition checks the actual dispatch, independently of
    # the notification path. Teardown covers both inputs without any start.
    m.configure(m.Cfg(threads=((),), registration='connect'))
    w=m.initial(); w.alive=(True,False)
    m.execute(w,0,('produce',))
    assert 'C6: completion of unstarted producer' in m.state_violations(w)
    w=m.initial(); w.alive=(True,False); w.pregs=('pending','inline')
    for slot in (1,0): m.execute(w,0,('producer_deregister',slot))
    assert w.pregs == ('removed','removed') and not w.producer_started
    assert {'producer.deregister.unstarted.0','producer.deregister.unstarted.1'} <= m.COV

    # Both constructed inputs must be removed before their containing state
    # ends, independently of whether either source also ends. Terminal checks
    # cannot rely on the state_end transition having performed its own check.
    for slot in (0, 1):
        for status in ('pending', 'executing', 'completed', 'inline'):
            m.configure(m.Cfg(threads=((),)))
            w=m.initial(); w.alive=(True,False)
            w.pregs=m.put(('removed','removed'),slot,status)
            msg='C6: producer registration %d survives containing storage' % slot
            assert msg in m.terminal_violations(w)
            m.execute(w,0,('state_end',))
            assert msg in w.errors
        # Self-removal is legal even with the invocation return still on stack.
        m.configure(m.Cfg(threads=((),)))
        w=m.initial(); w.alive=(True,False); w.pregs=('removed','removed')
        w.pregs=m.put(w.pregs,slot,'executing'); w.pthreads=m.put(w.pthreads,slot,0)
        m.execute(w,0,('producer_deregister',slot))
        m.execute(w,0,('state_end',))
        m.execute(w,0,('producer_invoke_end',slot))
        assert not w.errors and w.pregs == ('removed','removed')
    # Both the per-input trampoline entry (including a losing input) and Fn
    # check state lifetime. A late scope stop still selects an omitted input 1.
    for action in (('producer_invoke',0), ('producer_invoke',1), ('producer_fn',)):
        m.configure(m.Cfg(threads=((),)))
        w=m.initial(); w.fused_winner=0
        m.execute(w,0,action)
        assert 'C6: producer callback enters after containing storage lifetime' in w.errors
    m.configure(m.Cfg(threads=((),)))
    w=m.initial(); w.pregs=('removed','pending')
    m.execute(w,0,('scope_stop',))
    assert w.threads[0][0] == ('producer_invoke',1)
    m.execute(w,0,('producer_invoke',1))
    m.execute(w,0,('producer_fn',))
    assert 'C6: producer callback enters after containing storage lifetime' in w.errors

    # Full mutant claims cover every declared scenario in both checked modes;
    # every catalog killer is explicitly hand-declared in sensitivity.
    for mid,entry in CATALOG.items():
        configs=[t for t in tasks if t['mut']==mid]
        assert {(t['scen'],t['model']) for t in configs} == {(s,mode) for s in SCENARIOS for mode in ('sc','sc_hb')}
        assert {t['name'] for t in configs if t['name'] in failure_manifest()} == {
            mid+'.'+s+'.'+mode for s in entry['killers'] for mode in ('sc','sc_hb')}

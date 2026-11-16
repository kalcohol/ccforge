"""Independent oracle, sender ownership and per-storage regressions."""
import ast
from pathlib import Path
from engine.oracle import check, fixtures
from . import model as m, tiers
from .mutants import CATALOG, CONTROLS, failure_manifest
from .scenarios import SCENARIOS
ORACLES={'receiver_obligation','inner_expected','op_obligation','expected'}

def run():
    tasks=tiers.full(); names={t['name'] for t in tasks}
    assert len(names)==len(tasks) and failure_manifest()<=names
    for mid in CATALOG:
        assert {(t['scen'],t['model']) for t in tasks if t['mut']==mid}=={(s,mode) for s in SCENARIOS for mode in ('sc','sc_hb')}
    assert {t['mut'] for t in tiers.select('quick')}>=set(CATALOG)
    assert {t['control'] for t in tiers.select('quick')}>=set(CONTROLS)
    for mid, entry in CATALOG.items():
        assert {t['name'] for t in tasks if t['mut']==mid and t['name'] in failure_manifest()} == {mid+'.'+name for name in entry['killers']}
    # Each input can read an old false precheck and then register inline.
    # The paired marker must refer to this registration, not another path.
    for slot in (0,1):
        tag='precheck_to_register.stale_false.inline.'+str(slot)
        m.configure(m.Cfg(threads=((),()),stale=True,required_stale_coverage=(tag,)))
        w=m.initial()
        w.op_alive=w.receiver_alive=w.child_alive=True
        m.execute(w,1,('stop',slot))
        assert m.inner.choices(w,0,('query',slot))==(0,1)
        m.execute(w,0,('query',slot),0)
        m.execute(w,0,('register',slot))
        assert w.prechecks[slot]=='stale_false' and w.callbacks.status[slot]=='inline'
        assert tag in m.coverage()
        m.COV.remove(tag)
        try: m.coverage()
        except AssertionError: pass
        else: raise AssertionError('missing stale/inline path must fail coverage')
    races={s for s in SCENARIOS if s.startswith('d_register_')}
    assert races=={'d_register_'+r+'_'+str(i) for r in ('connect','start') for i in (0,1)}
    assert {(t['scen'],t['model']) for t in tiers.select('quick') if not t['mut'] and not t['control'] and t['scen'] in races}=={(s,mode) for s in races for mode in ('sc','sc_hb','sc_stale')}
    # Every executable action, including actions in the shared child script,
    # must have source or explicit model-only metadata.
    from engine import inner_operation
    for module in (m, inner_operation):
        tree=ast.parse(Path(module.__file__).read_text())
        actions=set()
        for node in ast.walk(tree):
            if isinstance(node,ast.Compare) and isinstance(node.left,ast.Name) and node.left.id=='a':
                for rhs in node.comparators:
                    values=rhs.elts if isinstance(rhs,(ast.Tuple,ast.List,ast.Set)) else (rhs,)
                    actions.update(v.value for v in values if isinstance(v,ast.Constant) and isinstance(v.value,str))
        assert actions<=set(m.STEPS),actions-set(m.STEPS)
    assert all(1<=len(tiers.configuration(t).threads)<=5 for t in tasks)
    check(ast.parse(Path(m.__file__).read_text()),ORACLES); fixtures(ORACLES)
    m.configure(m.Cfg(threads=((),)))
    w=m.initial(); w.receiver_obligation=True
    assert 'AS3: lost receiver obligation' in m.terminal_violations(w)
    # Moves transfer one association without an atomic modification or acquire.
    w=m.initial(); m.execute(w,0,('sender','s')); before=w.hist
    m.execute(w,0,('move_sender','s','moved'))
    assert dict(w.senders)=={'moved':0,'s':-1} and w.hist==before
    m.execute(w,0,('destroy_sender','s')); m.execute(w,0,('destroy_sender','moved'))
    assert not m.lib.scope_errors(w,True)
    # const& connect has a fresh identity; a rejection oracle remains stopped
    # even when the variant illicitly borrows the source's engaged association.
    for mutant in ('','const_reuses_association'):
        m.configure(m.Cfg(threads=((),),reject='const_connect',mut=mutant))
        w=m.initial(); m.execute(w,0,('sender','s')); m.execute(w,0,('const_connect','s'))
        assert not w.inner_expected
        m.execute(w,0,('connected',)); m.execute(w,0,('start',))
        assert w.expected=='stopped' and w.receiver_obligation
    for slot in (0,1):
        for status in ('pending','executing','completed','inline'):
            m.configure(m.Cfg(threads=((),)))
            w=m.initial(); w.op_alive=w.child_alive=True
            m.lib.cbset(w,'constructed',slot,True); m.lib.cbset(w,'status',slot,status)
            msg='AS2: registration %d survives storage'%slot
            assert msg in m.terminal_violations(w)
            m.execute(w,0,('op_end',)); assert msg in w.errors
    for action in (('invoke',0),('invoke',1),('fn',)):
        m.configure(m.Cfg(threads=((),)))
        w=m.initial(); m.execute(w,0,action)
        assert 'AS4: inner operation access after destruction' in w.errors
    # Exact destructor-body/member order, including reordered-member variant.
    for mutant in ('','association_last'):
        m.configure(m.Cfg(threads=((),),mut=mutant))
        w=m.initial(); w.op_alive=w.receiver_alive=w.child_alive=True; w.association=0
        m.execute(w,0,('destroy_op',))
        assert w.threads[0][:3]==(('deregister',1),('deregister',0),('child_end',))
        assert w.threads[0][3:5]==((('association_release',),('receiver_end',)) if mutant else (('receiver_end',),('association_release',)))
    m.configure(m.Cfg(threads=((),),mut='release_at_completion'))
    w=m.initial(); w.op_alive=w.receiver_alive=w.child_alive=True
    m.execute(w,0,('destroy_op',))
    assert ('child_end',) not in w.threads[0]
    # Every storage lifetime has an independent conflicting-write check.
    for accessor,end,field in ((m.touch,'op_end','op_alive'),(m.rcvr,'receiver_end','receiver_alive')):
        m.configure(m.Cfg(threads=((),()),hb=True))
        w=m.initial(); w.op_alive=w.receiver_alive=True
        accessor(w,0); m.execute(w,1,(end,))
        assert any('HB: unordered access' in e for e in w.errors)
    from engine.source_lock import resolve
    for row in m.step_table(): resolve(m.SOURCE.read_text(),row[2])
    for a in m.STEPS: assert a in m.describe((0,(a,)),w)

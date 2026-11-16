"""Independent obligations, source mapping and mutation fidelity regressions."""
import ast
from pathlib import Path
from engine.oracle import check, fixtures
from . import model as m, tiers
from .mutants import CATALOG, CONTROLS, failure_manifest
from .scenarios import SCENARIOS
ORACLES={'launch_obligation','completion_ref','allocated_obligation','state_constructed'}

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
        w.alive=w.child_alive=True
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
    # Obligations exist even when the discharging transition never executes.
    m.configure(m.Cfg(threads=((),)))
    w=m.initial(); w.launch_obligation=w.allocated_obligation=True
    assert any('lost completion' in e for e in m.terminal_violations(w))
    assert any('allocation obligation' in e for e in m.terminal_violations(w))
    w.completion_obligation=True
    assert 'lost child completion obligation' in m.terminal_violations(w)
    # A failed constructor ends partial storage and deallocates, but never
    # invokes the complete state's destructor or reference release protocol.
    m.configure(m.Cfg(threads=((),),failure='construction'))
    w=m.initial(); m.execute(w,0,('launch',)); m.execute(w,0,('failure_free',))
    assert not w.state_constructed and w.destroyed==0 and w.freed==1
    assert not m.state_violations(w)
    # A count is checked against independent owners, not only in decrement.
    w=m.initial(); w.refs=1
    assert 'SP1: reference ownership mismatch' in m.state_violations(w)
    # Initialization/add_ref are relaxed; every release is a separate RMW.
    m.configure(m.Cfg(threads=((),),stale=True))
    w=m.initial(); m.execute(w,0,('launch',)); m.execute(w,0,('add_ref',))
    assert w.refs==2 and w.hist[0][-1][2][0]==(0,)
    history=w.hist; m.execute(w,0,('take_association',)); assert w.hist==history
    m.execute(w,0,('release','launch')); assert w.refs==1
    assert w.hist[0][-1][2][0]==w.vc[0]
    # Each registration independently checked both at storage end and terminal.
    for slot in (0,1):
        for status in ('pending','executing','completed','inline'):
            m.configure(m.Cfg(threads=((),)))
            w=m.initial(); w.alive=w.child_alive=True
            m.lib.cbset(w,'constructed',slot,True); m.lib.cbset(w,'status',slot,status)
            msg='SP2: registration %d survives storage'%slot
            assert msg in m.terminal_violations(w)
            m.execute(w,0,('state_end',)); assert msg in w.errors
    for action in (('invoke',0),('invoke',1),('fn',)):
        m.configure(m.Cfg(threads=((),)))
        w=m.initial(); m.execute(w,0,action)
        assert any('child access after destruction' in e for e in w.errors)
    # Constructor failure of the reordered member destroys association before
    # catch deallocation; the unmodified constructor has never associated.
    for mutant in ('','associate_before_connect'):
        m.configure(m.Cfg(threads=((),),failure='connect',mut=mutant))
        w=m.initial(); m.execute(w,0,('launch',))
        if mutant: m.execute(w,0,('associate',))
        m.execute(w,0,('connect',))
        assert w.threads[0][0][0]==('association_release' if mutant else 'failure_free')
    # Dropping/relocating only the association read must not preserve the original.
    m.configure(m.Cfg(threads=((),),mut='read_after_destroy'))
    w=m.initial(); w.alive=True; w.refs=1; w.owners=('completion',)
    m.execute(w,0,('release','completion'))
    assert w.threads[0][0]==('allocator_copy',)
    # Storage end races use the shared memory checker, independently of tombstones.
    m.configure(m.Cfg(threads=((),()),hb=True))
    w=m.initial(); w.alive=True; m.touch(w,0); m.execute(w,1,('state_end',))
    assert 'HB: unordered access to state storage' in w.errors
    from engine.source_lock import resolve
    for row in m.step_table(): resolve(m.SOURCE.read_text(),row[2])
    for a in m.STEPS: assert a in m.describe((0,(a,)),w)

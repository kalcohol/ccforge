"""Library boundaries, independent obligations and lifetime/HB regressions."""
from types import SimpleNamespace
from .library_contracts import Library
from .memory import Memory, change, synchronization


def setup(hb=False, stale=False):
    w = SimpleNamespace(errors=(), vc=((1,0),(0,1)), views=((0,0),(0,0)),
                        accesses=(), alive=True, source_alive=(True,True))
    z=(0,0); pub=(z,z)
    w.hist=(((False,z,pub),),)*2
    def detect(w,e): w.errors += (e,)
    mem=Memory(SimpleNamespace(hb=hb,stale=stale),detect)
    def storage(w,t):
        if not w.alive: detect(w,'dead callback storage')
        mem.access(w,t,'callback storage')
    def source(w,t,slot):
        if not w.source_alive[slot]: detect(w,'dead source')
        mem.access(w,t,'source '+str(slot))
    lib=Library(mem,detect,set(),storage,source)
    lib.initialize(w,2)
    return w,lib,mem


def run():
    single_input_inner_test()
    w,l,m=setup(stale=True)
    assert not l.register(w,0,0,0)
    l.deregister(w,0,0)
    assert not l.request(w,1,0,0)  # Pending removal cancels.
    assert not w.callbacks.obligation[0]
    w,l,m=setup(hb=True,stale=True)
    change(w,'vc',0,(2,0)); l.request(w,0,0,0); l.request_end(w,0,0)
    assert m.load_choices(w,1,0)==(0,1)
    assert l.register(w,1,0,0)  # Atomic registration sees stop after stale false.
    assert w.vc[1][0]==2
    assert l.invoke(w,1,0); l.fn(w,1); l.invocation_end(w,1,0)
    w.source_alive=(False,True)
    l.deregister(w,1,0)  # Inline-only removal has no source dependency.
    assert not w.errors and not l.registration_errors(w)
    w,l,m=setup()
    for slot in (0,1): l.register(w,0,slot,slot)
    assert l.request(w,1,0,0)
    assert not l.removable(w,0,0) and l.removable(w,1,0)
    assert l.invoke(w,1,0)
    assert l.callback_errors(w,True)  # Commitment cannot substitute for Fn.
    assert l.request(w,0,1,1)
    assert not l.invoke(w,0,1)  # Losing invocation never waits for winner.
    l.invocation_end(w,0,1)
    l.fn(w,1)
    l.deregister(w,1,1); l.deregister(w,1,0)
    w.alive=False
    l.invocation_end(w,1,0)  # Stack-only return after self removal.
    assert not w.errors and not l.callback_errors(w,True)
    for slot in (0,1):
        for status in ('pending','executing','completed','inline'):
            w,l,m=setup(); l.cbset(w,'constructed',slot,True); l.cbset(w,'status',slot,status)
            assert l.registration_errors(w)==('registration %d survives storage'%slot,)
    for action in ('invoke','fn'):
        w,l,m=setup(); w.alive=False
        l.invoke(w,0,0) if action=='invoke' else l.fn(w,0)
        assert 'dead callback storage' in w.errors
    w,l,m=setup()
    before=w.hist
    assert l.associate(w,0,False)==-1 and w.hist==before
    a=l.associate(w,0); b=l.associate(w,0)
    assert a!=b and l.scope_errors(w,True)
    l.release(w,0,a); l.release(w,0,b)
    assert not l.scope_errors(w,True)
    l.join(w,0); l.destroy_scope(w,0)
    l.scope_access(w,0)
    assert 'scope access after destruction' in w.errors
    w,l,m=setup(); a=l.associate(w,0); l.release(w,0,a); l.release(w,0,a)
    assert l.scope_errors(w)
    w,l,m=setup(); l.request(w,0,0,0); l.join(w,0); l.destroy_scope(w,0)
    assert 'scope destroyed inside request_stop' in w.errors
    w,l,m=setup(hb=True); l.scope_access(w,0); l.join(w,1); l.destroy_scope(w,1)
    assert 'HB: unordered access to scope storage' in w.errors
    from .oracle import fixtures, check
    import ast
    from pathlib import Path
    from . import inner_operation
    protected=('child_started','completion_obligation','prestart')
    fixtures(protected)
    check(ast.parse(Path(inner_operation.__file__).read_text()), protected)


def single_input_inner_test():
    from .inner_operation import Inner
    w,l,m=setup()
    l.initialize(w,1)
    w.child_ready=False; w.child_started=False; w.notice=False
    cfg=SimpleNamespace(control='',mode='sync',registration='start')
    queue=[]
    child=Inner(cfg,m,l,lambda w,e: None,set(),lambda w,t,*ops: queue.extend(ops),lambda *a:None,lambda *a:None)
    child.execute(w,0,('query',0),0)
    assert queue==[('register',0)]
    queue.clear()
    m.publish(w,0,1,True)
    child.execute(w,0,('query',0),0)
    assert queue==[('invoke',-1),('register',0)]

"""spawn.hpp: launch/completion references and allocator/association teardown."""
import copy
from pathlib import Path
from types import SimpleNamespace
from engine.memory import Memory, put, change, synchronization
from engine.library_contracts import Library
from engine.inner_operation import Inner, FIELDS as INNER_FIELDS, ACTIONS as INNER_ACTIONS

SOURCE=Path(__file__).resolve().parents[2]/'backport/cpp26/execution/spawn.hpp'
C=None
COV=set()
REFS={
    'launch':'::__launch | auto* state = state_traits_t::allocate(state_alloc, 1);',
    'connect':'__state::__state | {}',
    'associate':'__state::__state | {}',
    'run':'__state::__run | __add_ref();',
    'add_ref':'__state::__add_ref | __refs.fetch_add(1, std::memory_order_relaxed);',
    'run_branch':'__state::__run | if (__association)',
    'run_tail':'__state::__run | __release();',
    'complete':'__state::__complete | __release();',
    'release':'__state::__release | __refs.fetch_sub(1, std::memory_order_acq_rel)',
    'take_association':'__state::__release | auto association = std::move(__association);',
    'allocator_copy':'__state::__release | rebound_alloc_t alloc{__alloc};',
    'destroy':'__state::__release | rebound_traits_t::destroy(alloc, this);',
    'deallocate':'__state::__release | rebound_traits_t::deallocate(alloc, this, 1);',
}
# Constructors have empty bodies. References use a real body fragment on run;
# whole-file hashing also locks member declarations and initializers.
REFS['connect']=REFS['associate']='model-only; call site __state::__run | if (__association)'
ACTIONS=('''launch connect associate constructed run add_ref run_branch run_tail complete release take_association allocator_copy destroy child_end state_end deallocate allocator_end association_release failure_free launch_allocator_end signal await join destroy_scope''').split()+INNER_ACTIONS
STEPS={a:SimpleNamespace(lines=REFS.get(a,'model-only: library contract / finite script / implicit destruction'),desc=a) for a in ACTIONS}
EXTRA_REFS=(
    '::__launch | state_traits_t::deallocate(state_alloc, state, 1);',
    '::__launch_with_env | auto started =',
    '::__spawn_wrapped | auto alloc = std::execution::get_allocator(env);',
    '::__spawn_wrapped | auto alloc = std::execution::get_allocator(source_env);',
    '__spawn_t::operator() | auto wrapped = token.wrap',
    '__state::__recv::set_value | __owner->__complete();',
    '__state::__recv::set_stopped | __owner->__complete();')
FIELDS=('''threads errors alive allocated refs owners association moved destroyed freed releases completions launch_obligation completion_ref allocated_obligation state_constructed launch_releases completion_releases signals vc views hist accesses allocator_alive''').split()+INNER_FIELDS+['prechecks']

class Cfg:
    def __init__(self,**kw):
        self.name='cfg'; self.threads=(); self.mode='own'; self.registration='start'; self.closed=False
        self.failure=''; self.hb=False; self.stale=False; self.mut=''; self.control=''; self.required_coverage=(); self.required_stale_coverage=()
        self.__dict__.update(kw)
class World: pass

def key(w): return tuple(getattr(w,f) for f in FIELDS)
def state(k):
    w=World()
    for f,v in zip(FIELDS,k): setattr(w,f,v)
    return w
def error(w,msg): w.errors+=(msg,)
def prepend(w,t,*ops): change(w,'threads',t,tuple(ops)+w.threads[t])
def signal(w,t,name):
    d=dict(w.signals); d[name]=synchronization(w,t); w.signals=tuple(sorted(d.items()))
def touch(w,t,write=False):
    if not w.alive: error(w,'SP1: state access after destruction')
    mem.access(w,t,'state storage',write)
def child_touch(w,t):
    if not w.child_alive: error(w,'SP2: child access after destruction')
    touch(w,t); mem.access(w,t,'inner storage')
def source(w,t,slot):
    if slot==0:
        lib.scope_access(w,t)
        if not w.scope.source_alive: error(w,'SP2: dead scope stop source')
        mem.access(w,t,'scope stop source')
    # Environment storage is an independently owned fixture, live throughout.
    else: mem.access(w,t,'environment source')
def allocator(w,t):
    if not w.allocator_alive: error(w,'SP2: allocator access after local destruction')
    if w.scope.acquired and not w.scope.engaged: error(w,'SP2: allocator access after association release')
    lib.scope_access(w,t); mem.access(w,t,'allocator storage')
def initial():
    w=World(); n=len(C.threads); z=(0,)*n; b=(0,)*3; pub=(z,b)
    w.threads=C.threads; w.errors=(); w.alive=w.allocated=False; w.refs=0; w.owners=()
    w.association=w.moved=-1; w.destroyed=w.freed=w.releases=w.completions=0
    w.launch_obligation=w.completion_ref=w.allocated_obligation=w.state_constructed=False; w.launch_releases=w.completion_releases=0; w.signals=(); w.accesses=()
    w.vc=tuple(tuple(int(t==u) for u in range(n)) for t in range(n)); w.views=(b,)*n
    w.hist=tuple(((v,z,pub),) for v in (0,False,False)); w.allocator_alive=True
    for f in ('child_alive','child_started','child_ready','claimed','completion_obligation','completed','notice','prestart'): setattr(w,f,False)
    w.notice_pub=pub; w.prechecks=('unqueried',)*2; lib.initialize(w,2)
    return w
def enabled(w,t,op):
    if op[0]=='await': return op[1] in dict(w.signals)
    if op[0]=='join': return not w.scope.engaged and 'released' in dict(w.signals)
    if C.control=='wait_cycle' and op[0]=='deregister' and w.callbacks.owner[op[1]]==t: return False
    return inner.enabled(w,t,op)
def execute(w,t,op,alt=0):
    a,*args=op; COV.add(a)
    # Coverage-only ghost state ties each query's observation to its own later
    # registration. It never affects enabling, delivery, ownership or oracles.
    if a=='query':
        slot=args[0]; history=w.hist[1+slot]
        result='true' if history[alt][0] else ('stale_false' if history[-1][0] else 'false')
        change(w,'prechecks',slot,result)
        COV.add('precheck.'+result+'.'+str(slot))
        if len(inner.choices(w,t,op))>1: COV.add('precheck.multiple_choices.'+str(slot))
    if inner.execute(w,t,op,alt):
        if a=='register':
            slot=args[0]
            COV.add('precheck_to_register.'+w.prechecks[slot]+'.'+w.callbacks.status[slot]+'.'+str(slot))
        return
    if a=='signal':
        signal(w,t,args[0])
        if C.control=='literal_launch_allocator_lifetime' and args[0]=='launch_returned': prepend(w,t,('launch_allocator_end',))
    elif a=='await': mem.acquire(w,t,dict(w.signals)[args[0]])
    elif a=='launch':
        w.alive=w.allocated=True; w.allocated_obligation=True
        w.refs=1; w.owners=('launch',); w.launch_obligation=True
        allocator(w,t); touch(w,t,True)
        # Atomic member initialization is not a release publication.
        mem.publish(w,t,0,1,'relaxed')
        if C.failure=='construction': prepend(w,t,('failure_free',))
        elif C.mut=='associate_before_connect': prepend(w,t,('associate',),('connect',),('constructed',))
        else: prepend(w,t,('connect',),('associate',),('constructed',))
    elif a=='connect':
        touch(w,t)
        if C.failure=='connect':
        # Throwing operation constructor leaves no complete child/registration.
            w.threads=put(w.threads,t,tuple(x for x in w.threads[t] if x[0] not in ('associate','constructed')))
            unwind=(('association_release',),) if w.association!=-1 else ()
            prepend(w,t,*unwind,('failure_free',))
        else: prepend(w,t,('child_construct',),('signal','connected_boundary'))
    elif a=='associate':
        w.association=lib.associate(w,t,not C.closed)
    elif a=='constructed':
        w.state_constructed=True
        prepend(w,t,('run',))
    elif a=='run':
        touch(w,t); w.completion_ref=True
        if C.mut!='drop_add_ref': prepend(w,t,('add_ref',),('run_branch',))
        else: prepend(w,t,('run_branch',))
    elif a=='add_ref':
        touch(w,t); w.refs+=1; w.owners+=('completion',)
        mem.publish(w,t,0,w.refs,'relaxed',rmw=True)
    elif a=='run_branch':
        touch(w,t); mem.access(w,t,'association member')
        ops=(('child_start',),) if w.association!=-1 or C.mut=='start_unassociated' else (('complete',),)
        if w.association==-1: COV.add('not_associated')
        if C.mut=='release_before_start': prepend(w,t,('release','launch'),*ops,('signal','launch_returned'))
        else: prepend(w,t,*ops,('run_tail',),('signal','launch_returned'))
    elif a=='run_tail':
        touch(w,t); prepend(w,t,('release','launch'))
    elif a=='complete':
        touch(w,t); w.completions+=1
        if w.association!=-1:
            w.completed=True
            mem.access(w,t,'completion payload',True)
        prepend(w,t,('release','completion'))
        if C.mut=='double_completion_release': prepend(w,t,('release','completion'))
    elif a=='release':
        touch(w,t)
        holder=args[0]
        if C.control=='skip_'+holder+'_release': return
        if holder=='launch': w.launch_releases+=1
        else: w.completion_releases+=1
        w.owners=tuple(x for x in w.owners if x!=holder); w.refs-=1
        mem.publish(w,t,0,w.refs,'relaxed' if C.mut=='relaxed_release' else 'acq_rel',rmw=True)
        if not w.refs:
            COV.add('destroyer.'+holder)
            take=() if C.mut=='read_after_destroy' else (('take_association',),)
            prepend(w,t,*take,('allocator_copy',),('destroy',))
    elif a=='take_association':
        touch(w,t); mem.access(w,t,'association member',True)
        w.moved=w.association; w.association=-1
    elif a=='allocator_copy': allocator(w,t); touch(w,t)
    elif a=='destroy':
        allocator(w,t)
        if C.control=='skip_destroy': prepend(w,t,('deallocate',)); return
        ops=(('deregister',1),('deregister',0),('child_end',)) if w.child_alive else ()
        tail=(('state_end',),('deallocate',),('allocator_end',),('association_release',))
        if C.mut=='association_before_destroy': ops=(('association_release',),)+ops
        if C.mut=='association_before_deallocate': tail=(('state_end',),('association_release',),('deallocate',),('allocator_end',))
        if C.mut=='read_after_destroy':
            if w.association!=-1: ops=(('association_release',),)+ops
            tail=(('state_end',),('take_association',),('deallocate',),('allocator_end',),('association_release',))
        prepend(w,t,*ops,*tail)
    elif a=='child_end':
        for e in lib.registration_errors(w): error(w,'SP2: '+e)
        child_touch(w,t); mem.access(w,t,'inner storage',True); w.child_alive=False
    elif a=='state_end':
        for e in lib.registration_errors(w): error(w,'SP2: '+e)
        if w.refs or w.owners or (w.completion_ref and (w.launch_releases,w.completion_releases)!=(1,1)): error(w,'SP1: destruction before both releases')
        touch(w,t,True); w.alive=False; w.destroyed+=1
    elif a=='deallocate':
        allocator(w,t)
        if C.control!='skip_deallocate': w.allocated=False; w.freed+=1
    elif a=='allocator_end': allocator(w,t); w.allocator_alive=False
    elif a=='association_release':
        identity=w.moved if w.moved!=-1 else w.association
        if identity!=-1:
            if w.alive or w.allocated or w.child_alive or lib.registration_errors(w) or w.allocator_alive:
                error(w,'SP2: association released before teardown and allocator end')
            if C.control!='skip_association':
                lib.release(w,t,identity); w.releases+=1; w.moved=w.association=-1
        signal(w,t,'released')
        if C.control=='strict_request_lifetime' and w.requests[0]!=-1 and not w.scope.engaged:
            prepend(w,t,('join',),('destroy_scope',))
    elif a=='launch_allocator_end':
    # Strict user allocator destructor which borrows scope-owned storage.
        lib.scope_access(w,t)
        if w.scope.acquired and not w.scope.engaged: error(w,'SP2: launch allocator destructor after association release')
    elif a=='failure_free':
        COV.add('failure.'+C.failure)
        # Unwinding initialized fields precedes catch deallocation; no try_associate.
        touch(w,t,True); w.alive=False
        allocator(w,t); w.allocated=False; w.freed+=1; w.refs=0; w.owners=(); w.allocator_alive=False
        signal(w,t,'released'); signal(w,t,'launch_returned')
    elif a=='join': lib.join(w,t)
    elif a=='destroy_scope': lib.destroy_scope(w,t)
    else: raise ValueError(op)
def successors(k):
    w=state(k)
    if w.errors: return []
    out=[]
    for t,script in enumerate(w.threads):
        if not script or not enabled(w,t,script[0]): continue
        for alt in inner.choices(w,t,script[0]):
            child=copy.copy(w); change(child,'threads',t,script[1:])
            if C.hb or C.stale: change(child,'vc',t,put(child.vc[t],t,child.vc[t][t]+1))
            execute(child,t,script[0],alt); out.append(((t,script[0]),child))
    return out
def state_violations(w):
    errors=list(w.errors)+inner.errors(w)+list(lib.scope_errors(w))
    if w.refs<0 or w.refs!=len(w.owners): errors.append('SP1: reference ownership mismatch')
    if w.destroyed>1 or w.freed>1: errors.append('SP1: duplicate destruction')
    if w.completions>1: errors.append('SP3: duplicate completion')
    if w.launch_releases>1 or w.completion_releases>1: errors.append('SP1: reference released twice')
    if C.closed and w.child_started: errors.append('SP3: not-associated child started')
    if w.callbacks.committed and not w.callbacks.calls:
        winner=w.callbacks.winner
        if winner!=-2 and (not w.threads[winner] or w.threads[winner][0]!=('fn',)):
            errors.append('SP3: committed stop dispatch lost')
    return tuple(errors)
def terminal_violations(w):
    errors=list(state_violations(w))+inner.errors(w,True)+list(lib.scope_errors(w,True))
    errors+=['SP2: '+e for e in lib.registration_errors(w)]
    if any(w.threads) or any(t!=-1 for t in w.callbacks.owner): errors.append('SP5: wait cycle or unfinished script')
    if w.allocated or w.alive or w.child_alive or w.refs or w.owners: errors.append('SP4: state leak')
    if w.completion_ref and (w.launch_releases,w.completion_releases)!=(1,1): errors.append('SP1: reference release obligation lost')
    if w.allocated_obligation and (w.destroyed!=int(w.state_constructed) or w.freed!=1): errors.append('SP4: allocation obligation lost')
    if w.launch_obligation and not C.failure and w.completions!=1: errors.append('SP3: lost completion obligation')
    return tuple(errors)
def is_blocked(k):
    w=state(k); return any(s and not enabled(w,t,s[0]) for t,s in enumerate(w.threads))
def is_aborted(w): return False
def metrics(): return {'max_store_buffer':0}
def coverage():
    if not C.mut and not C.control:
        required=set(C.required_coverage)
        if C.stale: required.update(C.required_stale_coverage)
        assert required<=COV,required-COV
    return sorted(COV)
def step_table(): return [('protocol',a,s.lines,'atomic / library / script',s.desc) for a,s in STEPS.items()]+[('source',str(i),r,'forwarding / construction',r) for i,r in enumerate(EXTRA_REFS)]
def describe_initial(w): return 'threads='+repr(w.threads)
def describe(meta,w):
    from engine.source_lock import resolve
    t,op=meta
    return 'T%d %s [%s] refs=%s owners=%s alive=%s cb=%s threads=%s'%(t,op,resolve(SOURCE.read_text(),STEPS[op[0]].lines),w.refs,w.owners,w.alive,w.callbacks,w.threads)
def configure(cfg):
    global C,mem,lib,inner
    C=cfg; COV.clear(); mem=Memory(cfg,error); lib=Library(mem,error,COV,child_touch,source)
    inner=Inner(cfg,mem,lib,error,COV,prepend,signal,child_touch)

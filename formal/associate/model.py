"""associate.hpp sender transfers, conditional union storage and teardown."""
import copy
from pathlib import Path
from types import SimpleNamespace
from engine.memory import Memory, put, change, synchronization
from engine.library_contracts import Library
from engine.inner_operation import Inner, FIELDS as INNER_FIELDS, ACTIONS as INNER_ACTIONS

SOURCE=Path(__file__).resolve().parents[2]/'backport/cpp26/execution/associate.hpp'
C=None
COV=set()
REFS={
    'sender':'__sender::__sender | __association = __token.try_associate();',
    'copy_sender':'__sender::__sender | __association = __token.try_associate();',
    'connect':'__sender::connect | std::move(__sndr), std::move(__association), std::move(rcvr)',
    'const_connect':'__sender::connect | wrapped_sender_t{__sndr}, __token.try_associate(), std::move(rcvr)',
    'construct_inner':'__op::__op | if (__association)',
    'start':'__op::start | if (!__association)',
    'complete':'__op::__recv::set_value | std::execution::set_value(std::move(*__rcvr), static_cast<Vs&&>(vs)...);',
    'deliver_stopped':'__op::start | std::execution::set_stopped(std::move(__rcvr));',
    'destroy_op':'__op::~__op | if (__association) std::destroy_at(std::addressof(__inner));',
}
# Both sender constructor bodies have the same assignment. Model-only metadata
# is explicit where a body fragment cannot uniquely choose an overload.
REFS['sender']=REFS['copy_sender']='model-only: sender association after wrapped sender construction/copy'
ACTIONS=('''sender copy_sender move_sender destroy_sender connect const_connect construct_inner connected start complete deliver_stopped receiver destroy_op child_end receiver_end association_release op_end receiver_probe signal await join destroy_scope''').split()+INNER_ACTIONS
STEPS={a:SimpleNamespace(lines=REFS.get(a,'model-only: library contract / finite script / implicit member destruction'),desc=a) for a in ACTIONS}
EXTRA_REFS=(
    '__op::__recv::set_error | std::execution::set_error(std::move(*__rcvr), static_cast<E&&>(e));',
    '__op::__recv::set_stopped | std::execution::set_stopped(std::move(*__rcvr));',
    '__op::__recv::get_env | return std::execution::get_env(*__rcvr);',
    '__associate_t::operator() | static_cast<S&&>(sndr), std::move(token)',
    '__associate_t::operator() | return {{}, std::decay_t<Token>(static_cast<Token&&>(token))};')
FIELDS=('''threads errors senders association op_alive receiver_alive connected_flag destroying released_ids received expected receiver_obligation inner_expected op_obligation signals vc views hist accesses''').split()+INNER_FIELDS+['prechecks']
class Cfg:
    def __init__(self,**kw):
        self.name='cfg'; self.threads=(); self.payload='value'; self.mode='own'; self.registration='start'; self.closed=False
        self.failure=''; self.reject=''; self.inline_destroy=False; self.receiver_touch=True
        self.hb=False; self.stale=False; self.mut=''; self.control=''; self.required_coverage=(); self.required_stale_coverage=()
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
    if not w.op_alive: error(w,'AS4: operation access after destruction')
    mem.access(w,t,'operation storage',write)
def child_touch(w,t):
    if not w.child_alive: error(w,'AS4: inner operation access after destruction')
    touch(w,t); mem.access(w,t,'inner storage')
def source(w,t,slot):
    if slot==0:
        lib.scope_access(w,t)
        if not w.scope.source_alive: error(w,'AS4: dead scope stop source')
        mem.access(w,t,'scope stop source')
    else: mem.access(w,t,'environment source')
def rcvr(w,t,write=False):
    if not w.receiver_alive: error(w,'AS4: receiver access after destruction')
    mem.access(w,t,'receiver storage',write)
def initial():
    w=World(); n=len(C.threads); z=(0,)*n; b=(0,)*3; pub=(z,b)
    w.threads=C.threads; w.errors=(); w.senders=(); w.association=-1
    w.op_alive=w.receiver_alive=w.connected_flag=w.destroying=False
    w.released_ids=(); w.received=(); w.expected=''; w.receiver_obligation=w.inner_expected=w.op_obligation=False
    w.signals=(); w.accesses=(); w.vc=tuple(tuple(int(t==u) for u in range(n)) for t in range(n)); w.views=(b,)*n
    w.hist=tuple(((v,z,pub),) for v in (0,False,False))
    for f in ('child_alive','child_started','child_ready','claimed','completion_obligation','completed','notice','prestart'): setattr(w,f,False)
    w.notice_pub=pub; w.prechecks=('unqueried',)*2; lib.initialize(w,2)
    return w
def sender_set(w,name,identity):
    d=dict(w.senders); d[name]=identity; w.senders=tuple(sorted(d.items()))
def enabled(w,t,op):
    if op[0]=='await': return op[1] in dict(w.signals)
    if op[0]=='join': return not w.scope.engaged and 'released' in dict(w.signals)
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
    if a=='signal': signal(w,t,args[0])
    elif a=='await':
        if C.control!='no_receiver_handoff' or args[0]!='child_started': mem.acquire(w,t,dict(w.signals)[args[0]])
    elif a in ('sender','copy_sender'):
        name=args[-1]
        allowed=not C.closed and C.reject!=('construction' if a=='sender' else 'copy')
        identity=lib.associate(w,t,allowed)
        sender_set(w,name,identity)
        COV.add('association.accepted' if identity!=-1 else 'association.rejected')
    elif a=='move_sender':
        src,dst=args; identity=dict(w.senders)[src]; sender_set(w,dst,identity)
        if C.mut!='move_leaves_source': sender_set(w,src,-1)
    elif a=='destroy_sender':
        name=args[0]; identity=dict(w.senders)[name]; lib.release(w,t,identity)
        d=dict(w.senders); del d[name]; w.senders=tuple(sorted(d.items()))
        signal(w,t,'sender_destroyed')
    elif a in ('connect','const_connect'):
        w.op_alive=w.receiver_alive=True; w.op_obligation=True
        touch(w,t,True); rcvr(w,t,True)
        if a=='connect':
            identity=dict(w.senders)[args[0]]; sender_set(w,args[0],-1)
        elif C.mut=='const_reuses_association': identity=dict(w.senders)[args[0]]
        else: identity=lib.associate(w,t,not C.closed and C.reject!='const_connect')
        w.association=identity
        # Contract records required construction independently of the altered guard.
        w.inner_expected=(not C.closed and C.reject!='const_connect') if a=='const_connect' else identity!=-1
        prepend(w,t,('construct_inner',),('connected',))
    elif a=='construct_inner':
        touch(w,t)
        if w.association!=-1 or C.mut=='construct_unassociated':
            if C.failure=='connect':
                COV.add('failure.connect')
                w.threads=put(w.threads,t,tuple(x for x in w.threads[t] if x[0]!='connected'))
                # Failed placement construction leaves no complete holder. Receiver and
                # association unwind in reverse member order.
                unwind=(('association_release',),('receiver_end',)) if C.mut=='association_last' else (('receiver_end',),('association_release',))
                prepend(w,t,*unwind,('op_end',),('signal','connect_failed'))
            else: prepend(w,t,('child_construct',))
    elif a=='connected':
        w.connected_flag=True; signal(w,t,'connected')
    elif a=='start':
        if not w.connected_flag: return  # Failure injection unwound the operation.
        touch(w,t)
        w.receiver_obligation=True
        w.expected=C.payload if w.inner_expected else 'stopped'
        if w.association!=-1 or C.mut=='start_unassociated': prepend(w,t,('child_start',))
        else: prepend(w,t,('deliver_stopped',))
    elif a=='complete':
        child_touch(w,t); w.completed=True
        seq=(('receiver',C.payload),)
        if C.mut=='release_at_completion': seq=(('association_release',),)+seq
        prepend(w,t,*seq)
    elif a=='deliver_stopped': prepend(w,t,('receiver','value' if C.control=='wrong_stopped' else 'stopped'))
    elif a=='receiver':
        rcvr(w,t)
        if w.expected!=args[0]: error(w,'AS3: wrong receiver completion')
        if C.control!='drop_receiver': w.received+=(args[0],)
        if C.inline_destroy:
            COV.add('receiver.self_destroy'); prepend(w,t,('destroy_op',))
        signal(w,t,'received')
    elif a=='destroy_op':
        if not w.op_alive or w.destroying: return
        w.destroying=True; touch(w,t)
        # Body destroys the holder, then implicit receiver/association teardown.
        ops=(('deregister',1),('deregister',0),('child_end',)) if w.association!=-1 else ()
        if C.mut=='skip_inner_destructor': ops=()
        tail=(('receiver_end',),('association_release',),('op_end',))
        if C.control=='receiver_after_destroy': tail+=(('receiver_probe',),)
        if C.mut=='association_last':
        # Moving the member last also moves its initializer after receiver and
        # union (inactive until constructor body), and destroys it after body
        # but before receiver. The body still destroys the active holder first.
            tail=(('association_release',),('receiver_end',),('op_end',))
        if C.mut=='release_before_inner': ops=(('association_release',),)+ops
        prepend(w,t,*ops,*tail)
    elif a=='child_end':
        for e in lib.registration_errors(w): error(w,'AS2: '+e)
        child_touch(w,t); mem.access(w,t,'inner storage',True); w.child_alive=False
    elif a=='receiver_end':
        rcvr(w,t,True)
        if C.receiver_touch:
            lib.scope_access(w,t)
            if w.inner_expected and w.association==-1: error(w,'AS2: receiver destructor after association release')
        w.receiver_alive=False
    elif a=='association_release':
        if w.association!=-1:
            if w.child_alive or w.receiver_alive or lib.registration_errors(w): error(w,'AS2: association release before members die')
            if C.control!='skip_association':
                lib.release(w,t,w.association); w.released_ids+=(w.association,); w.association=-1
        signal(w,t,'released')
        if C.control=='strict_request_lifetime' and w.requests[0]!=-1 and not w.scope.engaged:
            prepend(w,t,('join',),('destroy_scope',))
    elif a=='receiver_probe': rcvr(w,t)
    elif a=='op_end':
        for e in lib.registration_errors(w): error(w,'AS2: '+e)
        touch(w,t,True); w.op_alive=False
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
    if len(w.received)>1: errors.append('AS3: duplicate receiver completion')
    if (w.child_alive or w.child_started) and not w.inner_expected: errors.append('AS3: inner constructed or started without association')
    if w.callbacks.committed and not w.callbacks.calls:
        winner=w.callbacks.winner
        if winner!=-2 and (not w.threads[winner] or w.threads[winner][0]!=('fn',)):
            errors.append('AS3: committed stop dispatch lost')
    return tuple(errors)
def terminal_violations(w):
    errors=list(state_violations(w))+inner.errors(w,True)+list(lib.scope_errors(w,True))
    errors+=['AS2: '+e for e in lib.registration_errors(w)]
    if any(w.threads) or any(t!=-1 for t in w.callbacks.owner): errors.append('AS4: wait cycle or unfinished script')
    if w.op_alive or w.child_alive or w.receiver_alive or w.senders: errors.append('AS5: storage or sender leak')
    if w.receiver_obligation and len(w.received)!=1: errors.append('AS3: lost receiver obligation')
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
def step_table(): return [('protocol',a,s.lines,'member / library / script',s.desc) for a,s in STEPS.items()]+[('source',str(i),r,'forwarding',r) for i,r in enumerate(EXTRA_REFS)]
def describe_initial(w): return 'threads='+repr(w.threads)
def describe(meta,w):
    from engine.source_lock import resolve
    t,op=meta
    return 'T%d %s [%s] assoc=%s senders=%s receiver=%s cb=%s threads=%s'%(t,op,resolve(SOURCE.read_text(),STEPS[op[0]].lines),w.association,w.senders,w.receiver_alive,w.callbacks,w.threads)
def configure(cfg):
    global C,mem,lib,inner
    C=cfg; COV.clear(); mem=Memory(cfg,error); lib=Library(mem,error,COV,child_touch,source)
    inner=Inner(cfg,mem,lib,error,COV,prepend,signal,child_touch)

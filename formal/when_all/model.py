"""when_all pending credits, disposition, result storage and callback lifetimes."""
import copy
from pathlib import Path
from types import SimpleNamespace
from engine.memory import Memory, put, change, synchronization, empty_sync
from engine.library_contracts import Library
from engine.inner_operation import Inner, FIELDS as INNER_FIELDS, ACTIONS as INNER_ACTIONS
SOURCE=Path(__file__).resolve().parents[2]/'backport/cpp26/execution/when_all.hpp'
C=None
COV=set()
REFS={
 'register_outer':'__op::register_outer_stop_callback | __stop_callback.emplace(token, __outer_stop_callback{this});',
 'start':'__op::start | register_outer_stop_callback();',
 'start_children':'__op::__start_children | (std::execution::start(std::get<Is>(__children).__op), ...);',
 'value':'__child_recv::set_value | __self->__disp == op_t::__disposition::started',
 'store_value':'__child_recv::set_value | __self->child_fail(std::current_exception());',
 'fail':'__op::child_fail | __disp.exchange(__disposition::error)',
 'store_error':'__op::child_fail | __result.template emplace<1>( std::in_place_type<std::decay_t<E>>, static_cast<E&&>(e));',
 'stopped':'__op::child_stop | __disp.compare_exchange_strong( expected, __disposition::stopped)',
 'store_stopped':'__op::child_stop | __result.template emplace<2>(__stopped_tag{});',
 'done':'__op::child_done | if (--__pending == 0) deliver();',
 'outer_load':'__op::__outer_stop_callback::operator() | self->__pending.load(std::memory_order_acquire)',
 'outer_cas':'__op::__outer_stop_callback::operator() | self->__pending.compare_exchange_weak( pending, pending + 1, std::memory_order_acq_rel)',
 'outer_request':'__op::__outer_stop_callback::operator() | self->__stop_src.request_stop();',
 'deregister_outer':'__op::deliver | __stop_callback.reset();',
 'deliver':'__op::deliver | if (__result.index() == 1)',
 'request':'__op::child_stop | __stop_src.request_stop();',
}
# The throwing and nonthrowing error paths have identical storage fragments.
REFS['store_error']='model-only: child_fail stores selected error under __mtx; both construction paths'
ACTIONS='''construct start register_outer start_children complete value store_value fail stopped store_error store_stopped result_lock result_unlock request select request_end outer_stop outer_load outer_cas outer_request outer_return done deregister_outer deliver destroy deregister_child child_end source_end receiver_end op_end signal await'''.split()+['inner.'+a for a in INNER_ACTIONS+['complete']]
STEPS={a:SimpleNamespace(lines=REFS.get(a,'model-only: library/child contract, script, member initialization/destruction'),desc=a) for a in ACTIONS}
FIELDS='''threads errors children alive source_alive receiver_alive pending disp result partial expected_error expected_stopped child_events received required_stop outer_obligation throw_obligation literal_outer req_active signals local cas_spur mutex_owner mutex_pub vc views hist accesses destroy_started started first_stored_error'''.split()
class Cfg:
 throwing_values=()
 def __init__(self,**kw):
  self.name='cfg'; self.threads=(); self.payloads=('value','value'); self.modes=('own','own'); self.registrations=('start','start'); self.inline_destroy=True
  self.value_slots=True; self.stop_possible=True; self.registration_throw=False; self.hb=False; self.stale=False; self.mut=''; self.control=''; self.required_coverage=()
  self.__dict__.update(kw)
class World: pass
class Proxy:
 def __init__(self,w,i): object.__setattr__(self,'w',w); object.__setattr__(self,'i',i)
 def __getattr__(self,f):
  return self.w.children[self.i][INNER_FIELDS.index(f)] if f in INNER_FIELDS else getattr(self.w,f)
 def __setattr__(self,f,v):
  if f in INNER_FIELDS: change(self.w,'children',self.i,put(self.w.children[self.i],INNER_FIELDS.index(f),v))
  else: setattr(self.w,f,v)
class Child(Inner):
 def execute(self,w,t,op,alt):
  # A simple inplace token has one registration and no fused-token precheck.
  if op[0]=='prepare': self.prepend(w,t,('register',0)); return True
  # The legal child's internal readiness synchronization is a library contract,
  # separate from registration publication and from parent's pending chain.
  if op[0]=='fn' and w.child_ready:
   self.mem.acquire(w,t,dict(w.signals)['child_started.'+str(w.i)])
  return super().execute(w,t,op,alt)
def key(w): return tuple(getattr(w,f) for f in FIELDS)
def state(k):
 w=World()
 for f,v in zip(FIELDS,k): setattr(w,f,v)
 return w
def error(w,e): w.errors+=(e,)
def prepend(w,t,*ops): change(w,'threads',t,tuple(ops)+w.threads[t])
def child_prepend(p,t,*ops): prepend(p.w,t,*(('inner',p.i,op) for op in ops))
def signal(w,t,name):
 d=dict(w.signals); d[name]=synchronization(w,t); w.signals=tuple(sorted(d.items()))
def child_signal(p,t,name): signal(p.w,t,name+'.'+str(p.i))
def touch(w,t,write=False):
 if not w.alive: error(w,'W5: operation access after destruction')
 mem.access(w,t,'operation lifetime',write)
def child_touch(p,t):
 touch(p.w,t)
 if not p.child_alive: error(p,'W5: child access after destruction '+str(p.i))
 mem.access(p,t,'child lifetime '+str(p.i))
def source(p,t,slot):
 w=p.w
 if p.i==len(C.payloads): mem.access(w,t,'outer source'); return
 touch(w,t)
 if not w.source_alive: error(w,'W5: internal source access after destruction')
 mem.access(w,t,'internal source lifetime')
def source_touch(w,t): source(Proxy(w,0),t,0)
def outer_touch(p,t): touch(p.w,t); mem.access(p,t,'outer callback lifetime')
def initial():
 w=World(); n=len(C.threads); z=(0,)*n; b=(0,)*5; pub=(z,b); N=len(C.payloads)
 w.threads=C.threads; w.errors=(); w.alive=w.source_alive=w.receiver_alive=True
 w.pending=N; w.disp='started'; w.result='empty'; w.partial=(False,)*N if C.value_slots else ()
 w.first_stored_error=-1; w.expected_error=-1; w.expected_stopped=False; w.child_events=(); w.received=(); w.required_stop=False; w.outer_obligation=False; w.throw_obligation=False; w.literal_outer=False
 w.req_active=(0,)*n; w.signals=(); w.local=(-1,)*n; w.cas_spur=(False,)*n
 w.mutex_owner=-1; w.mutex_pub=pub; w.accesses=(); w.destroy_started=w.started=False
 w.vc=tuple(tuple(int(t==u) for u in range(n)) for t in range(n)); w.views=(b,)*n
 w.hist=tuple(((v,z,pub),) for v in (N,False,False,'started',False))
 w.children=tuple(tuple(False for _ in INNER_FIELDS) for _ in range(N+1))
 for i in range(N+1):
  p=Proxy(w,i)
  for f in ('child_alive','child_started','child_ready','claimed','completion_obligation','completed','notice','prestart'): setattr(p,f,False)
  p.notice_pub=pub; lib.initialize(p,1)
 return w
def enabled(w,t,op):
 a=op[0]
 if a=='await': return op[1] in dict(w.signals)
 if a=='inner': return inners[op[1]].enabled(Proxy(w,op[1]),t,op[2])
 if a=='deregister_outer': return lib_outer.removable(Proxy(w,len(C.payloads)),t,0)
 if a=='deregister_child': return lib.removable(Proxy(w,op[1]),t,0)
 if a=='result_lock': return w.mutex_owner==-1
 return True
def choices(w,t,op):
 a=op[0]
 if a=='outer_load': return mem.load_choices(w,t,0)
 if a=='outer_cas': return (0,1) if not w.cas_spur[t] else (0,)
 if a=='select':
  pending=tuple(i for i in range(len(C.payloads)) if Proxy(w,i).callbacks.status[0]=='pending')
  return pending or (-1,)
 if a=='inner': return inners[op[1]].choices(Proxy(w,op[1]),t,op[2])
 return (0,)
def execute(w,t,op,alt=0):
 a,*args=op; COV.add(a)
 N=len(C.payloads); outer=Proxy(w,N)
 if a=='signal': signal(w,t,args[0])
 elif a=='await': mem.acquire(w,t,dict(w.signals)[args[0]])
 elif a=='construct':
  prepend(w,t,*(('inner',i,('child_construct',)) for i in range(N)),('signal','connected'))
 elif a=='inner':
  i,action=args; p=Proxy(w,i); COV.add('inner.'+action[0])
  if action[0]=='complete':
   p.completed=True; prepend(w,t,('complete',i))
  else:
   inners[i].execute(p,t,action,alt)
   if action[0]=='register': COV.add('child.register.'+str(i)+'.'+p.callbacks.status[0])
 elif a=='start':
  touch(w,t); w.started=True
  if C.mut=='register_after_start': prepend(w,t,('start_children',),('register_outer',))
  else: prepend(w,t,('register_outer',),('start_children',))
 elif a=='register_outer':
  touch(w,t)
  if not C.stop_possible: COV.add('outer.impossible'); return
  if C.registration_throw:
   w.throw_obligation=True; COV.add('outer.throw')
   if C.control!='drop_throw_request': prepend(w,t,('request',))
   return
  if lib_outer.register(outer,t,0,4): prepend(w,t,('outer_load',),('outer_return',))
 elif a=='start_children':
  touch(w,t); prepend(w,t,*(('inner',i,('child_start',)) for i in range(N)))
 elif a=='complete':
  i=args[0]; child_touch(Proxy(w,i),t)
  # Child completion linearizes at disposition arbitration (or value storage).
  w.child_events+=(i,)
  kind=C.payloads[i]
  if kind=='error': prepend(w,t,('fail',i))
  elif kind=='stopped': prepend(w,t,('stopped',i))
  else: prepend(w,t,('value',i))
 elif a=='value':
  i=args[0]; touch(w,t); observed_disp=None
  if C.value_slots:
   observed_disp=mem.load(w,t,3,len(w.hist[3])-1)
  emplace=observed_disp=='started'
  if observed_disp is None:
   COV.add('value.skip_no_slots')
   emplace=C.control=='store_without_slots'
  elif not emplace:
   COV.add('value.skip_disposition')
   if observed_disp=='stopped' and i in C.throwing_values:
    COV.add('value.skip_throw_after_stop')
    emplace=C.control=='store_after_stopped'
  prepend(w,t,('store_value',i,observed_disp) if emplace else ('done',))
 elif a=='store_value':
  i,observed_disp=args; touch(w,t)
  if observed_disp is None:
   error(w,'W2: value emplacement without slots')
  else:
   if observed_disp!='started':
    error(w,'W2: value emplacement after observing '+observed_disp+' disposition')
   if i in C.throwing_values:
    COV.add('value.throw_from_'+w.disp)
    prepend(w,t,('fail',i)); return
   mem.access(w,t,'partial '+str(i),True); change(w,'partial',i,True)
  prepend(w,t,('done',))
 elif a=='fail':
  i=args[0]; touch(w,t); w.required_stop=True
  if w.expected_error==-1: w.expected_error=i
  old=w.disp; w.disp='error'; mem.publish(w,t,3,w.disp,'acq_rel',rmw=True)
  if old!='error' or C.mut=='overwrite_error':
   req=() if C.mut=='no_error_stop' else (('request',),)
   prepend(w,t,*req,('result_lock',),('store_error',i),('result_unlock',),('done',))
  else: prepend(w,t,('done',))
 elif a=='stopped':
  touch(w,t); w.expected_stopped=True; w.required_stop=True
  success=w.disp=='started'
  if C.mut!='stop_over_error': mem.load(w,t,3,len(w.hist[3])-1)
  if success or C.mut=='stop_over_error':
   if C.mut!='stop_over_error':
    w.disp='stopped'; mem.publish(w,t,3,w.disp,'acq_rel',rmw=True)
   prepend(w,t,('result_lock',),('store_stopped',),('result_unlock',))
  # A failed CAS performs no atomic write.
  req=() if C.mut=='no_stopped_stop' else (('request',),)
  change(w,'threads',t,w.threads[t][:3]+req+(('done',),)+w.threads[t][3:] if success or C.mut=='stop_over_error' else req+(('done',),)+w.threads[t])
 elif a=='result_lock': touch(w,t); mem.acquire(w,t,w.mutex_pub); w.mutex_owner=t
 elif a=='result_unlock': touch(w,t); w.mutex_pub=synchronization(w,t); w.mutex_owner=-1
 elif a=='store_error':
  touch(w,t); mem.access(w,t,'result',True)
  if w.first_stored_error==-1: w.first_stored_error=args[0]
  w.result='error.'+str(args[0])
 elif a=='store_stopped':
  touch(w,t); mem.access(w,t,'result')
  if w.result=='empty' or C.control=='stop_without_result_guard':
   mem.access(w,t,'result',True); w.result='stopped'
 elif a in ('request','outer_request'):
  source_touch(w,t); change(w,'req_active',t,w.req_active[t]+1)
  if not w.hist[1][-1][0]:
   mem.publish(w,t,1,True,'release'); prepend(w,t,('select',),('request_end',))
  else: prepend(w,t,('request_end',))
 elif a=='select':
  source_touch(w,t)
  if alt!=-1:
   COV.add('select.child.'+str(alt))
   p=Proxy(w,alt); lib.cbset(p,'obligation',0,True); lib.cbset(p,'status',0,'executing'); lib.cbset(p,'owner',0,t)
   mem.acquire(w,t,p.callbacks.publication[0]); COV.add('select.'+str(alt))
   prepend(w,t,('inner',alt,('invoke',0)),('inner',alt,('invoke_end',0)),('select',))
 elif a=='request_end': source_touch(w,t); change(w,'req_active',t,w.req_active[t]-1)
 elif a=='outer_stop':
  if w.pending>0: w.literal_outer=True
  if lib_outer.request(outer,t,0,4): prepend(w,t,('outer_load',),('outer_return',))
 elif a=='outer_load':
  if outer.callbacks.status[0] in ('absent','removed'): error(w,'W5: entry into dead outer callback')
  outer_touch(outer,t)
  value=mem.load(w,t,0,alt); change(w,'local',t,value)
  if value or C.mut=='credit_without_check': prepend(w,t,('outer_cas',))
  else: COV.add('outer.zero')
 elif a=='outer_cas':
  if outer.callbacks.status[0] in ('absent','removed'): error(w,'W5: entry into dead outer callback')
  outer_touch(outer,t)
  expected=w.local[t]
  if not alt and expected==w.pending and w.pending>0: w.outer_obligation=True
  # Failure orders follow the success order; failures never modify MO.
  observed=mem.load(w,t,0,len(w.hist[0])-1,'relaxed' if C.mut=='relaxed_credit' else 'acquire')
  if alt:
   change(w,'local',t,observed)
   change(w,'cas_spur',t,True); COV.add('cas.spurious')
   if observed or C.mut=='credit_without_check': prepend(w,t,('outer_cas',))
   return
  if expected!=w.pending:
   change(w,'local',t,w.pending)
   if w.pending or C.mut=='credit_without_check': prepend(w,t,('outer_cas',))
   return
  w.pending+=1; mem.publish(w,t,0,w.pending,'relaxed' if C.mut=='relaxed_credit' else 'acq_rel',rmw=True)
  COV.add('outer.credit')
  if C.control=='drop_outer_forward': prepend(w,t,('done',))
  elif C.mut=='request_after_done': prepend(w,t,('done',),('outer_request',))
  else: prepend(w,t,('outer_request',),('done',))
 elif a=='outer_return': lib_outer.invocation_end(outer,t,0)
 elif a=='done':
  if C.control=='drop_done': return
  touch(w,t); w.pending-=1
  mem.publish(w,t,0,w.pending,'relaxed' if C.mut=='relaxed_pending' or C.control=='credit_without_pending_release' else 'acq_rel',rmw=True)
  if w.pending==0:
   if C.mut=='no_deregister' or C.control=='member_without_deregister': prepend(w,t,('deliver',))
   elif C.mut=='deregister_after_deliver': prepend(w,t,('deliver',),('deregister_outer',))
   else: prepend(w,t,('deregister_outer',),('deliver',))
 elif a=='deregister_outer':
  touch(w,t)
  if C.control!='skip_outer_remove' and outer.callbacks.status[0] not in ('absent','removed'):
   lib_outer.deregister(outer,t,0); mem.access(w,t,'outer callback lifetime',True)
 elif a=='deliver':
  touch(w,t)
  if not w.receiver_alive: error(w,'W5: receiver access after destruction')
  mem.access(w,t,'receiver lifetime'); mem.access(w,t,'result')
  if outer.callbacks.status[0] not in ('absent','removed'): error(w,'W4: callback remains registered at delivery')
  if len(set(w.child_events))!=N: error(w,'W1: delivery before every child completion')
  expected='error.'+str(w.expected_error) if w.expected_error!=-1 else 'stopped' if w.expected_stopped else 'values'
  actual=w.result if w.result!='empty' else 'values'
  if C.throwing_values: COV.add('deliver.'+actual)
  if actual!=expected: error(w,'W2: disposition/outcome mismatch')
  if actual=='values':
   for i in range(len(w.partial)):
    mem.access(w,t,'partial '+str(i))
    if not w.partial[i]: error(w,'W2: missing partial value')
  if C.control!='drop_receiver': w.received+=(actual,)
  if C.inline_destroy: prepend(w,t,('destroy',))
  signal(w,t,'received')
 elif a=='destroy':
  if w.destroy_started: return
  w.destroy_started=True; touch(w,t)
  children=tuple(op for i in reversed(range(N)) for op in (('deregister_child',i),('child_end',i)))
  callback=(('deregister_outer',),)
  order=children+callback if C.mut=='callback_before_children' else callback+children
  prepend(w,t,*order,('source_end',),('receiver_end',),('op_end',))
 elif a=='deregister_child':
  i=args[0]
  if C.control!='skip_child_'+str(i): lib.deregister(Proxy(w,i),t,0)
 elif a=='child_end':
  i=args[0]; p=Proxy(w,i); child_touch(p,t)
  if Proxy(w,N).callbacks.status[0] not in ('absent','removed'): error(w,'W5: callback reset covering invariant at child destruction')
  for e in lib.registration_errors(p): error(w,'W5: child '+str(i)+' '+e)
  mem.access(w,t,'child lifetime '+str(i),True)
  if C.control!='skip_child_end_'+str(i): p.child_alive=False
 elif a=='source_end':
  source_touch(w,t)
  for i in range(N):
   for e in lib.registration_errors(Proxy(w,i)): error(w,'W5: source end '+str(i)+' '+e)
  if any(w.req_active): error(w,'W5: source destroyed inside request_stop')
  mem.access(w,t,'internal source lifetime',True)
  if C.control!='skip_source_end': w.source_alive=False
 elif a=='receiver_end':
  mem.access(w,t,'receiver lifetime',True)
  if C.control!='skip_receiver_end': w.receiver_alive=False
 elif a=='op_end':
  touch(w,t,True)
  for e in lib_outer.registration_errors(outer): error(w,'W5: outer '+e)
  if C.control!='skip_op_end': w.alive=False
 else: raise ValueError(op)
def successors(k):
 w=state(k)
 if w.errors: return []
 out=[]
 for t,script in enumerate(w.threads):
  if not script or not enabled(w,t,script[0]): continue
  for alt in choices(w,t,script[0]):
   v=copy.copy(w); change(v,'threads',t,script[1:])
   if C.hb or C.stale: change(v,'vc',t,put(v.vc[t],t,v.vc[t][t]+1))
   execute(v,t,script[0],alt); out.append(((t,script[0],alt),v))
 return out
def state_violations(w):
 e=list(w.errors)
 if len(w.received)>1: e.append('W1: duplicate receiver completion')
 if len(w.child_events)!=len(set(w.child_events)): e.append('W1: duplicate child completion')
 if w.first_stored_error!=-1 and w.result!='error.'+str(w.first_stored_error): e.append('W2: first stored error covering invariant')
 if w.received:
  if Proxy(w,len(C.payloads)).callbacks.status[0] not in ('absent','removed'): e.append('W4: reset covering invariant')
 if w.pending<0: e.append('W1: negative completion credits')
 for i in range(len(C.payloads)): e+=inners[i].errors(Proxy(w,i))
 return tuple(e)
def terminal_violations(w):
 e=list(state_violations(w))
 if any(w.threads): e.append('W4: deregistration wait cycle or unfinished script')
 if w.started and len(w.received)!=1: e.append('W1: lost receiver obligation')
 if (w.required_stop or w.outer_obligation or w.throw_obligation) and not w.hist[1][-1][0]: e.append('W3: lost stop obligation')
 if C.control=='literal_outer_forward' and w.literal_outer and not w.hist[1][-1][0]: e.append('W3: literal outer forwarding exceeds credit contract')
 for e0 in lib_outer.registration_errors(Proxy(w,len(C.payloads))): e.append('W5: outer '+e0)
 for i in range(len(C.payloads)):
  p=Proxy(w,i); e+=inners[i].errors(p,True)
  e+=['W5: child '+str(i)+' '+x for x in lib.registration_errors(p)]
  if p.child_alive: e.append('W5: child storage leak '+str(i))
 if w.alive or w.source_alive or w.receiver_alive: e.append('W5: operation storage leak')
 return tuple(e)
def is_blocked(k):
 w=state(k); return any(s and not enabled(w,t,s[0]) for t,s in enumerate(w.threads))
def is_aborted(w): return False
def metrics(): return {'max_store_buffer':0}
def coverage():
 if not C.mut and not C.control: assert set(C.required_coverage)<=COV,set(C.required_coverage)-COV
 return sorted(COV)
EXTRA_REFS=('__child_env::query | return __base->query(std::forward<Query>(query));',
 '__child_recv::get_env | return __env;',
 '__op::deliver_values | __values);')
def step_table():
 return [('protocol',a,s.lines,'atomic / plain / library / script',s.desc) for a,s in STEPS.items()]+[
  ('storage/env',str(i),r,'plain',r) for i,r in enumerate(EXTRA_REFS)]
def describe_initial(w): return repr(w.threads)
def describe(meta,w):
 from engine.source_lock import resolve
 t,op,*rest=meta; a='inner.'+op[2][0] if op[0]=='inner' else op[0]
 return '%s [%s] pending=%s result=%s events=%s alive=%s scripts=%s'%(meta,resolve(SOURCE.read_text(),STEPS[a].lines),w.pending,w.result,w.child_events,w.alive,w.threads)
def configure(cfg):
 global C,mem,lib,lib_outer,inners
 C=cfg; COV.clear(); mem=Memory(cfg,error); lib=Library(mem,error,COV,child_touch,source); lib_outer=Library(mem,error,COV,outer_touch,source)
 inners=[]
 for mode,registration in zip(cfg.modes,cfg.registrations):
  child_cfg=SimpleNamespace(**cfg.__dict__); child_cfg.mode=mode; child_cfg.registration=registration
  inners.append(Child(child_cfg,mem,lib,error,COV,child_prepend,child_signal,child_touch))

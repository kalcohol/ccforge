"""sync_wait receiver and driver over an abstract run_loop contract."""
import copy
from pathlib import Path
from types import SimpleNamespace
from engine.memory import Memory, put, change, synchronization
from engine.library_contracts import Library
from engine.inner_operation import Inner, FIELDS as INNER_FIELDS, ACTIONS as INNER_ACTIONS
SOURCE=Path(__file__).resolve().parents[2]/'backport/cpp26/execution/sync_wait.hpp'
C=None
COV=set()
REFS={
 'connect':'__driver::__run | auto op = std::execution::connect(',
 'start':'__driver::__run | std::execution::start(op);',
 'run':'__driver::__run | state.loop.run();',
 'read':'__driver::__run | if (state.error)',
 'store':'receiver::set_value | state_->result.emplace(std::forward<Vs>(vs)...);',
 'finish':'receiver::set_stopped | state_->loop.finish();',
}
ACTIONS='''connect start run read complete store finish finish_unlock finish_tail destroy_op child_end op_end state_end loop_end signal await result_probe op_probe loop_probe submit'''.split()+INNER_ACTIONS
STEPS={a:SimpleNamespace(lines=REFS.get(a,'model-only: abstract run_loop/child contract or implicit stack destruction'),desc=a) for a in ACTIONS}
EXTRA_REFS=('receiver::set_value | state_->error = std::current_exception();',
 'receiver::set_value | state_->loop.finish();',
 'receiver::set_error | state_->error = std::forward<E>(e);',
 'receiver::set_error | state_->loop.finish();')
FIELDS='''threads errors op_alive state_alive loop_alive result stores expected receiver_obligation finished finish_pub run_returned signals vc views hist accesses read_value queued push_pub'''.split()+INNER_FIELDS
class Cfg:
 def __init__(self,**kw):
  self.name='cfg'; self.threads=(); self.payload='value'; self.mode='own'; self.registration='none'; self.state_uses_loop=False
  self.hb=False; self.stale=False; self.mut=''; self.control=''; self.required_coverage=()
  self.__dict__.update(kw)
class World: pass
def key(w): return tuple(getattr(w,f) for f in FIELDS)
def state(k):
 w=World()
 for f,v in zip(FIELDS,k): setattr(w,f,v)
 return w
def error(w,e): w.errors+=(e,)
def prepend(w,t,*ops): change(w,'threads',t,tuple(ops)+w.threads[t])
def touch(w,t,which,write=False):
 if not getattr(w,which+'_alive'): error(w,'SW2: '+which+' access after destruction')
 mem.access(w,t,which+' lifetime',write)
def child_touch(w,t):
 touch(w,t,'op')
 if not w.child_alive: error(w,'SW2: child access after destruction')
 mem.access(w,t,'child lifetime')
def source(w,t,slot): pass # No stoppable query in the sync_wait environment.
def signal(w,t,name):
 d=dict(w.signals); d[name]=synchronization(w,t); w.signals=tuple(sorted(d.items()))
def initial():
 w=World(); n=len(C.threads); z=(0,)*n; b=(0,0,0); pub=(z,b)
 COV.add('construct.state_loop' if C.mut=='loop_after_state' else 'construct.loop_state')
 w.threads=C.threads; w.errors=(); w.op_alive=False; w.state_alive=w.loop_alive=True
 w.result='empty'; w.stores=0; w.expected=''; w.receiver_obligation=False
 w.queued=False; w.push_pub=pub; w.finished=w.run_returned=False; w.finish_pub=pub; w.read_value=''; w.signals=(); w.accesses=()
 w.vc=tuple(tuple(int(t==u) for u in range(n)) for t in range(n)); w.views=(b,)*n
 w.hist=tuple(((False,z,pub),) for _ in range(3))
 for f in ('child_alive','child_started','child_ready','claimed','completion_obligation','completed','notice','prestart'): setattr(w,f,False)
 w.notice_pub=pub; lib.initialize(w,2)
 return w
def enabled(w,t,op):
 if op[0]=='await': return op[1] in dict(w.signals)
 if op[0]=='run': return w.finished or w.queued
 return inner.enabled(w,t,op)
def execute(w,t,op,alt=0):
 a,*args=op; COV.add(a)
 if inner.execute(w,t,op,alt): return
 if a=='signal': signal(w,t,args[0])
 elif a=='await': mem.acquire(w,t,dict(w.signals)[args[0]])
 elif a=='connect':
  w.op_alive=True; touch(w,t,'op',True); prepend(w,t,('child_construct',))
 elif a=='start':
  touch(w,t,'op'); w.receiver_obligation=True; w.expected=C.payload
  ops=(('child_start',),('run',),('read',),('destroy_op',))
  if C.mut=='read_before_run': ops=(('child_start',),('read',),('run',),('destroy_op',))
  prepend(w,t,*ops)
 elif a=='complete':
  child_touch(w,t); w.completed=True
  ops=(('finish',),) if C.payload=='stopped' else (('store',),('finish',))
  if C.mut=='finish_before_store': ops=tuple(reversed(ops))
  if C.control=='double_store': ops=(('store',),)+ops
  prepend(w,t,*ops)
 elif a=='store':
  touch(w,t,'state'); mem.access(w,t,'result',True)
  if C.control!='drop_store': w.result=C.payload; w.stores+=1
 elif a=='finish':
  touch(w,t,'op') # Load the receiver's state pointer before entering the library.
  touch(w,t,'loop')
  if C.control!='drop_finish': prepend(w,t,('finish_unlock',),('finish_tail',))
 elif a=='finish_unlock':
  # The abstract library's final access and release, with no later loop access.
  touch(w,t,'loop'); w.finished=True; w.finish_pub=synchronization(w,t)
 elif a=='finish_tail':
  COV.add('finish.stack_return')
  if C.control=='after_finish_state': prepend(w,t,('result_probe',))
  if C.control=='after_finish_op': prepend(w,t,('op_probe',))
  if C.control=='after_finish_loop': prepend(w,t,('loop_probe',))
 elif a=='submit':
  touch(w,t,'loop'); w.queued=True; w.push_pub=synchronization(w,t)
 elif a=='run':
  touch(w,t,'loop')
  if w.queued:
   # Imported exactly-once task dispatch, without queue/mutex internals.
   mem.acquire(w,t,w.push_pub); w.queued=False
   COV.add('run.contract_task'); prepend(w,t,('produce',),('run',))
  else:
   if C.control!='no_finish_handoff': mem.acquire(w,t,w.finish_pub)
   w.run_returned=True; COV.add('run.contract_return')
 elif a=='read':
  touch(w,t,'state'); mem.access(w,t,'result')
  if not w.run_returned: error(w,'SW1: result read before run returned')
  w.read_value='stopped' if w.result=='empty' else w.result
  if w.read_value!=w.expected: error(w,'SW1: read did not see completion')
 elif a=='destroy_op':
  # The operation and receiver may borrow both state and loop until destruction.
  touch(w,t,'state'); touch(w,t,'loop'); touch(w,t,'op')
  prepend(w,t,('child_end',),('op_end',),*( (('loop_end',),('state_end',)) if C.mut=='loop_after_state' else (('state_end',),('loop_end',))))
 elif a=='child_end':
  child_touch(w,t); mem.access(w,t,'child lifetime',True); w.child_alive=False
 elif a=='op_end':
  touch(w,t,'op',True)
  if C.control!='skip_op_end': w.op_alive=False
 elif a=='state_end':
  touch(w,t,'state',True); mem.access(w,t,'result',True)
  if C.state_uses_loop: touch(w,t,'loop')
  if C.control!='skip_state_end': w.state_alive=False
 elif a=='loop_end':
  touch(w,t,'loop',True)
  if C.control!='skip_loop_end': w.loop_alive=False
 elif a=='result_probe': touch(w,t,'state'); mem.access(w,t,'result')
 elif a=='op_probe': touch(w,t,'op')
 elif a=='loop_probe': touch(w,t,'loop')
 else: raise ValueError(op)
def successors(k):
 w=state(k)
 if w.errors: return []
 out=[]
 for t,script in enumerate(w.threads):
  if not script or not enabled(w,t,script[0]): continue
  for alt in inner.choices(w,t,script[0]):
   v=copy.copy(w); change(v,'threads',t,script[1:])
   if C.hb or C.stale: change(v,'vc',t,put(v.vc[t],t,v.vc[t][t]+1))
   execute(v,t,script[0],alt); out.append(((t,script[0]),v))
 return out
def state_violations(w):
 e=list(w.errors)+inner.errors(w)
 if w.stores>1: e.append('SW3: duplicate result store')
 return tuple(e)
def terminal_violations(w):
 e=list(state_violations(w))+inner.errors(w,True)
 if any(w.threads): e.append('SW1: unfinished run contract')
 if w.receiver_obligation and w.stores!=int(C.payload!='stopped'): e.append('SW3: wrong number of result stores')
 if w.op_alive or w.state_alive or w.loop_alive or w.child_alive: e.append('SW2: storage leak')
 return tuple(e)
def is_blocked(k):
 w=state(k); return any(s and not enabled(w,t,s[0]) for t,s in enumerate(w.threads))
def is_aborted(w): return False
def metrics(): return {'max_store_buffer':0}
def coverage():
 if not C.mut and not C.control: assert set(C.required_coverage)<=COV,set(C.required_coverage)-COV
 return sorted(COV)
def step_table(): return [('protocol',a,s.lines,'plain / library / script',s.desc) for a,s in STEPS.items()]+[('receiver',str(i),r,'plain',r) for i,r in enumerate(EXTRA_REFS)]
def describe_initial(w): return repr(w.threads)
def describe(meta,w):
 from engine.source_lock import resolve
 return '%s [%s] result=%s alive=%s scripts=%s'%(meta,resolve(SOURCE.read_text(),STEPS[meta[1][0]].lines),w.result,(w.op_alive,w.state_alive,w.loop_alive),w.threads)
def configure(cfg):
 global C,mem,lib,inner
 C=cfg; COV.clear(); mem=Memory(cfg,error); lib=Library(mem,error,COV,child_touch,source)
 inner=Inner(cfg,mem,lib,error,COV,prepend,signal,child_touch)

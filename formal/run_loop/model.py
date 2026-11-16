"""run_loop.hpp queue, predicate wait, execution and final unlock lifetimes."""
import copy
from pathlib import Path
from types import SimpleNamespace
from engine.memory import Memory, put, change, synchronization, empty_sync
SOURCE=Path(__file__).resolve().parents[2]/'backport/cpp26/execution/run_loop.hpp'
C=None
COV=set()
REFS={
 'push':'run_loop::__push | std::lock_guard lk{__mtx_};',
 'link':'run_loop::__push | t->__next = nullptr;',
 'enqueue':'run_loop::__push | __tail_ = t;',
 'increment':'run_loop::__push | ++__task_count_;',
 'notify_push':'run_loop::__push | __cv_.notify_one();',
 'pop':'run_loop::__pop | std::unique_lock lk{__mtx_};',
 'predicate':'run_loop::__pop | __cv_.wait(lk,',
 'take':'run_loop::__pop | auto* t = __head_;',
 'execute':'run_loop::run | t->__execute(t);',
 'stop_query':'model-only: lambda body in __op constructor: acquire receiver token query',
 'complete':'model-only: lambda body in __op constructor: set_value/set_stopped',
 'decrement':'run_loop::run | --__task_count_;',
 'finish':'run_loop::finish | std::lock_guard lk{__mtx_};',
 'set_finishing':'run_loop::finish | __finishing_ = true;',
 'notify_finish':'run_loop::finish | __cv_.notify_all();',
 'destroy':'run_loop::~run_loop | if (__task_count_ > 0)',
 'run_return':'run_loop::run | break;',
}
ACTIONS='''push link enqueue increment notify_push pop predicate take execute stop_query complete count_lock decrement finish set_finishing notify_finish unlock wait wake run_return destroy task_end stop signal await tail tail_notify execute_return'''.split()
STEPS={a:SimpleNamespace(lines=REFS.get(a,'model-only: mutex/CV contract, script or implicit destruction'),desc=a) for a in ACTIONS}
FIELDS='''threads errors alive owner mutex_pub queue count finishing waiting notified local pushed executions returned completions task_alive signals vc views hist accesses aborted obligations spur active stop_seen'''.split()
class Cfg:
 def __init__(self,**kw):
  self.name='cfg'; self.threads=(); self.tasks=1; self.run_thread=1; self.hb=False; self.stale=False; self.mut=''; self.control=''; self.self_destroy=True; self.completion_finish=False; self.strict=False; self.hold_after_complete=False; self.required_coverage=(); self.required_stale_coverage=()
  self.__dict__.update(kw)
class World: pass
def key(w): return tuple(getattr(w,f) for f in FIELDS)
def state(k):
 w=World()
 for f,v in zip(FIELDS,k): setattr(w,f,v)
 return w
def error(w,e): w.errors+=(e,)
def prepend(w,t,*ops): change(w,'threads',t,tuple(ops)+w.threads[t])
def touch(w,t,write=False):
 if not w.alive: error(w,'R4: loop access after destruction')
 mem.access(w,t,'loop lifetime',write)
def plain(w,t,name,write=False): touch(w,t); mem.access(w,t,name,write)
def task_touch(w,t,i,write=False):
 if not w.task_alive[i]: error(w,'R1: task access after destruction')
 mem.access(w,t,'task '+str(i),write)
def signal(w,t,name):
 d=dict(w.signals); d[name]=synchronization(w,t); w.signals=tuple(sorted(d.items()))
def initial():
 w=World(); n=len(C.threads); z=(0,)*n; b=(0,)*C.tasks; pub=(z,b)
 w.threads=C.threads; w.errors=(); w.alive=True; w.owner=-1; w.mutex_pub=pub
 w.queue=(); w.count=0; w.finishing=False; w.waiting=False; w.notified=False
 w.local=(-1,)*n; w.pushed=(); w.executions=(); w.returned=False; w.completions=(); w.task_alive=(True,)*C.tasks
 w.stop_seen=(None,)*C.tasks; w.spur=False; w.active=(); w.signals=(); w.accesses=(); w.aborted=False; w.obligations=tuple(range(C.tasks))
 w.vc=tuple(tuple(int(t==u) for u in range(n)) for t in range(n)); w.views=(b,)*n
 w.hist=tuple(((False,z,pub),) for _ in range(C.tasks))
 return w
def lock(w,t):
 touch(w,t); w.owner=t; mem.acquire(w,t,w.mutex_pub)
def enabled(w,t,op):
 a=op[0]
 if a=='await': return op[1] in dict(w.signals)
 if a in ('push','pop','count_lock') or a=='finish' and C.mut!='finish_unlocked': return w.owner==-1
 if a=='wake': return w.waiting and w.owner==-1 and (w.notified or not w.spur)
 return True
def choices(w,t,op):
 if op[0]=='stop_query': return mem.load_choices(w,t,op[1])
 return (0,)
def execute(w,t,op,alt=0):
 a,*args=op; COV.add(a)
 if a=='signal': signal(w,t,args[0])
 elif a=='await': mem.acquire(w,t,dict(w.signals)[args[0]])
 elif a=='push':
  # Omit the entire submission, retaining the independent initial obligation.
  if C.control!='drop_push':
   lock(w,t); prepend(w,t,('link',args[0]),('enqueue',args[0]),('increment',args[0]),('notify_push',),('unlock',),('tail','push'))
 elif a=='link':
  # Represent node writes as queue accesses; dispatch uses the logical FIFO.
  i=args[0]; task_touch(w,t,i); plain(w,t,'queue',True)
 elif a=='enqueue':
  i=args[0]; plain(w,t,'queue',True); w.queue+=(i,); w.pushed+=(i,); signal(w,t,'enqueued.'+str(i))
  COV.add('enqueue.%d.%s_finish'%(i,'after' if w.finishing else 'before'))
 elif a=='increment':
  plain(w,t,'count',True)
  if C.mut!='push_no_count': w.count+=1
 elif a in ('notify_push','notify_finish'):
  if C.mut==('notify_push_outside' if a=='notify_push' else 'notify_finish_outside') and w.owner==t:
   assert w.threads[t][0]==('unlock',)
   change(w,'threads',t,w.threads[t][1:])
   prepend(w,t,('unlock',),('tail_notify',a))
  else:
   touch(w,t); w.notified=w.waiting or w.notified
 elif a=='tail_notify':
  touch(w,t); w.notified=w.waiting or w.notified
 elif a=='unlock':
  touch(w,t); assert w.owner==t
  w.mutex_pub=synchronization(w,t); w.owner=-1
 elif a=='tail':
  COV.add('tail.'+args[0])
  if not w.alive: COV.add('tail.after_destroy.'+args[0]) # Stack-only: unlock was the final access.
 elif a=='pop': w.spur=False; lock(w,t); prepend(w,t,('predicate',))
 elif a=='predicate':
  plain(w,t,'queue'); plain(w,t,'finishing')
  if C.mut=='wait_no_predicate': prepend(w,t,('wait',))
  elif w.queue or w.finishing and C.mut!='pop_ignore_finish': prepend(w,t,('take',))
  else: prepend(w,t,('wait',))
 elif a=='wait':
  # Predicate evaluation and CV wait's unlock/arming are indivisible under the mutex.
  touch(w,t); w.waiting=True; w.notified=False; w.owner=-1; w.mutex_pub=synchronization(w,t)
  prepend(w,t,('wake',))
 elif a=='wake':
  lock(w,t); w.waiting=False
  if not w.notified: w.spur=True
  COV.add('wake.notified' if w.notified else 'wake.spurious')
  prepend(w,t,('take',) if C.mut=='wait_no_predicate' else ('predicate',))
 elif a=='take':
  plain(w,t,'queue',True)
  if w.queue:
   i=w.queue[0]; task_touch(w,t,i); w.queue=w.queue[1:]; change(w,'local',t,i)
   if C.mut=='decrement_before_execute': prepend(w,t,('unlock',),('count_lock',),('decrement',),('unlock',),('execute',i))
   else: prepend(w,t,('unlock',),('execute',i))
  else: prepend(w,t,('unlock',),('run_return',))
 elif a=='execute':
  i=args[0]; task_touch(w,t,i)
  if t!=C.run_thread: error(w,'R1: execution off run thread')
  w.executions+=(i,); w.active+=(i,)
  prepend(w,t,('stop_query',i),('complete',i))
  if C.mut!='decrement_before_execute':
   change(w,'threads',t,w.threads[t][:2]+(('execute_return',i),('count_lock',),('decrement',),('unlock',),('pop',))+w.threads[t][2:])
  else: change(w,'threads',t,w.threads[t][:2]+(('execute_return',i),('pop',))+w.threads[t][2:])
 elif a=='stop_query':
  i=args[0]; task_touch(w,t,i)
  if len(mem.load_choices(w,t,i))>1: COV.add('token.multiple_choices')
  value=mem.load(w,t,i,alt)
  change(w,'stop_seen',i,value)
  COV.add('task.stopped' if value else 'task.value')
 elif a=='complete':
  i=args[0]; task_touch(w,t,i)
  actual=not w.stop_seen[i] if C.control=='wrong_kind' else w.stop_seen[i]
  if actual!=w.stop_seen[i]: error(w,'R1: incorrect stop outcome')
  w.completions+=(i,)
  signal(w,t,'complete.'+str(i))
  if C.hold_after_complete: prepend(w,t,('await','never'))
  if C.self_destroy and C.control!='skip_task_end': prepend(w,t,('task_end',i))
  if C.completion_finish: prepend(w,t,('finish',))
  if C.control=='drop_complete': w.completions=w.completions[:-1]
 elif a=='execute_return':
  w.active=tuple(i for i in w.active if i!=args[0])
 elif a=='task_end':
  i=args[0]; task_touch(w,t,i,True); change(w,'task_alive',i,False)
 elif a=='count_lock': lock(w,t)
 elif a=='decrement': plain(w,t,'count',True); w.count-=1
 elif a=='finish':
  if C.mut!='finish_unlocked': lock(w,t)
  prepend(w,t,('set_finishing',),('notify_finish',),*( () if C.mut=='finish_unlocked' else (('unlock',),)),('tail','finish'))
 elif a=='set_finishing': plain(w,t,'finishing',True); w.finishing=True
 elif a=='run_return':
  if C.control=='drop_return': return
  if not w.finishing or w.queue: error(w,'R2: return without finish and empty queue')
  w.returned=True; signal(w,t,'returned')
 elif a=='destroy':
  touch(w,t); plain(w,t,'count')
  remaining=tuple(i for i in w.pushed if i not in w.completions or i in w.active)
  if remaining and not w.count: error(w,'R5: destructor count disagrees with outstanding execution')
  if w.count:
   if C.strict: w.aborted=True; w.threads=tuple(() for _ in w.threads); COV.add('destructor.terminate'); return
   error(w,'R5: destruction with outstanding tasks')
  touch(w,t,True)
  if C.control!='skip_loop_end': w.alive=False
 elif a=='stop': mem.publish(w,t,args[0],True,'release')
 else: raise ValueError(op)
def successors(k):
 w=state(k)
 if w.errors or w.aborted: return []
 out=[]
 for t,script in enumerate(w.threads):
  if not script or not enabled(w,t,script[0]): continue
  for alt in choices(w,t,script[0]):
   v=copy.copy(w); change(v,'threads',t,script[1:])
   if C.hb or C.stale: change(v,'vc',t,put(v.vc[t],t,v.vc[t][t]+1))
   execute(v,t,script[0],alt); out.append(((t,script[0]),v))
 return out
def state_violations(w):
 e=list(w.errors)
 if len(w.executions)!=len(set(w.executions)) or len(w.completions)!=len(set(w.completions)): e.append('R1: duplicate execution/completion')
 if w.count<0: e.append('R5: negative count')
 return tuple(e)
def terminal_violations(w):
 e=list(state_violations(w))
 if w.aborted: return tuple(e)
 if any(w.threads): e.append('R3: unfinished wait or lost wakeup')
 if sorted(w.pushed)!=sorted(w.executions) or sorted(w.executions)!=sorted(w.completions): e.append('R1: lost execution obligation')
 if sorted(w.pushed)!=list(w.obligations): e.append('R1: lost push obligation')
 if not w.returned and not C.strict: e.append('R2: lost run return')
 if w.alive: e.append('R4: loop storage leak')
 for i in w.completions:
  if C.self_destroy and w.task_alive[i]: e.append('R1: task storage leak '+str(i))
 return tuple(e)
def is_blocked(k):
 w=state(k); return w.waiting or any(s and not enabled(w,t,s[0]) for t,s in enumerate(w.threads))
def is_aborted(w): return w.aborted
def metrics(): return {'max_store_buffer':0}
def coverage():
 if not C.mut and not C.control:
  required=set(C.required_coverage)
  if C.stale: required.update(C.required_stale_coverage)
  assert required<=COV,required-COV
 return sorted(COV)
def step_table(): return [('protocol',a,s.lines,'mutex / plain / library / script',s.desc) for a,s in STEPS.items()]
def describe_initial(w): return repr(w.threads)
def describe(meta,w):
 from engine.source_lock import resolve
 return '%s [%s] queue=%s count=%s finish=%s owner=%s alive=%s scripts=%s'%(meta,resolve(SOURCE.read_text(),STEPS[meta[1][0]].lines),w.queue,w.count,w.finishing,w.owner,w.alive,w.threads)
def configure(cfg):
 global C,mem
 C=cfg; COV.clear(); mem=Memory(cfg,error)

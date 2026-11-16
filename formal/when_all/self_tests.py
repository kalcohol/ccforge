"""Independent obligations, shared oracle regression and source mapping."""
import ast
from pathlib import Path
from engine.oracle import check, fixtures
from . import model as m

def run():
 protected={'expected_error','expected_stopped','child_events','required_stop','outer_obligation','throw_obligation','started'}
 check(ast.parse(Path(m.__file__).read_text()),protected); fixtures(protected)
 from engine.source_lock import resolve
 for row in m.step_table(): resolve(m.SOURCE.read_text(),row[2])
 m.configure(m.Cfg(threads=((),())))
 w=m.initial(); w.started=True
 assert 'W1: lost receiver obligation' in m.terminal_violations(w)
 w.required_stop=True
 assert 'W3: lost stop obligation' in m.terminal_violations(w)
 for a in m.STEPS:
  op=('inner',0,(a[6:],)) if a.startswith('inner.') else (a,)
  assert a.split('.')[-1] in m.describe((0,op),w)
 # Every child registration and every storage has an independent end check.
 for i in (0,1):
  for status in ('pending','executing','completed','inline'):
   m.configure(m.Cfg(threads=((),())))
   w=m.initial(); p=m.Proxy(w,i); p.child_alive=True
   m.lib.cbset(p,'constructed',0,True); m.lib.cbset(p,'status',0,status)
   assert any('child '+str(i)+' registration' in e for e in m.terminal_violations(w))
   m.execute(w,0,('child_end',i))
   assert any('child '+str(i)+' registration' in e for e in w.errors)
 for i in (0,1):
  for action in (('invoke',0),('fn',)):
   m.configure(m.Cfg(threads=((),())))
   w=m.initial(); m.execute(w,0,('inner',i,action))
   assert any('child access after destruction' in e for e in w.errors)
 for action in ('outer_load','outer_cas'):
  m.configure(m.Cfg(threads=((),())))
  w=m.initial(); m.execute(w,0,(action,))
  assert 'W5: entry into dead outer callback' in w.errors
 # Failed strong/weak CAS never writes; spurious failure rechecks zero.
 m.configure(m.Cfg(threads=((),()),stale=True))
 w=m.initial(); w.disp='error'; before=w.hist
 m.execute(w,0,('stopped',0)); assert w.hist==before
 w=m.initial(); o=m.Proxy(w,2); m.lib_outer.cbset(o,'status',0,'executing')
 w.pending=0; m.mem.publish(w,0,0,0,'relaxed'); m.change(w,'local',0,2); before=w.hist
 m.execute(w,0,('outer_cas',),1)
 assert w.hist==before and ('outer_cas',) not in w.threads[0]
 # The faithful CAS-removal mutant retains the independent result guard.
 m.configure(m.Cfg(threads=((),),mut='stop_over_error'))
 w=m.initial(); w.disp='error'; w.result='error.0'; before=w.hist
 m.execute(w,0,('stopped',1)); assert w.hist==before
 m.execute(w,0,('store_stopped',)); assert w.result=='error.0'
 # Lifetime end remains a conflicting write, with no synthetic handoff.
 m.configure(m.Cfg(threads=((),()),hb=True))
 w=m.initial(); m.touch(w,0); m.execute(w,1,('op_end',))
 assert 'HB: unordered access to operation lifetime' in w.errors
 from . import tiers
 from .mutants import CATALOG, CONTROLS, failure_manifest
 from .scenarios import SCENARIOS
 tasks=tiers.full(); names={t['name'] for t in tasks}
 assert len(names)==len(tasks) and failure_manifest()<=names
 for mid in CATALOG:
  assert {(t['scen'],t['model']) for t in tasks if t['mut']==mid}=={(s,mode) for s in SCENARIOS for mode in ('sc','sc_hb')}
 assert {t['mut'] for t in tiers.select('quick')}>=set(CATALOG)
 assert {t['control'] for t in tiers.select('quick')}>=set(CONTROLS)

 for mid, entry in CATALOG.items():
  assert {t['name'] for t in tasks if t['mut']==mid and t['name'] in failure_manifest()}=={mid+'.'+s for s in entry['killers']}

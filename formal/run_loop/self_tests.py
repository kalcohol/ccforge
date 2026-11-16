"""Direct obligations and source references independent of full exploration."""
import ast
from pathlib import Path
from engine.oracle import check, fixtures
from . import model as m

def run():
 protected={'obligations'}
 check(ast.parse(Path(m.__file__).read_text()),protected); fixtures(protected)
 from engine.source_lock import resolve
 for row in m.step_table(): resolve(m.SOURCE.read_text(),row[2])
 m.configure(m.Cfg(threads=((),())))
 w=m.initial()
 assert 'R1: lost push obligation' in m.terminal_violations(w)
 # The omission control must leave no submission effects or negative count.
 m.configure(m.Cfg(threads=((),()),control='drop_push'))
 w=m.initial(); m.execute(w,0,('push',0))
 assert w.obligations==(0,) and w.pushed==() and w.queue==() and w.count==0
 assert w.owner==-1 and w.threads==((),()) and w.signals==() and w.accesses==()
 assert 'R1: lost push obligation' in m.terminal_violations(w)
 assert not m.state_violations(w)
 for a in m.STEPS: assert a in m.describe((0,(a,)),w)
 # Queue publication must carry both data visibility and operation lifetime.
 m.configure(m.Cfg(threads=((),()),hb=True))
 w=m.initial(); m.task_touch(w,0,0); m.execute(w,1,('task_end',0))
 assert any('HB: unordered access to task 0'==e for e in w.errors)
 m.configure(m.Cfg(threads=((),())))
 w=m.initial(); w.alive=False; m.execute(w,0,('tail','finish'))
 assert not w.errors
 m.execute(w,0,('tail_notify','notify_finish'))
 assert 'R4: loop access after destruction' in w.errors
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
 # A concurrent token stop permits old false, but HB importing that stop
 # excludes the old value. This is independent of queue publication.
 m.configure(m.Cfg(threads=((),()),stale=True))
 w=m.initial(); m.execute(w,1,('stop',0))
 assert m.choices(w,0,('stop_query',0))==(0,1)
 m.execute(w,0,('stop_query',0),0); assert w.stop_seen[0] is False
 m.mem.acquire(w,0,m.synchronization(w,1))
 assert m.choices(w,0,('stop_query',0))==(1,)

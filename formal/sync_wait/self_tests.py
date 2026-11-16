"""Direct obligations and source references independent of full exploration."""
import ast
from pathlib import Path
from engine.oracle import check, fixtures
from . import model as m

def run():
 protected={'expected','receiver_obligation'}
 check(ast.parse(Path(m.__file__).read_text()),protected); fixtures(protected)
 from engine.source_lock import resolve
 for row in m.step_table(): resolve(m.SOURCE.read_text(),row[2])
 m.configure(m.Cfg(threads=((),())))
 w=m.initial()
 w.receiver_obligation=True
 assert 'SW3: lost result store obligation' in m.terminal_violations(w)
 for a in m.STEPS: assert a in m.describe((0,(a,)),w)
 for which,end in (('op','op_end'),('state','state_end'),('loop','loop_end')):
  m.configure(m.Cfg(threads=((),()),hb=True))
  w=m.initial(); w.op_alive=True; m.touch(w,0,which)
  m.execute(w,1,(end,))
  assert any('HB: unordered access to '+which+' lifetime'==e for e in w.errors)
 # Only stack return may follow finish's final unlock.
 m.configure(m.Cfg(threads=((),())))
 w=m.initial(); w.op_alive=False; w.state_alive=False; w.loop_alive=False
 m.execute(w,0,('finish_tail',)); assert not w.errors
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

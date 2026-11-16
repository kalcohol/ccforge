"""Spawn-safe exhaustive SC/HB cross product and stale baselines."""
from .scenarios import SCENARIOS
from .mutants import CATALOG, CONTROLS
def task(s,m='sc',mut='',control=''):
 return dict(name='.'.join((mut or control or 'base',s,m)),scen=s,model=m,mut=mut,control=control,stop=bool(mut or control),max=250000)
def full():
 return ([task(s,m) for s in SCENARIOS for m in ('sc','sc_hb','sc_stale')]+[task(s,m,k) for k in CATALOG for s in SCENARIOS for m in ('sc','sc_hb')]+[task(v['scen'],'sc_hb',control=k) for k,v in CONTROLS.items()])
def select(tier):
 rows=full()
 return rows if tier=='full' else [t for t in rows if t['control'] or t['mut'] and t['scen']==CATALOG[t['mut']]['killer'] or not t['mut'] and t['scen'] in ('a_one','b_pushers','d_stop','g_empty','h_early','i_early_execute','j_finish_push_before_run')]
def configuration(t):
 from .model import Cfg
 kw=dict(SCENARIOS[t['scen']]); kw.update(name=t['name'],hb=t['model']=='sc_hb',stale=t['model']=='sc_stale',mut=t['mut'],control=t['control'])
 if t['control']: kw.update(CONTROLS[t['control']].get('cfg',{}))
 return Cfg(**kw)
def identity(t):
 cfg=dict(configuration(t).__dict__); cfg.pop('name')
 return dict(cfg=cfg,stop=t['stop'],max_states=t['max'])

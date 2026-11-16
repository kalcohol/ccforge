"""Full cross product and bounded quick representatives, spawn-safe workers."""
from .scenarios import SCENARIOS
from .mutants import CATALOG, CONTROLS

def task(scen,model='sc',mut='',control=''):
    return dict(name='.'.join((mut or control or 'base',scen,model)),scen=scen,model=model,mut=mut,control=control,stop=bool(mut or control),max=250000)
def full():
    return ([task(s,m) for s in SCENARIOS for m in ('sc','sc_hb','sc_stale')]+
        [task(s,m,mut=k) for k in CATALOG for s in SCENARIOS for m in ('sc','sc_hb')]+
        [task(e['scen'],'sc_hb',control=k) for k,e in CONTROLS.items()])
def select(tier):
    tasks=full()
    if tier=='full': return tasks
    return [t for t in tasks if t['control'] or (t['mut'] and t['scen']==CATALOG[t['mut']]['killer']) or
        (not t['mut'] and (t['model']=='sc' or t['scen'] in ('a_later','d_sync','x_five','d_pre_sync','d_dual_sync','r_start_inline') or t['scen'].startswith('d_register_')))]
def configuration(t):
    from .model import Cfg
    kw=dict(SCENARIOS[t['scen']])
    kw.update(name=t['name'],hb=t['model']=='sc_hb',stale=t['model']=='sc_stale',mut=t['mut'],control=t['control'])
    if t['control']: kw['mut']=CONTROLS[t['control']].get('mut','')
    return Cfg(**kw)
def identity(t):
    cfg=dict(configuration(t).__dict__); cfg.pop('name')
    return dict(cfg=cfg,stop=t['stop'],max_states=t['max'])

"""Every mutant on every scenario in both modes; spawn-safe configuration."""
from .scenarios import SCENARIOS
from .mutants import CATALOG, CONTROLS

def task(s, m='sc', mut='', control=''):
    return dict(name='.'.join((mut or control or 'base', s, m)), scen=s, model=m, mut=mut, control=control, stop=bool(mut or control), max=250000)
def full():
    return ([task(s, m) for s in SCENARIOS for m in ('sc', 'sc_hb')] +
            [task(s, m, k) for k in CATALOG for s in SCENARIOS for m in ('sc', 'sc_hb')] +
            [task(v['scen'], m, control=k) for k, v in CONTROLS.items() for m in v.get('modes', ('sc_hb',))])
def select(tier):
    rows = full()
    return rows if tier == 'full' else [t for t in rows if t['control'] or t['mut'] and t['scen'] == CATALOG[t['mut']]['killer'] or not t['mut'] and t['scen'] in ('a_inline_value', 'a_own_value', 'b_two', 'c_store_detach', 'e_subscribe_walk', 'h_keepalive_only', 'h_exchange_race')]
def configuration(t):
    from .model import Cfg
    kw = dict(SCENARIOS[t['scen']]); kw.update(name=t['name'], hb=t['model'] == 'sc_hb', mut=t['mut'], control=t['control'])
    if t['control']:
        v = CONTROLS[t['control']]; kw['mut'] = v.get('mut', '')
        kw.update(v.get('cfg', {})); kw['required_evidence'] = v.get('evidence', {}).get(t['model'], ())
    return Cfg(**kw)
def identity(t):
    cfg = dict(configuration(t).__dict__); cfg.pop('name')
    return dict(cfg=cfg, stop=t['stop'], max_states=t['max'])

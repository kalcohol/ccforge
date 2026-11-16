"""Full cross product and bounded quick representatives; spawn-safe configs."""
from .scenarios import SCENARIOS
from .mutants import CATALOG, CONTROLS


def task(scen, model='sc', mut='', control=''):
    return dict(name='.'.join((mut or control or 'base', scen, model)), scen=scen, model=model,
                mut=mut, control=control, stop=bool(mut or control), max=1000000 if scen == 'x_destroy' else 400000)


def full():
    tasks = [task(s, m) for s in SCENARIOS for m in ('sc', 'sc_hb', 'sc_stale')]
    tasks += [task(s, m, mut=k) for k in CATALOG for s in SCENARIOS for m in ('sc', 'sc_hb')]
    tasks += [task(e['scen'], 'sc_hb', control=k) for k,e in CONTROLS.items()]
    return tasks


def select(tier):
    tasks = full()
    if tier == 'full':
        return tasks
    return [t for t in tasks if (t['mut'] and t['scen'] in CATALOG[t['mut']].get('quick', (CATALOG[t['mut']]['killer'],))) or t['control'] or
            t['model'] == 'sc' or (t['model'] == 'sc_hb' and t['scen'] in ('a_done', 'r_inline', 'e_sync', 'g_join')) or
            (t['model'] == 'sc_stale' and t['scen'] == 'd_pre')]


def configuration(t):
    from .model import Cfg
    kw = dict(SCENARIOS[t['scen']])
    kw.update(name=t['name'], hb=t['model']=='sc_hb', stale=t['model']=='sc_stale', mut=t['mut'], control=t['control'])
    if t['control']:
        kw['mut'] = CONTROLS[t['control']].get('mut', '')
    return Cfg(**kw)


def identity(t):
    cfg = dict(configuration(t).__dict__)
    cfg.pop('name')
    return dict(cfg=cfg, stop=t['stop'], max_states=t['max'])

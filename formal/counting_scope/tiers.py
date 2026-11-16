"""Bounded configuration matrix and explicit quick representatives."""

from .scenarios import SCENARIOS
from .mutants import CATALOG, CONTROLS


def task(scen, model='sc', scope='counting_scope', scheduler='inline', mut='', control=''):
    return dict(name='.'.join((mut or control or 'base', scen, scope, scheduler, model)),
                scen=scen, model=model, scope=scope, scheduler=scheduler, mut=mut, control=control,
                stop=bool(mut or control), max=300000)


def full():
    result = []
    for s in SCENARIOS:
        kinds = ('simple_counting_scope', 'counting_scope') if s[0] in 'abcdefg' else ('counting_scope',)
        for scope in kinds:
            for scheduler in ('inline', 'queued'):
                for model in ('sc', 'sc_hb', 'sc_stale'):
                    result.append(task(s, model, scope, scheduler))
    for mid, entry in CATALOG.items():
        for witness in entry.get('killers', (entry['killer'],)):
            for model in ('sc', 'sc_hb'):
                item = task(witness, model, mut=mid)
                if mid in ('F_reverse_registration', 'F_destroy_fn_first'):
                    item['stop'] = False  # Preserve independent F2/F4 evidence across the full witness.
                result.append(item)
    for name, entry in CONTROLS.items():
        result.append(task(entry['scen'], 'sc_hb', scheduler='queued', control=name))
    return result


QUICK_SCENARIOS = frozenset(('d_observers', 'a_race', 'a_empty', 'b_three', 'c_registered', 'd_close', 'd_open_join',
    'u_pre0', 'd_unused_closed_destroy', 'd_unused_closed_join', 'd_joining_observers',
    'u_retained_deadline', 's_multi', 's_external_complete', 'review_inline_registration_lifetime', 'f_observer_teardown',
    'e_nested', 'f_destroy', 'f_reenter', 'g_early', 'g_token', 's_scope', 's_fused', 's_inside_request',
    's_late', 'u_pending_remove', 'u_race', 'u_pre1', 'u_hb_pre', 'u_destroy', 'u_self0', 'u_self1', 'u_sibling'))


def select(tier):
    tasks = full()
    if tier == 'full':
        return tasks
    return [t for t in tasks if t['mut'] or t['control'] or
            (t['scen'] in QUICK_SCENARIOS and t['scope'] == 'counting_scope' and
             ((t['model'], t['scheduler']) in (('sc', 'inline'), ('sc_hb', 'queued')) or
              (t['model'] == 'sc_stale' and t['scheduler'] == 'inline' and t['scen'] in ('d_observers', 'u_hb_pre'))))]


def configuration(t):
    from .model import Cfg
    kw = dict(SCENARIOS[t['scen']])
    kw.update(name=t['name'], scope_kind=t['scope'], scheduler=t['scheduler'], hb=t['model']=='sc_hb',
              stale=t['model']=='sc_stale', mut=t['mut'], control=t['control'])
    if t['control'] == 'lost_join':
        kw['threads'] = (kw['threads'][0],)
    if t['scope'] == 'simple_counting_scope':
        kw['continuations'] = {j: tuple(op for op in script if op[0] != 'request')
                               for j, script in kw.get('continuations', {}).items()}
    return Cfg(**kw)


def identity(t):
    cfg = dict(configuration(t).__dict__)
    cfg.pop('name')
    return dict(cfg=cfg, stop=t['stop'], max_states=t['max'])

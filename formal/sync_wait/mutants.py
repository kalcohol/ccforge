"""Single source ordering changes and independent obligation controls."""
from pathlib import Path
ENTRIES={
 'finish_before_store':('Move finish before result store','own_value'),
 'read_before_run':('Move driver read before run','own_value'),
 'loop_after_state':('Declare loop after state, including construction/destruction order','value_destructor'),
}
CATALOG={k:dict(description=v[0],killer=v[1],outcome='killed',killers=()) for k,v in ENTRIES.items()}
CONTROLS={k:dict(scen='own_value',fail=True) for k in ('drop_store','drop_finish','double_store','after_finish_state','after_finish_op','after_finish_loop','skip_op_end','skip_state_end','skip_loop_end','no_finish_handoff')}
def failure_manifest():
 rows=[s for s in Path(__file__).with_name('sensitivity.txt').read_text().splitlines() if s and not s.startswith('#')]
 if rows!=sorted(set(rows)): raise ValueError('unsorted sensitivity')
 return frozenset(rows)
def declared(t):
 fail=t['name'] in failure_manifest()
 if t['control'] and fail!=CONTROLS[t['control']]['fail']: raise ValueError('control declaration mismatch')
 return fail

# Hand-reviewed complete SC/HB sweep; empty tuples are whole-sweep survivors.
KILLERS = {'finish_before_store': ('own_error.sc', 'own_error.sc_hb', 'own_stopped.sc', 'own_stopped.sc_hb', 'own_value.sc', 'own_value.sc_hb'), 'loop_after_state': ('value_destructor.sc', 'value_destructor.sc_hb'), 'read_before_run': ('inline_error.sc', 'inline_error.sc_hb', 'inline_stopped.sc', 'inline_stopped.sc_hb', 'inline_value.sc', 'inline_value.sc_hb', 'own_error.sc', 'own_error.sc_hb', 'own_stopped.sc', 'own_stopped.sc_hb', 'own_value.sc', 'own_value.sc_hb', 'scheduled_value.sc', 'scheduled_value.sc_hb', 'value_destructor.sc', 'value_destructor.sc_hb')}
for mid, names in KILLERS.items():
 CATALOG[mid]['killers']=names

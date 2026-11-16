"""Faithful single changes, whole SC/HB sweep and independent controls."""
from pathlib import Path
ENTRIES={
 'notify_push_outside':('Move push notify after unlock','b_pushers'),
 'notify_finish_outside':('Move finish notify after unlock','a_one'),
 'wait_no_predicate':('Remove the predicate from wait','c_wait'),
 'pop_ignore_finish':('Remove finishing from the predicate','g_empty'),
 'finish_unlocked':('Remove only finish lock','a_one'),
 'decrement_before_execute':('Move the count decrement before execute','i_early_execute'),
 'push_no_count':('Omit push count increment','h_early'),
}
CATALOG={k:dict(description=v[0],killer=v[1],outcome='killed',killers=()) for k,v in ENTRIES.items()}
CONTROLS={k:dict(scen='a_one',fail=True) for k in ('drop_complete','drop_push','drop_return','skip_loop_end','skip_task_end','wrong_kind')}
# Deliberately violates the submission-lifetime precondition.
CONTROLS['literal_late_push']=dict(scen='g_empty',fail=True,cfg=dict(tasks=1,threads=(
 (('finish',),('await','returned'),('push',0)), (('pop',),('destroy',)))))
def failure_manifest():
 rows=[s for s in Path(__file__).with_name('sensitivity.txt').read_text().splitlines() if s and not s.startswith('#')]
 if rows!=sorted(set(rows)): raise ValueError('unsorted sensitivity')
 return frozenset(rows)
def declared(t):
 fail=t['name'] in failure_manifest()
 if t['control'] and fail!=CONTROLS[t['control']]['fail']: raise ValueError('control declaration mismatch')
 return fail

# Hand-reviewed complete SC/HB sweep; empty tuples are whole-sweep survivors.
KILLERS = {'decrement_before_execute': ('i_early_execute.sc', 'i_early_execute.sc_hb'), 'finish_unlocked': ('a_one.sc', 'a_one.sc_hb', 'b_pushers.sc', 'b_pushers.sc_hb', 'c_wait.sc', 'c_wait.sc_hb', 'd_stop.sc', 'd_stop.sc_hb', 'f_finish_race.sc', 'f_finish_race.sc_hb', 'g_empty.sc', 'g_empty.sc_hb'), 'notify_finish_outside': ('a_one.sc', 'a_one.sc_hb', 'b_pushers.sc', 'b_pushers.sc_hb', 'c_wait.sc', 'c_wait.sc_hb', 'd_stop.sc', 'd_stop.sc_hb', 'f_finish_race.sc', 'f_finish_race.sc_hb', 'g_empty.sc', 'g_empty.sc_hb'), 'notify_push_outside': ('b_pushers.sc', 'b_pushers.sc_hb', 'e_finish_completion.sc', 'e_finish_completion.sc_hb', 'f_finish_race.sc', 'f_finish_race.sc_hb'), 'pop_ignore_finish': ('a_one.sc', 'a_one.sc_hb', 'b_pushers.sc', 'b_pushers.sc_hb', 'c_wait.sc', 'c_wait.sc_hb', 'd_stop.sc', 'd_stop.sc_hb', 'e_finish_completion.sc', 'e_finish_completion.sc_hb', 'f_finish_race.sc', 'f_finish_race.sc_hb', 'g_empty.sc', 'g_empty.sc_hb'), 'push_no_count': ('a_one.sc', 'a_one.sc_hb', 'b_pushers.sc', 'b_pushers.sc_hb', 'c_wait.sc', 'c_wait.sc_hb', 'd_stop.sc', 'd_stop.sc_hb', 'e_finish_completion.sc', 'e_finish_completion.sc_hb', 'f_finish_race.sc', 'f_finish_race.sc_hb', 'h_early.sc', 'h_early.sc_hb', 'i_early_execute.sc', 'i_early_execute.sc_hb'), 'wait_no_predicate': ('a_one.sc', 'a_one.sc_hb', 'b_pushers.sc', 'b_pushers.sc_hb', 'c_wait.sc', 'c_wait.sc_hb', 'd_stop.sc', 'd_stop.sc_hb', 'e_finish_completion.sc', 'e_finish_completion.sc_hb', 'f_finish_race.sc', 'f_finish_race.sc_hb', 'g_empty.sc', 'g_empty.sc_hb', 'i_early_execute.sc', 'i_early_execute.sc_hb')}
# The closed-submission finish/push race also kills these single changes.
for mid in ('finish_unlocked', 'notify_finish_outside', 'notify_push_outside',
            'pop_ignore_finish', 'push_no_count', 'wait_no_predicate'):
 KILLERS[mid]+=('j_finish_push_before_run.sc', 'j_finish_push_before_run.sc_hb')
for mid, names in KILLERS.items():
 CATALOG[mid]['killers']=names

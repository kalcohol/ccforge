"""Single source changes; survivors require explicit covering invariants/controls."""
from pathlib import Path
ENTRIES={
 'credit_without_check':('Remove pending nonzero test from callback credit loop','d_outer'),
 'no_deregister':('Remove reset before deliver','a_inline'),
 'deregister_after_deliver':('Move reset after delivery','a_inline'),
 'overwrite_error':('Ignore exchange result and always store error','b_two_errors'),
 'stop_over_error':('Remove disposition CAS guard; retain result-index guard','b_error_stop'),
 'no_error_stop':('Remove request_stop from child_fail','b_error'),
 'no_stopped_stop':('Remove request_stop from child_stop','c_stopped'),
 'relaxed_pending':('Make pending decrement relaxed','b_error'),
 'relaxed_credit':('Make callback CAS relaxed','d_outer'),
 'register_after_start':('Register outer callback after child start','a_inline'),
 'callback_before_children':('Move callback member before children including initialization/destruction','a_inline'),
 'request_after_done':('Move callback request_stop after child_done','d_outer'),
}
CATALOG={k:dict(description=v[0],killer=v[1],outcome='survives' if k in ('stop_over_error','relaxed_credit','callback_before_children') else 'killed',killers=()) for k,v in ENTRIES.items()}
CONTROLS={
 'drop_throw_request':dict(scen='e_throw',fail=True),
 'drop_stop':dict(scen='d_pre',fail=True),
 'drop_notice':dict(scen='d_pre',fail=True),
 'skip_child_2':dict(scen='d_multi_cancel',fail=True),
 'skip_child_end_2':dict(scen='d_multi_cancel',fail=True),
 'literal_outer_forward':dict(scen='d_outer',fail=True),
 'drop_outer_forward':dict(scen='d_outer',fail=True),
 **{k:dict(scen='a_values',fail=True) for k in ('skip_source_end','skip_receiver_end','skip_op_end','skip_child_end_0','skip_child_end_1')},
 'drop_done':dict(scen='a_inline',fail=True),
 'drop_receiver':dict(scen='a_inline',fail=True),
 'skip_outer_remove':dict(scen='a_inline',fail=True),
 'skip_child_0':dict(scen='a_values',fail=True),
 'skip_child_1':dict(scen='a_values',fail=True),
 'drop_completion':dict(scen='b_error',fail=True),
 'member_without_deregister':dict(scen='d_outer',mut='callback_before_children',fail=True),
 'credit_without_pending_release':dict(scen='d_outer',mut='relaxed_credit',fail=True),
 'stop_without_result_guard':dict(scen='b_error_stop',mut='stop_over_error',fail=True),
}
def failure_manifest():
 rows=[s for s in Path(__file__).with_name('sensitivity.txt').read_text().splitlines() if s and not s.startswith('#')]
 if rows!=sorted(set(rows)): raise ValueError('unsorted sensitivity')
 return frozenset(rows)
def declared(t):
 fail=t['name'] in failure_manifest()
 if t['control'] and fail!=CONTROLS[t['control']]['fail']: raise ValueError('control declaration mismatch')
 return fail

# Hand-reviewed complete SC/HB sweep; empty tuples are whole-sweep survivors.
KILLERS = {'callback_before_children': (), 'credit_without_check': ('d_ignore.sc', 'd_ignore.sc_hb', 'd_outer.sc', 'd_outer.sc_hb'), 'deregister_after_deliver': ('a_inline.sc', 'a_inline.sc_hb', 'a_three.sc', 'a_three.sc_hb', 'a_two_values.sc', 'a_two_values.sc_hb', 'a_values.sc', 'a_values.sc_hb', 'b_error.sc', 'b_error.sc_hb', 'b_error_stop.sc', 'b_error_stop.sc_hb', 'b_two_errors.sc', 'b_two_errors.sc_hb', 'c_stopped.sc', 'c_stopped.sc_hb', 'd_ignore.sc', 'd_ignore.sc_hb', 'd_later.sc', 'd_later.sc_hb', 'd_multi_cancel.sc', 'd_multi_cancel.sc_hb', 'd_nested.sc', 'd_nested.sc_hb', 'd_outer.sc', 'd_outer.sc_hb', 'd_pre.sc', 'd_pre.sc_hb', 'e_impossible.sc', 'e_impossible.sc_hb', 'e_throw.sc', 'e_throw.sc_hb'), 'no_deregister': ('a_inline.sc', 'a_inline.sc_hb', 'a_three.sc', 'a_three.sc_hb', 'a_two_values.sc', 'a_two_values.sc_hb', 'a_values.sc', 'a_values.sc_hb', 'b_error.sc', 'b_error.sc_hb', 'b_error_stop.sc', 'b_error_stop.sc_hb', 'b_two_errors.sc', 'b_two_errors.sc_hb', 'c_stopped.sc', 'c_stopped.sc_hb', 'd_ignore.sc', 'd_ignore.sc_hb', 'd_later.sc', 'd_later.sc_hb', 'd_multi_cancel.sc', 'd_multi_cancel.sc_hb', 'd_nested.sc', 'd_nested.sc_hb', 'd_outer.sc', 'd_outer.sc_hb', 'd_pre.sc', 'd_pre.sc_hb'), 'no_error_stop': ('b_error.sc', 'b_error.sc_hb', 'b_two_errors.sc', 'b_two_errors.sc_hb', 'd_multi_cancel.sc', 'd_multi_cancel.sc_hb', 'd_nested.sc', 'd_nested.sc_hb'), 'no_stopped_stop': ('c_stopped.sc', 'c_stopped.sc_hb'), 'overwrite_error': ('b_two_errors.sc', 'b_two_errors.sc_hb'), 'register_after_start': ('a_inline.sc', 'a_inline.sc_hb', 'a_three.sc', 'a_three.sc_hb', 'a_two_values.sc', 'a_two_values.sc_hb', 'a_values.sc', 'a_values.sc_hb', 'b_error.sc', 'b_error.sc_hb', 'b_error_stop.sc', 'b_error_stop.sc_hb', 'b_two_errors.sc', 'b_two_errors.sc_hb', 'c_stopped.sc', 'c_stopped.sc_hb', 'd_ignore.sc', 'd_ignore.sc_hb', 'd_multi_cancel.sc', 'd_multi_cancel.sc_hb', 'd_nested.sc', 'd_nested.sc_hb', 'd_outer.sc', 'd_outer.sc_hb', 'e_impossible.sc', 'e_impossible.sc_hb', 'e_throw.sc', 'e_throw.sc_hb'), 'relaxed_credit': (), 'relaxed_pending': ('a_two_values.sc_hb', 'b_error.sc_hb', 'b_error_stop.sc_hb', 'b_two_errors.sc_hb', 'c_stopped.sc_hb', 'd_ignore.sc_hb', 'd_later.sc_hb', 'd_multi_cancel.sc_hb', 'd_outer.sc_hb'), 'request_after_done': ('d_ignore.sc', 'd_ignore.sc_hb', 'd_outer.sc', 'd_outer.sc_hb'), 'stop_over_error': ()}
for mid, names in KILLERS.items():
 CATALOG[mid]['killers']=names

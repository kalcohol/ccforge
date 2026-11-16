"""Faithful one-source-change variants; survivors are explicit, never forced."""
from pathlib import Path

ENTRIES = {
    'deliver_locked': ('Deliver consumer before state mutex unlock', 'd_registered'),
    'keepalive_first': ('Release keepalive before delivery', 'r_inline'),
    'drop_inner_pin': ('Remove exactly the set_value inner receiver entry shared_ptr copy', 'r_inline'),
    'drop_request_pin': ('Remove exactly the request_stop shared_ptr copy', 'b_sync'),
    'owner_after': ('Release owner after receiver delivery', 'r_inline'),
    'callback_after': ('Reset callback after receiver delivery', 'a_done'),
    'abandon_stop_first': ('Request stop before detaching the consumer', 'c_robust'),
    'plain_active': ('Replace only __deliver active exchange by acquire load then release store', 'c_robust'),
    'owner_unlocked': ('Remove owner mutex in release_owner', 'c_robust'),
    'op_unlocked': ('Remove operation consumer mutex in destructor', 'c_robust'),
    'association_first': ('Release association before resource deallocation', 'g_join'),
    'consume_taken': ('Remove consumer_taken check in consume', 'a_defensive'),
    'abandon_taken': ('Remove consumer_taken check in abandon_unconsumed', 'r_inline'),
    'cancel_done': ('Remove done check in try_cancel', 'd_pre'),
    'strong_callback': ('Callback owns state strongly instead of weakly', 'a_done'),
    'member_order': ('Declare operation before stop source, moving initializer/destruction order', 'a_done'),
    'consume_first': ('Consume before stop callback installation', 'd_install'),
}
# Full qualification sweeps every scenario in SC and SC-HB. The named witness
# is retained as a cheap quick representative; all killers are listed below.
SURVIVORS = {'keepalive_first', 'drop_inner_pin', 'abandon_stop_first', 'abandon_taken', 'strong_callback', 'owner_after', 'drop_request_pin'}
CATALOG = {k: dict(description=v[0], killer=v[1], outcome='survives' if k in SURVIVORS else 'killed',
                   reason='One source change under the declared bounded scripts; see README for surviving variants.')
           for k, v in ENTRIES.items()}
# Every killing scenario found in the full SC/SC-HB sweep; both modes fail.
KILLERS = {
    'consume_taken': ('a_defensive',),
    'deliver_locked': ('d_install', 'd_registered', 'd_race', 'e_later', 'x_five', 'x_destroy', 'g_both_stop'),
    'callback_after': ('a_done', 'a_inline', 'a_error', 'a_race', 'c_robust', 'd_pre', 'd_install', 'd_registered', 'd_race', 'e_sync', 'e_later', 'f_closed', 'g_join', 'g_scope', 'g_scope_join', 'h_result', 'r_inline', 'r_unrelated', 'x_destroy', 'x_five', 'g_pre_launch', 'g_pre_start', 'g_both_stop'),
    'plain_active': ('c_robust', 'x_destroy', 'g_pre_start'),
    'owner_unlocked': ('c_robust', 'x_destroy', 'g_pre_start'),
    'op_unlocked': ('c_robust', 'x_destroy', 'g_pre_start'),
    'association_first': ('a_registered', 'a_done', 'a_inline', 'a_error', 'a_race', 'b_drop', 'b_move', 'b_done', 'b_sync', 'c_unstarted', 'c_robust', 'd_pre', 'd_install', 'd_registered', 'd_race', 'e_sync', 'e_later', 'g_join', 'g_scope', 'g_scope_join', 'h_allocation', 'h_install', 'h_consume', 'h_result', 'r_inline', 'r_unrelated', 'x_destroy', 'x_five', 'g_pre_launch', 'g_pre_start', 'g_both_stop', 'c_handoff', 'a_defensive', 'c_abandon'),
    'cancel_done': ('d_pre', 'd_install', 'd_registered', 'd_race', 'e_sync', 'e_later', 'x_five', 'x_destroy', 'g_both_stop'),
    'member_order': ('a_registered', 'a_done', 'a_inline', 'a_error', 'a_race', 'b_drop', 'b_move', 'b_done', 'b_sync', 'c_unstarted', 'c_robust', 'd_pre', 'd_install', 'd_registered', 'd_race', 'e_sync', 'e_later', 'f_closed', 'g_join', 'g_scope', 'g_scope_join', 'h_allocation', 'h_install', 'h_consume', 'h_result', 'r_inline', 'r_unrelated', 'x_destroy', 'x_five', 'g_pre_launch', 'g_pre_start', 'g_both_stop', 'c_handoff', 'a_defensive', 'c_abandon'),
    'consume_first': ('a_race', 'c_robust', 'd_pre', 'd_install', 'd_race', 'g_join', 'h_install', 'r_inline', 'r_unrelated', 'x_destroy', 'g_pre_start', 'a_done', 'a_inline', 'a_error', 'f_closed', 'h_consume', 'h_result', 'g_pre_launch', 'a_defensive'),
}
# Construction-time registration scenarios, declared alongside sensitivity.
for mid, scenarios in {
    'association_first': ('g_connect_pre_own', 'g_connect_pre_inline', 'g_connect_pre_sync', 'g_connect_pre_later', 'g_connect_pre_ignore', 'g_connect_pending'),
    'member_order': ('g_connect_pre_own', 'g_connect_pre_inline', 'g_connect_pre_sync', 'g_connect_pre_later', 'g_connect_pre_ignore', 'g_connect_pending', 'f_connect_closed', 'f_connect_stopped'),
    'callback_after': ('g_connect_pre_own', 'g_connect_pre_inline', 'g_connect_pre_sync', 'g_connect_pre_later', 'g_connect_pre_ignore', 'g_connect_pending', 'f_connect_closed', 'f_connect_stopped'),
    'consume_first': ('g_connect_pre_own', 'g_connect_pre_inline', 'g_connect_pre_sync', 'g_connect_pre_later', 'g_connect_pre_ignore', 'g_connect_pending', 'f_connect_closed', 'f_connect_stopped'),
}.items():
    KILLERS[mid] += scenarios
for mid in ('callback_after', 'member_order', 'consume_first'):
    KILLERS[mid] += ('f_connect_closed_late_stop',)
for mid, entry in CATALOG.items():
    entry["killers"] = KILLERS.get(mid, ())
    entry["reason"] = "All 44 scenarios in SC and SC-HB; survivors cover the entire list. See the checked invariants and combined controls."
CATALOG['consume_taken']['reason'] = 'The first invocation may already have taken set: a_defensive independently requires stopped (C2). I2 bounds call count and does not cover this guard.'
CATALOG["deliver_locked"]["quick"] = ("r_inline", "d_registered")

CONTROLS = {
    'literal_C1': dict(scen='c_robust', fail=True, reason='Stronger destruction-entry deadline independent of active arbitration'),
    'drop_receiver': dict(scen='a_done', fail=True, reason='Skip discharge of independently recorded started receiver obligation'),
    'skip_producer': dict(scen='a_registered', fail=True, reason='Skip dispatch after producer completion claim'),
    'skip_registration_teardown': dict(scen='a_done', fail=True, reason='Leave producer registration when source is destroyed'),
    'skip_stop_request': dict(scen='d_registered', fail=True, reason='Cancel succeeds without issuing the independent stop obligation'),
    'wait_cycle': dict(scen='r_inline', fail=True, reason='Receiver script self-acquires the same nonrecursive mutex twice'),
    'corrupt_result': dict(scen='a_done', fail=True, reason='Corrupt delivered result independently of stored result'),
    'no_mutex_release': dict(scen='a_race', fail=True, reason='Omit state mutex publication; result/lifetime HB fails'),
    'retain_keepalive': dict(scen='a_done', fail=True, reason='Skip keepalive release after independent producer completion obligation'),
    'receiver_reenter': dict(scen='r_inline', fail=False, reason='Receiver reenters state protocol outside its mutex'),
}

CONTROLS.update({
    'skip_state_teardown': dict(scen='f_connect_closed', fail=True, reason='Input 0 registration survives destruction without start; C6'),
    'skip_scope_teardown': dict(scen='f_connect_closed', fail=True, reason='Input 1 registration survives containing storage without start; C6'),
    'skip_state_teardown_late_stop': dict(scen='f_connect_closed_late_stop', fail=True, reason='Input 0 teardown omitted before storage end and later scope stop; C6'),
    'skip_scope_teardown_late_stop': dict(scen='f_connect_closed_late_stop', fail=True, reason='Input 1 teardown omitted before storage end and later scope stop; C6'),
    'wrong_install': dict(scen='h_install', fail=True, reason='Installation failure delivers stopped despite independent error expectation; C2'),
    'wrong_consume_failure': dict(scen='h_consume', fail=True, reason='Consume failure delivers stopped despite independent error expectation; C2'),
    'drop_prestart_notice': dict(scen='g_connect_pre_ignore', fail=True, reason='Fn notification before start is not preserved as child state; C6'),
    'complete_before_start': dict(scen='g_connect_pre_sync', fail=True, reason='Notification illegally dispatches completion before start; C6'),
    'skip_start_stop': dict(scen='g_connect_pre_sync', fail=True, reason='Preserved notification fails to trigger sync completion at start; C7'),
    'skip_unstarted_teardown': dict(scen='f_connect_closed', fail=True, reason='Constructed registrations survive destruction without start; C6'),
    'wrong_closed': dict(scen='f_closed', fail=True, reason='Store value despite independent not-started stopped outcome'),
    'wrong_cancel': dict(scen='d_pre', fail=True, reason='Successful cancellation stores value instead of stopped'),
    'wrong_producer': dict(scen='a_done', fail=True, reason='Producer result replaced by stopped'),
    'wrong_consumed': dict(scen='a_defensive', fail=True, reason='Defensive consumed path delivers producer result instead of stopped'),
    'operation_no_handoff': dict(scen='c_handoff', fail=True, reason='Remove operation construction/destruction publication'),
    'scope_no_handoff': dict(scen='g_scope_join', fail=True, reason='Remove scope destruction handoffs'),
    'drop_fused_pre_launch': dict(scen='g_pre_launch', fail=True, reason='Drop committed delivery after pre-launch stop'),
    'drop_fused_pre_start': dict(scen='g_pre_start', fail=True, reason='Drop committed delivery after stop at start boundary'),
    'drop_fused_both': dict(scen='g_both_stop', fail=True, reason='Drop committed delivery with two stop requesters'),
    'request_cover_removed': dict(scen='b_sync', mut='drop_request_pin', fail=True, reason='Remove caller owner as well as request pin; I1'),
    'repeat_consume': dict(scen='a_done', mut='consume_taken', fail=True, reason='Exceed I2 invocation bound; separate from defensive pre-taken C2 coverage'),
    'post_consume_abandon': dict(scen='r_inline', mut='abandon_taken', fail=True, reason='Remove no-abandon-after-consume script protection; I3'),
    'callback_cover_removed': dict(scen='a_done', mut='strong_callback', fail=True, reason='Remove callback reset on completion; I4/C6'),
    'callback_abandon_cover_removed': dict(scen='c_abandon', mut='strong_callback', fail=True, reason='Remove callback reset on abandonment; I4/C4'),
    'owner_cover_removed': dict(scen='r_inline', mut='owner_after', fail=True, reason='Remove abandonment release_owner; I5'),
    'abandon_cover_removed': dict(scen='c_robust', mut='abandon_stop_first', fail=True, reason='Remove abandonment active exchange; I6'),
    'keep_cover_removed': dict(scen='a_registered', mut='keepalive_first', fail=True, reason='Also remove entry pin and consumer delivery state owner; I8/C3'),
    'inner_cover_removed': dict(scen='a_registered', mut='drop_inner_pin', fail=True, reason='Also release keepalive early and remove consumer delivery state owner; I8/C3'),
})


def failure_manifest():
    names = [x for x in Path(__file__).with_name('sensitivity.txt').read_text().splitlines() if x and not x.startswith('#')]
    if names != sorted(set(names)):
        raise ValueError('sensitivity manifest must contain unique sorted names')
    return frozenset(names)


def declared(task):
    fail = task['name'] in failure_manifest()
    if task['control'] and fail != CONTROLS[task['control']]['fail']:
        raise ValueError('control contradicts sensitivity manifest: ' + task['name'])
    return fail

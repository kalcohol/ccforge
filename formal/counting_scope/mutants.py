"""Single-source-change mutants and explicit witnesses."""

from pathlib import Path

# Each tuple gives description, intended killer, and expected catalog outcome.
ENTRIES = {
    'J_complete_locked': ('Complete joiners before releasing the mutex', 'f_reenter', 'killed'),
    'J_wrong_decrement': ('Complete on old count two instead of one', 'b_three', 'killed'),
    'J_decrement_unlocked': ('Omit decrement mutex acquire/release', 'c_joiners', 'killed'),
    'J_join_unlocked': ('Omit start_join mutex acquire/release', 'c_joiners', 'killed'),
    'J_no_joining': ('Do not enter a joining state', 'a_before', 'killed'),
    'J_read_next_late': ('Read next link after receiver completion', 'c_registered', 'killed'),
    'J_touch_start': ('Touch operation after registration returns false', 'f_destroy', 'killed'),
    'J_accept_joined': ('Accept associations in closed or joined states', 'f_closed_reject', 'killed'),
    'J_close_unlocked': ('Omit close mutex acquire/release', 'd_close', 'killed'),
    'J_destructor_unlocked': ('Read destructor state without mutex', 'f_observer_teardown', 'killed'),
    'S_release_first': ('Holder releases before destroying child registrations', 's_holder_order', 'killed'),
    'F_plain_arbitration': ('Replace exchange by plain load followed by store', 'u_race', 'killed'),
    'F_no_prepare': ('Omit the logical stop check before publishing registrations', 'u_pre1', 'killed'),
    'F_reverse_registration': ('Swap registration members, including initializer placement and destruction order', 'u_pre0', 'killed'),
    'F_destroy_fn_first': ('Destroy Fn/flag before deregistrations', 'u_destroy', 'killed'),
}
CATALOG = {k: dict(description=v[0], killer=v[1], outcome=v[2],
    reason='The named bounded witness must expose the changed transition.') for k, v in ENTRIES.items()}
CATALOG['J_destructor_unlocked']['killers'] = ('f_observer_teardown', 'f_destroy')
CATALOG['J_destructor_unlocked']['reason'] = (
    'Observer teardown races remaining scope accesses; f_destroy passes because its continuation inherits unlock.')
CATALOG['J_decrement_unlocked']['reason'] = (
    'Both participants split list read/write pairs. SC reaches the join-state default; HB also detects unordered list accesses.')
CATALOG['J_join_unlocked']['reason'] = (
    'Both participants split list read/write pairs. Count and state reads interleave with the final decrement, reaching the join-state default.')
CATALOG['F_reverse_registration']['killers'] = ('u_pre0', 'u_pre1')
# In u_pre1 the swapped downstream constructor is inline before prepare: that
# direction remains conforming; u_pre0 exposes another thread winning first.
CATALOG['F_reverse_registration']['reason'] = 'F2 fails in u_pre0; u_pre1 is a passing directional control.'
CONTROLS = {
    'drop_dispatch': dict(scen='u_retained_deadline', fail=True,
        reason='Exchange wins and records a commitment but drops Fn dispatch; independent state check fails.'),
    'entry_before_return': dict(scen='u_retained_deadline', fail=True,
        reason='Strong specification deadline forbids a losing input request returning before the winner enters Fn.'),
    'skip_retained': dict(scen='u_same_source', fail=True,
        reason='Request skips retained registrations; independent request-return obligation fails.'),
    'literal_f3': dict(scen='u_pending_remove', fail=True, reason='Literal F3 forbids legal cancellation of a pending callback; specification counterexample.'),
    'lost_join': dict(scen='a_before', fail=True, reason='Script omits final release; J2 detects loss.'),
    'no_queue_release': dict(scen='a_before', fail=True, reason='Scheduler enqueue omits release; J6 detects unordered holder data.'),
    'ordered_queue': dict(scen='a_before', fail=False, reason='Unmodified queue handoff orders holder data.'),
}


def failure_manifest():
    lines = Path(__file__).with_name('sensitivity.txt').read_text().splitlines()
    names = [s for s in lines if s and not s.startswith('#')]
    if names != sorted(set(names)):
        raise ValueError('sensitivity manifest must contain unique sorted names')
    return frozenset(names)


def declared(task):
    expected = task['name'] in failure_manifest()
    if task.get('control') in CONTROLS and expected != CONTROLS[task['control']]['fail']:
        raise ValueError('control contradicts sensitivity manifest: ' + task['name'])
    return expected

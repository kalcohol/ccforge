"""Single source changes and independent obligation/covering controls."""
from pathlib import Path
ENTRIES = {
    'link_after_unlock': ('Move subscriber linking after start unlock', 'b_two'),
    'phase_before_lock': ('Move done check before start lock', 'c_store_detach'),
    'source_locked': ('Move source start before start unlock', 'a_inline_value'),
    'no_running': ('Omit idle to running assignment', 'b_two'),
    'no_keepalive': ('Omit keepalive assignment', 'h_keepalive_only'),
    'detach_unlocked': ('Remove notify detach lock only', 'c_store_detach'),
    'deliver_locked': ('Move notify unlock after delivery walk', 'e_subscribe_walk'),
    'retain_keepalive': ('Omit move of keepalive into notify pin', 'a_inline_value'),
    'done_unlocked': ('Move set_value phase done assignment after store unlock', 'c_store_detach'),
    'notify_in_store': ('Move set_value notify call before completion unlock', 'a_inline_value'),
    'no_completion_pin': ('Remove set_value shared_from_this pin; use raw pointer', 'a_inline_value'),
    'no_catch': ('Remove value caching try/catch', 'a_inline_throw'),
    'member_start': ('Replace start local shared_ptr copies with member references', 'd_late'),
    'no_active_store': ('Remove destructor active release store', 'h_keepalive_only'),
    'late_locked': ('Move done-path delivery before start unlock', 'e_subscribe_late'),
}
CATALOG = {k: dict(description=v[0], killer=v[1], outcome='survives' if k in ('no_completion_pin', 'detach_unlocked') else 'killed', killers=()) for k, v in ENTRIES.items()}
CONTROLS = {k: dict(scen='a_own_value', fail=True) for k in (
    'drop_start', 'drop_completion', 'drop_store', 'drop_receiver', 'skip_state_end', 'skip_free_0',
    'skip_release_sh.0', 'skip_release_sub.0', 'skip_release_st.1', 'skip_release_pin.1',
    'skip_release_op.state.0', 'skip_release_op.node.0', 'relaxed_release', 'skip_source_end', 'skip_allocator_end')}
for i in range(3):
    for prefix in ('skip_op_end_', 'skip_node_end_', 'skip_receiver_end_'):
        CONTROLS[prefix + str(i)] = dict(scen='b_three', fail=True)
    CONTROLS['skip_free_' + str(i + 1)] = dict(scen='b_three', fail=True)
CONTROLS['pin_cover_removed'] = dict(scen='h_keepalive_only', mut='no_completion_pin', fail=True)
CONTROLS['literal_abandonment'] = dict(scen='h_exchange_race', fail=True)
CONTROLS['detach_without_phase_guard'] = dict(scen='c_store_detach', mut='detach_unlocked', fail=True,
    modes=('sc', 'sc_hb'), evidence={'sc': ('failure.done_gate',)})
CONTROLS['after_op_access'] = dict(scen='a_inline_value', fail=True)
CONTROLS['normal'] = dict(scen='e_subscribe_walk', fail=False)
# A late receiver keeps its operation until start returns, so member ownership
# legitimately covers the whole call even without start's local copies.
from .scenarios import setup, seq
CONTROLS['late_external_destroy'] = dict(scen='d_late', mut='member_start', fail=False, modes=('sc', 'sc_hb'),
    cfg=dict(actions=(('destroy',), ()), threads=(setup(2) + seq(('drop_sender', 0), ('start', 0), ('start', 1), ('destroy_op', 1)),)),
    evidence={'sc': ('delivery.late',), 'sc_hb': ('delivery.late',)})
# Pause before member releases: exchange is after the store position but before
# op_end, and both state and node still have the operation's references.
for name, mut, fail in (('store_interval', '', False), ('store_interval_omitted', 'no_active_store', True)):
    CONTROLS[name] = dict(scen='h_exchange_race', mut=mut, fail=fail, modes=('sc', 'sc_hb'),
        cfg=dict(pause_destructor=True, threads=(setup(move=True) + seq(('start', 0), ('destroy_op', 0)),
            seq(('await', 'store_position.0'), 'completion'))),
        evidence={mode: ('exchange.during_destructor', 'failure.suppression_in_destructor') if fail else ('exchange.during_destructor', 'suppressed.0') for mode in ('sc', 'sc_hb')})

def failure_manifest():
    rows = [s for s in Path(__file__).with_name('sensitivity.txt').read_text().splitlines() if s and not s.startswith('#')]
    if rows != sorted(set(rows)): raise ValueError('sensitivity must be sorted and unique')
    return frozenset(rows)
def declared(t):
    fail = t['name'] in failure_manifest()
    if t['control'] and fail != CONTROLS[t['control']]['fail']: raise ValueError('control declaration mismatch')
    return fail

# Explicit scenario groups are hand declarations, never populated from results.
RACING = ('b_three', 'b_two', 'c_store_detach')
COMPLETING = ('a_inline_error', 'a_inline_stopped', 'a_inline_throw', 'a_inline_value',
    'a_own_error', 'a_own_stopped', 'a_own_throw', 'a_own_value', 'b_before', 'b_three', 'b_two',
    'c_store_detach', 'd_concurrent_late', 'd_late', 'e_subscribe_late', 'e_subscribe_sync',
    'e_subscribe_walk', 'f_copy_move', 'f_external_op', 'f_sender_release', 'h_exchange_race',
    'h_keepalive_only', 'x_five')
KILLER_SCENARIOS = {
    'link_after_unlock': ('b_before',) + RACING,
    'phase_before_lock': RACING,
    'source_locked': ('a_inline_error', 'a_inline_stopped', 'a_inline_throw', 'a_inline_value',
        'b_three', 'b_two', 'd_concurrent_late', 'd_late', 'e_subscribe_late', 'e_subscribe_sync', 'x_five'),
    'no_running': ('b_before',) + RACING,
    'no_keepalive': ('h_exchange_race', 'h_keepalive_only'),
    'detach_unlocked': (),
    'deliver_locked': ('e_subscribe_sync', 'e_subscribe_walk'),
    'retain_keepalive': COMPLETING,
    'done_unlocked': RACING,
    'notify_in_store': tuple(s for s in COMPLETING if s not in ('a_inline_error', 'a_inline_stopped', 'a_own_error', 'a_own_stopped')),
    'no_completion_pin': (),
    'no_catch': ('a_inline_throw', 'a_own_throw'),
    'member_start': ('b_three', 'b_two', 'c_store_detach', 'd_concurrent_late', 'd_late',
        'e_subscribe_late', 'e_subscribe_sync', 'e_subscribe_walk', 'x_five'),
    'no_active_store': ('h_exchange_race', 'h_keepalive_only'),
    'late_locked': ('e_subscribe_late',),
}
KILLERS = {mid: tuple(s + '.' + mode for s in scenarios for mode in ('sc', 'sc_hb')) for mid, scenarios in KILLER_SCENARIOS.items()}
KILLERS['phase_before_lock'] += ('b_before.sc_hb',)
for mid, names in KILLERS.items(): CATALOG[mid]['killers'] = names

"""Finite source/receiver scripts, including explicitly nonstandard abandonment."""
def seq(*ops): return tuple((x,) if isinstance(x, str) else x for x in ops)
def setup(n=1, move=False):
    return seq('make', *(('connect', i, 'move' if move and i == 0 else 'copy', 0) for i in range(n)), 'publish')
R = ('await', 'ready')
S = ('await', 'source_ready')
SCENARIOS = {}
for mode in ('inline', 'own'):
    for payload in ('value', 'error', 'stopped', 'throw'):
        driver = setup() + seq(('drop_sender', 0), ('start', 0))
        workers = () if mode == 'inline' else (seq(S, 'completion'),)
        SCENARIOS['a_' + mode + '_' + payload] = dict(threads=(driver,) + workers, mode=mode, payload=payload,
            required_coverage=('completion.' + mode, 'path.subscribe'))
SCENARIOS.update({
    'a_unused': dict(threads=(setup() + seq(('destroy_op', 0), ('drop_sender', 0)),)),
    'b_two': dict(nodes=2, mode='inline', threads=(setup(2) + seq(('drop_sender', 0), ('start', 0)), seq(R, ('start', 1))),
        required_coverage=('launcher.0', 'launcher.1', 'path.late', 'path.subscribe')),
    'b_three': dict(nodes=3, mode='inline', threads=(setup(3) + seq(('drop_sender', 0), ('start', 0)),
        seq(R, ('start', 1), ('start', 2))), required_coverage=('launcher.0', 'launcher.1', 'detach.size3')),
    'c_store_detach': dict(nodes=2, threads=(setup(2) + seq(('drop_sender', 0), ('start', 0), ('start', 1)), seq(S, 'completion')),
        required_coverage=('path.late', 'path.subscribe', 'subscribe.before_event', 'subscribe.after_event', 'late.before_detach', 'late.after_detach')),
    'd_late': dict(nodes=2, mode='inline', threads=(setup(2) + seq(('drop_sender', 0), ('start', 0), ('start', 1)),),
        required_coverage=('path.late',)),
    'd_concurrent_late': dict(nodes=3, mode='inline', threads=(setup(3) + seq(('start', 0), ('signal', 'completed'), ('drop_sender', 0)),
        seq(('await', 'completed'), ('start', 1)), seq(('await', 'completed'), ('start', 2))), required_coverage=('path.late',)),
    'e_subscribe_walk': dict(nodes=2, actions=(('destroy', 'subscribe:1', 'drop:0'), ('destroy',)),
        threads=(setup() + seq(('start', 0)), seq(S, 'completion')),
        required_coverage=('self_destroy.notify', 'subscribe_inline.notify', 'reentry.notify.nested_late', 'self_destroy.nested_late', 'drop_sender_inline.notify')),
    'e_subscribe_sync': dict(nodes=2, mode='inline', actions=(('destroy', 'subscribe:1', 'drop:0'), ('destroy',)),
        threads=(setup() + seq(('start', 0)),),
        required_coverage=('self_destroy.sync', 'subscribe_inline.sync', 'reentry.sync.nested_late', 'self_destroy.nested_late', 'drop_sender_inline.sync')),
    'e_subscribe_late': dict(nodes=3, mode='inline', actions=(('destroy',), ('destroy', 'subscribe:2', 'drop:0'), ('destroy',)),
        threads=(setup(2) + seq(('start', 0), ('start', 1)),),
        required_coverage=('self_destroy.sync', 'self_destroy.late', 'subscribe_inline.late', 'reentry.late.nested_late', 'self_destroy.nested_late', 'drop_sender_inline.late')),
    'f_sender_release': dict(threads=(setup() + seq(('start', 0)), seq(S, 'completion'), seq(R, ('drop_sender', 0))),
        required_coverage=('state_destroy.T0', 'state_destroy.T1', 'state_destroy.T2')),
    'f_copy_move': dict(threads=(seq('make', ('copy_sender', 1), ('connect', 0, 'move', 0), 'publish', ('start', 0)),
        seq(S, 'completion'), seq(R, ('drop_sender', 1)))),
    'f_external_op': dict(actions=((),), threads=(setup(move=True) + seq(('start', 0)), seq(S, 'completion'),
        seq(('await', 'receiver_return.0'), ('destroy_op', 0)))),
    'g_source_connect': dict(failure='source_connect', threads=(seq('make'),)),
    'g_node_move': dict(failure='node', threads=(seq('make', ('connect', 0, 'move', 0)),)),
    'g_node_copy': dict(failure='node', threads=(seq('make', ('connect', 0, 'copy', 0), ('drop_sender', 0)),)),
    'h_keepalive_only': dict(abandon=(0,), actions=((),), threads=(setup(move=True) + seq(('start', 0), ('destroy_op', 0), ('signal', 'abandoned')),
        seq(('await', 'abandoned'), 'completion')), required_coverage=('suppressed.0', 'state_destroy.T1', 'keepalive.only', 'receiver_end.T1', 'state_destroy.completion')),
    'h_exchange_race': dict(abandon=(0,), actions=((),), threads=(setup(move=True) + seq(('start', 0), ('signal', 'can_abandon')),
        seq(S, 'completion'), seq(('await', 'can_abandon'), ('destroy_op', 0))),
        required_coverage=('suppressed.0', 'receive')),
    # Five identities, with two concurrent late starts and independent owner drops.
    # Completion is synchronous on T0; T1/T2 have no startup overlap with it.
    'x_five': dict(nodes=3, mode='inline', threads=(seq('make', ('copy_sender', 1), ('copy_sender', 2),
        ('connect', 0, 'move', 0), ('connect', 1, 'copy', 1), ('connect', 2, 'copy', 2), 'publish', ('start', 0), ('signal', 'completed')),
        seq(('await', 'completed'), ('start', 1)), seq(('await', 'completed'), ('start', 2)),
        seq(('await', 'destroyed.1'), ('drop_sender', 1)), seq(('await', 'destroyed.2'), ('drop_sender', 2)))),
})

SCENARIOS['b_before'] = dict(nodes=3, threads=(setup(3) + seq(('drop_sender', 0), ('start', 0), ('start', 1)), seq(R, ('start', 2)), seq(('await', 'start_return.1'), ('await', 'start_return.2'), 'completion')), required_coverage=('launcher.0', 'launcher.2', 'detach.size3'))

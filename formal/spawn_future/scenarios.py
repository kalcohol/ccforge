"""Finite receiver and producer scripts; no unbounded retries or addresses."""

def seq(*ops):
    return tuple((x,) if isinstance(x, str) else x for x in ops)

L = seq('launch')
A = ('await', 'launched')
P = ('await', 'producer_started')
D = ('await', 'producer_done')
K = ('await', 'connected')
S = ('await', 'started')
R = ('await', 'received')
I = ('await', 'installed')
END = seq('destroy_op')
CON = seq(A, 'connect', 'start', 'destroy_op')
# Scripts destroy operations only after start returns, unless explicitly marked
# robustness. This is also needed for callback optional constructor lifetime.
SCENARIOS = {
    'a_registered': dict(threads=(L + seq('connect', 'start', ('signal', 'ready'), D, 'destroy_op'),
                                  seq(('await', 'ready'), 'produce')), token='unstoppable',
                         required_coverage=('consume.registered', 'receiver')),
    'a_done': dict(threads=(L + seq(D, 'connect', 'start', 'destroy_op'), seq(P, 'produce')),
                   required_coverage=('consume.deliver_now',)),
    'a_inline': dict(threads=(L + seq('connect', 'start', 'destroy_op'),), mode='inline'),
    'a_error': dict(threads=(L + seq(D, 'connect', 'start', 'destroy_op'), seq(P, 'produce')), payload='error'),
    'a_race': dict(threads=(L + seq('connect', 'start', R, 'destroy_op'), seq(P, 'produce'))),
    'b_drop': dict(threads=(L + seq('drop_future'), seq(P, 'produce')), mode='ignore'),
    'b_move': dict(threads=(L + seq('move_future'), seq(P, 'produce')), mode='ignore'),
    'b_done': dict(threads=(L + seq(D, 'drop_future'), seq(P, 'produce'))),
    'b_sync': dict(threads=(L + seq('drop_future'),), mode='sync'),
    'c_unstarted': dict(threads=(L + seq('connect', 'destroy_op'), seq(P, 'produce')), mode='ignore'),
    'c_robust': dict(threads=(L + seq('connect', 'start', 'destroy_op'), seq(P, 'produce')), mode='ignore'),
    'd_pre': dict(threads=(seq('stop_source') + L + seq('connect', 'start', 'destroy_op'), seq(P, 'wait_stop', 'produce')),
                  mode='later', required_coverage=('register.inline', 'cancel.success')),
    'd_install': dict(threads=(L + seq('connect', 'start', R, 'destroy_op'), seq(P, 'wait_stop', 'produce'), seq(A, 'stop_source')),
                      mode='later'),
    'd_registered': dict(threads=(L + seq('connect', 'start', R, 'destroy_op'), seq(P, 'wait_stop', 'produce'), seq(S, 'stop_source')),
                         mode='later'),
    'd_race': dict(threads=(L + seq('connect', 'start', R, 'destroy_op'), seq(P, 'produce'), seq(S, 'stop_source'))),
    'e_sync': dict(threads=(L + seq('connect', 'start', R, 'destroy_op'), seq(S, 'stop_source')), mode='sync'),
    'e_later': dict(threads=(L + seq('connect', 'start', R, 'destroy_op'), seq(P, 'wait_stop', 'produce'), seq(S, 'stop_source')),
                    mode='later'),
    'f_closed': dict(threads=(L + seq('connect', 'start', 'destroy_op'),), closed=True,
                     required_coverage=('launch.not_started', 'consume.deliver_now')),
    'g_join': dict(threads=(L + seq('connect', 'start', R, 'destroy_op'), seq(P, 'produce'), seq(('await', 'released'), 'join'))),
    'g_scope': dict(threads=(L + seq('connect', 'start', R, 'destroy_op'), seq(P, 'wait_stop', 'produce'), seq(S, 'scope_stop')),
                    mode='later'),
    'g_scope_join': dict(threads=(L + seq('connect', 'start', R, 'destroy_op'), seq(S, 'scope_stop'),
                                  seq(('await', 'released'), ('await', 'scope_stop_returned'), 'join')), mode='sync'),
    'h_allocation': dict(threads=(L + seq('connect'), seq(P, 'produce')), failure='allocation'),
    'h_install': dict(threads=(L + seq('connect', 'start', 'destroy_op'), seq(P, 'produce')), failure='install'),
    'h_consume': dict(threads=(L + seq('connect', 'start', 'destroy_op'), seq(P, 'produce')), failure='consume'),
    'h_result': dict(threads=(L + seq(D, 'connect', 'start', 'destroy_op'), seq(P, 'produce')), failure='result'),
    'r_inline': dict(threads=(L + seq('connect', 'start', R), seq(P, 'produce')), inline_destroy=True),
    'r_unrelated': dict(threads=(L + seq('connect', 'start', R, 'destroy_op'), seq(P, 'produce')), inline_unrelated=True),
    'x_destroy': dict(threads=(L, seq(P, 'produce'), seq(A, 'connect', 'start'), seq(S, 'stop_source'),
                                seq(S, 'destroy_op')), mode='ignore'),
    'x_five': dict(threads=(L, seq(P, 'wait_stop', 'produce'), seq(A, 'connect', 'start', R, ('signal', 'destroy_ready')),
                             seq(S, 'stop_source'), seq(('await', 'destroy_ready'), 'destroy_op')), mode='later'),
}

# These obligations are checked over each entire baseline search, across every
# scheduler interleaving, independently of expected record hashing.
for name, required in {
    'a_race': ('consume.registered', 'consume.deliver_now'),
    'a_inline': ('producer.inline',),
    'b_drop': ('producer.ignore',), 'b_sync': ('producer.sync',),
    'c_unstarted': ('abandon_active',), 'c_robust': ('abandon_active', 'receiver'),
    'd_install': ('register.inline', 'register.pending', 'deregister.wait', 'cancel.success'),
    'd_registered': ('deregister.self', 'deregister.wait'),
    'e_sync': ('producer.sync', 'deregister.self'),
    'e_later': ('producer.later', 'cancel.success'),
    'g_join': ('join',), 'g_scope': ('scope_stop', 'producer.later'),
    'g_scope_join': ('scope_request_end', 'join', 'producer.sync'),
    'h_allocation': ('failure.allocation',), 'h_install': ('failure.install',),
    'h_consume': ('failure.consume',), 'h_result': ('failure.result',),
    'r_inline': ('op_dead', 'receiver'), 'r_unrelated': ('receiver.unrelated_destruction',),
    'x_five': ('deregister.wait', 'cancel.success'),
    'x_destroy': ('deregister.wait', 'abandon_active'),
}.items():
    SCENARIOS[name]['required_coverage'] = required

# Scope requests can precede construction, race the start boundary, or compete
# with the receiver token. These are separate coverage/obligation witnesses.
SCENARIOS.update({
    'g_pre_launch': dict(threads=(seq('scope_stop') + L + seq('connect', 'start', 'destroy_op'),), mode='sync',
        required_coverage=('scope.stop_before_launch', 'producer.pre_stopped', 'producer.register.inline', 'producer.stop_delivered', 'producer.sync')),
    'g_pre_start': dict(threads=(L + seq('connect', 'start', 'destroy_op'),
        seq(('await', 'before_start'), 'scope_stop')), mode='sync',
        required_coverage=('scope.stop_before_start', 'producer.pre_stopped', 'producer.stop_delivered')),
    'g_both_stop': dict(threads=(L + seq('connect', 'start', R, 'destroy_op'), seq(S, 'scope_stop'),
        seq(S, 'stop_source')), mode='sync',
        required_coverage=('producer.stop_delivered', 'producer.deregister.wait', 'deregister.self',
                           'producer.winner.0', 'producer.winner.1', 'producer.loser')),
    'c_handoff': dict(threads=(L + seq('connect'), seq(K, 'destroy_op'), seq(P, 'produce')), mode='ignore'),
    # Internal defensive branch, not a second legal public consumer.
    'a_defensive': dict(threads=(L + seq(D, 'connect', 'defensive_take', 'start', 'destroy_op'), seq(P, 'produce')),
        required_coverage=('consume.already_consumed',)),
})

SCENARIOS['c_abandon'] = dict(threads=(L + seq('connect', 'start', 'destroy_op', ('signal', 'abandoned')),
    seq(('await', 'abandoned'), 'produce')), mode='ignore', required_coverage=('consume.registered', 'abandon_active'))

# A child may register during connect. All producer modes receive notification
# before start; only start permits completion. Later/own/ignore have a separate
# producer thread, while inline and sync finish through their start gate.
for mode in ('own', 'inline', 'sync', 'later', 'ignore'):
    producer = () if mode in ('inline', 'sync') else (seq(P, 'wait_stop', 'produce'),)
    SCENARIOS['g_connect_pre_' + mode] = dict(
        threads=(seq('scope_stop') + L + seq('connect', 'start', R, 'destroy_op'),) + producer,
        mode=mode, registration='connect', required_coverage=(
            'producer.registration.connect', 'producer.notification.before_start',
            'producer.register.inline', 'producer.completion.after_prestart', 'producer.' + mode))

SCENARIOS.update({
    # Unlike the pre-stopped constructor cases, the scope request can select an
    # already-pending registration before start, or race after start.
    'g_connect_pending': dict(threads=(L + seq('connect', 'start', R, 'destroy_op'),
        seq(('await', 'producer_connected'), 'scope_stop')), mode='sync', registration='connect',
        required_coverage=('producer.register.pending', 'producer.select.1',
                           'producer.notification.before_start', 'producer.completion.after_prestart')),
    'f_connect_closed': dict(threads=(L + seq('connect', 'start', 'destroy_op'),),
        closed=True, registration='connect', required_coverage=('launch.not_started',
            'producer.register.pending', 'producer.deregister.unstarted.0', 'producer.deregister.unstarted.1',
            'producer.storage_end.unstarted')),
    'f_connect_stopped': dict(threads=(seq('scope_stop') + L + seq('connect', 'start', 'destroy_op'),),
        closed=True, registration='connect', mode='sync', required_coverage=('launch.not_started',
            'producer.notification.before_start', 'producer.register.inline',
            'producer.deregister.unstarted.0', 'producer.deregister.unstarted.1')),
})

# A closed launch can destroy the child before a subsequent scope request.
# The source remains alive; its removed registration must never be selected.
SCENARIOS['f_connect_closed_late_stop'] = dict(
    threads=(L + seq('connect', 'start', 'destroy_op', 'scope_stop'),),
    closed=True, registration='connect', required_coverage=('launch.not_started',
        'producer.deregister.unstarted.0', 'producer.deregister.unstarted.1',
        'producer.storage_end.unstarted', 'scope.stop_after_state_end'))

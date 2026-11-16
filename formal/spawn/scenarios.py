"""Finite legal launch/child/request/join scripts, at most five threads."""
def seq(*ops): return tuple((x,) if isinstance(x,str) else x for x in ops)
P=('await','child_started')
L=seq('launch')
SCENARIOS={
    'a_inline':dict(threads=(L,),mode='inline',required_coverage=('completion.inline',)),
    'a_later':dict(threads=(L,seq(P,'produce')),required_coverage=('completion.own',)),
    'b_release_race':dict(threads=(L,seq(P,'produce')),required_coverage=('destroyer.launch','destroyer.completion')),
    'c_closed':dict(threads=(L,),closed=True,required_coverage=('not_associated',)),
    'c_connected_closed':dict(threads=(L+seq(('stop',0)),),closed=True,registration='connect',required_coverage=('register.pending.0','register.pending.1')),
    'c_stopped_closed':dict(threads=(seq(('stop',0))+L,),closed=True,registration='connect',mode='sync'),
    'd_sync':dict(threads=(L,seq(P,('stop',0))),mode='sync',required_coverage=('completion.sync','deregister.self','deregister.wait.0')),
    'd_dual_sync':dict(threads=(L,seq(P,('stop',0)),seq(P,('stop',1))),mode='sync',required_coverage=('completion.sync','winner.0','winner.1','loser','deregister.self')),
    'd_later':dict(threads=(L,seq(P,'wait_notice','produce'),seq(P,('stop',0))),mode='later',required_coverage=('completion.later',)),
    'd_ignore':dict(threads=(L,seq(P,'produce'),seq(P,('stop',0))),mode='ignore'),
    'd_connect_race':dict(threads=(L,seq(('await','connected_boundary'),('stop',0))),mode='sync',registration='connect',required_coverage=('notification.before_start','completion.sync')),
    'e_join':dict(threads=(L,seq(P,'produce'),seq('join','destroy_scope')),required_coverage=('join',)),
    'e_stop_join':dict(threads=(L,seq(P,('stop',0)),seq(('await','stop_returned.0'),'join','destroy_scope')),mode='sync'),
    'f_construction':dict(threads=(L,),failure='construction'),
    'f_connect':dict(threads=(L,),failure='connect'),
    'x_five':dict(threads=(L,seq(P,'wait_notice','produce'),seq(P,('stop',0)),seq(P,('stop',1)),seq(('await','stop_returned.0'),('await','stop_returned.1'),'join','destroy_scope')),mode='later',required_coverage=('winner.0','winner.1','loser','deregister.wait.0','deregister.wait.1')),
}
# No readiness await: each requester may run before either query, between the
# queries and registration, or after registration. Cover both input positions.
for registration in ('connect','start'):
    for slot in (0,1):
        suffix=str(slot)
        SCENARIOS['d_register_'+registration+'_'+suffix]=dict(
            threads=(L,seq(('stop',slot))),mode='sync',registration=registration,
            required_coverage=tuple(x+'.'+suffix for x in (
                'precheck.false','precheck.true','register.pending','register.inline','select',
                'precheck_to_register.false.pending','precheck_to_register.false.inline',
                'precheck_to_register.true.inline'))+('completion.sync',),
            required_stale_coverage=('precheck.multiple_choices.'+suffix,
                'precheck_to_register.stale_false.inline.'+suffix))
for mode in ('own','inline','sync','later','ignore'):
    workers=() if mode in ('inline','sync') else (seq(P,'wait_notice','produce'),)
    SCENARIOS['d_pre_'+mode]=dict(threads=(seq(('stop',0))+L,)+workers,registration='connect',mode=mode,required_coverage=('notification.before_start','stop.delivered','completion.'+mode))

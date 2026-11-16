"""Finite receiver/sender scripts; one operation, up to three sender objects."""
def seq(*ops): return tuple((x,) if isinstance(x,str) else x for x in ops)
P=('await','child_started')
R=('await','received')
S=seq(('sender','s'),('connect','s'),('destroy_sender','s'),'start')
END=seq(R,'destroy_op')
SCENARIOS={
    'a_inline':dict(threads=(S+END,),mode='inline',payload='error',receiver_touch=False,required_coverage=('completion.inline',)),
    'a_later':dict(threads=(S+END,seq(P,'produce'))),
    'c_closed':dict(threads=(S+END,),closed=True,required_coverage=('association.rejected',)),
    'c_reject_const':dict(threads=(seq(('sender','s'),('const_connect','s'),'start')+END+seq(('destroy_sender','s')),),reject='const_connect'),
    'd_sync':dict(threads=(S+END,seq(P,('stop',0))),mode='sync',required_coverage=('completion.sync','deregister.wait.0')),
    'd_dual_sync':dict(threads=(S+END,seq(P,('stop',0)),seq(P,('stop',1))),mode='sync',required_coverage=('completion.sync','winner.0','winner.1','loser')),
    'd_later':dict(threads=(S+END,seq(P,'wait_notice','produce'),seq(P,('stop',0))),mode='later'),
    'd_ignore':dict(threads=(S+END,seq(P,'produce'),seq(P,('stop',0))),mode='ignore'),
    'd_connect_race':dict(threads=(S+END,seq(('await','connected'),('stop',0))),mode='sync',registration='connect',required_coverage=('notification.before_start','completion.sync')),
    'e_join':dict(threads=(S+END,seq(P,'produce'),seq('join','destroy_scope'))),
    'e_stop_join':dict(threads=(S+END,seq(P,('stop',0)),seq(('await','stop_returned.0'),'join','destroy_scope')),mode='sync'),
    'f_connect':dict(threads=(seq(('sender','s'),('connect','s'),('destroy_sender','s')),),failure='connect'),
    'r_inline':dict(threads=(S+seq(R),seq(P,'produce')),inline_destroy=True),
    'r_sync':dict(threads=(S+seq(R),seq(P,('stop',0))),mode='sync',inline_destroy=True),
    'r_start_inline':dict(threads=(S+seq(R),),mode='inline',inline_destroy=True,required_coverage=('completion.inline','receiver.self_destroy')),
    's_unused':dict(threads=(seq(('sender','s'),('destroy_sender','s')),)),
    's_copy':dict(threads=(seq(('sender','s'),('copy_sender','s','copy'),('destroy_sender','s'),('destroy_sender','copy')),)),
    's_copy_rejected':dict(threads=(seq(('sender','s'),('copy_sender','s','copy'),('destroy_sender','s'),('destroy_sender','copy')),),reject='copy'),
    's_move':dict(threads=(seq(('sender','s'),('move_sender','s','moved'),('destroy_sender','s'),('connect','moved'),('destroy_sender','moved'),'start')+END,),mode='inline'),
    's_const':dict(threads=(seq(('sender','s'),('const_connect','s'),('destroy_sender','s'),'start')+END,),mode='inline'),
    's_unstarted':dict(threads=(seq(('sender','s'),('connect','s'),('destroy_sender','s'),'destroy_op',('stop',0)),),registration='connect'),
    'x_five':dict(threads=(S+seq(R,('signal','destroy_ready')),seq(P,'wait_notice','produce'),seq(P,('stop',0)),seq(P,('stop',1)),seq(('await','destroy_ready'),'destroy_op')),mode='later',required_coverage=('winner.0','winner.1','loser','deregister.wait.0','deregister.wait.1')),
}
# No readiness await: each requester may run before either query, between the
# queries and registration, or after registration. Cover both input positions.
for registration in ('connect','start'):
    for slot in (0,1):
        suffix=str(slot)
        SCENARIOS['d_register_'+registration+'_'+suffix]=dict(
            threads=(S+END,seq(('stop',slot))),mode='sync',registration=registration,
            required_coverage=tuple(x+'.'+suffix for x in (
                'precheck.false','precheck.true','register.pending','register.inline','select',
                'precheck_to_register.false.pending','precheck_to_register.false.inline',
                'precheck_to_register.true.inline'))+('completion.sync',),
            required_stale_coverage=('precheck.multiple_choices.'+suffix,
                'precheck_to_register.stale_false.inline.'+suffix))
for mode in ('own','inline','sync','later','ignore'):
    workers=() if mode in ('inline','sync') else (seq(P,'wait_notice','produce'),)
    SCENARIOS['d_pre_'+mode]=dict(threads=(seq(('stop',0))+S+END,)+workers,registration='connect',mode=mode,payload='stopped' if mode=='sync' else 'value',required_coverage=('notification.before_start','stop.delivered','completion.'+mode))

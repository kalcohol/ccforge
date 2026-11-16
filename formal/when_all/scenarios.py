"""Finite children: up to three, connect/start registration and cancellation modes."""
def seq(*ops): return tuple((v,) if isinstance(v,str) else v for v in ops)
def scenario(payloads,modes,registrations=None,outer=False,pre=False,**kw):
 n=len(payloads); regs=registrations or ('start',)*n
 driver=seq('construct','start')
 if pre: driver=seq('outer_stop')+driver
 workers=tuple(seq(('await','child_started.'+str(i)),*( (('inner',i,('wait_notice',)),) if mode=='later' else ()),('inner',i,('produce',))) for i,mode in enumerate(modes) if mode in ('own','ignore','later'))
 if outer: workers+=(seq('outer_stop'),)
 return dict(payloads=payloads,modes=modes,registrations=regs,threads=(driver,)+workers,**kw)
SCENARIOS={
 'a_inline':scenario(('value',),('inline',)),
 'a_values':scenario(('value','value'),('inline','own')),
 'a_three':scenario(('value','value','value'),('inline','inline','inline')),
 'b_error':scenario(('error','value'),('own','inline')),
 'b_two_errors':scenario(('error','error'),('own','own')),
 'b_error_stop':scenario(('error','stopped'),('own','own')),
 'c_stopped':scenario(('stopped','value'),('own','inline')),
 'd_outer':scenario(('value',),('own',),outer=True,required_coverage=('outer.credit','outer.zero','cas.spurious','deregister.wait.0')),
 'd_pre':scenario(('stopped',),('sync',),pre=True,required_coverage=('register.inline.0','notification.before_start')),
 'd_nested':scenario(('error','stopped'),('inline','sync'),('connect','connect')),
 'd_later':scenario(('stopped',),('later',),outer=True),
 'd_ignore':scenario(('value',),('ignore',),outer=True),
 'e_throw':scenario(('stopped',),('sync',),registration_throw=True),
 'e_impossible':scenario(('value',),('inline',),stop_possible=False),
}

SCENARIOS['a_two_values']=scenario(('value','value'),('own','own'))
SCENARIOS['d_multi_cancel']=scenario(('error','stopped','stopped'),('own','sync','sync'),('connect',)*3,required_coverage=('select.child.1','select.child.2','completion.sync'))
SCENARIOS['e_unstarted']=dict(payloads=('value',),modes=('own',),registrations=('connect',),threads=(seq('construct','destroy'),))

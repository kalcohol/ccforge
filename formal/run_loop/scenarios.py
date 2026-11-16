"""Finite producers close submission before final pop; tails may overlap teardown."""
def seq(*ops): return tuple((v,) if isinstance(v,str) else v for v in ops)
SCENARIOS={
 'a_one':dict(threads=(seq(('push',0),'finish'),seq('pop','destroy'))),
 'b_pushers':dict(required_coverage=('tail.after_destroy.push',),tasks=2,run_thread=2,threads=(seq(('push',0),('await','enqueued.1'),'finish'),seq(('push',1)),seq('pop','destroy'))),
 'c_wait':dict(threads=(seq(('push',0),'finish'),seq('pop','destroy')),required_coverage=('wake.spurious','wake.notified')),
 'd_stop':dict(threads=(seq(('push',0),'finish'),seq('pop','destroy'),seq(('stop',0))),required_coverage=('task.stopped','task.value'),required_stale_coverage=('token.multiple_choices',)),
 'e_finish_completion':dict(threads=(seq(('push',0)),seq('pop','destroy')),completion_finish=True),
 'f_finish_race':dict(tasks=2,run_thread=2,threads=(seq(('push',0),('await','enqueued.1'),'finish'),seq(('push',1)),seq('pop','destroy'))),
 'g_empty':dict(tasks=0,threads=(seq('finish'),seq('pop','destroy')),required_coverage=('tail.after_destroy.finish',)),
 'h_early':dict(threads=(seq(('push',0),'destroy'),seq(('await','complete.0'))),strict=True),
 'i_early_execute':dict(threads=(seq(('push',0),('await','complete.0'),'destroy'),seq('pop')),strict=True,hold_after_complete=True),
 # Only run waits for closed submissions; finish and the second push race.
 'j_finish_push_before_run':dict(tasks=2,run_thread=2,threads=(seq(('push',0),'finish'),seq(('push',1)),seq(('await','enqueued.0'),('await','enqueued.1'),'pop','destroy')),required_coverage=('enqueue.1.before_finish','enqueue.1.after_finish')),
}

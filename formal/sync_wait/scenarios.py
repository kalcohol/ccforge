"""Inline and independent-thread completions for all result alternatives."""
def seq(*ops): return tuple((v,) if isinstance(v,str) else v for v in ops)
SCENARIOS={}
for mode in ('inline','own'):
 for payload in ('value','error','stopped'):
  SCENARIOS[mode+'_'+payload]=dict(mode=mode,payload=payload,threads=(seq('connect','start'),)+( () if mode=='inline' else (seq(('await','child_started'),'produce'),)),required_coverage=('completion.'+mode,'run.contract_return'))
SCENARIOS['value_destructor']=dict(mode='inline',state_uses_loop=True,threads=(seq('connect','start'),))

SCENARIOS['scheduled_value']=dict(threads=(seq('connect','start'),seq(('await','child_started'),'submit')),required_coverage=('run.contract_task','run.contract_return'))

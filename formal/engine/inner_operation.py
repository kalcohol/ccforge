"""Finite abstract inner-operation scripts shared by protocol clients.

A child publishes readiness only after registration constructors return. This
models legal child implementations, including pre-start stop preservation.
"""
from .memory import change, synchronization

FIELDS = '''child_alive child_started child_ready claimed completion_obligation completed notice prestart notice_pub callbacks scope requests'''.split()
ACTIONS = '''child_construct prepare query register invoke fn invoke_end deregister child_start child_ready produce stop request_end query_env wait_notice'''.split()


class Inner:
    def __init__(self, cfg, mem, lib, error, cov, prepend, signal, touch):
        self.c, self.mem, self.lib = cfg, mem, lib
        self.error, self.cov, self.prepend = error, cov, prepend
        self.signal, self.touch = signal, touch

    def enabled(self, w, t, op):
        if op[0] == 'deregister':
            return self.lib.removable(w,t,op[1])
        if op[0] == 'wait_notice':
            return w.notice
        return True

    def choices(self,w,t,op):
        if op[0] in ('query','query_env'):
            return self.mem.load_choices(w,t,1+op[1])
        return (0,)

    def execute(self,w,t,op,alt):
        a,*args=op
        c,lib,mem=self.c,self.lib,self.mem
        if a == 'child_construct':
            w.child_alive=True
            self.touch(w,t)
            if c.registration=='connect':
                self.cov.add('registration.connect')
                self.prepend(w,t,('prepare',))
        elif a == 'prepare':
            self.prepend(w,t,('query',0))
        elif a == 'query':
            slot=args[0]
            lib.source(w,t,slot)
            stopped=mem.load(w,t,1+slot,alt)
            if stopped:
                lib.pre_stopped(w)
                self.prepend(w,t,('invoke',-1),('register',0),('register',1))
            elif slot==0:
                self.prepend(w,t,('query',1))
            else:
                self.prepend(w,t,('register',0),('register',1))
        elif a == 'register':
            slot=args[0]
            if lib.register(w,t,slot,1+slot):
                self.prepend(w,t,('invoke',slot),('invoke_end',slot))
        elif a == 'invoke':
            if lib.invoke(w,t,args[0]):
                if c.control!='drop_stop':
                    self.prepend(w,t,('fn',))
        elif a == 'fn':
            lib.fn(w,t)
            if not w.child_ready:
                w.prestart=True
                self.cov.add('notification.before_start')
            if c.control!='drop_notice':
                w.notice=True
                w.notice_pub=synchronization(w,t)
            if w.child_ready and c.mode=='sync' and not w.claimed:
                self.prepend(w,t,('produce',))
        elif a == 'invoke_end':
            lib.invocation_end(w,t,args[0])
        elif a == 'deregister':
            slot=args[0]
            if c.control != 'skip_input_'+str(slot):
                lib.deregister(w,t,slot)
        elif a == 'child_start':
            self.touch(w,t)
            w.child_started=True
            w.completion_obligation=True
            registration=(('prepare',),) if c.registration=='start' else ()
            self.prepend(w,t,*registration,('child_ready',))
        elif a == 'child_ready':
            w.child_ready=True
            self.signal(w,t,'child_started')
            if w.notice:
                mem.acquire(w,t,w.notice_pub)
            if c.mode=='inline' or c.mode=='sync' and w.notice:
                self.prepend(w,t,('produce',))
        elif a == 'produce':
            if not w.child_started:
                self.error(w,'completion before child start')
            if not w.claimed:
                w.claimed=True
                self.cov.add('completion.'+c.mode)
                if c.control!='drop_completion':
                    self.prepend(w,t,('complete',))
        elif a == 'stop':
            slot=args[0]
            selected=lib.request(w,t,slot,1+slot)
            self.prepend(w,t,*( (('invoke',slot),('invoke_end',slot)) if selected else ()),('request_end',slot))
        elif a == 'request_end':
            lib.request_end(w,t,args[0])
            self.signal(w,t,'stop_returned.'+str(args[0]))
        elif a == 'query_env':
            lib.source(w,t,args[0]); mem.load(w,t,1+args[0],alt)
        elif a == 'wait_notice':
            mem.acquire(w,t,w.notice_pub)
        else:
            return False
        return True

    def errors(self,w,terminal=False):
        errors=list(self.lib.callback_errors(w,terminal))
        if w.prestart and not w.notice:
            errors.append('lost pre-start stop notification')
        if w.completed and not w.child_started:
            errors.append('completion before child start')
        if terminal and w.completion_obligation and not w.completed:
            errors.append('lost child completion obligation')
        return errors

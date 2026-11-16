"""Protocol-independent borrowed stop sources and scope association contracts.

Immutable records live in the client's state key. Publications are library
handoffs, never invented modifications of a client's atomic objects. A client
supplies storage/source accessors and keeps invocation return on the stack.
The fused contract is the committed-delivery boundary checked by counting_scope.
"""
from typing import NamedTuple
from .memory import change, synchronization, empty_sync


class Callbacks(NamedTuple):
    status: tuple
    owner: tuple
    publication: tuple
    returned: tuple
    constructed: tuple
    obligation: tuple
    winner: int = -2
    calls: int = 0
    committed: bool = False


class Scope(NamedTuple):
    alive: bool
    source_alive: bool
    engaged: tuple
    acquired: tuple
    released: tuple
    publication: tuple
    next_id: int = 0
    joined: bool = False


class Library:
    def __init__(self, memory, detect, coverage, storage, source):
        self.mem, self.detect, self.cov = memory, detect, coverage
        self.storage, self.source = storage, source

    def initialize(self, w, inputs):
        pub = empty_sync(w)
        w.callbacks = Callbacks(('absent',)*inputs, (-1,)*inputs, (pub,)*inputs,
                                (pub,)*inputs, (False,)*inputs, (False,)*inputs)
        w.scope = Scope(True, True, (), (), (), pub)
        w.requests = (-1,)*inputs

    def cbset(self, w, field, slot, value):
        record = w.callbacks
        seq = getattr(record, field)
        w.callbacks = record._replace(**{field: seq[:slot]+(value,)+seq[slot+1:]})

    def register(self, w, t, slot, atomic):
        self.storage(w, t)
        self.source(w, t, slot)
        assert not w.callbacks.constructed[slot]
        self.cbset(w, 'constructed', slot, True)
        self.cbset(w, 'publication', slot, synchronization(w, t))
        stopped = self.mem.load(w, t, atomic, len(w.hist[atomic])-1)
        if stopped:
            self.cbset(w, 'status', slot, 'inline')
            self.cbset(w, 'owner', slot, t)
            self.cbset(w, 'obligation', slot, True)
            self.cov.add('register.inline.'+str(slot))
        else:
            self.cbset(w, 'status', slot, 'pending')
            self.cov.add('register.pending.'+str(slot))
        return stopped

    def request(self, w, t, slot, atomic):
        self.source(w, t, slot)
        change(w, 'requests', slot, t)
        if w.hist[atomic][-1][0]:
            return False
        self.mem.publish(w, t, atomic, True, 'release')
        # One registration per input source, no independent observer after
        # registration: publication/selection share this abstract linearization.
        if w.callbacks.status[slot] != 'pending':
            return False
        self.cbset(w, 'obligation', slot, True)
        self.cbset(w, 'status', slot, 'executing')
        self.cbset(w, 'owner', slot, t)
        self.mem.acquire(w, t, w.callbacks.publication[slot])
        self.cov.add('select.'+str(slot))
        return True

    def request_end(self, w, t, slot):
        self.source(w, t, slot)
        change(w, 'requests', slot, -1)

    def pre_stopped(self, w):
        # Independent delivery obligation, before the client's dispatch decision.
        w.callbacks = w.callbacks._replace(committed=True)

    def invoke(self, w, t, slot):
        self.storage(w, t)
        if w.callbacks.winner == -2:
            w.callbacks = w.callbacks._replace(winner=t, committed=True)
            self.cov.add('winner.'+str(slot))
            return True
        self.cov.add('loser')
        return False

    def fn(self, w, t):
        self.storage(w, t)
        w.callbacks = w.callbacks._replace(calls=w.callbacks.calls+1)
        self.cov.add('stop.delivered')

    def invocation_end(self, w, t, slot):
        # Stack-only after self destruction: no storage or source access here.
        self.cbset(w, 'owner', slot, -1)
        if w.callbacks.status[slot] == 'executing':
            self.cbset(w, 'status', slot, 'completed')
        self.cbset(w, 'returned', slot, synchronization(w, t))

    def removable(self, w, t, slot):
        ready = w.callbacks.owner[slot] in (-1, t)
        if not ready:
            self.cov.add('deregister.wait.'+str(slot))
        return ready

    def deregister(self, w, t, slot):
        assert self.removable(w, t, slot)
        self.storage(w, t)
        status = w.callbacks.status[slot]
        if status in ('absent', 'removed'):
            return
        # An inline-only constructor never linked into the source.
        if status != 'inline':
            self.source(w, t, slot)
        if w.callbacks.owner[slot] == -1:
            self.mem.acquire(w, t, w.callbacks.returned[slot])
        self.cov.add('deregister.self' if w.callbacks.owner[slot] == t else 'deregister.external')
        self.cbset(w, 'status', slot, 'removed')

    def registration_errors(self, w):
        return tuple('registration %d survives storage' % i for i, made in enumerate(w.callbacks.constructed)
                     if made and w.callbacks.status[i] != 'removed')

    def callback_errors(self, w, terminal=False):
        errors = []
        if w.callbacks.calls > 1:
            errors.append('fused callback delivered twice')
        if terminal and (w.callbacks.committed or any(w.callbacks.obligation)) and w.callbacks.calls != 1:
            errors.append('lost stop delivery obligation')
        return tuple(errors)

    def scope_access(self, w, t, write=False):
        if not w.scope.alive:
            self.detect(w, 'scope access after destruction')
        self.mem.access(w, t, 'scope storage', write)

    def associate(self, w, t, allowed=True):
        self.scope_access(w, t)
        self.mem.acquire(w, t, w.scope.publication)
        if not allowed or w.scope.joined:
            return -1  # No atomic modification on rejection.
        s = w.scope
        identity = s.next_id
        w.scope = s._replace(engaged=s.engaged+(identity,), acquired=s.acquired+(identity,),
                             next_id=identity+1, publication=synchronization(w, t))
        return identity

    def release(self, w, t, identity):
        if identity == -1:
            return
        self.scope_access(w, t)
        self.mem.acquire(w, t, w.scope.publication)
        s = w.scope
        if identity not in s.engaged:
            self.detect(w, 'association released twice')
        w.scope = s._replace(engaged=tuple(i for i in s.engaged if i != identity),
                             released=s.released+(identity,), publication=synchronization(w, t))

    def join(self, w, t):
        self.mem.acquire(w, t, w.scope.publication)
        self.scope_access(w, t)
        if w.scope.engaged:
            self.detect(w, 'join before final association release')
        w.scope = w.scope._replace(joined=True)
        self.cov.add('join')

    def destroy_scope(self, w, t):
        self.scope_access(w, t, True)
        self.mem.access(w, t, 'scope stop source', True)
        if w.scope.engaged or not w.scope.joined:
            self.detect(w, 'scope destroyed before join')
        if w.requests[0] != -1:
            self.detect(w, 'scope destroyed inside request_stop')
        if w.callbacks.status[0] not in ('absent', 'removed', 'inline'):
            self.detect(w, 'scope source destroyed with registration')
        w.scope = w.scope._replace(alive=False, source_alive=False)

    def scope_errors(self, w, terminal=False):
        s = w.scope
        errors = []
        if len(set(s.released)) != len(s.released):
            errors.append('association released twice')
        if terminal and (s.engaged or sorted(s.acquired) != sorted(s.released)):
            errors.append('association leak')
        return tuple(errors)

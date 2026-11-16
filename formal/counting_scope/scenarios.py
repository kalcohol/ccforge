"""Finite scripts; holders mirror the association contract, not implementation files."""


def A(h):
    return ('associate', h)


def R(h):
    return ('release', h)


def J(j=0):
    return ('join', j)


def D(j=0):
    return ('destroy_join', j)


CLOSE = ('close',)
DESTROY = ('destroy_scope',)
SCENARIOS = {
    'a_race': dict(threads=((('write', 0), R(0)), (J(),))),
    'a_before': dict(threads=((J(), ('signal', 'j')), (('await', 'j'), R(0)))),
    'a_after': dict(threads=((R(0), J(), DESTROY),)),
    'a_keep': dict(threads=((J(),),), keep_association=True),
    'a_empty': dict(initial_holders=(), threads=((J(), DESTROY),)),
    'b_three': dict(nholders=3, initial_holders=(0, 1, 2), threads=(
        (('write', 0), R(0)), (('write', 1), R(1)), (('write', 2), R(2)), (J(),))),
    'c_joiners': dict(njoiners=2, threads=((J(0),), (J(1),), (R(0),))),
    'c_registered': dict(njoiners=2, threads=((J(0), ('signal', 'j0')), (J(1), ('signal', 'j1')),
        (('await', 'j0'), ('await', 'j1'), R(0))), continuations={0: (D(0),), 1: (D(1),)}),
    'd_close': dict(nholders=2, threads=((CLOSE,), (A(1), R(1)), (J(),), (R(0),))),
    'd_open_join': dict(nholders=2, threads=((J(), ('signal', 'j')), (('await', 'j'), A(1), R(1), R(0)))),
    'd_limit': dict(nholders=3, limit=2, threads=((A(1), A(2), R(2), R(1), R(0), J()),)),
    'd_observers': dict(threads=((R(0), J(), ('signal', 'joined')),
        (('count',), ('closed',), ('await', 'joined'), ('count',), ('closed',)))),
    'e_nested': dict(nholders=2, threads=((A(1), ('write', 1), R(0), R(1)), (J(),))),
    'f_destroy': dict(threads=((J(),), (R(0),)), continuations={0: (D(), DESTROY)}),
    'f_reenter': dict(nholders=2, njoiners=2, threads=((J(),), (R(0),)),
        continuations={0: (D(), J(1), A(1), CLOSE, ('request', 0)), 1: (D(1),)}),
    'f_closed_reject': dict(nholders=2, threads=((CLOSE, A(1), R(0), J(), DESTROY),)),
    'f_two_destroy': dict(njoiners=2, threads=((J(0), ('signal', 'j0')), (J(1), ('signal', 'j1')),
        (('await', 'j0'), ('await', 'j1'), R(0))), continuations={0: (D(0), DESTROY), 1: (D(1),)}),
    'g_early': dict(strict=True, threads=((DESTROY,),)),
    'g_token': dict(strict=True, initial_holders=(), threads=((DESTROY, ('token',)),)),
    'g_open_drained': dict(strict=True, threads=((R(0), DESTROY),)),
    's_holder_order': dict(ncallbacks=1, inputs=((0,),), initial_callbacks=(0,),
        threads=((('holder_done', 0, 0),), (J(),)), continuations={0: (D(), DESTROY)}),
    's_scope': dict(ncallbacks=1, inputs=((0,),), initial_callbacks=(0,),
        bodies={0: (('holder_done', 0, 0),)}, threads=((('request', 0), ('signal', 'r0')), (J(),),
        (('wait_all',), ('await', 'r0'), ('wait_idle',), DESTROY))),
    's_fused': dict(ncallbacks=1, inputs=((0, 1),), initial_callbacks=(0,),
        bodies={0: (('holder_done', 0, 0),)}, threads=((('request', 0), ('signal', 'r0')), (('request', 1), ('signal', 'r1')), (J(),),
        (('wait_all',), ('await', 'r0'), ('await', 'r1'), ('wait_idle',), DESTROY))),
    's_inside_request': dict(strict=True, ncallbacks=1, inputs=((0,),), initial_callbacks=(0,),
        bodies={0: (('holder_done', 0, 0),)}, threads=((J(), ('signal', 'joined_start')),
        (('await', 'joined_start'), ('request', 0))), continuations={0: (D(), DESTROY)}),
    's_late': dict(ncallbacks=1, inputs=((0, 1),), initial_holders=(),
        threads=((('request', 0), ('construct', 0), ('destroy_cb', 0), J(), DESTROY),)),
    'u_race': dict(ncallbacks=1, inputs=((0, 1),), initial_holders=(),
        threads=((('construct', 0),), (('request', 0),), (('request', 1),))),
    'u_pre0': dict(ncallbacks=1, inputs=((0, 1),), initial_holders=(), stopped=(True, False),
        threads=((('construct', 0),), (('request', 1),))),
    'u_pre1': dict(ncallbacks=1, inputs=((0, 1),), initial_holders=(), stopped=(False, True),
        threads=((('construct', 0),), (('request', 0),))),
    'u_pre_both': dict(ncallbacks=1, inputs=((0, 1),), initial_holders=(), stopped=(True, True),
        threads=((('construct', 0), ('destroy_cb', 0)),)),
    'u_hb_pre': dict(ncallbacks=1, inputs=((0, 1),), initial_holders=(),
        threads=((('request', 1), ('signal', 'stop')), (('await', 'stop'), ('construct', 0)),
                 (('request', 0),))),
    'u_destroy': dict(ncallbacks=1, inputs=((0, 1),), initial_callbacks=(0,), initial_holders=(),
        threads=((('request', 0),), (('request', 1),), (('destroy_cb', 0),))),
    'u_self0': dict(ncallbacks=1, inputs=((0, 1),), initial_callbacks=(0,), initial_holders=(),
        bodies={0: (('destroy_cb', 0),)}, threads=((('request', 0),), (('request', 1),))),
    'u_self1': dict(ncallbacks=1, inputs=((0, 1),), initial_callbacks=(0,), initial_holders=(),
        bodies={0: (('destroy_cb', 0),)}, threads=((('request', 1),),)),
    'u_sibling': dict(ncallbacks=2, inputs=((0, 1), (0, 1)), initial_callbacks=(0, 1), initial_holders=(),
        bodies={0: (('destroy_cb', 1),)}, threads=((('request', 0),), (('request', 1),))),
    'u_pending_remove': dict(ncallbacks=2, inputs=((0, 1), (0, 1)), initial_callbacks=(0, 1), initial_holders=(),
        bodies={0: (('signal', 'in_request'), ('await', 'removed'))},
        threads=((('request', 0),), (('await', 'in_request'), ('destroy_cb', 1), ('signal', 'removed')))),
    'u_same_ctor': dict(ncallbacks=1, inputs=((0, 0),), initial_holders=(),
        threads=((('construct', 0), ('destroy_cb', 0)), (('request', 0),))),
    'u_same_source': dict(ncallbacks=1, inputs=((0, 0),), initial_callbacks=(0,), initial_holders=(),
        threads=((('request', 0),),)),
}


SCENARIOS.update({
    # Observer handoffs only: is_closed is true only in joined in this script.
    # Failed polling iterations are elided; successful loads remain explicit.
    'f_observer_teardown': dict(threads=((J(), ('wait_zero',), ('wait_closed',), DESTROY), (R(0),))),
    'd_unused_closed_destroy': dict(initial_holders=(), threads=((CLOSE, DESTROY),),
        required_coverage=('state.unused_and_closed', 'destructor.accept')),
    'd_unused_closed_join': dict(initial_holders=(), threads=((CLOSE, J(), DESTROY),),
        required_coverage=('state.unused_and_closed', 'join.empty')),
    'd_joining_observers': dict(nholders=3, initial_holders=(0, 1, 2), threads=(
        (J(), ('signal', 'registered'), ('await', 'observed3'), R(0), ('signal', 'two'),
         ('await', 'observed2'), CLOSE, ('closed',), R(1), R(2)),
        (('await', 'registered'), ('count',), ('closed',), ('signal', 'observed3'),
         ('await', 'two'), ('count',), ('signal', 'observed2'))),
        required_coverage=('observer.count.3', 'observer.count.2',
                           'observer.closed.open_and_joining', 'observer.closed.closed_and_joining')),
    's_multi': dict(nholders=3, initial_holders=(0, 1, 2), ncallbacks=3,
        inputs=((0,), (0,), (0,)), initial_callbacks=(0, 1, 2),
        bodies={c: (('holder_done', c, c),) for c in range(3)},
        threads=((J(), ('request', 0), ('wait_all',), DESTROY),),
        required_coverage=('request.select.0.0', 'request.select.1.0', 'request.select.2.0',
                           'deregister.self', 'join.inside_request', 'request.multiple_pending')),
    's_external_complete': dict(ncallbacks=1, inputs=((0,),), initial_callbacks=(0,),
        threads=((('request', 0), ('signal', 'requested')),
                 (('holder_done', 0, 0), ('await', 'requested'), ('wait_all',), DESTROY), (J(),)),
        required_coverage=('deregister.wait', 'deregister.external')),
    'review_inline_registration_lifetime': dict(ncallbacks=1, inputs=((0, 1),), initial_holders=(),
        stopped=(True, False), threads=((('construct', 0), J(), DESTROY, ('destroy_cb', 0)),),
        required_coverage=('register.inline', 'deregister.inline_only')),
})

# Isolate committed delivery and the stronger deadline specification control.
SCENARIOS['u_retained_deadline'] = dict(ncallbacks=1, inputs=((0, 1),), initial_callbacks=(0,),
    initial_holders=(), threads=((('request', 0),), (('request', 1),)))

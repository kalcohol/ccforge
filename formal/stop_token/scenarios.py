"""Scenario, model and mutant definitions shared by the drivers."""

LU = ("lu",)
REQ = ("req",)
SR = ("sr",)
DESTROY = ("destroy",)


def D(c):
    return ("dtor", c)


def K(c):
    return ("ctor", c)


SCEN = {
    # a. lock contention only
    "a_2thr_x3": dict(threads=[[LU] * 3, [LU] * 3]),
    "a_2thr_x6": dict(threads=[[LU] * 6, [LU] * 6]),
    "a_3thr_x2": dict(threads=[[LU] * 2] * 3),
    "a_3thr_x3": dict(threads=[[LU] * 3] * 3),
    "a_4thr_x1": dict(threads=[[LU]] * 4),
    "a_4thr_x2": dict(threads=[[LU] * 2] * 4),
    "a_4thr_x3": dict(threads=[[LU] * 3] * 4),
    "a_5thr_x1": dict(threads=[[LU]] * 5),
    # b. request_stop vs destructor on another thread (+ lock contender)
    "b_req_dtor_lu": dict(ncb=1, reg=(0,), threads=[[REQ], [D(0)], [LU]]),
    "b_req_dtor_lu2": dict(ncb=1, reg=(0,), threads=[[REQ], [D(0)], [LU, LU]]),
    "b_req_dtor_lu_lu": dict(ncb=1, reg=(0,), threads=[[REQ], [D(0)], [LU], [LU]]),
    # c. callback destroys its own inplace_stop_callback on the requester
    "c_self": dict(ncb=1, reg=(0,), bodies={0: [D(0)]}, threads=[[REQ], [LU]]),
    "c_self_lu_lu": dict(ncb=1, reg=(0,), bodies={0: [D(0)]}, threads=[[REQ], [LU], [LU]]),
    "c_self_plus_other": dict(ncb=2, reg=(0, 1), bodies={0: [D(0)]}, threads=[[REQ], [D(1)], [LU]]),
    "c_nested": dict(ncb=3, reg=(0, 1), bodies={0: [D(1), K(2), D(2), REQ]}, threads=[[REQ], [LU], [SR]]),
    # d. registration racing with request_stop
    "d_req_ctor_sr": dict(ncb=1, threads=[[REQ], [K(0), D(0)], [SR, SR]]),
    "d_req_ctor_lu": dict(ncb=1, threads=[[REQ], [K(0), D(0)], [LU]]),
    "d_req_ctor_ctor": dict(ncb=2, threads=[[REQ], [K(0), D(0)], [K(1), D(1)]]),
    "d_req_ctor_xdtor": dict(ncb=1, threads=[[REQ], [K(0)], [D(0)]]),
    # e. two concurrent request_stop
    "e_req_req_dtor": dict(ncb=1, reg=(0,), threads=[[REQ], [REQ], [D(0)]]),
    "e_req_req_sr": dict(ncb=1, reg=(0,), threads=[[REQ], [REQ], [SR, SR]]),
    "e_req_req_dtor_lu": dict(ncb=1, reg=(0,), threads=[[REQ], [REQ], [D(0)], [LU]]),
    # f. two callbacks: the invocation loop revisits the list
    "f_req_dtor_dtor": dict(ncb=2, reg=(0, 1), threads=[[REQ], [D(0)], [D(1)]]),
    "f_req_dtor_lu": dict(ncb=2, reg=(0, 1), threads=[[REQ], [D(0)], [LU]]),
    "f_req_dtor2_lu": dict(ncb=2, reg=(0, 1), threads=[[REQ], [D(1), D(0)], [LU]]),
    "f_body_kills_sibling": dict(ncb=2, reg=(0, 1), bodies={0: [D(1)]}, threads=[[REQ], [D(0)], [LU]]),
    "f_req_dtor_dtor_lu": dict(ncb=2, reg=(0, 1), threads=[[REQ], [D(0)], [D(1)], [LU]]),
    "f_3cb": dict(ncb=3, reg=(0, 1, 2), threads=[[REQ], [D(0)], [D(1), D(2)]]),
    # combined
    "x_req_req_ctor_dtor": dict(ncb=2, reg=(0,), threads=[[REQ], [REQ], [K(1), D(1)], [D(0)]]),
    "x_req_dtor_ctor_lu": dict(ncb=2, reg=(0,), threads=[[REQ], [D(0)], [K(1), D(1)], [LU]]),
    "x_self_dtor_ctor_lu": dict(ncb=3, reg=(0, 1), bodies={0: [D(0)]}, threads=[[REQ], [D(1)], [K(2), D(2)], [LU]]),
    "x5_req_dtor_ctor_lu_sr": dict(ncb=2, reg=(0,), threads=[[REQ], [D(0)], [K(1), D(1)], [LU], [SR]]),
    # g. strict-mode tombstone (precondition violation on purpose)
    "g_tombstone": dict(threads=[[LU], [LU], [DESTROY]], strict=True, precond_violation=True, check_inv=False),
    "g_tombstone_held": dict(
        threads=[[("lockhold",)], [LU], [DESTROY]], strict=True, precond_violation=True, check_inv=False
    ),
    "g_tombstone_held2": dict(
        threads=[[("lockhold",)], [LU], [LU], [DESTROY]], strict=True, precond_violation=True, check_inv=False
    ),
    "g_tombstone_req": dict(
        ncb=1, reg=(0,), threads=[[REQ], [LU], [DESTROY]], strict=True, precond_violation=True, check_inv=False
    ),
}

MODELS = {
    "sc_abs": dict(mem="sc", lib="abs"),
    "sc_abs_spur": dict(mem="sc", lib="abs", spurious=True),
    "sc_cnt": dict(mem="sc", lib="cnt_sc"),
    "x86_abs": dict(mem="x86", lib="abs"),
    "x86_cnt_sc": dict(mem="x86", lib="cnt_sc"),
    "x86_cnt_plain": dict(mem="x86", lib="cnt_plain"),
    "arm_cnt_sc": dict(mem="arm", lib="cnt_sc"),
    "arm_cnt_plain": dict(mem="arm", lib="cnt_plain"),  # negative control
    "arm_cnt_plain_rmwfence": dict(mem="arm", lib="cnt_plain", arm_rmw_early=False),
}


# scenarios restricted to a subset of models because of their size
HEAVY = {
    "x5_req_dtor_ctor_lu_sr": ("sc_abs", "x86_cnt_sc"),
    "a_4thr_x3": ("sc_abs", "sc_abs_spur", "x86_cnt_sc", "arm_cnt_sc"),
}

# ======================================================================= Model additions
LUx = LU
NEW_SCEN = {
    # self-destruction from inside the constructor's inline invocation (or, if
    # registration wins the race, from inside the requester's invocation)
    "s_inline_self": dict(ncb=1, bodies={0: [D(0)]}, threads=[[REQ], [K(0)], [LU]]),
    # the callback body itself takes the source lock while another thread awaits it
    "s_body_lu_await": dict(ncb=1, reg=(0,), bodies={0: [LU]}, threads=[[REQ], [D(0)], [LU]]),
    # an inline invocation (constructor after stop) destroys a pre-registered callback
    # that the requester may be executing: awaited path inside an inline invocation
    "s_inline_kills_reg": dict(ncb=2, reg=(0,), bodies={1: [D(0)]}, threads=[[REQ], [K(1), D(1)], [LU]]),
    # nested inline invocation on the requester destroys the executing callback
    "s_nested_inline_kills_exec": dict(ncb=2, reg=(0,), bodies={0: [K(1), D(1)], 1: [D(0)]}, threads=[[REQ], [LU]]),
    # two requesters, the winner's callback destroys itself
    "s_req_req_self": dict(ncb=1, reg=(0,), bodies={0: [D(0)]}, threads=[[REQ], [REQ], [LU]]),
    # deeper lock relay
    "a_3thr_x4": dict(threads=[[LU] * 4] * 3),
    "a_2thr_x8": dict(threads=[[LU] * 8] * 2),
}
SCEN.update(NEW_SCEN)

NEW_MODELS = {
    # libstdc++ 16 structure: load+compare,
    # spin, count++ , futex, count--, caller reload; notify: count load, futex wake.
    "sc_cnt16": dict(mem="sc", lib="cnt16_sc"),
    "x86_cnt16": dict(mem="x86", lib="cnt16_sc"),
    "arm_cnt16": dict(mem="arm", lib="cnt16_sc"),
    "arm_cnt16_plain": dict(mem="arm", lib="cnt16_plain"),  # negative control
    "arm_cnt16_plain_rmwfence": dict(mem="arm", lib="cnt16_plain", arm_rmw_early=False),
    # coherence-only (stale) relaxed/acquire loads of state_
    "sc_abs_stale": dict(mem="sc", lib="abs", stale=True),
    "sc_cnt16_stale": dict(mem="sc", lib="cnt16_sc", stale=True),
    "sc_abs_spur_stale": dict(mem="sc", lib="abs", stale=True, spurious=True),
}
MODELS.update(NEW_MODELS)


# Model: happens-before tracking (SC interleavings, vector clocks, ghost plain locations)
MODELS.update(
    {
        "sc_abs_hb": dict(mem="sc", lib="abs", hb=True),
        "sc_cnt16_hb": dict(mem="sc", lib="cnt16_sc", hb=True),
    }
)

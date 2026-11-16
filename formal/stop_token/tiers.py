"""Configuration matrices and explicit quick-tier manifest."""

from .scenarios import SCEN, MODELS, HEAVY, NEW_SCEN
from .mutants import MUTANTS, NEW_MUTANTS, HB_MUTANTS

ORIG_BASE_MODELS = ("sc_abs", "sc_abs_spur", "sc_cnt", "x86_abs", "x86_cnt_sc", "x86_cnt_plain", "arm_cnt_sc")
ORIG_SCEN = [s for s in SCEN if s not in NEW_SCEN]
MUT_SCEN = [
    "a_2thr_x3",
    "a_3thr_x2",
    "b_req_dtor_lu",
    "c_self",
    "c_self_plus_other",
    "d_req_ctor_lu",
    "d_req_ctor_sr",
    "e_req_req_dtor",
    "e_req_req_sr",
    "f_req_dtor_lu",
    "f_body_kills_sibling",
]
NEW_MUT_SCEN = MUT_SCEN + [
    "d_req_ctor_xdtor",
    "f_req_dtor_dtor",
    "s_inline_self",
    "s_body_lu_await",
    "s_inline_kills_reg",
    "s_nested_inline_kills_exec",
    "s_req_req_self",
]
SMALL = [
    "a_2thr_x3",
    "a_2thr_x6",
    "a_3thr_x2",
    "a_4thr_x1",
    "b_req_dtor_lu",
    "b_req_dtor_lu2",
    "c_self",
    "c_self_lu_lu",
    "c_self_plus_other",
    "c_nested",
    "d_req_ctor_sr",
    "d_req_ctor_lu",
    "d_req_ctor_xdtor",
    "e_req_req_dtor",
    "e_req_req_sr",
    "f_req_dtor_dtor",
    "f_req_dtor_lu",
    "f_req_dtor2_lu",
    "f_body_kills_sibling",
    "f_3cb",
    "g_tombstone",
    "g_tombstone_held",
    "g_tombstone_held2",
    "g_tombstone_req",
    "s_inline_self",
    "s_body_lu_await",
    "s_inline_kills_reg",
    "s_nested_inline_kills_exec",
    "s_req_req_self",
]


def T(kind, scen, model, Y, **kw):
    name = kw.pop("name", None) or "%s.%s.%s.Y%d" % (kind, scen, model, Y)
    d = dict(kind=kind, scen=scen, model=model, Y=Y, name=name)
    d.update(kw)
    return d


def tasks_regress():
    ts = []
    for s in (
        "a_2thr_x3",
        "a_3thr_x2",
        "b_req_dtor_lu",
        "c_self",
        "c_nested",
        "d_req_ctor_lu",
        "e_req_req_dtor",
        "f_req_dtor_lu",
        "g_tombstone_held2",
        "x_req_dtor_ctor_lu",
    ):
        for m in ORIG_BASE_MODELS:
            for Y in (0, 1):
                ts.append(T("regress", s, m, Y, name="regress.%s.%s.Y%d" % (s, m, Y)))
    return ts


def tasks_newscen():
    ts = []
    for s in NEW_SCEN:
        for m in ORIG_BASE_MODELS + ("sc_cnt16", "x86_cnt16", "arm_cnt16"):
            for Y in (0, 1):
                if s in ("a_3thr_x4", "a_2thr_x8") and m not in ("sc_abs", "sc_abs_spur", "x86_cnt_sc", "sc_cnt16"):
                    continue
                ts.append(T("newscen", s, m, Y))
        ts.append(T("newscen", s, "sc_abs", 4, name="newscen.%s.sc_abs.Y4" % s))
    return ts


def tasks_cnt16():
    ts = []
    for s in ORIG_SCEN:
        for m in ("sc_cnt16", "x86_cnt16", "arm_cnt16"):
            if s in HEAVY and m != "x86_cnt16":
                continue
            for Y in (0, 1):
                ts.append(T("cnt16", s, m, Y))
    for s in ("a_2thr_x3", "a_3thr_x2", "b_req_dtor_lu", "c_self", "d_req_ctor_lu", "e_req_req_dtor", "f_req_dtor_lu"):
        for m in ("arm_cnt16_plain", "arm_cnt16_plain_rmwfence"):
            ts.append(T("ctl16", s, m, 1, stop=True))
    return ts


def tasks_stale():
    ts = []
    for s in SMALL:
        for m in ("sc_abs_stale", "sc_cnt16_stale", "sc_abs_spur_stale"):
            for Y in (0, 1):
                ts.append(T("stale", s, m, Y, max=6_000_000))
    return ts


def tasks_newmut():
    ts = []
    for m, _d in NEW_MUTANTS:
        for s in NEW_MUT_SCEN:
            for mod in ("sc_abs", "x86_cnt_sc", "sc_cnt16"):
                for Y in (0, 1):
                    ts.append(
                        T("newmut", s, mod, Y, mut=m, muts=[m], stop=True, name="newmut.%s.%s.%s.Y%d" % (m, s, mod, Y))
                    )
            for Y in (0, 1):
                ts.append(
                    T(
                        "newmut_noinv",
                        s,
                        "sc_abs",
                        Y,
                        mut=m,
                        muts=[m],
                        stop=True,
                        extra=dict(check_inv=False),
                        name="newmutnoinv.%s.%s.sc_abs.Y%d" % (m, s, Y),
                    )
                )
    return ts


def tasks_oldmut16():
    """Original mutants under the libstdc++ 16 model and under stale reads."""
    ts = []
    for m, _d in MUTANTS:
        if m.startswith("Mg"):
            continue
        for s in MUT_SCEN:
            for mod in ("sc_cnt16", "x86_cnt16", "arm_cnt16", "sc_abs_stale"):
                for Y in (0, 1):
                    ts.append(
                        T(
                            "oldmut16",
                            s,
                            mod,
                            Y,
                            mut=m,
                            muts=[m],
                            stop=True,
                            name="oldmut16.%s.%s.%s.Y%d" % (m, s, mod, Y),
                        )
                    )
    return ts


def tasks_extra():
    ts = []
    for s in SMALL:
        if s.startswith("g_"):
            continue
        ts.append(T("extra", s, "sc_cnt16", 1, extra=dict(spurious=True), name="extra.%s.sc_cnt16_spur.Y1" % s))
        ts.append(T("extra", s, "sc_abs", 1, extra=dict(strict=True), name="extra.%s.sc_abs_strict.Y1" % s))
        ts.append(T("extra", s, "sc_cnt16", 0, extra=dict(strict=True), name="extra.%s.sc_cnt16_strict.Y0" % s))
    for s in ("a_2thr_x3", "a_3thr_x2", "b_req_dtor_lu", "c_self_lu_lu", "f_req_dtor_lu", "e_req_req_dtor"):
        ts.append(T("extra", s, "sc_abs_stale", 4, name="extra.%s.sc_abs_stale.Y4" % s, max=6_000_000))
        ts.append(T("extra", s, "sc_cnt16_stale", 4, name="extra.%s.sc_cnt16_stale.Y4" % s, max=6_000_000))
    return ts


def tasks_hb():
    ts = []
    big = [
        "b_req_dtor_lu_lu",
        "c_self_lu_lu",
        "d_req_ctor_ctor",
        "e_req_req_dtor_lu",
        "f_req_dtor_dtor_lu",
        "x_req_dtor_ctor_lu",
        "x_self_dtor_ctor_lu",
        "x_req_req_ctor_dtor",
        "a_3thr_x3",
        "a_4thr_x2",
    ]
    for s in [x for x in SMALL if not x.startswith("g_")] + big:
        for m in ("sc_abs_hb", "sc_cnt16_hb"):
            for Y in (0, 1):
                if s in big and (m != "sc_abs_hb" or Y != 1):
                    continue
                ts.append(T("hb", s, m, Y, max=8_000_000))
    for mu, _d in HB_MUTANTS:
        for s in NEW_MUT_SCEN + ["d_req_ctor_ctor", "x5_req_dtor_ctor_lu_sr"]:
            for Y in (0, 1):
                if s == "x5_req_dtor_ctor_lu_sr" and Y == 0:
                    continue
                ts.append(
                    T(
                        "hbmut",
                        s,
                        "sc_abs_hb",
                        Y,
                        mut=mu,
                        muts=[mu],
                        stop=True,
                        name="hbmut.%s.%s.sc_abs_hb.Y%d" % (mu, s, Y),
                        max=8_000_000,
                    )
                )
    return ts


SETS = {
    "hb": tasks_hb,
    "extra": tasks_extra,
    "regress": tasks_regress,
    "newscen": tasks_newscen,
    "cnt16": tasks_cnt16,
    "stale": tasks_stale,
    "newmut": tasks_newmut,
    "oldmut16": tasks_oldmut16,
}


def baseline_tasks():
    ts = []
    for s in ORIG_SCEN:
        g = s.startswith("g_")
        for Y in (0, 1):
            for m in ("sc_abs", "sc_abs_spur", "sc_cnt", "x86_abs", "x86_cnt_sc", "x86_cnt_plain", "arm_cnt_sc"):
                if s in HEAVY and m not in HEAVY[s]:
                    continue
                ts.append(dict(kind="baseline", scen=s, model=m, Y=Y, name="base.%s.%s.Y%d" % (s, m, Y)))
    for s in ("a_3thr_x2", "b_req_dtor_lu", "f_req_dtor_lu"):
        ts.append(dict(kind="baseline", scen=s, model="sc_abs", Y=2, name="base.%s.sc_abs.Y2" % s))
        ts.append(
            dict(
                kind="baseline",
                scen=s,
                model="sc_abs",
                Y=1,
                extra=dict(elig="any"),
                name="base.%s.sc_abs_eligany.Y1" % s,
            )
        )
        ts.append(
            dict(
                kind="baseline",
                scen=s,
                model="sc_abs",
                Y=1,
                extra=dict(weakfail=True),
                name="base.%s.sc_abs_weakfail.Y1" % s,
            )
        )
    for s in ("b_req_dtor_lu", "f_req_dtor_lu", "e_req_req_dtor"):
        ts.append(
            dict(
                kind="baseline",
                scen=s,
                model="sc_abs",
                Y=1,
                extra=dict(strict=True),
                name="base.%s.sc_abs_strict.Y1" % s,
            )
        )
    # negative controls: plain-load notify fast path under the ARM-like mapping
    for s in ("a_2thr_x3", "a_3thr_x2", "b_req_dtor_lu", "c_self", "d_req_ctor_lu", "e_req_req_dtor", "f_req_dtor_lu"):
        for m in ("arm_cnt_plain", "arm_cnt_plain_rmwfence"):
            ts.append(dict(kind="control", scen=s, model=m, Y=1, stop=True, name="ctl.%s.%s.Y1" % (s, m)))
    return ts


MUT_SCEN = [
    "a_2thr_x3",
    "a_3thr_x2",
    "b_req_dtor_lu",
    "c_self",
    "c_self_plus_other",
    "d_req_ctor_lu",
    "d_req_ctor_sr",
    "e_req_req_dtor",
    "e_req_req_sr",
    "f_req_dtor_lu",
    "f_body_kills_sibling",
]
MUT_MODELS = ["sc_abs", "sc_cnt", "x86_cnt_sc", "x86_cnt_plain", "arm_cnt_sc", "x86_abs"]


def mutant_tasks():
    ts = []
    for m, _d in MUTANTS:
        if m.startswith("Mg"):
            for s in ("g_tombstone", "g_tombstone_req", "g_tombstone_held", "g_tombstone_held2"):
                for Y in (0, 1):
                    ts.append(
                        dict(
                            kind="mutant",
                            mut=m,
                            scen=s,
                            model="sc_abs",
                            Y=Y,
                            muts=[m],
                            stop=True,
                            name="mut.%s.%s.sc_abs.Y%d" % (m, s, Y),
                        )
                    )
            continue
        for s in MUT_SCEN:
            for mod in MUT_MODELS:
                for Y in (0, 1):
                    ts.append(
                        dict(
                            kind="mutant",
                            mut=m,
                            scen=s,
                            model=mod,
                            Y=Y,
                            muts=[m],
                            stop=True,
                            name="mut.%s.%s.%s.Y%d" % (m, s, mod, Y),
                        )
                    )
        if m == "M5_no_final_handoff":
            ts.append(
                dict(
                    kind="mutant",
                    mut=m,
                    scen="b_req_dtor_lu",
                    model="sc_abs",
                    Y=1,
                    muts=[m],
                    stop=True,
                    extra=dict(strict=True),
                    name="mut.%s.b_req_dtor_lu.sc_abs_strict.Y1" % m,
                )
            )
        for s in MUT_SCEN:
            for mod in ("sc_abs", "x86_cnt_sc"):
                for Y in (0, 1):
                    ts.append(
                        dict(
                            kind="mutant_noinv",
                            mut=m,
                            scen=s,
                            model=mod,
                            Y=Y,
                            muts=[m],
                            stop=True,
                            extra=dict(check_inv=False),
                            name="mutnoinv.%s.%s.%s.Y%d" % (m, s, mod, Y),
                        )
                    )
    return ts


def configuration(task):
    from .model import Cfg

    kw = dict(SCEN[task["scen"]])
    kw.update(MODELS[task["model"]])
    kw.update(task.get("extra", {}))
    kw.update(Y=task["Y"], muts=frozenset(task.get("muts", ())), name=task["name"])
    return Cfg(**kw)


def identity(task):
    cfg = dict(configuration(task).__dict__)
    cfg.pop("name")
    cfg["muts"] = sorted(cfg["muts"])
    return dict(cfg=cfg, stop=task.get("stop", False), max_states=task.get("max", 8000000))


def full():
    import json

    seen, result = set(), []
    tasks = baseline_tasks() + mutant_tasks() + [t for fn in SETS.values() for t in fn()]
    for task in tasks:
        encoded = json.dumps(identity(task), sort_keys=True)
        if encoded not in seen:
            seen.add(encoded)
            result.append(task)
    return result


def select(tier):
    tasks = full()
    return tasks if tier == "full" else [t for t in tasks if t["name"] in QUICK]


QUICK = frozenset(
    [
        "base.a_2thr_x3.sc_abs.Y0",
        "base.a_2thr_x6.sc_abs.Y0",
        "base.a_3thr_x2.sc_abs.Y0",
        "base.a_3thr_x3.sc_abs.Y0",
        "base.a_4thr_x1.x86_abs.Y0",
        "base.a_4thr_x2.sc_abs.Y0",
        "base.a_4thr_x3.sc_abs.Y0",
        "base.a_5thr_x1.sc_abs.Y0",
        "base.b_req_dtor_lu.sc_abs.Y0",
        "base.b_req_dtor_lu2.sc_abs.Y0",
        "base.b_req_dtor_lu_lu.sc_abs.Y0",
        "base.c_nested.sc_abs.Y0",
        "base.c_self.sc_abs.Y0",
        "base.c_self_lu_lu.x86_abs.Y0",
        "base.c_self_plus_other.sc_abs.Y0",
        "base.d_req_ctor_ctor.sc_abs.Y0",
        "base.d_req_ctor_lu.sc_abs.Y0",
        "base.d_req_ctor_sr.sc_abs.Y0",
        "base.d_req_ctor_xdtor.sc_abs.Y0",
        "base.e_req_req_dtor.x86_abs.Y0",
        "base.e_req_req_dtor_lu.sc_abs.Y0",
        "base.e_req_req_sr.sc_abs.Y0",
        "base.f_3cb.sc_abs.Y0",
        "base.f_body_kills_sibling.x86_abs.Y0",
        "base.f_req_dtor2_lu.sc_abs.Y0",
        "base.f_req_dtor_dtor.sc_abs.Y0",
        "base.f_req_dtor_dtor_lu.x86_abs.Y0",
        "base.f_req_dtor_lu.sc_abs.Y0",
        "base.g_tombstone.sc_abs.Y0",
        "base.g_tombstone_held.arm_cnt_sc.Y0",
        "base.g_tombstone_held.sc_abs.Y0",
        "base.g_tombstone_held.sc_abs_spur.Y0",
        "base.g_tombstone_held.sc_cnt.Y0",
        "base.g_tombstone_held.x86_abs.Y0",
        "base.g_tombstone_held.x86_cnt_plain.Y0",
        "base.g_tombstone_held.x86_cnt_sc.Y0",
        "base.g_tombstone_held2.sc_abs.Y0",
        "base.g_tombstone_req.sc_abs.Y0",
        "base.x5_req_dtor_ctor_lu_sr.sc_abs.Y0",
        "base.x_req_dtor_ctor_lu.x86_abs.Y0",
        "base.x_req_req_ctor_dtor.x86_abs.Y0",
        "base.x_self_dtor_ctor_lu.x86_abs.Y0",
        "cnt16.g_tombstone_held.arm_cnt16.Y0",
        "cnt16.g_tombstone_held.sc_cnt16.Y0",
        "cnt16.g_tombstone_held.x86_cnt16.Y0",
        "ctl.b_req_dtor_lu.arm_cnt_plain_rmwfence.Y1",
        "ctl.c_self.arm_cnt_plain.Y1",
        "ctl.c_self.arm_cnt_plain_rmwfence.Y1",
        "ctl16.b_req_dtor_lu.arm_cnt16_plain_rmwfence.Y1",
        "ctl16.c_self.arm_cnt16_plain.Y1",
        "ctl16.c_self.arm_cnt16_plain_rmwfence.Y1",
        "hb.c_self.sc_abs_hb.Y0",
        "hb.c_self.sc_cnt16_hb.Y0",
        "hbmut.H1_lock_cas_relaxed.d_req_ctor_xdtor.sc_abs_hb.Y0",
        "hbmut.H2_unlock_exchange_relaxed.d_req_ctor_xdtor.sc_abs_hb.Y0",
        "hbmut.H3_stop_fetch_or_relaxed.e_req_req_sr.sc_abs_hb.Y0",
        "hbmut.H4_completion_load_relaxed.c_self.sc_abs_hb.Y0",
        "hbmut.H5_completion_store_relaxed.c_self.sc_abs_hb.Y0",
        "hbmut.H6_stop_requested_relaxed.e_req_req_sr.sc_abs_hb.Y0",
        "hbmut.H7_unlock_exchange_acquire_only.d_req_ctor_xdtor.sc_abs_hb.Y0",
        "mut.M10_stop_bit_plain_store.c_self.sc_abs.Y0",
        "mut.M11_no_completion_wait.d_req_ctor_sr.sc_abs.Y0",
        "mut.M12_no_loser_check.e_req_req_sr.sc_abs.Y0",
        "mut.M14_notify_before_release.c_self.sc_abs.Y0",
        "mut.M15_wait_without_bit_in_expected.c_self.sc_abs.Y0",
        "mut.M17_no_same_thread_check.c_self.sc_abs.Y0",
        "mut.M18_no_completion_notify.d_req_ctor_sr.sc_abs.Y0",
        "mut.M1_notify_one.b_req_dtor_lu.sc_abs.Y0",
        "mut.M2_wait_unarmed_after_failed_cas.c_self.sc_abs.Y0",
        "mut.M3_notify_from_stale_load.c_self.sc_abs.Y0",
        "mut.M4_completion_store_release.d_req_ctor_sr.x86_cnt_plain.Y0",
        "mut.M5_no_final_handoff.d_req_ctor_sr.sc_abs.Y0",
        "mut.M6_completion_notify_after_unlock.d_req_ctor_sr.sc_abs.Y0",
        "mut.M7_touch_node_after_self_destruction.c_self.sc_abs.Y0",
        "mut.M8_arm_fetch_or.a_2thr_x3.arm_cnt_sc.Y0",
        "mut.M8b_no_unlocked_return_in_yield.a_2thr_x3.arm_cnt_sc.Y1",
        "mut.M9_forget_stop_bit_in_known.c_self.arm_cnt_sc.Y0",
        "mut.Mg_destructor_no_notify.g_tombstone_held.sc_abs.Y0",
        "newmut.M19_awaited_after_unlock.d_req_ctor_xdtor.sc_abs.Y0",
        "newmut.M20_status_read_before_relock.d_req_ctor_xdtor.sc_abs.Y0",
        "newmut.M21_status_not_cleared.d_req_ctor_xdtor.sc_abs.Y0",
        "newmut.M22_completion_notify_before_store.d_req_ctor_xdtor.sc_abs.Y0",
        "newmut.M23_status_after_unlock.d_req_ctor_xdtor.sc_abs.Y0",
        "newmut.M25_handoff_before_wait.d_req_ctor_xdtor.sc_abs.Y0",
        "newmut.M28_completed_check_before_pending.d_req_ctor_xdtor.sc_abs.Y0",
        "newmut.M29_add_ignores_stop_bit.d_req_ctor_xdtor.sc_abs.Y0",
        "newmut.M30_invoke_under_lock.c_self.sc_abs.Y0",
        "newmut.M37_wait_on_reload_after_arming.c_self.sc_abs.Y0",
        "newmut.M41_unlink_after_invoke.c_self.sc_abs.Y0",
        "newscen.a_2thr_x8.sc_abs.Y0",
        "newscen.a_3thr_x4.sc_abs.Y0",
        "newscen.s_body_lu_await.sc_abs.Y0",
        "newscen.s_inline_kills_reg.sc_abs.Y0",
        "newscen.s_inline_self.sc_abs.Y0",
        "newscen.s_nested_inline_kills_exec.sc_abs.Y0",
        "newscen.s_req_req_self.x86_abs.Y0",
        "stale.g_tombstone_held.sc_abs_spur_stale.Y0",
        "stale.g_tombstone_held.sc_abs_stale.Y0",
        "stale.g_tombstone_held.sc_cnt16_stale.Y0",
    ]
)

"""Mutation operators and declared sensitivity expectations."""

CATALOG = {
    "H1_lock_cas_relaxed": {
        "description": "lock(): compare_exchange_weak success order relaxed instead of acquire ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "H2_unlock_exchange_relaxed": {
        "description": "unlock(): exchange relaxed instead of seq_cst ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "H3_stop_fetch_or_relaxed": {
        "description": "request_stop(): fetch_or(kStopRequested) relaxed instead of release ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "H4_completion_load_relaxed": {
        "description": "remove_callback(): completion load and wait relaxed instead of acquire ",
        "outcome": "survives by design",
        "reason": "The final source-lock handoff orders invocation before reclamation.",
    },
    "H5_completion_store_relaxed": {
        "description": "request_stop(): completion.store relaxed instead of seq_cst ",
        "outcome": "survives by design",
        "reason": "The final source-lock handoff orders invocation before reclamation.",
    },
    "H6_stop_requested_relaxed": {
        "description": "stop_requested(): load relaxed instead of acquire ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "H7_unlock_exchange_acquire_only": {
        "description": "unlock(): exchange acquire instead of seq_cst, i.e. no release ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M10_stop_bit_plain_store": {
        "description": "request_stop: stop bit written with a non-RMW store ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M11_no_completion_wait": {
        "description": "remove_callback: skip the completion wait ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M12_no_loser_check": {
        "description": "request_stop: drop the locked re-check of the stop bit ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M14_notify_before_release": {
        "description": "unlock: notify_all before the releasing exchange ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M15_wait_without_bit_in_expected": {
        "description": "wait_for_unlock: omit state |= kLockWaiters before wait ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M17_no_same_thread_check": {
        "description": "remove_callback: no request_thread comparison ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M18_no_completion_notify": {
        "description": "request_stop: completion stored but never notified ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M19_awaited_after_unlock": {
        "description": "record awaited after unlock instead of under the lock; "
        "inplace_stop_source::remove_callback | unlock(stop_site::remove_wait, state); ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M1_notify_one": {
        "description": "unlock: notify_one instead of notify_all ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M20_status_read_before_relock": {
        "description": "request_stop: read the invocation status before lock() at the source operation ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M21_status_not_cleared": {
        "description": "request_stop: omit node->status = nullptr ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M22_completion_notify_before_store": {
        "description": "request_stop: completion.notify_all before completion.store ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M23_status_after_unlock": {
        "description": "request_stop: publish node->status / request_thread after the unlock at the source operation ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M25_handoff_before_wait": {
        "description": "remove_callback: final lock/unlock handoff before the completion wait ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M28_completed_check_before_pending": {
        "description": "remove_callback: test status == nullptr before prev ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M29_add_ignores_stop_bit": {
        "description": "try_add_callback: link the node although the stop bit is set ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M2_wait_unarmed_after_failed_cas": {
        "description": "wait_for_unlock: wait on the reloaded word after a failed waiter-bit CAS ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M30_invoke_under_lock": {
        "description": "request_stop: invoke the callback while holding the source lock ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M37_wait_on_reload_after_arming": {
        "description": "wait_for_unlock: after arming, wait on a freshly loaded word instead of the armed value ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M3_notify_from_stale_load": {
        "description": "unlock: wake decision from a separate load before the exchange ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M41_unlink_after_invoke": {
        "description": "request_stop: unlink the node after the invocation instead of before ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M4_completion_store_release": {
        "description": "request_stop: completion.store(1, release) ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M5_no_final_handoff": {
        "description": "remove_callback: drop the final lock/unlock handoff ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M6_completion_notify_after_unlock": {
        "description": "request_stop: completion.notify_all issued after the unlock ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M7_touch_node_after_self_destruction": {
        "description": "request_stop: touch node although destroyed_on_requester ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M8_arm_fetch_or": {
        "description": "wait_for_unlock: waiter bit set even when the word is unlocked/changed ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M8b_no_unlocked_return_in_yield": {
        "description": "wait_for_unlock: drop the early return on an "
        "unlocked re-read, so the waiter-bit CAS can "
        "succeed on an unlocked word ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "M9_forget_stop_bit_in_known": {
        "description": "request_stop: omit state |= kStopRequested ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
    "Mg_destructor_no_notify": {
        "description": "strict ~inplace_stop_source: no notify_all after the tombstone ",
        "outcome": "killed",
        "reason": "At least one bounded configuration must expose the changed protocol.",
    },
}

CONTROLS = {
    "ctl.a_2thr_x3.arm_cnt_plain.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl.a_2thr_x3.arm_cnt_plain_rmwfence.Y1": {
        "fail": False,
        "reason": "The modeled RMW fence prevents the missed wake on this bounded scenario.",
    },
    "ctl.a_3thr_x2.arm_cnt_plain.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl.a_3thr_x2.arm_cnt_plain_rmwfence.Y1": {
        "fail": False,
        "reason": "The modeled RMW fence prevents the missed wake on this bounded scenario.",
    },
    "ctl.b_req_dtor_lu.arm_cnt_plain.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl.b_req_dtor_lu.arm_cnt_plain_rmwfence.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl.c_self.arm_cnt_plain.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl.c_self.arm_cnt_plain_rmwfence.Y1": {
        "fail": False,
        "reason": "The modeled RMW fence prevents the missed wake on this bounded scenario.",
    },
    "ctl.d_req_ctor_lu.arm_cnt_plain.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl.d_req_ctor_lu.arm_cnt_plain_rmwfence.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl.e_req_req_dtor.arm_cnt_plain.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl.e_req_req_dtor.arm_cnt_plain_rmwfence.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl.f_req_dtor_lu.arm_cnt_plain.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl.f_req_dtor_lu.arm_cnt_plain_rmwfence.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl16.a_2thr_x3.arm_cnt16_plain.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl16.a_2thr_x3.arm_cnt16_plain_rmwfence.Y1": {
        "fail": False,
        "reason": "The modeled RMW fence prevents the missed wake on this bounded scenario.",
    },
    "ctl16.a_3thr_x2.arm_cnt16_plain.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl16.a_3thr_x2.arm_cnt16_plain_rmwfence.Y1": {
        "fail": False,
        "reason": "The modeled RMW fence prevents the missed wake on this bounded scenario.",
    },
    "ctl16.b_req_dtor_lu.arm_cnt16_plain.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl16.b_req_dtor_lu.arm_cnt16_plain_rmwfence.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl16.c_self.arm_cnt16_plain.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl16.c_self.arm_cnt16_plain_rmwfence.Y1": {
        "fail": False,
        "reason": "The modeled RMW fence prevents the missed wake on this bounded scenario.",
    },
    "ctl16.d_req_ctor_lu.arm_cnt16_plain.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl16.d_req_ctor_lu.arm_cnt16_plain_rmwfence.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl16.e_req_req_dtor.arm_cnt16_plain.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl16.e_req_req_dtor.arm_cnt16_plain_rmwfence.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl16.f_req_dtor_lu.arm_cnt16_plain.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
    "ctl16.f_req_dtor_lu.arm_cnt16_plain_rmwfence.Y1": {
        "fail": True,
        "reason": "A plain waiter-count check permits a missed wake on the ARM-like mapping.",
    },
}

# The sensitivity manifest is independent of expected-result updates.
from pathlib import Path


def failure_manifest():
    lines = (Path(__file__).with_name("sensitivity.txt")).read_text().splitlines()
    names = [line for line in lines if line and not line.startswith("#")]
    if names != sorted(set(names)):
        raise ValueError("sensitivity manifest must contain unique sorted names")
    return frozenset(names)


FAIL_CONFIGURATIONS = failure_manifest()


def declared(task):
    expected = task["name"] in FAIL_CONFIGURATIONS
    if task["name"] in CONTROLS and CONTROLS[task["name"]]["fail"] != expected:
        raise ValueError("control contradicts sensitivity manifest: " + task["name"])
    return expected


MUTANTS = [
    (mid, CATALOG[mid]["description"])
    for mid in (
        "M1_notify_one",
        "M2_wait_unarmed_after_failed_cas",
        "M3_notify_from_stale_load",
        "M4_completion_store_release",
        "M5_no_final_handoff",
        "M6_completion_notify_after_unlock",
        "M7_touch_node_after_self_destruction",
        "M8_arm_fetch_or",
        "M8b_no_unlocked_return_in_yield",
        "M9_forget_stop_bit_in_known",
        "M10_stop_bit_plain_store",
        "M11_no_completion_wait",
        "M12_no_loser_check",
        "M14_notify_before_release",
        "M15_wait_without_bit_in_expected",
        "M17_no_same_thread_check",
        "M18_no_completion_notify",
        "Mg_destructor_no_notify",
    )
]
NEW_MUTANTS = [
    (mid, CATALOG[mid]["description"])
    for mid in (
        "M19_awaited_after_unlock",
        "M20_status_read_before_relock",
        "M21_status_not_cleared",
        "M22_completion_notify_before_store",
        "M23_status_after_unlock",
        "M25_handoff_before_wait",
        "M28_completed_check_before_pending",
        "M29_add_ignores_stop_bit",
        "M30_invoke_under_lock",
        "M37_wait_on_reload_after_arming",
        "M41_unlink_after_invoke",
    )
]
HB_MUTANTS = [
    (mid, CATALOG[mid]["description"])
    for mid in (
        "H1_lock_cas_relaxed",
        "H2_unlock_exchange_relaxed",
        "H7_unlock_exchange_acquire_only",
        "H3_stop_fetch_or_relaxed",
        "H6_stop_requested_relaxed",
        "H4_completion_load_relaxed",
        "H5_completion_store_relaxed",
    )
]

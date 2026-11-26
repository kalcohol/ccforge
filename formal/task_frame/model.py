"""Finite interleavings of task frame ownership and terminal delivery."""
from dataclasses import dataclass, replace
from types import SimpleNamespace

STEPS = {
    k: SimpleNamespace(lines=reference, desc=description)
    for k, (reference, description) in {
        "call": ("model-only: promise_type::get_return_object in task.hpp",
                 "the coroutine call returns the task that owns the suspended frame"),
        "move": ("__frame_owner::__release | return std::exchange(__handle_, {});",
                 "task move construction exchanges the handle out of its source"),
        "drop_source": ("__frame_owner::~__frame_owner | __destroy();",
                        "the moved-from task is destroyed"),
        "drop_task": ("__frame_owner::__destroy | if (auto __frame = __release()) {",
                      "task destruction releases a frame it still owns"),
        "connect": ("model-only; call site __frame_owner::__release | return std::exchange(__handle_, {});",
                    "connect exchanges the handle into the state's base before any member"),
        "construct": ("model-only: task::__state constructor in task.hpp",
                      "receiver, own environment and Environment are constructed in order"),
        "unwind": ("model-only: a throwing member initializer of task::__state in task.hpp",
                   "constructed members unwind and the base owner releases the frame"),
        "drop_operation": ("__frame_owner::__destroy | __frame.destroy();",
                           "state destruction destroys a frame it still owns, then its members"),
        "associate": ("model-only: task::__state::start in task.hpp",
                      "start associates the state, the receiver and the scheduler"),
        "resume": ("__terminal_core::__resume | __frame_.__get().resume();",
                   "start reads the handle and resumes the body directly"),
        "final": ("__final_awaiter::await_suspend | Core* const __core = __core_;",
                  "the body reached final suspension; the awaiter is read while the frame lives"),
        "stage": ("__error_awaiter::await_suspend | Payload __staged(std::move(__payload_));",
                  "the second staging move takes the payload to the await_suspend stack"),
        "rethrow": ("model-only: [expr.await] resumes the coroutine with the await_suspend exception",
                    "a throwing staging move leaves the frame alive and rethrows in the body"),
        "hook": ("model-only: promise_type::unhandled_stopped in task.hpp",
                 "the stopped hook reads the promise while the frame lives"),
        "exchange": ("__terminal_core::__retire_frame | auto __frame = __frame_.__release();",
                     "the terminal exchanges the handle out of the operation"),
        "destroy": ("__terminal_core::__retire_frame | __frame.destroy();",
                    "the terminal destroys the frame and everything suspended in it"),
        "convert": ("__terminal_core::__complete_error | Cerr(std::move(__staged))",
                    "the staged payload converts on the stack after the frame is gone"),
        "capture": ("__terminal_core::__complete_error | __failure = std::current_exception();",
                    "a conversion failure is captured for the exception_ptr channel"),
        "terminate": ("__terminal_core::__complete_error | std::terminate();",
                      "a conversion failure without an exception_ptr channel terminates"),
        "deliver_result": ("__terminal_core::__complete_result | __deliver_result(); || "
                           "::__send_result | std::execution::set_value(std::move(__rcvr), __data.__result.__take());",
                           "value: the operation is read and the receiver entered with the stored result"),
        "deliver_stored_exception": ("__terminal_core::__complete_result | __deliver_result(); || "
                                     "::__send_result | std::execution::set_error(std::move(__rcvr), std::move(__stored));",
                                     "a body exception or an uncaught staging failure, stored by "
                                     "unhandled_exception: the receiver is entered through the result path"),
        "deliver_stopped": ("__terminal_core::__complete_stopped | __deliver_stopped();",
                            "stopped: the operation is read and the receiver entered"),
        "deliver_error": ("__terminal_core::__complete_error | "
                          "__forward_error<Cerr>(Cerr(std::move(__staged)), __entered); || "
                          "__terminal_core::__forward_error | __deliver_error(__error_position<Cerr, Errors...>(),",
                          "typed error: the operation is read and the receiver entered with the converted value"),
        "deliver_routed_exception": ("__terminal_core::__complete_error | "
                                     "__forward_error<std::exception_ptr>(std::move(__failure), __entered);",
                                     "routed conversion failure: the receiver is entered outside the handler"),
        "callback": ("model-only: the receiver's completion function",
                     "the receiver may destroy the operation before it returns"),
        "leave": ("model-only: return from the terminal routine to its caller",
                  "nothing is read after the receiver returned"),
        "child_start": ("model-only: the await bridge connects and starts a child in await_suspend",
                        "the body suspends at an await and the child operation starts"),
        "child_complete": ("model-only: the child's completion signal",
                           "the child completes inside its start or on a worker"),
        "bridge_resume": ("model-only: the await bridge resumes the suspended body",
                          "a value completion resumes the body, which must still be alive"),
        "child_return": ("model-only: return from the child's start and the bridge's await_suspend",
                         "only stack values are used on the way out"),
        "start_return": ("model-only; call site __terminal_core::__resume | __frame_.__get().resume();",
                         "start returns without reading the operation again"),
        "await_terminal": ("model-only: the owner waits for the receiver's signal",
                           "an owner on another thread may act once the receiver was entered"),
    }.items()
}

# Operation and frame lifecycle values.
ABSENT, BUILDING, LIVE, DEAD = range(4)
C = None
COV = set()
PROGRAMS = {}


@dataclass(frozen=True)
class State:
    pcs: tuple
    task: bool = False       # the task object holds the handle
    stale: bool = False      # a moved-from task still holds the handle
    operation: int = ABSENT
    owns: bool = False       # the operation holds the handle
    local: bool = False      # a terminal in flight holds the exchanged handle
    frame: int = ABSENT
    destroys: int = 0
    members: int = 0
    entries: tuple = ()
    worker_enabled: bool = False
    aborted: bool = False
    faults: tuple = ()


def fault(w, text):
    return w if text in w.faults else replace(w, faults=w.faults + (text,))


def read_operation(w):
    if w.operation != LIVE:
        return fault(w, "TF5: operation read after its destruction")
    return w


def read_frame(w):
    if w.frame != LIVE:
        return fault(w, "TF5: frame read after its destruction")
    return w


def destroy_frame(w):
    if w.frame != LIVE:
        w = fault(w, "TF2: frame destroyed twice")
    return replace(w, frame=DEAD, destroys=w.destroys + 1)


def destroy_operation(w):
    # ~__state destroys a still-owned frame first, then the members.
    if w.owns:
        w = replace(destroy_frame(w), owns=False)
    return replace(w, operation=DEAD, members=0)


def step_call(w):
    return replace(w, task=True, frame=LIVE)


def take_from(w, holder):
    """A transfer or a destruction takes the handle from a holder that has it."""
    if not getattr(w, holder):
        return fault(w, "TF1: handle taken from a holder that does not own the frame")
    return w


def step_move(w):
    COV.add("task.move")
    w = take_from(w, "task")
    # The target takes the handle; the control leaves it in the source too.
    return replace(w, stale=C.control == "copying_move")


def step_drop_source(w):
    if w.stale:
        w = replace(destroy_frame(w), stale=False)
    return w


def step_drop_task(w):
    if w.task:
        COV.add("task.owned_destroy")
        return replace(destroy_frame(w), task=False)
    COV.add("task.empty_destroy")
    return w


def step_connect(w):
    w = take_from(w, "task")
    # The control exchanges the handle out of the task and then drops it.
    return replace(w, task=False, operation=BUILDING, owns=C.control != "drop_on_connect")


def step_construct(w):
    members = w.members + 1
    return replace(w, members=members, operation=LIVE if members == 3 else BUILDING)


def step_unwind(w):
    COV.add("connect.unwind")
    w = replace(w, members=0, operation=DEAD)
    if C.control == "raw_handle_state":
        # The exposition's raw handle member: nothing releases the frame.
        return replace(w, owns=False)
    return replace(destroy_frame(take_from(w, "owns")), owns=False)


def step_drop_operation(w):
    COV.add("operation.owned_destroy" if w.owns else "operation.empty_destroy")
    return destroy_operation(w)


def step_associate(w):
    return read_frame(read_operation(w))


def step_resume(w):
    return read_frame(read_operation(w))


def step_final(w):
    return read_frame(w)


def step_stage(w):
    return read_frame(w)


def step_rethrow(w):
    COV.add("error.staging_throw")
    return read_frame(w)


def step_hook(w):
    return read_frame(w)


def step_exchange(w):
    w = take_from(read_operation(w), "owns")
    if C.mut == "omit_exchange":
        # The handle is read without clearing the owner.
        return w
    return replace(w, owns=False, local=True)


def step_destroy(w):
    if C.mut != "omit_exchange":
        w = take_from(w, "local")
    return replace(destroy_frame(w), local=False)


def step_convert(w):
    if w.frame == LIVE:
        w = fault(w, "TF7: error converted before the frame was destroyed")
    return w


def step_capture(w):
    COV.add("error.conversion_routed")
    return w


def step_terminate(w):
    COV.add("error.conversion_terminates")
    return replace(w, aborted=True)


def deliver(kind):
    def step(w):
        w = read_operation(w)
        if w.frame == LIVE:
            w = fault(w, "TF3: receiver entered before the frame was destroyed")
        return replace(w, entries=w.entries + (kind,))
    return step


def step_callback(w):
    if C.destroy == "receiver":
        COV.add("terminal.receiver_destroys")
        return destroy_operation(w)
    return w


def step_leave(w):
    if C.mut == "read_after_callback":
        return read_operation(w)
    return w


def step_child_start(w):
    return replace(read_frame(w), worker_enabled=C.where == "worker")


def step_child_complete(w):
    return w


def step_bridge_resume(w):
    return read_frame(w)


def step_child_return(w):
    COV.add("return.after_terminal" if w.entries else "return.before_terminal")
    return w


def step_start_return(w):
    COV.add("start.after_terminal" if w.entries else "start.before_terminal")
    if C.control == "touch_after_resume":
        return read_operation(w)
    return w


def step_await_terminal(w):
    return w


ACTIONS = {
    "call": step_call, "move": step_move, "drop_source": step_drop_source,
    "drop_task": step_drop_task, "connect": step_connect, "construct": step_construct,
    "unwind": step_unwind, "drop_operation": step_drop_operation, "associate": step_associate,
    "resume": step_resume, "final": step_final, "stage": step_stage, "rethrow": step_rethrow,
    "hook": step_hook, "exchange": step_exchange, "destroy": step_destroy,
    "convert": step_convert, "capture": step_capture, "terminate": step_terminate,
    "deliver_result": deliver("result"), "deliver_stopped": deliver("stopped"),
    "deliver_error": deliver("error"), "deliver_stored_exception": deliver("exception"),
    "deliver_routed_exception": deliver("exception"),
    "callback": step_callback, "leave": step_leave, "child_start": step_child_start,
    "child_complete": step_child_complete, "bridge_resume": step_bridge_resume,
    "child_return": step_child_return, "start_return": step_start_return,
    "await_terminal": step_await_terminal,
}


def retire(cfg):
    """Exchange, destroy and enter the receiver, in the configured order."""
    head = {"result": ["final"], "exception": ["final"], "stopped": ["hook"]}.get(cfg.kind, [])
    release = ["exchange", "destroy"]
    if cfg.kind in ("result", "exception"):
        entry = ["deliver_result" if cfg.kind == "result" else "deliver_stored_exception", "callback"]
    elif cfg.kind == "stopped":
        entry = ["deliver_stopped", "callback"]
    else:
        entry = None
    if entry is not None:
        if cfg.mut == "deliver_before_destroy":
            return head + entry + release + ["leave"]
        return head + release + entry + ["leave"]
    # Typed errors: stage, release, convert, enter.
    if cfg.kind == "staging_throw":
        # The exception escapes the body and completes through final suspension.
        return ["stage", "rethrow", "final"] + release + ["deliver_stored_exception", "callback", "leave"]
    stage = ["stage"]
    if cfg.control == "stage_after_destroy":
        return release + stage + ["convert", "deliver_error", "callback", "leave"]
    if cfg.control == "convert_before_destroy":
        return stage + ["convert"] + release + ["deliver_error", "callback", "leave"]
    if cfg.kind == "error":
        return stage + release + ["convert", "deliver_error", "callback", "leave"]
    if cfg.kind == "conversion_routed":
        return stage + release + ["convert", "capture", "deliver_routed_exception", "callback", "leave"]
    assert cfg.kind == "conversion_terminates", cfg.kind
    return stage + release + ["convert", "terminate"]


def programs(cfg):
    """Straight-line programs of the starting thread and the worker."""
    main, worker = ["call"], []
    if cfg.scenario == "unconnected":
        return main + (["move", "drop_source"] if cfg.move else []) + ["drop_task"], worker
    main += ["connect"]
    if cfg.scenario == "failed_connect":
        return main + ["construct"] * (cfg.fail_at - 1) + ["unwind", "drop_task"], worker
    main += ["construct"] * 3
    if cfg.scenario == "unstarted":
        return main + ["drop_operation", "drop_task"], worker
    main += ["associate", "resume"]
    if cfg.where == "inline":
        main += retire(cfg)
    elif cfg.where == "child_inline":
        main += ["child_start", "child_complete"]
        if cfg.kind == "stopped":
            # The whole terminal runs inside the child's start.
            main += retire(cfg) + ["child_return"]
        else:
            # The bridge defers the resumption until the child's start returned.
            main += ["child_return", "bridge_resume"] + retire(cfg)
    else:
        main += ["child_start", "child_return"]
        worker = ["child_complete"] + ([] if cfg.kind == "stopped" else ["bridge_resume"]) + retire(cfg)
    main += ["start_return"]
    if cfg.destroy == "owner" and cfg.kind != "conversion_terminates":
        main += ["await_terminal", "drop_operation"]
    return main + ["drop_task"], worker


def configure(cfg):
    global C
    C = cfg
    COV.clear()
    PROGRAMS["main"], PROGRAMS["worker"] = programs(cfg)


def initial():
    return State((0, 0))


def key(w):
    return w


def state(k):
    return k


def enabled(w, thread, name):
    if thread == 1 and not w.worker_enabled:
        return False
    if name == "await_terminal":
        return bool(w.entries)
    return True


def successors(w):
    if w.aborted:
        return []
    out = []
    for thread, actor in enumerate(("main", "worker")):
        program = PROGRAMS[actor]
        pc = w.pcs[thread]
        if pc == len(program) or not enabled(w, thread, program[pc]):
            continue
        name = program[pc]
        if actor == "worker":
            COV.add("worker." + ("after_start_returned" if w.pcs[0] > PROGRAMS["main"].index("start_return")
                                 else "before_start_returned"))
        pcs = w.pcs[:thread] + (pc + 1,) + w.pcs[thread + 1:]
        out.append(((name, actor), ACTIONS[name](replace(w, pcs=pcs))))
    return out


def state_violations(w):
    errors = list(w.faults)
    owners = w.task + w.stale + w.owns + w.local
    # Each step moves the handle atomically, so a live frame always has one owner.
    if w.frame == LIVE and owners == 0:
        errors.append("TF1: a live frame has no owner")
    if owners > 1:
        errors.append("TF1: more than one owner holds the frame")
    if len(w.entries) > 1:
        errors.append("TF4: more than one terminal")
    return errors


def expected_entries():
    if C.scenario != "started" or C.kind == "conversion_terminates":
        return ()
    return ({"staging_throw": "exception", "conversion_routed": "exception"}.get(C.kind, C.kind),)


def terminal_violations(w):
    errors = state_violations(w)
    if w.destroys != 1 or w.frame != DEAD:
        errors.append("TF2: frame not released exactly once")
    if w.entries != expected_entries():
        errors.append("TF4: missing or wrong terminal")
    finished = all(pc == len(PROGRAMS[actor]) for pc, actor in zip(w.pcs, ("main", "worker")))
    retired = w.operation in (ABSENT, DEAD) and not (w.task or w.stale or w.owns or w.local or w.members)
    if not w.aborted and not (finished and retired):
        errors.append("TF6: a thread, an owner or the operation has not retired")
    return errors


def is_blocked(w):
    program = PROGRAMS["main"]
    return w.pcs[0] < len(program) and program[w.pcs[0]] == "await_terminal" and not w.entries


def is_aborted(w):
    return w.aborted


def metrics():
    return {"max_store_buffer": 0}


def coverage():
    if not C.mut and not C.control:
        assert C.required <= COV, C.required - COV
    return sorted(COV)


def step_table():
    return [("protocol", name, step.lines, "SC / language / caller", step.desc)
            for name, step in STEPS.items()]


def describe(meta, w):
    step, actor = meta
    return "%s %s: %s" % (actor, step, w)

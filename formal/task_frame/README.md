# Task frame ownership and terminal delivery

This model covers the frame owner and the terminal routines in the [terminal header](../../backport/cpp26/execution/detail/task_terminal.hpp) of `std::execution::task`. It gives pilot case F-07 bounded protocol evidence. It does not cover frame allocation, the promise environment, start-scheduler selection or the stop adapter, which later batches own.

## Protocol and assumptions

- One task has one coroutine frame. The handle is held by the task, then by the connected operation, then by a terminal in flight. Task move and connect exchange it out of their source. The operation's base adopts it before the receiver, the own environment and the `Environment` are constructed, so a throwing member unwinds the constructed ones and releases the frame.
- `start` reads the handle and resumes the body. A terminal reads its awaiter or the promise while the frame is alive, exchanges the handle out of the operation, destroys the frame and then enters the receiver. A typed error first moves its payload to the `await_suspend` stack and converts it after the frame is destroyed. A throwing staging move resumes the body with the exception, and the task then completes through final suspension. A conversion failure is routed to the `exception_ptr` channel, or terminates when that channel is absent.
- The receiver may destroy the operation inside its completion function. In the other variant an owner destroys the operation once the receiver was entered. On a worker terminal that owner is the starting thread, which can run while the worker is still returning.
- A child can complete inside its own start or on a worker thread. Stopped runs the whole terminal inside the child's completion. A value resumes the body, immediately on a worker and after the child's start has returned on the starting thread, as the await bridge does. The worker's steps interleave freely with the starting thread's return through the child's start, the bridge's `await_suspend` and the task's `start`. This covers a worker completion before and after that return is initiated.
- The model is sequentially consistent and has no synchronization of its own, because the header has none. The steps in `task.hpp`, the await bridge and the child are `model-only` and are not locked. No C++ lowering, compiler coroutine transformation, allocator behavior or user-supplied destructor is proved here. The C++ fixtures F-01 to F-07 replay the same paths in the real code, including both worker schedules under the sanitizers.

## Invariants and sensitivity

| Check | Obligation | Failing sensitivity case |
|---|---|---|
| TF1 | Exactly one of the task, a moved-from task, the operation and a terminal in flight holds a live frame, and every transfer takes the handle from a holder that has it | `copying_move.moved.none`, `raw_handle_state.failed_own_env.none`, `drop_on_connect.inline_result.owner`, `drop_on_connect.unstarted.none` |
| TF2 | The frame is destroyed exactly once, and never twice | `omit_exchange.inline_result.owner`, `omit_exchange.worker_stopped.receiver` |
| TF3 | The receiver is entered only after the frame is destroyed | `deliver_before_destroy.inline_result.owner`, `deliver_before_destroy.child_inline_stopped.receiver` |
| TF4 | A started task has exactly one terminal of the expected kind, and no other path has one | — |
| TF5 | Nothing reads the frame or the operation after its destruction | `read_after_callback.inline_result.receiver`, `read_after_callback.worker_stopped.owner`, `read_after_callback.worker_result.owner`, `touch_after_resume.inline_result.receiver`, `touch_after_resume.worker_stopped.receiver`, `stage_after_destroy.inline_error.owner` |
| TF6 | Every thread, owner and operation retires | — |
| TF7 | The error conversion runs after the frame is destroyed | `convert_before_destroy.inline_error.owner` |

The three mutants are the ones the pilot names. `omit_exchange` destroys the frame through the operation's handle without clearing it, so the operation's destructor releases it again. `deliver_before_destroy` enters the receiver first. `read_after_callback` reads the operation after the receiver returned. All three are killed, so none needs a redundant protection. The six negative controls cover a copying task move, the exposition's raw handle on a failed connect, a handle dropped at connect, a read after `resume` returned in `start`, an early conversion and a late staging move. The raw handle and the dropped handle both lose ownership of a live frame.

Each receiver entry has its own source anchor. A value and a stored exception both enter through `__complete_result` and `__send_result`. The stored exception covers a body exception and an uncaught staging failure. A typed error enters through the conversion call in `__complete_error`, and a routed conversion failure through the call after its handler.

The mutants and controls run in both tiers. Each tier run validates the exact named TF category for each negative case against the [sensitivity manifest](sensitivity.txt) before the shared runner can regenerate [expected records](expected/results.tsv) or refresh the lock. The passing configurations also require the scenario-specific transition coverage in [scenarios.py](scenarios.py), which includes a worker step both before and after the starting thread returned.

## Checking and maintenance

```bash
python3 -B formal/task_frame/check.py --check-source
python3 -B formal/task_frame/check.py --tier quick --jobs 4
python3 -B formal/task_frame/check.py --tier full --jobs 4
python3 -B formal/task_frame/check.py --steps
```

Quick selects twenty-six configurations and full selects forty. Each configuration is capped at 20,000 states. The tool saves results, summaries and shortest counterexamples under ignored `build/formal/task_frame/`. Review protocol fidelity against the C++ before refreshing [source.lock](source.lock). For changes to model or data, qualify a full run, inspect the named failures, update the expected records with `--tier full --update-expected`, then rerun separately with `--tier full --refresh-lock --assessment '<review>'`. The lock detects edits and checks function-local source anchors. It is not a proof of source/model equivalence.

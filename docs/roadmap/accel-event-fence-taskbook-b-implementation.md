# Taskbook B: Accel Event/Fence Implementation

## Objective

Implement event/fence by reusing the existing command sender path.

## Implementation Direction

1. Add an internal `__event_state` with:
   - mutex + condition variable;
   - one-shot ready flag;
   - `mark_ready()`;
   - `ready()`;
   - a wait helper that can wake periodically and observe context stop.
2. Add public `forge::accel::event` as a copyable `shared_ptr` handle to
   `__event_state`.
3. Add an internal stopped signal path to the existing command receiver so
   command actions can request `set_stopped()` without building a second sender
   implementation.
4. Implement:
   - `record_event(q, ev)` as a command that marks `ev` ready;
   - `wait_event(q, ev)` as a command that waits for ready or context stop;
   - `fence(q)` as a no-op command sender.
5. Keep all public functions header-only and under `namespace forge::accel`.

## Constraints

- Do not add new runtime threads.
- Do not add a separate event scheduler or waiter queue.
- Do not block forever on shutdown if an event is never recorded.
- Do not run user receiver completion under an accel mutex.
- Preserve existing copy/submit behavior and signatures.

## Risks

- A wait command can block the serialized mock queue. This is acceptable for V1
  but must be documented as a dependency-cycle limitation.
- If context stop does not wake an unready wait command, destructor shutdown can
  hang. Tests must cover this.
- Exceptions used for internal stopped signaling must not leak as user errors.

## Verification

Build the accel tests after implementation:

```bash
cmake --build build/accel-event --target test_forge_accel_event
ctest --test-dir build/accel-event -R '^forge_accel' --output-on-failure
```

Suggested commit:

```text
feat(forge): add accel event and fence commands
```

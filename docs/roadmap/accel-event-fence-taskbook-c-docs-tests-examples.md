# Taskbook C: Accel Event/Fence Tests, Docs, Examples

## Objective

Make the event/fence surface verifiable and teachable.

## Tests

Add `test/forge/runtime/test_forge_accel_event.cpp` and register it under the
existing accel test gate.

Required cases:

- event starts unready;
- copied event handle becomes ready after `record_event`;
- `wait_event` completes for an already ready event;
- `wait_event` on an unready event completes stopped after `context::request_stop`;
- `fence` waits behind an already accepted blocking command.

Keep tests concrete. Prefer `std::execution::sync_wait` for simple cases and a
small custom receiver only for asynchronous fence/stop windows.

## Docs

Update:

- `docs/forge-accel.md`
- `docs/forge-utilities.md`
- `README.md` if the public summary mentions accel command surface.

Docs must say:

- event is one-shot;
- `record_event`, `wait_event`, and `fence` are mock command senders;
- dependency cycles are not detected;
- no vendor backend, native handle, or multi-stream model is implied.

## Examples

Add a small example only if it clarifies usage better than tests. Preferred
shape:

```cpp
forge::accel::event uploaded;
sync_wait(copy_to_device(q, device, host));
sync_wait(record_event(q, uploaded));
sync_wait(wait_event(q, uploaded));
sync_wait(submit(q, [&] { /* mutate device */ }));
sync_wait(fence(q));
```

Register the example under `FORGE_HAS_FORGE_ACCEL_BACKEND`.

## Verification

Focused:

```bash
cmake -S . -B build/accel-event -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON
cmake --build build/accel-event --target test_forge_accel_event forge_accel_event
ctest --test-dir build/accel-event -R '^forge_accel' --output-on-failure
```

Final:

```bash
scripts/verify-native.sh llvm tsan asan
```

If a Windows host is available, run the Windows/MSVC smoke wrapper with private
host/path values supplied through environment variables.

Suggested commit:

```text
docs(forge): document accel event and fence commands
```

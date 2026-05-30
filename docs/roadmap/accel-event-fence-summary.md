# Forge Accel Event/Fence Round

## Objective

Add a minimal event/fence surface to the portable `forge::accel` mock backend.

This round closes the V1 gap that was intentionally deferred in the first accel
round: commands already compose as senders, but there is no explicit queue
marker that can be passed around as a lightweight completion boundary.

## Scope

Target public shape:

```cpp
namespace forge::accel {

class event;

auto record_event(queue& q, event ev);
auto wait_event(queue& q, event ev);
auto fence(queue& q);

} // namespace forge::accel
```

`event` is a small copyable handle to a one-shot mock event state. `record_event`
marks it ready when the queue reaches that command. `wait_event` completes when
the event is ready. `fence` is a queue barrier command that completes after all
previously accepted commands in the same queue ordering have completed.

## Mandatory Decisions

1. No vendor API, driver header, CUDA/HIP/SYCL binding, kernel compilation,
   platform backend, or CMake accelerator language enablement.
2. No second scheduler. Event/fence commands must reuse the existing accel
   queue/strand command path.
3. Completion signatures remain bounded:
   - `set_value_t()`
   - `set_error_t(std::exception_ptr)`
   - `set_stopped_t()`
4. `record_event` and `wait_event` accept the event handle by value. The handle
   is copyable, and copies observe the same one-shot ready state.
5. Event V1 is one-shot. It does not provide reset/re-record lifecycle semantics.
6. `wait_event` must not make `context::shutdown()` hang forever. If waiting on
   an unready event and the owning context is stopped, it must complete stopped.
7. Dependency cycles are not detected. Waiting on an event that can only be
   recorded by a later command on the same serialized queue is user error.

## Non-Goals

- No multi-stream or parallel queue model.
- No cross-process, OS, driver, or native handle event.
- No event pooling or allocation policy expansion.
- No typed error changes.
- No changes to standard backport headers.
- No real accelerator backend.

## Taskbooks

1. `docs/roadmap/accel-event-fence-taskbook-a-contract.md`
2. `docs/roadmap/accel-event-fence-taskbook-b-implementation.md`
3. `docs/roadmap/accel-event-fence-taskbook-c-docs-tests-examples.md`

## Verification Baseline

Focused:

```bash
cmake -S . -B build/accel-event -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON
cmake --build build/accel-event --target test_forge_accel_event
ctest --test-dir build/accel-event -R '^forge_accel' --output-on-failure
```

Final:

```bash
scripts/verify-native.sh llvm tsan asan
FORGE_WINDOWS_HOST=<windows-host> \
FORGE_WINDOWS_VC_VARS='C:\path\to\VC\Auxiliary\Build\vcvars64.bat' \
scripts/verify-windows-msvc-ssh.sh
```

Do not put private hostnames or local installation paths in committed docs.

## Acceptance Criteria

- Event handles are copyable and share one ready state.
- `record_event` marks an event ready at the correct queue point.
- `wait_event` completes after a ready event and stops when context stop prevents
  progress.
- `fence` acts as a FIFO barrier behind already accepted work.
- ASan/TSan stay clean for the new event/fence tests.
- Docs and examples explain the one-shot/cycle limitations plainly.

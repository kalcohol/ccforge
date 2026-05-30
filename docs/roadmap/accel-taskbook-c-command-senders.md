# Taskbook C: Accel Command Senders

## Objective

Add the sender/receiver surface that makes the mock accel core useful:
host/device copies, device/device copies, kernel-like command submission, and a
small event/fence shape.

## Public Senders

Candidate surface:

```cpp
namespace forge::accel {

template<class T>
auto copy_to_device(queue& q, device_buffer<T>& dst, std::span<const T> src);

template<class T>
auto copy_to_host(queue& q, std::span<T> dst, const device_buffer<T>& src);

template<class T>
auto copy_device_to_device(queue& q, device_buffer<T>& dst, const device_buffer<T>& src);

template<class F>
auto submit(queue& q, F&& command);

auto record_event(queue& q);
auto wait_event(queue& q, event ev);

} // namespace forge::accel
```

`submit` runs a host callable on the mock backend. It models a kernel launch
completion, not actual device execution. It should be useful for examples that
mutate `device_buffer` contents without claiming hardware acceleration.

## Completion Signatures

Copy and submit senders:

```cpp
std::execution::completion_signatures<
    std::execution::set_value_t(),
    std::execution::set_error_t(std::exception_ptr),
    std::execution::set_stopped_t()>
```

Event recording may complete with `set_value_t(event)` if returning an event
handle is the cleanest implementation. If the event API gets awkward, prefer
shipping copy/submit first and explicitly deferring standalone events over
adding a half-correct event abstraction.

## Semantics

- Commands are accepted on `start`, not on sender construction.
- Commands execute in FIFO order on a queue.
- Queue capacity is checked on `start`.
- Size mismatches complete with `set_error(std::exception_ptr)`.
- Callable exceptions complete with `set_error(std::exception_ptr)`.
- Start-time receiver stop completes stopped.
- Post-enqueue per-operation stop callback is optional in V1. If not
  implemented, document it like channel and IO V1.
- Command completion happens outside internal locks.
- Operation state owns the receiver until exactly one completion.
- Borrowed host spans and referenced device buffers must outlive completion.

## Implementation Direction

1. Build one reusable internal command-record path.
2. Keep records move-only and heap-owned if that is simpler to make lifetime
   correct.
3. Use `std::exception_ptr` consistently for V1 errors.
4. Avoid a broad property/query framework. Queue ordering and lifecycle are
   enough for V1.
5. Keep sender types concrete and header-only. Do not use `erased_sender` inside
   the accel implementation unless there is a direct benefit.

## Tests

Add focused tests, likely split by behavior:

- `test_forge_accel_copy.cpp`
  - H2D then D2H round trip;
  - D2D copy;
  - size mismatch routes `set_error`;
  - move-only operation state is safe;
  - borrowed span lifetime is documented by test comments, not hidden by copies.
- `test_forge_accel_submit.cpp`
  - submit mutates a device buffer;
  - FIFO order is visible through buffer contents;
  - thrown callable routes `set_error(std::exception_ptr)`;
  - start-time stop routes stopped;
  - queue capacity routes stopped.
- `test_forge_accel_event.cpp`, if event ships:
  - record event after a copy;
  - wait event before a dependent copy;
  - event handle is safe to move/copy as designed.

Tests should use `std::execution::sync_wait`, `then`, `when_all`, `starts_on`, or
`continues_on` where they demonstrate real composition instead of only direct
API calls.

## Risks

- Capturing host spans by value does not own data. Do not accidentally make tests
  safe by copying host data into the sender unless that is the documented API.
- A `submit` callable that touches buffers concurrently can create data races.
  V1 queue serialization should make single-queue examples race-free; document
  that cross-queue access is not modeled yet.
- Events can become a second scheduler. Keep them minimal or defer.

## Verification

Focused:

```bash
cmake --build build/local --target test_forge_accel_copy test_forge_accel_submit
ctest --test-dir build/local -R '^forge_accel' --output-on-failure
```

Sanitizers:

```bash
scripts/verify-native.sh tsan asan
```

## Commit

Suggested commit:

```text
feat(forge): add accel copy and submit senders
```

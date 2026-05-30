# Taskbook C: Readiness Senders

## Objective

Expose practical sender/receiver operations for Linux fd readiness:

- `forge::io::context::readable(int fd)`
- `forge::io::context::writable(int fd)`

These operations should compose with the existing Forge execution utilities.

## Sender Shape

Each readiness sender completes with:

```cpp
std::execution::set_value_t()
std::execution::set_error_t(std::exception_ptr)
std::execution::set_stopped_t()
```

Reasons:

- readiness itself has no value payload in V1;
- typed IO errors are reserved for the future typed-error erased sender round;
- `set_stopped` covers context stop, explicit cancel, and start-time receiver stop.

## Environment

The sender env should expose completion-scheduler metadata only if a meaningful
Forge scheduler is available.

Do not fabricate a CPU scheduler. If completions happen on the IO poller thread,
document that callbacks run on the IO context's poller thread. Users can compose
with `continues_on(pool.get_scheduler())` or submit into a `strand` for CPU work.

## Operation State

Operation state owns:

- receiver;
- fd;
- readiness kind;
- a shared/internal pending record if the context needs to complete from the
  poller thread after `start()` returns.

Rules:

- `connect` should not register the fd. Registration happens in `start()`.
- `start()` observes receiver stop token before enqueueing.
- V1 does not register per-operation stop callbacks for already-enqueued idle
  waiters. Context `request_stop()`, `cancel(fd)`, and readiness events wake them.
- Completion must not access the operation state after invoking the receiver if
  receiver code may destroy the operation state.

Use the safe ownership patterns from `bounded_channel`, `timer_context`, and
`erased_sender`: pending records outlive operation state when the poller needs to
complete asynchronously.

## Public Header Shape

Preferred:

```cpp
#include <forge/io.hpp>

forge::io::context io;
auto s = io.readable(fd);
```

`<forge/execution.hpp>` may include `<forge/io.hpp>` only when doing so is
portable under IO `OFF` and non-Linux builds. If the IO umbrella must include
Linux headers, keep it separate and document that users include `<forge/io.hpp>`
for IO.

## Tests

Required tests:

- sender concepts compile under the Forge execution backport;
- `sync_wait(io.readable(fd))` completes after a write;
- `starts_on(pool.get_scheduler(), io.readable(fd))` or equivalent composition
  works when meaningful;
- readable completion can schedule CPU work on `runtime_context`;
- completion callback can destroy the operation state without UAF;
- receiver start-time stop token completes stopped without registering fd;
- context shutdown after enqueue completes stopped;
- duplicate waiter error path reaches `set_error(std::exception_ptr)`.

Composition examples in tests:

- pipe producer on `static_thread_pool`, readiness on `io::context`, CPU
  continuation on `strand`;
- pending readiness owned by `async_scope`, then `resource_context` shutdown.

## Non-Goals

- Do not add `read_some` or `write_some` yet.
- Do not own user buffers.
- Do not implement post-enqueue per-op stop callback unless a separate lifecycle
  proof and TSAN/ASAN stress test are added in this same taskbook.
- Do not add typed errors.

## Risks

- Readiness senders that complete on the poller thread can block the poller if
  user callbacks do heavy work. Docs and examples should immediately continue on
  a CPU scheduler for heavy work.
- Operation-state self-destruction must be safe. Treat receiver completion as
  the last use of operation-owned state.
- FD borrowed lifetime must be visible in every example.

## Verification

```bash
cmake --build build/local --target test_forge_io_context test_forge_execution_header
ctest --test-dir build/local -R 'forge_io|forge_execution_header' --output-on-failure
scripts/verify-native.sh tsan asan
```

## Commit

Suggested commit:

```text
feat(forge): expose fd readiness senders
```

# Taskbook B: Linux Epoll Context

## Objective

Implement the Linux ownership and event-loop core for `forge::io::context`.

This taskbook should establish lifetime correctness before public readiness
senders become rich. It may expose a minimal internal-ready API for tests, but
the final public sender surface belongs to Taskbook C.

## Current Facts

- `timer_context`, `runtime_context`, `async_scope`, `bounded_channel`, and
  `strand` already define local patterns for owning threads, waking waiters,
  blocking destructors, and lock-outside-completion.
- `resource_policy` is available for context-owned dynamic storage.
- `erased_sender` exists but typed errors are intentionally out of scope.

## Proposed Files

- `include/forge/io/context.hpp`
- `include/forge/io.hpp`
- `test/forge/runtime/test_forge_io_context.cpp`

Use a subdirectory because IO is expected to grow. Keep the umbrella header small.

## Ownership Model

`forge::io::context` owns:

- an epoll fd;
- an eventfd used to wake shutdown/cancel changes;
- one poller thread;
- a mutex-protected map from fd to pending read/write records;
- an event buffer allocated through the configured memory resource;
- action batches that deliver completions outside the mutex.

The poller thread should never invoke user receiver completions while holding the
context mutex.

## Lifecycle

Constructor:

1. normalize `context_options::memory`;
2. create epoll fd;
3. create nonblocking eventfd;
4. register eventfd with epoll;
5. start poller thread only after all state is initialized.

Shutdown:

1. `close()` marks ingress closed and wakes poller.
2. `request_stop()` marks stopped, drains pending records into stopped actions,
   and wakes poller.
3. `shutdown()` calls both.
4. `wait()` joins the poller and delivers or confirms all pending completions.
5. Destructor calls `shutdown()` and `wait()`.

If an OS call fails during construction, throw `std::system_error` before the
context becomes visible.

## FD Registration Rules

V1 supports at most:

- one pending read waiter per fd;
- one pending write waiter per fd.

When the first waiter for an fd is registered, add or modify the fd in epoll.
When a waiter completes or is cancelled, update or remove the epoll interest.

Use level-triggered epoll in V1. Avoid edge-triggered semantics until examples
and tests need them.

## Completion Policy

- Readiness event wins -> complete the matching operation with `set_value()`.
- Context stop wins -> complete pending operations with `set_stopped()`.
- Duplicate pending waiter for the same fd/readiness kind -> complete the new
  operation with `set_error(std::exception_ptr)` wrapping `std::system_error`
  with `std::errc::operation_in_progress`.
- Invalid fd or epoll registration failure -> complete with `set_error`.
- Poller-level fatal error -> stop the context and complete pending operations
  with `set_error` where possible.

All completions must be exactly-once.

## Resource Policy

Use the configured memory resource for:

- pending record containers;
- event buffers;
- action batches;
- shared pending records if shared ownership is needed.

Document any remaining allocation paths. Do not rewrite already-stable runtime
intrusive keepalive nodes in this round.

## Tests

Use real Linux descriptors:

- `pipe2(O_NONBLOCK)` or `socketpair(AF_UNIX, SOCK_NONBLOCK, 0, ...)`;
- `eventfd(EFD_NONBLOCK | EFD_CLOEXEC)` where useful.

Required tests:

- construct/destroy empty context;
- `readable(fd)` completes after writing to the pipe/socketpair;
- `writable(fd)` completes for a writable socketpair end;
- `request_stop()` cancels an idle pending waiter with stopped;
- duplicate read waiter for the same fd completes the second with error;
- `cancel(fd)` stops pending read/write waiters for that fd;
- destructor with an idle waiter does not leak or hang;
- custom memory resource observes context-owned allocations.

Stress tests:

- readiness and `request_stop()` racing do not double-complete;
- many fds become ready and complete without holding the context mutex during
  callbacks.

## Risks

- Closing an fd while it is still registered can cause fd-reuse confusion. V1
  should document this as caller error and examples must cancel/drain before
  closing.
- Poller thread startup order matters. Start it only after epoll/eventfd and
  containers are initialized.
- Stop/cancel racing with readiness needs a clear exactly-one winner.
- `eventfd` wake bytes must be drained; otherwise the poller may spin.

## Verification

```bash
cmake --build build/local --target test_forge_io_context
ctest --test-dir build/local -R 'forge_io_context' --output-on-failure
scripts/verify-native.sh tsan asan
```

## Commit

Suggested commit:

```text
feat(forge): add linux io context core
```

# Forge IO post-enqueue stop callbacks

## Goal

Upgrade `forge::io::context` readiness senders from start-time stop-token
observation to per-operation post-enqueue cancellation for pending
`readable(fd)` / `writable(fd)` waiters.

This closes the main V1 IO cancellation caveat: a receiver stop token requested
after `start()` should wake and complete an idle readiness waiter without
requiring fd readiness, `cancel(fd)`, `request_stop()`, or `shutdown()`.

## Scope

- Register stop callbacks only after a readiness record is accepted into the
  pending fd table.
- Remove the exact pending record when the receiver token stops.
- Update epoll interest after removing a canceled read/write slot.
- Wake the poller through the existing `eventfd`.
- Complete canceled operations with `set_stopped()` outside the context mutex.
- Preserve the existing borrowed-fd and one-read/one-write-waiter rules.

## Non-Goals

- No async read/write buffer abstraction.
- No fd ownership changes.
- No Windows IOCP, kqueue, or io_uring backend work.
- No typed-error changes.
- No public cancellation-token abstraction beyond receiver env stop tokens.

## Acceptance

- Pending `readable(fd)` stop-token request completes stopped immediately.
- Pending `writable(fd)` stop-token request completes stopped immediately.
- A canceled waiter is removed from the fd table and does not later consume
  readiness.
- Duplicate waiter, context `cancel(fd)`, `request_stop()`, and readiness paths
  remain unchanged.
- ASan/TSan stay clean.

## Taskbooks

1. `docs/roadmap/io-stop-callback-taskbook-a-contract.md`
2. `docs/roadmap/io-stop-callback-taskbook-b-implementation.md`
3. `docs/roadmap/io-stop-callback-taskbook-c-docs-verification.md`

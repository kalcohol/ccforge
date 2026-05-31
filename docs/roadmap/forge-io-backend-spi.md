# `forge::io` backend SPI sketch

This is a design sketch for future IO backend proofs. It is not a public plugin
ABI and it does not approve `io_uring`, kqueue, or a networking abstraction by
itself.

The shipped IO surface has two intentionally different backend models:

- Linux `epoll` / `eventfd` readiness;
- Windows IOCP completion.

Future backends should preserve that honesty. Do not force every platform into
one fake abstraction if the OS completion model is different.

## portable user-facing shape

The stable vocabulary is small:

- owning `forge::io::context`;
- borrowed OS handles;
- borrowed byte spans for one-shot read/write helpers;
- Linux readiness senders: `readable(fd)` and `writable(fd)`;
- platform read/write helpers: `async_read_some(...)` and
  `async_write_some(...)`;
- `close()`, `request_stop()`, `shutdown()`, `wait()`, and per-handle
  `cancel(...)`;
- default exception errors and opt-in typed-error variants.

Readiness and completion backends do not need identical APIs. For example, IOCP
does not expose `readable()` / `writable()` because completion packets already
represent submitted operations.

## backend contract

Every backend proof must define:

- how operations are accepted or rejected after `close()` / `shutdown()`;
- how context stop completes pending operations;
- whether receiver stop tokens cancel already accepted operations;
- how cancellation is drained before record ownership is released;
- which thread invokes receiver completion;
- whether completion can ever run while holding backend locks;
- which OS handles and buffers are borrowed and how long they must live;
- which stable typed errors are exposed by `_typed` variants.

The default rule is exactly one terminal completion and no receiver completion
under backend internal locks.

## Linux readiness policy

The existing Linux backend is level-triggered readiness. It reports "fd appears
ready"; it does not own the following syscall unless the user chooses
`async_read_some` / `async_write_some`.

Consequences:

- `readable(fd)` / `writable(fd)` only complete with `set_value()`;
- EOF, socket errors, and short IO are observed by the user's syscall;
- if another consumer drains the fd between readiness and syscall, `EAGAIN` /
  `EWOULDBLOCK` is a normal syscall error;
- one pending read waiter and one pending write waiter per fd are supported.

`io_uring` should not be added as "better epoll". It should only be considered
when the project needs kernel submission/completion queue semantics and can test
the different cancellation/drain behavior.

## Windows IOCP policy

The existing Windows backend is completion-based. Operations are explicitly
submitted and complete through IOCP packets.

Consequences:

- no readiness senders are exposed;
- `CancelIoEx` is asynchronous and the completion packet must still be drained;
- handles must support overlapped IO and remain valid until operation completion
  or context drain;
- the current proof remembers associated handles; high-churn production
  pruning is a future hardening task, not a portable contract.

## future backend entry checklist

Before adding a new IO backend, require:

- an explicit gate and configure probe;
- gate-off builds with zero backend tests/examples registered;
- focused tests for accept, cancel, request-stop, close, shutdown, and borrowed
  lifetime boundaries;
- sanitizer coverage where the backend is available;
- install-package behavior that reruns probes in the consumer project;
- documentation of readiness vs completion semantics;
- typed-error mapping review for stable portable categories only.

kqueue or another readiness backend can follow the Linux shape if the semantics
match. A completion backend should follow the IOCP shape instead of pretending to
be readiness.

# Forge runtime lifecycle contract

`include/forge/` runtime primitives are Forge extensions. They are not standard
backports and they do not add names to `namespace std`.

This document fixes the lifecycle vocabulary used by Forge runtime utilities so
new facilities do not drift apart.

## Core Terms

`close()` means graceful ingress close:

- future work/messages are rejected;
- already accepted work may still complete normally;
- buffered channel values may drain;
- close by itself does not request stop.

`request_stop()` means cooperative cancellation:

- owned operations should observe a stop token where the primitive supports one;
- pending operations may complete `set_stopped()`;
- already running user code is not forcibly interrupted.

`shutdown()` means `close()` plus `request_stop()` for owning runtime objects.
It is the normal "session is ending" operation.

`wait()` is a blocking drain helper:

- it waits for work already accepted by that primitive according to the
  primitive's documented scope;
- it must not complete user callbacks while holding internal locks;
- it must avoid self-deadlock when called from the primitive's own worker or
  completion callback.

`join()` is the preferred async surface when a primitive can expose a sender
that completes once the object is drained. Blocking `wait()` may still exist for
tests, destructors, and simple shutdown paths.

## Destructor Policy

Owning Forge runtime objects should be safe to destroy. The preferred policy is:

- `shutdown()`;
- drain accepted work with `wait()` or an equivalent internal join;
- document that destruction may block.

This is an intentional Forge extension tradeoff. Some standard-style scope
facilities require explicit join before destruction and treat destruction with
outstanding work as a precondition violation. Forge owning contexts instead
prefer safe destruction for resource/session management.

Non-owning views and lightweight handles should not block in destructors.

## Current Utilities

- `static_thread_pool::shutdown()` stops accepting new schedule operations and
  drains accepted work. `wait()` waits for the queue and active tasks to empty.
- `timer_context::shutdown()` stops accepting timers and completes pending timers
  stopped. `wait()` waits for accepted timer operations.
- `runtime_context::wait()` is a practical single-hop drain:
  pool -> timers -> pool. It is not an unbounded quiescence protocol.
- `async_scope` owns eager-start sender work. `close()` rejects future spawn,
  `request_stop()` exposes a requested stop token through owned receiver envs,
  and destruction performs `shutdown()` plus `wait()`.
- `bounded_channel` provides graceful `close()` draining and cancel-now
  `request_stop()`. Its v1 operation stop-token support is pre-start only;
  post-enqueue receiver stop requires a later channel state change.
- `erased_sender` forwards downstream stop tokens through its v1 bounded env
  model.
- `task` completes receivers from coroutine final suspend; custom receivers must
  not synchronously destroy the connected task operation state from inside the
  completion callback.

## V1 Cancellation Boundaries

Forge primitives should prefer a clearly documented small guarantee over a
half-correct broad one.

For pending operation cancellation, a v1 primitive may choose one of these
levels:

- pre-start observation only;
- primitive-owned close/shutdown wakeups;
- full per-operation stop-callback cancellation, only when callback lifetime and
  exactly-once completion are proven and tested under sanitizers.

If full callback cancellation is not implemented, document the missing case
explicitly.

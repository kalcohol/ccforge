# Taskbook B: implementation

## Objective

Install stop callbacks on pending IO readiness records using the existing
`shared_ptr` record ownership model and `eventfd` wake path.

## Implementation Steps

1. Make `__state::start(record)` report whether the record became pending.
2. Add `cancel_record(record)` or equivalent helpers that:
   - lock the context;
   - find the exact fd entry and read/write slot;
   - verify the slot holds the same record;
   - remove the slot and decrement `pending`;
   - update or delete epoll interest for the fd;
   - arrange a stopped completion outside the mutex;
   - wake the poller.
3. Add per-record stop callback state:
   - erase receiver token with `std::any_stop_token`;
   - store `std::stop_callback_for_t<std::any_stop_token, Fn>` in the concrete
     record;
   - callback captures weak pointers to the state and base record.
4. Install the callback only after `start(record)` reports pending.
5. If callback construction throws, cancel the already-pending operation
   stopped.

## Guardrails

- Do not make operation state movable or stack-owned.
- Do not keep references to receiver env or stop-token temporaries.
- Do not allocate an action vector inside the stop callback; complete the
  removed record directly after unlocking.
- Keep epoll interest updates under the same mutex that protects `fd_waiters`.

## Risks

- Stop callbacks may run synchronously during registration if the token is
  already stopped. The record must already be visible in the pending table.
- Readiness and stop can race. The exact-slot check plus `done` flag must make
  the losing path a no-op.
- Removing one slot from an fd that also has the other readiness kind pending
  must leave the remaining epoll interest intact.

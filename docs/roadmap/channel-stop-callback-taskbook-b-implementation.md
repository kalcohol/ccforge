# Taskbook B: implementation

## Objective

Install stop callbacks on pending `bounded_channel` send/recv records using the
existing `shared_ptr` record ownership model.

## Implementation Steps

1. Make `__state::start_send` and `__state::start_recv` report whether the
   operation became pending.
2. Add `cancel_send(record)` / `cancel_recv(record)` helpers that:
   - lock the channel;
   - find the exact pending record;
   - erase it from the pending queue;
   - queue a stopped completion action.
3. Add per-record stop callback state:
   - use `std::any_stop_token` to erase token type;
   - store `std::stop_callback_for_t<std::any_stop_token, Fn>` in the record;
   - callback captures weak pointers to state and record.
4. Install the callback only after `start_send` / `start_recv` returns pending.
5. If callback construction fails, cancel the pending operation stopped.

## Guardrails

- Do not call user completion while holding the channel mutex.
- Do not change completion signatures.
- Do not introduce a worker thread.
- Do not make send/recv records movable or stack-owned.

## Risks

- Stop callbacks can run synchronously during registration if the token is
  already stopped. Install only after the record is visible in the pending queue.
- A value completion and a stop callback can race; the existing `done` flag must
  stay the final completion arbiter.

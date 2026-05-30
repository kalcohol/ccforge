# Taskbook A: contract and tests-first shape

## Objective

Define the exact cancellation contract for receiver stop-token requests that
arrive after `forge::io::context::readable(fd)` or `writable(fd)` has been
started and accepted as pending.

## Contract

1. Start-time stopped receivers continue to complete stopped without registering
   fd interest.
2. If a readiness operation becomes pending and its receiver env exposes a stop
   token convertible to `std::any_stop_token`, the operation installs one stop
   callback.
3. When that callback runs, it removes the exact record from the matching fd
   read/write slot, decrements the pending count, updates epoll interest, wakes
   the poller, and completes stopped outside the context mutex.
4. If the fd also becomes ready or the context cancels/stops concurrently, the
   existing record `done` flag remains the final completion arbiter.
5. Receivers without a stoppable token retain existing behavior.

## Tests To Add First

- Pending `readable(fd)` with a receiver stop token completes stopped after the
  token is requested.
- A canceled read waiter does not consume later pipe readability; a following
  `readable(fd)` can complete on the same fd after data is written.
- Pending `writable(fd)` with a receiver stop token completes stopped.
- Existing duplicate waiter and context `cancel(fd)` tests still pass.

## Guardrails

- Do not call user completion under the context mutex.
- Do not allocate from inside the stop callback.
- Do not change completion signatures.
- Do not change borrowed-fd lifetime rules.

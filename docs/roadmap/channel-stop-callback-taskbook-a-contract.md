# Taskbook A: contract

## Objective

Define channel post-enqueue stop behavior before changing ownership-sensitive
record code.

## Current Facts

- `bounded_channel` records are heap-allocated and held through `shared_ptr`.
- Pending queues hold `shared_ptr<__send_base<T>>` and
  `shared_ptr<__recv_base<T>>`.
- Completion paths already use `done.exchange(...)` and run outside the channel
  mutex.
- Docs currently describe post-enqueue receiver stop-token support as deferred.

## Contract

1. `start()` still checks receiver stop token before enqueue.
2. If an operation becomes pending and its receiver env has a stoppable token,
   register a stop callback.
3. The callback removes the record from the corresponding pending queue under
   the channel mutex.
4. The callback completes the record stopped outside the mutex.
5. If the operation already completed by value/close/request_stop, the callback
   is a no-op.
6. No callback is registered for immediate handoff, buffering, close, or stopped
   completion.

## Tests

- Pending recv canceled by token does not consume a later `try_send`.
- Pending send canceled by token does not become a later buffered value.
- Existing reentrant completion test remains green.

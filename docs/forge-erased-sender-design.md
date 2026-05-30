# `forge::erased_sender` Design Note

This note captures the intended future path for a fully-erased sender facility. It does not change the current `forge::any_sender_of` contract.

## Status

Design accepted for a future task. No public `forge::erased_sender` type exists yet.

## Relationship To `any_sender_of`

`forge::any_sender_of<CompletionSignatures>` remains intentionally narrow:

- one value shape
- errors collapse through `sync_wait()`'s exception path
- no connectable erased sender interface

`forge::erased_sender` should be a separate type with a separate name. It must not silently widen `any_sender_of`, because existing users can rely on the small storage-oriented `sync_wait()` helper remaining simple.

## Proposed V1 Surface

Candidate API:

```cpp
namespace forge {

template<class CompletionSignatures>
class erased_sender;

} // namespace forge
```

Supported completion signatures for v1:

- any number of unique `set_value_t(Vs...)` alternatives
- optional `set_error_t(std::exception_ptr)`
- optional `set_stopped_t()`

Unsupported in v1:

- arbitrary typed errors such as `set_error_t(std::error_code)`
- allocator-aware storage
- semantic equality
- changing `any_sender_of`

The typed-error restriction is deliberate. Arbitrary error alternatives require a broader receiver vtable matrix and should be a later design extension rather than part of the first useful erased sender.

## Value Model

Use the existing execution meta utilities in `backport/cpp26/execution/detail/value_result.hpp` as the source of truth for value alternatives:

- collect unique decayed `tuple<...>` value shapes
- preserve the existing single-value-shape versus variant-of-tuples rules
- avoid creating another divergent typelist implementation under `include/forge`

The erased receiver side should dispatch each supported `set_value_t(Vs...)` alternative to the target receiver without losing value shape.

## Error And Stopped Model

V1 should only support `set_error_t(std::exception_ptr)`.

For concrete senders that expose other typed errors, construction should fail at compile time unless the user adapts the sender first, for example by converting errors to `std::exception_ptr` with an explicit adapter.

`set_stopped_t()` should be forwarded if present in `CompletionSignatures`; otherwise a stopped completion from the source sender should be a construction-time or connect-time contract violation, not silently translated.

## Receiver Erasure

A fully-erased sender needs a receiver-erasure layer that is broader than current `any_receiver_of`:

- one vtable entry per unique value tuple alternative
- one `std::exception_ptr` error entry
- optional stopped entry
- `get_env` forwarding for the concrete downstream receiver

The vtable should be generated from `CompletionSignatures`, not from observed runtime behavior.

## Operation-State Ownership

Preferred first implementation:

- store the concrete source sender in an erased shared state
- `connect(erased_sender, receiver)` creates a heap-owned operation model
- the operation model owns the concrete operation state returned by connecting the concrete sender to the erased receiver
- `start()` forwards to the concrete operation state

Heap-first is acceptable for v1. SBO can be added only after the ownership model is proven under ASan/TSan and move-only sender tests.

## Stop And Env

The erased receiver must forward `get_env(receiver)` to the concrete receiver so source senders can observe stop tokens and receiver environment queries.

This is a gating requirement. If env forwarding becomes ambiguous or requires invasive changes to the backport, stop at design-only rather than shipping a partial erased sender that breaks cancellation propagation.

## Test Plan

Compile-time tests:

- accepts one value shape
- accepts multiple value shapes
- accepts `set_error_t(std::exception_ptr)`
- rejects arbitrary typed errors
- rejects unsupported completion signatures with a clear diagnostic where feasible

Runtime tests:

- one value shape is delivered
- multiple value shapes dispatch to the correct receiver path
- `std::exception_ptr` error is delivered
- stopped is delivered
- receiver env and stop token propagate through the erased receiver
- move-only source sender works if claimed supported
- operation-state lifetime is clean under ASan/TSan

## Stop Criteria

Do not implement v1 if:

- typed errors leak back into scope
- receiver env forwarding cannot be made correct
- operation-state ownership requires changing existing `any_sender_of`
- the design cannot be tested without depending on undefined receiver lifetime behavior

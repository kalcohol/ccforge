# `std::execution` conformance ledger

This ledger records the current Forge execution backport state after the latest
runtime hardening rounds. It is deliberately separate from
[`std::execution`](../backports/execution.md): that page is user-facing
documentation, while this page is an engineering audit log for future native
handoff and conformance work.

## current implementation status

The execution backport currently includes:

- sender factories: `just`, `just_error`, `just_stopped`, `read_env`;
- adaptors: `then`, `upon_error`, `upon_stopped`, `let_value`, `let_error`,
  `let_stopped`, `write_env`;
- scheduler adaptors: `starts_on`, `continues_on`, `transfer_just`, serial
  `bulk`;
- composition: `into_variant`, `when_all`, `when_all_with_variant`, `split`,
  `ensure_started`, `start_detached`, `spawn_future`;
- consumers: `sync_wait`, `sync_wait_with_variant`;
- stop-token support: `inplace_stop_source/token/callback`,
  `never_stop_token`, `any_stop_token`;
- coroutine bridge: `as_awaitable`, `with_awaitable_senders`;
- scopes: `simple_counting_scope`, stop-aware `counting_scope`;
- domain dispatch: receiver-env late domain selection plus `transform_sender`
  and `transform_env` wrapping.

Several older audit notes are therefore closed and should not be carried forward
as open work: identity-only domain dispatch, single-shape `sync_wait`,
non-stop-aware `counting_scope`, and incomplete `when_all` cartesian value
signatures.

## intentional extensions and compatibility risks

Forge has a few pragmatic backport-only conveniences:

- non-copyable non-const lvalue senders can be destructively moved by selected
  library adaptors. Native C++26 implementations require callers to spell
  `std::move(sndr)`;
- `counting_scope::join()` keeps Forge's blocking extension rather than the
  final standard sender-returning shape;
- `ensure_started` is a multi-consumer cached sender; it is not yet the exact
  single-shot/cancel-on-abandon standard shape.

These are useful on today's toolchains, but they are native-handoff migration
risks. Any example intended to compile unchanged under native C++26 should avoid
depending on them.

## remaining conformance gaps

Track these as current gaps until a focused taskbook closes them:

- throwing receiver completion callbacks are not supported;
- execution-domain dispatch remains a draft subset and does not implement the
  full recursive standard model;
- `ensure_started` does not support move-only value results and does not request
  stop when the returned sender is abandoned;
- `spawn_future` uses `get_allocator` for its shared state, but auxiliary
  consumer/callback allocation is not fully allocator-aware;
- `counting_scope::join()` is still the Forge blocking extension;
- native `std::execution` has no stable mainstream implementation in the normal
  verification matrix, so native handoff for execution itself remains a future
  integration risk.

## stdexec feasibility status

NVIDIA stdexec is useful as a semantic reference implementation for senders and
receivers. It is not a direct replacement for this repository's native-handoff
lane:

- Forge exposes `std::execution` through `<execution>`;
- stdexec exposes its own `stdexec::` surface and headers such as
  `<stdexec/execution.hpp>`;
- a meaningful compatibility lane would need an adapter/shim plan before it can
  test Forge `include/forge/` utilities against stdexec.

The optional script `scripts/probe-stdexec-feasibility.sh` only checks that a
locally provided stdexec checkout and the Forge execution backport can each
compile tiny smoke programs. It intentionally does not fetch stdexec and is not
part of the default verification floor.

## next useful checks

1. Keep `scripts/verify-native.sh gcc-exec` as the current libstdc++ execution
   backport lane.
2. Use `scripts/probe-stdexec-feasibility.sh` only as a local spike when a
   stdexec checkout is available.
3. If stdexec comparison becomes valuable, write a separate taskbook for the
   adapter layer and define exactly which examples/tests must be portable across
   Forge and stdexec.


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
  `spawn_future`, plus Forge/stdexec-era extension names `ensure_started` and
  `start_detached`;
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

## compatibility classification

The table below separates current-draft conformance work from Forge convenience
extensions. This is the source of truth for native handoff risk triage.

| Item | Classification | Current state | Native-handoff action |
| --- | --- | --- | --- |
| Library adaptor non-copyable lvalue `connect` | Backport-only convenience | Selected library senders destructively move non-const non-copyable lvalues. | Portable examples should spell `std::move(sndr)`; keep tests for the Forge convenience. |
| `forge::async_scope::spawn(lvalue)` | Forge extension convenience | Mirrors the destructive-move convention for non-copyable non-const lvalue senders. | Portable code should spell `scope.spawn(std::move(sndr))`; keep documented as a Forge convenience. |
| `std::execution::ensure_started` | Non-WD extension name | Exposed as a multi-consumer cached eager sender. Current working-draft execution wording no longer has this name. | Keep as an explicit Forge/stdexec-era extension unless owner approves removal or migration to a `forge::` name. |
| `std::execution::start_detached` | Non-WD extension name | Exposed as fire-and-forget with terminate-on-error semantics. Current working-draft execution wording uses `spawn` with scope tokens for the standard fire-and-forget shape. | Keep as an explicit Forge/stdexec-era extension; examples may use it when demonstrating Forge/backport utilities. |
| `std::execution::spawn` | Missing current-WD surface | Not implemented. | Future conformance task if standard-shaped scope spawning becomes a priority. |
| `std::execution::counting_scope::join()` | Shape mismatch | Forge keeps a blocking `void join()` member. Current working-draft `join()` is a sender-returning async surface. | Owner-deferred Tier A conformance item; do not document the blocking member as standard-shaped. |
| Scope-token `wrap` / `associate` / member `spawn` | Shape mismatch | Forge tokens use early practical helpers: `wrap` associates work on connect, `associate` is a token member, and `token.spawn` fire-and-forgets through `start_detached`. Current working-draft `simple_counting_scope::token::wrap` is an identity wrapper, `counting_scope::token::wrap` only adds stop-token fusion, and standard `spawn` owns association. | Treat the whole scope-token surface as one future conformance slice; do not adjust only `join()` in isolation. |
| Throwing receiver completion callbacks | Intentional unsupported boundary | `set_value`, `set_error`, and `set_stopped` must be `noexcept`; a negative compile probe enforces this. | Keep rejected unless a focused task rewrites completion dispatch. |
| Execution domain dispatch | Draft subset | Receiver-env late-domain selection, scheduler-derived completion domain, `transform_sender`, and `transform_env` wrapper are implemented, but the full recursive standard model is not. | Track as Tier B conformance work. |
| `forge::any_scheduler` | Forge local utility | Models Forge's local scheduler concept, with shared-state identity equality and backport CPO completion-scheduler roundtrip. | Native member-query scheduler roundtrip remains a forward-compat caveat. |
| `forge::wait_result` | Forge local utility | Synchronously preserves value, stopped, and closed-set typed error without throwing. | Use when typed errors must cross a synchronous boundary; it is not `std::execution::sync_wait`. |
| `forge::erased_sender` | Forge local utility | Connectable erased sender with multiple value shapes, closed-set typed errors, and bounded env/stop-token forwarding. | Keep under `forge::`; do not treat as standard execution surface. |
| Receiver env stop-token propagation | Required behavior for Forge utilities | `wait_result`, `erased_sender`, runtime senders, and IO/accel wrappers preserve receiver stop-token visibility in their supported env model. | Keep regression tests when touching type erasure or wrapper receivers. |

## remaining conformance gaps

Track these as current gaps until a focused taskbook closes them:

- execution-domain dispatch remains a draft subset and does not implement the
  full recursive standard model;
- standard-shaped `spawn` is not implemented;
- `ensure_started`, if kept, does not support move-only value results and does
  not request stop when the returned sender is abandoned;
- `spawn_future` uses `get_allocator` for its shared state, but auxiliary
  consumer/callback allocation is not fully allocator-aware;
- `counting_scope::join()` is still the Forge blocking extension rather than a
  sender-returning standard-shaped operation;
- scope-token `wrap` / `associate` / member `spawn` retain Forge's early
  practical scope model rather than the current working-draft split between
  token wrapping and top-level `spawn`;
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

The optional script `scripts/probe-stdexec-feasibility.sh` checks a locally
provided stdexec checkout plus a small named set of Forge execution facilities.
It intentionally does not fetch stdexec and is not part of the default
verification floor. When `STDEXEC_ROOT` is absent it exits with skip code 77 and
prints `result=skipped`; a successful probe prints `result=passed`.

Current named checks:

- `stdexec_just_smoke`: stdexec headers compile a tiny `stdexec::just` program;
- `forge_execution_sync_wait`: Forge `<execution>` backport runs
  `sync_wait(just(42))`;
- `forge_wait_result_typed_error`: `forge::wait_result` preserves a closed-set
  typed error;
- `forge_erased_sender_typed_error`: `forge::erased_sender` carries the same
  typed error across an erased sender boundary;
- `forge_any_scheduler`: `forge::any_scheduler` models the local Forge
  scheduler concept and schedules successfully;
- `forge_receiver_stop_env`: receiver env stop-token propagation remains
  observable through `forge::wait_result`.

These checks are a feasibility ledger, not a compatibility proof. They do not
adapt Forge `std::execution` code onto stdexec's namespace and should not be
treated as evidence that native `std::execution` handoff is complete.

## next useful checks

1. Keep `scripts/verify-native.sh gcc-exec` as the current libstdc++ execution
   backport lane.
2. Use `scripts/probe-stdexec-feasibility.sh` only as a local spike when a
   stdexec checkout is available; review each named check result rather than
   only the final `result=passed` line.
3. If stdexec comparison becomes valuable, write a separate taskbook for the
   adapter layer and define exactly which examples/tests must be portable across
   Forge and stdexec.

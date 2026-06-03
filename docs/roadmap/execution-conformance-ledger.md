# `std::execution` conformance ledger

This ledger records the current Forge execution backport state after the latest
runtime hardening rounds. It is deliberately separate from
[`std::execution`](../backports/execution.md): that page is user-facing
documentation, while this page is an engineering audit log for future native
handoff and conformance work.

The active convergence checklist is
[`execution-wd-convergence-checklist.md`](execution-wd-convergence-checklist.md).
The owner has accepted breaking API changes when they move `std::execution`
toward the current working draft, so the "native-handoff action" column below
now prefers standard convergence over preserving old extension spellings.

## current implementation status

This snapshot was audited against the live working draft `[exec]` text at
<https://eel.is/c++draft/exec> on 2026-06-03. The working draft is the
authority for the classification below; papers and reference implementations
are only used as explanatory context.

The execution backport currently includes:

- sender factories: `just`, `just_error`, `just_stopped`, `read_env`;
- adaptors: `then`, `upon_error`, `upon_stopped`, `let_value`, `let_error`,
  `let_stopped`, `write_env`, `unstoppable`;
- scheduler adaptors: `starts_on`, `continues_on`, `on`, `affine`,
  `transfer_just`, policy-shaped serial `bulk`, `bulk_chunked`, and
  `bulk_unchunked`;
- composition: `into_variant`, `when_all`, `when_all_with_variant`, `split`,
  `associate`, `spawn`, `spawn_future`;
- consumers: `sync_wait`, `sync_wait_with_variant`;
- stop-token support: `inplace_stop_source/token/callback`,
  `never_stop_token`, `any_stop_token`;
- coroutine bridge: `as_awaitable`, `with_awaitable_senders`;
- scopes: `simple_counting_scope`, stop-aware `counting_scope`;
- domain dispatch: receiver-env start-domain selection plus sender-env
  completion-domain recursive `transform_sender` during `connect` and
  completion-signature computation.

Several older audit notes are therefore closed and should not be carried forward
as open work: identity-only domain dispatch, single-shape `sync_wait`,
non-stop-aware `counting_scope`, and incomplete `when_all` cartesian value
signatures.

## working-draft coverage matrix

| Surface | Status | Evidence / remaining gap |
| --- | --- | --- |
| `just`, `just_error`, `just_stopped` | Implemented | `just.hpp`; covered by MVP/wave tests. |
| `read_env`, `write_env` | Implemented subset | `read_env.hpp`, `write_env.hpp`; env query forwarding is tag-invoke based in this backport. |
| `then`, `upon_error`, `upon_stopped` | Implemented | `then.hpp`, `upon.hpp`; exception fallback reports `std::exception_ptr` where supported. |
| `let_value`, `let_error`, `let_stopped` | Implemented | `let.hpp`; lifecycle-sensitive storage is covered by execution adaptor tests. |
| `starts_on`, `continues_on`, `transfer_just` | Implemented subset | `on.hpp`, `continues_on.hpp`; schedule errors are included in completion signatures and runtime tests. |
| `on` | Implemented subset | `on.hpp` exposes both current-WD forms. The closure form requires the child attributes to expose `get_completion_scheduler<set_value_t>` through this backport's one-argument query model. |
| `bulk` | Implemented serial subset | `bulk.hpp`; accepts current-WD execution-policy-shaped calls when the underlying standard library exposes execution policy traits/objects, and executes iterations serially in the completing agent. The policy is not used to introduce parallelism. |
| `bulk_chunked`, `bulk_unchunked` | Implemented serial subset | `bulk.hpp`; accept current-WD execution-policy-shaped calls when the underlying standard library exposes execution policy traits/objects. `bulk_unchunked` matches serial `bulk`, while `bulk_chunked` uses one non-empty `[0, shape)` chunk. |
| `unstoppable` sender adaptor | Implemented | `unstoppable.hpp`; implemented as a thin `write_env` wrapper that injects `never_stop_token`. |
| `stopped_as_optional`, `stopped_as_error` | Implemented | `stopped_as.hpp`; these are practical stopped adapters used by the backport. |
| `into_variant` | Implemented | `into_variant.hpp`; reused by `sync_wait` / `when_all` value-shape handling. |
| `when_all`, `when_all_with_variant` | Implemented subset | Cartesian value signature support and outer stop propagation are implemented; keep lifecycle tests when changing shared state. |
| `split` | Implemented subset | `split.hpp`; fail-fast terminates on impossible empty result state. |
| `sync_wait`, `sync_wait_with_variant` | Implemented subset | Multiple value alternatives are supported; synchronous typed-error consumption remains a Forge `wait_result` extension rather than `sync_wait`. |
| `associate`, `spawn` | Implemented current-WD subset | Top-level association and fire-and-forget spawn are implemented; `spawn` accepts only `set_value()` / `set_stopped()` senders. |
| `spawn_future` | Implemented subset | Eager single-consumer future sender; shared state and consumer records honor `get_allocator(env)`, while `any_stop_token` callback internals remain allocator-neutral. |
| `simple_counting_scope`, `counting_scope` | Implemented current-WD-shaped subset | Token `wrap`, top-level association/spawn, stop-token injection, and async sender-returning `join()` are implemented. |
| `as_awaitable`, `with_awaitable_senders` | Implemented Forge-compatible subset | Coroutine bridge preserves historical single-value tuple behavior; multi-value alternatives use `variant<tuple<...>>`. |
| `affine` | Implemented subset | `affine.hpp`; implemented as a thin current-WD spelling over the existing `continues_on` transfer subset. |
| `get_env` | Implemented subset | Member-first with tag-invoke fallback and `empty_env` default. |
| `get_scheduler` | Implemented subset | Tag-invoke query object; does not exactly model current WD member `query(...)` wording. |
| `get_start_scheduler` | Implemented subset | Tag-invoke environment query object; `make_prop` / `write_env` forwarding tests cover the current backport query model. |
| `get_delegation_scheduler` | Implemented subset | Tag-invoke environment query object; `make_prop` / `write_env` forwarding tests cover the current backport query model. |
| `get_completion_scheduler` | Implemented subset | Tag-invoke query object; scheduler envs expose roundtrip in Forge/backport style. |
| `forwarding_query` | Implemented subset | Exposed as the current WD query with member `.query(forwarding_query)` support and Forge tag-invoke fallback; Forge query objects advertise forwarding where applicable. |
| `get_await_completion_adaptor` | Implemented subset | Tag-invoke query object exposed for coroutine environments; no default adaptor is provided. |
| `get_domain`, `get_completion_domain` | Implemented subset | Recursive `connect` transform model exists; non-default-domain `get_completion_signatures(sender, env)` recomputes through the transformed sender type before reading signatures, including rawless source senders rescued by transform. |
| `get_allocator` | Implemented subset | Tag-invoke query object; used by `spawn`/`spawn_future` allocator paths. No default allocator query is provided for `empty_env`. |
| `get_stop_token` | Implemented subset | Tag-invoke query object with `empty_env -> never_stop_token` fallback. |
| `get_forward_progress_guarantee` | Implemented subset | Tag-invoke scheduler query object with `weakly_parallel` fallback for local scheduler-shaped types; built-in backport schedulers and `forge::static_thread_pool` report conservative values. |

## compatibility classification

The table below separates current-draft conformance work from Forge convenience
extensions. This is the source of truth for native handoff risk triage.

| Item | Classification | Current state | Native-handoff action |
| --- | --- | --- | --- |
| Library adaptor non-copyable lvalue `connect` | Converged standard-shaped behavior | Standard backport senders require explicit `std::move(sndr)` for move-only lvalues. | Keep tests and examples spelling explicit move; do not reintroduce destructive lvalue connect in standard paths. |
| `forge::async_scope::spawn(lvalue)` | Forge extension convenience | Destructively moves non-copyable non-const lvalue senders as a Forge runtime convenience. | Keep documented as a Forge-only extension; native-friendly examples should spell `scope.spawn(std::move(sndr))`. |
| `std::execution::ensure_started` | Removed non-WD extension name | No longer exposed by the `<execution>` backport. | Keep out of `std::execution`; add only under `forge::` if a future utility task needs it. |
| `std::execution::start_detached` | Removed non-WD extension name | No longer exposed by the `<execution>` backport; Forge runtime code uses `forge::start_detached`. | Keep standard paths on `spawn`; keep detach utility under `forge::`. |
| `std::execution::spawn` | Implemented current-WD subset | Top-level `spawn(sender, token[, env])` allocates detached state, associates through `token.try_associate()`, and starts eagerly. It accepts only `set_value()` / `set_stopped()` senders. | Keep tests aligned with current-WD fire-and-forget spelling. |
| `std::execution::counting_scope::join()` | Implemented current-WD-shaped subset | `simple_counting_scope::join()` and `counting_scope::join()` return async senders; `start()` registers the join operation and count drain completes receivers outside the scope mutex. | Keep stress coverage for last-decrement vs join-register races; do not reintroduce blocking `void join()` or start-time waits. |
| Scope-token `wrap` / `associate` / member `spawn` | Converged surface with subset semantics | Token-member `associate` / `spawn` are removed. `simple_counting_scope::token::wrap` is identity forwarding; `counting_scope::token::wrap` only injects scope stop token. Top-level `associate` / `spawn` / `spawn_future` own association. | Continue testing allocator/env and async join details; do not restore token-member helpers in `std::execution`. |
| Throwing receiver completion callbacks | Intentional unsupported boundary | `set_value`, `set_error`, and `set_stopped` must be `noexcept`; a negative compile probe enforces this. | Keep rejected unless a focused task rewrites completion dispatch. |
| Execution domain dispatch | Tested current-WD subset | `connect` applies sender completion-domain recursion followed by receiver start-domain recursion, with default-domain direct-connect preserved when both domains are default. `get_completion_signatures(sender, env)` uses the same transformed sender type before reading signatures on non-default-domain paths. | Keep coverage for recursive transforms and transformed-signature computation, including rawless source senders rescued by transform. |
| `forge::any_scheduler` | Forge local utility | Models Forge's local scheduler concept, with shared-state identity equality and backport CPO completion-scheduler roundtrip. | Native member-query scheduler roundtrip remains a forward-compat caveat. |
| `forge::wait_result` | Forge local utility | Synchronously preserves value, stopped, and closed-set typed error without throwing. | Use when typed errors must cross a synchronous boundary; it is not `std::execution::sync_wait`. |
| `forge::erased_sender` | Forge local utility | Connectable erased sender with multiple value shapes, closed-set typed errors, and bounded env/stop-token forwarding. | Keep under `forge::`; do not treat as standard execution surface. |
| Receiver env stop-token propagation | Required behavior for Forge utilities | `wait_result`, `erased_sender`, runtime senders, and IO/accel wrappers preserve receiver stop-token visibility in their supported env model. | Keep regression tests when touching type erasure or wrapper receivers. |

## remaining conformance notes

- `spawn_future` uses `get_allocator` for its shared state and consumer record.
  `any_stop_token` callback/type-erasure control blocks remain
  allocator-neutral by accepted design, because changing the standard-shaped
  erased stop-token API would increase native-handoff risk. See
  [`execution-stop-token-allocator-design.md`](execution-stop-token-allocator-design.md);
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

Latest local status for this convergence round:

- `STDEXEC_ROOT` absent: skipped with exit code 77 as intended;
- `STDEXEC_ROOT` set to a local stdexec checkout: all named checks passed;
- this remains an optional reference probe and is not promoted to the default
  verification floor.

## next useful checks

1. Keep `scripts/verify-native.sh gcc-exec` as the current libstdc++ execution
   backport lane.
2. Use `scripts/probe-stdexec-feasibility.sh` only as a local spike when a
   stdexec checkout is available; review each named check result rather than
   only the final `result=passed` line.
3. If stdexec comparison becomes valuable, write a separate taskbook for the
   adapter layer and define exactly which examples/tests must be portable across
   Forge and stdexec.

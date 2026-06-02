# `std::execution` current-WD convergence checklist

This checklist is the implementation plan for moving the execution backport
from a mixed Forge/stdexec-era surface to a current-working-draft-shaped subset.
It complements the broader
[`std::execution` conformance ledger](execution-conformance-ledger.md).

Primary draft references checked for this round:

- [`execution::spawn`](https://eel.is/c++draft/exec.spawn)
- [`execution::spawn_future`](https://eel.is/c++draft/exec.spawn.future)
- [Counting scopes](https://eel.is/c++draft/exec.counting.scopes)
- [`simple_counting_scope::token`](https://eel.is/c++draft/exec.simple.counting.token)
- [`counting_scope`](https://eel.is/c++draft/exec.scope.counting)

## target state

| Area | Current-WD target | Current Forge state | Action |
| --- | --- | --- | --- |
| `spawn` | `std::execution::spawn(sndr, token, env)` is a `void` CPO that allocates a detached operation, associates via `token.try_associate()`, and starts eagerly. | Implemented as `spawn(sndr, token[, env])`; accepts `set_value()` / `set_stopped()` senders. | Keep examples/tests on top-level `spawn`; do not route standard paths through token-member helpers. |
| `spawn_future` | Uses `token.wrap(sndr)`, allocator from `env` or wrapped sender env, eager state, cancellation-on-abandon, and consumer stop callback. | Implemented with eager state, shared-state allocator support, and allocator-aware consumer record allocation; `any_stop_token` callback/type-erasure internals remain allocator-neutral. | Keep the remaining allocator gap scoped to stop-token type erasure rather than `spawn_future` state ownership. |
| `simple_counting_scope::token::wrap` | Returns `std::forward<Sender>(snd)`; it does not create an association. | Implemented as identity forwarding. | Keep association in top-level algorithms. |
| `counting_scope::token::wrap` | Returns `stop-when(std::forward<Sender>(snd), scope stop token)`. | Implemented as stop-token env injection without association ownership. | Keep association in top-level algorithms. |
| `scope_token::associate` | No token-member `associate` in the current target surface. | Removed from scope tokens; top-level `associate(sender, token)` is implemented. | Keep token surface narrow. |
| `scope_token::spawn` | No token-member `spawn` in the current target surface. | Removed from scope tokens; top-level `spawn(sender, token[, env])` is implemented. | Keep examples/tests on top-level `spawn`. |
| `simple_counting_scope::join` / `counting_scope::join` | Return senders produced from a scope-join sender shape. | Async sender-returning join is implemented; `start()` registers the join operation and returns without blocking. | Keep join tests covering zero-count, registered waiter, and single-thread composition cases. |
| `ensure_started` | Not current-WD `[exec]` surface. | Removed from the `<execution>` backport. | Keep out of `std::execution`; add only under `forge::` if a future utility task needs it. |
| `start_detached` | Not current-WD `[exec]` surface. | Removed from the `<execution>` backport; `forge::start_detached` carries the utility behavior. | Keep standard paths on `spawn`; keep detach utility under `forge::`. |
| Non-copyable lvalue sender convenience | Native-shaped code requires explicit `std::move(sndr)`. | Standard-shaped backport paths now require explicit move for non-copyable lvalue senders; `forge::async_scope` retains a documented Forge-only convenience. | Keep examples/tests spelling explicit `std::move`; do not reintroduce destructive lvalue connect in standard paths. |
| Domain dispatch | Full recursive current-WD model. | `connect` now applies sender completion-domain recursion followed by receiver start-domain recursion; completion-signature queries still do not fully recompute through transformed senders. | Keep `connect` tests; treat transformed completion-signature recomputation as the remaining focused gap. |

## standard-surface cleanup order

1. Implement enough `spawn` support to replace standard-path `start_detached`
   usage coherently.
2. Keep examples and tests on top-level `spawn` / `associate` spelling,
   keeping `join()` in sender-consuming form.
3. Keep `ensure_started` and `start_detached` out of `std::execution` public
   surface.
4. Keep destructive-move lvalue convenience out of standard backport adaptors.
5. Re-run `spawn_future` tests after token `wrap` semantics change, because
   the current draft calls `token.wrap(sndr)` before allocator/env selection.

## focused verification map

| Change | Focused checks |
| --- | --- |
| `spawn` / scope / join | `execution_counting_scope`, `execution_spawn_future`, `execution_api_core`, `gcc-exec`, `tsan`, `asan` |
| Non-WD surface removal or migration | `execution_wave1`, `execution_wave2`, `execution_api_core`, affected `forge_` tests |
| Lvalue move cleanup | `execution_adaptors`, `execution_wave1`, `execution_spawn_future`, `forge_async_scope`, `forge_task` |
| Domain recursion | `execution_domain`, `execution_adaptors`, `execution_wave1`, `gcc-exec` |
| `spawn_future` allocator audit | `execution_spawn_future`, `gcc-exec`, `tsan`, `asan` |

## privacy and platform notes

This round does not depend on private hardware, private Windows paths, or
vendor accelerator SDKs. If Windows-sensitive scripts or IOCP code are touched,
run the documented Windows matrix without committing hostnames or local install
paths.

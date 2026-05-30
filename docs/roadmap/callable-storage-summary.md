# Forge callable storage cleanup

## Goal

Close the largest remaining resource-policy gap in `forge::static_thread_pool`:
its queue currently stores `std::function<void()>`, so the configured
`std::pmr::memory_resource*` controls deque nodes but not necessarily the
callable target allocation.

This round should replace that internal queue item with a small Forge-owned,
move-only callable record allocated from the pool memory resource.

## Scope

- Add an internal, non-public callable record for `void()` tasks.
- Use it in `static_thread_pool` instead of `std::function<void()>`.
- Keep the existing public API and scheduler behavior unchanged.
- Preserve default constructor behavior and bounded-queue semantics.
- Update docs so the old `std::function` caveat is removed for the pool.

## Non-Goals

- No public `forge::move_only_function` API in this round.
- No changes to `timer_context` callback storage.
- No allocator-aware `async_scope` op-state rewrite.
- No typed-error erased sender changes.
- No changes to standard backport headers.

## Acceptance

- Existing thread-pool, runtime, resource-context, strand, IO, and accel tests
  stay green.
- A focused resource-policy test proves custom pool memory observes queued task
  storage as well as queue nodes.
- ASan/TSan stay clean for the forge utility subset.

## Taskbooks

1. `docs/roadmap/callable-storage-taskbook-a-contract.md`
2. `docs/roadmap/callable-storage-taskbook-b-implementation.md`
3. `docs/roadmap/callable-storage-taskbook-c-docs-verification.md`

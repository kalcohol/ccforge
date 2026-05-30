# Taskbook A: contract and test shape

## Objective

Define the exact boundary before implementation: this is an internal pool queue
storage change, not a new public function-wrapper facility.

## Current Facts

- `static_thread_pool` stores `std::pmr::deque<std::function<void()>>`.
- `static_thread_pool_options::memory` is already plumbed into the deque.
- Docs correctly say `std::function` target allocations are not controlled.

## Contract

1. Public API remains unchanged:
   - `static_thread_pool_options`
   - `static_thread_pool::scheduler`
   - `schedule(pool.get_scheduler())`
2. Queue capacity semantics remain unchanged:
   - full queue -> `set_stopped`;
   - shutdown after accept drains accepted work;
   - shutdown before accept rejects future work.
3. Allocation failure while preparing an accepted task must not escape
   `operation_state::start()` because `start()` is `noexcept`. It may complete
   the schedule operation with stopped.
4. Internal task storage is move-only and resource-backed.

## Tests

- Existing pool tests remain the primary regression surface.
- Add or strengthen a resource-policy test that creates pending queued work with
  a custom counting resource and verifies allocations are observed and released.
- Keep sanitizer coverage through the existing `forge_` test name.

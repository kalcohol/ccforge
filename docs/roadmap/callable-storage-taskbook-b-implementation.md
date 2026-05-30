# Taskbook B: implement pool callable storage

## Objective

Replace the `std::function<void()>` queue item with a minimal internal move-only
callable record allocated from the pool memory resource.

## Implementation Steps

1. Add a private `__pool_detail::__task` type in `include/forge/static_thread_pool.hpp`.
2. Store:
   - erased object pointer;
   - invoke/destroy function pointers;
   - owning `std::pmr::memory_resource*`.
3. Allocate the concrete callable object through `memory_resource::allocate`.
4. Destroy and deallocate through the same resource.
5. Make `__task` move-only and default-constructible so worker threads can move
   a queued task into a local variable.
6. Change the queue to `std::pmr::deque<__pool_detail::__task>`.
7. Construct queued tasks only after shutdown/capacity checks pass.
8. If allocation/construction fails in `start()`, complete stopped.

## Guardrails

- Do not make `__task` public.
- Do not add small-buffer optimization in this round; heap-first is simpler to
  audit and proves resource routing.
- Do not change worker shutdown/drain behavior.
- Do not add exceptions to the schedule completion signatures.

## Risks

- `start()` is `noexcept`; allocation failure must be contained.
- Task destruction must be correct when queued work is dropped during shutdown.
- Worker local move must leave the deque item empty before pop/destruction.

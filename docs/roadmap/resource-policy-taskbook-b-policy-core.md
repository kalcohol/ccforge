# Taskbook B: Resource Policy Core

## Objective

Introduce the minimal public resource policy vocabulary used by runtime primitives.
V1 should be practical and boring: `std::pmr::memory_resource*`, default resource,
aggregate-friendly options, no broad policy framework.

## Current Facts

- `static_thread_pool_options` already exists with `thread_count` and `queue_capacity`.
- `bounded_channel` currently takes capacity directly.
- Several primitives allocate records using standard containers, `new`, `make_shared`, or
  `std::function`.
- `std::function` target allocation is not controlled by `pmr` in the current pool design.

## Proposed Header

Candidate:

```cpp
#include <forge/resource_policy.hpp>
```

Candidate types:

```cpp
namespace forge {

struct resource_policy {
    std::pmr::memory_resource* memory = std::pmr::get_default_resource();
};

[[nodiscard]] auto default_memory_resource() noexcept
    -> std::pmr::memory_resource*;

} // namespace forge
```

If a separate `resource_policy` type feels too ceremonial during implementation, keep only shared
helpers and options fields. Do not add abstraction unless it removes real duplication.

## Steps

1. Add `include/forge/resource_policy.hpp`.
2. Include `<memory_resource>` and provide a tiny helper for null-to-default normalization if useful.
3. Add unit tests for default resource selection and custom counting resource plumbing.
4. Add the header to `<forge/execution.hpp>` only if it is stable and dependency-free.
5. Document V1 limitations: no promise that every standard-library internal allocation is captured.

## Tests

Create a small `counting_resource` test helper in `test/forge/runtime/` or local test file:

- counts allocations and deallocations;
- forwards to `std::pmr::new_delete_resource()`;
- is thread-safe when used under pool/channel tests.

Avoid global allocator interposition.

## Risks

- A policy object that stores raw `memory_resource*` does not own the resource. Docs must say the
  resource must outlive primitives using it.
- If options accept null, behavior must be defined. Prefer normalizing null to
  `std::pmr::get_default_resource()`.

## Verification

```bash
cmake --build build/local --target test_forge_resource_policy
ctest --test-dir build/local -R 'forge_resource_policy' --output-on-failure
```

## Done When

- A stable, documented resource policy vocabulary exists.
- Tests prove custom `memory_resource` can be observed.
- No existing runtime constructor changes behavior.

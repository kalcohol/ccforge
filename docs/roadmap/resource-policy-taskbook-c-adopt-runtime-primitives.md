# Taskbook C: Adopt Resource Policy In Runtime Primitives

## Objective

Adopt the V1 memory policy in runtime primitives where it gives immediate value, while preserving
existing constructors and clearly documenting partial coverage.

## Priority Order

1. `static_thread_pool`
2. `bounded_channel`
3. `strand`
4. `async_scope`
5. `resource_context`

Stop after each primitive if the diff starts to grow. Small commits are preferred over a large
all-at-once migration.

## static_thread_pool

Current state:

- queue is `std::deque<std::function<void()>>`;
- options already include `thread_count` and `queue_capacity`;
- default constructor delegates to options constructor and must remain source-compatible.

Plan:

- add `std::pmr::memory_resource* memory` to `static_thread_pool_options`;
- consider `std::pmr::deque<std::function<void()>>` for queue node allocation;
- keep old constructor path equivalent to default options;
- document that `std::function` target allocations are not fully controlled in V1.

Tests:

- default constructor behavior unchanged;
- default-resource path remains behaviorally equivalent and does not add observable blocking or
  capacity behavior changes;
- bounded queue tests still pass;
- existing pool/single-thread/runtime/system/resource_context tests stay green;
- custom counting resource observes queue allocation under pressure.

## bounded_channel

Current state:

- buffer and pending queues use `std::deque`;
- pending operation records use `shared_ptr`;
- action batches use `std::vector`;
- constructor takes capacity directly.

Plan:

- add `bounded_channel_options{capacity, memory}`;
- preserve `bounded_channel(std::size_t capacity)`;
- use pmr containers for buffer/pending/action storage where practical;
- use `std::allocate_shared` for records/control blocks where it does not complicate lifetime.

Tests:

- move-only values still work;
- close-drain and request_stop semantics unchanged;
- custom resource sees allocations/deallocations for buffered/pending paths;
- ASAN/TSAN cover send/recv races.

## strand

Current state:

- queue is `std::deque<std::shared_ptr<record_base>>`;
- pending stop collection uses `std::vector`;
- records are heap allocated.
- runner node uses intrusive refcount keepalive, similar in spirit to `async_scope`.

Plan:

- add `strand_options{memory}` or constructor overload accepting memory policy;
- use pmr queue/vector and `allocate_shared` for records/state if straightforward;
- keep runner node allocation out of V1 pmr coverage by default; do not rewrite its intrusive
  keepalive unless the change is trivial and sanitizer-reverified;
- keep `strand{scheduler}` constructor unchanged.

Tests:

- FIFO and no-overlap tests unchanged;
- reentrant scheduling remains serial;
- custom resource observes pending record allocation.

## async_scope

Current state:

- scope state uses `std::make_shared`;
- spawned operation node uses raw `new` plus intrusive refcount keepalive;
- this is lifetime-sensitive because it supports synchronous completion from `forge::task`.

Plan:

- default decision: defer allocator-aware spawned op-state nodes in V1;
- document spawned op-state allocation as a known not-yet-policy-controlled path;
- only adopt pmr here if the change is mechanically small, preserves the intrusive refcount semantics
  exactly, and adds synchronous-completion regression tests before implementation.

Tests:

- `forge::task` synchronous completion remains safe;
- first error capture unchanged;
- ASAN/TSAN pass.

## resource_context

Current state:

- combines `runtime_context` and `async_scope`;
- good place to pass one policy into owned runtime pieces.

Plan:

- add options only after pool/channel/scope policy shape is stable;
- preserve existing constructor behavior;
- document policy lifetime: memory resource must outlive the context.

## Risks

- It is easy to overstate allocation control. V1 must say "policy-aware paths" rather than
  "all allocations".
- `shared_ptr` allocator propagation and op-state self-lifetime are subtle. Prefer correctness over
  full coverage.
- Do not trade the already-verified `async_scope` / strand runner keepalive model for marginal V1
  allocation coverage.
- Do not introduce a template explosion just to make allocation configurable.

## Verification

Focused after each primitive:

```bash
cmake --build build/local --target test_forge_thread_pool test_forge_channel test_forge_strand test_forge_async_scope
ctest --test-dir build/local -R 'forge_thread_pool|forge_channel|forge_strand|forge_async_scope' --output-on-failure
```

Before committing lifecycle-sensitive changes:

```bash
scripts/verify-native.sh tsan asan
```

## Done When

- Adopted primitives accept policy/options without breaking old constructors.
- Tests prove custom resources are used on meaningful paths.
- Documentation states exact coverage and caveats.

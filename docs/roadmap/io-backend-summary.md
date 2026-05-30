# Forge IO backend round

This round starts the IO part of the Forge runtime vision with a deliberately
small Linux fd readiness backend.

The goal is not to become a full networking library. The goal is to provide a
sender/receiver-friendly bridge from real OS readiness events into the existing
Forge lifecycle primitives: `resource_context`, `async_scope`, `bounded_channel`,
`strand`, `erased_sender`, and `resource_policy`.

## Scope

Implement V1 under `namespace forge::io`.

Target surface:

```cpp
namespace forge::io {

enum class readiness {
    read,
    write
};

struct context_options {
    std::pmr::memory_resource* memory = forge::default_memory_resource();
    std::size_t max_events = 64;
};

class context {
public:
    explicit context(context_options = {});
    ~context() noexcept;

    context(const context&) = delete;
    context& operator=(const context&) = delete;

    auto readable(int fd);
    auto writable(int fd);

    void cancel(int fd) noexcept;
    void close() noexcept;
    void request_stop() noexcept;
    void shutdown() noexcept;
    void wait() noexcept;
};

} // namespace forge::io
```

Names may change during implementation if the codebase points to a clearer local
style. Keep the public names short and practical. Do not put IO extensions in
`namespace std`.

## Mandatory Decisions

1. V1 is Linux-only and uses `epoll` plus `eventfd`.
2. `FORGE_ENABLE_FORGE_IO=AUTO` enables the backend only when the platform
   support exists. `ON` without support is a configure error. `OFF` skips IO
   tests/examples.
3. Windows IOCP, macOS/BSD kqueue, and Linux `io_uring` are explicitly deferred.
   Zig may help build or bind C ABI code later, but it does not erase these
   platform semantic differences.
4. V1 exposes readiness senders only. It does not expose async read/write buffer
   operations yet.
5. FDs are borrowed. The caller owns them and must keep them open until pending
   readiness operations complete or are cancelled/drained.
6. V1 supports context-level cancellation and start-time receiver stop
   observation. Per-operation stop callbacks for already-enqueued idle waiters
   are deferred unless the ownership model is proven and sanitizer-covered.
7. At most one pending readable waiter and one pending writable waiter per fd are
   supported in V1. Duplicate waiters complete with `set_error(std::exception_ptr)`.
8. Completion callbacks must not run while holding the context mutex.
9. Resource policy controls context-owned queues/records/event buffers where
   practical. Do not claim full allocation control unless tests prove it.

## Non-Goals

- No general socket abstraction.
- No owning fd wrapper beyond examples using existing RAII facilities.
- No `io_uring` submission queue.
- No Windows IOCP backend.
- No kqueue backend.
- No full async read/write semantics.
- No typed-error erased sender changes in this round.
- No changes to standard backport headers unless a compile-only include issue is
  discovered.

## Taskbooks

1. `docs/roadmap/io-backend-taskbook-a-gates-contract.md`
2. `docs/roadmap/io-backend-taskbook-b-linux-epoll-context.md`
3. `docs/roadmap/io-backend-taskbook-c-readiness-senders.md`
4. `docs/roadmap/io-backend-taskbook-d-docs-examples-verification.md`

## Sequencing

Taskbook A is a gating and contract pass. It should not add backend logic.

Taskbook B builds the Linux event-loop ownership model and verifies lifecycle
behavior before public senders depend on it.

Taskbook C adds sender/receiver surface and detailed correctness tests.

Taskbook D makes the feature teachable with examples and final documentation.

## Verification Baseline

Focused local verification after relevant taskbooks:

```bash
cmake -S . -B build/local -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON
cmake --build build/local --target test_forge_io_context forge_io_readiness
ctest --test-dir build/local -R 'forge_io|forge_execution_header' --output-on-failure
```

Full final verification:

```bash
scripts/verify-native.sh llvm zig local tsan asan
```

If CMake gate logic changes substantially, also verify:

```bash
cmake -S . -B build/io-off -DFORGE_BUILD_TESTS=ON -DFORGE_ENABLE_FORGE_IO=OFF
cmake -S . -B build/io-on -DFORGE_BUILD_TESTS=ON -DFORGE_ENABLE_FORGE_IO=ON
```

On non-Linux hosts, `FORGE_ENABLE_FORGE_IO=AUTO` should configure with IO skipped;
`ON` should fail with a clear message.

## Acceptance Criteria

- Linux fd readiness senders work with real pipe/socketpair/eventfd descriptors.
- `close`, `request_stop`, `shutdown`, `wait`, and destructor behavior match
  `docs/forge-runtime.md`.
- No user completion runs under the context mutex.
- Pending readiness operations are completed exactly once.
- FD ownership limits are documented and examples show the correct RAII pattern.
- Sanitizer subsets include the new `forge_io` tests.

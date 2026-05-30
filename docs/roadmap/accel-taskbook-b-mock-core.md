# Taskbook B: Mock Accel Core

## Objective

Implement the portable core objects for `forge::accel`: context, queue, event
state, and mock device buffers. This taskbook should establish lifecycle,
ordering, resource-policy, and capacity behavior before copy/kernel senders are
added.

## Proposed Files

- `include/forge/accel.hpp`
- `include/forge/accel/context.hpp`
- `include/forge/accel/buffer.hpp`
- `include/forge/accel/event.hpp`
- `include/forge/accel/detail/...` only where it keeps public headers readable

Avoid a large single header if it becomes difficult to audit, but do not split
tiny types into many files just to create structure.

## Core Shape

```cpp
namespace forge::accel {

struct context_options {
    std::size_t thread_count = 1;
    std::optional<std::size_t> queue_capacity = std::nullopt;
    std::pmr::memory_resource* memory = forge::default_memory_resource();
};

class context {
public:
    explicit context(context_options = {});
    ~context() noexcept;

    context(const context&) = delete;
    context& operator=(const context&) = delete;

    queue get_queue();

    void close() noexcept;
    void request_stop() noexcept;
    void shutdown() noexcept;
    void wait() noexcept;
};

class queue {
public:
    [[nodiscard]] bool closed() const noexcept;
};

template<class T>
class device_buffer {
public:
    explicit device_buffer(context&, std::size_t size);
    [[nodiscard]] std::size_t size() const noexcept;
};

class event;

} // namespace forge::accel
```

Exact names can change during implementation, but keep aggregate options and
small handle types.

## Implementation Direction

1. Reuse Forge runtime primitives where they fit:
   - `resource_context` for owned CPU execution;
   - `strand` or equivalent queue state for FIFO command serialization;
   - `resource_policy` for queue records and mock device storage.
2. Keep V1 single-queue unless a multi-queue design is nearly free. Multiple
   hardware streams are not required.
3. Make queue capacity explicit:
   - no capacity means unbounded;
   - capacity full should reject a new command with stopped completion in
     Taskbook C;
   - count accepted but not yet completed command records.
4. Store mock device data in policy-aware storage, such as `std::pmr::vector<T>`
   or byte storage with typed views.
5. Restrict `device_buffer<T>` to safe value categories if needed. A
   `std::is_trivially_copyable_v<T>` restriction is acceptable for V1 and fits
   H2D/D2H semantics.
6. Normalize null memory resources to `forge::default_memory_resource()`.

## Lifecycle Requirements

- `close()` rejects future commands but lets accepted commands drain.
- `request_stop()` requests cooperative cancellation and completes not-yet-run
  commands stopped when possible.
- `shutdown()` is close + stop.
- `wait()` must not execute user completions while holding internal locks.
- Destructor performs shutdown + wait.
- Calling shutdown/wait repeatedly is safe.
- No operation should touch a destroyed context state. Use shared state handles
  for queued command records.

## Tests

Add `test/forge/accel/test_forge_accel_context.cpp` with focused cases:

- context constructs and destructs without explicit shutdown;
- queue preserves FIFO order on the mock backend;
- close rejects later command records but drains accepted records;
- request_stop stops pending records;
- queue capacity full produces the selected stopped path;
- custom counting memory resource observes buffer and record allocations;
- repeated shutdown/wait is harmless;
- no user completion runs under internal locks, tested with a reentrant submit if
  the public sender surface exists by the end of this taskbook.

If command senders are not yet present, use a temporary internal test hook only
if it does not leak into the public API. Prefer adding the first minimal command
sender in Taskbook C over creating a broad test-only API.

## Risks

- Reimplementing a second runtime instead of reusing existing primitives.
- Hiding capacity or resource-policy behavior until later command senders make
  it harder to test.
- Letting a buffer operation capture raw context pointers instead of shared
  state handles.

## Verification

```bash
cmake -S . -B build/local -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON
cmake --build build/local --target test_forge_accel_context
ctest --test-dir build/local -R '^forge_accel_context' --output-on-failure
```

Run sanitizer subsets after the first queued command path exists:

```bash
scripts/verify-native.sh tsan asan
```

## Commit

Suggested commit:

```text
feat(forge): add portable accel mock context
```

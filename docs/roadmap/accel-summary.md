# Forge Accel Support Round

This round starts the `forge::accel` part of the Forge runtime vision.

The goal is not to bind CUDA, HIP, SYCL, Vulkan, OpenCL, FPGA SDKs, or NPU
driver APIs. The goal is to build a small, portable support layer that can model
the common structure those systems need: command queues, fences/events,
host/device-style buffers, copy operations, kernel-like submissions, resource
policy, cancellation, and drain.

V1 should use a mock/in-memory backend. That backend is not fake test code; it is
the executable semantic model for the API. Vendor/platform backends can be
considered later only after this model proves useful and stable.

## Scope

Implement V1 under `namespace forge::accel`.

Target public shape:

```cpp
namespace forge::accel {

struct context_options {
    std::size_t thread_count = 1;
    std::optional<std::size_t> queue_capacity = std::nullopt;
    std::pmr::memory_resource* memory = forge::default_memory_resource();
};

class context;
class queue;
class event;

template<class T>
class device_buffer;

template<class T>
auto copy_to_device(queue&, device_buffer<T>& dst, std::span<const T> src);

template<class T>
auto copy_to_host(queue&, std::span<T> dst, const device_buffer<T>& src);

template<class T>
auto copy_device_to_device(queue&, device_buffer<T>& dst, const device_buffer<T>& src);

template<class F>
auto submit(queue&, F&& command);

auto record_event(queue&);
auto wait_event(queue&, event);

} // namespace forge::accel
```

Names may change during implementation if local code shows a clearer shape, but
keep the API small, C++-idiomatic, and easy to explain from examples.

## Mandatory Decisions

1. V1 is a portable mock/in-memory backend. It must not use CUDA, HIP, SYCL,
   OpenCL, Vulkan, vendor SDKs, driver headers, or CMake language enablement for
   accelerators.
2. `forge::accel` depends on Forge runtime/resource facilities. It may reuse
   `resource_context`, `strand`, `bounded_channel`, `any_scheduler`, and
   `resource_policy` instead of inventing a separate runtime.
3. `FORGE_ENABLE_FORGE_ACCEL=AUTO` enables the mock backend when the required
   Forge runtime/resource gates are enabled. `ON` requires those gates; `OFF`
   skips accel tests/examples.
4. Completion signatures are bounded:
   - `set_value_t()` for copy and kernel-like commands;
   - `set_value_t(event)` for `record_event`, if this remains the clearest event
     shape during implementation;
   - `set_error_t(std::exception_ptr)`;
   - `set_stopped_t()`.
5. Error typing remains `std::exception_ptr` in this round. Typed-error erasure
   is a later project.
6. Queue capacity full completes stopped, matching the existing bounded runtime
   style unless implementation finds a stronger local precedent.
7. Host spans are borrowed and must outlive the submitted operation. Device
   buffers are owning handles and must outlive operations using them.
8. Commands complete exactly once. User completions must not run while holding
   internal mutexes.
9. `context` is an owning runtime object. Destructor performs shutdown + wait
   and may block, consistent with `docs/forge-runtime.md`.
10. The mock backend must be portable enough for future Windows base testing. No
    platform-specific code should be needed for V1.

## Non-Goals

- No real GPU/NPU/FPGA execution.
- No CUDA/HIP/SYCL namespace or backend.
- No kernel compilation, module loading, graph capture, streams from vendor
  libraries, pinned memory, unified memory, DMA, or command buffer recording.
- No tensor library and no neural-network operators.
- No async read/write integration with IO backend in this round.
- No typed-error erased sender changes.
- No changes to standard backport headers.

## Taskbooks

1. `docs/roadmap/accel-taskbook-a-contract-gates.md`
2. `docs/roadmap/accel-taskbook-b-mock-core.md`
3. `docs/roadmap/accel-taskbook-c-command-senders.md`
4. `docs/roadmap/accel-taskbook-d-docs-examples-verification.md`

## Sequencing

Taskbook A fixes the gate and lifecycle contract before public code exists.

Taskbook B builds the portable mock backend core: context, queue, device buffer,
resource policy, lifecycle, and capacity behavior.

Taskbook C adds command senders: copies, kernel-like submit, and the smallest
useful event/fence shape.

Taskbook D makes the feature teachable with examples and final documentation.

Each taskbook should land as one or more focused commits. Do not accumulate a
large mixed implementation diff.

## Verification Baseline

Focused local verification after implementation:

```bash
cmake -S . -B build/local -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON
cmake --build build/local --target test_forge_accel_context test_forge_accel_copy
ctest --test-dir build/local -R '^forge_accel' --output-on-failure
```

Gate checks:

```bash
cmake -S . -B build/accel-off -DFORGE_BUILD_TESTS=ON -DFORGE_ENABLE_FORGE_ACCEL=OFF
ctest --test-dir build/accel-off -N -R '^forge_accel'

cmake -S . -B build/accel-on -DFORGE_BUILD_TESTS=ON -DFORGE_ENABLE_FORGE_ACCEL=ON
ctest --test-dir build/accel-on -N -R '^forge_accel'
```

Full final verification:

```bash
scripts/verify-native.sh llvm zig local tsan asan
```

When a Windows machine is available, V1 accel mock tests should be part of the
base non-IO Forge verification because they require no vendor drivers.

## Acceptance Criteria

- `forge::accel` works without platform/vendor accelerator dependencies.
- Queue order, capacity, stop, shutdown, wait, and destructor behavior match the
  documented Forge runtime vocabulary.
- Copy operations move values through real mock device buffers.
- Kernel-like submissions can mutate mock device buffers and compose with CPU
  continuations.
- Examples teach ownership, borrowed spans, drain, cancellation, and resource
  policy clearly.
- Sanitizer subsets include the new `forge_accel` tests.

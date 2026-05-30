# Taskbook A: Accel Contract And Gates

## Objective

Turn the existing accel placeholder gate into an executable policy for a
portable mock backend, and record the V1 contract before implementation.

## Current Facts

- `FORGE_ENABLE_FORGE_ACCEL` already exists as a tri-state cache option.
- `FORGE_TEST_ENABLE_FORGE_ACCEL` already exists as a test gate.
- There is no `include/forge/accel/` or `include/forge/accel.hpp` yet.
- `docs/roadmap/forge-runtime-vision.md` chooses `forge::accel` as the short,
  neutral namespace for accelerator-like support.
- Existing lifecycle terms are fixed in `docs/forge-runtime.md`.
- Resource policy is available through `forge::resource_policy` and
  `forge::default_memory_resource()`.

## Required Decisions

1. `AUTO`:
   - enable accel V1 when Forge runtime and resource-policy facilities are
     enabled;
   - do not probe CUDA, HIP, SYCL, OpenCL, Vulkan, FPGA SDKs, NPU SDKs, or driver
     headers.
2. `ON`:
   - require the Forge runtime/resource dependencies;
   - produce a clear configure error if those dependencies are disabled.
3. `OFF`:
   - skip accel tests/examples;
   - do not include accel in aggregate example/test targets.
4. Add an internal CMake variable such as `FORGE_HAS_FORGE_ACCEL_BACKEND` or
   `FORGE_HAS_FORGE_ACCEL_MOCK_BACKEND` for tests/examples.
5. Keep the mock backend portable. It should not branch on Linux/Windows/macOS.

## Contract To Record

- `forge::accel::context` is an owning runtime primitive.
- `context::shutdown()` closes ingress, requests stop, and drains accepted
  command work.
- `context::wait()` blocks until accepted command work is completed or stopped.
- `context` destruction performs shutdown + wait and may block.
- `queue` is a lightweight handle/view into a context-owned command queue.
- V1 queues are FIFO and single-command-at-a-time. Parallel streams are deferred.
- Host spans passed to copy operations are borrowed and must outlive operation
  completion.
- `device_buffer<T>` owns mock device storage and must outlive operations using
  it.
- V1 commands complete with `set_error(std::exception_ptr)` for thrown
  exceptions or contract violations.
- Queue capacity full completes stopped.
- Receiver stop-token support may be start-time only unless a stronger
  stop-callback model is implemented and sanitizer-covered.

## CMake Direction

1. Keep the tri-state helper style already used in `forge.cmake`.
2. Do not add vendor dependency probes.
3. Add accel tests only when both:
   - `FORGE_TEST_ENABLE_FORGE_ACCEL=ON`;
   - accel backend is enabled.
4. Ensure sanitizer scripts continue to pick up tests through the `forge_` test
   prefix.
5. Update `docs/testing.md` so accel is no longer described as a placeholder
   once Taskbook B lands.

## Tests

Add configure-level checks:

- `FORGE_ENABLE_FORGE_ACCEL=OFF` configures and registers no `forge_accel` tests.
- `FORGE_ENABLE_FORGE_ACCEL=ON` configures when runtime/resource gates are on.
- `AUTO` behaves like enabled on normal Forge test builds because V1 has no
  external backend dependency.

If runtime/resource gates are explicitly disabled, `FORGE_ENABLE_FORGE_ACCEL=ON`
should fail with a clear message and `AUTO` should skip.

## Risks

- Accidentally adding CUDA/HIP/SYCL probes would turn this support layer into a
  vendor integration round.
- Registering accel tests without the `forge_` prefix would silently drop
  sanitizer coverage.
- Enabling accel while runtime/resource dependencies are disabled would produce
  confusing compile errors later.

## Verification

```bash
cmake -S . -B build/accel-auto -DFORGE_BUILD_TESTS=ON
ctest --test-dir build/accel-auto -N -R '^forge_accel'

cmake -S . -B build/accel-off -DFORGE_BUILD_TESTS=ON -DFORGE_ENABLE_FORGE_ACCEL=OFF
ctest --test-dir build/accel-off -N -R '^forge_accel'

cmake -S . -B build/accel-on -DFORGE_BUILD_TESTS=ON -DFORGE_ENABLE_FORGE_ACCEL=ON
```

## Commit

Suggested commit:

```text
build(forge): enable portable accel gate
```

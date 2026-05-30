# Taskbook D: Accel Docs, Examples, Verification

## Objective

Make `forge::accel` teachable. This round should show how modern C++ sender
composition expresses command queues, buffers, copies, kernel-like work,
resource lifetime, cancellation, and drain without relying on vendor APIs.

## Required Examples

1. `example/forge_accel_copy_example.cpp`
   - create an accel context and queue;
   - allocate a mock device buffer;
   - copy host data to device and back;
   - verify the round trip;
   - show `shutdown()`/`wait()` or RAII ownership clearly.

2. `example/forge_accel_pipeline_example.cpp`
   - H2D copy;
   - kernel-like `submit` that transforms the device buffer;
   - D2H copy;
   - CPU continuation using existing execution adaptors;
   - clear comments around borrowed spans and buffer lifetime.

3. `example/forge_inference_runtime_sketch.cpp`
   - combine `bounded_channel`, `resource_context`, `async_scope`, `strand`, and
     `accel::queue`;
   - model request ingestion, ordered per-session control, accel command work,
     and graceful shutdown;
   - keep the code small enough to read as an example. If it grows into an
     application, split or defer it rather than shipping a confusing demo.

Examples should be practical, not API tours. The reader should be able to see
who owns each resource, when cancellation is requested, and when work is drained.

## Docs

Add or update:

- `docs/forge-accel.md`
- `docs/forge-runtime.md`
- `docs/forge-utilities.md`
- `docs/testing.md`
- `README.md` public feature summary
- `docs/roadmap/forge-runtime-vision.md` if implementation diverges

Docs must state:

- V1 is a mock/in-memory backend and does not perform hardware acceleration.
- No CUDA/HIP/SYCL/vendor dependency is required.
- Host spans are borrowed.
- Device buffers are owning mock storage.
- Resource policy controls documented queues/records/buffers, not every possible
  standard-library allocation.
- Queue capacity full completes stopped.
- Error paths use `std::exception_ptr`.
- Post-enqueue receiver stop-token cancellation is either supported and tested
  or explicitly deferred.
- Windows base support expectation: mock accel should be part of future Windows
  non-IO verification because it has no platform dependency.

## CMake

- Add accel tests/examples under `FORGE_ENABLE_FORGE_ACCEL` and
  `FORGE_TEST_ENABLE_FORGE_ACCEL`.
- Keep target names prefixed with `forge_`.
- Do not add CUDA/HIP/SYCL language enables or package probes.
- Do not make non-accel examples depend on accel.
- Gate-off configure should register zero `forge_accel` tests.

## Verification

Focused:

```bash
cmake -S . -B build/local -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON
cmake --build build/local --target \
  test_forge_accel_context \
  test_forge_accel_copy \
  test_forge_accel_submit \
  forge_accel_copy \
  forge_accel_pipeline
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

Optional if CMake or backport include paths are touched:

```bash
scripts/verify-native.sh gcc16 gcc-exec
```

## Review Checklist

- No new names in `namespace std`.
- No vendor/platform dependency probes.
- No public claim of real hardware acceleration.
- Completion signatures are documented and tested.
- Queue lifecycle matches `docs/forge-runtime.md`.
- Sanitizer test counts do not drop.
- Examples compile in the same environments as other Forge examples.
- Docs explain limitations without undercutting the useful path.

## Done When

- A user can learn the intended style from examples without reading tests.
- `forge::accel` provides a credible semantic base for future CUDA/HIP/SYCL,
  FPGA, NPU, or custom-device proofs without depending on any of them.
- Full requested verification is green.

# Taskbook A: Forge Feature Gates

## Objective

Add durable CMake gates for non-backport Forge facilities so future IO, accel,
and typed-erasure work can be optional without destabilizing existing builds.

## Current Facts

- Existing tests have `FORGE_TEST_ENABLE_FORGE`.
- `include/forge/` facilities are header-only and mostly dependency-free.
- Future IO and accel backends will introduce platform/vendor dependencies.
- Directly including a dependency-free header should remain possible; gates should primarily control
  umbrella headers, examples, tests, and dependency-heavy backends.

## Proposed Gates

Feature gates:

```cmake
FORGE_ENABLE_FORGE_RUNTIME=ON
FORGE_ENABLE_FORGE_RESOURCE_POLICY=ON
FORGE_ENABLE_FORGE_IO=AUTO
FORGE_ENABLE_FORGE_ACCEL=AUTO
FORGE_ENABLE_FORGE_TYPED_ERASURE=OFF
```

Test gates:

```cmake
FORGE_TEST_ENABLE_FORGE_RUNTIME=ON
FORGE_TEST_ENABLE_FORGE_RESOURCE=ON
FORGE_TEST_ENABLE_FORGE_IO=ON
FORGE_TEST_ENABLE_FORGE_ACCEL=ON
FORGE_TEST_ENABLE_FORGE_ERASURE=ON
```

`AUTO` means enable only when dependencies are available. Explicit `ON` with missing dependencies
should fail configure with a clear message. `OFF` means do not build related tests/examples and do
not include dependency-heavy umbrella content.

For this resource-policy round, IO/accel/typed-erasure gates are placeholders only. Do not add
epoll, io_uring, IOCP, CUDA, HIP, SYCL, or compiler-driver probes until a real backend target exists.
An unused placeholder gate should not affect configure time or platform compatibility.

## Steps

1. Add cache options in the existing CMake style.
2. Keep `FORGE_TEST_ENABLE_FORGE=ON` as the parent gate.
3. Wire current Forge tests under `FORGE_TEST_ENABLE_FORGE_RUNTIME` and
   `FORGE_TEST_ENABLE_FORGE_ERASURE` without changing default build output.
4. Add placeholder gates for IO and accel but do not add backend targets or dependency probes yet.
5. Update `docs/testing.md` with the new gates and dependency semantics.
6. Add a small configure-only check or CI note that gate-off configure succeeds.

## Risks

- Over-gating direct headers would make header-only usage worse. Avoid `#error` in dependency-free
  headers.
- If tests are moved under gates incorrectly, sanitizer subsets may silently stop covering runtime
  primitives. Preserve `forge_` test names and ctest labels/regex behavior. After this taskbook,
  `scripts/verify-native.sh tsan asan` must still run at least the current 31-test sanitizer subset.

## Verification

```bash
cmake -S . -B build/gate-default -DFORGE_BUILD_TESTS=ON
cmake --build build/gate-default
ctest --test-dir build/gate-default -R '^forge_' --output-on-failure

cmake -S . -B build/gate-resource-off \
  -DFORGE_BUILD_TESTS=ON \
  -DFORGE_ENABLE_FORGE_RESOURCE_POLICY=OFF

scripts/verify-native.sh tsan asan
```

## Done When

- Default configure/build/test output is unchanged for current users.
- The sanitizer subset test count does not drop from the current 31/31 baseline.
- New gate names are documented.
- Future taskbooks can attach IO/accel/typed-erasure targets without inventing new policy.

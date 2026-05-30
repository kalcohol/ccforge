# Taskbook C: docs and verification

## Objective

Update IO docs to describe receiver stop-token cancellation accurately and run
the focused plus sanitizer verification set.

## Documentation

Update `docs/forge-io.md`:

- remove the V1 caveat that post-enqueue receiver stop requires an external
  channel/context state change;
- document that pending readiness operations register receiver stop callbacks
  when the receiver env exposes a stoppable token;
- keep the borrowed-fd warning and one-read/one-write-waiter rule unchanged.

If needed, mirror the lifecycle wording in `docs/forge-runtime.md` or
`docs/forge-utilities.md`.

## Verification

Run:

```sh
cmake -S . -B build/io-stop -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON \
  -DFORGE_TEST_ENABLE_SIMD=OFF \
  -DFORGE_TEST_ENABLE_SUBMDSPAN=OFF \
  -DFORGE_TEST_ENABLE_LINALG=OFF \
  -DFORGE_TEST_ENABLE_NATIVE_HANDOFF=OFF
cmake --build build/io-stop --target test_forge_io_context
ctest --test-dir build/io-stop -R '^forge_io_context$' --output-on-failure

scripts/verify-native.sh llvm
scripts/verify-native.sh asan
scripts/verify-native.sh tsan
```

After push, run the Windows MSVC smoke because the public runtime headers are
included by the cross-platform smoke suite.

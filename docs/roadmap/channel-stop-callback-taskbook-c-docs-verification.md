# Taskbook C: docs and verification

## Objective

Update the channel docs and prove the new cancellation model is sanitizer-clean.

## Docs

Update:

- `docs/forge-utilities.md`
- `docs/forge-runtime.md`

The docs should say:

- pre-start stop still completes stopped without enqueue;
- pending `async_send` / `async_recv` operations register stop callbacks when a
  stoppable token is present;
- callback-driven cancellation removes the pending operation and completes it
  stopped;
- `close()` and channel-level `request_stop()` semantics are unchanged.

## Verification

Run:

```bash
cmake -S . -B build/channel-stop -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON -DFORGE_TEST_ENABLE_SIMD=OFF -DFORGE_TEST_ENABLE_SUBMDSPAN=OFF -DFORGE_TEST_ENABLE_LINALG=OFF -DFORGE_TEST_ENABLE_NATIVE_HANDOFF=OFF
cmake --build build/channel-stop --target test_forge_channel
ctest --test-dir build/channel-stop -R '^forge_channel$' --output-on-failure
scripts/verify-native.sh llvm
scripts/verify-native.sh asan
scripts/verify-native.sh tsan
```

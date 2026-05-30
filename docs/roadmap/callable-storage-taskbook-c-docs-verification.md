# Taskbook C: docs and verification

## Objective

Make the new allocation boundary clear and verify the runtime layer stayed
stable.

## Docs

Update:

- `docs/forge-utilities.md`
- `docs/forge-runtime.md`
- resource-policy roadmap notes if they still mention the old pool caveat

The new wording should say:

- `static_thread_pool` uses the configured resource for queue nodes and queued
  task callable records;
- this is an internal pool detail, not a public `move_only_function` API;
- other caveats remain: `async_scope` op-state, timer items, and strand runner
  keepalive nodes are still outside the resource policy.

## Verification

Run:

```bash
cmake -S . -B build/callable-storage -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON -DFORGE_TEST_ENABLE_SIMD=OFF -DFORGE_TEST_ENABLE_SUBMDSPAN=OFF -DFORGE_TEST_ENABLE_LINALG=OFF -DFORGE_TEST_ENABLE_NATIVE_HANDOFF=OFF
cmake --build build/callable-storage --target test_forge_thread_pool
ctest --test-dir build/callable-storage -R '^forge_(thread_pool|runtime_context|resource_context|strand|accel)' --output-on-failure
scripts/verify-native.sh llvm
scripts/verify-native.sh asan
scripts/verify-native.sh tsan
```

Windows smoke is useful after push because this touches a core public header.

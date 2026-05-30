# Taskbook D: Examples, Docs, Verification

## Objective

Make the resource policy round teachable and auditable. Examples are part of the feature, not a
nice-to-have.

## Examples

Add or update examples with small, readable scenarios:

1. `example/forge_resource_policy_example.cpp`
   - creates a counting or monotonic memory resource;
   - configures a bounded pool/channel with it;
   - runs a tiny producer/consumer flow;
   - prints or asserts allocation observations without relying on global allocator hooks.

2. `example/forge_bounded_pipeline_example.cpp`
   - combines `resource_context`, `bounded_channel`, `strand`, and `async_scope`;
   - demonstrates graceful `close()` drain and `shutdown()`;
   - uses bounded capacity to show backpressure behavior.

Avoid making these examples synthetic API tours. They should show ownership, stop/drain, and
resource boundaries clearly.

## Docs

Update:

- `docs/forge-utilities.md`
- `docs/forge-runtime.md` if lifecycle wording changes
- `docs/testing.md` for gates
- `README.md` if the public feature list changes
- this roadmap/taskbook set if implementation intentionally diverges

Docs must explicitly state:

- `memory_resource*` is non-owning and must outlive configured primitives;
- V1 controls only documented policy-aware paths;
- `static_thread_pool` callable storage is handled by the callable-storage follow-up;
- gate semantics for `ON` / `OFF` / `AUTO`.

## CMake

Add examples/tests under the same gate model:

- current default builds remain unchanged;
- resource-policy examples build when resource policy is enabled;
- future IO/accel gates have placeholders but no fake targets.

Keep test target names prefixed with `forge_` so sanitizer scripts continue to pick them up.

## Verification

Focused:

```bash
cmake -S . -B build/local -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON
cmake --build build/local --target forge_resource_policy forge_bounded_pipeline
ctest --test-dir build/local -R '^forge_' --output-on-failure
```

Full:

```bash
scripts/verify-native.sh llvm zig local tsan asan
```

Optional if CMake changed substantially:

```bash
scripts/verify-native.sh gcc16 gcc-exec
```

## Review Checklist

- No new names in `namespace std`.
- No backport behavior changed.
- Default runtime tests still build and run.
- Sanitizers cover touched lifecycle-sensitive code.
- Examples compile in the same environments as other Forge examples.
- Docs do not claim zero allocation or full allocation control unless tests prove it.

## Done When

- The round is understandable from examples alone.
- Docs and roadmap match actual behavior.
- Full requested verification is green.

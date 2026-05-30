# Taskbook D: IO Docs, Examples, And Verification

## Objective

Make the IO backend teachable and auditable. Examples are part of the feature,
not a garnish.

## Documentation

Add or update:

- `docs/forge-io.md`
- `docs/forge-runtime.md`
- `docs/forge-utilities.md`
- `docs/testing.md`
- `README.md`
- `docs/roadmap/forge-runtime-vision.md` if implementation intentionally
  diverges from the vision.

Docs must clearly state:

- V1 is Linux fd readiness only.
- FDs are borrowed and must outlive pending operations.
- Close fd only after readiness operation completion, `cancel(fd)` plus drain, or
  context shutdown/drain.
- `readable`/`writable` complete on readiness, not after performing IO.
- Callbacks may run on the IO poller thread; heavy CPU work should continue on a
  pool or strand.
- `request_stop()` cancels pending operations; per-operation post-enqueue stop
  callback is deferred if not implemented.
- `AUTO`/`ON`/`OFF` gate behavior.
- Windows IOCP, kqueue, and io_uring are future work.
- Errors are currently delivered as `std::exception_ptr`.

## Examples

Add at least two examples.

### `example/forge_io_readiness_example.cpp`

Small tutorial:

- create a nonblocking pipe or socketpair;
- create `forge::io::context`;
- start `io.readable(read_fd)`;
- write one byte;
- observe readiness completion;
- close descriptors through RAII after drain.

Keep it short and readable.

### `example/forge_io_pipeline_example.cpp`

Composition example:

- `resource_context` or `runtime_context` owns CPU work;
- `io::context` owns fd readiness;
- `bounded_channel` carries messages;
- `strand` serializes a fake protocol/session state;
- readiness triggers a CPU continuation that reads from the fd and sends a
  message through the channel;
- shutdown order is explicit.

This should teach "who owns what" more than it teaches socket programming.

## CMake

- IO examples build only when `FORGE_ENABLE_FORGE_IO` resolves enabled.
- IO tests keep `forge_` prefix so sanitizer filters include them.
- `FORGE_TEST_ENABLE_FORGE_IO=OFF` skips IO tests without affecting other Forge
  runtime tests.
- `FORGE_ENABLE_FORGE_IO=OFF` skips IO examples/tests.

## Verification

Focused:

```bash
cmake -S . -B build/local -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON
cmake --build build/local --target test_forge_io_context forge_io_readiness forge_io_pipeline
ctest --test-dir build/local -R 'forge_io|forge_execution_header' --output-on-failure
```

Gates:

```bash
cmake -S . -B build/io-off -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON -DFORGE_ENABLE_FORGE_IO=OFF
ctest --test-dir build/io-off -N -R '^forge_io'

cmake -S . -B build/io-test-off -DFORGE_BUILD_TESTS=ON -DFORGE_TEST_ENABLE_FORGE_IO=OFF
ctest --test-dir build/io-test-off -N -R '^forge_io'
```

Full:

```bash
scripts/verify-native.sh llvm zig local tsan asan
```

If standard backport headers or native detection are unexpectedly touched, also
run:

```bash
scripts/verify-native.sh gcc16 gcc-exec
```

## Acceptance Criteria

- Examples compile and demonstrate correct lifetime.
- Docs do not claim cross-platform IO.
- Docs do not claim async read/write.
- Gate-off configure paths are verified.
- Sanitizer test counts do not drop.
- Worktree is clean and commits are split by logical topic.

## Commit

Suggested commit:

```text
docs(forge): document linux io readiness backend
```

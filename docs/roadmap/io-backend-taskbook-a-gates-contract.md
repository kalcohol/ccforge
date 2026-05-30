# Taskbook A: IO Gates And Contract

## Objective

Turn the existing placeholder IO feature gate into an executable policy and
document the exact V1 contract before backend code exists.

## Current Facts

- `FORGE_ENABLE_FORGE_IO` already exists as a tri-state cache option.
- `FORGE_TEST_ENABLE_FORGE_IO` already exists as a test gate.
- There is no `include/forge/io/` or `include/forge/io.hpp` yet.
- The current runtime lifecycle vocabulary is documented in
  `docs/forge-runtime.md`.
- Resource policy is available via `forge::resource_policy` and
  `forge::default_memory_resource()`.

## Required Decisions

1. `AUTO`:
   - Linux with `sys/epoll.h` and `sys/eventfd.h`: enable IO.
   - Other platforms: skip IO without warning.
2. `ON`:
   - Linux with required headers: enable IO.
   - Unsupported platform or missing headers: configure error with a clear message.
3. `OFF`:
   - Do not build IO examples/tests.
   - Public non-backend umbrella headers may still be includeable if they do not
     require platform headers.
4. Keep dependency detection shallow. Do not probe `io_uring`, IOCP, kqueue,
   OpenSSL, liburing, or networking libraries in this round.

## Implementation Direction

1. Add CMake probe variables in the existing `forge.cmake` style.
2. Prefer compile checks for Linux headers over string-only platform checks.
3. Export an internal CMake variable such as `FORGE_HAS_FORGE_IO_BACKEND` for
   tests/examples.
4. Add test/example gating without changing existing non-IO test counts.
5. Add or update docs:
   - `docs/forge-runtime.md` lifecycle terms if IO needs a specific note;
   - `docs/forge-utilities.md` or a new `docs/forge-io.md` for IO contracts;
   - `docs/testing.md` for gate behavior.

## Contract To Record

- `forge::io::context` is an owning runtime primitive.
- `readable(fd)` and `writable(fd)` are borrowed-fd readiness operations.
- The caller must keep the fd open until completion, cancellation, or context
  drain. Closing an fd behind a pending operation is user error because fd numbers
  can be reused by the OS.
- `close()` rejects new operations but does not force-complete already pending
  operations by itself.
- `request_stop()` cancels pending operations and wakes the poller.
- `shutdown()` is `close()` plus `request_stop()`.
- `wait()` blocks until the poller has exited and pending completions have been
  delivered or cancelled.
- Destructor calls `shutdown()` and `wait()`, so it may block.

If implementation shows a better split between `close()` and `request_stop()`,
update this contract before adding senders.

## Tests

Add configure-level tests:

- IO `OFF` configures and registers no `forge_io` tests.
- IO `AUTO` on Linux registers IO tests.
- IO `ON` on Linux configures.

Do not require non-Linux CI for this round, but the CMake branch should be
readable and deterministic.

## Risks

- Silent sanitizer coverage regression if IO tests are not named with the
  `forge_` prefix.
- Over-eager dependency probes can make non-IO users fail configure.
- Public headers that include Linux-only headers unconditionally would break
  portability even when the backend is gated off.

## Verification

```bash
cmake -S . -B build/io-auto -DFORGE_BUILD_TESTS=ON
ctest --test-dir build/io-auto -N -R '^forge_io'

cmake -S . -B build/io-off -DFORGE_BUILD_TESTS=ON -DFORGE_ENABLE_FORGE_IO=OFF
ctest --test-dir build/io-off -N -R '^forge_io'

cmake -S . -B build/io-on -DFORGE_BUILD_TESTS=ON -DFORGE_ENABLE_FORGE_IO=ON
```

## Commit

Suggested commit:

```text
build(forge): make io backend gates executable
```

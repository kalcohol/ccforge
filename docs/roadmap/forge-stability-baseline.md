# Forge stability baseline

This page records the current delivery baseline for `include/forge/` work and
the loop used to decide whether more runtime/IO/accel work is justified. It is a
maintenance guide, not a promise that every optional proof backend is production
complete.

## scope

The stable target is:

- a C++ backport library with optional `forge::` runtime support;
- clear ownership, cancellation, drain, and typed-error boundaries;
- portable proof surfaces for IO and accelerator-shaped command systems;
- examples that teach safe composition.

The target is not:

- a complete networking framework;
- a CUDA/HIP/SYCL runtime;
- a tensor or model-serving framework;
- a clone of NVIDIA stdexec/exec;
- a reason to change standard backport behavior as a side effect of
  `forge::` work.

## current delivered surface

Core runtime utilities:

- `forge::static_thread_pool`
- `forge::single_thread_context`
- `forge::system_context`
- `forge::timer_context`
- `forge::runtime_context`
- `forge::async_scope`
- `forge::bounded_channel`
- `forge::resource_context`
- `forge::strand`
- `forge::task`

Resource and type-erasure utilities:

- `forge::resource_policy`
- `forge::any_scheduler`
- `forge::erased_sender<CompletionSignatures>`
- narrow `forge::any_sender_of` / `forge::any_receiver_of`

Platform/proof surfaces:

- `forge::io` Linux epoll/eventfd readiness backend
- `forge::io` Windows IOCP proof backend
- `forge::accel` portable mock command backend
- opt-in typed-error variants for `forge::io`
- opt-in typed-error variants for `forge::accel`

## verification floor

Use these as the default evidence set for behavior-changing work:

```sh
scripts/verify-native.sh llvm
scripts/verify-native.sh tsan asan
scripts/verify-install-package.sh
```

When a Windows host is available, also run the Windows/MSVC smoke script. Keep
machine-specific hostnames, user directories, and installation paths in local
environment variables or shell history only; do not commit them.

The most recent known-good shape for this track was:

- LLVM/libc++ all-backport path: all tests passing;
- TSAN forge/execution subset: all tests passing;
- ASAN/UBSAN forge/execution subset: all tests passing;
- install package consumer smoke: passing;
- Windows/MSVC smoke: passing, including IOCP and accel gate checks.

Do not turn these total counts into hard-coded policy. Test totals are expected
to grow. Prefer named critical tests and feature-gate registration checks.

## gate expectations

For optional features, validate behavior by registration shape:

- `FORGE_ENABLE_FORGE_IO=AUTO` or `ON`
  - on Linux: registers Linux IO tests/examples;
  - on Windows: registers IOCP tests/examples;
  - on unsupported platforms with `AUTO`: skips IO backend tests/examples;
  - with `ON` and no backend: configure should fail.
- `FORGE_ENABLE_FORGE_IO=OFF`
  - registers zero IO backend tests/examples.
- `FORGE_ENABLE_FORGE_ACCEL=AUTO` or `ON`
  - registers portable mock accel tests/examples when runtime/resource gates are
    available.
- `FORGE_ENABLE_FORGE_ACCEL=OFF`
  - registers zero accel tests/examples.

For sanitizer coverage, new runtime tests should use the `forge_` test prefix
unless there is a specific reason not to. The sanitizer gate filters are intended
to catch execution and `forge::` lifetime issues.

## convergence checklist

Use this checklist after each taskbook round.

| Target | Evidence | Current status |
| --- | --- | --- |
| Verification is repeatable | `scripts/verify-native.sh`, `scripts/verify-install-package.sh`, `scripts/verify-windows-msvc-ssh.sh`, `docs/testing.md` | In place |
| Feature gates are testable | IO/accel ON/AUTO/OFF registration checks in `scripts/verify-windows-msvc.ps1`; local `ctest -N -R 'forge_io\|forge_accel'` gate checks | In place |
| Resource behavior is auditable | `forge_resource_policy`, `example/forge_resource_policy_example.cpp`, `example/forge_bounded_pipeline_example.cpp` | Needs an allocation audit table |
| IO lifecycle is explicit | `docs/forge-io.md`, `forge_io_context`, `forge_io_iocp`, `example/forge_io_read_write_example.cpp` | Needs periodic cancellation/lifetime re-audit |
| Accel lifecycle is explicit | `docs/forge-accel.md`, `forge_accel_context`, `forge_accel_event`, `forge_accel_typed_error`, `example/forge_accel_pipeline_example.cpp` | Needs periodic event/buffer/session re-audit |
| Typed-error boundaries are usable | `forge_erased_sender`, `forge_accel_typed_error`, `example/forge_io_typed_error_example.cpp`, `example/forge_accel_typed_error_example.cpp` | Usable; ergonomics can improve |
| Examples teach composition | `docs/forge-cookbook.md`, `example/forge_graceful_shutdown_example.cpp`, `example/forge_inference_runtime_sketch.cpp`, `^example_` smoke tests | Good baseline; needs periodic teaching pass |
| Deferred large backends stay explicit | `docs/roadmap/forge-runtime-vision.md` decision gates | In place |

A round is complete when every changed row has concrete evidence: a test name,
an example path, a doc section, or a deliberate limitation entry.

## self-loop protocol

Every new runtime/IO/accel taskbook should follow this loop:

1. **Inspect** current code and docs. Do not implement from stale taskbook facts.
2. **Implement one slice** with a focused commit.
3. **Verify** with focused tests, then the taskbook's required gates.
4. **Review** for UAF, races, completion-under-lock, stop-callback lifetime,
   gate drift, and docs drift.
5. **Update backlog** by editing the relevant roadmap/taskbook if a new
   high-value gap is found.
6. **Converge check** against the checklist above.

If a remaining item is high-value and low/medium-risk, create the next small
round and repeat. If the remaining work requires vendor SDKs, new OS backends,
or broad API commitments, stop and require a separate owner decision.

## known intentional boundaries

- Windows/MSVC smoke is a manual/optional gate, not a replacement for Linux
  container verification.
- `forge::accel` is mock/in-memory and vendor-neutral; it does not imply CUDA,
  HIP, SYCL, FPGA, or NPU driver support.
- Linux `io_uring`, macOS/BSD kqueue, and production IOCP hardening are deferred
  unless separately approved.
- `std::execution` backport behavior should not be changed merely to support a
  `forge::` extension.

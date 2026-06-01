# Backend proof policy

This policy applies to optional platform/vendor proof backends under
`include/forge/`. It does not apply to standard backports under `backport/`.

The goal is to prove that Forge's runtime substrate can express real systems
without turning the project into a networking, GPU, tensor, or driver framework.

## gate policy

Every optional backend must have an explicit CMake feature gate:

- `AUTO`: enable only when the platform/backend probe succeeds;
- `ON`: require the backend and fail configure if unavailable;
- `OFF`: register zero tests/examples for that backend.

Do not add SDK probes before backend code exists. A future CUDA/HIP/SYCL,
`io_uring` or a vendor-device proof must add its probe in the same
taskbook as the backend code and tests.

Package installs must not freeze the build machine's probe result. Installed
configs should rerun backend probes in the consumer project.

## verification policy

Each backend proof needs:

- a focused test name with a `forge_` prefix where possible;
- example smoke tests for public examples;
- gate-off registration checks;
- sanitizer coverage if the backend is available in the sanitizer environment;
- install-package coverage for header and CMake propagation;
- platform smoke when the backend needs an OS/toolchain not covered by the
  Linux container matrix.

Do not hard-code one global CTest count as policy. Counts grow. Prefer nonzero
or zero registration checks for specific regexes.

Windows/MSVC smoke is a manual or self-hosted gate. Public docs and scripts must
not contain private hostnames, user names, or local installation paths; keep
those in environment variables or local shell history.

## lifetime policy

Backends must state ownership precisely:

- OS handles are borrowed unless the type name says otherwise;
- spans are borrowed unless the command packet owns storage explicitly;
- context resources are non-owning and must outlive the context and objects that
  allocate from them;
- receiver completion must not run under backend internal locks;
- cancellation must not release operation storage until in-flight callbacks or
  completion packets can no longer touch it.

Future stronger storage categories, such as pinned host memory, native device
allocations, owning command packets, or exported native handles, must be
explicit opt-in types. They should not silently change existing borrowed-span
contracts.

## typed-error policy

Default backend APIs use `set_error(std::exception_ptr)`.

Typed-error variants are opt-in and should expose only stable portable
categories:

- invalid handle / buffer / event;
- operation already in progress;
- capacity or size mismatch;
- cancellation / stopped when it is part of the sender contract;
- backend/system error with the original code preserved where possible.

Vendor/platform status codes should not be added to portable enums
speculatively. A real backend may preserve raw status in backend-specific detail
only after a mapping decision and tests. `forge::erased_sender` may carry typed
errors that are explicitly declared in its target completion signatures, and
`forge::wait_result` is the synchronous boundary helper for examples and tests.

## project identity rule

A new backend proof is an owner decision, not routine maintenance, when it
introduces:

- a vendor SDK or driver header;
- a new OS backend model;
- native handles in public API;
- a new error taxonomy;
- a long-running background runtime beyond existing Forge contexts.

When in doubt, write a taskbook first and keep the portable surface mock-first.

## current backend stance

The practical IO pair is Linux `epoll/eventfd` readiness plus Windows IOCP
completion. They are intentionally separate backend models. Do not force IOCP
through the Linux readiness state machine, and do not add kqueue without a real
BSD/macOS owner and verification host.

Linux `io_uring` is deferred until the project needs kernel submission/completion
queue semantics that `epoll` readiness plus one-shot read/write cannot express.
If that day comes, treat it as a new backend proof with its own gate, examples,
and sanitizer story.

The practical accel stance is vendor-neutral. Keep the portable surface centered
on queues, command submission, buffers, async copy, event/fence boundaries, and
message-style device sessions. CUDA/HIP/SYCL, FPGA, NPU, or driver SDK support
is a separate proof only after the mock shape has demonstrated a stable need.

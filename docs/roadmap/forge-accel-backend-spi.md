# `forge::accel` backend SPI sketch

This is a design sketch for future accelerator backend proofs. It is not a
public plugin ABI, and it does not approve CUDA, HIP, SYCL, FPGA, or NPU vendor
dependencies by itself.
General gate, lifetime, verification, and typed-error rules are defined in
[backend proof policy](forge-backend-proof-policy.md).

The current shipped backend is the portable mock/in-memory reference backend in
`include/forge/accel/mock/`. Backend-neutral vocabulary lives in
`include/forge/accel/`. A future backend should preserve the same user-facing
shape before it exposes vendor-specific details.

## portable concepts

The stable portable vocabulary is intentionally small:

- owning backend context, currently `forge::accel::mock::context`;
- lightweight device and queue handles derived from the context;
- queue kind metadata for general, compute, copy, and command/message lanes;
- optional `device_session` for command/response style devices;
- owning host and device buffers with portable `memory_kind` metadata;
- byte-oriented host and device buffers for command/model IO proof;
- borrowed host spans for copy commands;
- owning command packets for command/response runtimes that should not borrow
  caller-owned response storage;
- explicit `flush` / `invalidate` coherence command boundaries for cached-like
  memory proofs;
- minimal event / record / wait / fence completion boundary;
- model/session execute proof over byte-size IO descriptors and borrowed byte
  spans, without tensor or graph semantics;
- command senders for H2D, D2H, D2D, generic `submit`, and message submit.

This vocabulary is meant to cover common stream/queue/event/device-memory
patterns found in GPU, NPU, FPGA, and other accelerator runtimes without
binding Forge to any specific vendor SDK.

## sender contract

Backend command senders must keep the existing Forge runtime contract:

- exactly one terminal completion;
- no receiver completion while holding backend internal mutexes;
- callback or completion-packet storage must outlive the callback return path;
- default APIs use `set_error(std::exception_ptr)`;
- opt-in typed APIs use `set_error(forge::accel::error)`;
- queue-capacity or closed-context rejection completes as stopped where possible;
- request-stop is best-effort and must not claim to interrupt an already running
  kernel/callable unless the backend has tested support for that behavior.

Typed errors should stay as a small portable classification. Vendor status codes
may be preserved as backend-specific detail only after a separate mapping
decision.

## lifetime contract

The current public contract is borrowed-by-default:

- device handles expose portable `device_info` and availability only; a backend
  must document whether "lost" and "reset" are simulated flags, native device
  loss, driver reset, or context rebuild;
- device-bound queues and sessions must check device availability before
  running queued commands, and must map lost-device rejection to the portable
  `device_lost` classification;
- host spans must outlive command completion;
- `host_buffer<T>` and `device_buffer<T>` must outlive command completion;
- moving a buffer object while a command that captured it is pending is a caller
  error;
- `memory_kind` values are portable metadata unless a backend explicitly
  documents stronger native allocation behavior;
- cached-like memory requires explicit command-boundary coherence operations
  when the backend documents that requirement;
- `event` is a shared completion marker, not a dependency graph node.
- `submit_packet` owns request/response storage until terminal completion;
  `submit_message` is the explicitly borrowed response path.
- `model_bindings` stores borrowed byte spans; a backend that supports stronger
  native tensor or buffer ownership must expose that as an explicit opt-in type.
- queued-command timeout may reject work that has not started by the deadline,
  but it must not claim to interrupt a command/kernel that is already running.

A future backend may add pinned host buffers, native event handles, or stronger
backend-specific packet ownership, but those must be explicit opt-in types. They
should not silently change the borrowed contract of the current mock surface.

## event and fence boundary

Events must remain minimal unless a real backend proof needs more:

- `record_event(queue, event)` records readiness after earlier accepted work on
  that queue reaches the command;
- `wait_event(queue, event)` waits for a marker to become ready or for context
  stop;
- `fence(queue)` is a no-op command boundary for previously accepted work.
- A context may expose multiple queues. FIFO is guaranteed per queue; cross-queue
  ordering is expressed only through explicit event record/wait operations.

Do not turn this into a general dependency graph in the portable layer. Cross
queue dependency management, native event export, timeline semaphores, and graph
submission are separate backend-specific proposals.

## backend proof checklist

Before adding a real backend, require:

- an explicit gate and CMake detection policy;
- gate-off builds with zero backend tests/examples registered;
- no vendor headers included from the portable mock headers;
- focused tests for copy, submit, event/fence, shutdown, and typed errors;
- documentation of which resources are owned, borrowed, pinned, or vendor-owned;
- examples that use the portable surface first, with native handles only in a
  clearly marked backend-specific example.

The first real backend proof should be reviewed as a new project identity
decision, not as routine maintenance.

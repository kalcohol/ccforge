# `forge::accel` runtime v2 roadmap

This roadmap is the long-lived goal anchor for evolving `forge::accel` from the
current portable mock command backend into a backend-neutral accelerator runtime
support layer.

It does not approve CUDA, HIP, SYCL, CANN, AXCL, XRT, FPGA, NPU, or other vendor
dependencies. It also does not make Forge a tensor framework, model server, or
driver wrapper. The target layer sits above vendor runtime/driver SDKs and below
tensor/DL frameworks:

```text
DL/tensor graph, model serving policy        out of scope for this roadmap
forge::accel runtime support vocabulary      target scope
Vendor runtime/driver SDK APIs               future optional backend inputs
Kernel drivers, firmware, hardware           out of scope
```

## why this exists

The current `forge::accel` surface is a useful proof: it has context, device,
queue, buffers, copy commands, generic submit, message-style sessions, minimal
events/fences, typed errors, and examples. It is intentionally dependency-free
and sanitizer-friendly.

For a practical NPU/GPGPU runtime, the next step is not to bind a vendor SDK.
The next step is to define the accelerator runtime vocabulary that real backends
would need to satisfy:

- memory categories and coherence boundaries;
- stream/queue capabilities and ordering;
- event/fence semantics across queues;
- device/context/session lifecycle;
- command packets, responses, timeouts, and reset boundaries;
- model/session/IO-binding proof for NPU-style inference runtimes;
- examples that show modern C++ composition rather than raw callback plumbing.

The mock/reference backend is the executable specification for that vocabulary.

## non-goals

- Do not implement or wrap kernel drivers.
- Do not introduce vendor SDK dependencies in this roadmap.
- Do not implement CUDA/HIP/SYCL/CANN/AXCL/XRT backends here.
- Do not create a dynamic plugin ABI.
- Do not implement tensor graph optimization, operator fusion, quantization, or
  serving policy.
- Do not expose native handles from the portable surface.
- Do not weaken existing `forge::` lifecycle rules: no completion under internal
  locks, exactly-one terminal completion, and explicit shutdown/wait contracts.

## target structure

The desired final shape separates vocabulary from the reference implementation:

```text
forge::accel                    backend-neutral vocabulary and contracts
forge::accel::mock              dependency-free reference backend
future optional backend         separate owner-approved proof
```

Short-term source compatibility is not a blocker for this roadmap because this
is not a standard backport surface. Prefer the clean final shape over preserving
accidental names from the V1 mock proof. If compatibility aliases are retained,
they must be documented as transitional, not as the long-term vocabulary.

## reference backend policy

The mock backend is not a test-only fake. It is a public reference backend and
executable specification:

- dependency-free and portable;
- suitable for examples and cookbook recipes;
- covered by sanitizer lanes;
- able to simulate capacity, reset, device-lost, timeout, invalid buffer, and
  typed-error paths;
- able to model queue ordering and cross-queue event behavior;
- explicit that it does not provide hardware acceleration or realistic
  performance.

Real backend proofs must first explain how they map to the reference backend's
contracts before exposing backend-specific extensions.

## phase 1: vocabulary split

Target:

- Introduce a clear `forge::accel` vocabulary layer for device IDs, device info,
  memory kinds, queue kinds, event/fence concepts, copy kinds, command status,
  error categories, and model/session metadata.
- Move the current mock implementation toward an explicit
  `forge::accel::mock` namespace/module.
- Decide whether V1 names remain as transitional aliases.

Acceptance:

- Public docs distinguish portable vocabulary from mock/reference backend.
- Existing accel examples are either migrated to `mock` or explicitly marked as
  using the reference backend.
- Gate-off and gate-on behavior remains deterministic.
- Focused tests prove aliases, if any, point at the documented layer.

## phase 2: memory model

Target:

- Add portable memory vocabulary for host, pinned host, mapped/shared host,
  device, cached device, managed/shared, and imported/exported storage where the
  distinction is useful.
- Add explicit coherence operations such as flush and invalidate for memory
  kinds that need them.
- Add byte-oriented buffer support, alignment, and owned command storage where
  needed.

Acceptance:

- Mock backend simulates memory kind constraints and coherence errors.
- Copy commands validate memory categories and sizes.
- Docs state which memory is owned, borrowed, pinned, mapped, cached, or mock.
- Examples show staging, pinned-like host buffers, cached-memory flush/invalidate
  proof, and size/alignment error handling.

## phase 3: queue and event model

Target:

- Model multiple queues per context.
- Distinguish queue capabilities such as compute, copy, and command/message.
- Support cross-queue events/fences without becoming a general graph scheduler.
- Add timeout/cancellation boundaries where they can be tested.

Acceptance:

- Mock tests cover H2D/compute/D2H overlap semantics at the ordering level.
- Same-queue and cross-queue event behavior is documented and tested.
- Device stop/reset wakes blocked waits without lost wakeups.
- No receiver completion runs under internal locks.

## phase 4: device, context, and session lifecycle

Target:

- Model device discovery and device info without probing real hardware.
- Model context ownership, device reset/lost, and session reset boundaries.
- Provide a thread/context binding story suitable for vendor APIs that require
  current-context setup, without freezing a native API.

Acceptance:

- Mock backend can simulate available/unavailable/lost devices.
- Session reset and context shutdown drain accepted work safely.
- Tests cover reset while commands are pending and while completions schedule
  follow-up work.
- Docs map these concepts to typical NPU/GPU runtime responsibilities without
  naming a required vendor dependency.

## phase 5: command packet and completion bridge

Target:

- Add an owning command packet path for command/response runtimes.
- Add command IDs, status, timeout, response storage, and reset/abort results.
- Define how a backend callback/completion packet maps into sender completion.

Acceptance:

- Mock command packets survive asynchronous completion without borrowed lifetime
  hazards.
- Typed and exception-based error paths both work across `forge::erased_sender`.
- Tests cover capacity, timeout, reset, command failure, stopped, and success.
- Examples show request/response device channels without raw callback ownership.

## phase 6: model/session inference proof

Target:

- Add a NPU-style model/session proof without implementing tensor graph logic.
- Model load/unload, usage query, IO metadata, IO binding, and async execute.
- Keep tensors as byte/span/buffer bindings, not a full tensor library.

Acceptance:

- Mock model can declare inputs/outputs and reject mismatched bindings.
- Async execute composes with queues, events/fences, typed errors, shutdown, and
  reset.
- Examples cover single inference, batched requests, graceful shutdown, and error
  reporting.
- Docs clearly say this is a runtime proof, not a DL framework.

## phase 7: examples and cookbook

Target:

- Provide a progressive example set from simple copy through reference inference
  runtime.
- Teach the intended C++ style: sender pipelines, scopes, channels, strands,
  typed errors, and resource policy.

Acceptance:

- Examples are smoke-tested.
- Cookbook links exact example files and avoids duplicating large semantics that
  can drift from code.
- Examples cover simple copy, memory kinds, cross-queue event, command packet,
  model execute, error handling, reset, and graceful shutdown.

## phase 8: final audit

Target:

- Verify the v2 layer is internally consistent and ready to serve as the stable
  foundation for future optional real backend proofs.

Acceptance:

- `docs/forge-accel.md`, cookbook, examples, tests, CMake gates, and roadmap are
  consistent.
- No vendor headers are included by portable accel headers.
- Sanitizer lanes cover the mock/reference backend.
- Windows smoke expectations are documented without private hostnames or paths.
- Remaining work is clearly classified as future optional backend proof, not
  unfinished v2 foundation.

## self-loop rule

Each phase must loop until it satisfies its acceptance criteria:

1. inspect the current implementation against this roadmap;
2. implement the smallest coherent slice;
3. add focused tests for every behavior changed or newly specified;
4. run focused tests and the required verification gates;
5. update docs, examples, and this roadmap status when behavior changes;
6. commit a focused change;
7. repeat the phase if review, tests, or code inspection reveal unresolved
   correctness or documentation gaps.

Do not advance to the next phase while the current phase has known correctness,
lifetime, cancellation, or contract gaps.

## verification baseline

At minimum, implementation phases should use:

```bash
scripts/verify-native.sh llvm
scripts/verify-native.sh tsan
scripts/verify-native.sh asan
```

When CMake gates or install behavior changes, also run:

```bash
scripts/verify-install-package.sh
```

When Windows-sensitive behavior changes, use the Windows/MSVC smoke scripts with
host details supplied outside the repository.

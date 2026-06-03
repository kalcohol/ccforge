# `forge::accel` runtime v3 roadmap

This roadmap is the public, privacy-safe goal anchor for evolving
`forge::accel` from a portable mock/reference backend into a host/device runtime
substrate proof.

It does not approve CUDA, HIP, SYCL, FPGA, NPU, driver, firmware, or framework
dependencies. It also does not make Forge a tensor framework, model server, or
driver wrapper. The target layer sits between framework/backend glue and future
optional vendor runtime inputs:

```text
DL/framework backend glue                 design notes + examples only
forge::accel runtime substrate            this v3 target
Vendor runtime / driver SDK APIs          future optional backend inputs
Kernel drivers, firmware, hardware        out of scope
```

The private and project-specific materials that motivated this roadmap are not
part of the repository. Repository docs, examples, taskbooks, and commits must
describe only generic host/device runtime mechanisms. Do not record private
project names, device codenames, private API names, private paths, private
struct/enum/field names, logs, or source snippets.

## why this exists

The v2 accel foundation is complete as a dependency-free executable
specification:

- backend-neutral vocabulary;
- mock and CPU reference backends;
- queues, copies, submit commands, events, fences, and typed errors;
- request packets, protocol envelopes, request correlation, late response
  accounting, and trace;
- device/session reset, stale session, drain freeze, worker fault, and model
  execute proofs;
- examples and conformance tests.

The remaining practical gap is not a vendor wrapper. The gap is a host/device
runtime substrate:

- host API request admission;
- control plane and message plane separation;
- routing to a device-side worker;
- stream/queue execution and completion response;
- event/fence notification between host and device work;
- stream-ordered device-to-host callback ingress;
- lifecycle recovery after device lost, host lost, stale session, worker fault,
  drain/freeze, or host sleep/resume;
- framework glue expectations such as device guard, stream guard, allocator,
  event wrapper, per-stream synchronize, op dispatch, and tensor/model binding.

## non-goals

- Do not implement or wrap kernel drivers.
- Do not introduce vendor SDK dependencies in this roadmap.
- Do not expose native handles from the portable surface.
- Do not create a dynamic plugin ABI.
- Do not implement tensor graph optimization, graph capture execution,
  operator fusion, quantization, or serving policy.
- Do not implement stream-ordered async allocation/free as a default portable
  contract.
- Do not weaken existing `forge::` lifecycle rules: exactly-one terminal
  completion, no receiver completion under internal locks, explicit
  shutdown/wait contracts, and wakeup predicates published under the waiter
  mutex.

## privacy and documentation rule

This roadmap intentionally uses generic terms: control plane, message plane,
worker, stream, event, callback, session, epoch, heartbeat, transport, and
framework glue. These are public runtime concepts, not references to any private
project.

Future docs and examples must preserve that boundary. If a design cannot be
explained without private names or private structs, it is not ready to enter the
repository.

## target structure

The desired v3 shape keeps v2's namespace split:

```text
forge::accel                    backend-neutral vocabulary and contracts
forge::accel::mock              dependency-free runtime-substrate proof backend
forge::accel::cpu               dependency-free CPU/SIMD reference backend
future optional vendor backend  separate owner-approved proof
```

The mock backend remains the executable specification. CPU remains the
dependency-free proof that portable command vocabulary can drive real CPU/SIMD
work. Vendor backends remain owner-gated and should first map their mechanics
back to the portable contracts below.

## phase 1: roadmap and vocabulary contracts

Target:

- Publish this v3 roadmap and link it from the docs index and runtime vision.
- Record small inspection vocabulary that helps future backends without
  over-promising hardware facts:
  - extended `device_info` fields such as total-memory-like capacity,
    capability version, and maximum queue count, defaulting to zero/unknown when
    a backend has no real value;
  - diagnostic stringification for portable accel error categories.

Acceptance:

- Public docs distinguish completed v2 foundation from planned v3 substrate.
- Text is privacy-safe and contains no private names or source details.
- Vocabulary additions stay small value types/helpers, not native handles.

## phase 2: transport and message plane

Target:

- Define a dependency-free message transport proof over existing protocol
  vocabulary.
- Distinguish posted and non-posted calls:
  - posted: admission/enqueue returns first, completion arrives asynchronously;
  - non-posted: operation completion is the matching response boundary.
- Distinguish enqueue acknowledgement from execution completion response.
- Preserve request/response correlation, duplicate request rejection, late
  response discard, lifecycle signal bypass, capacity/backpressure, and typed
  transport errors.

Acceptance:

- Transport tests prove posted and non-posted modes can share one transport.
- Late responses are discarded and counted.
- Lifecycle/control signals do not depend on the normal request-pending map.
- No fd/HANDLE/ioctl/native-handle vocabulary appears in the portable surface.

## phase 3: control plane and lifecycle

Target:

- Separate control/lifecycle events from ordinary business command packets.
- Model context/session open and close, online/offline, host lost, heartbeat,
  worker fault, freeze, drain, resume, epoch bump, and stale-session closure.
- Treat heartbeat as the primary worker liveness signal in the proof, not as
  decorative telemetry.
- Add scoped current-device guard / query support as host runtime convenience.

Acceptance:

- Device lost, host lost, heartbeat timeout, drain freeze, worker fault, and
  reset have explicit admission outcomes.
- Host-lost cleanup prevents new admission until stale worker/session state is
  closed.
- Current-device guard state is scoped, thread-local, and restored on scope
  exit.

## phase 4: worker runtime and stream semantics

Target:

- Model worker identity through `worker_key`.
- Give each worker stream `0` as the default stream.
- Model stream FIFO nodes:
  - command node;
  - module/command dispatch node keyed by portable `module_id` + `command_id`;
  - event-record node;
  - event-wait node with optional timeout;
  - stream-ordered callback node;
  - sync/fence node.
- Add stream query and per-stream synchronize with timeout.
- Define sticky stream error observation and clearing. Preferred proof
  semantics: record the first non-success stream error, continue later nodes,
  report and clear the sticky error at stream synchronize, and keep query
  non-mutating.
- Add event elapsed-time proof and trace activity duration with start/end
  timestamps.

Acceptance:

- Worker/stream tests prove FIFO ordering, default stream behavior,
  module/command dispatch, stream query, stream synchronize timeout, sticky
  error observation/clear, event elapsed time, trace duration, drain, and worker
  fault behavior.
- The proof does not become a general dependency graph scheduler.
- CPU backend conformance remains focused on portable queue/copy/submit/event
  behavior; mock-only worker internals stay mock-specific.

## phase 5: stream-ordered device-to-host callbacks

Target:

- Add a dependency-free proof for callback nodes inserted into stream FIFO.
- Host registers a callback and receives a callback ID.
- A stream reaches a callback node, emits a callback-invoke message, dispatches
  the host callback on a scheduler/strand, and optionally sends a
  callback-complete acknowledgement back through the transport/control path.
- Unregister drains or rejects in-flight callbacks according to the contract.

Acceptance:

- Callback trigger is stream-ordered, not an arbitrary side-channel interrupt.
- User callback never runs under backend internal locks.
- Invoke/complete, unregister, reset/lost during callback, callback error, and
  concurrent invoke/unregister/shutdown paths are tested.

## phase 6: power/resume and framework glue

Target:

- Document conservative power/resume behavior:
  - host sleep request;
  - quiesce/checkpoint before sleep;
  - device low-power entry after drain/fence;
  - wake source;
  - resume re-probe;
  - epoch/session/worker generation validation;
  - stale-session failure or explicit proof of survival.
- Record framework-style glue expectations without including framework headers:
  - device guard and stream guard;
  - allocator and memory pool contracts;
  - record-stream / tensor lifetime expectations;
  - event wrapper;
  - per-stream synchronize;
  - peer access enable/query;
  - graph capture state-machine boundaries;
  - stream priority as optional metadata;
  - user trace range markers;
  - async copy and op dispatch;
  - model/session binding;
  - typed-error mapping.

Acceptance:

- Docs clearly state which items are implemented proofs, which are contracts,
  and which remain owner-gated future backend work.
- Examples remain dependency-free and do not include framework headers.
- The PPT outline can later use this material without inventing mechanics.

## self-loop rule

Each phase must loop until it satisfies its acceptance criteria:

1. Inspect the current implementation against this roadmap.
2. Write or update the minimal public contract before broad implementation.
3. Implement the smallest coherent slice.
4. Add focused tests, including stress tests for callback, wakeup, lifecycle,
   and worker/stream races.
5. Run focused tests and required verification gates.
6. Update docs, examples, and this roadmap status.
7. Commit and push the logical change.
8. Repeat the phase if review, tests, or code inspection reveal unresolved
   correctness or documentation gaps.

Do not advance while the current phase has known UAF, lost wakeup,
double-completion, completion-under-lock, privacy, or contract gaps.

## verification baseline

At minimum:

```bash
scripts/verify-native.sh llvm
```

When runtime concurrency changes:

```bash
scripts/verify-native.sh tsan
scripts/verify-native.sh asan
```

When CMake gates or install behavior changes:

```bash
scripts/verify-install-package.sh
```

When Windows-sensitive behavior changes, use the Windows/MSVC smoke scripts with
host details supplied outside the repository.


# `forge::accel`

`forge::accel` is Forge's accelerator-shaped runtime support layer. It provides
backend-neutral vocabulary in `forge::accel`, a dependency-free executable
mock/fault-injection backend in `forge::accel::mock`, and a dependency-free
CPU/SIMD reference backend in `forge::accel::cpu`.

The mock backend uses CPU storage plus Forge runtime primitives to model the
engineering shape of accelerator work:

- contexts, devices, queues, sessions, epochs, and worker generations;
- host/device/staging buffers and memory-kind metadata;
- H2D, D2H, D2D copy, coherence commands, and kernel-like submit;
- event/fence ordering between queues;
- request/response command packets, protocol envelopes, and typed errors;
- optional in-memory telemetry for state-machine inspection.

It does not bind CUDA, HIP, SYCL, OpenCL, Vulkan, FPGA SDKs, NPU SDKs, kernel
drivers, firmware, tensor graphs, or model-serving policy. Future real backend
proofs must be optional and must map back to these portable contracts before
exposing backend-specific extensions. Backend entry rules are tracked in the
[`forge::accel` backend SPI sketch](roadmap/forge-accel-backend-spi.md) and the
[backend proof policy](roadmap/forge-backend-proof-policy.md).
The mock backend also runs the repository-local
`forge_accel_backend_conformance` test suite, which records the portable
contract a future backend proof must satisfy. The CPU backend runs the same
portable conformance suite so the vocabulary is tested against a second backend,
not just the mock state machine.

```cpp
#include <forge/accel.hpp>
```

## Vocabulary

`forge::accel` owns the portable vocabulary. These are small value types, not
driver handles or wire-format structs:

- identity: `context_id`, `device_id`, `stream_id`, `session_id`,
  `request_id`, `event_id`, `command_id`;
- lifecycle: `device_epoch`, `worker_generation`, `worker_key`;
- device and IO metadata: `device_info`, `memory_kind`, `queue_kind`,
  `copy_kind`, `model_io_info`, `model_io_descriptor`;
- completion and errors: `command_status`, `error_kind`, `operation_error`,
  `command_error`, and typed `error`.

The current stable `error_kind` set covers invalid context/binding/buffer/memory,
size mismatch, coherence requirement, invalid event, command failure, timeout,
abort, user exception, stale session, device lost, drain freeze, late response,
worker fault, protocol error, and unknown.

## Mock Backend

`forge::accel::mock::context` owns the reference backend. Its destructor calls
`shutdown()` and `wait()`, so destruction can block while accepted work drains or
stops.

```cpp
auto options = forge::accel::mock::context_options{};
options.thread_count = 2;
options.queue_capacity = 8;
options.device_count = 2;
options.memory = resource; // non-owning, optional

forge::accel::mock::context ctx{options};
```

`memory` is a non-owning `std::pmr::memory_resource*` and must outlive the
context, buffers, and pending work that use it. The resource controls the mock
context state, internal runtime/strand queues, command records, sessions, and
owning buffers. It does not make mock memory pinned, mapped, managed, or backed
by a vendor allocator.

## CPU Reference Backend

`forge::accel::cpu::context` is a real CPU-work reference backend. It uses the
same queue/copy/submit/event vocabulary as the mock backend, but intentionally
does less fault injection: no sessions, packets, trace sink, cached-memory
coherence proof, or model runtime. Its purpose is to validate that portable
accelerator-shaped code can run useful work through a non-mock backend before
any vendor SDK proof is approved.

```cpp
forge::accel::cpu::context ctx{forge::accel::cpu::context_options{
    .thread_count = 2,
    .queue_capacity = 8,
}};

auto copy_q = ctx.get_queue(forge::accel::queue_kind::copy);
auto compute_q = ctx.get_queue(forge::accel::queue_kind::compute);
forge::accel::cpu::device_buffer<float> device{ctx, 1024};
```

CPU `device_buffer<T>` owns 64-byte aligned storage through the configured
`std::pmr::memory_resource`. H2D/D2H/D2D commands perform real element copies
between host spans and this aligned storage. `submit(q, callable)` runs user
work on the queue's serialized lane, so examples can use `std::simd` over
`device_buffer<T>::span()` without inventing a vendor kernel interface.

The CPU backend keeps the same lifecycle shape:

- `close()` rejects future command admission and drains accepted work;
- `request_stop()` asks pending work and event waits to stop;
- `shutdown()` is `close()` plus `request_stop()`;
- `wait()` drains accepted work and returns immediately when called from backend
  work to avoid self-deadlock.

The CPU backend still is not CUDA/HIP/SYCL, does not expose native handles, and
does not model hardware queues, DMA, driver reset, pinned memory, or kernel
preemption. It is a portable reference backend for the command vocabulary.

The lifecycle verbs match the rest of `forge::`:

- `close()` rejects future command admission and lets accepted work drain;
- `request_stop()` asks pending work to stop; running user callables are not
  force-interrupted;
- `shutdown()` is `close()` plus `request_stop()`;
- `wait()` blocks until accepted work is done; if called from an accel command
  completion path it returns immediately to avoid self-deadlock.

Queue capacity is context-wide for accepted command work. When capacity is full,
newly started command senders complete stopped. Receiver stop tokens are checked
at `start()`; after a command is accepted into a serial queue, per-command stop is
not modeled. Use context shutdown, device/session reset, or explicit event/fence
ordering for runtime-level control.

## Queues and Commands

`context::get_queue(kind)` creates or returns a lightweight queue handle.
`device::get_queue(kind)` creates a device-bound queue. Each queue owns a
`forge::strand`, so work on one queue is FIFO and single-lane, while different
queues may progress concurrently depending on `thread_count`.

```cpp
auto copy_q = ctx.get_queue(forge::accel::queue_kind::copy);
auto compute_q = ctx.get_queue(forge::accel::queue_kind::compute);

forge::accel::mock::copy_to_device(copy_q, device, host_span);
forge::accel::mock::copy_to_host(copy_q, host_span, device);
forge::accel::mock::copy_device_to_device(copy_q, dst, src);
forge::accel::mock::submit(compute_q, [&] {
    for (auto& value : device.span()) {
        value *= 2;
    }
});
```

Default command APIs complete with:

```cpp
std::execution::completion_signatures<
    std::execution::set_value_t(),
    std::execution::set_error_t(std::exception_ptr),
    std::execution::set_stopped_t()>
```

Opt-in typed variants keep the same behavior but surface errors as
`forge::accel::error`:

```cpp
forge::accel::mock::copy_to_device_typed(q, buffer, host);
forge::accel::mock::submit_typed(q, callable);
forge::accel::mock::submit_packet_typed(session, packet, handler, options);
forge::accel::mock::record_event_typed(q, ev);
forge::accel::mock::wait_event_typed(q, ev);
forge::accel::mock::fence_typed(q);
```

Typed command sender can cross `forge::erased_sender` and can be consumed with
`forge::wait_result`:

```cpp
using command = std::execution::completion_signatures<
    std::execution::set_value_t(),
    std::execution::set_error_t(forge::accel::error),
    std::execution::set_stopped_t()>;

forge::erased_sender<command> op{
    forge::accel::mock::copy_to_device_typed(q, buffer, host)};
auto result = forge::wait_result(std::move(op));
```

## Memory and Buffers

`memory_kind` is portable metadata:

- `host`: ordinary owning host staging storage;
- `pinned_host`: pinned-like host staging metadata only;
- `mapped_host`: mapped/shared-like host metadata only;
- `device`: ordinary mock device storage;
- `cached_device`: mock storage requiring explicit coherence commands;
- `managed`: shared-like metadata accepted for host and device buffers.

`host_buffer<T>` and `device_buffer<T>` own mock storage and require trivially
copyable `T`. Byte aliases are available as `host_byte_buffer` and
`device_byte_buffer`.

Invalid host/device kind combinations throw `operation_error{invalid_memory_kind}`.
Size mismatches throw `operation_error{size_mismatch}`.

`cached_device` intentionally catches missing coherence boundaries:

- after H2D into cached device memory, call `flush(q, buffer)` before host-side or
  copy-source reads;
- after D2D writes into cached device memory, call `invalidate(q, buffer)` before
  host-side or copy-source reads.

These commands are mock proof rules, not a hardware cache model. Direct
`device_buffer<T>::span()` exposes raw mock storage and is intended for examples
and `submit` callables.

## Events and Fences

`mock::event` is a copyable shared generation marker. It starts unrecorded and
unready. `record_event(q, ev)` reserves the next generation at sender start and
publishes it when the queued record command completes.

```cpp
forge::accel::mock::event uploaded;

std::execution::sync_wait(forge::accel::mock::copy_to_device(copy_q, device, host));
std::execution::sync_wait(forge::accel::mock::record_event(copy_q, uploaded));

auto snapshot = std::execution::sync_wait(
    forge::accel::mock::query_event(uploaded));

std::execution::sync_wait(forge::accel::mock::wait_event(compute_q, uploaded));
std::execution::sync_wait(forge::accel::mock::synchronize_event(compute_q, uploaded));
std::execution::sync_wait(forge::accel::mock::fence(compute_q));
```

`query_event` returns `event_snapshot{record_generation, completed_generation,
ready}` without blocking. `wait_event` captures the target generation at sender
start. `synchronize_event` waits for the latest generation already reserved at
start; if no generation exists, it is a queue no-op boundary. `fence(q)` is a
no-op queued command used to observe that prior accepted work on the queue has
reached a boundary.

Events are not native CUDA/HIP/SYCL handles, timeline semaphores, dependency
graphs, or cycle detectors. Same-queue wait-before-record can block that queue
until timeout or context stop; use events as explicit cross-queue or already
ordered boundaries.

## Devices, Sessions, and Recovery

Mock contexts create `device_count` devices. Device metadata is synthetic and
portable:

```cpp
auto dev = ctx.get_device(forge::accel::device_id{0});
auto infos = ctx.device_infos();
auto devices = ctx.devices();
```

`device.open_session()` creates a command/session lane bound to the device epoch.
`device.get_queue(kind)` creates a device-bound queue. Device-bound work checks
device availability and worker generation before execution.

`device.mark_lost()` makes the device unavailable. Not-yet-running device-bound
commands complete with `error_kind::device_lost`; already running callables are
not force-interrupted. `device.reset()` clears the mock lost flag and increments
`device_epoch`. Existing sessions remain bound to the old epoch and later fail
with `error_kind::stale_session`; a new session binds the new epoch.

`device_session::reset()` marks only that session as reset. Queued session work
that has not started completes stopped.

Drain and worker fault simulation:

- `device.begin_drain_freeze()` rejects new device work with
  `error_kind::drain_freeze`; already accepted work drains;
- `device.complete_drain()` unfreezes and increments `worker_generation`;
- `device.mark_worker_fault()` rejects work with `error_kind::worker_fault`;
- `device.clear_worker_fault(expected_generation)` clears only if the expected
  generation matches, then advances the generation.

This models user-space runtime stale-handle and worker-instance boundaries. It
does not model driver reload, firmware reset, or native context rebuild.

## Message, Packet, and Request Runtime Proofs

`submit_message(session, request, response, handler)` is the borrowed response
form. `response` must outlive command completion.

`submit_packet(session, command_packet{...}, handler, options)` owns request and
response storage inside sender state and returns the completed packet on success.
`command_options::timeout` is measured from sender `start()`, before the command
enters the session queue. Timeout completes with `error_kind::timeout` but does
not interrupt a handler that already started.

`mock::request_session` builds a small request/response runtime on top of
`device_session`: it assigns monotonically increasing `request_id` values,
tracks pending requests, supports optional timeout, and counts late responses.
Use it when the caller wants posted or synchronous request correlation without
owning a raw callback record.

## Protocol Envelope Proof

`protocol_envelope` is portable message vocabulary for runtime experiments. It
separates:

- `message_kind`: request, response, notify, signal;
- `protocol_route`: source and destination endpoints;
- `protocol_meta`: request/session/context/stream IDs;
- `module_id` and `command_id`;
- owning `protocol_payload`;
- optional `lifecycle_signal`.

`mock::protocol::loopback_transport` is an in-memory proof with request and
completion/signal channels. Responses are accepted only while their request ID
is pending; unknown or late responses are discarded and counted. Lifecycle
signals bypass the pending map.

This proof is not a packed ABI, ioctl contract, kernel/userspace contract, SDK
message struct, or serialization format.

## Model Execute Proof

`mock::model` describes NPU-style model/session/IO-binding behavior without
tensors or model formats:

```cpp
forge::accel::mock::model model{forge::accel::mock::model_descriptor{
    .inputs = {forge::accel::model_io_descriptor{.byte_size = 4}},
    .outputs = {forge::accel::model_io_descriptor{.byte_size = 4}},
}};

auto session = model.open_session(ctx.get_device());
forge::accel::mock::model_bindings bindings{model};
bindings.bind_input(0, std::span<const std::byte>{input});
bindings.bind_output(0, std::span<std::byte>{output});

auto op = forge::accel::mock::execute(session, std::move(bindings));
```

`execute` validates that required inputs/outputs are bound and byte sizes match,
then fills outputs with a deterministic mock byte pattern. It proves async
execution, binding lifetime, and error handling; it is not a numerical inference
engine.

## Trace Sink

`mock::trace_sink` is an optional in-memory telemetry proof. It observes the mock
backend state machine; it is not a production profiler.

```cpp
forge::accel::mock::trace_sink trace;
auto options = forge::accel::mock::context_options{};
options.trace = &trace;

forge::accel::mock::context ctx{options};
auto q = ctx.get_queue();

std::execution::sync_wait(forge::accel::mock::submit(q, [] {}));
auto events = trace.snapshot();
```

`context_options::trace` is non-owning and must outlive the context and pending
work. Disabled tracing changes no command behavior. Recording failures are
ignored so trace allocation cannot turn a command into an error.

Trace events include command submitted/started/completed/stopped/error/timeout,
device-lost and session-stale markers, lifecycle signals, context/device/session
/stream IDs, command IDs where available, device epoch, and worker generation.
`trace_sink` uses a mutex-protected PMR vector and never calls user code.

The mock backend deliberately avoids Perfetto, ETW, LTTng, OpenTelemetry, vendor
timestamp correlation, and native driver timestamps.

## Ownership Rules

- Host spans are borrowed and must outlive command completion.
- `submit_message` borrows its response object; `submit_packet` owns its packet.
- `model_bindings` stores borrowed byte spans.
- `host_buffer<T>` and `device_buffer<T>` must outlive commands that capture
  them.
- Moving or destroying buffers while commands are pending is caller error.
- Same-queue access is serialized; cross-queue access to the same buffer is the
  caller's responsibility and should be ordered with events/fences.
- User completion callbacks are not invoked under accel internal mutexes.

## Examples

Progressive examples:

- `example/forge_accel_copy_example.cpp`: simple H2D/D2H copy;
- `example/forge_accel_pipeline_example.cpp`: H2D -> submit -> D2H;
- `example/forge_accel_cpu_copy_example.cpp`: CPU reference H2D/D2H copy with
  aligned device storage;
- `example/forge_accel_cpu_pipeline_example.cpp`: CPU reference copy/compute
  queue ordering with events;
- `example/forge_accel_cpu_simd_example.cpp`: CPU reference submit running
  `std::simd` over aligned device storage;
- `example/forge_accel_backend_switch_example.cpp`: the same command vocabulary
  logic run against mock and CPU reference backends;
- `example/forge_io_accel_pipeline_example.cpp`: Linux IO read/write handoff
  into the CPU reference accel queue;
- `example/forge_accel_event_example.cpp`: cross-queue event generations,
  query, wait, synchronize, and fence;
- `example/forge_accel_memory_example.cpp`: memory kinds, byte buffers, cached
  flush/invalidate proof, typed coherence error;
- `example/forge_accel_staging_buffer_example.cpp`: owning host staging buffers;
- `example/forge_accel_message_device_example.cpp`: borrowed message command;
- `example/forge_accel_session_reset_example.cpp`: session reset, device lost,
  stale session, and recovery;
- `example/forge_accel_packet_example.cpp`: owning command packet and timeout;
- `example/forge_accel_request_runtime_example.cpp`: request IDs, sync/post
  request handling, and typed error boundary;
- `example/forge_accel_protocol_transport_example.cpp`: envelope route/meta,
  response, late response discard, and lifecycle signal;
- `example/forge_accel_model_example.cpp`: model/session/IO-binding proof;
- `example/forge_accel_typed_error_example.cpp`: typed accel error through
  `forge::erased_sender` and `forge::wait_result`;
- `example/forge_accel_trace_example.cpp`: optional command timeline trace;
- `example/forge_inference_runtime_sketch.cpp`: channel + accel queue sketch;
- `example/forge_reference_runtime_example.cpp`: owning request/response service
  with bounded ingress, typed accel boundary, serialized stats, and graceful
  drain.

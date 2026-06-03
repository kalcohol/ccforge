# `forge::accel`

`forge::accel` 是 Forge 的 accelerator-shaped runtime support layer。它提供三层内容：

- `forge::accel` 中的 backend-neutral vocabulary；
- `forge::accel::mock` 中的 dependency-free mock / fault-injection backend；
- `forge::accel::cpu` 中的 dependency-free CPU/SIMD reference backend。

Mock backend 使用 CPU storage 和 Forge runtime primitives，模拟 accelerator work 在工程上
常见的形状：

- context、device、queue、session、epoch 和 worker generation；
- host / device / staging buffer，以及 `memory_kind` metadata；
- H2D、D2H、D2D copy、coherence command 和 kernel-like `submit`；
- queue 之间的 event / fence ordering；
- request/response command packet、protocol envelope 和 typed error；
- 用于 state-machine inspection 的可选 in-memory telemetry。

它不绑定 CUDA、HIP、SYCL、OpenCL、Vulkan、FPGA SDK、NPU SDK、kernel driver、
firmware、tensor graph 或 model-serving policy。未来真实 backend proof 必须保持可选，
并且先映射回这些 portable contracts，再暴露 backend-specific extension。Backend entry
rules 记录在 [`forge::accel` backend SPI sketch](roadmap/forge-accel-backend-spi.md)
和 [backend proof policy](roadmap/forge-backend-proof-policy.md) 中。

Mock backend 还运行仓库内的 `forge_accel_backend_conformance` test suite。这个 suite
记录 future backend proof 必须满足的 portable contract。CPU backend 也运行同一套
portable conformance suite，因此 vocabulary 不只在 mock state machine 上验证。

```cpp
#include <forge/accel.hpp>
```

## Vocabulary

`forge::accel` owns portable vocabulary。这些都是小 value types，不是 driver handle 或
wire-format struct：

- identity：`context_id`、`device_id`、`stream_id`、`session_id`、`request_id`、
  `event_id`、`command_id`；
- lifecycle：`device_epoch`、`worker_generation`、`worker_key`；
- device / IO metadata：`device_info`、`memory_kind`、`queue_kind`、`copy_kind`、
  `model_io_info`、`model_io_descriptor`；
- completion / error：`command_status`、`error_kind`、`operation_error`、
  `command_error` 和 typed `error`。

当前稳定的 `error_kind` 覆盖 invalid context / binding / buffer / memory、size mismatch、
coherence requirement、invalid event、command failure、timeout、abort、user exception、
stale session、device lost、host lost、drain freeze、late response、worker fault、
protocol error 和 unknown。`error_kind_to_string(kind)` 与
`command_status_to_string(status)` 提供稳定诊断字符串，方便 log、trace 和 framework
边界统一展示。

## Mock backend

`forge::accel::mock::context` owns reference backend。它的 destructor 会调用
`shutdown()` 和 `wait()`，因此析构可能在 accepted work drain 或 stop 时阻塞。

```cpp
auto options = forge::accel::mock::context_options{};
options.thread_count = 2;
options.queue_capacity = 8;
options.device_count = 2;
options.memory = resource; // non-owning, optional

forge::accel::mock::context ctx{options};
```

`memory` 是 non-owning `std::pmr::memory_resource*`，必须活得比 context、buffer 和
pending work 更久。该 resource 控制 mock context state、internal runtime / strand
queue、command record、session 和 owning buffer 的分配。它不会把 mock memory 变成
pinned、mapped、managed，也不会接入 vendor allocator。

## CPU reference backend

`forge::accel::cpu::context` 是真实 CPU-work reference backend。它使用与 mock backend
相同的 queue / copy / submit / event vocabulary，但故意减少 fault injection：不提供
session、packet、trace sink、cached-memory coherence proof 或 model runtime。它的用途是
在任何 vendor SDK proof 被批准之前，验证 portable accelerator-shaped code 能通过一个
非 mock backend 执行有用 work。

```cpp
forge::accel::cpu::context ctx{forge::accel::cpu::context_options{
    .thread_count = 2,
    .queue_capacity = 8,
}};

auto copy_q = ctx.get_queue(forge::accel::queue_kind::copy);
auto compute_q = ctx.get_queue(forge::accel::queue_kind::compute);
forge::accel::cpu::device_buffer<float> device{ctx, 1024};
```

CPU `device_buffer<T>` 通过配置的 `std::pmr::memory_resource` own 64-byte aligned
storage。H2D / D2H / D2D command 会在 host span 和 aligned storage 之间执行真实 element
copy。`submit(q, callable)` 在 queue 的 serialized lane 上运行 user work，因此示例可以在
`device_buffer<T>::span()` 上使用 `std::simd`，而不需要发明 vendor kernel interface。

CPU backend 保持与其它 Forge runtime 相同的 lifecycle shape：

- `close()` 拒绝后续 command admission，并 drain accepted work；
- `request_stop()` 请求 pending work 和 event wait 停止；
- `shutdown()` 是 `close()` + `request_stop()`；
- `wait()` drain accepted work；如果从 backend work 内部调用，则立即返回以避免
  self-deadlock。

CPU backend 仍然不是 CUDA / HIP / SYCL，不暴露 native handle，也不建模 hardware queue、
DMA、driver reset、pinned memory 或 kernel preemption。它只是 command vocabulary 的
portable reference backend。

Queue capacity 是 context-wide 的 accepted command work 容量。容量满时，新启动的 command
sender 完成为 stopped。Receiver stop token 在 `start()` 时检查；command accepted 进
serial queue 后，per-command stop 不在 v1 建模。需要 runtime-level control 时，使用
context shutdown、device/session reset，或显式 event/fence ordering。

## Queues and commands

`context::get_queue(kind)` 创建或返回 lightweight queue handle。`device::get_queue(kind)`
创建 device-bound queue。每个 queue owns 一个 `forge::strand`，所以同一 queue 内 work
FIFO 且 single-lane；不同 queue 是否并行推进取决于 `thread_count`。

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

默认 command API 的 completion signatures 是：

```cpp
std::execution::completion_signatures<
    std::execution::set_value_t(),
    std::execution::set_error_t(std::exception_ptr),
    std::execution::set_stopped_t()>
```

Opt-in typed variants 保持相同行为，但把 error surface 成 `forge::accel::error`：

```cpp
forge::accel::mock::copy_to_device_typed(q, buffer, host);
forge::accel::mock::submit_typed(q, callable);
forge::accel::mock::submit_packet_typed(session, packet, handler, options);
forge::accel::mock::record_event_typed(q, ev);
forge::accel::mock::wait_event_typed(q, ev);
forge::accel::mock::fence_typed(q);
```

Typed command sender 可以跨过 `forge::erased_sender`，也可以用 `forge::wait_result`
同步消费：

```cpp
using command = std::execution::completion_signatures<
    std::execution::set_value_t(),
    std::execution::set_error_t(forge::accel::error),
    std::execution::set_stopped_t()>;

forge::erased_sender<command> op{
    forge::accel::mock::copy_to_device_typed(q, buffer, host)};
auto result = forge::wait_result(std::move(op));
```

## Memory and buffers

`memory_kind` 是 portable metadata：

- `host`：普通 owning host staging storage；
- `pinned_host`：仅表示 pinned-like host staging metadata；
- `mapped_host`：仅表示 mapped/shared-like host metadata；
- `device`：普通 mock device storage；
- `cached_device`：需要显式 coherence command 的 mock storage；
- `managed`：host 和 device buffer 都接受的 shared-like metadata。

`host_buffer<T>` 和 `device_buffer<T>` own mock storage，并要求 `T` 是 trivially copyable。
Byte aliases 是 `host_byte_buffer` 和 `device_byte_buffer`。

非法 host/device kind 组合抛出 `operation_error{invalid_memory_kind}`。Size mismatch 抛出
`operation_error{size_mismatch}`。

`cached_device` 故意用来捕获缺失的 coherence boundary：

- H2D 写入 cached device memory 后，在 host-side 或 copy-source read 前调用
  `flush(q, buffer)`；
- D2D 写入 cached device memory 后，在 host-side 或 copy-source read 前调用
  `invalidate(q, buffer)`。

这些 command 是 mock proof rules，不是 hardware cache model。`device_buffer<T>::span()`
暴露 raw mock storage，主要用于 example 和 `submit` callable。

## Events and fences

`mock::event` 是 copyable shared generation marker。它初始为 unrecorded / unready。
`record_event(q, ev)` 在 sender start 时 reserve 下一代 generation，并在 queued record
command 完成时发布该 generation。

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

`query_event` 返回
`event_snapshot{record_generation, completed_generation, ready}`，不会阻塞。
`wait_event` 在 sender start 时捕获 target generation。`synchronize_event` 等待 start
时已经 reserved 的最新 generation；若还没有 generation，它只是 queue no-op boundary。
`fence(q)` 是 no-op queued command，用来观察该 queue 上先前 accepted work 是否已到达
boundary。

Event 不是 native CUDA/HIP/SYCL handle、timeline semaphore、dependency graph 或 cycle
detector。Same-queue wait-before-record 会阻塞该 queue，直到 timeout 或 context stop；
应把 event 用作 cross-queue 或 already-ordered boundary。

## Devices, sessions, and recovery

Mock context 创建 `device_count` 个 device。Device metadata 是 synthetic 且 portable 的：

```cpp
auto dev = ctx.get_device(forge::accel::device_id{0});
auto infos = ctx.device_infos();
auto devices = ctx.devices();
```

`device.open_session()` 创建绑定到当前 device epoch 的 command/session lane。
`device.get_queue(kind)` 创建 device-bound queue。Device-bound work 在执行前会检查 device
availability 和 worker generation。

`device.mark_lost()` 让 device unavailable。尚未运行的 device-bound command 会以
`error_kind::device_lost` 完成；已经运行中的 callable 不会被强制中断。
`device.reset()` 清除 mock lost flag，并递增 `device_epoch`。现有 session 仍绑定旧
epoch，之后会以 `error_kind::stale_session` 失败；新 session 绑定新 epoch。

`device_session::reset()` 只重置该 session。尚未启动的 queued session work 完成为 stopped。

Drain、heartbeat 和 worker fault simulation：

- `device.begin_drain_freeze()` 用 `error_kind::drain_freeze` 拒绝新的 device work；已
  accepted 的 work 继续 drain；
- `device.complete_drain()` 解除 freeze 并递增 `worker_generation`；
- `device.note_heartbeat()` 记录 worker liveness tick；
- `device.mark_heartbeat_timeout_if_stale(timeout)` 在 heartbeat 超时时 latch
  `error_kind::worker_fault`；
- `device.mark_worker_fault()` 用 `error_kind::worker_fault` 拒绝 work；
- `device.clear_worker_fault(expected_generation)` 仅在 expected generation 匹配时清除
  fault，然后推进 generation。

Host-lost cleanup 与 device-lost 不同：`device.begin_host_lost_cleanup()` 表示 host
侧会话丢失，mock 会冻结新的 device work admission，并用 `error_kind::host_lost` 拒绝
新 work；`device.complete_host_lost_cleanup()` 表示旧 worker/session state 已清退，mock
会推进 `device_epoch` 和 `worker_generation`，因此旧 session 变成 stale，新 session
才能接纳后续业务。

这建模的是 user-space runtime stale-handle 和 worker-instance boundary，不是 driver
reload、firmware reset 或 native context rebuild。

`current_device_guard` 是 thread-local host convenience，用于 framework-style glue 在当前
线程选择默认 device：

```cpp
{
    forge::accel::current_device_guard guard{forge::accel::device_id{0}};
    auto current = forge::accel::current_device();
}
```

它不会执行 hardware context switch，也不会跨线程传播。

## Message, packet, and request runtime proofs

`submit_message(session, request, response, handler)` 是 borrowed response form。
`response` 必须活到 command completion。

`submit_packet(session, command_packet{...}, handler, options)` 在 sender state 中 own
request / response storage，并在成功时返回 completed packet。`command_options::timeout`
从 sender `start()` 开始计时，早于 command 进入 session queue。Timeout 会以
`error_kind::timeout` 完成，但不会打断已经开始运行的 handler。

`mock::request_session` 在 `device_session` 之上构建小型 request/response runtime：它分配
单调递增的 `request_id`，追踪 pending request，支持可选 timeout，并统计 late response。
当 caller 需要 posted 或 synchronous request correlation，但不想自己 own raw callback
record 时，可以使用它。

## Protocol envelope proof

`protocol_envelope` 是 runtime experiment 用的 portable message vocabulary。它拆分：

- `message_kind`：request、response、notify、signal；
- `protocol_route`：source / destination endpoint；
- `protocol_meta`：request / session / context / stream IDs；
- `module_id` 和 `command_id`；
- owning `protocol_payload`；
- optional `lifecycle_signal`。

`mock::protocol::loopback_transport` 是 in-memory proof，包含 request 和
completion/signal channel。只有 request ID 仍在 pending map 中时，response 才会被接受；
unknown 或 late response 会被丢弃并计数。Lifecycle signal 会绕过 pending map。

Transport proof 区分两种 call mode：

- `posted`：host 只等待 admission / enqueue result，completion 后续从 completion
  channel 到达；
- `non_posted`：host 把匹配 response 当作 operation completion boundary。

两种模式共用同一条 transport 和 request-pending map。`submit_posted` /
`submit_non_posted` 返回 `transport_result`，可区分 `ok`、invalid message、duplicate
request、not accepted 和 late response。Legacy `submit_request` / `deliver_response`
仍返回 `bool`，只保留兼容的 accepted / rejected 语义。

这不是 packed ABI、ioctl contract、kernel/userspace contract、SDK message struct 或
serialization format。

## Model execute proof

`mock::model` 描述 NPU-style model / session / IO-binding behavior，不包含 tensor 或
model format：

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

`execute` 验证 required input/output 已绑定且 byte size 匹配，然后用 deterministic mock
byte pattern 填充 output。它证明 async execution、binding lifetime 和 error handling；
它不是 numerical inference engine。

## Trace sink

`mock::trace_sink` 是可选 in-memory telemetry proof。它观察 mock backend state machine；
它不是 production profiler。

```cpp
forge::accel::mock::trace_sink trace;
auto options = forge::accel::mock::context_options{};
options.trace = &trace;

forge::accel::mock::context ctx{options};
auto q = ctx.get_queue();

std::execution::sync_wait(forge::accel::mock::submit(q, [] {}));
auto events = trace.snapshot();
```

`context_options::trace` 是 non-owning，必须活得比 context 和 pending work 更久。关闭
tracing 不改变 command 行为。Recording failure 会被忽略，因此 trace allocation 不会把
command 变成 error。

Trace event 包含 command submitted / started / completed / stopped / error / timeout、
device-lost 和 session-stale marker、lifecycle signal、context/device/session/stream IDs、
command IDs、device epoch 和 worker generation。`trace_sink` 使用 mutex-protected PMR
vector，不会调用 user code。

Mock backend 故意避开 Perfetto、ETW、LTTng、OpenTelemetry、vendor timestamp correlation
和 native driver timestamp。

## Ownership rules

- Host spans 是 borrowed，必须活到 command completion。
- `submit_message` borrows response object；`submit_packet` owns packet。
- `model_bindings` 存储 borrowed byte spans。
- `host_buffer<T>` 和 `device_buffer<T>` 必须活得比捕获它们的 command 更久。
- Pending command 期间 move 或 destroy buffer 是 caller error。
- Same-queue access 会被序列化；cross-queue 访问同一 buffer 是 caller responsibility，
  应使用 event / fence 排序。
- User completion callback 不会在 accel internal mutex 下调用。

## Examples

建议按从简单到组合的顺序阅读：

- `example/forge_accel_copy_example.cpp`：简单 H2D / D2H copy；
- `example/forge_accel_pipeline_example.cpp`：H2D -> submit -> D2H；
- `example/forge_accel_cpu_copy_example.cpp`：CPU reference H2D / D2H copy，使用
  aligned device storage；
- `example/forge_accel_cpu_pipeline_example.cpp`：CPU reference copy / compute queue
  ordering with events；
- `example/forge_accel_cpu_simd_example.cpp`：CPU reference `submit` 在 aligned device
  storage 上运行 `std::simd`；
- `example/forge_accel_backend_switch_example.cpp`：同一份 command vocabulary logic 在
  mock 和 CPU reference backend 上运行；
- `example/forge_io_accel_pipeline_example.cpp`：Linux IO read/write handoff 到 CPU
  reference accel queue；
- `example/forge_accel_event_example.cpp`：cross-queue event generation、query、wait、
  synchronize 和 fence；
- `example/forge_accel_memory_example.cpp`：memory kinds、byte buffers、cached
  flush/invalidate proof 和 typed coherence error；
- `example/forge_accel_staging_buffer_example.cpp`：owning host staging buffers；
- `example/forge_accel_message_device_example.cpp`：borrowed message command；
- `example/forge_accel_session_reset_example.cpp`：session reset、device lost、stale
  session 和 recovery；
- `example/forge_accel_packet_example.cpp`：owning command packet 和 timeout；
- `example/forge_accel_request_runtime_example.cpp`：request IDs、sync/post request
  handling 和 typed error boundary；
- `example/forge_accel_protocol_transport_example.cpp`：envelope route/meta、response、
  late response discard 和 lifecycle signal；
- `example/forge_accel_model_example.cpp`：model/session/IO-binding proof；
- `example/forge_accel_typed_error_example.cpp`：typed accel error 跨过
  `forge::erased_sender` 和 `forge::wait_result`；
- `example/forge_accel_trace_example.cpp`：可选 command timeline trace；
- `example/forge_inference_runtime_sketch.cpp`：channel + accel queue sketch；
- `example/forge_reference_runtime_example.cpp`：owning request/response service，包含
  bounded ingress、typed accel boundary、serialized stats 和 graceful drain。

# `forge::accel` 使用说明

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

## 核心 vocabulary

`forge::accel` owns portable vocabulary。这些都是小 value types，不是 driver handle 或
wire-format struct：

- identity：`context_id`、`device_id`、`stream_id`、`session_id`、`request_id`、
  `event_id`、`module_id`、`command_id`；
- lifecycle：`device_epoch`、`worker_generation`、`worker_key`；
- device / IO metadata：`device_info`、`memory_kind`、`queue_kind`、`copy_kind`、
  `model_io_info`、`model_io_descriptor`；
- completion / error：`command_status`、`error_kind`、`operation_error`、
  `command_error` 和 typed `error`。

当前稳定的 `error_kind` 覆盖 invalid context / binding / buffer / memory、size mismatch、
coherence requirement、invalid event、command failure、timeout、abort、user exception、
stale session、device lost、host lost、drain freeze、late response、worker fault、
protocol error、resource exhausted 和 unknown。`error_kind_to_string(kind)` 与
`command_status_to_string(status)` 提供稳定诊断字符串，方便 log、trace 和 framework
边界统一展示。

## Mock backend 角色

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

## CPU reference backend 角色

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

## Queue 与 command

`context::get_queue(kind)` 创建或返回 lightweight queue handle。`device::get_queue(kind)`
创建 device-bound queue。每个 queue owns 一个 `forge::strand`，所以同一 queue 内 work
FIFO 且 single-lane；不同 queue 是否并行推进取决于 `thread_count`。

Mock backend 把 queue 视为 device worker stream 的 proof。`queue_kind::general` 和
device session 的 command queue 使用 `stream_id{0}` 作为 default stream；显式 copy /
compute queue 会分配非零 stream id。这对应常见 runtime 中“没有显式 stream 参数的 API
仍落到默认 stream”的语义。

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
forge::accel::mock::enqueue_callback_typed(q, dispatcher, id);
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

`query_stream(q)` 是 non-blocking stream snapshot，报告 stream id、queue kind、closed /
idle 状态、pending node 数和 sticky error。`synchronize_stream(q, options)` 是 host
blocking wait，只等待该 stream 变 idle 或 timeout，不等整个 context，也不同于 enqueue 一个
`fence(q)` sender：

```cpp
auto snapshot = forge::accel::mock::query_stream(compute_q);

auto sync = forge::accel::mock::synchronize_stream(
    compute_q,
    forge::accel::mock::stream_sync_options{.timeout = 100ms});
```

Sticky error 的 v3 proof 语义是：stream 记录第一条 non-success error，后续 node 仍继续
执行；`query_stream` 只观察不清除；`synchronize_stream` 默认返回并清除当前 sticky error。
这更接近 runtime 的 sync-point error aggregation，而不是把每个 stream 变成 error 后自动
跳过后续节点的 dependency graph。

## Memory 与 buffer

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

## Event 与 fence

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

Event wait 是 posted stream node，不阻塞 host 线程；真正等待发生在执行该 node 的
queue worker 上。为了避免单 worker 跨队列 record/wait 互相饿死，mock 和 CPU backend
默认使用 `thread_count = 2`。当未 ready 的 event wait 发现没有 spare worker budget 时，
typed path 报告 `error_kind::resource_exhausted`，exception path 抛出对应
`operation_error`，而不是静默挂死。

Event 不是 native CUDA/HIP/SYCL handle、timeline semaphore、dependency graph 或 cycle
detector。Same-queue wait-before-record 会阻塞该 queue，直到 timeout 或 context stop；
应把 event 用作 cross-queue 或 already-ordered boundary。

`elapsed_time(ev)` 在 event 已被 record 并完成后返回 mock steady-clock duration。它只用于
proof / profiling-style 教学，不表示 vendor timestamp correlation；未完成或 invalid event
会抛出 `operation_error{invalid_event}`。

## Host callback 机制

`host_callback_dispatcher` 是 stream-ordered device-to-host callback proof。Callback 不是
device 随时打断 host 的随机 interrupt；host 先注册 callback，再把 callback node 插入某条
stream。Worker FIFO 执行到该 node 时，在当前 stream queue 的 strand 上调用 dispatcher
并运行 callback body；callback 完成后记录 completion ACK，stream 上后续 node 才继续推进。

```cpp
forge::accel::mock::host_callback_dispatcher callbacks;
auto id = callbacks.register_callback([] {
    // host callback body, running as a stream-ordered queue node.
});

std::execution::sync_wait(
    forge::accel::mock::enqueue_callback(compute_q, callbacks, id));

auto completions = callbacks.completions();
```

`unregister_callback(id)` 会拒绝后续 invoke，并等待已经 in-flight 的 invoke 完成。Callback
body 不在 accel internal mutex 下运行；从 callback body 内 unregister 自己不会等待自己
完成，避免重入自锁。Callback 抛异常时，typed variant 会报告
`error_kind::user_exception`；callback id 不存在时报告 `error_kind::protocol_error`。

Callback node 在创建时捕获 `{callback_id, registration epoch}` 和 registry state，而不是在执行时
按 id 查找最新 handler。因此：

- 同一个 id 不能在仍 registered 时重复注册；要替换 handler，必须先
  `unregister_callback(id)`；
- unregister 后再 register 会产生新的 epoch，旧 queued node 不会跑新 handler，而是以
  stopped completion 结束；
- `close()` / `shutdown()` 阻止未来 callback body 执行；已 in-flight 的 body 可以完成；
- `wait()` 在 `close()` / `shutdown()` 之后是 registry barrier，会等待 in-flight body
  drain。尚未执行到 dispatcher 的 pending callback node 之后到达时会完成 stopped；
- callback body 仍然是用户代码，捕获的对象必须自行保证活到 body 返回。

`completions()` 返回 diagnostic history。默认只保留最近 1024 条 callback completion，避免
长生命周期 dispatcher 无界增长。可用 `host_callback_dispatcher_options::completion_capacity`
调整；设为 `std::nullopt` 表示显式选择 unbounded history，设为 `0` 表示不保留 history。

## Device、session 与 recovery

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

`current_stream_guard` / `current_stream()` 提供同样的 scoped thread-local stream
convenience。它通常由 framework backend glue 在进入一个 op dispatch boundary 时设置，
让普通 C++ callable 能观察“当前 stream”。

这些 guard 不会执行 hardware context switch，也不会跨线程传播。

## Power/resume 与 framework glue contract

Power/resume 的 portable default 是保守的：sleep 前先 quiesce / drain / fence；resume 后
重新 probe，并用 `device_epoch`、`session_id` 和 `worker_generation` 验证旧 handle 是否仍
有效。除非某个真实 backend proof 明确证明 command / kernel / session 能跨低功耗存活，
否则旧 session 应视为可能 stale，重新创建 session 是默认安全路径。

Mock proof 中可以用 host-lost cleanup 表达这一点：host 侧会话丢失后，device 侧先冻结新
admission，清退旧 worker/session state，完成后推进 epoch / generation。旧 session 随后
以 `error_kind::stale_session` 失败，新 session 才接纳业务。

Framework-style backend glue 的常见需求包括：

- device guard / stream guard；
- allocator 和 memory-pool contract；
- record-stream / tensor lifetime；
- event wrapper 和 per-stream synchronize；
- peer access enable/query；
- graph capture state-machine boundary；
- stream priority metadata；
- user trace range marker；
- async copy、op dispatch、model/session binding；
- typed error 到 framework error 的映射。

Forge 当前只把这些需求记录为 portable contracts 和 dependency-free examples。它不包含
PyTorch / CUDA / HIP / SYCL / ACL / CNRT header，也不实现 tensor graph、graph optimizer、
stream-ordered async allocation/free、native peer routing 或 vendor profiler integration。
这些能力如果进入仓库，必须作为单独 owner-gated backend proof，并先映射回这里的 portable
contracts。

Per-stream synchronize 已在 mock worker proof 中实现，语义是等待单条 stream 的 pending
node 归零，可选 timeout，并可观察/清除 sticky stream error。它不同于 whole-context
`wait()`，也不同于 tensor framework 的全设备同步。

## Message、packet 与 request runtime proof

`submit_message(session, request, response, handler)` 是 borrowed response form。
`response` 必须活到 command completion。

`submit_packet(session, command_packet{...}, handler, options)` 在 sender state 中 own
request / response storage，并在成功时返回 completed packet。`command_options::timeout`
从 sender `start()` 开始计时，早于 command 进入 session queue。Timeout 会以
`error_kind::timeout` 完成，但不会打断已经开始运行的 handler。

`command_packet` 可以携带 `module_id + command_id`。直接传 handler 时，handler 就是这个
packet node 的实现；使用 `command_dispatcher<Request, Response>` 时，mock backend 按
portable key 查 handler：

```cpp
forge::accel::mock::command_dispatcher<request, response> dispatch;
dispatch.register_handler(
    forge::accel::module_id{2},
    forge::accel::command_id{7},
    [](request& in, response& out) {
        out.value = in.value * 4;
    });

auto op = forge::accel::mock::submit_packet(
    session,
    forge::accel::mock::command_packet{
        forge::accel::module_id{2},
        forge::accel::command_id{7},
        request{11},
        response{}},
    dispatch);
```

这只是 module/command dispatch 的 portable proof：没有 handler 会以
`error_kind::protocol_error` 失败；handler 返回非 ok status 会沿既有 command error path
传播。

`mock::request_session` 在 `device_session` 之上构建小型 request/response runtime：它分配
单调递增的 `request_id`，追踪 pending request，支持可选 timeout，并统计 late response。
当 caller 需要 posted 或 synchronous request correlation，但不想自己 own raw callback
record 时，可以使用它。

## Protocol envelope proof（协议封装验证）

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

## Model execute proof（模型执行验证）

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

## Trace sink（追踪输出）

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
module/command IDs、device epoch 和 worker generation。Completed command event 还带
activity duration：`timestamp` 是 activity start，`end_timestamp` 是 activity end，
`has_end_timestamp` 表示该 duration 是否存在。`trace_sink` 使用 mutex-protected PMR
vector，不会调用 user code。

Mock backend 故意避开 Perfetto、ETW、LTTng、OpenTelemetry、vendor timestamp correlation
和 native driver timestamp。

## Ownership 规则

- Host spans 是 borrowed，必须活到 command completion。
- `submit_message` borrows response object；`submit_packet` owns packet。
- `model_bindings` 存储 borrowed byte spans。
- `host_buffer<T>` 和 `device_buffer<T>` 必须活得比捕获它们的 command 更久。
- Pending command 期间 move 或 destroy buffer 是 caller error。
- Same-queue access 会被序列化；cross-queue 访问同一 buffer 是 caller responsibility，
  应使用 event / fence 排序。
- User completion callback 不会在 accel internal mutex 下调用。

## 示例

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
- `example/forge_accel_callback_example.cpp`：stream-ordered host callback 和 completion
  ACK；
- `example/forge_accel_request_runtime_example.cpp`：request IDs、sync/post request
  handling 和 typed error boundary；
- `example/forge_accel_protocol_transport_example.cpp`：envelope route/meta、response、
  late response discard 和 lifecycle signal；
- `example/forge_accel_model_example.cpp`：model/session/IO-binding proof；
- `example/forge_accel_typed_error_example.cpp`：typed accel error 跨过
  `forge::erased_sender` 和 `forge::wait_result`；
- `example/forge_accel_trace_example.cpp`：可选 command timeline trace；
- `example/forge_accel_resume_revalidation_example.cpp`：resume 后 epoch/session
  revalidation sketch；
- `example/forge_accel_framework_glue_example.cpp`：不含 framework header 的 device guard /
  stream guard / per-stream synchronize sketch；
- `example/forge_inference_runtime_sketch.cpp`：channel + accel queue sketch；
- `example/forge_reference_runtime_example.cpp`：owning request/response service，包含
  bounded ingress、typed accel boundary、serialized stats 和 graceful drain。

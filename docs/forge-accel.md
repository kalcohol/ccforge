# `forge::accel`

`forge::accel` 是 Forge 的 accelerator-like 支撑层。V2 方向把 portable
runtime vocabulary 放在 `forge::accel`，并把 dependency-free reference backend
放在 `forge::accel::mock`。当前可执行后端是 portable mock/in-memory backend，用纯
CPU 存储和 Forge runtime 原语固定 command queue、device buffer、copy 和
kernel-like submit 的 sender 语义。

它不绑定 CUDA、HIP、SYCL、OpenCL、Vulkan、FPGA SDK 或 NPU driver，也不声明真实硬件
加速。真实 vendor/platform backend 只有在这个语义模型稳定并确实有价值后才应作为
独立轮次评估。
Future backend entry rules are tracked in the
[`forge::accel` backend SPI sketch](roadmap/forge-accel-backend-spi.md) and the
[backend proof policy](roadmap/forge-backend-proof-policy.md).

入口头：

```cpp
#include <forge/accel.hpp>
```

## portable vocabulary

`forge::accel` 本层提供 backend-neutral vocabulary。设备和运行时身份包括
`device_id`、`context_id`、`stream_id`、`session_id`、`request_id`、`event_id`、
`event_generation`、`device_epoch`、`worker_generation` 和 `worker_key`；队列、
内存和命令 vocabulary 包括 `device_info`、`memory_kind`、`queue_kind`、
`copy_kind`、`command_id`、`command_status`、`error_kind` 和 `model_io_info`。

这些类型是 Forge 的 portable proof vocabulary：它们不绑定 mock storage，不声明真实
硬件能力，不是 wire format、driver ABI 或 OS process identity。需要跨进程或跨设备
传输时，应由具体 backend/protocol proof 显式选择编码方式。

## mock reference backend

- `forge::accel::mock::context`：拥有型 mock accelerator context。析构会
  `shutdown()` + `wait()`，因此可能阻塞。
- `forge::accel::mock::device`：轻量 device handle，由 context 产生，不拥有真实硬件；
  可查询 `info()` / `available()`，并用 `mark_lost()` / `reset()` 模拟设备丢失边界。
- `forge::accel::mock::device_session`：mock device session，用于表达 NPU/FPGA 风格
  command/response 生命周期和 reset 边界。
- `forge::accel::mock::queue`：轻量 queue handle。`context::get_queue(kind)` 可创建
  `general`、`compute`、`copy` 或 `command` queue；每个 queue 独立 FIFO 串行执行
  command。
- `forge::accel::mock::host_buffer<T>`：由 context resource 分配的 owning host staging
  storage。可标注为 `host`、`pinned_host`、`mapped_host` 或 `managed` kind，但这些都是
  mock metadata，不会调用 OS/vendor pinned allocation。
- `forge::accel::mock::device_buffer<T>`：拥有 mock device storage。可标注为 `device`、
  `cached_device` 或 `managed` kind。当前 mock backend 要求 `T` trivially copyable。
- `forge::accel::mock::host_byte_buffer` / `device_byte_buffer`：byte-oriented owning
  buffers，用于 command packet、model IO 和不想先引入 tensor shape 的 proof。
- `forge::accel::mock::model` / `model_session` / `model_bindings`：NPU-style
  model/session/IO-binding proof。它只验证 byte-size metadata 和 borrowed byte spans，
  不实现 tensor、operator graph 或真实推理引擎。

`context_options` 可配置线程数、command queue 容量、mock device 数量和 resource：

```cpp
forge::accel::mock::context ctx{forge::accel::mock::context_options{
    .thread_count = 2,
    .queue_capacity = 8,
    .device_count = 2,
    .memory = resource,
}};
```

`memory` 是非拥有 `std::pmr::memory_resource*`，必须活得比使用它的 context 和
buffers 更久。

## commands

Mock command sender：

```cpp
auto copy_q = ctx.get_queue(forge::accel::queue_kind::copy);
auto compute_q = ctx.get_queue(forge::accel::queue_kind::compute);

forge::accel::mock::copy_to_device(copy_q, device, std::span<const T>{host});
forge::accel::mock::copy_to_host(copy_q, std::span<T>{host}, device);
forge::accel::mock::copy_device_to_device(copy_q, dst, src);
forge::accel::mock::flush(copy_q, cached_device);
forge::accel::mock::invalidate(copy_q, cached_device);
forge::accel::mock::submit(compute_q, [&] {
    for (auto& value : device.span()) {
        value *= 2;
    }
});
```

也可以通过 `device` 和 `device_session` 表达设备绑定 command：

```cpp
auto device = ctx.get_device();
auto q = device.get_queue(forge::accel::queue_kind::compute);
auto session = device.open_session();

forge::accel::mock::submit(q, [&] {
    // command for a device-bound queue
});

forge::accel::mock::submit(session, [&] {
    // command for a device-like lane
});
```

这些 sender 在 `start()` 时接受 command，而不是构造 sender 时。completion signatures：

```cpp
std::execution::completion_signatures<
    std::execution::set_value_t(),
    std::execution::set_error_t(std::exception_ptr),
    std::execution::set_stopped_t()>
```

Default APIs preserve the exception-based boundary used by the rest of the
runtime layer. Opt-in typed-error variants are available when a boundary needs a
closed error type:

```cpp
forge::accel::mock::copy_to_device_typed(q, device, std::span<const T>{host});
forge::accel::mock::copy_to_host_typed(q, std::span<T>{host}, device);
forge::accel::mock::copy_device_to_device_typed(q, dst, src);
forge::accel::mock::flush_typed(q, cached_device);
forge::accel::mock::invalidate_typed(q, cached_device);
forge::accel::mock::submit_typed(q, callable);
forge::accel::mock::submit_message_typed(session, request, response, handler);
forge::accel::mock::submit_packet_typed(session, packet, handler, options);
forge::accel::mock::record_event_typed(q, ev);
forge::accel::mock::wait_event_typed(q, ev);
forge::accel::mock::fence_typed(q);
```

Typed variants complete with:

```cpp
std::execution::completion_signatures<
    std::execution::set_value_t(),
    std::execution::set_error_t(forge::accel::error),
    std::execution::set_stopped_t()>
```

`forge::accel::error` carries:

- `kind`：`invalid_buffer`、`invalid_memory_kind`、`size_mismatch`、
  `coherence_required`、`invalid_event`、`command_failed`、`timeout`、`aborted`、
  `user_exception`、`unknown`，以及为 future backend 保留的 `invalid_context`；
- `status`：`submit_message_typed` 返回 `command_status::failed` 时保留 command
  status；
- `cause`：原始 `std::exception_ptr`，用于需要重新抛出或记录底层诊断的边界。

当前 typed errors 是 mock backend 的稳定小闭集；它们不试图声明 CUDA/HIP/SYCL 或
其它 vendor status model。

Typed command sender 可以直接跨 `forge::erased_sender` 边界，并用
`forge::wait_result` 同步消费：

```cpp
using command = std::execution::completion_signatures<
    std::execution::set_value_t(),
    std::execution::set_error_t(forge::accel::error),
    std::execution::set_stopped_t()>;

forge::erased_sender<command> op{
    forge::accel::mock::copy_to_device_typed(q, buffer, host)};
auto result = forge::wait_result(std::move(op));
```

## memory kinds and coherence proof

`memory_kind` 是 portable vocabulary。Mock backend 只用它来固定可测试的工程语义：

- `host`：普通 owning host staging buffer；
- `pinned_host`：pinned-like staging metadata，用于让示例表达“给设备传输准备的 host
  buffer”，但不做真实 pin；
- `mapped_host`：mapped/shared-like host metadata，不暴露 native mapping；
- `device`：普通 mock device storage；
- `cached_device`：需要显式 coherence operation 的 mock device storage；
- `managed`：host/device 两侧都允许构造的 shared-like metadata。

错误的构造组合会抛出 `operation_error{invalid_memory_kind}`。例如 host buffer 不能用
`device` kind，device buffer 不能用 `pinned_host` kind。

`cached_device` 用来捕捉常见 runtime 边界错误：

- H2D 写入 cached device buffer 后，需要先 `flush(q, buffer)`，再从 host 侧
  `copy_to_host` / 作为 D2D source 读取；
- D2D 写入 cached device buffer 后，需要先 `invalidate(q, buffer)`，再从 host 侧或
  另一个 copy command 读取。

这些规则是 mock proof，不是硬件 cache model。Direct `device_buffer<T>::span()` 是
raw mock storage access，适合 examples 和 `submit` callable；coherence 检查发生在
copy command 边界。用户仍需保证 buffer 不会在 pending command 期间被移动或销毁。

## lifecycle

- `close()`：拒绝后续 command，已接受 command 继续 drain。
- `request_stop()`：请求停止，pending command 尽量以 stopped 完成；正在运行的用户
  callable 不会被强制中断。
- `shutdown()`：`close()` + `request_stop()`。
- `wait()`：等待已接受 command work 完成或停止。若从 accel command completion
  内部调用，为避免自死锁会直接返回。
- 析构：执行 `shutdown()` + `wait()`。

Queue 容量当前按 context 统计已接受 command，而不是 per-queue。容量满时，新启动的
command 以 stopped 完成。receiver stop token 当前只在
`start()` 前检查；command 被接受到串行 queue 后不会因该 receiver token 后续 stop
而单独取消。需要取消已接受 command 时，使用 context `request_stop()` / `shutdown()`
或 `device_session::reset()`；这保持 mock command record 简单，避免在没有真实硬件
取消语义时引入第二套 per-command scheduler。

`device_session::reset()` 标记该 session 已 reset。之后尚未执行的 session command 会
以 stopped 完成；已经进入用户 callable 的 command 不会被强制中断。这模拟 NPU/FPGA
command channel 常见的 reset 边界，但不试图声明真实硬件 reset 语义。

## devices and lifecycle simulation

Mock context 默认创建一个 available device。测试和示例可以通过
`context_options::device_count` 构造 no-device 或 multi-device 场景：

```cpp
auto infos = ctx.device_infos();
auto devices = ctx.devices();
auto dev = ctx.get_device(forge::accel::device_id{0});
```

`device_info` 只描述 portable mock metadata，例如 ordinal、name 和 availability。它不
来自真实 OS/vendor probe。`device.get_queue(kind)` 创建绑定该 device 的 queue；
`device.open_session()` 创建绑定该 device 的 message/session lane。

`device.mark_lost()` 让 device 变为 unavailable。之后尚未运行的 device-bound
queue/session command 会完成 error：

- default API：`set_error(std::exception_ptr)`，其中保存
  `operation_error{error_kind::invalid_context, ...}`；
- typed API：`set_error(forge::accel::error{error_kind::invalid_context, ...})`。

已经进入用户 callable 的 command 不会被强制中断。`device.reset()` 只清除 mock lost
flag，用于继续测试后续 command；它不是 vendor device reset、driver reload 或 native
context rebuild 的模型。

## device sessions and message commands

`device_session` 是 vendor-neutral proof，不绑定 CUDA/HIP/SYCL，也不暴露 native
handle。它的用途是让用户把“向设备发送 command packet，等待 completion/response”的
工程形状写成 sender pipeline。

```cpp
struct request_packet { int count; };
struct response_packet { int count; };

response_packet response{};
auto op = forge::accel::mock::submit_message(
    session,
    request_packet{128},
    response,
    [](request_packet& request, response_packet& out) noexcept {
        out.count = request.count;
        return forge::accel::command_status::ok;
    });
```

`submit_message(session, request, response, handler)` 在 session queue 上运行 handler。
handler 可以返回：

- `command_status::ok`：正常 `set_value()`；
- `command_status::failed`：通过 `set_error(std::exception_ptr)` 传播
  `forge::accel::command_error`；typed variant 传播
  `forge::accel::error{error_kind::command_failed, ...}`；
- `command_status::stopped`：完成为 stopped。

handler 也可以返回 `void`，此时只要没有抛异常就视为成功。`response` 是 borrowed，
必须活到 command completion。

`submit_packet(session, packet, handler, options)` 是 owning command-packet 变体。
它把 request/response storage 放进 sender-owned state；成功时返回完成后的 packet：

```cpp
using packet_t = forge::accel::mock::command_packet<
    request_packet,
    response_packet>;

auto result = std::execution::sync_wait(
    forge::accel::mock::submit_packet(
        session,
        packet_t{
            forge::accel::command_id{1},
            request_packet{128},
            response_packet{}},
        [](request_packet& request, response_packet& out) noexcept {
            out.count = request.count;
            return forge::accel::command_status::ok;
        },
        forge::accel::mock::command_options{
            .timeout = std::chrono::seconds{1}}));
```

`command_options::timeout` is measured from sender `start()`, before the command
is queued on the session lane. If the command is still pending when the deadline
is reached, it completes with `operation_error{error_kind::timeout}` or typed
`error{error_kind::timeout}`. A timeout does not interrupt a handler that has
already started running.

Use `submit_message` for small borrowed response examples and `submit_packet`
for callback/completion-bridge style code where the response object should
survive asynchronous completion without a caller-owned borrowed lifetime.

## request/response runtime proof

`mock::request_session` builds a small runtime proof on top of `device_session`.
It allocates monotonically increasing `request_id` values, tracks an in-memory
pending set, and lets response delivery race with timeout/stop/shutdown while
still completing the caller exactly once.

```cpp
auto device = ctx.get_device();
forge::accel::mock::request_session requests{device.open_session()};

auto result = forge::wait_result(
    requests.submit_request(
        request,
        response,
        [](auto& request, auto& response) noexcept {
            response = handle(request);
        }));
```

The sender returned by `submit_request` is the operation boundary. A synchronous
API can wait on it with `sync_wait` or `wait_result`; a posted API can attach
`then` / `upon_error` / `upon_stopped` and start it detached. This keeps
"posted vs synchronous" as a caller policy rather than a separate runtime
mechanism.

Timeouts are pending-map timeouts. If a timeout wins before the queued response
handler runs, the caller observes `operation_error{error_kind::timeout}` (or
typed `error_kind::timeout` through `submit_request_typed`), the pending entry is
removed, and a later response is counted as a discarded late response. This is a
portable proof of request correlation, not a wire protocol or driver ABI.

## model/session execute proof

`mock::model` lets examples describe an accelerator-like model runtime without
introducing tensor semantics or a model format parser:

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

`execute` validates that every required input and output is bound and that each
binding matches the declared byte size. It then fills outputs with a deterministic
mock byte pattern derived from the inputs. The pattern only proves async
execution and binding lifetime; it is not a numerical inference result.

Model IO bindings are borrowed spans and must outlive execute completion.
`model::unload()` marks the mock model unavailable for later execute commands.
`model_session::reset()` uses the same session reset boundary as packet/message
commands. `execute_typed` maps validation failures to `forge::accel::error`.

## ownership

- Host spans 是 borrowed；调用方必须保证它们活到 command completion。
- `host_buffer<T>` 是 owning host storage，可用 `span()` 传给 copy command；它同样必须
  活到相关 command completion。
- `device_buffer<T>` 必须活到使用它的 command completion。
- `submit_packet` owns its request/response packet until terminal completion;
  `submit_message` still borrows the response reference.
- `model_bindings` stores borrowed byte spans; input/output storage must outlive
  `execute` completion.
- Buffer `kind()` 只是 portable metadata。`pinned_host`、`mapped_host`、`managed` 和
  `cached_device` 不声明真实 OS/vendor memory behavior。
- command 捕获的是 buffer object 地址和 borrowed span。command pending 期间移动或销毁
  参与的 `mock::host_buffer<T>` / `mock::device_buffer<T>` / host span 是调用方错误；
  当前 mock backend 不尝试
  pin 或自动延长这些对象的 lifetime。
- 当前 mock backend 串行化同一 queue 上的 buffer 访问。跨 queue 并发访问同一 buffer
  仍是调用方需要用 event/fence 明确排序的责任。
- User completion 不在 accel 内部 mutex 下执行。

## events and fences

Mock backend 提供 generation-based completion-boundary 事件。事件可用于同一
context 内不同 queue 之间的 ordering proof：

```cpp
forge::accel::mock::event uploaded;

std::execution::sync_wait(forge::accel::mock::copy_to_device(copy_q, device, std::span<const T>{host}));
std::execution::sync_wait(forge::accel::mock::record_event(copy_q, uploaded));
auto snapshot = std::execution::sync_wait(forge::accel::mock::query_event(uploaded));
std::execution::sync_wait(forge::accel::mock::wait_event(compute_q, uploaded));
std::execution::sync_wait(forge::accel::mock::synchronize_event(compute_q, uploaded));
std::execution::sync_wait(forge::accel::mock::fence(compute_q));
```

- `event` 是可复制的共享 generation 标记，默认未 ready。
- `event` 不绑定 context，control block 使用普通共享分配；它不继承
  `context_options::memory`。
- `record_event(q, ev)` 在 sender start/enqueue 时 reserve 下一代 generation，
  并作为 queue command 在完成时 publish 该 generation。
- `event::record_generation()` 是已 reserve 的最新 generation；
  `event::completed_generation()` 是已 publish 的最新 generation。`ready()` 等价于
  “至少有一次 record，且 completed generation 已追上最新 record generation”。
- `query_event(ev)` 非阻塞返回 `event_snapshot`，包含 record/completed generation
  和 ready 状态；`query_event_typed(ev)` 用 typed error surface 包装 invalid-event
  路径。
- `wait_event(q, ev)` 在 sender start/enqueue 时捕获目标 generation。若此前已有
  reserved generation，它等待该 generation publish；若 event 尚未 record，它等待
  第一代 generation。`event_wait_options{.timeout = ...}` 可设置超时。
- `synchronize_event(q, ev)` 是 fence-like wait boundary：它等待 start/enqueue
  当下已经 reserve 的最新 generation；若 event 尚未 record，它只是 queue 上的
  no-op wait boundary。
- `fence(q)` 是 queue 上的 no-op command，可作为“之前已接受 command 已到达”的
  sender 边界。
- 同一个 queue 内仍保持 FIFO；不同 queue 可以并发推进，实际并发度取决于
  `context_options::thread_count` 和 host scheduler。

这些 API 只描述 portable mock backend 的 completion boundary，主要用于“已经按顺序
record 后再 wait”的 queue 边界，或跨线程/跨 context 的轻量同步 proof。它们不是
native CUDA/HIP/SYCL event handle，不建模 dependency graph、timeline semaphore，
也不检测 dependency cycle。若把未 publish generation 的 `wait_event` 排在同一
queue 的对应 `record_event` 前面，该 queue 会等待到该 generation publish、timeout
或 context stop；调用方应按明确的 command 顺序使用它。

## examples

- `example/forge_accel_copy_example.cpp`
- `example/forge_accel_pipeline_example.cpp`
- `example/forge_accel_event_example.cpp`
- `example/forge_accel_memory_example.cpp`
- `example/forge_accel_staging_buffer_example.cpp`
- `example/forge_accel_message_device_example.cpp`
- `example/forge_accel_session_reset_example.cpp`
- `example/forge_accel_packet_example.cpp`
- `example/forge_accel_request_runtime_example.cpp`
- `example/forge_accel_model_example.cpp`
- `example/forge_accel_typed_error_example.cpp`
- `example/forge_inference_runtime_sketch.cpp`
- `example/forge_reference_runtime_example.cpp`

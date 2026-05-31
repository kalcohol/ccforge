# `forge::accel`

`forge::accel` 是 Forge 的 accelerator-like 支撑层。V1 是 portable
mock/in-memory backend，用纯 CPU 存储和 Forge runtime 原语固定 command queue、
device buffer、copy 和 kernel-like submit 的 sender 语义。

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

## core types

- `forge::accel::context`：拥有型 mock accelerator context。析构会
  `shutdown()` + `wait()`，因此可能阻塞。
- `forge::accel::device`：轻量 device handle，由 context 产生，不拥有真实硬件。
- `forge::accel::device_session`：mock device session，用于表达 NPU/FPGA 风格
  command/response 生命周期和 reset 边界。
- `forge::accel::queue`：轻量 queue handle。V1 单 queue 按 FIFO 串行执行 command。
- `forge::accel::host_buffer<T>`：由 context resource 分配的 owning host staging
  storage。它不是 pinned memory，只固定 staging buffer 的所有权和分配来源。
- `forge::accel::device_buffer<T>`：拥有 mock device storage。V1 要求
  `T` trivially copyable。

`context_options` 可配置线程数、command queue 容量和 resource：

```cpp
forge::accel::context ctx{forge::accel::context_options{
    .thread_count = 1,
    .queue_capacity = 8,
    .memory = resource,
}};
```

`memory` 是非拥有 `std::pmr::memory_resource*`，必须活得比使用它的 context 和
buffers 更久。

## commands

V1 command sender：

```cpp
forge::accel::copy_to_device(q, device, std::span<const T>{host});
forge::accel::copy_to_host(q, std::span<T>{host}, device);
forge::accel::copy_device_to_device(q, dst, src);
forge::accel::submit(q, [&] {
    for (auto& value : device.span()) {
        value *= 2;
    }
});
```

也可以通过 `device_session` 表达设备会话上的 command：

```cpp
auto device = ctx.get_device();
auto session = device.open_session();

forge::accel::submit(session, [&] {
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
forge::accel::copy_to_device_typed(q, device, std::span<const T>{host});
forge::accel::copy_to_host_typed(q, std::span<T>{host}, device);
forge::accel::copy_device_to_device_typed(q, dst, src);
forge::accel::submit_typed(q, callable);
forge::accel::submit_message_typed(session, request, response, handler);
forge::accel::record_event_typed(q, ev);
forge::accel::wait_event_typed(q, ev);
forge::accel::fence_typed(q);
```

Typed variants complete with:

```cpp
std::execution::completion_signatures<
    std::execution::set_value_t(),
    std::execution::set_error_t(forge::accel::error),
    std::execution::set_stopped_t()>
```

`forge::accel::error` carries:

- `kind`：`invalid_buffer`、`size_mismatch`、`invalid_event`、`command_failed`、
  `user_exception`、`unknown`，以及为 future backend 保留的 `invalid_context`；
- `status`：`submit_message_typed` 返回 `command_status::failed` 时保留 command
  status；
- `cause`：原始 `std::exception_ptr`，用于需要重新抛出或记录底层诊断的边界。

V1 的 typed errors 是 mock backend 的稳定小闭集；它们不试图声明 CUDA/HIP/SYCL 或
其它 vendor status model。

Typed command sender 可以直接跨 `forge::erased_sender` 边界，并用
`forge::wait_result` 同步消费：

```cpp
using command = std::execution::completion_signatures<
    std::execution::set_value_t(),
    std::execution::set_error_t(forge::accel::error),
    std::execution::set_stopped_t()>;

forge::erased_sender<command> op{
    forge::accel::copy_to_device_typed(q, buffer, host)};
auto result = forge::wait_result(std::move(op));
```

## lifecycle

- `close()`：拒绝后续 command，已接受 command 继续 drain。
- `request_stop()`：请求停止，pending command 尽量以 stopped 完成；正在运行的用户
  callable 不会被强制中断。
- `shutdown()`：`close()` + `request_stop()`。
- `wait()`：等待已接受 command work 完成或停止。若从 accel command completion
  内部调用，为避免自死锁会直接返回。
- 析构：执行 `shutdown()` + `wait()`。

Queue 容量满时，新启动的 command 以 stopped 完成。receiver stop token V1 只在
`start()` 前检查；command 入队后不注册 per-operation stop callback。

`device_session::reset()` 标记该 session 已 reset。之后尚未执行的 session command 会
以 stopped 完成；已经进入用户 callable 的 command 不会被强制中断。这模拟 NPU/FPGA
command channel 常见的 reset 边界，但不试图声明真实硬件 reset 语义。

## device sessions and message commands

`device_session` 是 vendor-neutral proof，不绑定 CUDA/HIP/SYCL，也不暴露 native
handle。它的用途是让用户把“向设备发送 command packet，等待 completion/response”的
工程形状写成 sender pipeline。

```cpp
struct request_packet { int count; };
struct response_packet { int count; };

response_packet response{};
auto op = forge::accel::submit_message(
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

## ownership

- Host spans 是 borrowed；调用方必须保证它们活到 command completion。
- `host_buffer<T>` 是 owning host storage，可用 `span()` 传给 copy command；它同样必须
  活到相关 command completion。
- `device_buffer<T>` 必须活到使用它的 command completion。
- command 捕获的是 buffer object 地址和 borrowed span。command pending 期间移动或销毁
  参与的 `host_buffer<T>` / `device_buffer<T>` / host span 是调用方错误；V1 不尝试
  pin 或自动延长这些对象的 lifetime。
- V1 单 queue 串行化同一 queue 上的 buffer 访问。跨 queue 并发访问尚未建模。
- User completion 不在 accel 内部 mutex 下执行。

## events and fences

V1 提供最小 completion-boundary 事件：

```cpp
forge::accel::event uploaded;

std::execution::sync_wait(forge::accel::copy_to_device(q, device, std::span<const T>{host}));
std::execution::sync_wait(forge::accel::record_event(q, uploaded));
std::execution::sync_wait(forge::accel::wait_event(q, uploaded));
std::execution::sync_wait(forge::accel::fence(q));
```

- `event` 是可复制的共享完成标记，默认未 ready。
- `event` 不绑定 context，control block 使用普通共享分配；它不继承
  `context_options::memory`。
- `record_event(q, ev)` 作为 queue command 运行，完成时把 `ev` 标记为 ready。
- `wait_event(q, ev)` 作为 queue command 运行，等待 `ev` ready；若 context stop，
  以 stopped 完成。
- `fence(q)` 是 queue 上的 no-op command，可作为“之前已接受 command 已到达”的
  sender 边界。

这些 API 只描述 portable mock backend 的 completion boundary，主要用于“已经按顺序
record 后再 wait”的 queue 边界，或跨线程/跨 context 的轻量同步 proof。它们不暴露
native CUDA/HIP/SYCL event handle，不建模跨 queue dependency graph，也不检测
dependency cycle。若把未 ready event 的 `wait_event` 排在同一 queue 的
`record_event` 前面，该 queue 会等待到 event ready 或 context stop；调用方应按
明确的 command 顺序使用它。

## examples

- `example/forge_accel_copy_example.cpp`
- `example/forge_accel_pipeline_example.cpp`
- `example/forge_accel_event_example.cpp`
- `example/forge_accel_staging_buffer_example.cpp`
- `example/forge_accel_message_device_example.cpp`
- `example/forge_accel_typed_error_example.cpp`
- `example/forge_inference_runtime_sketch.cpp`

# `forge::accel`

`forge::accel` 是 Forge 的 accelerator-like 支撑层。V1 是 portable
mock/in-memory backend，用纯 CPU 存储和 Forge runtime 原语固定 command queue、
device buffer、copy 和 kernel-like submit 的 sender 语义。

它不绑定 CUDA、HIP、SYCL、OpenCL、Vulkan、FPGA SDK 或 NPU driver，也不声明真实硬件
加速。真实 vendor/platform backend 只有在这个语义模型稳定并确实有价值后才应作为
独立轮次评估。

入口头：

```cpp
#include <forge/accel.hpp>
```

## Core Types

- `forge::accel::context`：拥有型 mock accelerator context。析构会
  `shutdown()` + `wait()`，因此可能阻塞。
- `forge::accel::queue`：轻量 queue handle。V1 单 queue 按 FIFO 串行执行 command。
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

## Commands

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

这些 sender 在 `start()` 时接受 command，而不是构造 sender 时。completion signatures：

```cpp
std::execution::completion_signatures<
    std::execution::set_value_t(),
    std::execution::set_error_t(std::exception_ptr),
    std::execution::set_stopped_t()>
```

## Lifecycle

- `close()`：拒绝后续 command，已接受 command 继续 drain。
- `request_stop()`：请求停止，pending command 尽量以 stopped 完成；正在运行的用户
  callable 不会被强制中断。
- `shutdown()`：`close()` + `request_stop()`。
- `wait()`：等待已接受 command work 完成或停止。若从 accel command completion
  内部调用，为避免自死锁会直接返回。
- 析构：执行 `shutdown()` + `wait()`。

Queue 容量满时，新启动的 command 以 stopped 完成。receiver stop token V1 只在
`start()` 前检查；command 入队后不注册 per-operation stop callback。

## Ownership

- Host spans 是 borrowed；调用方必须保证它们活到 command completion。
- `device_buffer<T>` 必须活到使用它的 command completion。
- V1 单 queue 串行化同一 queue 上的 buffer 访问。跨 queue 并发访问尚未建模。
- User completion 不在 accel 内部 mutex 下执行。

## Event/Fence Status

Standalone `event` / `record_event` / `wait_event` 没有进入 V1。当前 copy 和
`submit` sender 自身就是可组合的 completion boundary；如果 future backend 需要跨
queue event/fence，会作为独立小轮次补充，避免把 event 做成第二套 scheduler。

## Examples

- `example/forge_accel_copy_example.cpp`
- `example/forge_accel_pipeline_example.cpp`
- `example/forge_inference_runtime_sketch.cpp`

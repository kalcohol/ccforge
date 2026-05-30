# Forge runtime 远景图

本文记录 `include/forge/` 扩展设施的中长期方向。它不是标准 backport
计划，也不向 `namespace std` 增加名字。目标是让 Forge 在
`std::execution` backport 之上，提供一组小而实用的运行时原语，服务
结构化并发、消息通路、推理 runtime、设备/加速卡会话和资源生命周期管理。

## 当前基线

已交付的 `forge::` 设施包括：

- `static_thread_pool` / `single_thread_context` / `system_context`
- `timer_context` / `runtime_context`
- `async_scope`
- `bounded_channel`
- `resource_context`
- `strand`
- `io::context` (Linux fd readiness backend)
- `task`
- `any_scheduler`
- 窄 `any_sender_of` / `any_receiver_of`
- connectable `erased_sender` v1

这些设施的生命周期词汇由 `docs/forge-runtime.md` 固定：
`close()` 是 graceful ingress close，`request_stop()` 是协作取消，
`shutdown()` 是 close + stop，`wait()` 是阻塞 drain。拥有型 context 的析构允许
`shutdown()` + `wait()`，因此可能阻塞。

## 设计原则

- Backport 与 extension 分层清楚：标准设施在 `backport/`，Forge 扩展在
  `include/forge/`。
- 不克隆 NVIDIA stdexec/exec 的完整扩展栈；只吸收明确有用、能测试、能维护的小原语。
- 默认体验应可直接用；涉及平台、厂商或重依赖的功能使用 `AUTO`/`ON`/`OFF`
  feature gate。
- 结构化并发优先：资源生命周期、取消、drain、错误传播必须比裸线程/回调更清楚。
- Examples 是一等交付物。每个新设施都要有简单示例和至少一个组合示例，展示现代
  C++ 如何优雅地表达生命周期、并发、取消和资源边界。

## 推荐推进顺序

1. Resource policy / allocator policy
2. IO backend
3. `accel` scheduler and command pipeline
4. typed-error erased sender

顺序理由：

- Resource policy 是 IO 和 accel 的基础。队列节点、pending operation、timer item、
  command record、host/device staging buffer 都需要明确内存来源和容量策略。
- IO 和 accel 会引入平台/厂商后端。先统一资源策略，可以避免后端各自发明 allocation
  和 backpressure 规则。
- Typed-error erasure 最抽象。等 IO/accel 的真实错误形状出现后，再决定 vtable
  和 completion 策略更稳。

## Project Identity Checkpoint

Forge 的目标不是变成完整 runtime framework、网络库、GPU runtime、tensor runtime 或
vendor driver wrapper。更准确的身份是：

> C++ backport + 面向资源型异步系统的组合式支撑层。

Resource policy、IO readiness、`accel` command queue sketch 和 typed-error erasure
都应服务这个支撑层：抽出生命周期、调度、消息、资源、错误和组合方式这些共性，而不是
绑定某个具体平台或厂商栈。

具体要求：

- vendor/platform backend 只是验证支撑层是否能表达真实系统的 optional proof；
- 任何 vendor/platform backend 都必须有清楚的 optional gate、examples 和验证边界；
- 不以 "full stdexec parity" 或 "general-purpose networking/GPU framework" 为目标。
- 不把 CUDA/HIP/SYCL、IOCP、kqueue、io_uring、tensor kernel runtime 做成默认依赖。

## Portability And Windows Expectations

Linux 是当前最容易持续验证的平台，因为已有 podman 验证镜像和 `epoll/eventfd`
backend。Windows 支持需要被纳入远景，但不应通过在 Linux 代码里堆兼容分支来假装完成。

Windows 阶段性预期：

1. 基础 backport 与 `forge::` 纯 header/runtime 设施应能在 Windows + MSVC 或 clang-cl
   上 configure/build/test。
2. 在没有 IOCP backend 前，`FORGE_ENABLE_FORGE_IO=AUTO` 应跳过 IO backend；
   `FORGE_ENABLE_FORGE_IO=ON` 应给出清楚 configure error。
3. Linux-only IO headers 不应在 IO disabled 或非 Linux build 下破坏普通用户 include。
4. 若准备 Windows 机器，优先建立一个可重复执行的验证脚本，而不是依赖手工点击：
   - CMake configure/build with `FORGE_BUILD_TESTS=ON`;
   - `ctest` 覆盖 backport + non-IO `forge::` tests；
   - gate-off/gate-on configure 行为；
   - 未来 IOCP backend 单独挂在 `FORGE_ENABLE_FORGE_IO=AUTO/ON` 下。
5. 只有在 Windows 环境稳定后，才启动 IOCP taskbook。IOCP 与 `epoll` 的 completion
   语义不同，应作为独立 backend 设计，不应强行套 Linux fd readiness 状态机。

如果 owner 提供 Windows 主机，建议作为 self-hosted/manual verification 环境先接入；
正式 CI 化之前，至少记录可复现命令和预期 test count。
当前可复现入口见 `scripts/verify-windows-msvc.ps1` 和
`scripts/verify-windows-msvc-ssh.sh`；公开文档和脚本不得写入私有主机名或本地安装路径。

## Feature Gates

长期建议使用两类开关。

功能开关：

```cmake
FORGE_ENABLE_FORGE_RUNTIME=ON
FORGE_ENABLE_FORGE_RESOURCE_POLICY=ON
FORGE_ENABLE_FORGE_IO=AUTO
FORGE_ENABLE_FORGE_ACCEL=AUTO
FORGE_ENABLE_FORGE_TYPED_ERASURE=OFF
```

测试开关：

```cmake
FORGE_TEST_ENABLE_FORGE_RUNTIME=ON
FORGE_TEST_ENABLE_FORGE_RESOURCE=ON
FORGE_TEST_ENABLE_FORGE_IO=ON
FORGE_TEST_ENABLE_FORGE_ACCEL=ON
FORGE_TEST_ENABLE_FORGE_ERASURE=ON
```

`AUTO` 表示依赖可用时启用，不可用时跳过；显式 `ON` 缺依赖应报错。纯 header
设施不应因为全局 gate 变成不可 include；gate 主要控制 umbrella header、examples、
tests 和带外部依赖的 backend。IO gate 已用于 Linux `epoll`/`eventfd` readiness
backend；accel 和 typed-erasure gate 仍是惰性占位，不做 CUDA、HIP、SYCL 等探测。

## Resource Policy

Resource policy 解决实际 runtime 资源问题：

- 队列和 pending operation 的内存来源；
- bounded queue/channel 的容量和 backpressure；
- command/event record 的复用；
- host/device staging buffer 的分配；
- OOM 或 capacity full 时的 completion 策略。

V1 应优先使用 `std::pmr::memory_resource*` 作为稳定接口，而不是发明大型 policy
framework。需要如实记录限制：当前 `static_thread_pool` 使用
`std::deque<std::function<void()>>`，即使用 `pmr::deque` 也不能控制
`std::function` 内部 target allocation。若需要完全控制 task closure allocation，
应另开小任务引入 Forge 自有 move-only callable storage。

## IO Backend

IO backend 必须接触真实底层设施，否则只是多包一层线程池。V1 已收窄为 Linux
fd readiness backend；后续仍建议分三层推进：

- 通用 API 层：readiness sender、async read/write、close/shutdown；
- 后端层：Linux `epoll`/`eventfd` 起步，后续可评估 `io_uring`；Windows 是 IOCP，
  macOS/BSD 是 kqueue；
- 生命周期层：pending IO 挂到 `async_scope` / `resource_context`，析构时取消、关闭、等待。

第一版不承诺全平台。建议先做 Linux fd readiness backend，并把 Windows/IOCP 明确
defer。Zig 可以帮助构建和 C ABI 互操作，但不能消除 epoll/io_uring/IOCP 语义差异。

## Accel Scheduler

短命名采用 `forge::accel`，避免 `gpu` 过窄，也避免 `device` 与普通 IO 设备混淆。
目标覆盖 GPU、NPU、FPGA、DSP、专用推理卡和 GPGPU。

`accel` 的第一目标不是绑定 CUDA/HIP/SYCL。V1 应先从这些场景吸收共同结构：

- command queue / stream 的生命周期；
- event/fence 的 sender completion 形状；
- host/device/staging buffer 的资源策略；
- H2D、D2H、D2D copy 的 backpressure 和错误模型；
- kernel-like command 的提交、完成、取消和 drain 语义。

候选 surface：

```cpp
forge::accel::device
forge::accel::queue
forge::accel::stream
forge::accel::event
forge::accel::device_buffer
forge::accel::host_buffer
forge::accel::copy_to_device(...)
forge::accel::copy_to_host(...)
forge::accel::copy_device_to_device(...)
forge::accel::submit_kernel(...)
```

具体后端若未来需要，可放在：

```cpp
forge::accel::cuda
forge::accel::hip
forge::accel::sycl
```

核心接口不应强依赖 CUDA/HIP/SYCL。第一版建议用 mock/in-memory backend 和 examples
验证语义；只有当抽象能表达真实 pipeline 后，再选择一个可选 vendor/platform backend
做 proof。

## Typed-Error Erased Sender

当前 `forge::erased_sender` v1 支持多个 value shape，但 error 收敛为
`std::exception_ptr`。typed-error erasure 的目标是保留 `set_error_t(E)` 类型信息，
例如：

- `std::error_code` for IO；
- driver error code for CUDA/HIP；
- device status for FPGA/NPU；
- allocation failure / capacity exceeded；
- resource closed / operation canceled。

这是高价值但高复杂度设施。它需要多 value shape、多 error type、stopped 的 typed
completion vtable。应在 IO/accel 真实错误模型明确后单独推进。

## Examples Strategy

Examples 必须从“能编译”升级为“能教会人怎么组合”：

- `forge_resource_policy_example.cpp`：固定 arena + bounded pool/channel；
- `forge_bounded_pipeline_example.cpp`：thread pool + strand + channel + scope；
- `forge_io_readiness_example.cpp`：fd readiness sender + resource lifetime；
- `forge_accel_copy_example.cpp`：host/device copy + CPU continuation；
- `forge_accel_pipeline_example.cpp`：H2D -> kernel -> D2H -> CPU postprocess；
- `forge_inference_runtime_sketch.cpp`：请求 channel、strand 顺序控制、accel queue、
  scope 生命周期和 resource shutdown。

这些 examples 应避免营销式代码，重点展示“资源在哪里、取消如何传播、何时 drain、
错误如何处理、谁拥有谁”。

# Forge runtime 远景图

本文记录 `include/forge/` 扩展设施的中长期方向。它不是标准 backport
计划，也不向 `namespace std` 增加名字。目标是让 Forge 在
`std::execution` backport 之上，提供一组小而实用的运行时原语，服务
结构化并发、消息通路、推理 runtime、设备/加速卡会话和资源生命周期管理。
当前稳定交付基线和自循环验收规则见
[`forge stability baseline`](forge-stability-baseline.md)。

## 当前基线

已交付的 `forge::` 设施包括：

- `static_thread_pool` / `single_thread_context` / `system_context`
- `timer_context` / `runtime_context`
- `async_scope`
- `bounded_channel`
- `resource_context`
- `strand`
- `io::context` (Linux epoll/eventfd readiness + Windows IOCP proof)
- `accel::context` / `accel::device` / `accel::device_session` /
  `accel::queue` / `accel::device_buffer` / `accel::event`
- `accel` mock copy / submit / submit_message / event / fence command senders
- `resource_policy` and resource-backed pool callable storage
- `task`
- `any_scheduler`
- 窄 `any_sender_of` / `any_receiver_of`
- connectable `erased_sender` with closed-set typed errors
- opt-in `forge::io` typed-error sender variants
- opt-in `forge::accel` typed-error sender variants

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
4. typed-error integration for IO and accel boundaries

顺序理由：

- Resource policy 是 IO 和 accel 的基础。队列节点、pending operation、timer item、
  command record、host/device staging buffer 都需要明确内存来源和容量策略。
- IO 和 accel 会引入平台/厂商后端。先统一资源策略，可以避免后端各自发明 allocation
  和 backpressure 规则。
- Typed-error integration 最抽象。`erased_sender` 已能保留声明内的 typed error；
  IO 和 accel 都已有 opt-in typed variants。后续问题是具体 platform/vendor backend
  是否需要自己的错误映射，而不是默认扩大现有 API。

## project identity checkpoint

Forge 的目标不是变成完整 runtime framework、网络库、GPU runtime、tensor runtime 或
vendor driver wrapper。更准确的身份是：

> C++ backport + 面向资源型异步系统的组合式支撑层。

Resource policy、IO readiness、`accel` command queue sketch 和 typed-error integration
都应服务这个支撑层：抽出生命周期、调度、消息、资源、错误和组合方式这些共性，而不是
绑定某个具体平台或厂商栈。

具体要求：

- vendor/platform backend 只是验证支撑层是否能表达真实系统的 optional proof；
- 任何 vendor/platform backend 都必须有清楚的 optional gate、examples 和验证边界；
- 不以 "full stdexec parity" 或 "general-purpose networking/GPU framework" 为目标。
- 不把 CUDA/HIP/SYCL、IOCP、kqueue、io_uring、tensor kernel runtime 做成默认依赖。

## maintenance mode and decision gates

当前 `include/forge/` 设施已经进入较稳定的维护态。默认下一步应优先做：

- bug fixes and sanitizer-found lifetime fixes;
- docs/examples/cookbook，让现有设施更容易被正确组合；
- verification coverage，尤其是 Windows/MSVC smoke、gate-off/gate-on 行为和 sanitizer
  子集；
- 小而明确的 ergonomic helpers，前提是能复用现有 runtime/lifetime 模型。

以下事项仍在远景内，但不应在没有单独拍板和新任务书时顺手启动：

- 新平台 IO backend：macOS/BSD kqueue、Linux `io_uring`，或 Windows IOCP 的
  production hardening beyond the current proof；
- 真实 accelerator backend：CUDA/HIP/SYCL 或厂商 SDK proof；
- 真实 backend 的 vendor/platform typed-error mapping；
- 让标准 backport 的已知限制发生行为级变化，例如 throwing receiver completion、
  `ensure_started` 单发/取消语义、`spawn_future` 更完整 allocator 传播。

每次启动这些大项前，先写一份总计划和若干子任务书，明确 gate、examples、测试矩阵和
回滚边界。没有明确收益或验证条件时，维持现状比扩大 surface 更好。

## portability and Windows expectations

Linux 是当前最容易持续验证的平台，因为已有 podman 验证镜像和 `epoll/eventfd`
backend。Windows 支持已经有可重复 smoke 脚本和 IOCP proof backend；后续仍应保持为
独立 backend，而不是通过在 Linux 状态机里堆兼容分支来假装跨平台。

Windows 阶段性预期：

1. 基础 backport 与 `forge::` 纯 header/runtime 设施应能在 Windows + MSVC 或 clang-cl
   上 configure/build/test。
2. 有 IOCP backend 时，`FORGE_ENABLE_FORGE_IO=AUTO` / `ON` 应启用 IOCP tests/examples；
   `FORGE_ENABLE_FORGE_IO=OFF` 应跳过 IO tests/examples。
3. Linux-only IO headers 不应在 IO disabled 或非 Linux build 下破坏普通用户 include。
4. 若准备 Windows 机器，优先建立一个可重复执行的验证脚本，而不是依赖手工点击：
   - CMake configure/build with `FORGE_BUILD_TESTS=ON`;
   - `ctest` 覆盖 backport + `forge::` tests；
   - gate-off/gate-on configure 行为；
   - IOCP backend 单独挂在 `FORGE_ENABLE_FORGE_IO=AUTO/ON` 下。
5. IOCP 与 `epoll` 的 completion 语义不同，应继续作为独立 backend 维护，不应强行套
   Linux fd readiness 状态机。

如果 owner 提供 Windows 主机，建议作为 self-hosted/manual verification 环境先接入；
正式 CI 化之前，至少记录可复现命令和预期 test count。
当前可复现入口见 `scripts/verify-windows-msvc.ps1` 和
`scripts/verify-windows-msvc-ssh.sh`；公开文档和脚本不得写入私有主机名或本地安装路径。

## feature gates

长期建议使用两类开关。

功能开关：

```cmake
FORGE_ENABLE_FORGE_RUNTIME=ON
FORGE_ENABLE_FORGE_RESOURCE_POLICY=ON
FORGE_ENABLE_FORGE_IO=AUTO
FORGE_ENABLE_FORGE_ACCEL=AUTO
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
backend 和 Windows IOCP proof backend；accel gate 已用于 portable mock backend；
erasure 设施是 header-only，不再有独立功能 gate。accel 当前不做 CUDA、HIP、SYCL
或 vendor SDK 探测。

## resource policy

Resource policy 解决实际 runtime 资源问题：

- 队列和 pending operation 的内存来源；
- bounded queue/channel 的容量和 backpressure；
- command/event record 的复用；
- host/device staging buffer 的分配；
- OOM 或 capacity full 时的 completion 策略。

V1 使用 `std::pmr::memory_resource*` 作为稳定接口，而不是发明大型 policy
framework。`static_thread_pool` 已把 queued task callable record 纳入 pool
resource，`timer_context` 已把 state、timer op data、timer item control block 和
timer queue 纳入 resource；仍需如实记录其它未纳入路径，例如
`async_scope` op-state、`strand` runner keepalive node 和部分 `std::function`
target 分配。

## IO backend

IO backend 必须接触真实底层设施，否则只是多包一层线程池。当前已落地 Linux
fd readiness backend 和 Windows IOCP completion proof；后续仍建议分三层推进：

- 通用 API 层：readiness sender、async read/write、close/shutdown；
- 后端层：Linux `epoll`/`eventfd` 与 Windows IOCP 已有 proof，后续可评估 macOS/BSD
  kqueue；Linux `io_uring` 仅在明确需要 kernel submission/completion queue 语义时再做；
- 生命周期层：pending IO 挂到 `async_scope` / `resource_context`，析构时取消、关闭、等待。

第一版不承诺全平台。Linux fd readiness backend 与 Windows IOCP proof 已落地；
kqueue、`io_uring` 和 IOCP production hardening 仍需独立 taskbook。Zig 可以帮助
构建和 C ABI 互操作，但不能消除 epoll/io_uring/IOCP 语义差异。

## accel scheduler

短命名采用 `forge::accel`，避免 `gpu` 过窄，也避免 `device` 与普通 IO 设备混淆。
目标覆盖 GPU、NPU、FPGA、DSP、专用推理卡和 GPGPU。

`accel` 的第一目标不是绑定 CUDA/HIP/SYCL。当前 V1 已用 portable mock backend
落地以下共同结构：

- command queue / stream 的生命周期；
- event/fence 的 sender completion 形状；
- host/device/staging buffer 的资源策略；
- H2D、D2H、D2D copy 的 backpressure 和错误模型；
- kernel-like command 的提交、完成、取消和 drain 语义。

当前 surface：

```cpp
forge::accel::context
forge::accel::device
forge::accel::device_session
forge::accel::queue
forge::accel::device_buffer
forge::accel::event
forge::accel::copy_to_device(...)
forge::accel::copy_to_host(...)
forge::accel::copy_device_to_device(...)
forge::accel::submit(...)
forge::accel::submit_message(...)
forge::accel::record_event(...)
forge::accel::wait_event(...)
forge::accel::fence(...)
```

具体后端若未来需要，可放在：

```cpp
forge::accel::cuda
forge::accel::hip
forge::accel::sycl
```

核心接口不应强依赖 CUDA/HIP/SYCL。mock/in-memory backend 和 examples 已用于验证
语义；只有当抽象需要真实设备语义证明时，才选择一个可选 vendor/platform backend 做
proof。

## typed-error erased sender

`forge::erased_sender` 已支持多个 value shape，并保留目标
`CompletionSignatures` 中声明的 typed error 形状，例如：

- `std::error_code` for IO；
- driver error code for CUDA/HIP；
- device status for FPGA/NPU；
- allocation failure / capacity exceeded；
- resource closed / operation canceled。

当前剩余问题不是 erased sender 的基本 typed-error vtable。IO 已提供
`readable_typed` / `writable_typed` / `async_read_some_typed` /
`async_write_some_typed` 这组 opt-in typed variants；默认 IO surface 仍使用
`std::exception_ptr`。accel 已提供 `copy_to_device_typed` / `copy_to_host_typed` /
`copy_device_to_device_typed` / `submit_typed` / `submit_message_typed` /
`record_event_typed` / `wait_event_typed` / `fence_typed` 这组 opt-in typed variants；
默认 accel surface 仍使用 `std::exception_ptr`。真实 backend 若引入 vendor-specific
错误码，应作为独立 mapping 决策，不应反向污染 portable mock API。

## examples strategy

Examples 必须从“能编译”升级为“能教会人怎么组合”：

- `forge_resource_policy_example.cpp`：固定 arena + bounded pool/channel；
- `forge_bounded_pipeline_example.cpp`：thread pool + strand + channel + scope；
- `forge_io_readiness_example.cpp`：fd readiness sender + resource lifetime；
- `forge_accel_copy_example.cpp`：host/device copy + CPU continuation；
- `forge_accel_pipeline_example.cpp`：H2D -> kernel -> D2H -> CPU postprocess；
- `forge_accel_message_device_example.cpp`：device session + message command；
- `forge_inference_runtime_sketch.cpp`：请求 channel、strand 顺序控制、accel queue、
  scope 生命周期和 resource shutdown。

这些 examples 应避免营销式代码，重点展示“资源在哪里、取消如何传播、何时 drain、
错误如何处理、谁拥有谁”。

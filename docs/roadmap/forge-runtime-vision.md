# Forge runtime 远景图

本文记录 `include/forge/` 扩展设施的中长期方向。它不是标准 backport 计划，也不向
`namespace std` 增加名字。目标是让 Forge 在 `std::execution` backport 之上，提供一组
小而实用的运行时原语，服务结构化并发、消息通路、IO/protocol flow 和资源生命周期管理。
当前稳定交付基线和自循环验收规则见
[`Forge 稳定性基线`](forge-stability-baseline.md)。

## 当前基线

已交付的 `forge::` 设施包括：

- `static_thread_pool` / `single_thread_context` / `system_context`
- `timer_context` / `runtime_context`
- `async_scope`
- `bounded_channel`
- `resource_context`
- `strand`
- `io::context` (Linux epoll/eventfd readiness + Windows IOCP proof)
- coroutine-native byte IO helpers (`memory_read_stream`, `memory_write_stream`,
  `read_exactly`, `write_all`, `read_until`, borrowed and PMR-owned stream erasure,
  direct-awaitable async stream concepts, `io_task`, `await_sender`, `as_sender`,
  `when_all_results`, `when_any_results`, `with_timeout`,
  `<forge/io/timer_await.hpp>` timer facade, `<forge/io/context_await.hpp>` backend bridge)
- `resource_policy` and resource-backed pool callable storage
- `task`
- `any_scheduler`
- 窄 `any_sender_of` / `any_receiver_of`
- connectable `erased_sender` with closed-set typed errors
- opt-in `forge::io` typed-error sender variants

这些设施的生命周期词汇由 `docs/forge-runtime.md` 固定：
`close()` 是 graceful ingress close，`request_stop()` 是协作取消，`shutdown()` 是
close + stop，`wait()` 是阻塞 drain。拥有型 context 的析构允许 `shutdown()` +
`wait()`，因此可能阻塞。

## 设计原则

- Backport 与 extension 分层清楚：标准设施在 `backport/`，Forge 扩展在
  `include/forge/`。
- 不克隆 NVIDIA stdexec/exec 的完整扩展栈；只吸收明确有用、能测试、能维护的小原语。
- 默认体验应可直接用；涉及平台或重依赖的功能使用 `AUTO` / `ON` / `OFF` feature gate。
- 结构化并发优先：资源生命周期、取消、drain、错误传播必须比裸线程/回调更清楚。
- Examples 是一等交付物。每个新设施都要有简单示例和至少一个组合示例，展示现代 C++
  如何表达生命周期、并发、取消和资源边界。

## 推荐推进顺序

1. Resource policy / allocator policy
2. Runtime primitives and type erasure
3. IO backend and coroutine-native byte IO
4. Typed-error integration for IO/protocol boundaries

顺序理由：

- Resource policy 是 runtime 和 IO 的基础。队列节点、pending operation、timer item 和
  callback record 都需要明确内存来源和容量策略。
- IO 会引入平台后端。先统一资源策略，可以避免后端各自发明 allocation 和 backpressure
  规则。
- Typed-error integration 最抽象。`erased_sender` 已能保留声明内的 typed error；IO 已有
  opt-in typed variants。后续问题是具体 platform backend 是否需要自己的错误映射，而不是
  默认扩大现有 API。

## 项目身份检查点

Forge 的目标不是变成完整 runtime framework、网络库、tensor runtime 或 vendor driver
wrapper。更准确的身份是：

> C++ backport + 面向资源型异步系统的组合式支撑层。

Resource policy、IO readiness、coroutine-native byte IO 和 typed-error integration 都应服务
这个支撑层：抽出生命周期、调度、消息、资源、错误和组合方式这些共性，而不是绑定某个具体
平台或厂商栈。

“不做网络库”指不做 TCP/DNS/UDP/TLS、socket option、endpoint resolution 这类
networking framework surface；它不排除 byte-stream transport substrate 本身。
2026-08 确认：非以太网介质的字节流场景（io_uring SQ/CQ、RoCEv2/RDMA、DMA、
PCIe/UALink/UCIe 类加速器互连）属于本支撑层要服务的 workload。这类 backend 仍受
backend proof 政策与独立 taskbook gate 约束。

未来 backend shape 记录在 [`forge::io` backend SPI 草案](forge-io-backend-spi.md)。
Gate、lifetime、verification 和 typed-error 规则记录在
[`backend proof` 策略](forge-backend-proof-policy.md)。这些是 design constraints，不是
已发布的 plugin ABI。

具体要求：

- platform backend 只是验证支撑层是否能表达真实系统的 optional proof；
- 任何 platform backend 都必须有清楚的 optional gate、examples 和验证边界；
- 不以 "full stdexec parity" 或 "general-purpose networking framework" 为目标；
- 不把 IOCP、io_uring、TLS、DNS 或完整 socket framework 做成默认依赖。

## 维护态与决策 gate

当前 `include/forge/` 设施已经进入较稳定的维护态。默认下一步应优先做：

- bug fixes 和 sanitizer-found lifetime fixes；
- docs/examples/cookbook，让现有设施更容易被正确组合；
- verification coverage，尤其是 Windows/MSVC smoke、gate-off/gate-on 行为和 sanitizer
  子集；
- 小而明确的 ergonomic helpers，前提是能复用现有 runtime/lifetime 模型。

Reference runtime helpers 仍延后。当前 examples 已证明组合模式，但尚未重复出足够小、足够
稳定的 public shape 来冻结成 helper。若未来确实需要 helper，优先考虑 `service_scope` 或
`owned_service` 这种只表达 lifecycle 的 utility，暴露
scheduler/spawn/close/request_stop/shutdown/wait；避免把 IO、protocol、tensor 或 serving
policy 内置进通用 helper。

以下事项仍在远景内，但不应在没有单独拍板和新任务书时顺手启动：

- 新平台 IO backend：Linux `io_uring` proof 已于 2026-08 按独立 taskbook 落地（见
  `forge-io-backend-spi.md`）；超出 proof 的 io_uring hardening（SQPOLL、registered
  buffers、multishot），或 Windows IOCP 超出当前 proof 的 production hardening，例如
  explicit owned-handle lifetimes 或 high-churn handle-pool policy；
- 完整 networking 方向：TCP/DNS/UDP/TLS、socket option、endpoint/address resolution、
  certificate/security policy；
- 外部生态 adapter：Boost.Asio、OpenSSL、WolfSSL 或其它库的 adapter matrix；
- 标准 backport 的未来 conformance 复查。`std::execution` stop-token type-erasure
  control block 已接受 allocator-neutral 取舍；不要把它当成开放 bug 继续打磨。

每次启动这些大项前，先写一份总计划和若干子任务书，明确 gate、examples、测试矩阵和回滚
边界。没有明确收益或验证条件时，维持现状比扩大 surface 更好。Backend proof work 也必须满足
[`backend proof` 策略](forge-backend-proof-policy.md)。

## 可移植性与 Windows 预期

Linux 是当前最容易持续验证的平台，因为已有 podman 验证镜像和 `epoll/eventfd`
backend。Windows 支持已经有可重复 smoke 脚本和 IOCP proof backend；后续仍应保持为独立
backend，而不是通过在 Linux 状态机里堆兼容分支来假装跨平台。

Windows 阶段性预期：

1. 基础 backport 与 `forge::` 纯 header/runtime 设施应能在 Windows + MSVC 或 clang-cl
   上 configure/build/test。
2. 有 IOCP backend 时，`FORGE_ENABLE_FORGE_IO=AUTO` / `ON` 应启用
   backend-specific IOCP tests/examples；`FORGE_ENABLE_FORGE_IO=OFF` 应跳过
   backend-specific IO tests/examples，但不关闭 backend-free byte IO/coroutine tests。
3. Linux-only IO headers 不应在 IO disabled 或非 Linux build 下破坏普通用户 include。
4. 若准备 Windows 机器，优先建立一个可重复执行的验证脚本，而不是依赖手工点击：
   - 使用 `FORGE_BUILD_TESTS=ON` 进行 CMake configure/build；
   - `ctest` 覆盖 backport + `forge::` tests；
   - gate-off/gate-on configure 行为；
   - IOCP backend 单独挂在 `FORGE_ENABLE_FORGE_IO=AUTO/ON` 下。
5. IOCP 与 `epoll` 的 completion 语义不同，应继续作为独立 backend 维护，不应强行套
   Linux fd readiness 状态机。

如果 owner 提供 Windows 主机，建议作为 self-hosted/manual verification 环境先接入；不依赖
GitHub hosted CI。当前可复现入口是 `scripts/verify-windows-msvc.ps1`：它应在 Windows
主机上直接运行，并通过参数或环境变量接收 MSVC Build Tools 位置等本机信息。
`scripts/verify-windows-msvc-ssh.sh` 和 `scripts/verify-windows-msvc-matrix.sh` 只是从
Linux/macOS 调用远端 Windows 主机的 transport wrapper。公开文档和脚本不得写入私有主机名
或本地安装路径。整体 local/self-hosted verification floor 入口见
`scripts/verify-selfhosted-floor.sh`。

## Feature gates（功能 gate）

长期建议使用两类开关。

功能开关：

```cmake
FORGE_ENABLE_FORGE_RUNTIME=ON
FORGE_ENABLE_FORGE_RESOURCE_POLICY=ON
FORGE_ENABLE_FORGE_IO=AUTO
```

测试开关：

```cmake
FORGE_TEST_ENABLE_FORGE_RUNTIME=ON
FORGE_TEST_ENABLE_FORGE_RESOURCE=ON
FORGE_TEST_ENABLE_FORGE_IO=ON
FORGE_TEST_ENABLE_FORGE_ERASURE=ON
```

`AUTO` 表示依赖可用时启用，不可用时跳过；显式 `ON` 缺依赖应报错。纯 header 设施不应因为
全局 gate 变成不可 include；gate 主要控制 umbrella header、examples、tests 和带外部依赖的
backend。IO gate 已用于 Linux `epoll`/`eventfd` readiness backend 和 Windows IOCP proof
backend；erasure 设施是 header-only，不再有独立功能 gate。

## Resource policy（资源策略）

Resource policy 解决实际 runtime 资源问题：

- 队列和 pending operation 的内存来源；
- bounded queue/channel 的容量和 backpressure；
- callback/timer record 的复用；
- OOM 或 capacity full 时的 completion 策略。

V1 使用 `std::pmr::memory_resource*` 作为稳定接口，而不是发明大型 policy framework。
`static_thread_pool` 已把 queued task callable record 纳入 pool resource，`timer_context`
已把 state、timer op data、timer item control block、timer queue 和 timer callback callable
record 纳入 resource；`async_scope` op-state 和 `strand` runner keepalive node 也已纳入
resource。Owning stream wrappers 也用注入 resource 持有 concrete stream object；async
wrapper 的固定 operation slot 不做 erasure-layer per-operation allocation。Concrete
stream/awaitable、coroutine frame、OS thread 或 kernel object 等路径不在该声明内。

## IO backend（IO 后端）

IO backend 必须接触真实底层设施，否则只是多包一层线程池。当前已落地 Linux fd readiness
backend 和 Windows IOCP completion proof；后续仍建议分三层推进：

- 通用 API 层：readiness sender、async read/write、close/shutdown；
- 后端层：Linux `epoll`/`eventfd` 与 Windows IOCP 已有 proof；Linux `io_uring`
  已作为 coroutine-native completion proof 落地（独立 gate，不参与 portable
  context 选择）；
- 生命周期层：pending IO 挂到 `async_scope` / `resource_context`，析构时取消、关闭、等待。

第一版不承诺全平台。Linux fd readiness backend 与 Windows IOCP proof 已落地；macOS/BSD
kqueue 当前不在项目需求内。`io_uring` 的 defer 重估条件已于 2026-08 触发（byte-stream
fabric 方向确认），随后按独立 taskbook 落地为 coroutine-native completion proof（见
`forge-io-backend-spi.md`）。RoCEv2/RDMA
类 fabric backend 需要独立 taskbook 和可验证硬件（或 soft-RoCE）故事，当前仍 deferred。
IOCP 当前 proof 已覆盖 completion drain、per-operation cancellation 和 conservative
associated-handle pruning；更强的 owned-handle lifetime 或 high-churn handle-pool policy
仍需独立 taskbook。

## Coroutine-native byte IO

Coroutine-native byte IO 是当前 IO 方向的下一层 ergonomics，而不是标准库 `<io>` backport。
它应继续满足：

- API 放在 `forge::io`，不进入 `namespace std`；
- 每个 primitive/backend 只选择一个原生 async 协议：sender 或 coroutine awaitable，
  另一侧经显式 bridge 到达，不做平行双实现。现有 readiness/IOCP backend 保持
  sender 原生 + coroutine facade；io_uring completion backend 已按此原则落地为
  coroutine-native + sender bridge。生命周期词汇（`close()` / `request_stop()` /
  `shutdown()` / `wait()`）与验证矩阵两侧共享；
- borrowed stream erasure 保持最小 non-owning boundary；PMR-owned sync 与
  direct-awaitable async wrappers 已作为 header-only Forge extension 落地，但跨版本
  ABI-stable/plugin erasure 仍需独立设计；
- `io_task<T>` 与 sender bridge 必须清楚说明 single-use、stopped 和 frame lifetime；
- `<forge/io/context_await.hpp>` 中的 `async_read_some` / `async_write_some`
  context overload（Linux 另有 `readable` / `writable`）是现有
  `forge::io::context` sender 的 coroutine facade，不改变底层 fd / `HANDLE` /
  buffer 的 borrowed lifetime；
- Windows IOCP named-pipe coroutine smoke 应作为未来 Windows gate 补上。

不要提前承诺 `std::io`、`std::networking`、`<io>` 或 `<networking>`。若 WG21 后续 adopted
wording，另开 taskbook 评估是否做 standard-shaped backport。

## Typed-error erased sender（类型化错误擦除 sender）

`forge::erased_sender` 已支持多个 value shape，并保留目标 `CompletionSignatures` 中声明的
typed error 形状，例如：

- `std::error_code` for IO；
- allocation failure / capacity exceeded；
- resource closed / operation canceled。

当前剩余问题不是 erased sender 的基本 typed-error vtable。IO 已提供 `readable_typed` /
`writable_typed` / `async_read_some_typed` / `async_write_some_typed` 这组 opt-in typed
variants；默认 IO surface 仍使用 `std::exception_ptr`。`forge::wait_result(sender)` 可在同步
边界保留 value / typed error / stopped，避免示例和插件边界重复手写 receiver。

## Examples 策略

Examples 必须从“能编译”升级为“能教会人怎么组合”：

- `forge_resource_policy_example.cpp`：`forge::resource_policy`、固定 arena 和 bounded
  pool/channel；
- `forge_bounded_pipeline_example.cpp`：thread pool + strand + channel + scope；
- `forge_io_readiness_example.cpp`：fd readiness sender + resource lifetime；
- `forge_io_read_write_example.cpp`：borrowed span async read/write；
- `forge_io_typed_error_example.cpp`：typed IO error 穿过 erased sender；
- `forge_memory_stream_example.cpp`：backend-free stream protocol；
- `forge_owned_async_stream_example.cpp`：owning async stream 的 separate-TU protocol
  boundary；
- `forge_coro_line_pipeline_example.cpp`：coroutine protocol + strand state update。

这些 examples 应避免营销式代码，重点展示“资源在哪里、取消如何传播、何时 drain、
错误如何处理、谁拥有谁”。

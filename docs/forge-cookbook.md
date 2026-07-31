# Forge cookbook（实践手册）

这份文档把 `include/forge/` 扩展设施按工程用法串起来。它不是 API
reference；reference 见 [`forge::` 扩展工具](forge-utilities.md)、[runtime
lifecycle contract](forge-runtime.md)、[`forge::io`](forge-io.md) 和
[`forge::erased_sender` 设计与限制](forge-erased-sender-design.md)。

核心心智模型：

- 用 scheduler 表达“在哪里运行”；
- 用 sender 表达“做什么、如何完成”；
- 用 scope/context 表达“谁拥有正在运行的工作”；
- 用 channel 表达“消息与 backpressure”；
- 用 strand 表达“共享状态串行化”；
- 用 resource policy 表达“队列、pending record 和 callback record 从哪里分配”。

## 先读哪几个例子

建议按这个顺序读：

1. `example/forge_thread_pool_example.cpp`：最小 scheduler + `schedule`。
2. `example/forge_runtime_context_example.cpp`：显式拥有 runtime，并用 `wait()` drain。
3. `example/forge_channel_example.cpp`：异步 send/recv 与 close/stop 语义。
4. `example/forge_graceful_shutdown_example.cpp`：服务入口 close/drain 与 stop/cancel。
5. `example/forge_bounded_pipeline_example.cpp`：arena、bounded channel、scope、strand
   组合成一个受控 pipeline。
6. `example/execution_on_example.cpp`：用 `on` 显式切换起始 scheduler，或在 closure
   内临时切换再切回原 completion scheduler。
7. `example/execution_spawn_future_example.cpp`：对比 scope token 下的 fire-and-forget
   `spawn` 与可等待的 `spawn_future`。
8. `example/execution_affine_example.cpp`：用 `affine` 把完成切回消费方 receiver
   environment 的 start scheduler。
9. `example/execution_unstoppable_example.cpp`：把内层 sender 从外部 stop-token 中隔离。
10. `example/forge_io_read_write_example.cpp`：borrowed buffer async read/write。
11. `example/forge_io_typed_error_example.cpp`：typed IO error 跨 erased sender 边界，
    并用 `forge::wait_result` 消费。
12. `example/forge_io_iocp_example.cpp`：Windows IOCP completion proof。
13. `example/forge_io_pipeline_example.cpp`：Linux fd readiness 与 CPU runtime handoff。
14. `example/forge_memory_stream_example.cpp`：backend-free scripted stream。
15. `example/forge_coro_io_example.cpp`：`io_env` propagation proof。
16. `example/forge_coro_interop_example.cpp`：`io_task` 与 sender / erased sender 互通。
17. `example/forge_context_await_example.cpp`：现有 `forge::io::context` sender 的
    coroutine facade。
18. `example/forge_coro_line_pipeline_example.cpp`：memory stream read -> coroutine parse
    -> strand state update -> response write。

这些例子优先展示“资源在哪里、取消如何传播、何时 drain、谁拥有谁”，不是为了把 API
调用堆到最多。

## 学习路径

CPU service 或 message pipeline 建议按这个顺序读：

1. `example/forge_thread_pool_example.cpp`
2. `example/execution_on_example.cpp`
3. `example/execution_spawn_future_example.cpp`
4. `example/execution_affine_example.cpp`
5. `example/execution_unstoppable_example.cpp`
6. `example/forge_channel_example.cpp`
7. `example/forge_graceful_shutdown_example.cpp`
8. `example/forge_bounded_pipeline_example.cpp`

OS IO handoff 建议按这个顺序读：

1. `example/forge_io_readiness_example.cpp`
2. `example/forge_io_read_write_example.cpp`
3. `example/forge_io_pipeline_example.cpp`
4. `example/forge_io_typed_error_example.cpp`
5. `example/forge_io_iocp_example.cpp`：验证 Windows completion backend。

Coroutine-native byte IO / protocol code 建议按这个顺序读：

1. `example/forge_memory_stream_example.cpp`：scripted reads 上的 length-prefixed parser。
2. `example/forge_stream_erasure_example.cpp`：`any_read_stream` / `any_write_stream`
   上的 protocol boundary。
3. `example/forge_line_protocol_example.cpp`：`read_until` + memory streams 上的 line
   request/response。
4. `example/forge_coro_io_example.cpp`：`io_env` propagation proof。
5. `example/forge_coro_interop_example.cpp`：`io_task` 与 sender/erased sender 互通。
6. `example/forge_context_await_example.cpp`：现有 `forge::io::context` sender 的
   coroutine facade。
7. `example/forge_coro_line_pipeline_example.cpp`：memory stream read -> coroutine parse
   -> strand state update -> response write。

这些路径刻意保持 example-first。详细契约放在各 feature docs 中，因此 cookbook 只是地图，
不是重复的 API reference。

## 覆盖地图

| 场景 | 示例 |
| --- | --- |
| execution scope work | `example/execution_phase4_example.cpp`, `example/execution_spawn_future_example.cpp` |
| execution scheduler adaptors | `example/execution_on_example.cpp`, `example/execution_affine_example.cpp`, `example/execution_unstoppable_example.cpp` |
| CPU scheduler basics | `example/forge_thread_pool_example.cpp`, `example/forge_runtime_context_example.cpp` |
| timer / single-thread / system context | `example/forge_timer_context_example.cpp`, `example/forge_single_thread_context_example.cpp`, `example/forge_system_context_example.cpp` |
| channel backpressure | `example/forge_channel_example.cpp`, `example/forge_bounded_pipeline_example.cpp` |
| graceful close vs stop | `example/forge_graceful_shutdown_example.cpp` |
| PMR / bounded allocation | `example/forge_resource_policy_example.cpp`, `example/forge_bounded_pipeline_example.cpp` |
| C++26 constant / padded layout foundations | `example/constant_wrapper_example.cpp`, `example/padded_mdspan_layout_example.cpp` |
| serialized session state | `example/forge_strand_example.cpp`, `example/forge_bounded_pipeline_example.cpp` |
| type-erased boundary | `example/forge_type_erased_boundary_example.cpp`, `example/forge_io_typed_error_example.cpp` |
| backend-free protocol streams | `example/forge_memory_stream_example.cpp`, `example/forge_stream_erasure_example.cpp`, `example/forge_line_protocol_example.cpp` |
| coroutine-native byte IO proofs | `example/forge_coro_io_example.cpp`, `example/forge_coro_interop_example.cpp`, `example/forge_context_await_example.cpp`, `example/forge_coro_line_pipeline_example.cpp` |
| Linux IO readiness/read-write | `example/forge_io_readiness_example.cpp`, `example/forge_io_read_write_example.cpp`, `example/forge_io_pipeline_example.cpp` |
| Windows IOCP proof | `example/forge_io_iocp_example.cpp` |

`example/CMakeLists.txt` 会把已构建的示例注册成 `example_<target>_smoke`，所以这些
路径不是只编译不运行的文档片段；受 IO 或 mdspan gate 控制的示例只在对应 target
存在时注册 smoke test。

## Recipe：CPU work queue（CPU 工作队列）

适用场景：把一批 CPU work 放到固定线程池里执行，最后等待所有已接受 work 完成。

使用：

- `forge::static_thread_pool` 或 `forge::runtime_context`
- `std::execution::schedule(scheduler)`
- `std::this_thread::sync_wait(...)` 用于示例或边界同步

关键点：

- `shutdown()` 拒绝未来 work，但已接受 work 会 drain；
- `wait()` 等队列和正在运行的任务清空；
- 有界队列满时，schedule operation 以 stopped 完成，而不是无限增长。

参考：

- `example/forge_thread_pool_example.cpp`
- `example/forge_runtime_context_example.cpp`
- `example/forge_graceful_shutdown_example.cpp`
- `example/forge_resource_context_example.cpp`

## Recipe：bounded producer/consumer（有界生产消费）

适用场景：消息系统、协议请求队列、跨线程 backpressure。

使用：

- `forge::bounded_channel<T>`
- `async_send(value)` / `async_recv()`
- `close()` 表示 producer 不再发送，consumer drain 剩余值后收到 stopped；
- `request_stop()` 表示取消 pending operation 并丢弃 buffer。

关键点：

- channel 的 completion 不在内部 mutex 下调用，receiver 可以安全 re-enter；
- pending send/recv 支持 receiver stop-token 取消；
- 对长期运行的 consumer，推荐由 `async_scope` 或 `resource_context` 拥有。

参考：

- `example/forge_channel_example.cpp`
- `example/forge_graceful_shutdown_example.cpp`
- `example/forge_bounded_pipeline_example.cpp`

## Recipe：serialize shared state（串行化共享状态）

适用场景：session 状态、protocol state machine、统计结果、非线程安全资源。

使用：

- `forge::strand`
- `std::execution::schedule(strand.get_scheduler())`
- `std::execution::continues_on(sender, strand.get_scheduler())`

关键点：

- strand 保证 FIFO 且同一时间最多一个 task 处于用户 completion；
- completion 在锁外执行，允许在 completion 内继续向同一 strand 提交后续 work；
- `shutdown()` 会把 pending/future work 以 stopped 完成。

参考：

- `example/forge_strand_example.cpp`
- `example/forge_bounded_pipeline_example.cpp`
- `example/forge_io_pipeline_example.cpp`

## Recipe：own a session（拥有会话生命周期）

适用场景：一个连接、一个协议会话、一个资源型服务实例。

使用：

- `forge::resource_context`
- `spawn(sender)` 让 context 拥有 eager-start work；
- `close()` 停止接受新 work；
- `request_stop()` 协作取消；
- `wait()` drain runtime 和 scope。

关键点：

- 析构会 `shutdown()` + `wait()`，所以可能阻塞；
- 如果要清楚控制停机时序，显式调用 `shutdown()` / `wait()`；
- session 内部 worker 循环通常从 channel 读请求，channel close 后自然退出。

参考：

- `example/forge_resource_context_example.cpp`
- `example/forge_graceful_shutdown_example.cpp`
- `example/forge_bounded_pipeline_example.cpp`

## Recipe：bounded allocations（有界分配）

适用场景：嵌入式、实时-ish pipeline、服务端热路径中的稳定分配边界。

使用：

- `std::pmr::synchronized_pool_resource`，或其他适合跨线程访问的自定义
  `std::pmr::memory_resource`
- `static_thread_pool_options{.memory = resource}`
- `bounded_channel_options{.memory = resource}`
- `strand_options{.memory = resource}`
- `timer_context_options{.memory = resource}` 或 `runtime_context_options{.memory = resource}`

关键点：

- pool 的 queue node 和 queued task callable record 受 resource 控制；
- channel、strand、timer callback/queue records 的受控路径使用传入 resource；
- 如果同一个 resource 会被多个 runtime primitive 或 worker 线程共享，不要直接使用裸
  `std::pmr::monotonic_buffer_resource`；可把固定 buffer 放在 monotonic upstream
  后面，再由 `std::pmr::synchronized_pool_resource` 作为对外 resource；
- `async_scope` op-state、strand runner keepalive node 和 timer callback callable record
  已受 resource 控制。

参考：

- `example/forge_resource_policy_example.cpp`
- `example/forge_bounded_pipeline_example.cpp`

## Recipe：IO into protocol work（IO 转协议工作）

适用场景：Linux fd readiness 或 Windows IOCP completion 边界，随后切回 CPU runtime
处理协议状态。

使用：

- `forge::io::context`
- Linux: `readable(fd)` / `writable(fd)`
- Linux/Windows: `async_read_some(...)` / `async_write_some(...)`
- `std::execution::continues_on(..., strand.get_scheduler())`

关键点：

- fd / `HANDLE` 和 buffer 是 borrowed，调用方负责生命周期；
- `readable` / `writable` 是 readiness；`async_read_some` / `async_write_some`
  是 borrowed-span convenience，不拥有 buffer；
- Linux pending readiness 支持 close/stop wakeup 和 receiver stop-token 取消；
- Windows IOCP V1 是 completion backend，不提供 `readable` / `writable` readiness
  sender；入队后取消通过 `cancel` / `request_stop` / `shutdown` 触发 `CancelIoEx`。

参考：

- `example/forge_io_readiness_example.cpp`
- `example/forge_io_read_write_example.cpp`
- `example/forge_io_typed_error_example.cpp`
- `example/forge_io_iocp_example.cpp`
- `example/forge_io_pipeline_example.cpp`

## Recipe：coroutine-native protocol work（协程协议工作）

适用场景：希望把 byte stream protocol 写成 `co_await` 流程，同时仍能和 sender/runtime
设施互通。

使用：

- `forge::io::memory_read_stream` / `memory_write_stream` 做 backend-free 测试；
- `forge::io::read_exactly` / `write_all` / `read_until` 做小型 stream algorithm；
- `forge::io::io_task<T>` 表达 coroutine-native byte IO flow；
- `forge::io::await_sender` / `as_sender` 桥接 sender 与 coroutine；
- `<forge/io/context_await.hpp>` 提供
  `forge::io::async_read_some(context, handle, span)` /
  `async_write_some(...)` coroutine facade；Linux 另有
  `readable(context, fd)` / `writable(context, fd)`。

关键点：

- stream erasure 是 borrowed wrapper，不拥有底层 stream；
- `as_sender(io_task<T>)` 是 single-use bridge；
- IO context 的 fd / `HANDLE` / buffer 仍由调用方拥有；
- Windows IOCP named-pipe coroutine smoke 是后续验证 gate，不是当前默认 Linux lane 的一部分。

参考：

- `example/forge_memory_stream_example.cpp`
- `example/forge_stream_erasure_example.cpp`
- `example/forge_line_protocol_example.cpp`
- `example/forge_coro_io_example.cpp`
- `example/forge_coro_interop_example.cpp`
- `example/forge_context_await_example.cpp`
- `example/forge_coro_line_pipeline_example.cpp`

## Recipe：type erase at boundaries（边界类型擦除）

适用场景：插件边界、队列中存放异构 sender、将具体 scheduler 隐藏在运行时配置后面。

使用：

- `forge::any_scheduler` 擦除 `schedule()` 形状；
- `forge::erased_sender<CompletionSignatures>` 擦除 connectable sender；
- `forge::any_sender_of` / `forge::any_receiver_of` 只用于窄便利场景。

关键点：

- `erased_sender` 支持多个 value shape，并保留声明内的 typed error 形状；
- error 类型必须在目标 `CompletionSignatures` 中显式声明，未声明 typed error 会被编译期拒绝；
- `forge::wait_result(sender)` 可同步消费 value / typed error / stopped 三种结果；
- 如果只需要保存 scheduler，优先 `any_scheduler`，不要在 scheduler 边界上强行使用
  `erased_sender`。

参考：

- `example/forge_any_scheduler_example.cpp`
- `example/forge_type_erased_boundary_example.cpp`
- `example/forge_io_typed_error_example.cpp`
- `example/forge_any_sender_example.cpp`
- `example/forge_any_receiver_example.cpp`

## Shutdown checklist（关闭检查表）

写一个拥有型 runtime 或 session 时，按这个顺序检查：

1. 新入口是否有 `close()` 或 `shutdown()` 后拒绝策略。
2. Pending operation 是否会被 close/stop 唤醒。
3. Receiver completion 是否在内部锁外调用。
4. Receiver stop-token 的 callback 生命周期是否短于 receiver completion 生命周期。
5. `wait()` 是否只承诺有限 drain，还是需要无界 loop-until-idle。
6. 析构是否可能阻塞；如果会，文档是否写清楚。
7. Examples 是否展示了正常完成和停机路径。

这也是新增 `forge::` 原语时的审查清单。

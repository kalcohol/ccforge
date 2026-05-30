# Forge cookbook

这份文档把 `include/forge/` 扩展设施按工程用法串起来。它不是 API
reference；reference 见 [`forge::` 扩展工具](forge-utilities.md)、[runtime
lifecycle contract](forge-runtime.md)、[`forge::accel` mock command backend](forge-accel.md)
和 [`forge::erased_sender` 设计与限制](forge-erased-sender-design.md)。

核心心智模型：

- 用 scheduler 表达“在哪里运行”；
- 用 sender 表达“做什么、如何完成”；
- 用 scope/context 表达“谁拥有正在运行的工作”；
- 用 channel 表达“消息与 backpressure”；
- 用 strand 表达“共享状态串行化”；
- 用 resource policy 表达“队列、pending record 和命令记录从哪里分配”。

## 先读哪几个例子

建议按这个顺序读：

1. `example/forge_thread_pool_example.cpp`：最小 scheduler + `schedule`。
2. `example/forge_runtime_context_example.cpp`：显式拥有 runtime，并用 `wait()` drain。
3. `example/forge_channel_example.cpp`：异步 send/recv 与 close/stop 语义。
4. `example/forge_graceful_shutdown_example.cpp`：服务入口 close/drain 与 stop/cancel。
5. `example/forge_bounded_pipeline_example.cpp`：arena、bounded channel、scope、strand
   组合成一个受控 pipeline。
6. `example/forge_io_read_write_example.cpp`：Linux borrowed buffer async read/write。
7. `example/forge_io_pipeline_example.cpp`：Linux fd readiness 与 CPU runtime handoff。
8. `example/forge_accel_staging_buffer_example.cpp`：owning host staging buffer 与 mock
   device buffer。
9. `example/forge_accel_pipeline_example.cpp`：mock device buffer、copy、submit 和 CPU
   continuation。
10. `example/forge_inference_runtime_sketch.cpp`：把请求通道、runtime、strand、accel queue
   和资源生命周期放在同一个推理 runtime sketch 里。

这些例子优先展示“资源在哪里、取消如何传播、何时 drain、谁拥有谁”，不是为了把 API
调用堆到最多。

## Recipe: CPU work queue

适用场景：把一批 CPU work 放到固定线程池里执行，最后等待所有已接受 work 完成。

使用：

- `forge::static_thread_pool` 或 `forge::runtime_context`
- `std::execution::schedule(scheduler)`
- `std::execution::sync_wait(...)` 用于示例或边界同步

关键点：

- `shutdown()` 拒绝未来 work，但已接受 work 会 drain；
- `wait()` 等队列和正在运行的任务清空；
- 有界队列满时，schedule operation 以 stopped 完成，而不是无限增长。

参考：

- `example/forge_thread_pool_example.cpp`
- `example/forge_runtime_context_example.cpp`
- `example/forge_graceful_shutdown_example.cpp`
- `example/forge_resource_context_example.cpp`

## Recipe: bounded producer/consumer

适用场景：消息系统、推理请求队列、设备 command staging、跨线程 backpressure。

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

## Recipe: serialize shared state

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

## Recipe: own a session

适用场景：一个连接、一个设备会话、一个推理 worker、一个资源型服务实例。

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
- `example/forge_inference_runtime_sketch.cpp`

## Recipe: bounded allocations

适用场景：嵌入式、实时-ish pipeline、服务端热路径、推理 runtime 中的稳定分配边界。

使用：

- `std::pmr::monotonic_buffer_resource` 或自定义 `std::pmr::memory_resource`
- `static_thread_pool_options{.memory = resource}`
- `bounded_channel_options{.memory = resource}`
- `strand_options{.memory = resource}`
- `timer_context_options{.memory = resource}` 或 `runtime_context_options{.memory = resource}`
- `forge::accel::context_options{.memory = resource}`

关键点：

- pool 的 queue node 和 queued task callable record 受 resource 控制；
- channel、strand、timer、accel command/pending records 的受控路径使用传入 resource；
- `async_scope` op-state、strand runner keepalive node 和部分 `std::function` target 分配仍是已知未完全受控路径。

参考：

- `example/forge_resource_policy_example.cpp`
- `example/forge_bounded_pipeline_example.cpp`
- `example/forge_inference_runtime_sketch.cpp`

## Recipe: fd readiness into protocol work

适用场景：Linux fd readiness、pipe/socket/eventfd 边界，随后切回 CPU runtime 处理协议状态。

使用：

- `forge::io::context`
- `readable(fd)` / `writable(fd)`
- `std::execution::continues_on(..., strand.get_scheduler())`

关键点：

- fd 是 borrowed，调用方负责 fd 生命周期；
- `readable` / `writable` 是 readiness；`async_read_some` / `async_write_some`
  是 borrowed-span convenience，不拥有 buffer；
- pending IO 支持 close/stop wakeup 和 receiver stop-token 取消；
- 非 Linux 平台下 IO backend gate 会跳过，Windows IOCP 需要独立 backend。

参考：

- `example/forge_io_readiness_example.cpp`
- `example/forge_io_read_write_example.cpp`
- `example/forge_io_pipeline_example.cpp`

## Recipe: accelerator-shaped pipeline

适用场景：先用 portable mock backend 验证 command queue / buffer / copy / submit
形状，再决定是否需要真实 accelerator backend。

使用：

- `forge::accel::context`
- `forge::accel::host_buffer<T>`
- `forge::accel::device_buffer<T>`
- `copy_to_device` / `copy_to_host` / `copy_device_to_device`
- `submit(queue, callable)`
- `event` / `record_event` / `wait_event` / `fence`

关键点：

- 当前 backend 是 in-memory mock，不依赖 CUDA/HIP/SYCL；
- queue 命令按 FIFO 运行；
- host spans 是 borrowed，`device_buffer` 拥有 mock device storage；
- `host_buffer` 可表达由 Forge resource 分配的 owning host staging storage，但不是
  vendor pinned memory；
- event 是 one-shot completion marker，不建模跨 queue dependency graph。

参考：

- `example/forge_accel_copy_example.cpp`
- `example/forge_accel_event_example.cpp`
- `example/forge_accel_staging_buffer_example.cpp`
- `example/forge_accel_pipeline_example.cpp`
- `example/forge_inference_runtime_sketch.cpp`

## Recipe: type erase at boundaries

适用场景：插件边界、队列中存放异构 sender、将具体 scheduler 隐藏在运行时配置后面。

使用：

- `forge::any_scheduler` 擦除 `schedule()` 形状；
- `forge::erased_sender<CompletionSignatures>` 擦除 connectable sender；
- `forge::any_sender_of` / `forge::any_receiver_of` 只用于窄便利场景。

关键点：

- `erased_sender` v1 支持多个 value shape，但 error 收敛到 `std::exception_ptr`；
- typed error erasure 是未来独立大项；
- 如果只需要保存 scheduler，优先 `any_scheduler`，不要在 scheduler 边界上强行使用
  `erased_sender`。

参考：

- `example/forge_any_scheduler_example.cpp`
- `example/forge_type_erased_boundary_example.cpp`
- `example/forge_any_sender_example.cpp`
- `example/forge_any_receiver_example.cpp`

## Shutdown checklist

写一个拥有型 runtime 或 session 时，按这个顺序检查：

1. 新入口是否有 `close()` 或 `shutdown()` 后拒绝策略。
2. Pending operation 是否会被 close/stop 唤醒。
3. Receiver completion 是否在内部锁外调用。
4. Receiver stop-token 的 callback 生命周期是否短于 receiver completion 生命周期。
5. `wait()` 是否只承诺有限 drain，还是需要无界 loop-until-idle。
6. 析构是否可能阻塞；如果会，文档是否写清楚。
7. Examples 是否展示了正常完成和停机路径。

这也是新增 `forge::` 原语时的审查清单。

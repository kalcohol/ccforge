# Forge cookbook

这份文档把 `include/forge/` 扩展设施按工程用法串起来。它不是 API
reference；reference 见 [`forge::` 扩展工具](forge-utilities.md)、[runtime
lifecycle contract](forge-runtime.md)、[`forge::accel` runtime vocabulary and backends](forge-accel.md)
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
6. `example/execution_on_example.cpp`：用 `on` 显式切换起始 scheduler，或在 closure
   内临时切换再切回原 completion scheduler。
7. `example/execution_spawn_future_example.cpp`：对比 scope token 下的 fire-and-forget
   `spawn` 与可等待的 `spawn_future`。
8. `example/execution_affine_example.cpp`：用 `affine` 表达目标 scheduler affinity 的
   serial transfer subset。
9. `example/execution_unstoppable_example.cpp`：把内层 sender 从外部 stop-token 中隔离。
10. `example/forge_io_read_write_example.cpp`：borrowed buffer async read/write。
11. `example/forge_io_typed_error_example.cpp`：typed IO error 跨 erased sender
   边界，并用 `forge::wait_result` 消费。
12. `example/forge_io_iocp_example.cpp`：Windows IOCP completion proof。
13. `example/forge_io_pipeline_example.cpp`：Linux fd readiness 与 CPU runtime handoff。
14. `example/forge_accel_staging_buffer_example.cpp`：owning host staging buffer 与 mock
   device buffer。
15. `example/forge_accel_memory_example.cpp`：memory kinds、byte buffers、cached-memory
    `flush` / `invalidate` proof 和 typed coherence error。
16. `example/forge_accel_message_device_example.cpp`：device session 与 message command
    形状。
17. `example/forge_accel_session_reset_example.cpp`：session reset 如何停止后续 command。
18. `example/forge_accel_packet_example.cpp`：owning command packet 与 completion
    bridge。
19. `example/forge_accel_request_runtime_example.cpp`：request ID、sync/post request
    handling 和 typed error boundary。
20. `example/forge_accel_protocol_transport_example.cpp`：portable envelope、late
    response discard 和 lifecycle signal。
21. `example/forge_accel_model_example.cpp`：NPU-style model/session/IO-binding proof。
22. `example/forge_accel_typed_error_example.cpp`：在 accelerator boundary 保留 typed
    error。
23. `example/forge_accel_trace_example.cpp`：可选 in-memory command timeline。
24. `example/forge_accel_pipeline_example.cpp`：mock device buffer、copy、submit 和 CPU
   continuation。
25. `example/forge_accel_cpu_copy_example.cpp`：用 CPU reference backend 跑真实
    H2D/D2H copy。
26. `example/forge_accel_cpu_pipeline_example.cpp`：CPU reference copy/compute queue
    和 event ordering。
27. `example/forge_accel_cpu_simd_example.cpp`：在 aligned CPU device buffer 上跑
    `std::simd`。
28. `example/forge_accel_backend_switch_example.cpp`：同一份 command vocabulary
    逻辑在 mock 和 CPU reference backend 上运行。
29. `example/forge_io_accel_pipeline_example.cpp`：Linux IO read/write handoff 到
    CPU reference accel queue。
30. `example/forge_inference_runtime_sketch.cpp`：把请求通道、runtime、strand、accel queue
   和资源生命周期放在同一个推理 runtime sketch 里。
31. `example/forge_reference_runtime_example.cpp`：一个拥有型 request/response service
    pattern，展示 bounded ingress、accel command、serialized stats、typed boundary
    errors、device-loss recovery、trace snapshot 和 graceful drain 如何放在同一个
    reference runtime 中。

这些例子优先展示“资源在哪里、取消如何传播、何时 drain、谁拥有谁”，不是为了把 API
调用堆到最多。

## learning paths

For a CPU service or message pipeline:

1. `example/forge_thread_pool_example.cpp`
2. `example/execution_on_example.cpp`
3. `example/execution_spawn_future_example.cpp`
4. `example/execution_affine_example.cpp`
5. `example/execution_unstoppable_example.cpp`
6. `example/forge_channel_example.cpp`
7. `example/forge_graceful_shutdown_example.cpp`
8. `example/forge_bounded_pipeline_example.cpp`
9. `example/forge_reference_runtime_example.cpp`

For OS IO handoff:

1. `example/forge_io_readiness_example.cpp`
2. `example/forge_io_read_write_example.cpp`
3. `example/forge_io_pipeline_example.cpp`
4. `example/forge_io_typed_error_example.cpp`
5. `example/forge_io_accel_pipeline_example.cpp` when validating Linux IO
   handoff into accelerator-shaped work.
6. `example/forge_io_iocp_example.cpp` when validating the Windows completion
   backend.

For accelerator-shaped work without vendor SDKs:

1. `example/forge_accel_copy_example.cpp`
2. `example/forge_accel_event_example.cpp`
3. `example/forge_accel_memory_example.cpp`
4. `example/forge_accel_pipeline_example.cpp`
5. `example/forge_accel_message_device_example.cpp`
6. `example/forge_accel_session_reset_example.cpp`
7. `example/forge_accel_packet_example.cpp`
8. `example/forge_accel_request_runtime_example.cpp`
9. `example/forge_accel_protocol_transport_example.cpp`
10. `example/forge_accel_model_example.cpp`
11. `example/forge_accel_typed_error_example.cpp`
12. `example/forge_accel_trace_example.cpp`
13. `example/forge_accel_cpu_copy_example.cpp`
14. `example/forge_accel_cpu_pipeline_example.cpp`
15. `example/forge_accel_cpu_simd_example.cpp`
16. `example/forge_accel_backend_switch_example.cpp`
17. `example/forge_io_accel_pipeline_example.cpp`
18. `example/forge_inference_runtime_sketch.cpp`
19. `example/forge_reference_runtime_example.cpp`

These paths intentionally stay example-first. The detailed contracts live in
the feature docs, so the cookbook remains a map rather than a duplicated API
reference.

## coverage map

| 场景 | 示例 |
| --- | --- |
| execution scope work | `example/execution_phase4_example.cpp`, `example/execution_spawn_future_example.cpp` |
| execution scheduler adaptors | `example/execution_on_example.cpp`, `example/execution_affine_example.cpp`, `example/execution_unstoppable_example.cpp` |
| CPU scheduler basics | `example/forge_thread_pool_example.cpp`, `example/forge_runtime_context_example.cpp` |
| timer / single-thread / system context | `example/forge_timer_context_example.cpp`, `example/forge_single_thread_context_example.cpp`, `example/forge_system_context_example.cpp` |
| channel backpressure | `example/forge_channel_example.cpp`, `example/forge_bounded_pipeline_example.cpp` |
| graceful close vs stop | `example/forge_graceful_shutdown_example.cpp` |
| PMR / bounded allocation | `example/forge_resource_policy_example.cpp`, `example/forge_bounded_pipeline_example.cpp` |
| serialized session state | `example/forge_strand_example.cpp`, `example/forge_bounded_pipeline_example.cpp` |
| type-erased boundary | `example/forge_type_erased_boundary_example.cpp`, `example/forge_io_typed_error_example.cpp`, `example/forge_accel_typed_error_example.cpp` |
| Linux IO readiness/read-write | `example/forge_io_readiness_example.cpp`, `example/forge_io_read_write_example.cpp`, `example/forge_io_pipeline_example.cpp` |
| Windows IOCP proof | `example/forge_io_iocp_example.cpp` |
| IO to accelerator-shaped work | `example/forge_io_accel_pipeline_example.cpp` |
| accelerator-shaped commands | `example/forge_accel_copy_example.cpp`, `example/forge_accel_event_example.cpp`, `example/forge_accel_memory_example.cpp`, `example/forge_accel_pipeline_example.cpp`, `example/forge_accel_cpu_copy_example.cpp`, `example/forge_accel_cpu_pipeline_example.cpp`, `example/forge_accel_cpu_simd_example.cpp`, `example/forge_accel_backend_switch_example.cpp` |
| device/session lifecycle and commands | `example/forge_accel_message_device_example.cpp`, `example/forge_accel_session_reset_example.cpp`, `example/forge_accel_packet_example.cpp`, `example/forge_accel_request_runtime_example.cpp` |
| protocol and telemetry proofs | `example/forge_accel_protocol_transport_example.cpp`, `example/forge_accel_trace_example.cpp` |
| model/session IO binding | `example/forge_accel_model_example.cpp` |
| reference runtime pattern | `example/forge_inference_runtime_sketch.cpp`, `example/forge_reference_runtime_example.cpp` |

`example/CMakeLists.txt` 会把已构建的示例注册成 `example_<target>_smoke`，所以这些
路径不是只编译不运行的文档片段；受 IO、accel 或 mdspan gate 控制的示例只在对应 target
存在时注册 smoke test。

## recipe: CPU work queue

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

## recipe: bounded producer/consumer

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

## recipe: serialize shared state

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

## recipe: own a session

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

## recipe: bounded allocations

适用场景：嵌入式、实时-ish pipeline、服务端热路径、推理 runtime 中的稳定分配边界。

使用：

- `std::pmr::synchronized_pool_resource`，或其他适合跨线程访问的自定义
  `std::pmr::memory_resource`
- `static_thread_pool_options{.memory = resource}`
- `bounded_channel_options{.memory = resource}`
- `strand_options{.memory = resource}`
- `timer_context_options{.memory = resource}` 或 `runtime_context_options{.memory = resource}`
- `forge::accel::mock::context_options{.memory = resource}`

关键点：

- pool 的 queue node 和 queued task callable record 受 resource 控制；
- channel、strand、timer callback/queue records、accel command/pending records 的受控路径使用传入 resource；
- 如果同一个 resource 会被多个 runtime primitive 或 worker 线程共享，不要直接使用裸
  `std::pmr::monotonic_buffer_resource`；可把固定 buffer 放在 monotonic upstream
  后面，再由 `std::pmr::synchronized_pool_resource` 作为对外 resource；
- `async_scope` op-state、strand runner keepalive node 和 timer callback callable record 已受 resource 控制。

参考：

- `example/forge_resource_policy_example.cpp`
- `example/forge_bounded_pipeline_example.cpp`
- `example/forge_inference_runtime_sketch.cpp`

## recipe: IO into protocol work

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
- `example/forge_io_accel_pipeline_example.cpp`

## recipe: accelerator-shaped pipeline

适用场景：先用 portable mock backend 验证 command queue / buffer / copy / submit
形状，再用 CPU reference backend 证明同一套 vocabulary 能跑真实 CPU/SIMD work，
最后再决定是否需要真实 vendor backend。

使用：

- `forge::accel::mock::context`
- `forge::accel::cpu::context`
- `forge::accel::mock::host_buffer<T>`
- `forge::accel::mock::device_buffer<T>`
- `forge::accel::cpu::device_buffer<T>`
- `forge::accel::mock::host_byte_buffer` / `device_byte_buffer`
- `copy_to_device` / `copy_to_host` / `copy_device_to_device`
- `flush` / `invalidate` for cached-memory proof
- `submit(queue, callable)`
- `submit_packet(session, command_packet{...}, handler, command_options{...})`
- `request_session`
- `protocol_envelope` / `mock::protocol::loopback_transport`
- `model` / `model_session` / `model_bindings` / `execute`
- `copy_to_device_typed` / `copy_to_host_typed` / `submit_typed` for typed
  boundary errors
- `event` / `record_event` / `wait_event` / `fence`
- `device` / `device_session` / `submit_message`
- `trace_sink`

关键点：

- 当前 backend 是 in-memory mock，不依赖 CUDA/HIP/SYCL；
- CPU reference backend 同样不依赖 CUDA/HIP/SYCL；它使用 aligned CPU storage，
  让 H2D/D2H/D2D copy 和 `std::simd` submit 在真实内存路径上运行；
- 每个 queue 命令按 FIFO 运行；跨 queue ordering 用 `event` 显式表达，不隐式生成 graph；
- host spans 是 borrowed，`device_buffer` 拥有 mock device storage；
- `host_buffer` 可表达由 Forge resource 分配的 owning host staging storage，但不是
  vendor pinned memory；
- `memory_kind` 是 portable metadata；`cached_device` 用 `flush` / `invalidate`
  模拟 command-boundary coherence error；
- event 是 one-shot completion marker，不建模跨 queue dependency graph。
- `device_info` / `device` / `device_session` 是 vendor-neutral discovery 和
  message-command proof，不暴露真实设备 handle；mock device loss 会映射到
  `device_lost`，reset 后旧 session 会映射到 `stale_session`。
- `submit_message` 借用 response；`submit_packet` 持有 request/response packet 并在
  成功时返回完成后的 packet，适合 callback/completion bridge 风格。
- `request_session` 持有 pending request map、分配递增 request ID，并统计 late
  response；`protocol_envelope` 是对象级 envelope proof，不是 ABI。
- `trace_sink` 是可选 in-memory telemetry proof，不是生产 profiler。
- `model` proof 只绑定 byte spans 并检查 IO byte size，不提供 tensor/graph/operator
  语义。
- 默认 accel API 使用 `set_error(std::exception_ptr)`；`*_typed` variants 使用
  `set_error(forge::accel::error)`，适合类型擦除或插件边界。

参考：

- `example/forge_accel_copy_example.cpp`
- `example/forge_accel_event_example.cpp`
- `example/forge_accel_memory_example.cpp`
- `example/forge_accel_staging_buffer_example.cpp`
- `example/forge_accel_message_device_example.cpp`
- `example/forge_accel_session_reset_example.cpp`
- `example/forge_accel_packet_example.cpp`
- `example/forge_accel_request_runtime_example.cpp`
- `example/forge_accel_protocol_transport_example.cpp`
- `example/forge_accel_model_example.cpp`
- `example/forge_accel_typed_error_example.cpp`
- `example/forge_accel_trace_example.cpp`
- `example/forge_accel_pipeline_example.cpp`
- `example/forge_accel_cpu_copy_example.cpp`
- `example/forge_accel_cpu_pipeline_example.cpp`
- `example/forge_accel_cpu_simd_example.cpp`
- `example/forge_accel_backend_switch_example.cpp`
- `example/forge_io_accel_pipeline_example.cpp`
- `example/forge_inference_runtime_sketch.cpp`

## recipe: reference runtime service

适用场景：把 CPU runtime、bounded message queue、accelerator-like command queue、
resource policy 和序列化 session state 组合成一个拥有型服务对象。这个 recipe 是
pattern，不是新的 framework API；它展示在用户代码里如何把已有原语拼成清晰的
runtime 边界。

使用：

- `forge::resource_context` 拥有 worker；
- `forge::bounded_channel<Request>` 表达 bounded ingress；
- `forge::bounded_channel<Response>` 表达 bounded response path；
- `forge::accel::mock::context` / `queue` / `device_buffer` 表达 device-like work；
- `forge::strand` 序列化统计或 session state；
- `forge::wait_result` 消费 opt-in typed accel errors。

关键点：

- 当前保持 example-only。`inference_runtime_sketch` 和 `reference_runtime_example`
  还没有重复到足以冻结一个新的 public helper；
- 如果未来新增 helper，它应是小的 lifecycle utility，例如 `service_scope` 或
  `owned_service`，只封装 scheduler/spawn/close/request_stop/shutdown/wait，
  不内置 IO、accel、tensor 或 serving policy；
- service 析构可以阻塞，因为它显式拥有 runtime/context；
- request channel `close()` 后，worker 会 drain 已接受请求并关闭 response channel；
- close 后新 request 会被拒绝，示例用断言钉住这个 admission boundary；
- response channel capacity 小于 request 数时，consumer 必须继续 drain response，
  这正是 backpressure 的教学点；
- typed errors 保留在 command boundary，默认 surface 不需要扩大成全局错误体系；
- reference runtime 示例会执行一个 `size_mismatch` 和一个 `device_lost` command，
  然后 reset mock device 并继续处理后续 request；
- trace snapshot 用来验证 command timeline 和 lifecycle event 可观察，但不会改变
  runtime 行为。

参考：

- `example/forge_reference_runtime_example.cpp`
- `example/forge_inference_runtime_sketch.cpp`
- `example/forge_io_accel_pipeline_example.cpp`

## recipe: type erase at boundaries

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
- `example/forge_accel_typed_error_example.cpp`
- `example/forge_any_sender_example.cpp`
- `example/forge_any_receiver_example.cpp`

## shutdown checklist

写一个拥有型 runtime 或 session 时，按这个顺序检查：

1. 新入口是否有 `close()` 或 `shutdown()` 后拒绝策略。
2. Pending operation 是否会被 close/stop 唤醒。
3. Receiver completion 是否在内部锁外调用。
4. Receiver stop-token 的 callback 生命周期是否短于 receiver completion 生命周期。
5. `wait()` 是否只承诺有限 drain，还是需要无界 loop-until-idle。
6. 析构是否可能阻塞；如果会，文档是否写清楚。
7. Examples 是否展示了正常完成和停机路径。

这也是新增 `forge::` 原语时的审查清单。

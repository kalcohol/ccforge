# `forge::` 扩展工具

`include/forge/` 下的头文件是 Forge 自带的实用扩展，不是标准 backport，也不向 `namespace std` 注入名字。它们的目标是补齐使用 `std::execution` 时常见的运行时设施，让下游不必为基本线程池、单线程执行上下文、定时器和窄类型擦除再写一套本地胶水。

运行时对象的 `close()` / `request_stop()` / `shutdown()` / `wait()` 语义见 [Forge runtime lifecycle contract](forge-runtime.md)。新增 `forge::` 运行时设施应先对齐这份契约，再扩展具体行为。

聚合头：

```cpp
#include <forge/execution.hpp>
```

该头会包含 `async_scope.hpp`、`any_sender.hpp`、`any_receiver.hpp`、`any_scheduler.hpp`、`channel.hpp`、`erased_sender.hpp`、`resource_policy.hpp`、`resource_context.hpp`、`runtime_context.hpp`、`static_thread_pool.hpp`、`strand.hpp`、`single_thread_context.hpp`、`system_context.hpp`、`timer_context.hpp` 和 `task.hpp`。如果只需要单个设施，也可以直接包含对应头文件。

## Resource Policy

- `forge::resource_policy`：V1 资源策略词汇，当前只包含非拥有的 `std::pmr::memory_resource*`。`default_memory_resource()` 返回 `std::pmr::get_default_resource()`，`normalize_memory_resource(ptr)` 会把 `nullptr` 归一为默认 resource。

资源策略不拥有 `memory_resource`；调用方必须保证传入的 resource 活得比使用它的 runtime primitive 更久。V1 只控制明确接入的路径，不承诺全局零分配：

- `static_thread_pool` 使用 resource 控制队列 `pmr::deque` 节点；`std::function` 内部 target allocation 仍由标准库实现决定。
- `bounded_channel` 使用 resource 控制 buffer、pending send/recv 队列、action 批次和 send/recv record control block。
- `strand` 使用 resource 控制 state、pending queue、stop 批次和 receiver record；runner node 仍保持原来的 intrusive keepalive `new/delete`，以保护已验证的同步完成生命周期模型。
- `runtime_context` / `resource_context` 会把 resource 传给内部 `static_thread_pool`。`async_scope` spawned op-state 与 `timer_context` timer item 目前不受该 policy 控制。

## 调度与上下文

- `forge::static_thread_pool`：固定大小线程池，提供 `scheduler`，可通过 `std::execution::schedule(pool.get_scheduler())` 产生 sender。默认构造路径保持无界队列；需要有界 ingress 时可传入 `static_thread_pool_options{.queue_capacity = N}`，需要控制队列节点分配时可传入 `.memory = resource`。队列满、shutdown 后新启动或 receiver 已停止的 schedule operation 会以 `set_stopped` 完成。已接受的任务会在 `shutdown()` 后继续 drain；`wait()` 会等待队列和正在运行的任务清空。其 schedule sender env 会通过 Forge backport 的 `get_completion_scheduler<set_value_t>` CPO 返回原 scheduler。
- `forge::single_thread_context`：单工作线程上下文，复用 `static_thread_pool{1}`，适合需要串行化执行或测试调度切换的场景。
- `forge::system_context` / `forge::get_system_scheduler()`：进程内共享线程池单例，适合示例和轻量工具。长期服务建议显式持有自己的 pool/context，以便控制 shutdown 时机。
- `forge::timer_context`：单线程定时上下文，提供 `schedule_after(duration)` 与 `schedule_at(time_point)`。到期完成 `set_value()`；shutdown、已停止 receiver、shutdown 后入队或入队后 receiver stop token 请求停止，都会完成 `set_stopped()`。`wait()` 会等待已接受 timer 操作完成。
  - 入队后取消采用轮询实现而非 stop callback：只要有可停止的 timer 待处理，worker 线程就会把等待上限压到 `poll_interval`（1ms）并在每次唤醒时检查 stop token。代价是取消最多有约 1ms 延迟，且当存在带 stop token 的长时定时器时 worker 会以约 1kHz 周期性唤醒（略有空转）；换来的是单 worker 作为已入队项的唯一完成者，从根本上消除了 value/stopped 完成路径的竞争与 stop-callback 生命周期问题。
- `forge::runtime_context`：显式拥有的运行时上下文，组合一个 `static_thread_pool` 和一个 `timer_context`。`runtime_context_options` 可配置线程数、pool 队列容量和 pool 队列 resource。`get_scheduler()` 返回 CPU scheduler，`schedule_after` / `schedule_at` 转发到内部 timer；`shutdown()` 同时停止 timer 和 pool，`wait()` 执行实用的 pool -> timer -> pool drain，覆盖常见 CPU/timer 单跳交接。
- `forge::strand`：scheduler 串行化 wrapper。`strand{scheduler}.get_scheduler()` 返回一个 scheduler，接受的 schedule work 按 FIFO 运行，并保证同一 strand 上最多一个任务处于用户 completion 中。`strand_options{.memory = resource}` 可控制 pending queue 和 receiver record 分配。`shutdown()` 会把 pending/future work 以 stopped 完成；其 schedule sender env 同样暴露 Forge backport completion-scheduler roundtrip。
- `forge::async_scope`：拥有一组 eager-start sender work 的结构化并发 scope。`spawn(sender)` 在 scope open 时启动并返回 `true`，`close()` 后拒绝新任务，`request_stop()` 会让后续和已拥有任务的 receiver env 暴露已请求的 stop token，`shutdown()` 等价于 close + request stop。析构会 `shutdown()` 并 `wait()`，因此可能阻塞到 scope-owned work 完成或响应停止。scope 捕获第一个 error 为 `std::exception_ptr`，可通过 `first_error()` / `rethrow_if_error()` 读取。

`async_scope` 使用 start-detached 风格的 heap op-state keepalive：同步完成时不会在 source `start()` 调用栈内销毁 source operation-state，异步完成时由 terminal completion 释放最后引用。这允许它安全接住 `forge::task` 这类在 `final_suspend` 同步发 completion 的 sender。

- `forge::resource_context`：资源/会话 owning runtime shell，组合 `runtime_context` 与 `async_scope`。`resource_context_options` 可配置内部 runtime 的线程数、pool 队列容量和 pool 队列 resource；scope op-state 尚不受 resource policy 控制。它不是硬件驱动框架，也不强制拥有 channel；用户可把设备句柄、`bounded_channel<Command>` 和 `bounded_channel<Event>` 与它并排存放。`shutdown()` 先 close/request_stop scope，再关闭 runtime；析构会 shutdown + wait，因此适合资源会话的安全收尾。

## 消息通道

- `forge::bounded_channel<T>`：有界 FIFO 消息通道，提供 `async_send(T)`、`async_recv()`、`try_send(T)`、`try_recv()`、`close()`、`request_stop()` 和 `shutdown()`。可用 `bounded_channel_options{.capacity = N, .memory = resource}` 控制容量和 channel 内部 buffer/pending/record 分配。send 在值被缓冲或直接交给等待中的 receiver 后完成 `set_value()`；recv 在收到值时完成 `set_value(T)`。`close()` 拒绝新 send 并允许已缓冲值 drain；`request_stop()` 取消 pending send/recv 并丢弃缓冲值。

`bounded_channel` 的 V1 stop-token 支持是保守的：operation `start()` 前如果 receiver stop token 已请求，会直接 `set_stopped()`；已经入队的 pending send/recv 不注册 per-op stop callback，因此 receiver stop token 在入队后才被请求时，不会单独唤醒 idle waiter。此时需要 channel `close()` / `request_stop()` 或其它 channel 状态变化来完成 pending operation。

这些设施的 schedule/timer operation state 应按 sender/receiver 常规约定保持存活直到完成；它们不是 cancel-on-destroy 句柄。`runtime_context::wait()` 不是无界 quiescence 协议：如果回调递归地持续提交新 CPU/timer work，调用方仍应自行定义停止条件。

## Coroutine Sender

- `forge::task<T>`：协程返回类型，同时建模 sender。task body 可以 `co_await` 同步或异步 sender，外部可以用 `std::execution::sync_wait` 或其他 sender 组合器消费。

当前限制：`forge::task` 在 coroutine `final_suspend` 中同步发出 receiver completion；自定义 receiver 不应在 `set_value` / `set_error` / `set_stopped` 回调内同步销毁连接的 task operation-state。

## 类型擦除

- `forge::any_receiver_of<CompletionSignatures>`：窄 receiver 类型擦除，使用 64B SBO + 堆回退。value completion 采用声明的单一 value tuple 形状；error completion 折叠为 `std::exception_ptr`。
- `forge::any_sender_of<CompletionSignatures>`：窄 sender 存储工具，使用 64B SBO + 堆回退，并提供 `sync_wait()` 直接运行存储的 sender。
- `forge::any_scheduler`：窄 scheduler 类型擦除，面向 `schedule()` 这一种常见形状。它按共享 erased state 做 identity equality；拷贝出的 `any_scheduler` 相等，两个分别擦除同一个 concrete scheduler 的对象也会因为 state 不同而不相等。
- `forge::erased_sender<CompletionSignatures>`：connectable sender 类型擦除。v1 是 move-only、heap-first 实现，支持多个唯一 value 形状、`set_error_t(std::exception_ptr)` 和 `set_stopped_t()`；typed error、allocator-aware storage、语义 equality 和任意自定义 receiver env 查询都不属于 v1。

`any_sender_of` 不是通用 connectable erased sender：它不做多 completion-shape vtable 分发，也不承诺保留任意 `set_error_t(E)` 类型。需要 connectable erased sender 时使用 `erased_sender`；两者语义边界见 [`forge::erased_sender` 设计与限制](forge-erased-sender-design.md)。

两个擦除类型对“空对象”的处理不对称，使用时需注意：空 `any_scheduler`（默认构造）被 schedule 时以 `set_error` 完成；而空 `erased_sender`（默认构造）在 `connect` 时抛 `std::runtime_error`。请仅在持有有效底层对象后再连接/调度。

`any_scheduler` 建模的是 Forge 当前 backport 的本地 scheduler concept。Forge 公共 scheduler 的 schedule sender env 已通过 backport 的 tag-invoke `get_completion_scheduler<set_value_t>` CPO 暴露 completion-scheduler roundtrip；原生 C++26 member-query env 口径仍是 forward-compat caveat。

## 示例与测试

示例位于：

- `example/forge_thread_pool_example.cpp`
- `example/forge_single_thread_context_example.cpp`
- `example/forge_system_context_example.cpp`
- `example/forge_timer_context_example.cpp`
- `example/forge_runtime_context_example.cpp`
- `example/forge_async_scope_example.cpp`
- `example/forge_channel_example.cpp`
- `example/forge_resource_policy_example.cpp`
- `example/forge_resource_context_example.cpp`
- `example/forge_strand_example.cpp`
- `example/forge_bounded_pipeline_example.cpp`
- `example/forge_any_scheduler_example.cpp`
- `example/forge_any_sender_example.cpp`
- `example/forge_any_receiver_example.cpp`

对应测试在 `test/forge/` 下，可通过以下命令单独运行：

```bash
ctest --test-dir build/local -R '^forge_' --output-on-failure
```

# `forge::` 扩展工具

`include/forge/` 下的头文件是 Forge 自带的实用扩展，不是标准 backport，也不向
`namespace std` 注入名字。它们的目标是补齐使用 `std::execution` 时常见的运行时设施，
让下游不必为基本线程池、单线程执行上下文、定时器和窄类型擦除再写一套本地胶水。

头文件命名也按这两层区分：标准 backport 模仿标准库入口，使用 `<execution>`、
`<memory>` 这类无扩展名头；非标准 `forge::` 扩展保持 `<forge/*.hpp>` 形式。

运行时对象的 `close()` / `request_stop()` / `shutdown()` / `wait()` 语义见
[forge runtime lifecycle contract](forge-runtime.md)。新增 `forge::` 运行时设施应先
对齐这份契约，再扩展具体行为。

聚合头：

```cpp
#include <forge/execution.hpp>
```

该头会包含 `async_scope.hpp`、`any_sender.hpp`、`any_receiver.hpp`、
`any_scheduler.hpp`、`channel.hpp`、`erased_sender.hpp`、`resource_policy.hpp`、
`resource_context.hpp`、`runtime_context.hpp`、`static_thread_pool.hpp`、
`start_detached.hpp`、`strand.hpp`、`single_thread_context.hpp`、
`system_context.hpp`、`timer_context.hpp`、`task.hpp` 和 `wait_result.hpp`。如果只需要
单个设施，也可以直接包含对应头文件。

IO backend umbrella 使用独立头：

```cpp
#include <forge/io.hpp>
```

它只在 `FORGE_ENABLE_FORGE_IO` gate 探测到 backend 时暴露 `forge::io::context`；byte
IO vocabulary、memory streams、coroutine bridge 和 combinators 使用
`<forge/io/...>` direct headers，不受 OS backend gate 控制。详见
[`forge::io`](forge-io.md)。

## Resource policy（资源策略）

- `forge::resource_policy`：V1 资源策略词汇，当前只包含非拥有的
  `std::pmr::memory_resource*`。`default_memory_resource()` 返回
  `std::pmr::get_default_resource()`，`normalize_memory_resource(ptr)` 会把
  `nullptr` 归一为默认 resource。

资源策略不拥有 `memory_resource`；调用方必须保证传入的 resource 活得比使用它的
runtime primitive 更久。如果同一个 resource 会被多个 runtime primitive 或多个
worker 线程共享，resource 本身也必须是线程安全的，例如使用
`std::pmr::synchronized_pool_resource`；不要把裸
`std::pmr::monotonic_buffer_resource` 同时交给多线程 runtime 路径。V1 只控制明确接入
的路径，不承诺全局零分配：

`async_scope::wait()` 会等到 spawned op-state node 已析构并归还给 resource；resource
仍然是 non-owning，必须继续活过 scope 对象本身。`strand` 的 runner node 则可能在
terminal completion 尾部释放，因此它的 resource 还必须活到该释放路径结束。

- `static_thread_pool` 使用 resource 控制队列 `pmr::deque` 节点和内部 queued task
  callable record；这是 pool 的私有实现细节，不是公开的 `move_only_function` API。
- `bounded_channel` 使用 resource 控制 state、buffer 和 send/recv record control
  block；pending send/recv 使用 record 内嵌链，不另行分配。
- `strand` 使用 resource 控制 state、pending queue、stop 批次、receiver record 和
  runner keepalive node。
- `timer_context` 使用 resource 控制 state、timer op data、timer item control block、
  timer queue 和 timer callback callable record。
- `runtime_context` 会把 resource 传给内部 `static_thread_pool` 和
  `timer_context`。`resource_context` 还会把同一 resource 传给内部 `async_scope`
  spawned op-state。

Allocation audit:

| component | resource-controlled paths | intentionally uncontrolled / deferred | evidence |
| --- | --- | --- | --- |
| `static_thread_pool` | pool queue `pmr::deque` nodes and queued task callable records | worker thread objects and OS thread resources | `forge_thread_pool`, `example/forge_resource_policy_example.cpp` |
| `timer_context` | context state, timer op data, timer item control blocks, timer queue, timer callback callable records | OS timer worker thread resources | `forge_timer_context` |
| `runtime_context` | forwards the resource to the internal pool and timer | no separate allocation policy beyond its members | `forge_runtime_context` |
| `resource_context` | forwards the resource to the internal runtime and async scope spawned op-state | no separate allocation policy beyond its members | `forge_resource_context` |
| `bounded_channel<T>` | channel state, buffer, send/recv record control blocks | pending send/recv links are embedded in records; storage inside user-provided `T` values is the user's responsibility | `forge_channel` |
| `strand` | strand state, pending queue, stopped batches, receiver records, runner keepalive nodes | underlying scheduler resources remain owned by that scheduler | `forge_strand` |
| `async_scope` | scope state and spawned op-state nodes | source sender internals remain controlled by the source sender | `forge_async_scope` |
| `forge::io` Linux backend | context state, fd waiter map, epoll event buffer, readiness records | deferred completion batches use an allocation-free intrusive record chain; fd ownership and borrowed buffers stay with the caller; OS kernel objects are outside PMR | `forge_io_context` |
| `forge::io` Windows backend | context state, pending record map, associated handle cache, IO records | `HANDLE` ownership and borrowed buffers stay with the caller; IOCP/kernel resources are outside PMR | `forge_io_iocp` |
| type erasure helpers | none in V1 | `any_sender_of`, `any_receiver_of`, `any_scheduler`, and `erased_sender` use SBO/default heap storage and are not allocator-aware | `forge_any_sender`, `forge_any_receiver`, `forge_any_scheduler`, `forge_erased_sender` |

Failure policy:

- Capacity full or shutdown-after-start paths complete with stopped where the
  primitive can make that decision without throwing from `start()`.
- Allocation failure generally follows the default exception path. Typed-error
  variants only classify allocation/capacity failures when the classification is
  stable for that surface.
- `bounded_channel::async_send` reports a buffer-node allocation failure through
  `set_error(std::exception_ptr)`; `strand` treats an enqueue allocation failure
  like a scheduler launch failure and fails closed with stopped completions.
- Forge does not claim global zero allocation. The policy is scoped to the
  paths listed above.

## 调度与上下文

- `forge::static_thread_pool`：固定大小线程池，提供 `scheduler`，可通过
  `std::execution::schedule(pool.get_scheduler())` 产生 sender。默认构造路径保持无界队列；
  需要有界 ingress 时可传入 `static_thread_pool_options{.queue_capacity = N}`，需要控制
  队列节点和 queued task callable record 分配时可传入 `.memory = resource`。队列满、
  shutdown 后新启动、task record 分配失败或 receiver 已停止的 schedule operation 会以
  `set_stopped` 完成。已接受的任务会在 `shutdown()` 后继续 drain；`wait()` 会等待队列
  和正在运行的任务清空；如果从 pool 自己的 worker 线程调用，`wait()` 会立即返回以避免
  自锁。pool 对象本身不得在自己的 worker 线程上析构；完整 teardown 应由外层 owner
  调用 `shutdown()` / `wait()`。其 schedule sender env 会通过 Forge backport 的
  `get_completion_scheduler<set_value_t>` CPO 返回原 scheduler。
- `forge::single_thread_context`：单工作线程上下文，复用 `static_thread_pool{1}`，适合需要串行化执行或测试调度切换的场景。
- `forge::system_context` / `forge::get_system_scheduler()`：进程内共享线程池单例，适合示例
  和轻量工具。该 singleton 是 process-lifetime 对象，不在 C++ static teardown 期间析构，
  以避免静态析构顺序中的悬垂访问。`shutdown()` 只请求停止；如果需要等待已接受的
  work 完成，调用 `wait()`。长期服务建议显式持有自己的 pool/context，以便控制
  shutdown / wait 时机。
- `forge::timer_context`：单线程定时上下文，提供 `schedule_after(duration)` 与
  `schedule_at(time_point)`。到期完成 `set_value()`；shutdown、已停止 receiver、
  shutdown 后入队或入队后 receiver stop token 请求停止，都会完成 `set_stopped()`。
  `timer_context_options{.memory = resource}` 可控制 state、timer op data、timer item
  control block、timer queue 和 timer callback callable record 分配；`wait()` 会等待已
  接受 timer 操作完成。timer sender 只有 value/stopped completion；item 或 callback
  storage 分配失败也完成为 stopped，不保留 allocation exception。
  - 入队后取消使用 per-item stop callback 唤醒 worker。callback 只标记 item 并通知
    condition variable；真正的 value/stopped completion 仍由 timer worker 在线程外部锁
    之外执行，且 completion 前会先销毁 callback registration，避免 stop callback 与
    receiver completion 同时触碰同一 op data。
- `forge::runtime_context`：显式拥有的运行时上下文，组合一个 `static_thread_pool` 和一个
  `timer_context`。`runtime_context_options` 可配置线程数、pool 队列容量和共享
  resource。`get_scheduler()` 返回 CPU scheduler，`schedule_after` / `schedule_at` 转发
  到内部 timer；`shutdown()` 同时停止 timer 和 pool，`wait()` 执行实用的
  pool -> timer -> pool drain，覆盖常见 CPU/timer 单跳交接。析构会先 `shutdown()`，再用
  同一 drain 顺序等待已接受 work，之后才销毁内部 timer 和 pool。
- `forge::strand`：scheduler 串行化 wrapper。`strand{scheduler}.get_scheduler()` 返回
  一个 scheduler，接受的 schedule work 按 FIFO 运行，并保证同一 strand 上最多一个任务
  处于用户 completion 中。`strand_options{.memory = resource}` 可控制 pending queue 和
  receiver record 分配。`shutdown()` 会把 pending/future work 以 stopped 完成；从该
  strand 正在执行的 completion 内部调用 `wait()` 会立即返回以避免自锁，完整 drain 应由
  外部 owner 调用；其 schedule sender env 同样暴露 Forge backport
  completion-scheduler roundtrip。若底层 scheduler launch 失败，strand 会 fail closed
  并把 pending work 完成为 stopped；当前不区分瞬态 full queue 与永久 shutdown。用于
  strand 的 resource 还需覆盖 runner keepalive node 的 completion 尾部释放。
- `forge::async_scope`：拥有一组 eager-start sender work 的结构化并发 scope。
  `spawn(sender)` 在 scope open 时启动并返回 `true`，`close()` 后拒绝新任务，
  `request_stop()` 会让后续和已拥有任务的 receiver env 暴露已请求的 stop token，
  `shutdown()` 等价于 close + request stop。析构会 `shutdown()` 并 `wait()`，因此可能
  阻塞到 scope-owned work 完成或响应停止。scope 对象不得在它自己拥有的 spawned
  work body / completion callback 内调用 `wait()` 或析构；该 work 自身仍计入 active
  count，外层 owner 应负责 drain。scope 捕获第一个 error 为
  `std::exception_ptr`，可通过 `first_error()` / `rethrow_if_error()` 读取。

`spawn(sender)` 对 non-copyable non-const lvalue sender 采用 Forge runtime convenience：它会 destructively move 该 lvalue 并启动工作。若代码需要在 native C++26 execution 实现下无感迁移，请显式写 `std::move(sender)`。

`async_scope` 使用 start-detached 风格的 heap op-state keepalive：同步完成时不会在
source `start()` 调用栈内销毁 source operation-state，异步完成时由 terminal completion
释放最后引用。这允许它安全接住 `forge::task` 这类在 `final_suspend` 同步发 completion
的 sender。Node 析构和 deallocate 发生在 active count 归零之前，因此
`async_scope::wait()` 也是 scope-owned operation-state destruction barrier；
`async_scope_options::memory` 仍必须活过 scope 对象本身。

- `forge::resource_context`：资源/会话 owning runtime shell，组合 `runtime_context` 与
  `async_scope`。`resource_context_options` 可配置内部 runtime 的线程数、pool 队列容量
  和共享 resource；同一 resource 会传给 runtime 和 scope op-state。它不是硬件驱动框架，
  也不强制拥有 channel；用户可把设备句柄、`bounded_channel<Command>` 和
  `bounded_channel<Event>` 与它并排存放。`shutdown()` 先 close/request_stop scope，再
  关闭 runtime；析构会 shutdown + wait，因此适合资源会话的安全收尾。

## IO backend（IO 后端）

- `forge::io::context`：平台 IO context。Linux backend 是 `epoll` + `eventfd`
  readiness context，提供 `readable(fd)` / `writable(fd)` sender，以及 borrowed-span
  `async_read_some` / `async_write_some` convenience。Windows backend 是小型 IOCP
  completion proof，提供 overlapped `async_read_some` / `async_write_some`。

fd / `HANDLE` 和 buffer 都是 borrowed，调用方必须保证它们活到 operation 完成、
`cancel(...)` 后 drain，或 context shutdown/wait 之后。Linux readiness sender 完成
`set_value()` 只表示 fd ready，真正的 `read(2)` / `write(2)` 由用户代码执行；Linux
async read/write convenience 和 Windows IOCP operation 完成 `set_value(std::size_t)`。
默认 API 使用 `std::exception_ptr` 错误；`*_typed` opt-in 变体可把稳定错误分类暴露为
`forge::io::error`。

macOS/BSD kqueue 当前不在项目需求内；Linux `io_uring` 仅在需要 kernel
submission/completion queue 语义时才应单独立项。详细语义见 [`forge::io`](forge-io.md)。

## 消息通道

- `forge::bounded_channel<T>`：有界 FIFO 消息通道，提供 `async_send(T)`、
  `async_recv()`、`try_send(T)`、`try_recv()`、`close()`、`request_stop()` 和
  `shutdown()`。可用 `bounded_channel_options{.capacity = N, .memory = resource}` 控制
  容量和 channel 内部 state/buffer/record 分配。`T` 必须 nothrow move constructible，
  以保证 asynchronous `start()`、close 和 cancellation 路径不因值移动抛异常。send
  在值被缓冲或直接交给等待中的
  receiver 后完成 `set_value()`；recv 在收到值时完成 `set_value(T)`。`close()` 拒绝新
  send 并允许已缓冲值 drain；`request_stop()` 取消 pending send/recv 并丢弃缓冲值。

`bounded_channel` 会观察 receiver stop token：operation `start()` 前如果 token 已请求，
会直接 `set_stopped()`；如果 send/recv 已经进入 pending 队列且 token 后续请求停止，
channel 会把该 pending operation 从队列移除并在 mutex 外完成 `set_stopped()`。不带
stoppable token 的 pending operation 仍由 value、`close()` 或 channel-level
`request_stop()` 完成。

这些设施的 schedule/timer operation state 应按 sender/receiver 常规约定保持存活直到完成；
它们不是 cancel-on-destroy 句柄。`runtime_context::wait()` 不是无界 quiescence 协议：
如果回调递归地持续提交新 CPU/timer work，调用方仍应自行定义停止条件。

## Coroutine sender（协程 sender）

- `forge::task<T>`：协程返回类型，同时建模 sender。task body 可以 `co_await` 同步或
  异步 sender，外部可以用 `std::this_thread::sync_wait` 或其他 sender 组合器消费。

当前限制：`forge::task` 在 coroutine `final_suspend` 中同步发出 receiver completion；
自定义 receiver 不应在 `set_value` / `set_error` / `set_stopped` 回调内同步销毁连接的
task operation-state。
Stopped completion 通过 coroutine bridge 的内部异常回到 task frame，用户 `catch(...)`
可以捕获它；一旦 promise 标记 stopped，后续异常不会改写最终 stopped completion。

## 类型擦除

- `forge::any_receiver_of<CompletionSignatures>`：窄 receiver 类型擦除，使用 64B SBO +
  堆回退。构造阶段按 `receiver_of` 的 call-expression 规则接受底层 receiver，因此其
  handler 可以接受声明参数的隐式转换；value completion 的调用参数必须与声明的单一
  value tuple 精确匹配，不做缺省补值。声明中的 `T` / `T&` / `const T&` 类别会原样保留，reference payload
  只在同步 completion call stack 内借用，不会在擦除边界物化副本；error completion
  折叠为 `std::exception_ptr`。
- `forge::any_sender_of<CompletionSignatures>`：窄 sender 存储工具，使用 64B SBO + 堆回退，
  并提供 `sync_wait()` 直接运行存储的 sender。底层 sender 的单一 value tuple 必须与
  `CompletionSignatures` 精确一致。
- `forge::any_scheduler`：窄 scheduler 类型擦除，面向 `schedule()` 这一种常见形状。它按
  共享 erased state 做 identity equality；拷贝出的 `any_scheduler` 相等，两个分别擦除
  同一个 concrete scheduler 的对象也会因为 state 不同而不相等。内部 erased receiver
  会转发 downstream receiver stop token，因此底层 scheduler operation 仍能观察调用方取消。
- `forge::erased_sender<CompletionSignatures>`：connectable sender 类型擦除。当前实现是
  move-only、heap-first，支持多个唯一 value 形状、closed-set `set_error_t(E)` typed
  errors（包括 `std::exception_ptr`）和 `set_stopped_t()`。Value/error dispatch 保留
  `CompletionSignatures` 声明的引用类别；reference payload 只在同步 completion call
  stack 内借用。构造约束按实际 erased receiver env（含 downstream stop token）推导，
  因而会拒绝在该 env 下新增未声明 completion 的 source sender。allocator-aware storage、
  语义 equality 和任意自定义 receiver env 查询不属于当前范围。
- `forge::wait_result(sender)`：同步运行 sender 并返回一个小 result 对象，保留 value、
  closed-set typed error 和 stopped。它是 Forge 便利设施，不改变
  `std::this_thread::sync_wait`；`set_error(E)` 保留声明的 error 类型，value materialization
  失败、typed error materialization 失败，或 sender/adaptor 显式传出的
  `std::exception_ptr` 会作为 `std::exception_ptr` error 保存。

`any_sender_of` 不是通用 connectable erased sender：它不做多 completion-shape vtable 分发，
也不承诺保留任意 `set_error_t(E)` 类型。需要 connectable erased sender 时使用
`erased_sender`；两者语义边界见
[`forge::erased_sender` 设计与限制](forge-erased-sender-design.md)。

两个擦除类型对“空对象”的处理不对称，使用时需注意：空 `any_scheduler`（默认构造）被
schedule 时以 `set_error` 完成；而空 `erased_sender`（默认构造）在 `connect` 时抛
`std::runtime_error`。请仅在持有有效底层对象后再连接/调度。

`any_scheduler` 建模的是 Forge 当前 backport 的本地 scheduler concept。Forge 公共
scheduler 的 schedule sender env 已通过 backport 的 tag-invoke
`get_completion_scheduler<set_value_t>` CPO 暴露 completion-scheduler roundtrip；原生
C++26 member-query env 口径仍是 forward-compat caveat。

## 示例与测试

完整示例清单按 backport、execution、runtime/erasure 与 coroutine-native IO 分组维护在
[`docs/examples.md`](examples.md)。

对应测试在 `test/forge/` 下，可通过以下命令单独运行：

```bash
ctest --test-dir build/local -R '^forge_' --output-on-failure
```

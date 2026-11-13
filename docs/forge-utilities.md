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

该头会包含 `async_scope.hpp`、`any_stop_token.hpp`、`any_sender.hpp`、`any_receiver.hpp`、
`any_scheduler.hpp`、`channel.hpp`、`erased_sender.hpp`、`resource_policy.hpp`、
`resource_context.hpp`、`runtime_context.hpp`、`static_thread_pool.hpp`、
`start_detached.hpp`、`strand.hpp`、`single_thread_context.hpp`、
`system_context.hpp`、`timer_context.hpp`、`task.hpp` 和 `wait_result.hpp`。如果只需要
单个设施，也可以直接包含对应头文件。

`forge::any_stop_token` 是 Forge extension 的 copyable stop-token type erasure，直接入口是
`<forge/any_stop_token.hpp>`。当前标准 execution vocabulary 不定义
`std::any_stop_token`；Forge 不向 `namespace std` 注入这个非标准名字。分别擦除但引用
同一个逻辑 stop state 的 token 比较相等。已支持的标准 stop-token 的 disengaged 表示统一为空，
但不能仅凭 `stop_possible()==false` 判空：失去 source 的 `std::stop_token` 仍保留原状态身份。
自定义 token 按其类型和自身相等运算保存身份；擦除层不推断未知类型的跨类型空状态。

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
terminal completion 尾部释放；容量通知回调也持有共享 state，因此 resource 必须活到
这些释放路径结束。直接使用 pool scheduler 的 strand 会保留 pool state；应先结束 strand
及其 scheduler handle 的使用，再销毁底层 pool 和 resource。

- `static_thread_pool` 使用 resource 控制共享 pool state 块、队列 `pmr::deque` 节点
  和内部 queued task callable record；这是 pool 的私有实现细节，不是公开的
  `move_only_function` API。worker 在 detach 路径下持有 state 直到退出，因此
  resource 必须活过 detached drain 尾部（见下文调度小节）。
- `bounded_channel` 使用 resource 控制 state、buffer 和 send/recv record control
  block；pending send/recv 使用 record 内嵌链，不另行分配。
- `strand` 使用 resource 控制 state、pending queue、stop 批次、receiver record 和
  runner keepalive node；容量 waiter 内嵌在 state 中，注册和通知不额外分配。
- `timer_context` 使用 resource 控制 state、timer op data、timer item control block、
  timer queue 和 timer callback callable record。
- `runtime_context` 会把 resource 传给内部 `static_thread_pool` 和
  `timer_context`。`resource_context` 还会把同一 resource 传给内部 `async_scope`
  spawned op-state。

## 分配审计

| 组件 | resource 控制的路径 | 未控制的路径 | 参考示例 |
| --- | --- | --- | --- |
| static_thread_pool | state、队列节点、task callable record | worker 对象与 OS 线程 | `forge_thread_pool` |
| timer_context | state、op data、timer item、队列、callback record | OS 线程；stop-token 擦除与 callback 注册走全局堆 | `forge_timer_context` |
| runtime_context | 转发给 pool 和 timer | 无独立分配策略 | `forge_runtime_context` |
| resource_context | 转发给 runtime 和 scope | 无独立分配策略 | `forge_resource_context` |
| bounded_channel | state、buffer、send/recv record | 用户 T 的内部存储；stop-token 擦除与 callback 注册走全局堆，pending 链内嵌 | `forge_channel` |
| strand | state、队列、stop 批次、receiver record、runner node、内嵌容量 waiter | 底层 scheduler；any_scheduler 构造走全局堆，非直接 pool 路径的 runner 还有 erased operation 分配 | `forge_strand` |
| async_scope | state、spawned op-state node | source sender 内部路径 | `forge_async_scope` |
| Linux IO context | state、fd map、event buffer、readiness record | fd、借用 buffer、内核对象；deferred 链内嵌不分配 | `forge_io_readiness` |
| Windows IO context | state、pending map、handle cache、IO record | HANDLE、借用 buffer、内核对象 | `forge_io_iocp` |
| 类型擦除 | 无 | any_sender/receiver 64B SBO 溢出走堆；any_scheduler state 与每次 connect 走 shared_ptr 堆；erased_sender 使用堆，无 allocator 支持 | `forge_type_erased_boundary` |

容量满或关闭后的路径在可于 `start()` 内不抛异常地判定时以 stopped 完成。分配失败通常遵循异常路径；typed-error 仅暴露稳定分类。`bounded_channel::async_send` 的 buffer 节点分配失败以 `set_error(std::exception_ptr)` 完成；strand 入队分配失败会关闭并以 stopped 完成。stop-token 擦除和 callback 注册也可能分配失败。资源策略不承诺全局零分配。

## 调度与上下文

- `forge::static_thread_pool`：固定大小线程池，提供 `scheduler`，可通过
  `std::execution::schedule(pool.get_scheduler())` 产生 sender。默认构造路径保持无界队列；
  需要有界 ingress 时可传入 `static_thread_pool_options{.queue_capacity = N}`，需要控制
  队列节点和 queued task callable record 分配时可传入 `.memory = resource`。队列满、
  shutdown 后新启动、task record 分配失败或 receiver 已停止的 schedule operation 会以
  `set_stopped` 完成。pool 和 scheduler 的 `closed()` 在 shutdown 后返回 `true`，
  单纯满队仍返回 `false`。已接受的任务会在 `shutdown()` 后继续 drain；`wait()` 会等待队列
  和正在运行的任务清空；如果从 pool 自己的 worker 线程调用，`wait()` 会立即返回以避免
  自锁。在 pool 自己的 worker 线程上析构 pool（例如 pool 内任务拥有 pool 或其外层
  `runtime_context`）不会 terminate：析构走 detach 路径，worker 持有共享 state 在
  后台排空队列后退出，与 `timer_context` 的同线程析构契约一致。代价是析构返回时
  不保证 drain 已结束，pool 使用的 memory resource 必须活过该 detached drain
  尾部，且 detach 后排空的 backlog 任务不得再触碰 pool 对象本身（pool 对象已不
  存在，只有共享 state 活着）；确定性 teardown 仍应由外层 owner 在非 worker
  线程上 `shutdown()` / `wait()` / 析构。注意 `resource_context` 不在此例外内：
  它的析构先阻塞在 scope wait 上，从自己 scope 的任务里销毁会死锁（见
  forge-runtime.md）。其 schedule sender env 会通过 Forge backport 的
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
  `schedule_after` 的延迟锚定在操作 `start()` 的时刻而非 sender 组合时刻：sender 在
  队列里滞留多久都不吞噬延迟（`with_timeout`/`async_sleep_for` 因此对延迟启动同样
  成立）。`schedule_at` 是绝对时限，组合时即换算为 steady deadline。
  `timer_context_options{.memory = resource}` 可控制 state、timer op data、timer item
  control block、timer queue 和 timer callback callable record 分配；`wait()` 会等待已
  接受 timer 操作完成。timer sender 只有 value/stopped completion；item 或 callback
  storage 分配失败也完成为 stopped，不保留 allocation exception。
  - 入队后取消使用 per-item stop callback 唤醒 worker。callback 只标记 item 并通知
    condition variable；真正的 value/stopped completion 仍由 timer worker 在线程外部锁
    之外执行，且 completion 前会先销毁 callback registration，避免 stop callback 与
    receiver completion 同时触碰同一 op data。
  - 销毁 started 且尚未完成的 timer operation state（弃置）是安全的：析构会先声明
    completion（此后 worker 永不触碰 receiver），再把仍在队列中的 item 注销并平衡
    pending 计数。若 worker 先赢得 completion 声明，析构按交付线程区分：completion
    正发生在本调用栈上（正常 continuation 销毁自身 state）时直接放行以免自锁，另一
    线程的在途交付则等 receiver completion 与 stop registration 拆除都结束后才返回。
    被弃置的 timer 不产生任何 completion，析构返回后结果不再变化。
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
  completion-scheduler roundtrip。直接以 `static_thread_pool::scheduler` 构造时，
  runner 因有界队列满而被拒绝会保留 pending FIFO，并向 pool 注册一次性容量通知。
  worker 取出 queued task、释放容量后，在 pool 锁外通知 strand 重新入队 runner；
  若容量被其他提交占用而再次拒绝，则重新注册，不轮询、不递归执行 strand work，
  也不需要后续提交来驱动恢复。runner 尾部重启被拒绝时使用相同机制。
  pool 的 `shutdown()` 会以关闭通知唤醒停驻的 strand，将 pending work 完成为 stopped；
  strand 自身 `shutdown()` 或析构会注销尚未分发的通知。`wait()` 等待恢复后的工作或
  stopped drain 完成；容量为 0 时无法恢复，须关闭 pool 或 strand。
  其他 scheduler（包括先经 `any_scheduler` 擦除的 pool scheduler）没有此通知能力，
  runner 的 stopped 拒绝仍使 strand 永久关闭；仅提供 `closed()` 查询不足以支持重试。
  真正的 pool 关闭、分配失败、error completion 或 launch 异常也会 fail closed，
  并把 pending/future work 完成为 stopped。用于
  strand 的 resource 还需覆盖 runner keepalive node 和已分发容量通知的释放尾部。
- `forge::async_scope`：拥有一组 eager-start sender work 的结构化并发 scope。
  `spawn(sender)` 在 scope open 时启动并返回 `true`，`close()` 后拒绝新任务，
  `request_stop()` 会让后续和已拥有任务的 receiver env 暴露已请求的 stop token，
  `shutdown()` 等价于 close + request stop。析构会 `shutdown()` 并 `wait()`，因此可能
  阻塞到 scope-owned work 完成或响应停止。scope 对象不得在它自己拥有的 spawned
  work body / completion callback 内调用 `wait()` 或析构；该 work 自身仍计入 active
  count，外层 owner 应负责 drain。scope 捕获第一个 error 为
  `std::exception_ptr`，可通过 `first_error()` / `rethrow_if_error()` 读取。
  `spawn` 返回 `false` 覆盖两种情况：scope 已 close，以及 op-state node 分配 /
  sender 拷贝失败；后者的异常会记录进 `first_error()`，因此两者可由
  `closed()` 与 `first_error()` 区分。

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
  `bounded_channel<Event>` 与它并排存放。`request_stop()` 会请求 scope stop，并把
  pending runtime timer 完成为 stopped；direct pool work 仍按 drain 语义完成。
  `shutdown()` 先 close/request_stop scope，再关闭 runtime；析构会 shutdown + wait，
  因此适合资源会话的安全收尾。

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

macOS/BSD kqueue 当前不在项目需求内；Linux `io_uring` 已作为独立 gate 的
coroutine-native completion backend proof 落地。详细语义见 [`forge::io`](forge-io.md)。

## 消息通道

- `forge::bounded_channel<T>`：有界 FIFO 消息通道，提供 `async_send(T)`、
  `async_recv()`、`try_send(T&&)` / `try_send(const T&)`、`try_recv()`、`close()`、
  `request_stop()` 和
  `shutdown()`。可用 `bounded_channel_options{.capacity = N, .memory = resource}` 控制
  容量和 channel 内部 state/buffer/record 分配。`T` 必须 nothrow move constructible，
  以保证 asynchronous `start()`、close 和 cancellation 路径不因值移动抛异常。send
  在值被缓冲或直接交给等待中的
  receiver 后完成 `set_value()`；recv 在收到值时完成 `set_value(T)`。`close()` 拒绝新
  send 并允许已缓冲值 drain；`request_stop()` 取消 pending send/recv 并丢弃缓冲值。
  `try_send` 被拒绝（满、closed、stopped）时不消费 rvalue 实参，调用方的对象保持
  原样可重试。

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

`forge::task` 在 coroutine `final_suspend` 中同步发出 receiver completion。自定义
receiver 在不再需要完成参数后，可以在 `set_value` / `set_error` / `set_stopped`
回调内销毁连接的 task operation-state；销毁也会释放 coroutine frame 和其中的结果，
此后不得访问 receiver 自身或指向该存储的参数引用。
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

## 取消与入口关闭

`timer_context::request_stop()` 永久关闭定时器入口；`runtime_context` 和 `resource_context` 会转发此操作，后续 timer 也以 stopped 完成。它们的 CPU pool 仍按排空语义工作。
`static_thread_pool` 和 `strand` 只在 `start()` 前检查 receiver stop token；入队后请求停止不会撤回工作。strand 的 `shutdown()` 会停止待处理工作。
直接以 `static_thread_pool::scheduler` 构造的 strand 在满队拒绝时保留 FIFO，由容量通知重试，
无需后续提交；pool 关闭会主动通知停驻的 strand，pending work 以 stopped 完成。
其他 scheduler（包括 `any_scheduler`）在 runner 被拒绝后仍使 strand 永久关闭。

## 通道容量

`bounded_channel_options::capacity` 默认 0，表示会合：send 必须交给等待中的 receiver 才完成。需要缓冲时显式指定正容量。
`capacity()` 读取容量，`closed()` 查询关闭状态。析构调用 `shutdown()`，丢弃缓冲并以 stopped 完成待处理操作，不执行阻塞等待。

## 同步结果与分离启动

`wait_result` 是 `wait_result_of` 的便利入口，返回 `wait_result_value<Value, Error>`。
`has_value()`、`has_error()`、`stopped()` 查询三种完成状态，`operator bool` 等同于 `has_value()`；`value()` / `error()` 读取对应内容，状态不符时抛异常；`error_if<E>()` 返回指针或空指针。
多个 value 形状或多个 error 类型使用 `variant`；error 集合始终包含 `std::exception_ptr`。`wait_stopped_t` 表示停止状态。
`forge::start_detached` 遇到 error completion 会在运行时终止程序；需要观察失败时应由 scope 拥有工作。
空 `any_sender_of` 的同步消费抛 `runtime_error`；空或已移动的 `forge::task` 连接时抛 `logic_error`。

## 两种协程任务

`forge::task` 面向 sender 组合，`io_task` 面向 coroutine-native byte IO，两者具有不同的取消与环境契约，这是有意的设计。`io_task` 内用 `await_sender` 消费 sender，用 `as_sender` 将任务交给 sender 消费者；因此 `forge::task` 也可 await 经该桥转换的 IO 任务。
依据 [exec.async.ops](https://eel.is/c++draft/exec.async.ops)，task operation-state 的生命周期可以在 completion operation 内结束；完成参数若仍需使用，应在销毁前转移到独立存储。


### task 环境与结果

- `forge::task` 从 coroutine `final_suspend` 发出 receiver completion。Custom receiver
  可以在不再需要完成参数后，于 `set_value` / `set_error` / `set_stopped` callback
  内销毁连接的 task operation-state；销毁后不得再访问 receiver 或 frame 内的结果。
  连接时会把 downstream receiver 的 stop token 与
  `get_start_scheduler`（若存在）擦除后放进 task promise env：task 内 `co_await`
  的 sender 能观察调用方取消，需要 start scheduler 的算法（如
  `counting_scope::join`）也能拿到与外层一致的调度器（例如 `sync_wait` 的
  run_loop）。外层 env 没有 start scheduler 时回退到一个 inline 调度器：join 这类
  completion 会停留在触发它的线程上（等价于无重调度的旧行为）。其他 receiver env
  query 不会传播。Stopped completion 通过 coroutine bridge 的内部异常返回 task
  frame，可以被用户 `catch(...)` 捕获；promise 的 stopped 状态是 sticky 的，后续异常
  不会覆盖 stopped completion。Moved-from task 不能再次连接，尝试连接会抛
  `std::logic_error`。`task<T>` 支持 throwing-move 的 `T`：completion 把已存储
  的结果按引用交给 receiver（调用表达式中不构造 `T`），物化值的 receiver（如
  `sync_wait`）在自己体内 try/catch 并把抛出路由到 error channel；`co_return`
  时 move 进 promise result variant 的异常落在 `unhandled_exception`。

## 扩展入口弃用与迁移

以下旧入口保留至少一个弃用发布版本；可独立标注的入口使用 `[[deprecated]]`，计划在后续版本移除，具体版本由维护者决定。既有调用的完成行为、异常传播和结果类型保持不变。等待入口的两个 `std::execution` 别名继续通过 using-declaration 引用 `std::this_thread` 中的同一对象，以保留实体身份和同时导入两个命名空间时的非限定查找；它们仅在文档中弃用，不单独添加属性。

| 弃用入口 | 替代入口 |
| --- | --- |
| `std::execution::split(s)` | `forge::split(s)` |
| `std::execution::empty_env` | 新代码使用 `std::execution::env<>`；需要旧类型身份时使用 `forge::empty_env` |
| `std::execution::transfer_just(sch, values...)` | `forge::transfer_just(sch, values...)` 或 `std::execution::continues_on(std::execution::just(values...), sch)` |
| `std::execution::make_prop(q, value)` | `std::execution::prop(q, value)` |
| `std::execution::sync_wait(s)` | `std::this_thread::sync_wait(s)`；依赖扩展结果形态时使用 `forge::sync_wait(s)` |
| `std::execution::sync_wait_with_variant(s)` | `std::this_thread::sync_wait_with_variant(s)` |
| `std::this_thread::sync_wait(s)` 接受多种 value completion 的扩展 | `forge::sync_wait(s)` 保持原 tuple-or-variant 结果；标准替代为 `std::this_thread::sync_wait_with_variant(s)`，其结果形态不同 |

Forge 入口由 `<forge/execution_extensions.hpp>` 提供，也由 `<forge/execution.hpp>` 汇总。`forge::split` 与原入口共用缓存实现，保持惰性启动、单次执行和 `const&` 重放语义。它依赖 backport 的共享状态，仅在 `FORGE_EXECUTION_BACKPORT_ACTIVE` 已定义时提供；原生 execution 让位时不注入此算法。

`forge::transfer_just` 仅组合标准 `just` 与 `continues_on`，完整原生实现下也可用。`forge::empty_env` 在 backport 下保留旧类型身份，在完整原生实现下等同于 `std::execution::env<>`。`forge::sync_wait` 在 backport 下保留旧的 tuple-or-variant 行为，在完整原生实现下转用标准单一 value completion 的 `std::this_thread::sync_wait`；跨实现消费多种 value completion 应使用标准 `sync_wait_with_variant`。零种 value completion 的既有扩展暂时保留，本次未增加弃用诊断。

依据为 2026-11-13 查阅的 [execution synopsis](https://eel.is/c++draft/execution.syn)、[env](https://eel.is/c++draft/exec.env)、[prop](https://eel.is/c++draft/exec.prop) 与 [sync_wait](https://eel.is/c++draft/exec.sync.wait)。

## 扩展重载弃用与迁移

以下旧入口保留至少一个带 `[[deprecated]]` 的发布版本；计划在后续版本移除，具体版本由维护者决定。旧入口的计算结果保持不变。

| 弃用入口 | 替代入口 |
| --- | --- |
| `hermitian_matrix_rank_1_update(t, x, A)` | `hermitian_matrix_rank_1_update(Scalar{1}, x, A, t)` |
| `hermitian_matrix_rank_2_update(t, x, y, A)` | `hermitian_matrix_rank_2_update(x, y, A, t)` |
| `hermitian_matrix_product(t, A, B, C)` | `hermitian_matrix_product(A, t, B, C)` |
| `hermitian_matrix_rank_k_update(t, A, C)` | `hermitian_matrix_rank_k_update(Scalar{1}, A, C, t)` |
| `std::linalg::vector_two_norm(v[, init])` 的整数元素或整数 `Scalar` 实例化 | `<forge/linalg.hpp>` 中的 `forge::vector_two_norm(v[, init])`，或显式转换为符合草案的浮点/复数输入及 `Scalar` |

上表矩阵算法位于 `std::linalg`；`Scalar{1}` 应采用原输出元素对应的标量类型。浮点和复数的标准范数调用不产生弃用诊断。`forge::vector_two_norm` 共用原范数实现，整数结果仍向零截断并饱和；原生 linalg 让位时转用标准入口，整数扩展仅在 `FORGE_LINALG_BACKPORT_ACTIVE` 已定义时受支持。`matrix_frob_norm` 与 `vector_sum_of_squares` 的整数支持不在此次弃用范围内。

依据为 2026-11-13 查阅的 [linalg synopsis](https://eel.is/c++draft/linalg.syn) 与 [vector_two_norm Mandates](https://eel.is/c++draft/linalg.algs.blas1.nrm2)。整数实例化超出当前草案要求；[LWG 4315](https://cplusplus.github.io/LWG/issue4315) 仅作为相关问题线索，不将未来可能的修改视为当前标准保证。

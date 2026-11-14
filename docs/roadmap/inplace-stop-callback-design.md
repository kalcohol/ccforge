# `inplace_stop_callback` 无分配设计

`std::inplace_stop_callback` 遵守标准 source lifetime，以 callback object
内联的 node 注册到 source 的 intrusive list。不再为 registration 分配 control block。
constructor 的异常规格为 `noexcept(is_nothrow_constructible_v<CallbackFn, Initializer>)`，
只有 callback function 的初始化可以抛出异常。用户 callback 抛出异常时调用 `terminate`，
包括已停止 token 上的立即调用。

语义依据为工作草案的
[`[stoptoken.inplace]`](https://eel.is/c++draft/stoptoken.inplace)、
[`[stopsource.inplace]`](https://eel.is/c++draft/stopsource.inplace)、
[`[stopcallback.inplace]`](https://eel.is/c++draft/stopcallback.inplace) 和
[`[stoptoken.concepts]`](https://eel.is/c++draft/stoptoken.concepts)。
本设计由这些契约及下述 lifetime 不变量推导；不依赖其他库的实现代码。

## 存储与状态

source 的 `state_` 是显式按 word 对齐的 `atomic<uint32_t>`，最低两位分别为
`kStopRequested` 和 `kLocked`，其余位为零。`callbacks_` 指向 pending list 的头。
stop bit 只从零变为一，lock bit 可反复取得和释放。source 不可复制或移动。

每个 callback node 保存 immutable `invoke_fn`、`next`、指向前一个链接的 `prev`、
`request_thread`、`removed` 和 word 对齐的 `atomic<uint32_t> completion`。
`prev != nullptr` 表示 pending，可以在常数时间摘链；`removed != nullptr` 表示
requester 已选中该 node。`removed` 指向该次 invocation 的 requester stack 上的 bool。
invocation 正常返回后，`removed` 清空且 `completion` 从零变为一。
立即调用与 disengaged token 不加入 list，callback 的 `source_` 为 null。

`CallbackFn` 完整构造后才发布 node。所有 list 与执行状态的修改由 source lock 保护；
`invoke_fn` 发布后不变，可以在锁外读取。`completion` 的等待在锁外进行。
调用线程必须保证正在构造的 callback storage 有效；构造过程立即调用 callback 时，
实现会在调用前解除 source 关联，调用后不再访问 callback storage。

## 注册、请求与注销

注册先取得 source lock。尚未停止时插入 list 头部并解锁；否则解锁，在构造线程通过
`noexcept` thunk 立即调用 callback。即使原 requester 正阻塞于另一个 callback，
新的 registration 也不等待该 invocation。立即 invocation 不再加入 list。

首个 `request_stop()` 在锁内以 release RMW 设置 stop bit。后续请求返回 false；
只有赢家执行 pending list。赢家每次只摘下一个 node，设置 stack flag pointer 和
requesting thread id，然后解锁并调用。它不保留 sibling 指针或 detached chain。
返回后重新取得 source lock，先检查 stack flag：

- flag 为 true：callback 已在当前线程析构，不再读取 node 的任何成员；
- flag 为 false：node 仍有效，清空 `removed`，以 `seq_cst` store 写入 `completion = 1`，
  在锁内 `notify_all()`，然后继续下一次选择。

所有用户 invocation 都在锁外。pending sibling destructor 取得锁即可摘链，既不会被
调用，也不必等待正在执行的其他 callback。嵌套 registration 立即调用；嵌套
`request_stop()` 返回 false。

node destructor 在锁内分三种情况：pending node 直接摘链；已完成 node 直接返回；
正在执行的 node 比较 thread id。同线程只把 requester stack flag 写为 true，解锁返回。
另一线程则解锁，以 acquire load / `wait(0, acquire)` 等待自身的 `completion`。
观察到完成后仍须重新取得并释放 source lock，才可销毁 callback function 与 node。
这次锁交接保证 requester 的 `notify_all()` 已经结束：仅观察完成值不足以保证 atomic
storage 可以释放，因为 store 与 notify 是两个独立操作。

等待只依赖自己的 invocation。source lock 的持有区间只包含短暂的 list/state 操作与
notification，不包含用户 callback，也不包含等待 invocation 的操作。
source lock 竞争使用 atomic wait，长 invocation 的等待也使用 atomic wait，不做无限忙等。

## 内存顺序与不变量

| 操作 | memory order 与理由 |
| --- | --- |
| source lock 初始 load、CAS failure、lock wait 与重试 load | `relaxed`；只决定重试，不读取受保护数据 |
| lock CAS success | `acquire`；承接前一个 owner 的 release unlock，发布注册、摘链及执行状态 |
| unlock `fetch_and` | `release`；发布受保护写入，保留 stop bit；随后 notify 唤醒竞争者 |
| 设置 stop bit 的 `fetch_or` | `release`；查询可观察请求之前的写入；赢家判断和设置在同一临界区 |
| `stop_requested()` 及严格 source probe | `acquire`；与 stop 发布同步，查询保持单调 |
| `completion.store(1)` | `seq_cst`；发布 invocation 的写入，并封闭 completion 与 notify 内部 waiter count 之间的 store-buffer 模式，防止 plain load 漏看 waiter 而跳过唤醒 |
| `completion.load` / `completion.wait` | `acquire`；保证 invocation 返回强先发生于另一线程的 callback function 析构 |
| `notify_all()` | 不承担 publication；相应 atomic 的值与 acquire/release 负责同步 |

pending node 恰好属于一个 list；摘链后没有 list traversal 保存它。选中 node 的执行权只属于
首个 requester，因此每个未注销 invocation 至多调用一次。其他线程的 destructor 在完成
之前不能释放执行中的 node；同线程析构后 requester 只使用 stack flag。未自毁时的最终
node 访问和 notify 受 source lock 保护，等待方的最后一次锁交接覆盖 storage reclamation。
completion 只从零变为一，没有 node 重注册或 completion ABA；lock word 可循环，但等待者
只需等到能重新竞争锁，lock 的 acquire CAS 才是访问 list 的授权。

completion 的 `seq_cst` store 还保证唤醒协议的顺序：若 notify 以 plain load 查询 waiter
count，不能让通知方漏看已经登记的 waiter，同时等待方仍读取旧 completion 并进入睡眠。
仅有 release store 不足以排除这类 store-buffer 交错；最终 source lock 交接仍负责
notify 结束前的 node storage 保护，不能由更强的 completion store 替代。

未来可在 source state word 中增加 has-waiters bit，由 lock 等待方在 parking 前设置，
让 unlock 仅在可能存在等待者时 notify。它可减少无竞争路径上的通知及标准库内部共享表的
原子操作，但增加 waiter bit 的 RMW、清除与重新检查协议及缓存竞争成本；这只是性能取舍，
不影响正确性。当前 unlock 保持无条件通知。

source 必须活过其全部 associated registrations 及正在执行的 `request_stop()`，包括
callback 返回后的 source 访问。source destructor 假定此前置条件已满足；不提供 source
在自己的请求栈上同步销毁的扩展。token equality 比较 source identity；两个 disengaged
token 相等，`stop_possible()` 只检查 source pointer，`stop_requested()` 查询关联 source。

## 七项门槛

1. 条件 `noexcept`：构造只初始化内联成员并注册；static assertions 覆盖 nothrow callback、
   reference callback 与 throwing initializer，runtime test 验证原初始化异常传播。
2. registration 零分配：已移除 shared control block、mutex 和 condition variable。
   allocation probe 对单次与重复注册断言零，分别覆盖停止之前和之后，并拒绝所有 allocation。
   operation-state allocation suites 的 calibration 也必须为零。
3. 同线程自毁不等待；另一线程等待自身 invocation：stack flag 与 completion/lock handshake
   分别处理。semaphore 固定 blocking callback，普通写入验证 callback 与析构之间的同步。
4. pending sibling 可销毁：只摘当前 node，其他 node 仍在受锁保护的 list 内。
   tests 覆盖不同 list 位置的删除，以及另一个 invocation 阻塞时的跨线程 pending 注销。
5. 并发 register/request/destroy：`ConcurrentRegisterRequestDestroyStress` 使用常驻的四个
   worker，两个注册注销、两个请求停止；2,000 轮，每轮 24 次短生命周期注册，检查唯一
   请求赢家、单次 invocation 和持续注册的 witness。此 suite 不使用 allocation replacement，
   可直接在 TSan 下运行及重复。
6. 既有 operation 自毁 tests 全部保留，普通与严格 lifetime suites 继续覆盖
   when_all、counting scope、spawn_future、erased token、timer、channel 和 IO。
7. source lifetime 的具体保护保持不变；严格 source probe 与 ASan negative control 保留。
   callback/node 自毁允许，source 自毁违反前置条件，两者不可混同。

## source lifetime 清点

| 表面 | lifetime 保护及边界 |
| --- | --- |
| `when_all` | outer stop callback 在 children 尚未结束时取得 completion credit，内部 stop 返回后归还；child error/stopped 未归还的 pending credit 保护 sibling stop。outer registration 在 receiver completion 前注销。 |
| `counting_scope::token::wrap` / stop-when | token 保存两个输入 token；child callback 直接注册到两者，用 atomic exchange 保证 `Fn` 至多调用一次。operation 不拥有融合 source。输入 source owner 必须覆盖注册与 stop 调用。 |
| `counting_scope` | scope owner 必须覆盖 `request_stop()`，包括使 join receiver 同步完成的调用。 |
| `spawn_future` | producer stop 调用持有 shared-state owner；consumer weak lock、delivery/abandon 的局部 owner 继续保护 source。consumer registration 在 completion 前注销；producer child 的 registration 由含 source 的 shared state 覆盖至 child 析构。 |
| `any_stop_token` | invoker 先复制 shared callable owner，调用返回前保持有效；borrowed inplace source 仍由 caller 保证。类型擦除自己的 allocation 保持原设计。 |
| `timer_context` | callback 标记并唤醒 worker；shared item/operation data 保持有效，worker completion 前注销 callback。caller 保证 source。 |
| `channel` | callback 先取得 record/state owner，可同步移除自身 registration 并完成 receiver；caller 保证 source。 |
| `io::as_sender(io_task)` | 每次 fused source stop propagation 取得 completion credit；最后一个 credit 归还才交付原 frame 中的结果。先注销输入 callback；receiver token 查询结果移除 cv/ref 后注册。 |
| `io::when_all_results` / `when_any_results` / `with_timeout` | child receiver 与 downstream forwarding callback 持有 shared-state owner，覆盖 sibling stop；选出 terminal completion 后在 state mutex 外注销 downstream registration，再调用 receiver。with_timeout 复用 when_any_results。 |
| Linux epoll | callback 持有 record/state，可同步 stopped completion 和 operation 自毁；所有 terminal channel 都先注销 registration，再调用 receiver。caller 保证正在执行的 stop 调用。 |
| Linux io_uring | callback 在 context mutex 下排队 cancel；await_resume 先注销 registration，再向 coroutine 返回结果或抛出 stopped。context、fd、buffer、source 维持 borrowed lifetime。 |
| Windows IOCP | callback 请求 CancelIoEx；completion packet 决定终态；所有 terminal channel 都先注销 registration，再调用 receiver。caller 保证正在执行的 stop 调用。 |
| `async_scope` | shared state 持有 source；owner 不在自身 work/completion 中析构或 wait，并覆盖 stop 调用。 |
| `wait_result` | stack state 持有 source；connected operation 先于 state 析构，没有对外 request 接口。 |
| `strand` | 仅在 start 时查询 receiver token，不保存 stop registration；completion 后不再查询该 token。 |

completion 允许 owner 释放 receiver 的 borrowed source。因而即使 operation 或 shared record
仍存活，registration 也必须先注销；保留 record 的 shared owner 不能替代此顺序。
source owner 仍须另外覆盖所有尚未返回的 `request_stop()`，包括返回 false 的请求。
`async_scope` 的 start reference 可使最后一次 operation 析构发生在调用 `spawn()` 的线程；
cleanup 的等待不能依赖 `spawn()` 返回后才会发出的信号。

stop-when 按 [`[exec.stop.when]`](https://eel.is/c++draft/exec.stop.when) 直接转发原 completion
channel 和参数引用，不复制 packet、不增加 move、不合成 stopped。`callback_type<Fn>` 按
`Fn`、atomic flag、两个输入 registration 的顺序构造，逆序析构；逻辑 token 已停止时先在
构造线程调用 trampoline，再发布 registration，防止另一输入抢先。两个输入同时停止、
同源或在两次 registration 之间停止，exchange 都只允许一次 `Fn` invocation。
trampoline 调用后不再访问 storage；输掉 exchange 的 invocation 直接返回，不等待赢家。
析构先注销两者，等待其他线程 invocation，然后销毁 flag 和 `Fn`。
child 负责 registration lifetime 及 completion 与 operation 析构的协调。
每个 child callback 对应两个输入 registration；输入为 inplace token 时二者现在均零分配。

`when_all` 和 `io_task` 的 credit 延后原交付路径，使用已有 result storage；
spawn_future、erased callable 与 IO aggregate 的保护只复制已有 shared owner。
`any_stop_token.hpp` 和 IO 不依赖已删除的 control-block internals，无需改变公开行为。

## 严格测试模式与验证表面

`FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME` 仍只由专用 test executable 的 PRIVATE
定义开启。每次 invocation 返回都先 volatile 读取 source atomic，即使 callback 已自毁；
取得 source lock 后，仅当 stack flag 为 false 时 volatile 读取 node 的 `invoke_fn`。
此时跨线程 destructor 尚不能释放 node，已自毁 node 完全不访问。
source destructor 以 release store 将 state 写为专用 poison bit，lock 与 query 检查该 bit，
对仍可寻址 storage 上的延迟注销直接终止；ASan 继续检查已经释放的 storage。
这个 probe 不延长 lifetime，也不能区分同一地址重新构造后的 source identity。
宏未定义时不增加成员或 probe。生产路径本身也需要重新访问 source。

`FORGE_EXECUTION_TEST_STOP_HOOKS` 仅在 primitive test target 中定义，thread-local observer
在跨线程 executing-node 注销分支解锁后通知测试，测试收到通知才释放 invocation。
`DetectsDeregistrationAfterSourceDestruction` 覆盖 pending 和 completed registration 的
poison 检查；completion probe 在 receiver 内销毁 source，保留 operation 至 completion
之后，覆盖 execution adaptor、IO coroutine/combinator、timer、channel、backend 和 strand。

`StopCallbackLifetimeDeathTest.ProbeDetectsSourceDestroyedInsideRequest` 在 ASan 严格模式下
故意销毁当前 source，要求报告 heap-use-after-free，验证 source check 不会被删除。
`execution_stop_intrusive` 是新增的独立 CTest entry；相同 source 也编入
`execution_stop_callback_strict_lifetime`。`execution_stop_callback_allocations` 保留独立
allocation replacement executable；它在 TSan 下以 77 跳过，其余 primitive/stress tests
仍运行。`forge_stop_callback_strict_lifetime_*` 继续重建 runtime、coroutine 和可用 backend。

既有 proof surface 包括 `CountingFusionSameThreadReceiverDestroysOperation`、
`CountingFusionWaitsForOtherThreadAndForwardsOriginalReference`、
`WhenAllResultsAllowsSelfDestroyDuringSiblingStop`、`SiblingStopFinishesBeforeCrossThreadDelivery`、
`InlineTaskCompletionAllowsReceiverSelfDestruction`、
`EarlyCrossThreadCompletionAllowsReceiverSelfDestruction`、
`WrapCompletionCanDestroyOperationAfterDeregistration`、
`SynchronousConsumerCompletionCanDestroyOperation`，以及 timer、channel、epoll、IOCP 的
operation 自毁和 strict heap regressions。它们与全部 stop-token suites 一并保留。

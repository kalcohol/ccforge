# `inplace_stop_callback` 无分配设计

> 状态：active；最后核对：2026-11-14；取代关系：无。

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

source 的 `state_` 是显式按 word 对齐的 `atomic<uint32_t>`。bit 0 为 `kStopRequested`，
bit 1 为 `kLocked`，bit 2 为 `kLockWaiters`，其余位为零；严格测试模式另以 bit 31 作
tombstone。`callbacks_` 指向 pending list 的头。stop bit 只从零变为一，lock bit 可反复
取得和释放。`kLockWaiters` 表示可能有线程在 `lock()` 中等待当前持锁区间结束：它只在
`kLocked` 置位时出现，只由竞争者在已加锁的 word 上以 CAS 设置，只由 unlock 与 `kLocked`
一起清除。持锁期间，其他线程对 `state_` 的唯一写入就是这次 CAS；新增的 `state_` 写入方
必须保持这一性质。source 不可复制或移动。

每个 callback node 保存 immutable `invoke_fn`、`next`、指向前一个链接的 `prev`、
`request_thread`、`status` 和 word 对齐的 `atomic<uint32_t> completion`。
`prev != nullptr` 表示 pending，可以在常数时间摘链；`status != nullptr` 表示 requester
已选中该 node 且 invocation 尚未完成。`status` 指向该次 invocation 在 requester stack 上的
`invocation_status`，取值为 `running`、`destroyed_on_requester` 或 `awaited`，只在
source lock 下读写。invocation 返回后 `status` 清空；只有另一线程的 destructor 已登记
`awaited` 时，`completion` 才从零变为一。立即调用与 disengaged token 不加入 list，
callback 的 `source_` 为 null。

`CallbackFn` 完整构造后才发布 node。所有 list 与执行状态的修改由 source lock 保护；
`invoke_fn` 发布后不变，可以在锁外读取。`completion` 的等待在锁外进行。
调用线程必须保证正在构造的 callback storage 有效；构造过程立即调用 callback 时，
实现会在调用前解除 source 关联，调用后不再访问 callback storage。

## 注册、请求与注销

注册先取得 source lock。尚未停止时插入 list 头部并解锁；否则解锁，在构造线程通过
`noexcept` thunk 立即调用 callback。即使原 requester 正阻塞于另一个 callback，
新的 registration 也不等待该 invocation。立即 invocation 不再加入 list。

首个 `request_stop()` 在锁内以 release RMW 设置 stop bit。后续请求返回 false；
只有赢家执行 pending list。赢家每次只摘下一个 node，在 stack 上建立初值为 `running` 的
`invocation_status`，设置指向它的 `status` 和 requesting thread id，然后解锁并调用。
它不保留 sibling 指针或 detached chain。返回后重新取得 source lock，检查该状态：

- `destroyed_on_requester`：callback 已在当前线程析构，不再读取 node 的任何成员；
- `running`：没有线程等待该 invocation，清空 `status`，不写 `completion`，也不 notify；
- `awaited`：另一线程的 destructor 正在等待，清空 `status`，以 `seq_cst` store 写入
  `completion = 1`，在锁内 `notify_all()`。

随后继续下一次选择。所有用户 invocation 都在锁外。pending sibling destructor 取得锁
即可摘链，既不会被调用，也不必等待正在执行的其他 callback。嵌套 registration 立即调用；
嵌套 `request_stop()` 返回 false。

node destructor 在锁内分四种情况：pending node 直接摘链；`status` 为 null 的已完成 node
直接返回，不读取 `completion`；正在执行的 node 比较 thread id。同线程只把 requester
stack 上的状态写为 `destroyed_on_requester`，解锁返回。另一线程则在同一临界区内把状态
写为 `awaited`，解锁后以 acquire load / `wait(0, acquire)` 等待自身的 `completion`。
观察到完成后仍须重新取得并释放 source lock，才可销毁 callback function 与 node。
这次锁交接保证 requester 的 `notify_all()` 已经结束：仅观察完成值不足以保证 atomic
storage 可以释放，因为 store 与 notify 是两个独立操作。

completion 的发布由锁内状态精确决定。destructor 只在看到 `status != nullptr` 的临界区内
写入 `awaited`；requester 清空 `status` 的临界区在锁序上晚于它，因此必然读到 `awaited`。
晚于该临界区的 destructor 看到 `status == nullptr`，不会等待。没有 destructor 等待时，
`request_stop()` 既不写 `completion`，也不调用其 `notify_all()`。

等待只依赖自己的 invocation。source lock 的持有区间只包含短暂的 list/state 操作与
notification，不包含用户 callback，也不包含等待 invocation 的操作。长 invocation 的
等待使用 atomic wait，不做无限忙等。

### source lock 的等待与唤醒

`lock()` 发现 `kLocked` 已置位时，先最多 `yield` 四次并重新读取 word；持锁区间只有几次
写入，多数竞争在此期间结束，无需设置 waiter bit。仍被占用时，竞争者以 relaxed CAS 在刚
观察到的已加锁 word 上设置 `kLockWaiters`（已置位则跳过），然后在同时含 `kLocked` 与
`kLockWaiters` 的值上 `wait`。CAS 失败时重新判断，不在未设置 waiter bit 的值上等待。

unlock 以一次 `exchange` 写入持有者已知的 word，即 `lock()` 的返回值，若本临界区设置了
stop bit 再加 `kStopRequested`。由于持锁期间其他线程只能设置 waiter bit，这次 `exchange`
等价于 `fetch_and(~(kLocked | kLockWaiters))`，却不需要 load 或 CAS 循环。只有返回的旧值
含 `kLockWaiters` 时，unlock 才在释放之后调用 `notify_all()`。必须唤醒全部等待者：unlock
已为所有等待者清除了 waiter bit，抢锁失败者重新设置后再等待；`notify_one` 会让其余等待者
在已清除的位上永久阻塞。

不丢唤醒：等待者只在含 `kLocked | kLockWaiters` 的值上阻塞。该值属于某个持锁区间，而
持锁区间内 waiter bit 不会被清除；结束该区间的 unlock 是 RMW，必然读到 waiter bit，并在
其后调用 `notify_all()`。按 [`[atomics.wait]`](https://eel.is/c++draft/atomics.wait)，在该
unlock 之前观察到这个值而阻塞的 wait 都有资格被这次 notify 解除阻塞。被唤醒者重新竞争，
锁又被占用时重新设置 waiter bit。值的 ABA 不影响这一论证：等待者观察到哪个持锁区间的值，
就由那个区间的 unlock 负责唤醒。没有线程在持锁时阻塞，因此不会死锁。

唤醒延迟：负责唤醒的是读到 waiter bit 的那次 unlock。该线程若在 RMW 与 notify 之间被
抢占，锁可能已被其他线程取得并释放，而这些 unlock 读不到 waiter bit，不会 notify；等待者
要到该线程恢复运行才被唤醒。这是延迟而不是丢失唤醒，上界是该线程被抢占的时长。条件通知
同时去掉了“后续任意 unlock 都会顺带唤醒”的冗余：标准库内部若漏掉一次唤醒，等待者将永久
阻塞。下文对标准库的假设排除这种情况。

公平性：source lock 不保证公平，刚释放锁的线程可以在被唤醒者之前再次取得锁。临界区很短、
多个线程连续竞争同一个 source 时，各线程完成的操作数可能明显不均；被唤醒者会继续竞争，
不会丢失唤醒，但完成时间没有上界。标准不要求 `inplace_stop_source` 的同步是公平的。

## 内存顺序与不变量

| 操作 | memory order 与理由 |
| --- | --- |
| source lock 初始 load、退让后的重读、CAS failure、lock wait 及其后的重读 | `relaxed`；只决定重试，不读取受保护数据 |
| lock CAS success | `acquire`；不含 `kLocked` 的值只由 unlock 写入，CAS 直接读取前一个 owner 的 unlock 并与之同步，发布注册、摘链及执行状态 |
| 设置 `kLockWaiters` 的 CAS | `relaxed`（success 与 failure）；不发布数据。它位于当前持锁区间之内，结束该区间的 unlock RMW 必然读到它；作为 RMW 也不打断 stop bit 的 release sequence |
| unlock `exchange` | `seq_cst`；release 部分发布受保护写入并保留 stop bit；返回值精确决定是否 notify；seq_cst 还使这次写入先于标准库 notify 内部对 waiter count 的检查 |
| unlock 的 `notify_all()` | 仅在旧值含 `kLockWaiters` 时调用，位于释放之后；不承担 publication，只访问 source 的 `state_` |
| 设置 stop bit 的 `fetch_or` | `release`；查询可观察请求之前的写入；赢家判断和设置在同一临界区。必须保持 RMW，以保留竞争者同时设置的 waiter bit |
| `stop_requested()` 及严格 source probe | `acquire`；与 stop 发布同步。其后对 `state_` 的写入都是 RMW，release sequence 不中断，查询保持单调 |
| `status` 指针、`invocation_status`、`request_thread` 与链表 | 普通访问，由 source lock 保护 |
| `completion.store(1)`（仅 `awaited`） | `seq_cst`；发布 invocation 的写入，并封闭 completion 与 notify 内部 waiter count 之间的 store-buffer 模式 |
| `completion.load` / `completion.wait` | `acquire`；保证 invocation 返回强先发生于另一线程的 callback function 析构 |
| `completion.notify_all()`（仅 `awaited`） | 不承担 publication；在锁内调用，等待方的最后一次锁交接覆盖 storage reclamation |
| 严格测试模式 destructor 的 `exchange(kDestroyed)` | `release`；写入 tombstone；旧值含 waiter bit 时 notify，使违反前置条件而阻塞的线程在 lifetime 检查中终止 |

pending node 恰好属于一个 list；摘链后没有 list traversal 保存它。选中 node 的执行权只属于
首个 requester，因此每个未注销 invocation 至多调用一次。其他线程的 destructor 在完成
之前不能释放执行中的 node；同线程析构后 requester 只使用 stack 上的状态。未自毁时的最终
node 访问和 completion notify 受 source lock 保护，等待方的最后一次锁交接覆盖 storage
reclamation。unlock 在释放之后的 notify 只访问 source 自身的 `state_`，由 source lifetime
前置条件覆盖。completion 只从零变为一，没有 node 重注册或 completion ABA；lock word 可循环，
但等待者只需等到能重新竞争锁，lock 的 acquire CAS 才是访问 list 的授权。

completion 的 `seq_cst` store 还保证唤醒协议的顺序：若 notify 以 plain load 查询 waiter
count，不能让通知方漏看已经登记的 waiter，同时等待方仍读取旧 completion 并进入睡眠。
仅有 release store 不足以排除这类 store-buffer 交错；最终 source lock 交接仍负责
notify 结束前的 node storage 保护，不能由更强的 completion store 替代。unlock 的
`exchange` 取 `seq_cst` 出于同一理由。协议不使用 `atomic_thread_fence`。

### 对标准库 wait/notify 的假设

条件通知要求：每次实际发出的 notify 之前的写入（unlock 的 `seq_cst` exchange、completion 的
`seq_cst` store）先于标准库在 notify 中决定是否唤醒的检查。

- libstdc++：notify 以 seq_cst load 读取共享表中的 waiter count，计数为零时不唤醒；waiter 先以
  seq_cst RMW 增加计数，再由 futex 比较值。双方都是 seq_cst，store-buffer 交错被排除。
- libc++：`atomic<uint32_t>` 使用全局表路径，notify 先对表项的 platform state 做 RMW，再读取
  contention state，waiter 以 platform state 为监视值，不依赖调用方的写入顺序；按类型选择的
  native 路径与 libstdc++ 相同。处于轮询或 yield 阶段、尚未进入睡眠的等待者已经设置 waiter bit，
  unlock 仍会为它发出 notify。
- MSVC STL：notify 直接调用 `WakeByAddressAll`，是否需要唤醒由系统内部决定且未公开；`seq_cst`
  exchange 在 x64 为 `xchg`，在 ARM64 带完整屏障，调用前的写入已全局可见。

若某个库以 relaxed load 检查 waiter count 且不加屏障，这一假设不成立。条件通知的测试随
execution 测试组在所有测试平台运行；AArch64 上的 libstdc++ 与 MSVC ARM64 不在测试平台之列，
只有上述静态论证。

## 七项门槛

1. 条件 `noexcept`：构造只初始化内联成员并注册；static assertions 覆盖 nothrow callback、
   reference callback 与 throwing initializer，runtime test 验证原初始化异常传播。
2. registration 零分配：已移除 shared control block、mutex 和 condition variable。
   allocation probe 对单次与重复注册断言零，分别覆盖停止之前和之后，并拒绝所有 allocation。
   operation-state allocation suites 的 calibration 也必须为零。
3. 同线程自毁不等待；另一线程等待自身 invocation：stack 上的 invocation 状态与
   completion/lock handshake 分别处理，只有登记 `awaited` 时才发布 completion。
   semaphore 固定 blocking callback，普通写入验证 callback 与析构之间的同步。
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
取得 source lock 后，仅当 stack 上的状态不是 `destroyed_on_requester` 时 volatile 读取
node 的 `invoke_fn`；`awaited` 分支在 completion notify 之后再读取一次，最终锁交接缺失时
由 ASan 报告 heap-use-after-free。此时跨线程 destructor 尚不能释放 node，已自毁 node
完全不访问。source destructor 以 release exchange 将 state 写为专用 poison bit，lock、
unlock 与 query 检查该 bit，对仍可寻址 storage 上的延迟访问直接终止；旧值含 waiter bit
时 destructor 还会 notify，使阻塞在 `lock()` 中的线程醒来并终止，而不是永久睡眠。ASan
继续检查已经释放的 storage。这个 probe 不延长 lifetime，也不能区分同一地址重新构造后的
source identity。宏未定义时不增加成员或 probe。生产路径本身也需要重新访问 source。

在严格模式或定义 `FORGE_EXECUTION_TEST_STOP_HOOKS` 的构建中，unlock 检查 exchange 返回的
旧值：去掉 waiter bit 后必须等于持有者已知的 word 加 `kLocked`，否则输出
`inplace stop source lock word changed while held` 并终止。该检查覆盖 primitive test
target、`execution_stop_callback_strict_lifetime` 和全部
`forge_stop_callback_strict_lifetime_*`；生产构建与非严格 forge 套件不做检查。

`FORGE_EXECUTION_TEST_STOP_HOOKS` 仅在 primitive test target 中定义。thread-local
`deregister_wait_hook` 在跨线程 executing-node 注销分支登记 `awaited` 并解锁后通知测试，
测试收到通知才释放 invocation。thread-local `event_hook(void*, stop_site)` 在每次 unlock
释放之前以所在位置调用，此时仍持锁，observer 可以阻塞以保持持锁，但不得再进入同一
source；`lock()` 设置 waiter bit 之后、`wait` 之前以 `lock_park` 调用；unlock 需要唤醒时
以 `lock_notify` 调用；`awaited` 分支在 store 与 notify 之间以 `completion_published` 调用。

`execution_stop_wait` 与 `execution_stop_wait_strict` 借助这些 hooks 在每个 unlock 位置
持锁，使竞争者真正在 `lock()` 中阻塞，再检查一次释放后全部竞争者完成；Linux 上通过
`/proc/self/task/<tid>/syscall` 确认线程阻塞于 futex，其他平台只断言进度。它们还覆盖
重新设置 waiter bit 的接力唤醒、completion 等待、晚到 destructor 既不等待也不触发
notify、最终锁交接阻塞到 requester unlock、destructor 在 notify 返回前观察到 completion、
无竞争路径不发出任何 notify，以及 oversubscription 与持锁注入下的压力。每个测试带
watchdog，丢失唤醒时以退出码 124 失败。严格目标另以 death test 检查持锁期间销毁 source
时 unlock 终止，以及 destructor 唤醒阻塞的竞争者后由其终止。无竞争路径的配对测量方法见
[测量说明](../../test/execution/benchmark/MEASUREMENT_PROTOCOL.txt)。

`DetectsDeregistrationAfterSourceDestruction` 覆盖 pending 和 completed registration 的
poison 检查；completion probe 在 receiver 内销毁 source，保留 operation 至 completion
之后，覆盖 execution adaptor、IO coroutine/combinator、timer、channel、backend 和 strand。

`StopCallbackLifetimeDeathTest.ProbeDetectsSourceDestroyedInsideRequest` 在 ASan 严格模式下
故意销毁当前 source，要求报告 heap-use-after-free，验证 source check 不会被删除。
`execution_stop_intrusive` 是独立的 CTest entry；相同 source 也编入
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

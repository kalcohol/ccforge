# `inplace_stop_callback` 无分配设计记录

> 状态：active；最后核对：2026-11-14；取代关系：无。


这份记录说明 Forge execution backport 的 `std::inplace_stop_callback` 为什么暂时保留
per-registration control-block allocation，以及未来移除该分配必须同时证明哪些不变量。
它记录的是一个明确的 conformance residual，不把当前实现描述成 current-WD complete。

## 2026-08-23 决策复核

本轮对照 2026-08-22 working-draft snapshot (`2f5924ff8ee5`) 的
`[stopcallback.inplace.cons]`，constructor 仍要求
`noexcept(is_nothrow_constructible_v<CallbackFn, Initializer>)`。同时复核 NVIDIA stdexec
snapshot `f91f63636f24`：其实现仍把 callback node 内联在 callback object 中，并提供该条件
`noexcept`。

Forge 当前实现仍有下述两项偏差：registration 分配独立 control block，且 nothrow callback
不能使 constructor 成为 `noexcept`。本轮没有发现新的 correctness bug，也没有测得 consumer
要求 registration 零分配；相反，现有 self-destroy / sibling destroy / concurrent deregistration
proof surface 继续依赖 detached control-block lifetime。因此维持方向 C，不启动 intrusive rewrite。
重新开启条件仍以本文“后续实现门槛”为准，不能只为匹配 exception specification 而删除已证明的
重入行为。

## 当前偏差

Current WD `[stopcallback.inplace.cons]` 要求 callback constructor 的异常规格等于
`is_nothrow_constructible_v<CallbackFn, Initializer>`，并且 callback registration 只能抛出
callback 自身构造产生的异常。当前 Forge implementation 为每个 registration 调用
`make_shared<callback_control>()`，因此：

- 即使 callback 是 nothrow constructible，constructor 仍可能抛 `bad_alloc`；
- 每次 registration 都产生一次 allocator-neutral heap allocation；
- constructor 不能声明 WD 要求的条件 `noexcept`。

这是真实偏差，不应与 `forge::any_stop_token` 类型擦除所接受的 allocator-neutral allocation
混为一谈。

## 为什么当前 control block 存在

`request_stop()` 会先在 source lock 下 detach 全部 callback，再在锁外调用用户代码。
独立 control block 在 callback object 和 source object 之外保存：

- callback 是否已经 detached、cancelled 或正在执行；
- 发起 stop 的 thread id；
- concurrent deregistration 等待当前 callback 返回所需的 mutex / condition variable；
- detached callback chain 的所有权。

这使当前 backport 还能承受一个超出标准 lifetime precondition 的 Forge 路径：

```text
request_stop()
  -> callback completes an operation
     -> receiver synchronously destroys that operation
        -> callback object, or a nested stop source, is destroyed
```

`docs/roadmap/unified-cancellation-design.md` 和对应 ASan tests 已把这条路径纳入 Forge
backport 的 proof surface。共享 control block 让 `request_stop()` 在用户 callback 返回后
不必再次访问已经销毁的 callback object，也不依赖 owning operation 仍然存在。
其中 source 同步销毁属于历史扩展能力，不是当前 sender 可以依赖的标准契约。
下文的 lifetime 准备已保护具体持有者，并移除 stop-when 的内部 source；callback 自毁和
sibling 注销仍须支持，不能因为 source lifetime 已收敛而删除这些 primitive 回归。

## 参考实现对照

本轮以 2026-08-22 NVIDIA stdexec snapshot
(`f91f63636f24a85b594dfb19b79d191ccdccd5ec`) 作为当前 reference implementation；它仍采用
allocation-free intrusive 方案。历史审计同时保留 libunifex 的 out-of-line state machine
作为第二份成熟实现经验：

- [NVIDIA stdexec `stop_token.hpp`](https://github.com/NVIDIA/stdexec/blob/f91f63636f24a85b594dfb19b79d191ccdccd5ec/include/stdexec/stop_token.hpp)
- [libunifex `inplace_stop_token.hpp`](https://github.com/facebookexperimental/libunifex/blob/effb7527401b32b5a2d82fdf6d1a8e8810cbdb07/include/unifex/inplace_stop_token.hpp)
  与
  [out-of-line state machine](https://github.com/facebookexperimental/libunifex/blob/effb7527401b32b5a2d82fdf6d1a8e8810cbdb07/source/inplace_stop_token.cpp)

两者都把 callback node 内联在 `inplace_stop_callback`，通过 source-owned spin lock、
`removedDuringCallback` stack flag 和 per-callback completion atomic 协调注销。它们满足
标准要求的无分配和条件 `noexcept`，但共同依赖标准 lifetime rule：

- source 必须活过所有 associated token 和 callback；
- `request_stop()` 在 callback 返回后会重新访问 source；
- source destructor 会断言没有 callback 且 source lock 不在使用中。

因此，直接复制这套实现会删除 Forge 当前已测试的重入销毁能力。

Intel bare-metal sender library 也使用 intrusive allocation-free callback list，但它没有实现
标准要求的 concurrent callback deregistration wait，不能作为本仓库线程安全实现的模板。

## 候选方向

### 方向 A：采用标准 lifetime rule

依据 working draft 独立实现 intrusive state machine，并把 constructor 恢复为条件
`noexcept`。参考实现只用于理解不变量，不复制其代码。

优点：

- 直接满足 current WD；
- 无 per-registration allocation；
- 状态机已有成熟实现经验。

前置工作：

- 找出所有依赖 callback/source 在 `request_stop()` 栈上被销毁的 Forge sender；
- 给每个具体 sender 增加 completion keepalive，或收紧其 self-destroy receiver contract；
- 在 native stand-aside lane 证明相同的 lifetime contract。

在这些前置工作完成前直接采用方向 A 会重新引入 UAF。

### 方向 B：stack-owned detached request context

理论上可以让 `request_stop()` 在栈上建立 detached-list context；callback node 原子发布该
context，pending callback destructor 在 context lock 下摘链，running callback destructor
按 thread id 决定等待或标记 self-removal。source 被销毁后，request loop 只访问栈上
context。

这条路线可能同时保留无分配与 Forge 重入扩展，但目前没有成熟参考实现，且必须证明：

- registration / detach / destructor 三方 publication 没有漏唤醒或 ABA；
- pending callback 可被任意 callback 同步销毁而不留下 raw-pointer UAF；
- concurrent destructor 返回前，request context 不会离开栈；
- self-destroying running callback 返回后，request loop 不再触碰 callback storage；
- source destruction 与尚未观察到 detached context 的 destructor 不竞争。

在有完整状态机证明和 deterministic interleaving tests 之前，不实现这条路线。

### 方向 C：暂时保留独立 control block

保持当前 correctness behavior，接受 allocation 和 constructor exception-spec deviation。
这是当前选择。它不是永久标准化方案，也不是性能最优方案。

## 后续实现门槛

未来重开此项时，必须一次满足：

1. `is_nothrow_constructible_v<inplace_stop_callback<F>, inplace_stop_token, F>` 对
   nothrow `F` 为 true；
2. callback registration 本身零 allocation；
3. callback destructor 在另一线程执行 callback 时等待，在同一 callback thread 自毁时不等待；
4. callback 可销毁任意尚未执行的 sibling callback；
5. concurrent register / request / destroy stress 在 TSan 下通过；
6. 所有现有 operation self-destroy ASan tests 保持通过；
7. 若选择标准 lifetime rule，先修改具体 Forge sender，不能靠删除既有 regression test
   掩盖 UAF。

在这些条件具备前，不把一个局部 `source` condition variable patch 当作修复。

## 标准 lifetime rule 的准备

`request_stop()` 返回前 source 必须存活。这与 callback object 在自己的 invocation 中注销、
或销毁尚未执行的 sibling callback 是两条不同的规则；后两者仍受
[`[stoptoken.concepts]`](https://eel.is/c++draft/stoptoken.concepts) 的 deregistration 语义保护。
不能为了检测 source lifetime 而无条件读取已自毁的 callback node。

对 `backport/cpp26/execution/` 与 `include/forge/` 的持有者及 registration 清点如下。
表中“满足”表示相应 source 的调用栈生命周期已有保护或明确的 owner 前置条件，
不表示 registration 的分配和异常规格偏差已经消除。

| 表面 | lifetime 保护及边界 |
| --- | --- |
| `stop_token.hpp` | 保留 detached control block；source owner 须覆盖全部 registration 与 stop 调用。允许 callback 自毁和 sibling 注销。 |
| `when_all` | outer stop callback 在 children 尚未结束时取得 completion credit，内部 `request_stop()` 返回后归还；child error/stopped 自身尚未归还的 pending credit 保护 sibling stop。满足。 |
| `counting_scope::token::wrap` / 内部 stop-when | token 只保存两个输入 token；每个 child callback 直接向两个输入注册，以 atomic exchange 保证 `Fn` 至多调用一次。operation 不再持有 source 或输入 callback；同线程完成并销毁 operation 不会销毁正在执行的 source。满足输入 source 的 owner 前置条件时安全。 |
| `counting_scope` | scope owner 必须覆盖 `request_stop()`；即使 stop 使 join receiver 同步完成，也不能在 stop 返回前销毁 scope。 |
| `spawn_future` | producer stop 调用持有 shared-state owner，也覆盖其中的 stop-when operation；consumer callback 的 weak lock、delivery/abandon 的局部 owner 继续保护其他访问。满足。 |
| `any_stop_token` | invoker 在调用 erased callable 前复制 shared owner，callable 活到 invocation 返回；它不拥有 borrowed inplace source。满足 caller 的 source 前置条件时安全。 |
| `timer_context` | callback 只标记并唤醒 worker；item/operation data 由已有 shared owner 保护，worker 在 completion 前注销 callback。source 由 caller 保证存活。 |
| `channel` | send/recv callback 先取得 record/state owner，可同步移除自身 registration 并完成 receiver；不拥有 source，caller 负责其 lifetime。 |
| `io::as_sender(io_task)` | 融合 source 的每次 stop propagation 取得 completion credit；task 结果留在原 frame，最后一个 credit 归还后才走原交付路径，注销输入 callback 后交付。receiver token 查询结果先移除 cv/ref，保证引用返回的 token 也安装 callback。满足。 |
| `io::when_all_results` / `when_any_results` / `with_timeout` | child receiver 原有 shared owner 保护 sibling stop；downstream stop forwarding callback 新增 shared-state owner 覆盖 source 调用。`with_timeout` 复用 `when_any_results`。满足。 |
| Linux epoll | callback 持有 record/state，允许同步 stopped completion 和 operation 自毁；caller source 必须存活。 |
| Linux io_uring | callback 只在 context mutex 下排队 cancel，不同步 resume；CQE drain 与 callback 注销协调 frame lifetime。context、fd、buffer、source 均须遵守既有 borrowed lifetime。 |
| Windows IOCP | callback 只请求 `CancelIoEx`，completion packet 决定终态；record owner 和 callback 注销保护并发 completion。caller source 必须存活。 |
| `async_scope` | shared state 持有 source；owner 不得在自己的 work/completion 中析构或 wait，且须覆盖 stop 调用。 |
| `wait_result` | stack state 持有 source；connected operation 在 state 前析构，API 没有对外的 request 接口。 |

[`[exec.stop.when]`](https://eel.is/c++draft/exec.stop.when) 的 forwarding 语义要求保留
completion channel 和参数引用身份，不能通过复制 packet 或保存失败时改成 stopped 来延长
lifetime。stop-when 不新增结果存储或分配；其 completion 直接转发。child 自己负责 callback
lifetime，以及 completion、operation 析构与其他线程 invocation 之间的协调。若 completion
会同步销毁 operation，child 须保证正在执行的 callback 不再访问失效的 operation；
提前注销是实现这种协调的一种方式，并非所有 child 都必须在 completion 前注销。
child 保留到 operation 析构的 callback 也会继续占用输入 registration。
[`[stoptoken.concepts]`](https://eel.is/c++draft/stoptoken.concepts) 要求注销等待另一线程的
invocation 返回。当前 backport destructor 在 control mutex 下检查 running 和 request thread，
通过 condition variable 等待 running 清除，符合这一要求。

stop-when 已按 `[exec.stop.when]` 的 `stoken-t` 语义移除 fused source 和临时
`__stopped_source`。token 的两个查询仍返回输入查询的逻辑或；`callback_type<Fn>` 按
`Fn`、atomic flag、scope registration、receiver registration 的顺序构造，逆序析构。
每个 registration 使用其输入 token 的 `stop_callback_for_t`；发布 registration 前先查询
逻辑 token，已停止则在构造线程调用 trampoline，避免第二个输入已停止时被第一个输入的
并发请求抢先。底层 registration 也可在构造时立即调用 trampoline，flag 此时已初始化，
第二次调用只交换 flag 后返回。两次 registration
之间出现 stop request、两个输入同时停止、或两个 token 指向相同 source 时，也只有一个
trampoline 能调用 `Fn`。trampoline 调用 `Fn` 后不再访问 callback storage。

析构先注销两个 registration，再销毁 flag 和 `Fn`。另一线程正在运行的 trampoline 由
对应 registration destructor 等待；同线程 self-deregistration 不等待自己。输掉 exchange
的 trampoline 不等待获胜者，不调用 `Fn`，因此获胜者销毁 callback 并等待另一 registration
时不会形成等待环。构造尚未结束时，调用方仍须保证正在构造的 callback storage 有效。
registration 构造失败按普通成员展开顺序注销先前成功的 registration；异常由 child 的
注册位置处理，stop-when 不再合成 stopped completion 或额外 completion signature。

原同线程开放项已关闭：receiver 销毁 operation 仅销毁 child 和 registrations，正在执行的
source 属于输入 owner，仍须活过 `request_stop()`。每个 child registration 现在对应两个
输入 registration，当前 backport 的每个输入 registration 仍分配 control block；没有其他
新增分配，零分配与条件 `noexcept` 的 residual 保持不变。

上表其余持有者的复核结论为：`when_all` 和 `as_sender(io_task)` 的 completion credit、
`spawn_future` / erased callable / IO aggregate 的局部 shared owner 均覆盖 source 调用；
timer、channel、epoll、io_uring 和 IOCP 不拥有 caller source，其 callback 注销仍遵守原
borrowed lifetime；`counting_scope`、`async_scope` 的 owner 前置条件及 `wait_result` 的
stack 析构顺序保持。静态审计未发现其他依赖同线程销毁正在执行 source 的内部路径；
callback 自毁与 sibling 注销仍是必须保留的合法行为，不能与 source 自毁混同。

`when_all` 已有 aggregate result 存储，`io_task` 已有 frame result 存储，credit 只推迟走
原有交付路径，不复制参数、不增加 move、不改变 value/error/stopped 的选择。`spawn_future`、
erased callable 和 IO aggregate 只复制现有 shared owner。上述保护均不增加分配；
`any_stop_token` 的既有类型擦除分配和 backport registration 分配仍保留。

### 严格测试模式

`FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME` 只由专用 test executable 的 PRIVATE
definition 开启，没有 library option，也不导出到 consumer。callback invocation 返回后，
probe 读取 source atomic state；在 control lock 内，仅当 deregistration 尚未清除 node
pointer 时才 volatile 读取 callback storage。这保留合法 self/sibling destruction 与
concurrent deregistration，ASan 可以捕获 source 被同步释放的路径。
macro 未定义时，`stop_token.hpp` 的原执行路径不增加任何语句、成员或检查。

`execution_stop_callback_strict_lifetime` 汇集 stop-token、when_all、counting-scope 与
spawn-future 的原有 suites 及 heap operation 自毁回归；ASan 专属 negative control
`ProbeDetectsSourceDestroyedInsideRequest` 验证 probe 确实报告 UAF。
`forge_stop_callback_strict_lifetime_*` 重新构建 runtime、coroutine 和可用 backend suites。
两个 sanitizer lane 的现有 test selector 都包含这些名字；io_uring runtime gate 在严格版本
已构建时执行它，未构建时仍执行普通版本。
callback 不允许抛异常的要求保持不变。

`CountingFusionWaitsForOtherThreadAndForwardsOriginalReference` 用 semaphore 固定交错：
线程 A 阻塞在 child stop callback，线程 B 交付不可复制对象的 value/error 引用或 stopped；
child 进入 registration 注销后仍没有 receiver completion，释放 A 后才交付原地址并由
receiver 释放 operation；两个输入分别覆盖，wrapper 自身不再持有 registration。
`execution_stop_callback_allocations` 在注册完成后拒绝 allocation，验证重叠 completion
不复制会抛异常的 payload、不分配且保持 channel 和地址；TSan 因全局 allocation replacement
冲突按返回码 77 跳过该独立 executable，其他严格 lifetime 测试仍运行。

`StopCallbackLifetimeTest.CountingFusionSameThreadReceiverDestroysOperation` 是普通 passing
regression，覆盖两个输入和 value/error/stopped；严格 CTest entry 不再排除该路径，也不再
提供独立的 known-open target。新增直接 registration tests 覆盖双输入预先停止、两个
registration 之间停止、并发 stop/self-destruction、输掉仲裁的 invocation 与析构重叠，以及
两次注销先于 `Fn` 析构的顺序。ASan 严格模式仍须运行完整 suite 和上述 negative control。

原有 ASan proof surface 包括 `WhenAllResultsAllowsSelfDestroyDuringSiblingStop`、
`SiblingStopFinishesBeforeCrossThreadDelivery`、
`InlineTaskCompletionAllowsReceiverSelfDestruction`、
`EarlyCrossThreadCompletionAllowsReceiverSelfDestruction`、
`WrapCompletionCanDestroyOperationAfterDeregistration`、
`SynchronousConsumerCompletionCanDestroyOperation`，以及 timer、channel、epoll、IOCP 的
operation 自毁测试。新增 heap 回归覆盖 outer stop、两个融合输入、consumer stop、
task env/receiver stop 及两个 IO combinator 的 downstream stop。

### 状态机改动之前仍需证明

当前仍采用方向 C 的存储。stop-when 同线程 source 销毁的结构性缺口已通过直接 registration
关闭；所有 lifetime 保护仍须在 ASan、TSan、MSVC、AppleClang 与 native stand-aside 环境
实际验证，不能以静态审计代替运行结果。输入 source 的 owner 前置条件不能取消。
后续独立状态机实现仍须满足上面的七项门槛，特别是 same-thread removal、pending sibling
removal、cross-thread destructor wait、registration publication 和条件 `noexcept`。
必须保留严格 suites 与 negative control；移除 control block 后应将 probe 调整为新 node
的合法访问规则，而不是删掉 source lifetime 检查。零 allocation 的 registration 和
constructor 的条件 `noexcept` 尚未实现。

# Unified cancellation design（统一取消语义）

本文整理 CC Forge 当前的 cancellation surface。它不是新机制提案，也不是要求每个
primitive 都升级到同一种最强取消模型；目标是把全仓已经实现、已接受和明确延后的取消语义
放进同一张地图，避免后续在 `stop_token`、`request_stop()`、operation-state 自毁和
coroutine stopped 传播上重新推导。

## 总体原则

- cancellation 是 cooperative。Forge 不强制中断已经运行中的 user code。
- `close()`、`request_stop()`、`shutdown()`、`wait()` / `join()` 的含义以
  [`forge-runtime.md`](../forge-runtime.md) 为准。
- receiver completion 必须在内部 mutex 外执行；如果需要在 completion 中销毁
  operation-state，implementation 必须已经证明 completion 后不再访问该 operation-state。
- 没有证明 full per-operation stop callback 前，不要声明 full cancellation support。
- 对 platform backend，保留平台模型：Linux readiness 和 Windows IOCP completion 不互相伪装。

## Taxonomy

| 类别 | 含义 | 典型例子 |
| --- | --- | --- |
| No cancellation support | API 不观察 caller stop token；只能靠对象生命周期或外部协议结束。 | 纯 synchronous memory stream vocabulary。 |
| Pre-start observation | `start()` 前或 start 边沿检查 stop token；已经 accepted 的 work 不注册 per-operation callback。 | 一些 scheduler edge 和 fail-closed path。 |
| Primitive-owned shutdown | `close()` / `request_stop()` / `shutdown()` 由 owning primitive 统一唤醒 pending work。 | `static_thread_pool`、`strand`、`async_scope` teardown。 |
| Per-operation callback | pending operation 注册 receiver stop token callback，callback 只负责把该 operation 从 pending set 中移除并完成 stopped 或请求 backend cancel。 | `bounded_channel` pending send/recv、`timer_context` timer item、Linux IO waiter、Windows IOCP record。 |
| Sibling cancellation | 一个 child 的 error / EOF / stopped 会请求 shared stop source，让 sibling 观察 stop。 | `std::execution::when_all`、`forge::io::when_all_results`。 |
| Fused token / composed stop source | 多个 stop source 组合成 child env 中暴露的 single stop token。 | `counting_scope::token::wrap`、`as_sender(io_task<T>, env)`。 |
| Stopped-as-coroutine-exception | sender stopped 进入 coroutine frame 时先表现为内部异常，再由 task/sender bridge 映射回 stopped channel。 | `forge::task`、`forge::io::io_task` 的 `await_sender` / `as_sender`。 |

这些类别是描述当前 behavior 的标签，不是等级目标。一个 API 可以同时属于多类，例如
`as_sender(io_task<T>, env)` 同时使用 fused token 和 stopped-as-coroutine-exception。

## 当前 API map

| 区域 | API / primitive | 当前类别 | 关键保证 | 限制 / accepted tradeoff |
| --- | --- | --- | --- | --- |
| Backport stop tokens | `inplace_stop_source/token/callback` | Per-operation callback primitive | Backport 版本支持 callback list detach、锁外 invoke、callback destructor wait。 | 标准形状的 `inplace_stop_source` 仍要求 source lifetime 合法；不要假设所有 native 实现都容忍 source 在自身 `request_stop()` 栈上被销毁。 |
| Backport stop tokens | `never_stop_token` | No cancellation support marker | 用于缺失 stop query 的 fallback。 | 永不请求停止。 |
| Backport stop tokens | `any_stop_token` | Type-erased per-operation token | 擦除 downstream receiver stop token 并保持 callback lifetime 正确。 | 内部 control block allocator-neutral；这是 native handoff 友好的取舍。 |
| Execution consumers | `sync_wait` | Caller-owned stop source | receiver env 暴露 `inplace_stop_token`；`set_stopped` 返回 empty optional。 | Blocking consumer，不是 cancellation framework。 |
| Execution composition | `when_all` | Sibling cancellation | child env 暴露 shared stop token；error 覆盖 stopped；外部 stop token 可传给 children。 | 至少一个 child sender；完整泛化仍受 backport subset 限制。 |
| Execution scope | `simple_counting_scope::token::wrap` | No child-token fusion | Current-WD shape 下 simple scope wrap 是 identity forwarding。 | Association 由 `associate` / `spawn` / `spawn_future` 拥有。 |
| Execution scope | `counting_scope::token::wrap` | Fused token / composed stop source | child env 暴露 fused stop token；scope stop 与 downstream receiver/env stop 任一请求都会让 child 观察 stop。 | 这是当前实现选择；签名可能声明 stopped 兜底以覆盖 deferred connect failure。 |
| Execution scope | `spawn` | Scope-owned execution | 只接受 `set_value()` / `set_stopped()` sender；error/value sender 需先转换。 | 不提供 fire-and-forget error channel。 |
| Execution scope | `spawn_future` | Cancel-on-abandon + single-consumer future | abandoned/unconsumed future 和 unstarted connected operation 请求 stop；consumer stop token 也请求 producer stop。 | Single-consumer；typed result 经 future sender 交付。 |
| Forge scheduler | `static_thread_pool` schedule op | Pre-start observation + primitive-owned shutdown | start 边沿观察 receiver stop token；shutdown 后/queue 拒绝时完成 stopped。 | 已运行 user work 不被强制中断。 |
| Forge scheduler | `single_thread_context` / `resource_context` | Primitive-owned shutdown | 组合内部 runtime/scope，destructor shutdown + wait。 | Owner 负责不在 own worker/completion 内销毁。 |
| Forge scheduler | `strand` | Primitive-owned shutdown + fail closed | pending/future strand work 在 shutdown 或 runner launch failure 后 stopped；self-wait guard 避免死锁。 | 底层 scheduler transient refusal 与 shutdown 不区分，文档化为 fail closed。 |
| Forge runtime | `bounded_channel` async send/recv | Per-operation callback | pending send/recv 注册 receiver stop token；callback 从 queue 移除并在 mutex 外完成 stopped。 | Direct send/recv 是同步 value path，不代表 pending cancellation。 |
| Forge runtime | `timer_context` timer sender | Per-operation callback + primitive-owned shutdown | receiver stop callback 唤醒 worker；shutdown 把 pending timer 完成为 stopped。 | 已 started operation-state 必须活到 terminal completion；提前销毁不是取消协议。 |
| Forge runtime | `async_scope` | Primitive-owned shutdown + env stop propagation | `request_stop()` 通过 owned receiver env 暴露 stop token；destructor shutdown + wait。 | `wait()` 不是 user memory_resource teardown barrier；scope 不应在 own work/completion 内调用 `wait()` 或析构。 |
| Forge runtime | `forge::task` | Stop-token forwarding + stopped-as-coroutine-exception | connect 时把 downstream receiver stop token 擦除进 promise env；task 内 awaited sender 可观察该 token。final_suspend 同步完成 receiver；stopped 经内部异常回到 task frame，最终映射为 stopped completion。 | 只转发 stop token，不转发任意 receiver env query。用户 `catch(...)` 可捕获取消；promise stopped 状态是 sticky，后续异常不会覆盖 stopped。Custom receiver 不应在 completion 中销毁 connected task op-state。 |
| Forge erasure | `erased_sender` | Stop-token forwarding | erased receiver env 暴露 downstream stop token as `std::any_stop_token`。 | 只承诺 bounded env model 中的 stop token，不擦除任意 env query。 |
| Forge erasure | `any_scheduler` | Stop-token preserving schedule erasure | 内部 erased receiver 保留 downstream stop token visibility。 | Scheduler surface 很窄，只擦除 `schedule()`。 |
| Forge IO backend | Linux `io::context` readiness / async read-write | Per-operation callback + primitive-owned shutdown | receiver stop token 可取消 pending waiter；`request_stop()` / `cancel(fd)` / shutdown 完成 stopped；completion 在 mutex 外运行。 | fd 和 buffer borrowed；nonblocking fd 是 async read/write convenience 前置条件。 |
| Forge IO backend | Windows IOCP async read-write | Completion-based backend cancel | receiver stop token 或 `cancel(HANDLE)` 调用 `CancelIoEx`；最终由 completion packet 决定 value/stopped/error。 | HANDLE 和 buffer borrowed；不提供 readiness sender；random-access file offset 和 handle ownership scoped out。 |
| Coroutine IO | `io_env` | Borrowed cancellation environment | 携带 `std::inplace_stop_token`、executor、memory resource pointer。 | `inplace_stop_source` 必须活过使用该 env 的 operation/await 链。 |
| Coroutine IO | `await_sender(sender)` | Stopped-as-coroutine-exception + env forwarding | source sender receiver env 暴露 `io_env.stop_token` 和 scheduler；stopped 抛 `sender_stopped`。Inline completion 在 `await_suspend` 返回后继续，避免递归 resume。 | 不自动 hop 回 executor；source sender 必须允许 operation-state 在 receiver completion callback 内被销毁。 |
| Coroutine IO | `as_sender(io_task<T>, env)` | Fused token + stopped-as-coroutine-exception | operation-state 融合 `io_env.stop_token` 与 receiver/env stop token；任一 stop 请求都会让 coroutine 内 `await_sender` 观察 stopped；结果映射为 sender completion。 | Single-use；operation-state 持有 task 到 terminal completion。 |
| Coroutine IO | `when_all_results` | Sibling cancellation + partial-result retention | child error/EOF/stopped 请求 shared stop source；pending sibling 通过 fused token 观察 stop；已完成 child result 不丢失。 | 只证明 two-child `io_task<io_result<...>>` shape；variadic/policy-based combinator deferred。 |

## Self-destroy 与 stop-source 重入

Forge 支持若干 receiver completion 中同步销毁 operation-state 的测试路径，但这不是所有 sender
的无条件要求。当前已验证的规则是：

- implementation 在调用 receiver completion 后不能再访问 operation-state 成员，除非它先建立了
  独立 keepalive；
- completion 必须在 internal lock 外执行；
- self-destroy 测试只证明对应 sender 的 implementation，不把该能力扩展到任意 third-party
  sender；
- `forge::task` 和 `io_task` 的 final-suspend completion 仍要求 custom receiver 不要在
  completion callback 内同步销毁 connected task operation-state。

更深的 cross-cutting case 是 stop-source reentrancy：

```text
request_stop()
  -> callback completes sibling
     -> aggregate receiver completion
        -> receiver destroys operation-state
           -> operation-state destroys the stop_source currently executing request_stop()
```

`forge::io::when_all_results` 有 focused ASan regression 覆盖这条路径：
`WhenAllResultsAllowsSelfDestroyDuringSiblingStop`。它证明当前 Forge/backport
`std::inplace_stop_source` implementation 能承受该重入销毁模式。

不要把这个测试外推成标准保证。若某个平台未来使用 native `std::inplace_stop_source`
stand-aside，并且 native implementation 不容忍 source 在自身 `request_stop()` 栈上被销毁，
这类 self-destroy receiver 仍可能需要额外 keepalive 或文档化 precondition。当前 policy 是：

- 对 Forge/backport path：使用 sanitizer tests 锁住已证明的 self-destroy path；
- 对 native stand-aside path：不声明超出标准 stop-source lifetime 规则的 guarantee；
- 发现 native 差异时，优先为具体 sender 加 keepalive 或收紧 receiver contract，不引入全局
  cancellation abstraction。

## Stopped-as-coroutine-exception

`forge::task` 和 `forge::io::io_task` 都需要把 sender stopped 传过 coroutine frame。
当前做法是用内部异常表示 stopped：

- `forge::task` 的 awaitable bridge 把 stopped 作为内部异常回到 task frame；
- `forge::io::await_sender` 在 `set_stopped` 后让 `await_resume()` 抛
  `forge::io::sender_stopped`；
- `as_sender(io_task<T>, env)` 捕获 `sender_stopped` 并交付 `set_stopped()`。

这个模型的 sharp edge 是用户 `catch(...)` 可以捕获 stopped 异常。如果 coroutine body 吞掉该
异常并继续执行，promise 的 stopped 状态仍是 sticky 的：后续普通 error 不应覆盖已经决定的
stopped completion。这个行为已经在 `forge-runtime.md` / `forge-io.md` 中写明；不要在设计
note 中把它改写成非抛 channel。非抛 stopped channel 可以作为未来 task redesign 的独立
taskbook，而不是本轮小修。

## Known accepted limitations

- `split` 是非 WD extension；不实现完整 stop-source/on_done cycle。
- `bulk` policy 入口仍是串行 subset；execution policy 不引入 parallel cancellation。
- `timer_context` operation-state 提前销毁不是取消协议。
- `async_scope::wait()` / destructor 等待 scope work 计数归零，但不拥有用户 memory resource
  teardown。
- `system_context` 是 process-lifetime singleton；`shutdown()` 不隐式 join，确定性排空请显式
  `wait()`。
- `io_env` 是 borrowed；`std::inplace_stop_source`、scheduler 和 memory resource lifetime
  由调用方保证。
- `await_sender` 不自动 executor hop，completion thread 可能是 source scheduler、Linux poller
  或 Windows IOCP worker。
- Windows IOCP cancel 是 completion-based；`CancelIoEx` 请求取消后仍以最终 completion packet
  为准，成功 completion 可以赢过 cancel。

## Do not fix in this design round

- 不新增全仓统一 cancellation base class 或 type-erased cancellation object。
- 不把 every primitive 升级到 per-operation callback；只有已经证明 callback lifetime 和
  exactly-once 的 path 才使用该层级。
- 不把 coroutine stopped 改成非异常 channel；这会牵动 task ABI/behavior，需要单独设计。
- 不把 Windows IOCP completion 包装成 Linux readiness。
- 不新增 networking surface、`io_uring` backend 或 `<io>` / `<networking>` backport。

## Current action items

本次审计没有发现新的 user-visible correctness bug。审计中发现的文档漂移已经同步修正：
`docs/forge-runtime.md` 的 `io_task` 条目现在明确
`as_sender(io_task<T>, env)` 会融合 `io_env.stop_token` 和 receiver/env stop token。

除此之外，不建议从本文直接派生新的实现任务。若后续 review 在某个具体 primitive 上发现
native stop-source lifetime 差异或未覆盖的 callback path，应写 focused taskbook，并附
对应 ASan/TSan 或 Windows lane evidence。

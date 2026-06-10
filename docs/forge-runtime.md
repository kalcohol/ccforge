# Forge runtime lifecycle contract（生命周期契约）

`include/forge/` 下的 runtime primitives 都是 Forge extension。它们不是
standard backport，也不会向 `namespace std` 添加名字。

这份文档统一 Forge runtime utilities 使用的生命周期词汇，避免后续设施在
`close()`、`request_stop()`、`shutdown()`、`wait()` 和 `join()` 的语义上漂移。

## 核心术语

`close()` 表示 graceful ingress close：

- 未来的 work / message 会被拒绝；
- 已经 accepted 的 work 仍可正常完成；
- channel 中已经 buffered 的 value 可以继续 drain；
- 单独调用 `close()` 不表示 request stop。

`request_stop()` 表示 cooperative cancellation：

- primitive 支持 stop-token 时，owned operation 应观察该 token；
- pending operation 可以完成为 `set_stopped()`；
- 已经运行中的 user code 不会被强制中断。

`shutdown()` 表示 owning runtime object 的 `close()` + `request_stop()`。它是
“session 正在结束”的常规操作。

`wait()` 是 blocking drain helper：

- 它等待该 primitive 文档化范围内已经 accepted 的 work；
- 它不能在持有 internal lock 时调用 user callback；
- 当它从该 primitive 自己的 worker 或 completion callback 中被调用时，必须避免
  self-deadlock。

`join()` 是 primitive 可以暴露 sender 时的首选 async surface。它在对象 drain 后完成。
Blocking `wait()` 仍可用于测试、destructor 和简单 shutdown path。

## Destructor policy（析构策略）

Owning Forge runtime objects 应该可以安全析构。推荐策略是：

- 调用 `shutdown()`；
- 用 `wait()` 或等价的内部 join drain 已接受 work；
- 明确记录 destructor 可能阻塞。

这是 Forge 有意选择的 extension tradeoff。某些 standard-style scope 要求用户在析构
前显式 join，并把 outstanding work 下析构视为 precondition violation。Forge 的
owning contexts 更偏向 resource/session management 场景里的安全析构。

`wait()` 的 self-deadlock guard 不等于 destructor 可以从自己的 worker 或 owned
completion 中运行。`static_thread_pool`、`io::context`、`accel::*::context` 和
`async_scope` 这类 owning primitive 必须由外层 owner 管理 lifetime；不要在它们自己
拥有的 work body / completion callback 内销毁该 primitive。完整 teardown 应由外层
owner 调用 `shutdown()` / `wait()` 后离开作用域。

Non-owning view 和 lightweight handle 不应在 destructor 中阻塞。

## 当前 utilities

- `forge::start_detached(sender)` 是 Forge 的非标准 fire-and-forget utility。value /
  stopped completion 会释放 heap state；error completion 会调用 `std::terminate()`。
  Standard-shaped code 应优先使用
  `std::execution::spawn(sender, scope.get_token()[, env])`。
- `forge::static_thread_pool::shutdown()` 停止接受新的 schedule operation，并 drain 已
  accepted 的 work。`wait()` 等待 queue 和 active task 清空；如果从 pool 自己的
  worker thread 调用，它会立即返回以避免 self-deadlock。`options` 可携带 non-owning
  `std::pmr::memory_resource*`，用于 queue node 和 queued task callable-record 分配。
  Pool 对象本身不得在自己的 worker thread 上析构；外层 owner 负责 teardown。
- `forge::timer_context::shutdown()` 停止接受新的 timer，并把 pending timer 完成为
  stopped。`wait()` 等待 accepted timer operation。`options` 可携带 non-owning
  `std::pmr::memory_resource*`，用于 state、timer op data、timer item control block
  和 timer queue 分配。Pending timer 在 receiver env 中存在 stoppable token 时会
  注册 stop callback；callback 会唤醒 worker，不依赖周期性 polling。已 `start()`
  的 timer operation state 必须保持存活直到 value / stopped completion；提前销毁
  operation state 不是取消协议，调用方应通过 receiver stop token 或
  `timer_context::shutdown()` 取消。
- `forge::runtime_context::wait()` 是 practical single-hop drain：`pool -> timers ->
  pool`。它不是 unbounded quiescence protocol。
- `forge::async_scope` owns eager-start sender work。`close()` 拒绝后续 spawn，
  `request_stop()` 通过 owned receiver env 暴露 requested stop token，destructor 会执行
  `shutdown()` + `wait()`。对 non-copyable non-const lvalue sender，
  `spawn(sender)` 会 destructive move；如果代码需要在 native C++26 实现下保持同一源码，
  请显式写 `std::move(sender)`。`options` 可携带 non-owning
  `std::pmr::memory_resource*`，用于 scope state 和 spawned op-state node 分配。
  该 resource 必须活到 scope 对象和已接受工作的 terminal completion 释放尾部之后；
  `wait()` 等待 scope work 计数归零，但不把用户 resource 变成 owned teardown barrier。
  Scope 对象不得在它自己拥有的 spawned work body / completion callback 内析构。
- `forge::resource_context` 把 runtime context 和 async scope 组合成 resource session
  根对象。它的 `options` 会把 resource policy 传给内部 runtime 和 scope op-state
  allocation path。Destructor 执行 owning-context shutdown 和 wait。
- `forge::strand` 串行化 accepted scheduler work。Shutdown 会把 pending 和 future
  strand work 完成为 stopped。`options` 可为 pending queue、receiver record 和 runner
  keepalive node 提供 memory resource。若 `wait()` 从该 strand 正在运行的 completion
  中调用，会立即返回以避免 self-deadlock；完整 drain 应由外层 owner 执行。底层
  scheduler 若在 runner launch 时拒绝或失败，strand 会保守地 fail closed：pending
  work 完成为 stopped，并拒绝后续 work。当前 API 不区分 bounded queue full 这类瞬态
  失败与 shutdown。`strand_options::memory` 同样是 non-owning：调用方应让该 resource
  活过 strand 对象和 runner keepalive node 的 terminal release tail。
- `forge::bounded_channel` 提供 graceful `close()` draining 和 cancel-now
  `request_stop()`。`options` 可为 buffer、pending operation、action batch 和 record
  提供 memory resource。Pending send/recv 在 receiver stop token 可用时注册 stop
  callback；callback 会从 pending queue 移除 operation，并在 channel mutex 外完成
  stopped。
- `forge::io::context` owns platform IO worker。Linux backend 使用 epoll/eventfd
  readiness poller；Windows backend 使用小型 IOCP completion worker。`close()` 拒绝新
  operation，同时允许已 pending operation 正常完成；`request_stop()` 会把 pending
  operation 完成为 stopped 或发起取消；`shutdown()` 组合 close 和 context stop。
  `wait()` join worker。File descriptor、Windows handle 和 user buffer 都是 borrowed，
  必须活到 pending operation 完成，或在 close 前先 cancel 并 drain。
- `forge::accel::mock::context` owns portable mock / in-memory accelerator-shaped
  command queues。每个 queue 是 FIFO；cross-queue ordering 通过 event 表达。
  `close()` 拒绝后续 command 并 drain accepted work；`request_stop()` 尽可能停止
  pending queued command；`shutdown()` 组合二者。`wait()` drain accepted command work；
  若从 accel command completion 中调用，会立即返回以避免 self-deadlock。Host spans 是
  borrowed；context 对象不得在自己的 command completion 内析构；
  `mock::host_buffer<T>` / `mock::device_buffer<T>` own mock storage。
  `memory_kind` 是 portable metadata，不代表真实 vendor allocation。
- Mock device 是 context-owned handle，带 portable metadata。Device-bound queue 和
  session 会在运行 command 前检查 availability。Mock device loss 映射到
  `device_lost`；`reset` 增加 `device_epoch`，旧 session 后续映射到 `stale_session`。
  Session packet command 可以 own request/response storage 直到 terminal completion。
  Packet timeout 是 queued-command deadline，不会打断已经运行中的 user code。Model
  execute proof 验证 byte-span IO binding 和 session/context lifecycle；它不是 tensor
  或 inference engine layer。
- `forge::erased_sender` 通过 v1 bounded env model 转发 downstream stop token。
- `forge::any_scheduler` 是窄 scheduler 类型擦除；通过内部 erased receiver 保留
  downstream receiver stop-token visibility，因此底层 scheduler operation 仍能观察
  调用方的 stop token。
- `forge::system_context` 是 process-lifetime singleton。它故意不在 C++ static teardown
  阶段销毁；`shutdown()` 只请求停止，不隐式阻塞或 join worker。需要排空已经接受的
  work 时应显式调用 `wait()`。长生命周期服务如果需要 deterministic shutdown，仍应显式
  own pool/context。
- `forge::task` 从 coroutine `final_suspend` 发出 receiver completion。Custom receiver
  不应在 `set_value` / `set_error` / `set_stopped` callback 内同步销毁连接的 task
  operation-state。Stopped completion 通过 coroutine bridge 的内部异常返回 task frame，
  可以被用户 `catch(...)` 捕获；promise 的 stopped 状态是 sticky 的，后续异常不会覆盖
  stopped completion。

## V1 cancellation 边界

Forge primitive 应优先提供小而明确的 guarantee，而不是半正确的大 guarantee。

对 pending operation cancellation，v1 primitive 可以选择以下层级之一：

- 只在 pre-start 观察 stop token；
- 只通过 primitive-owned close/shutdown wakeup 取消；
- full per-operation stop-callback cancellation。只有在 callback lifetime 和 exactly-once
  completion 已证明并在 sanitizer 下测试后，才应选择这个层级。

如果没有实现 full callback cancellation，文档必须明确写出缺失的 case。

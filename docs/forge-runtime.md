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

`wait()` 不是外部提交线程的 join。开始 teardown 前，owner 必须先停止并 join 可能并发
调用 sender `start()` / primitive submission API 的线程；shutdown 后被拒绝的 operation
可以在提交线程内同步完成为 stopped，而 primitive 无法把该外部调用栈纳入自己的 drain
计数。只有在 external submission 已 quiescent 后，`shutdown()` + `wait()` 才构成完整的
owning-context teardown barrier。

直接以 `static_thread_pool::scheduler` 构造的 `strand` 在有界池满队拒绝 runner 时保留
pending FIFO，注册一次性容量通知；worker 释放容量后通知其重新入队 runner，尾部重启
被拒绝时也采用相同机制，无需后续提交或轮询。pool 关闭会主动通知停驻的 strand，
pending work 以 stopped 完成，`wait()` 因而能够结束；容量为 0 时须关闭 pool 或 strand。
其他 scheduler（包括 `any_scheduler`）在 stopped 拒绝后仍使 strand 永久关闭；仅有
`closed()` 查询不足以支持重试。分配失败、error completion 和 launch 异常也会关闭 strand。

未来 primitive 若提供异步 drain，可采用 sender 形式的 `join()`；当前 Forge 原语只提供阻塞 `wait()`。

## Destructor policy（析构策略）

Owning Forge runtime objects 应该可以安全析构。推荐策略是：

- 调用 `shutdown()`；
- 用 `wait()` 或等价的内部 join drain 已接受 work；
- 明确记录 destructor 可能阻塞。

这是 Forge 有意选择的 extension tradeoff。某些 standard-style scope 要求用户在析构
前显式 join，并把 outstanding work 下析构视为 precondition violation。Forge 的
owning contexts 更偏向 resource/session management 场景里的安全析构。

`wait()` 的 self-deadlock guard 通常不等于 destructor 可以从自己的 worker 或 owned
completion 中运行。`async_scope` 这类 owning primitive 必须由外层 owner 管理
lifetime；不要在它自己拥有的 work body / completion callback 内销毁。
完整 teardown 应由外层 owner 调用 `shutdown()` / `wait()` 后离开作用域。

`forge::io::context`、`forge::timer_context` 和 `forge::static_thread_pool` 是经过
测试的同线程析构例外：从自己的 worker / poller completion 中析构时，destructor 会
shutdown 并 detach 当前 worker（pool 会 detach 全部 worker），内部 state 由
worker/operation keepalive 留到 terminal release tail。这个能力不延伸到外部
submitter，也不自动延长自定义 `memory_resource` 的寿命；使用自定义 resource 时，
应优先由外层 owner 正常 drain，或保证 resource 活到 detached worker 和最后一个
record 全部释放。detach 后仍在排空的 backlog 任务不得再触碰 pool 对象本身
（例如捕获了 `&pool` 或再取 `get_scheduler()`）：pool 对象在析构返回后即不存在，
只有共享 state 仍活着。

`resource_context` 明确排除在该例外之外：它的 `wait()` 会阻塞在
`scope_.wait()` 直到所有 spawn 的任务完成，从自己 scope 里的任务销毁它会等待
自身、在到达 pool 的 detach 逃生口之前就死锁。resource_context 必须由外层
owner 析构，或先在非 worker 线程上 `request_stop()` + `wait()`。

Non-owning view 和 lightweight handle 不应在 destructor 中阻塞。

## 原语参考与取消层级

具体接口、资源寿命和分配路径见[工具参考](forge-utilities.md)。

| 原语 | 取消层级 | 参考 |
| --- | --- | --- |
| pool / strand | start 前检查；strand shutdown 取消 pending | [调度](forge-utilities.md#调度与上下文) |
| timer / channel | per-operation callback | [取消](forge-utilities.md#取消与入口关闭)、[通道](forge-utilities.md#通道容量) |
| async_scope / resource_context | scope token；timer 停止同时永久关入口 | [调度](forge-utilities.md#调度与上下文) |
| IO context | 后端取消及 drain | [IO](forge-io.md#lifecycle生命周期) |
| task / io_task | 各自的 coroutine 契约 | [任务桥接](forge-utilities.md#两种协程任务) |

新增原语的要求见[贡献指南](contributing-forge-primitives.md)。

当前 `forge::` 原语没有异步 `join()` 接口；使用阻塞 `wait()`。需要 sender 形式的 join 时参见 [execution scopes](backports/execution.md)。timer 的 `request_stop()` 永久拒绝后续定时器，是通用 close/stop 分离规则的例外。

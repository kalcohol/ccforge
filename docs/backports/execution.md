# `std::execution` backport 说明

当前为 P2300 senders/receivers 的 Phase 1-4 backport（Phase 4 部分功能）。

## 已实现

- Sender 工厂：`just`、`just_error`、`just_stopped`、`read_env`
- 适配器：`then`、`upon_error`、`upon_stopped`、`let_value`、`let_error`、
  `let_stopped`、`write_env`、`unstoppable`
- 调度器适配器：`starts_on`、`continues_on`（schedule_from）、`on`、`affine`、
  `transfer_just`、`bulk` / `bulk_chunked` / `bulk_unchunked`（串行 subset）
- 组合器：`into_variant`、`when_all`（完整笛卡尔积签名、外层取消传播）、
  `when_all_with_variant`、`split`、`associate`、`spawn`、`spawn_future`
- 消费者：`sync_wait`（单一 value completion 返回 `optional<tuple<...>>`，多组
  value completions 返回 `optional<variant<tuple<...>, ...>>`）、
  `sync_wait_with_variant`（均通过 `std::this_thread`）
- Stopped 工具：`stopped_as_optional`、`stopped_as_error`
- 调度器：`inline_scheduler`、`run_loop`（mutex+cv，跨工具链可移植）
- Stop tokens：`inplace_stop_source/token/callback`、`never_stop_token`、`any_stop_token`（类型擦除）、stoppable concepts
- Coroutine 桥：`as_awaitable`、`with_awaitable_senders`（需要 C++20 coroutines；
  单一 value completion 保持返回 `tuple`，多组 value completions 返回
  `variant<tuple<...>, ...>`）
- 基础设施：`completion_signatures_of_t`、`enable_sender`、`forwarding_query`、
  `get_start_scheduler`、`get_delegation_scheduler`、`get_forward_progress_guarantee`、
  `get_completion_scheduler`、`get_completion_domain`、`get_await_completion_adaptor`、
  `transform_completion_signatures`、CPO 分发基础设施
- 域调度：`default_domain`、`get_domain` CPO、receiver-env start-domain 选取、
  sender-env completion-domain 选取、`connect_t` recursive `transform_sender`
- Async scope subset：`simple_counting_scope`、`counting_scope`（独立 stop-aware scope）

## 当前限制

- Receiver completion callbacks 当前必须为 `noexcept`，包括 `set_value`、`set_error` 和
  `set_stopped`；throwing completion callbacks 尚不支持，并由配置期 negative compile
  probe 覆盖。
- Library-provided sender 的 `connect_t` 提供 rvalue 移动路径和 copyable lvalue 拷贝路径；
  non-copyable lvalue sender 需要显式传入 `std::move(sndr)`，以保持 native C++26
  handoff 时的源码形态一致。const non-copyable lvalue 仍不可连接。
- Execution domain 支持仍是 draft 子集：`connect_t` 已按 receiver env 选取 start
  domain，并按 sender env 选取 completion domain，支持 start/completion 两阶段
  recursive `transform_sender`；scheduler-derived start domain 仅在 scheduler 显式定制
  `get_completion_domain<set_value_t>` 时生效，否则会回退到 `default_domain`。
  `get_completion_signatures(sender, env)` 会先按同一 transform 模型得到最终 sender 类型，
  再读取 completion signatures；非 default-domain 路径允许原 sender 没有 raw
  completion signatures，只要 transformed sender 提供可用 signatures。
- `bulk` / `bulk_unchunked` / `bulk_chunked` 在底层标准库暴露 execution policy traits /
  objects 时接受 current-WD 的 execution-policy-shaped 调用，同时保留旧的无 policy
  便利入口；当前实现仍是串行 subset：`bulk` 与 `bulk_unchunked` 在完成线程逐 index
  调用，`bulk_chunked` 对非零 shape 调用一个 `[0, shape)` chunk；policy 不引入并行执行。
- `on` 当前是 current-WD 的实用 subset：`on(scheduler, sender)` 通过 receiver env 中的
  `get_start_scheduler` 返回原调度器；`on(sender, scheduler, closure)` 要求 child
  sender attributes 暴露 `get_completion_scheduler<set_value_t>`，再通过
  `continues_on(closure(continues_on(child, sch)), orig_sch)` 返回原
  value-completion scheduler。
- `continues_on` / `affine` 当前只实现单一 value completion shape 的 transfer subset；
  value 参数会 decay-copy 到调度 hop 的 operation state。多 value-alternative 和引用
  value-signature 的逐位 WD 语义需要单独重构，不应从当前 subset 推断。
- `split` 是保留的非 WD extension。它缓存单一 value completion shape，并在内部订阅者
  callback 入链分配失败时以 `set_error(std::exception_ptr)` 完成；它没有实现完整
  stop-source/on_done cycle，abandoned never-completing source 的取消不是当前保证。
- `ensure_started` / `start_detached` 不再由 `<execution>` backport 暴露；这两个名字不是
  当前 working draft `[exec]` surface。需要 fire-and-forget 时，standard-shaped code 应
  使用 scope-token based `spawn(sender, token[, env])`；Forge runtime extension 侧保留
  `forge::start_detached(sender)`。
- `sync_wait` 会把 `set_error(std::exception_ptr)` 原样 rethrow；其他 typed error 会先包装
  进 `std::exception_ptr` 再 rethrow，因此调用方需要按原 error 类型捕获。若需要同步消费
  value / stopped / closed-set typed error 而不抛异常，使用 Forge 扩展层的
  `forge::wait_result(sender)`。
- `spawn_future` 当前返回 move-only single-consumer future sender；其 shared-state 和
  consumer record 分配会使用 `env` 中的 `get_allocator`。下游 stop-token 会通过
  `any_stop_token` 类型擦除注册 callback；该标准形状的类型擦除层内部 control block 保持
  allocator-neutral，这是为了降低 native handoff 风险而接受的取舍。设计记录见
  [`execution-stop-token-allocator-design.md`](../roadmap/execution-stop-token-allocator-design.md)。
- Scope-token surface 已改为 current-WD-shaped：
  `simple_counting_scope::token::wrap(sender)` 是 identity forwarding，
  `counting_scope::token::wrap(sender)` 会给 child env 暴露一个 fused stop token：
  scope stop 和下游 receiver/env stop 任一请求都会让 child 观察到 stop。scope
  association 由 top-level `associate(sender, token)`、`spawn(sender, token[, env])`
  和 `spawn_future(sender, token[, env])` 持有。`spawn` 只接受 completion signatures
  为 `set_value()` / `set_stopped()` 的 sender；会产生 value 或 error completion 的
  sender 需要先转换成无 error、无 value 的形态。fused token 使用共享 control block，
  因此 `read_env(get_stop_token)` 把 token 作为 value 返回时不会悬垂；父级 scope /
  downstream callback 只在 operation active 期间保持注册。
- `simple_counting_scope::join()` / `counting_scope::join()` 返回异步 sender，可用
  `sync_wait(scope.join())` 等 sender 消费方式等待 drain；`start()` 只注册 join
  operation，最后一个 scope association 释放时在锁外完成 join receiver。
- `forge::task` 在 coroutine `final_suspend` 中同步发出 receiver completion；自定义
  receiver 不应在 `set_value` / `set_error` / `set_stopped` 回调内同步销毁连接的 task
  operation-state。当前 coroutine bridge 把 stopped completion 作为内部异常回到 task
  frame；用户代码里的 `catch(...)` 可以捕获该取消路径。一旦 task promise 进入 stopped
  状态，后续异常不会覆盖 stopped completion；不要在 task body 中吞掉 catch-all 后继续
  假设可以报告普通 error。

Forge 自带 sender/receiver/scheduler 已优先采用当前 C++26 draft 的成员式定制（如
`connect` / `get_env` / `set_value` / `schedule`）。CPO 层仍保留 `tag_invoke`
fallback 以兼容既有自定义类型；新代码建议优先使用成员式定制。当原生 `<execution>`
可用时，整个 backport 自动禁用。

## 验证

`scripts/verify-native.sh tsan` 与 `scripts/verify-native.sh asan` 分别覆盖 execution
子集的 ThreadSanitizer 与 ASan+UBSan 路径；`gcc-exec` 覆盖 libstdc++ 上的
execution-only 路径。

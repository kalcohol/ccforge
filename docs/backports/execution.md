# `std::execution` backport 说明

当前为 P2300 senders/receivers 的 Phase 1-4 backport（Phase 4 部分功能）。

## 已实现

- Sender 工厂：`just`、`just_error`、`just_stopped`、`read_env`
- 适配器：`then`、`upon_error`、`upon_stopped`、`let_value`、`let_error`、
  `let_stopped`、`write_env`、`unstoppable`
- 调度器适配器：`starts_on`、`continues_on`、`on`、`affine`、`bulk` /
  `bulk_chunked` / `bulk_unchunked`（串行 subset）
- Forge 兼容扩展：`transfer_just`、`split`
- 组合器：`into_variant`、`when_all`（至少一个 child sender；完整笛卡尔积签名、外层取消传播）、
  `when_all_with_variant`、`split`、`associate`、`spawn`、`spawn_future`
- 消费者：`sync_wait`（单一 value completion 返回 `optional<tuple<...>>`，多组
  value completions 返回 `optional<variant<tuple<...>, ...>>`）、
  `sync_wait_with_variant`（均通过 `std::this_thread`）
- Stopped 工具：`stopped_as_optional`、`stopped_as_error`
- 调度器：`inline_scheduler`、`run_loop`（mutex+cv，跨工具链可移植）
- Stop tokens：`inplace_stop_source/token/callback`、`never_stop_token`、`any_stop_token`（类型擦除）、stoppable concepts
- Coroutine 桥：`as_awaitable`、`with_awaitable_senders`（需要 C++20 coroutines；
  单一 value completion 保持返回 `tuple`，多组 value completions 返回
  `variant<tuple<...>, ...>`）；mixin 会保留普通 awaitable，并按 current WD
  提供 continuation / stopped 传播接口
- 基础设施：`completion_signatures_of_t`、`value_types_of_t`、`error_types_of_t`、
  `sends_stopped`、`enable_sender`、`std::forwarding_query`、
  `get_start_scheduler`、`get_delegation_scheduler`、`get_forward_progress_guarantee`、
  `get_completion_scheduler`、`get_completion_domain`、`get_await_completion_adaptor`、
  CPO 分发基础设施。各 adaptor 内部有私有 completion-signature transform helper，
  但当前不暴露 public `transform_completion_signatures` 名字
- 域调度：`default_domain`、`get_domain` CPO、receiver-env start-domain 选取、
  sender-env completion-domain 选取、`connect_t` recursive `transform_sender`
- Async scope subset：`simple_counting_scope`、`counting_scope`（独立 stop-aware scope）

## 当前限制

- 支持的 public include 是 `<execution>`，并应通过 `forge::std` / `forge::forge`
  target 消费。`backport/cpp26/execution/*.hpp` 是实现 fragment，不是独立公共入口；
  直接 include fragment（尤其先于平台 `<execution>`）不受支持，可能绕过 wrapper
  prelude / feature decisions，并在 libc++ inline namespace 或
  `FORGE_HAS_NATIVE_EXECUTION_POLICIES` 等配置上形成跨 TU 不一致。
- Receiver completion callbacks 当前必须为 `noexcept`，包括 `set_value`、`set_error` 和
  `set_stopped`；throwing completion callbacks 尚不支持，并由配置期 negative compile
  probe 覆盖。
- `inplace_stop_callback` 当前为保持 Forge 已验证的 callback/source 重入销毁路径，使用
  allocator-neutral control block。因此 registration 会分配，constructor 尚未满足
  current WD 对 nothrow callback 的条件 `noexcept`。不要在 `noexcept` 路径中假设注册
  不会失败；收敛方案和实现门槛见
  [`inplace-stop-callback-design.md`](../roadmap/inplace-stop-callback-design.md)。
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
- `continues_on` 当前只实现单一 value completion shape 的 transfer subset；value
  参数会 decay-copy 到调度 hop 的 operation state。`affine(sender)` / `sender | affine`
  从 receiver environment 的 `get_start_scheduler` 取得目标 scheduler，并通过
  unstoppable schedule sender 返回该 execution resource；它继承同一个单一 value
  shape 限制。多 value-alternative 和引用 value-signature 的逐位 WD 语义需要单独
  重构，不应从当前 subset 推断。
  `continues_on` 只发布能够保证的 value completion scheduler attribute；目标 scheduler
  自身的 error / stopped completion 可能在未转移的 agent 上发生，因此不为这两个
  disposition 声称唯一 completion scheduler。
- `schedule_from`、`apply_sender` 和 `transform_env` 尚未实现。它们需要与 domain
  customization 一起设计，不能用只转发到 `continues_on` 的同名空壳代替。
- `split` 是保留的非 WD extension。它缓存单一 value completion shape，并以 `const&`
  向每个订阅者广播缓存值；内部订阅者 callback 入链分配失败时以
  `set_error(std::exception_ptr)` 完成。它没有实现完整
  stop-source/on_done cycle，abandoned never-completing source 的取消不是当前保证。
- `ensure_started` / `start_detached` 不再由 `<execution>` backport 暴露；这两个名字不是
  当前 working draft `[exec]` surface。需要 fire-and-forget 时，standard-shaped code 应
  使用 scope-token based `spawn(sender, token[, env])`；Forge runtime extension 侧保留
  `forge::start_detached(sender)`。
- `sync_wait` 会把 `set_error(std::exception_ptr)` 原样 rethrow，把
  `set_error(std::error_code)` 映射为保留原 code 的 `std::system_error`；其他 typed error
  会先包装进 `std::exception_ptr` 再 rethrow，因此调用方需要按原 error 类型捕获。若需要同步消费
  value / stopped / closed-set typed error 而不抛异常，使用 Forge 扩展层的
  `forge::wait_result(sender)`。
- Current-WD 拼写是 `std::this_thread::sync_wait` 与
  `std::this_thread::sync_wait_with_variant`。Forge backport 暂时也在
  `std::execution` 中 re-export 这两个对象以兼容旧源码，但 portable consumer、文档与
  示例不依赖该扩展别名。
- `spawn_future` 当前返回 move-only single-consumer future sender；其 shared-state 和
  consumer record 分配会使用 `env` 中的 `get_allocator`。下游 stop-token 会通过
  `any_stop_token` 类型擦除注册 callback；该标准形状的类型擦除层内部 control block 保持
  allocator-neutral，这是为了降低 native handoff 风险而接受的取舍。设计记录见
  [`execution-stop-token-allocator-design.md`](../roadmap/execution-stop-token-allocator-design.md)。
  丢弃未消费的 future sender，或销毁尚未 `start()` 的 connected consumer operation，
  都会请求停止 eager producer 并最终释放 scope association。Child 的 value/error
  completion 会先存为 decay-copy，再以 rvalue 交给 future receiver；future 公开的
  completion signatures 也使用这些 decayed types，不保留 child 的引用类别。
  Consumer record 在 future sender 的 `connect()` 中分配，因此 `connect()` 可以因
  allocator 或 receiver construction 失败而抛出；不要把该 backport 路径放进假设
  `connect()` 无条件 `noexcept` 的代码。Connected consumer 与 shared state 在 active
  期间有短暂的双向 strong ownership，terminal completion、未启动 operation 析构和
  abandon 路径负责切断该关系；不要绕过这些公开 ownership 路径手工拆状态。
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
  operation。若 scope 已空，join 在 `start()` 调用线程内联完成；否则最后一个
  association 释放后，completion 会调度到 receiver env 的
  `get_start_scheduler`，并在 scope mutex 外执行。join completion 会建立 terminal
  `joined` state；此后 `try_associate()` 返回 disengaged
  association，`spawn()` 不再接受新 work。Scope destructor 仍保留 Forge 既有的宽松
  diagnostic：只在 outstanding association count 非零时 terminate；已经自然 drain
  但未消费 `join()` 的 scope 不额外终止。
- `forge::task` 在 coroutine `final_suspend` 中同步发出 receiver completion；自定义
  receiver 不应在 `set_value` / `set_error` / `set_stopped` 回调内同步销毁连接的 task
  operation-state。当前 coroutine bridge 把 stopped completion 作为内部异常回到 task
  frame；用户代码里的 `catch(...)` 可以捕获该取消路径。一旦 task promise 进入 stopped
  状态，后续异常不会覆盖 stopped completion；不要在 task body 中吞掉 catch-all 后继续
  假设可以报告普通 error。

Forge 自带 sender/receiver/scheduler 已优先采用当前 C++26 draft 的成员式定制（如
`connect` / `get_env` / `set_value` / `schedule`）。一参数 environment/scheduler query
也采用 member `.query(cpo)` 优先、`tag_invoke` fallback；组合 environment 保持这一
优先级和 leftmost-wins。新代码建议优先使用成员式定制。当原生 `<execution>` 可用时，
整个 backport 自动禁用。

## 验证

`scripts/verify-native.sh tsan` 与 `scripts/verify-native.sh asan` 分别覆盖 execution
子集的 ThreadSanitizer 与 ASan+UBSan 路径；`gcc-exec` 覆盖 libstdc++ 上的
execution-only 路径。

# `std::execution`

当前为 P2300 senders/receivers 的 Phase 1-4 backport（Phase 4 部分功能）。

## 已实现

- Sender 工厂：`just`、`just_error`、`just_stopped`、`read_env`
- 适配器：`then`、`upon_error`、`upon_stopped`、`let_value`、`let_error`、`let_stopped`、`write_env`
- 调度器适配器：`starts_on`、`continues_on`（schedule_from）、`transfer_just`、`bulk`（串行）
- 组合器：`into_variant`、`when_all`（完整笛卡尔积签名、外层取消传播）、`when_all_with_variant`、`split`、`associate`、`spawn`、`spawn_future`
- 消费者：`sync_wait`（单一 value completion 返回 `optional<tuple<...>>`，多组 value completions 返回 `optional<variant<tuple<...>, ...>>`）、`sync_wait_with_variant`（均通过 `std::this_thread`）
- Stopped 工具：`stopped_as_optional`、`stopped_as_error`
- 调度器：`inline_scheduler`、`run_loop`（mutex+cv，跨工具链可移植）
- Stop tokens：`inplace_stop_source/token/callback`、`never_stop_token`、`any_stop_token`（类型擦除）、stoppable concepts
- Coroutine 桥：`as_awaitable`、`with_awaitable_senders`（需要 C++20 coroutines；单一 value completion 保持返回 `tuple`，多组 value completions 返回 `variant<tuple<...>, ...>`）
- 基础设施：`completion_signatures_of_t`、`enable_sender`、`get_completion_scheduler`、`get_completion_domain`、`transform_completion_signatures`、CPO 分发基础设施
- 域调度：`default_domain`、`get_domain` CPO、receiver-env late domain 选取、`connect_t` domain `transform_sender` / `transform_env` wrapper
- Async scope subset：`simple_counting_scope`、`counting_scope`（独立 stop-aware scope）

## 当前限制

- Receiver completion callbacks 当前必须为 `noexcept`，包括 `set_value`、`set_error` 和 `set_stopped`；throwing completion callbacks 尚不支持，并由配置期 negative compile probe 覆盖。
- Library-provided sender 的 `connect_t` 提供 rvalue 移动路径、copyable lvalue 拷贝路径，以及 non-copyable non-const lvalue 的 destructive-move 连接路径。连接 non-copyable lvalue 后不应再复用该 sender；const non-copyable lvalue 仍不可连接。该 destructive-move lvalue 路径是 backport-only 便利；原生 C++26 实现下应显式传入 `std::move(sndr)`。
- Execution domain 支持仍是 draft 子集：`connect_t` 已按 receiver env 选取 start domain，并支持 scheduler-derived completion domain、`transform_sender` recovery 和 `transform_env` wrapper；scheduler-derived domain 仅在 scheduler 显式定制 `get_completion_domain` 时生效，否则会回退到 `default_domain`；完整标准递归 `transform_sender` 分发模型尚未实现。
- `ensure_started` / `start_detached` 不再由 `<execution>` backport 暴露；这两个名字不是当前 working draft `[exec]` surface。需要 fire-and-forget 时，standard-shaped code 应使用 scope-token based `spawn(sender, token[, env])`；Forge runtime extension 侧保留 `forge::start_detached(sender)`。
- `sync_wait` 会把 `set_error(std::exception_ptr)` 原样 rethrow；其他 typed error 会先包装进 `std::exception_ptr` 再 rethrow，因此调用方需要按原 error 类型捕获。若需要同步消费 value / stopped / closed-set typed error 而不抛异常，使用 Forge 扩展层的 `forge::wait_result(sender)`。
- `spawn_future` 当前返回 move-only single-consumer future sender；其 shared-state 分配会使用 `env` 中的 `get_allocator`，但 consumer/callback 辅助分配尚未完整 allocator-aware。
- Scope-token surface 已改为 current-WD-shaped：`simple_counting_scope::token::wrap(sender)` 是 identity forwarding，`counting_scope::token::wrap(sender)` 只注入 scope stop token；scope association 由 top-level `associate(sender, token)`、`spawn(sender, token[, env])` 和 `spawn_future(sender, token[, env])` 持有。`spawn` 只接受 completion signatures 为 `set_value()` / `set_stopped()` 的 sender；会产生 value 或 error completion 的 sender 需要先转换成无 error、无 value 的形态。
- `simple_counting_scope::join()` / `counting_scope::join()` 已返回 sender，可用 `sync_wait(scope.join())` 等 sender 消费方式等待 drain；实现目前仍以 start 时阻塞等待为主，完整异步 join-state 模型尚未接入。
- `forge::task` 在 coroutine `final_suspend` 中同步发出 receiver completion；自定义 receiver 不应在 `set_value` / `set_error` / `set_stopped` 回调内同步销毁连接的 task operation-state。

Forge 自带 sender/receiver/scheduler 已优先采用当前 C++26 draft 的成员式定制（如 `connect` / `get_env` / `set_value` / `schedule`）。CPO 层仍保留 `tag_invoke` fallback 以兼容既有自定义类型；新代码建议优先使用成员式定制。当原生 `<execution>` 可用时，整个 backport 自动禁用。

## 验证

`scripts/verify-native.sh tsan` 与 `scripts/verify-native.sh asan` 分别覆盖 execution 子集的 ThreadSanitizer 与 ASan+UBSan 路径；`gcc-exec` 覆盖 libstdc++ 上的 execution-only 路径。

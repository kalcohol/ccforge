# `std::execution` backport 说明

当前为 P2300 senders/receivers 的实用 current-draft subset；精确覆盖与偏差见下文及
[`execution-conformance-ledger.md`](../roadmap/execution-conformance-ledger.md)。

## 如何使用

包含 `<execution>` 并链接 `forge::std`（需要扩展时使用 `forge::forge`），采用 C++23 或更新模式。

```cpp
#include <execution>
auto work = std::execution::just(21)
    | std::execution::then([](int n) { return n * 2; });
auto result = std::this_thread::sync_wait(std::move(work));
// result holds tuple<int> on value completion.
```

## 特性宏

包含 `<execution>` 后 backport 不定义 `__cpp_lib_senders`，不能据此宣称完整标准实现。

## Native 让位

统一策略见 [native handoff](../native-handoff.md)。`FORGE_FORCE_SENDERS_BACKPORT` 只用于诊断，具有 ODR 风险。

## 覆盖范围

### 已实现

- Sender 工厂：`just`、`just_error`、`just_stopped`、`read_env`
- 适配器：`then`、`upon_error`、`upon_stopped`、`let_value`、`let_error`、
  `let_stopped`、`write_env`、`unstoppable`
- 调度器适配器：`starts_on`、`continues_on`、`on`、`affine`、`bulk` /
  `bulk_chunked` / `bulk_unchunked`（串行 subset）
- Forge 兼容扩展：`transfer_just`、`split`
- 组合器：`into_variant`、`when_all`（至少一个 child sender；完整笛卡尔积签名、外层取消传播）、
  `when_all_with_variant`、`associate`、`spawn`、`spawn_future`
- 消费者：`sync_wait`（单一 value completion 返回 `optional<tuple<...>>`，多组
  value completions 返回 `optional<variant<tuple<...>, ...>>`，零组返回
  `optional<tuple<>>`；WD 要求恰好一组 value completion signature，接受零/多组
  是 Forge 扩展。结果类型按 run_loop 环境 `sync_wait_env` 推导，该 env 暴露
  `get_scheduler` / `get_start_scheduler` / `get_delegation_scheduler`）、
  `sync_wait_with_variant`（均通过 `std::this_thread`）
- Stopped 工具：`stopped_as_optional`、`stopped_as_error`
  `stopped_as_optional` 要求恰好一个非 `void` value completion signature；单一签名
  的多个参数形成 `optional<tuple<...>>`。无 value、零参数 value 或多个 value 候选
  不满足该约束。当前实现将 shape 验证延迟到实际环境下的 completion-signature
  查询或 `connect`；创建 adaptor 时不猜测 `empty_env`。这是一项已接受的 creation
  Mandate 延迟偏差，尚未实现 WD `make-sender` 的无环境 / 环境依赖分类。
- 调度器：`inline_scheduler`、`run_loop`（mutex+cv，跨工具链可移植）
- Stop tokens：`inplace_stop_source/token/callback`、`never_stop_token`、stoppable concepts
- Coroutine 桥：`as_awaitable`、`with_awaitable_senders`（需要 C++20 coroutines；
  按 current WD 形状，单一 value completion 的零/一/多值分别产生
  `void` / `T` / `tuple<...>`，多组 value completion alternatives 不满足
  awaitable 约束）；mixin 会保留普通 awaitable，并按 current WD
  提供 continuation / stopped 传播接口。`as_awaitable` 按 direct/transformed
  `as_awaitable` member、普通 awaiter（含 member/free `operator co_await`）、
  sender bridge 和 identity fallback 的顺序分派
- 基础设施：`completion_signatures_of_t`、`value_types_of_t`、`error_types_of_t`、
  `sends_stopped`、`enable_sender`、`std::forwarding_query`、
  `get_start_scheduler`、`get_delegation_scheduler`、`get_forward_progress_guarantee`、
  `get_completion_scheduler`、`get_completion_domain`、`get_await_completion_adaptor`、
  CPO 分发基础设施。各 adaptor 内部有私有 completion-signature transform helper，
  但当前不暴露 public `transform_completion_signatures` 名字
- 域调度：`default_domain`、`get_domain` CPO、receiver-env start-domain 选取、
  sender-env completion-domain 选取、public recursive `transform_sender`、
  domain-first/tag-fallback `apply_sender`
- Async scope subset：`simple_counting_scope`、`counting_scope`（独立 stop-aware scope）

## 限制与偏差

### 当前限制

- 支持的 public include 是 `<execution>`，并应通过 `forge::std` / `forge::forge`
  target 消费。`backport/cpp26/execution/*.hpp` 是实现 fragment，不是独立公共入口；
  直接 include fragment（尤其先于平台 `<execution>`）不受支持，可能绕过 wrapper
  prelude / feature decisions，并在 libc++ inline namespace 或
  `FORGE_HAS_NATIVE_EXECUTION_POLICIES` 等配置上形成跨 TU 不一致。
- 工作草案 MANDATE-NOTHROW 与 [exec.recv.concepts] 要求完成函数
  `set_value`、`set_error`、`set_stopped` 不抛异常，receiver 可不抛异常地移动。
  这些是标准要求；配置期负面编译探测固定这些规则。
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
  recursive `transform_sender`；public `transform_sender(sender, env)`、`connect_t` 与
  completion-signature computation 共用这套顺序。`default_domain` 优先调用 sender tag
  的 tagged transform，`apply_sender` 优先调用显式 domain customization，再回退到
  algorithm tag customization。旧的两参数 domain transform 仍作为 Forge source-
  compatibility extension 保留，不代表 current-WD spelling。Scheduler-derived start
  domain 仅在 scheduler 显式定制
  `get_completion_domain<set_value_t>` 时生效，否则会回退到 `default_domain`。
  `get_domain` 与双参数 `get_completion_domain` 查询均采用 member-query-first、
  tag-invoke fallback；查询只选择 domain 类型，并按 current WD 默认构造该类型，
  不保留 query 返回对象携带的运行时状态。sender attributes 只跨 adaptor 边界转发声明为 `forwarding_query` 且默认 query 可常量求值为 true 的查询。详细分类边界见
  [环境查询分类](../roadmap/execution-conformance-ledger.md#实际-query-分类)。
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
  `continues_on` 为转移后的 value 和 stopped completion 发布 destination scheduler
  attribute。调度失败的 error completion 允许发生在 unspecified agent，因此不为
  error disposition 声称唯一 completion scheduler；completion domain 同样只从
  destination scheduler 或其 schedule sender 派生，不借用 child domain。
- `starts_on` 和 `associate` 在 `connect` 阶段构造 child operation；构造失败由
  `connect` 抛出，不会扩大 sender 的 completion signatures。`let_*` 与
  `continues_on` 必须在收到上游 completion 后选择并构造动态 child/schedule
  operation，因此当前实现会为这些真实的 late-connect 失败保留
  `set_error(std::exception_ptr)` 通道。`starts_on` 的 source child environment 以
  目标 scheduler 覆盖 `get_scheduler` 和 `get_start_scheduler`；目标 scheduler 有
  直接 `get_domain` 定制时优先使用它，否则保留 outer environment 的 domain 查询及
  现有 scheduler-derived domain 选取，不额外插入会遮蔽这些路径的 `default_domain`。
  其余 forwarding query 继续来自 outer receiver environment。
- `schedule_from` 是 current-WD 的单参数 departure marker sender，`continues_on`
  通过它让 source completion domain 定制离开当前 execution resource 的方式。
  `transform_env` 已不在当前 working draft surface，因此本 backport 不暴露该旧拼写。
- Live draft 已包含 `std::execution::task`、`task_scheduler` 和 `with_error`，但 Forge
  不 backport 这组 coroutine task。[P3552R3](https://wg21.link/P3552R3) 已进入 working
  paper，随后 [P4007R3](https://wg21.link/P4007R3) 仍记录
  task family 的开放问题，并区分其中不能在发布后兼容修正的项目；在 wording、ownership、
  allocator、stop/error 和 native-handoff 模型形成独立任务书前，`forge::task` 与
  `forge::io::io_task` 都只是不同契约的 Forge extensions，不能冒充这组标准类型。
- `as_awaitable` 是 sender-to-awaitable 方向的桥接，不代表反方向也已实现。普通 awaitable
  当前不会自动满足 backport 的 `sender`，`get_completion_signatures` 不会从其
  `await_resume` 合成 signatures，`connect` 也没有 current-WD 的
  `connect-awaitable` fallback。不要直接将普通 awaitable 传给 `sync_wait` 等 sender
  consumer；需要此能力时应单独验证 coroutine operation 的 ownership、异常、stopped
  和 completion 内自毁契约，再实现这条独立路径。
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
  consumer record 分配会使用 `env` 中的 `get_allocator`。下游 stop-token callback
  直接按 receiver env 返回的具体 token 类型注册，不要求 native execution 提供额外的
  erased-token vocabulary。Forge runtime 自己的类型擦除取舍见
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
  stop callback 在 `start()` 内注册，并在转交 terminal completion 前注销；注册分配失败
  会清理已注册的 callback 并交付已声明的 `set_stopped()`，不启动 child。
- `simple_counting_scope::join()` / `counting_scope::join()` 返回异步 sender，可用
  `sync_wait(scope.join())` 等 sender 消费方式等待 drain；`start()` 只注册 join
  operation。若 scope 已空，join 在 `start()` 调用线程内联完成；否则最后一个
  association 释放后，completion 会调度到 receiver env 的
  `get_start_scheduler`，并在 scope mutex 外执行。join completion 会建立 terminal
  `joined` state；此后 `try_associate()` 返回 disengaged
  association，`spawn()` 不再接受新 work。两个 scope 的 destructor 遵循
  [working draft](https://eel.is/c++draft/exec.simple.counting.ctor)：仅允许
  `joined`、`unused` 或 `unused-and-closed` state，否则调用 `terminate`，包括
  count 已自然 drain 到零但未消费 `join()` 的情况。Destructor 在 mutex 下检查
  state，与最后一次 association decrement 的 critical section 同步。
- `forge::task` 的完成与取消契约见[工具参考](../forge-utilities.md#两种协程任务)。

Forge 自带 sender/receiver/scheduler 已优先采用当前 C++26 draft 的成员式定制（如
`connect` / `get_env` / `set_value` / `schedule`）。一参数 environment/scheduler query
采用 member `.query(cpo)` 优先、`tag_invoke` fallback。
`prop.query(q, args...)` 以 `const Value&` 返回属性且为 `noexcept`；
[`env.query(q, args...)`](https://eel.is/c++draft/exec.env) 的参与约束使用
`e.query(QueryTag(), args...)`；默认模式按声明顺序选择首个可经兼容 dispatch
（member 优先、`tag_invoke` fallback）回答的子环境，因此混合子环境的普通 CPO 调用
保持原有 scheduler / stop-token 选择。strict 测试模式仅选择能回答具名左值 `q`
的成员查询。借用子环境保持 referent 的 cv/ref 限定；结果保留引用类型，条件 `noexcept` 根据所选调用的实际表达式计算。
`env` 的旧 `tag_invoke` 接口继续保留。
`sync_wait` env 与 `let` / `when_all` helper env 提供 const member
`query`；`when_all` 只转发一参数 forwarding query。
`run_loop` / `inline_scheduler` attrs、`continues_on` attrs、`write_env` /
`counting_scope` / `spawn_future` env 仍采用已有兼容接口；其成员查询、scheduler 与 schedule attrs 的
查询一致性、环境相关 inline attrs 和 `FWD-ENV` 过滤仍有待补齐。
`prop` / `env` 的 assignability 等其他 surface 差异仍属于现有 subset 边界。
新代码建议优先使用成员式定制。原生 P2300 senders 被检测到时让位，详见 [native handoff](../native-handoff.md)。


### 分配与可移植性边界

内部 operation state 的小缓冲容量取决于 adaptor：例如 `let_*` 使用 1024 字节缓冲，`when_all` 的每个 child 使用 512 字节缓冲，其 stop callback 缓冲为 256 字节。`__op_storage` 只有在对象大小不超过缓冲容量、且对齐要求不超过缓冲对齐（默认 `alignof(std::max_align_t)`）时才内联存储；任一条件不满足就通过 `new` / `delete` 使用全局堆。这些内部路径不保证遵从 `get_allocator(env)`。`counting_scope` 的融合 stop 状态、stop callback 控制块也使用全局分配。
只有 `schedule_from` 提供结构化访问；其他库 sender 的 `tag_of_t` 无法成立，自定义 domain 不能按工作草案方式匹配它们。
`simple_counting_scope::count()` 和 `is_closed()` 是 Forge 诊断扩展，不可移植。

## 示例

示例门控与所有文件见[示例索引](../examples.md)。只在注入或完整原生时构建；明确依赖扩展者另列限制。验证流程见[测试说明](../testing.md)，测试组为 `FORGE_TEST_ENABLE_EXECUTION`。

- [execution_affine_example.cpp](../../example/execution_affine_example.cpp)：execution affine 的接口组合与完成结果。

- [execution_combinators_example.cpp](../../example/execution_combinators_example.cpp)：when_all、结果复用、bulk 与 scope spawn（Forge 扩展拼写）。

- [execution_coro_example.cpp](../../example/execution_coro_example.cpp)：execution coro 的接口组合与完成结果。

- [execution_example.cpp](../../example/execution_example.cpp)：execution 的接口组合与完成结果。

- [execution_let_example.cpp](../../example/execution_let_example.cpp)：execution let 的接口组合与完成结果。

- [execution_on_example.cpp](../../example/execution_on_example.cpp)：execution on 的接口组合与完成结果。

- [execution_schedulers_example.cpp](../../example/execution_schedulers_example.cpp)：execution schedulers 的接口组合与完成结果。

- [execution_scope_example.cpp](../../example/execution_scope_example.cpp)：scope spawn 与异步 join。

- [execution_spawn_future_example.cpp](../../example/execution_spawn_future_example.cpp)：execution spawn future 的接口组合与完成结果。

- [execution_stop_token_example.cpp](../../example/execution_stop_token_example.cpp)：execution stop token 的接口组合与完成结果。

- [execution_unstoppable_example.cpp](../../example/execution_unstoppable_example.cpp)：execution unstoppable 的接口组合与完成结果。

`sender_t`、`receiver_t`、`operation_state_t`、`scheduler_t` 是旧兼容别名；新代码使用对应的 `_tag` 名。`forge::any_stop_token` 是非标准擦除，分配不遵从 env allocator，不能用作标准 stop-token API 的替代承诺。

## 验证

`scripts/verify-native.sh tsan` 与 `scripts/verify-native.sh asan` 分别覆盖 execution
子集的 ThreadSanitizer 与 ASan+UBSan 路径；`gcc-exec` 覆盖 libstdc++ 上的
execution-only 路径。

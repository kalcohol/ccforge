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
- Forge 兼容扩展：`forge::transfer_just`、`forge::split`；旧 `std::execution` 入口已弃用
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
- `continues_on` 保留 predecessor 的全部 value completion shapes，并将 value / error
  datums decay-copy 到去重的 tagged tuple `variant`。只有某个 datum 的 decay-copy
  可能抛出时才额外声明 `set_error_t(std::exception_ptr)`；scheduler sender 声明的 error
  和 stopped completions 也会加入结果 signatures。scheduler operation 在 `connect`
  阶段构造，连接失败直接从 `connect` 抛出；decay-copy 失败则缓存 `exception_ptr`，
  仍经过 scheduler hop 后交付，scheduler 的 error / stopped 优先于缓存结果。
  `affine(sender)` / `sender | affine` 从 receiver environment 的 `get_start_scheduler`
  取得目标 scheduler，通过 unstoppable schedule sender 复用同一 transfer 路径，
  因而也保留多种 value shapes，并采用相同的条件 `exception_ptr` 规则。
  [`continues_on`](https://eel.is/c++draft/exec.continues.on) 的 attributes 通过成员
  `query` 为 value 和 stopped completion 发布 destination scheduler。调度失败的
  error completion 允许发生在 unspecified agent，因此不为 error disposition
  声称唯一 completion scheduler 或 domain。value / stopped 及默认 tag 的
  completion-domain 成员优先查询 destination schedule sender attributes，未命中时
  查询 destination scheduler；默认模式保留 member-first / tag-invoke fallback。
  所有 completion-scheduler / completion-domain tags 均从 child 转发入口排除；
  destination 查询不可用时不会回退到 child domain，包括 value、stopped、error 和
  默认 tag。其余 child attributes 只经 `forwarding_query` 过滤后 variadic 转发，
  保留引用答案与条件 `noexcept`。目标 domain 查询的参与条件、异常规格、分派与公开
  CPO 的 completion-scheduler 回退，以及两条内部 receiver 路径与 signatures 推导均使用
  [`FWD-ENV`](https://eel.is/c++draft/exec.snd.expos) 的过滤视图。receiver 的按值环境
  由视图持有，借用环境保留引用。过滤视图的具体类型无需等于原环境类型，forwarding query 的
  引用返回类型与对象身份保持不变。
- `starts_on` 和 `associate` 在 `connect` 阶段构造 child operation；构造失败由
  `connect` 抛出，不会扩大 sender 的 completion signatures。`let_*` 在收到上游
  completion 后选择并构造 successor operation，因此仍为 late-connect 失败保留
  `set_error(std::exception_ptr)` 通道。`starts_on` 的 source child environment 以
  目标 scheduler 覆盖 `get_scheduler` 和 `get_start_scheduler`；目标 scheduler 有
  直接 `get_domain` 定制时优先使用它，否则保留 outer environment 的 domain 查询及
  现有 scheduler-derived domain 选取，不额外插入会遮蔽这些路径的 `default_domain`。
  其余 forwarding query 继续来自 outer receiver environment。
- [`let_value`、`let_error`、`let_stopped`](https://eel.is/c++draft/exec.let) 的 child
  environment 按对应 completion tag 依次选择：从 predecessor attributes 与 receiver 的
  `FWD-ENV` 查询 completion scheduler，并构造 `SCHED-ENV`；若该查询不可用，则以相同的
  attributes 和 `FWD-ENV` 查询 completion domain；两者均不可用时使用空环境。
  带环境参数的 scheduler 查询优先，只有 unary 查询的 attributes 仍通过 CPO 的
  `TRY-QUERY` 回退提供 scheduler。选中的 scheduler 按值保存，并作为 child 的
  `get_start_scheduler` 答案；因此 inline schedule predecessor 使用 receiver 环境中的
  scheduler，run-loop predecessor 则保留其所属 loop 的 scheduler。receiver env 缺少
  `get_scheduler` 时，inline predecessor 在 strict 模式选择 completion domain；默认模式
  仍可通过旧 unary 查询选择 inline scheduler。Child 查询优先访问选中的
  环境，未命中时仅从 receiver environment 转发 forwarding queries。
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
  `std::execution` 中保留这两个已在文档中弃用的对象别名以兼容旧源码，但 portable consumer、文档与
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
[`write_env`](https://eel.is/c++draft/exec.write.env) 的 child env 提供一参数 const member
`query`：先查询 overlay，未命中时仅转发 receiver env 中满足 `forwarding_query` 的查询。
因此 base 的本地查询不可见，overlay 提供的本地查询仍可见；结果引用与条件 `noexcept` 保留。
[`inline_scheduler`](https://eel.is/c++draft/exec.inline.scheduler) 与 schedule sender attrs
提供相同成员查询：带一个环境参数的 value completion scheduler / domain 分别按
`get_scheduler(env)` / 草案 `get_domain(env)` 的规则选择。旧的一参数 scheduler self-answer 仅通过
tag-invoke 保留为默认模式的兼容扩展，不属于成员查询，strict 模式不接受它；
环境相关查询优先使用传入环境。该环境没有 scheduler 时，strict 模式的 `let` 使用
completion-domain 分支，默认模式仍可通过 unary 回退选择 inline scheduler。
[`run_loop`](https://eel.is/c++draft/exec.run.loop.types) scheduler 与 schedule attrs 均通过
成员提供 value / stopped completion scheduler，并返回同一 loop 的 scheduler；不提供
error completion scheduler 或 unary completion-domain 成员。带环境参数时，attrs 的
completion-domain 成员转发到 scheduler CPO，两侧均得到 `default_domain`；
该 CPO 的 error-tag fallback 可用性不代表 schedule sender 提供 error completion。
两个 scheduler 的 progress 查询也有成员入口。Completion-scheduler CPO 支持带环境参数的
直接查询，未命中时按
`TRY-QUERY` 回退至一参数查询；原有一参数 dispatch 顺序不变。该入口未实现草案完整的
scheduler 递归解析与 scheduler 默认回退，仍属于现有 query subset。
`get_domain` 从 scheduler 派生 domain 时保留原环境类型，在完整重载集合中按成员优先、
tag-invoke 回退选择答案；精确环境成员与 ADL 重载均保留相对于同类通用回退的优先级，
strict 模式仍只考虑成员。原环境类型的保留属于兼容扩展。Inline completion-domain 成员
在内部按草案的 `get_domain` 顺序选择类型：先取环境的直接 domain，再用
[`HIDE-SCHED`](https://eel.is/c++draft/exec.queries.expos) 环境查询 scheduler completion domain，
最后取 `default_domain`。隐藏环境屏蔽 `get_scheduler` / `get_domain`，使 inline 及转发其查询的
scheduler 终止递归，同时保留外层自定义重载的选择。
`counting_scope` / `spawn_future` 的 stop-when environment 现已提供成员查询，
只转发 `FWD-ENV` queries，并保持融合 stop token 的覆盖优先级。
`continues_on` attributes 与两条内部 receiver environment 均支持成员 `query`；
completion tags 只由 destination 提供，不参与 child attribute 转发。
默认与 strict 模式均检查目标查询缺失时的
[`get_completion_domain`](https://eel.is/c++draft/exec.get.compl.domain) 回退，避免 child domain 泄漏。
`prop` / `env` 的 assignability 等其他 surface 差异仍属于现有 subset 边界。
新代码建议优先使用成员式定制。原生 P2300 senders 被检测到时让位，详见 [native handoff](../native-handoff.md)。


### 分配与可移植性边界

`let_value` / `let_error` / `let_stopped` 将 predecessor operation state 直接内联存储，
分别以去重后的 `variant` 保存 decayed 参数 tuples 和所有可能的 successor operation state
的 typed holders，两个 `variant` 均以 `monostate` 初始化。holder 以普通数据成员保存 operation state，
由 factory prvalue 直接初始化，避免空且不可移动的类型作为潜在重叠子对象时失去 guaranteed copy elision。
参数与 successor 在对应 completion 到达后原位构造；
successor 和参数先于 predecessor 析构，保证同步完成路径中的引用生命周期。
参数 decay-copy、函数调用和 successor `connect` 的潜在异常通过
`set_error(std::exception_ptr)` 报告；现有 completion signatures 保留该通道。

`when_all` 在静态类型的 `tuple` 中保存各 child 的 typed holder，
child operation state 在 holder 的构造函数中直接初始化为普通数据成员，
values 以 decayed tuple 的 `optional<variant<...>>` 保存（保留多 value shape 扩展），
errors、stop source 和具体类型的 `optional` stop callback 均在 operation state 内存储。
stop callback 在启动任何 child 前注册，并在向下游发送 completion 前注销；
child `connect` 抛出时，已构造的 children 随 tuple 构造展开而析构。
上述 `let_*` / `when_all` 的 operation-state storage 本身不再分配，
不依赖固定容量或 `std::max_align_t` 对齐上限，也不会因 operation state 或 callback
对象的大小、对齐要求而回退到全局分配；所需空间计入具体 operation state 的大小与对齐要求。
在 senders、schedulers、receivers、environment queries、结果类型及用户函数均不分配的前提下，
这些组合的 `connect`、`start` 和 completion 路径保留的全局分配仅来自
`inplace_stop_callback`：每次在 stoppable `inplace_stop_token` 上注册 callback，
仍按既有设计创建一个独立的 control block。
`when_all` 仅在 receiver token 的 `stop_possible()` 为 `true` 时注册；
嵌套 `when_all` 会在 parent 的 stop source 上注册，因此仍有相应的 control-block 分配。
没有此类注册时，上述路径无需全局分配；自定义 stop token 的 callback 构造若自行分配，
也须单独计入。control block 的设计约束见
[`inplace_stop_callback` 设计记录](../roadmap/inplace-stop-callback-design.md)。

`continues_on` 将 scheduler operation 和 predecessor operation 作为普通成员直接构造，
结果用 tagged decayed tuples 的 `variant` 原位保存；`schedule_from` 只转发 departure
marker 的 child，不增加独立缓冲。`starts_on` 以普通成员保存 scheduler operation 和
child operation，仍在 outer `connect` 中预连接 child；这保留现有异常传播方式，
与 working draft 的 `let_value` 展开在 child 连接时机上仍有差异。
`on` 的两种形式将其组合 sender 的 operation 作为普通成员从 factory prvalue
直接初始化。这些路径不再使用大小或对齐不足时分配的缓冲，支持大于固定缓冲容量、
over-aligned 以及空的 immovable operation states；空间与对齐需求计入外层 operation。
在组成对象及其查询、结果构造不自行分配时，唯一保留的分配边界仍是组合中实际发生的
stop callback registration；`inplace_stop_callback` 的 control block 规则不变。

所有 adaptor 均使用 concrete operation type 的原位存储，已移除 `__op_storage`
小缓冲及其按大小、对齐触发的全局 `new` 回退。`associate` 只在 association 成功时
构造 union 中的 typed holder；holder 内的 operation 是普通成员，不依赖向
potentially-overlapping subobject 的 copy elision。`join()` 的 schedule operation、
`split` shared state 的 child operation、`spawn` / `spawn_future` state 的 spawned
operation 均为普通成员，支持 immovable、超过 4 KiB 及 over-aligned operation。

`spawn` 和 `spawn_future` 的 allocator 均按以下顺序选择：显式 environment 的
`get_allocator(env)`、`token.wrap(sndr)` 的 sender attributes 中的 allocator、
`std::allocator<void>`。第二种情况还向 spawned environment 注入所选 allocator；
缺省情况不额外添加 allocator query。`spawn` 用 rebound allocator 分配一个 state，
其 operation 无独立分配。`spawn_future` 用同一所选 allocator 分别分配生命周期 resource、
state 和 state 的 `shared_ptr` control block；连接 consumer 时用 `allocate_shared`
分配合并的 consumer record/control block。resource 持有 association，所有内部 allocator
副本共同保持其存活；consumer 在完成前解除与外层 operation 的存储所有权。
以上存储均不回退至另一 allocator；connect 或分配抛出时销毁已构造对象并回收内存。
state、consumer、全部 control block 以及 resource 自身均销毁并 deallocate 后，才释放
association；已完成的 consumer operation 可以继续存活，不保留上述分配。

`counting_scope::token::wrap` 使用内部 stop-when：operation 内持有
`inplace_stop_source`、所需的具体 token callback 和 child operation，不分配融合状态。
不可停止的 receiver token 直接使用 scope token；其他情况在连接 child 前注册两个输入，
在转发 completion 前注销。融合 token 的 `stop_requested()` 和 `stop_possible()` 分别返回
两个输入查询结果的逻辑或；child 在 connect 时注册的 callback 也可在 start 前收到通知，
且最多调用一次。注册时输入已请求停止，则在注册线程立即调用该 callback，即使输入的
其他 callback 仍在阻塞停止传播。当前 callback 注册失败保留 `set_stopped()` 完成路径。
completion 前先注销两个输入 callback；另一线程上的 callback invocation 返回后才继续，
随后按原 channel 和原参数引用直接转发，不保存参数、不增加分配，也不把 value/error
改成 stopped。同一线程的 stop propagation 内若 receiver 销毁 operation，仍依赖 backport
的独立 control block；采用标准 source lifetime 规则前必须解决这一边界。
scope 本身须活过其全部 stop 调用，包括使 join receiver 同步完成的调用。详见
[`inplace_stop_callback` lifetime 设计](../roadmap/inplace-stop-callback-design.md)。
`spawn_future` 的 producer environment 按
`write_env(stop-when(sndr, ssource.get_token()), env)` 组成；stop-when 边界仅转发
`FWD-ENV` queries，融合 token 覆盖原 stop token。consumer stop callback 仍直接使用
receiver environment 的具体 token 类型。`split` 为非 WD extension，shared state 和
subscriber 的合并 control block 使用 source attributes 的 allocator，缺省为
`std::allocator<void>`；subscriber 入链不再分配，缓存仍以 `const&` 重放。
对于自身不分配的 sender、scheduler、receiver、所选 allocator、environment queries、
decayed 结果构造及自定义 stop-token callbacks，这些路径剩余的全局分配仅来自每次
`inplace_stop_callback` registration 的 control block；上述用户代码自行分配的内存另计。使用缺省
allocator 时，`spawn` / `spawn_future` / `split` 的上述共享或 detached 存储仍会分配。
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

## 扩展入口弃用与迁移

以下旧入口保留至少一个弃用发布版本；可独立标注的入口使用 `[[deprecated]]`，计划在后续版本移除，具体版本由维护者决定。既有调用的完成行为、异常传播和结果类型保持不变。等待入口的两个 `std::execution` 别名继续通过 using-declaration 引用 `std::this_thread` 中的同一对象，以保留实体身份和同时导入两个命名空间时的非限定查找；它们仅在文档中弃用，不单独添加属性。

| 弃用入口 | 替代入口 |
| --- | --- |
| `std::execution::split(s)` | `forge::split(s)` |
| `std::execution::empty_env` | 新代码使用 `std::execution::env<>`；需要旧类型身份时使用 `forge::empty_env` |
| `std::execution::transfer_just(sch, values...)` | `forge::transfer_just(sch, values...)` 或 `std::execution::continues_on(std::execution::just(values...), sch)` |
| `std::execution::make_prop(q, value)` | `std::execution::prop(q, value)` |
| `std::execution::sync_wait(s)` | `std::this_thread::sync_wait(s)`；依赖扩展结果形态时使用 `forge::sync_wait(s)` |
| `std::execution::sync_wait_with_variant(s)` | `std::this_thread::sync_wait_with_variant(s)` |
| `std::this_thread::sync_wait(s)` 接受多种 value completion 的扩展 | `forge::sync_wait(s)` 保持原 tuple-or-variant 结果；标准替代为 `std::this_thread::sync_wait_with_variant(s)`，其结果形态不同 |

Forge 入口由 `<forge/execution_extensions.hpp>` 提供，也由 `<forge/execution.hpp>` 汇总。`forge::split` 与原入口共用缓存实现，保持惰性启动、单次执行和 `const&` 重放语义。它依赖 backport 的共享状态，仅在 `FORGE_EXECUTION_BACKPORT_ACTIVE` 已定义时提供；原生 execution 让位时不注入此算法。

`forge::transfer_just` 仅组合标准 `just` 与 `continues_on`，完整原生实现下也可用。`forge::empty_env` 在 backport 下保留旧类型身份，在完整原生实现下等同于 `std::execution::env<>`。`forge::sync_wait` 在 backport 下保留旧的 tuple-or-variant 行为，在完整原生实现下转用标准单一 value completion 的 `std::this_thread::sync_wait`；跨实现消费多种 value completion 应使用标准 `sync_wait_with_variant`。零种 value completion 的既有扩展暂时保留，本次未增加弃用诊断。

依据为 2026-11-13 查阅的 [execution synopsis](https://eel.is/c++draft/execution.syn)、[env](https://eel.is/c++draft/exec.env)、[prop](https://eel.is/c++draft/exec.prop) 与 [sync_wait](https://eel.is/c++draft/exec.sync.wait)。

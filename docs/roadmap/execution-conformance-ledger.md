# `std::execution` 一致性台账

> 状态：active；最后核对：2026-11-13，基线 `5e4a9eb`；取代关系：无。状态分类不代表正文已全部重新审计。


这份台账记录最近几轮 runtime hardening 后，Forge execution backport 的当前状态。它刻意与
用户向的 [`std::execution`](../backports/execution.md) 文档分开：后者说明如何使用，本文是为
future native handoff 和 conformance work 保留的工程审计记录。

当前收敛清单见
[`execution-wd-convergence-checklist.md`](execution-wd-convergence-checklist.md)。
Owner 已接受为了贴近当前 working draft 而做 breaking API change，因此下表的
"native-handoff action" 优先标准收敛，而不是保留旧 extension spelling。

## 当前实现状态

本快照在 2026-08-23 对照 2026-08-22 live working draft `[exec]`
<https://eel.is/c++draft/exec> 审计。下面的分类以 working draft 为准；论文和参考实现只作为
解释上下文。

Execution backport 当前包含：

- sender factories：`just`、`just_error`、`just_stopped`、`read_env`；
- adaptors：`then`、`upon_error`、`upon_stopped`、`let_value`、`let_error`、
  `let_stopped`、`write_env`、`unstoppable`；
- scheduler adaptors：`starts_on`、`continues_on`、`on`、`affine`、policy-shaped
  serial `bulk`、`bulk_chunked` 和 `bulk_unchunked`；
- Forge compatibility extensions：`transfer_just`、`split`；
- composition：`into_variant`、`when_all`、`when_all_with_variant`、`split`、
  `associate`、`spawn`、`spawn_future`；
- consumers：`sync_wait`、`sync_wait_with_variant`；
- stop-token support：`inplace_stop_source/token/callback`、`never_stop_token`；
- coroutine bridge：`as_awaitable`、`with_awaitable_senders`；
- scopes：`simple_counting_scope`、stop-aware `counting_scope`；
- domain dispatch：receiver-env start-domain selection，以及 `connect` 和
  completion-signature computation 中的 sender-env completion-domain 递归
  `transform_sender`，并公开 current-shaped `transform_sender` / `apply_sender` entry points。

因此，不应再把若干早期审计记录当成 open work：identity-only domain dispatch、
single-shape `sync_wait`、non-stop-aware `counting_scope`、以及不完整的 `when_all`
cartesian value signatures 都已经关闭。

标为 "Implemented subset" 的行不是自动 backlog。它们记录这个 header-only backport
有意采用的实用内部模型与当前 WD 措辞的差异，例如 tag-invoke based query object 或 serial
bulk execution。这些 residuals 已接受，除非出现具体下游需求、native-handoff blocker 或
correctness issue，否则不作为下一轮默认目标。符合标准的原生实现可用时，backport 会整体
让位，而不是混用 native query internals 与 backport query internals。

## Working-draft 覆盖矩阵

下表有 32 个按能力分组的 implemented / subset 行，不是 `[execution.syn]` 声明数的完整
分母；一个表格行可能覆盖多个标准名字。为避免把“未列出”误读成“已实现”，当前 draft
中明确未实现或暂不纳入本 backport 的分组紧随其后列出。

| Surface | 状态 | 证据 / 剩余差异 |
| --- | --- | --- |
| `just`, `just_error`, `just_stopped` | Implemented subset | `just.hpp`；receiver allocator 在 noexcept get-state 内逐元素重建 data；无 allocator 按值类别直接复制/移动；connect 按 /28 条件 noexcept。尚未提供通用 product/tag surface。 |
| `read_env`, `write_env` | Implemented subset | `read_env` 以 `Q()(Env&)` 的有效、非 void 结果约束参与条件；value 类型与条件 error channel 取自保存的 `Q&` 对 receiver environment 的实际调用，保留 cv/ref 与环境值类别；两工厂支持 constexpr。`write_env` 保留 overlay 优先与 receiver FWD-ENV 过滤，const-lvalue 直接连接 child。Query data 使用 receiver allocator 重建，connect 受有效查询约束并条件 noexcept。缺失 dependent-sender 诊断仍为 subset。 `read_env_t` 与单参数 `write_env(env)` 已弃用，首选 `read_env` 对象及 `write_env(sndr, env)`。 |
| `then`, `upon_error`, `upon_stopped` | Implemented subset | `then.hpp`, `upon.hpp`；binary callable 满足 movable-value，closure 按 bound-argument 初始化约束；每个匹配 completion 的 invocability 参与 signature/connect 约束。Child 与 signatures 使用 FWD-ENV 及一致值类别，const-lvalue 直接连接 child，不预先复制；callable 使用 receiver allocator 重建；connect 条件 noexcept，调用异常条件添加 exception_ptr。尚未提供通用 product/tag surface。 |
| `let_value`, `let_error`, `let_stopped` | Implemented subset | `let.hpp`；completion-tag 对应的 let-env 选择 completion scheduler、domain 或空环境，successor 仅转发 receiver forwarding queries。按值保存 scheduler，保留默认模式的 legacy get_scheduler。Args 与 predecessor/successor typed holders 分别位于去重 variant；先保存参数，再销毁 predecessor，随后调用函数并连接 successor，无类型擦除或存储回退分配。Decay-copy、invoke 与 connect 使用同一 nothrow 公式，条件添加 exception_ptr。无效 successor/参数通过约束拒绝；let_stopped creation 要求 nullary invocable。Predecessor 的连接和签名采用 /10 的未过滤环境，/9 与 /12 冲突见下文。 |
| `starts_on` | Implemented subset | `on.hpp`；typed variant 在目标 hop 后连接 child，移动/连接失败在该 agent 报告 exception_ptr，签名采用 lowering 的条件 nothrow 公式；lambda 的异常规格含 scheduler。Scheduler connect 仍在 outer connect；调度 signatures、guards、operation 与 runtime 统一使用 scheduler 非 const 左值。无 heap/type erasure。Child 保留目标 get_scheduler/get_start_scheduler 与目标派生 domain，其余 receiver queries 经 FWD-ENV；此环境偏差等待 WD 澄清。 |
| `continues_on` | Implemented subset | `continues_on.hpp`；typed scheduler/predecessor operations 与 tagged decayed tuple variant，无存储回退分配；result datums 要求 decay-copyable，失败通过 signature/connect 约束拒绝。仅声明可能抛出的 decay-copy exception_ptr，合入实际非 const 左值 schedule 结果的 error/stopped；signature、guard、operation 和 runtime 类别一致。Completion queries 按所传环境的实际 tag 集合参与；unary 采用空环境，目标 schedule 无 error 时可发布 error scheduler/domain。目标 domain 仍先 schedule attrs 后 scheduler；全部 completion tags 禁止 child 转发，其他 forwarding attrs、两条 receiver env 与 signatures 使用 FWD-ENV，借用引用身份保持。Connect 仍 potentially throwing，尚无通用 make-sender surface。 保留 partial application 与 pipe，等待 WD 澄清。 |
| `on` | Implemented subset | `on.hpp` 暴露两种形式及 scheduler/closure pipe。组合 `continues_on(starts_on(...), orig_sch)` 或 `continues_on(closure(continues_on(...)), orig_sch)` 的 operation 以普通成员直接构造，无类型擦除或缓冲回退；支持多种 value shapes，沿用条件 `exception_ptr` 规则。第一种形式从 receiver env 查询 `get_start_scheduler`；closure form 在 signatures 与 connect 中均用 `get_completion_scheduler<set_value_t>(get_env(child), get_env(rcvr))`，传入 receiver environment 本身，支持环境相关答案及 inline fallback；原 completion scheduler 查询缺失时，signatures 不参与，sender_in 与 connectability 均为 false。 第一形式仅转发 child 的非 completion forwarding attributes；第一形式随 starts_on 采用 hop 后 child connect；closure 形式保持 continues_on 的 eager child connect。非 pipeable closure 的两种调用已弃用并保留兼容。sender 与 closure 基类兼有时选择 sender 形式。 |
| `bulk` | Implemented serial subset | `bulk.hpp`；当底层标准库暴露 execution policy traits/objects 时接受 current-WD policy-shaped calls，并在 completing agent 中串行执行。Policy 不引入并行。每个 value shape 检查 unchunked arity，child/signatures 使用 FWD-ENV，callable 使用 receiver allocator；connect 保持 potentially throwing。 无 policy 调用已弃用，串行替代为 `forge::bulk`。 |
| `bulk_chunked`, `bulk_unchunked` | Implemented serial subset | `bulk.hpp`；当底层标准库暴露 execution policy traits/objects 时接受 current-WD policy-shaped calls。`bulk_unchunked` 等价 serial `bulk`，`bulk_chunked` 使用一个非空 `[0, shape)` chunk。 每个 value shape 按 complete 的 arity 检查，child/signatures 使用 FWD-ENV 及一致值类别，const-lvalue 不预先复制 child；callable 使用 receiver allocator，connect 条件 noexcept。 无 policy 调用已弃用，串行替代为 `forge::bulk_chunked` 与 `forge::bulk_unchunked`。 |
| `unstoppable` sender adaptor | Implemented subset | `unstoppable.hpp`；结果为 write_env(sndr, prop(get_stop_token, never_stop_token{})) 的类型；nullary closure、bare pipe 与 closure 组合已弃用并保留兼容。 |
| `stopped_as_optional`, `stopped_as_error` | Implemented subset | `stopped_as.hpp`；提供 direct-call；`stopped_as_optional` 要求恰好一个非 `void` value signature，单签名多个参数形成 `optional<tuple<...>>`；该 shape 验证延迟到实际环境的 signature 查询 / `connect`，是已接受的 creation Mandate 延迟偏差，未实现 WD `make-sender` 的无环境 / 环境依赖分类；CPO 自身可直接 pipe，nullary closure 已弃用并保留兼容；`stopped_as_error(error)` 返回标准 pipe closure。 Optional child/signatures 使用 FWD-ENV 并检查 value 保存；error child 保留未过滤环境。stopped_as_error binary 要求 movable-value，closure 要求可初始化 bound error；signature/connect 对无效 child 替换失败。 |
| `into_variant` | Implemented subset | `into_variant.hpp`；CPO 自身支持 direct-call 和 bare pipe；nullary closure 已弃用并保留兼容；`sync_wait` / `when_all` 复用单参数入口。 Child/signatures 经 FWD-ENV 及一致值类别，const-lvalue 不预先复制 child；全部 result datums 要求 decay-copyable，connect 条件 noexcept。通用 product/tag surface 仍未提供。 |
| `when_all`, `when_all_with_variant` | Implemented subset | `when_all` 按 WD 要求至少一个 child sender；typed tuple 保存各 child 的 typed holder，operation state 在 holder 内作为普通成员从 factory prvalue 直接初始化，支持空、non-final、immovable 类型；values、errors、stop source 和 optional stop callback 静态存储，operation-state storage 本身不再分配，无大小或对齐导致的堆回退。非分配 children / schedulers / receivers / environment queries / 用户函数 / 结果类型的组合无需全局分配；top-level 是否注册取决于 receiver token 的 `stop_possible()`，inner `when_all` 会在 parent 的 stop source 上注册，inplace registration 均零分配。自定义 token callback 的自行分配另计。Child `connect` 抛出时销毁已构造 children。保留 error 优先于 stopped、stop request 行为、child start 前注册 callback 和 completion 前注销 callback；child env 暴露 stop token 并转发一参数 forwarding queries。保留多 value shape 的 Cartesian signatures 扩展，区别于 WD 的单 value shape 约束；分配、异常和既有 lifecycle tests 覆盖。 兼容 awaitable child 经共享 CPO 连接，另含全局 frame allocation。 |
| `split` | Implemented subset | 非 WD extension；shared state 内直接构造 concrete child operation，缓存单一 value completion shape 并以 `const&` 广播。shared state 与各 subscriber/control block 使用 source attributes 的 allocator，缺省为 `std::allocator<void>`；入链不分配。无 operation 大小或对齐触发的全局回退；在 sender、scheduler、receiver、allocator、environment queries、decayed 结果构造及自定义 stop-token callbacks 均不自行分配时，inplace callback registration 不分配，也无额外的全局分配；上述用户代码的分配另计。未完成 source 的 abandonment stop 协议仍未实现。 可缓存兼容 awaitable；child frame 的全局分配另计。 |
| `sync_wait`, `sync_wait_with_variant` | Implemented subset | env 通过 const member `query` 提供 scheduler、start scheduler 和 delegation scheduler；`sync_wait` 接受零种或多种 value completion 的标准拼写已弃用，改用 `forge::sync_wait`（仅 backport 保证扩展语义）；`sync_wait_with_variant` 保留零种 value completion 的 stopped/error 行为且不产生弃用诊断；同步 typed-error consumption 保持为 Forge `wait_result` extension，而不是 `sync_wait`。 接受实际 run_loop 环境兼容的 awaitable，包含其全局 frame allocation。 |
| `associate` | Implemented current-WD subset | association 成功后才在 typed manual-lifetime holder 中 connect wrapped sender；operation 不单独分配，connect failure 直接抛出。先销毁 child operation，再销毁 receiver，最后释放 association；child 使用 counting_scope wrapper 暴露的 token 注册 inplace callback 时不分配；sender、scheduler、receiver、environment queries、decayed 结果构造及自定义 stop-token callbacks 自行分配的内存另计。 接受兼容 awaitable，child frame 全局分配另计。 |
| `spawn` | Implemented current-WD subset | 仅接受 `set_value()` / `set_stopped()`；operation 为 allocator-backed state 的普通成员。allocator 优先取显式 `get_allocator(env)`，其次取 wrapped sender attributes 的 allocator 并注入 spawned environment，否则取 `std::allocator<void>`；一个 rebound state allocation，销毁并 deallocate 后才释放 association。在 sender、scheduler、receiver、所选 allocator、environment queries、decayed 结果构造及自定义 stop-token callbacks 均不自行分配时，inplace callback registration 不分配，也无额外的全局分配；上述用户代码的分配另计。 awaitable 始终声明 exception_ptr，因此不满足 spawn 的 completion 限制。 |
| `spawn_future` | Implemented subset | Eager single-consumer future；operation 是 state 的普通成员，producer 使用 `write_env(stop-when(sndr, ssource token), env)`，stop-when 仅转发 `FWD-ENV` queries 并覆盖 stop token。allocator 优先取显式 env，其次取 wrapped sender attributes 并注入 env，否则为 `std::allocator<void>`；生命周期 resource、state、state control block、consumer 的合并 record/control block 均使用该 allocator；consumer 完成时解除外层 operation 的存储所有权。resource 持有 association，最后一个内部 allocator 副本消失时先销毁并 deallocate resource，再 disassociate，因此全部 state、consumer 和 control block 的回收均先于 join 完成。在 sender、scheduler、receiver、allocator、environment queries、decayed 结果构造及自定义 stop-token callbacks 均不自行分配时，inplace callback registration 不分配，也无额外的全局分配；上述用户代码的分配另计。value/error 按 decayed types 缓存并以 rvalue 交付，consumer stop 保留具体 token 行为；try-cancel 后 producer 未结束时仍保留 state 和 association。 可 eager 消费兼容 awaitable，producer child frame 的全局分配另计。 |
| `inplace_stop_source/token/callback` | Implemented current-WD semantics | intrusive registration 零分配；constructor 为条件 `noexcept`，仅 callback 初始化可抛出。支持立即调用、自毁、pending sibling 注销和跨线程 invocation 等待；source owner 须覆盖注册与 stop 调用。source lock 保护 intrusive list；requester stack 上的 invocation 状态处理同线程自毁并登记跨线程等待，per-node completion 与锁交接保护跨线程回收；没有等待者时不发出 notify。协议见 [stop callback 设计](inplace-stop-callback-design.md)。 |
| `simple_counting_scope`, `counting_scope` | Implemented current-WD-shaped subset | `join()` 的 schedule operation 为 typed 普通成员。counting token wrap 的 operation 只持有 receiver 和 child operation；按 `[exec.stop.when]` 暴露的融合 token 只保存两个输入 token，stop queries 返回逻辑或，仅 `FWD-ENV` queries 可穿过 wrapper。每个 child `callback_type<Fn>` 直接使用两个输入各自的 `stop_callback_for_t`，atomic exchange 保证并发停止时也至多调用一次；发布 registration 前先查询逻辑 token，任一输入已停止时在构造线程立即调用，后续停止由底层 registration 捕获。两个 registration 先于 `Fn` 析构，trampoline 调用 `Fn` 后不访问已可能销毁的 storage，child 管理 callback lifetime 和注册失败；wrapper 原样转发 completion，不合成 stopped 或额外 signature。一个 child callback 对应两个输入 registration；inplace 输入的 registration 均零分配，无其他融合状态分配。输入 source owner 须覆盖全部 stop 调用；completion 前注销 receiver registration，source lifetime 边界详见设计记录。sender、scheduler、receiver、environment queries、decayed 结果构造及自定义 stop-token callbacks 自行分配的内存另计。destructor、join 和 disassociation 顺序保持。 token.wrap 接受 awaitable，停止融合与 association 协议不变；child connect 的全局 frame allocation 另计。 |
| `as_awaitable`, `with_awaitable_senders` | Implemented current-WD-shaped subset | `as_awaitable` 按 direct member、transformed/adapted member、普通 awaiter、transformed/adapted sender bridge、identity fallback 的顺序分派；普通 awaiter 覆盖直接 awaiter、member/free `operator co_await`，completion adaptor 缺失时按 identity 处理。Sender bridge 对零值、单值和单一多参数 value completion 分别返回 `void`、该值类型和 `tuple<...>`；多 value-alternative sender 退回原表达式，因而不会经该 bridge 变成 awaitable。`with_awaitable_senders` 提供 current-WD-shaped continuation / stopped 传播。 direct member 结果有 awaitable Mandate，7.2 noexcept 按实际表达式；prepared sender 在 receiver 实际环境要求 receiver-of，无 unhandled_stopped 的 promise 不可接收 stopped。Mixin 有 class-type 约束，handler 按表达式存在性检测。receiver 直接暴露 promise env 属 WD latitude；保留 deferred connect / symmetric transfer、decay 后去重、prepared 多值 identity fallback、lvalue decay-copy 和 member-first acceptance leniencies。 |
| Ordinary awaitable as sender | Implemented current-WD-shaped subset | marker-first private enable-sender concept 保留 user specialization；domain transform / member signatures 后按 await-result 合成 value、exception_ptr 与 stopped signatures（无环境时无 await_transform）。connect 的 direct / transformed / noexcept 路径均支持 awaitable fallback，frame 全局分配，operation noexcept move 转移 handle 并清空源；完成在挂起 frame 的 await_suspend 内交付并允许 receiver 自毁。GET-AWAITER 为受约束的 member / ADL emulation，竞争候选保守拒绝；不是编译器内建 await-expression 反射。内部 completion awaiter 为局部类，排除 user receiver/value ADL。若类型兼具有效 awaiter 方法与 deleted/inaccessible/ambiguous 的 co_await operator，probe 将 operator 视作缺失，类型可满足 sender，但实际 await/connect 为 ill-formed（已接受 emulation 边界）。 |
| `schedule_from` | Implemented current-WD-shaped subset | 单参数 departure marker，product access 暴露 schedule_from_t；attributes、child receiver 与 signatures 采用 FWD-ENV，保留引用身份与值所有权，const-lvalue 直接连接同类别 child；无独立结果缓冲。continues_on 经该 marker 允许 source domain 定制 departure；generic child operation immovability 仍依赖 child connect 契约。 |
| `transform_sender`, `apply_sender` | Implemented current-WD-shaped subset | 公开入口为受约束函数模板，支持显式模板实参与 ADL；default_domain 标准成员限制 sender / queryable，默认左值结果保留引用；内部 connect 保留自定义 source 的借用连接扩展。transform 仍依次递归应用 completion / start domain；apply 采用 explicit-domain-first、algorithm-tag fallback。默认域两参数成员与自定义 domain 的两参数回退已弃用。回退仍在 completion 与 start 两阶段各自调用。 |
| `affine` | Implemented subset | `affine.hpp`；签名与 connect 要求环境提供 infallible start scheduler。有效时按 child 值类别优先选择 .affine()，否则以 continues_on 和转发全部 scheduler queries 的 unstoppable wrapper 保留多 value shapes；connect 仍 potentially throwing。未为 library senders 添加可选 .affine() 成员。Forge pool/strand 在所有环境声明 value/stopped；仅 stoppable token 环境满足 infallible-scheduler，env<>（无 start scheduler）及无 stop token / never_stop_token 的 start 环境均不满足 sender_in。Wrapper 按 &、const&、&& 转发 schedule 类别，mutable-only schedule 可用；签名与 connect 对四种 child cv/ref 选择相同定制。 |
| `env`, `prop` | Implemented query surface subset | `prop` 的 const variadic query、tag holder 与 value 两元素 aggregate 形状及花括号/圆括号两参数 CTAD 保留，含函数模板内非依赖表达式与 reference_wrapper 解包；holder 删除拷贝/移动赋值，对 const 可赋值 tag 也生效，空 tag 不增加大小。callable Mandate 因 schedule attributes 使用 prop 时递归触发 scheduler concept 而暂缓；两模板均不可赋值，保留所需的拷贝/移动构造。`env` 参数约束 queryable；显式 guide 使用 `template<queryable... Envs>` 以保证 Clang 的 reference_wrapper 解包，WD 的 `template<class... Envs>` 与此写法无可观察差异，非 queryable 类型本就不能形成 env。借用 referent 的 cv/ref、查询返回引用及底层条件 noexcept 保留。默认 member-first / tag-invoke dispatch 与 strict 成员选择均有探针。 `make_env` 仅文档弃用，改用 `env{...}`。 |
| `get_env` | Implemented subset | Member-first、tag-invoke fallback；默认结果与 `__empty_env` / `forge::empty_env` 均为 `env<>`。选中查询要求 noexcept 和 queryable 结果，支持 constexpr；两种 dispatch 均执行 Mandates。 |
| `sender_tag`, `receiver_tag`, `operation_state_tag`, `scheduler_tag`, `tag_of_t` | Implemented subset | 只有 `schedule_from` 提供结构化访问，其他库 sender 的 `tag_of_t` 无法成立；自定义 domain 无法按工作草案方式匹配这些库 sender。暴露 marker spelling；旧 `*_t` 标记别名仅文档弃用，属性留待锁定头文件迁移。 |
| `get_scheduler` | Implemented subset | Member-first、tag-invoke fallback；直接查询须 noexcept，再通过 value completion-scheduler CPO 与 HIDE-SCHED(env) 解析 scheduler。结果可能从借用引用变为 scheduler 值；inline_scheduler 的属性注入经 /5.2 的 auto(q) 返回值。默认与 strict 探针覆盖。 |
| `get_start_scheduler` | Implemented subset | Member-first、tag-invoke fallback；两分支均检查 noexcept 与去 cv/ref 后的 scheduler 结果，支持 constexpr。 |
| `get_delegation_scheduler` | Implemented subset | Member-first、tag-invoke fallback；两分支均检查 noexcept 与去 cv/ref 后的 scheduler 结果，支持 constexpr。 |
| `get_completion_scheduler` | Implemented subset | 限制三个 completion tags；TRY-QUERY 后递归解析 value scheduler，直至无查询或同型且相等。非空环境且输入满足 scheduler、auto(q) 可构造时按 /5.2 返回输入副本；每个成功分支均检查去 cv/ref 后的 scheduler 结果 Mandate。仅在指定表达式上判断 MANDATE-NOTHROW，存储用的复制/移动无需声明 noexcept；终端借用答案保留引用。Inline scheduler / attrs 无 unary value 答案；环境缺少 scheduler 时两者均通过各自默认分支取得 inline scheduler，let 在两种模式均选择 SCHED-ENV(inline_scheduler)。Run-loop 与 Forge schedulers 对实际 completion tags 验证 scheduler / attrs parity；未诊断 signature consistency，见措辞问题。 |
| `forwarding_query` | Implemented subset | 位于 std，兼容 execution using；member 与 tag-invoke 分支均检查 noexcept / bool。FWD-ENV 使用独立软 gate，forwarding member 与保留的 tag-invoke 答案先于派生 shortcut（包括显式 false）；没有这两种定制的派生 tag 不要求默认构造；其余保持默认实例的常量分类。when_all 与 let receiver2 的 concept-context gate 不变。 |
| `get_await_completion_adaptor` | Implemented subset | Member-first、tag-invoke fallback；无 default adaptor。支持 constexpr，两分支均执行 nothrow Mandate。 |
| `get_domain`, `get_completion_domain` | Implemented subset | 查询只选择 domain 类型，公开 CPO 按 MANDATE-NOTHROW(D()) 默认构造；D() 不可用时查询不参与；可用但抛出的构造违反硬 Mandate。get_domain 的 scheduler 分支使用 HIDE-SCHED，以 addressof 取得环境地址，删除或重载 operator& 不影响 domain 选择与后续 transformation；精确原环境 overload 不再控制此选择。Completion-domain /2.3 经过完整 completion-scheduler CPO，并保留 continues_on 的过滤环境映射。递归 connect / transformed completion signatures 共用 domain model。 |
| `get_allocator` | Implemented subset | 标准实体位于 std，execution using 保持同一实体；无 default allocator。选中 member / tag 查询要求 noexcept 与去 cv/ref 后的 simple-allocator，支持 constexpr。用于 spawn / spawn_future / split 的 allocator 查询。 |
| `get_stop_token` | Implemented subset | 标准实体位于 std，execution using 保持同一实体；缺失查询返回 never_stop_token，提供 std::stop_token_of_t。选中 member / tag 查询要求 noexcept 与去 cv/ref 后的 stoppable_token，支持 constexpr。 |
| `get_forward_progress_guarantee` | Implemented subset | 按 scheduler 表达式类别、copyable、queryable、sender schedule result 与 progress atom 检查。内部 scheduler shape 打破定义循环，公开 schedule_result_t；schedule CPO 检查 sender 结果，start 检查左值与 void。所有仓库 scheduler 均显式提供 progress 成员。weakly_parallel 默认分支已弃用，后续移除并要求提供 progress 查询；结果/noexcept Mandates 在两种 dispatch 均适用。 |
| `completion_signatures`, `completion_signatures_of_t`, value/error utilities | Implemented utility subset | awaitable 在有 / 无环境两种查询中合成 signatures，member forms 优先； 有效完成签名约束；选中 signatures 查询须返回 completion_signatures specialization，alias 要求 sender_in。引用、固定参数个数的 Tuple / Variant 与 stopped 收集由编译探针固定；dependent-sender 诊断另列。 `value_types_of`、`error_types_of` 与 `sends_stopped_v` 已弃用，标准替代见执行库说明。 |
| Algorithm `_t` names | Implemented names | then / upon / let / bulk family、on、into_variant、stopped utilities 及 spawn_t / associate_t 暴露 WD 类型别名；未加入 native COMPLETE probe 集合。结构化 sender tag 匹配仍依赖独立 product surface。 |
| Completion receiver value categories | Implemented | set_value / set_error / set_stopped 拒绝左值和 const 右值；两条 dispatch 均执行 void / noexcept Mandates。 |

### 未实现 / 暂不纳入的 current-draft surface

| Surface | 处置 | 原因 / 后续条件 |
| --- | --- | --- |
| `task`, `task_scheduler`, `with_error` | Deferred after current-WD audit | [P3552R3](https://wg21.link/P3552R3) 已进入 working paper，且 2026-08-22 draft snapshot (`2f5924ff8ee5`) 含完整 task family；但 2026-05 [P4007R3](https://wg21.link/P4007R3) 仍列出开放问题，并明确区分可在发布后修正与发布后无法兼容修正的项目。稳定性 gate 因此未通过。`forge::task` / `forge::io::io_task` 是不同契约的 Forge extensions，不能当作该标准 surface。重开需负责人另行决定，并明确 wording revision、frame/operation ownership、allocator、stop/error、self-destroy 和 COMPLETE/PARTIAL native handoff；下一次工作草案快照审计时复查。 |
| `parallel_scheduler`, `get_parallel_scheduler` | Out of scope | 当前 `bulk` family 是明确的 serial subset；本仓库没有可承诺标准 parallel scheduler 语义的 execution resource。 |
| `dependent_sender_error`, dependent-sender concept surface | Not implemented | 当前 completion-signature diagnostics 不建模这组 dependent sender contract。需要先有可移植编译器/参考实现证据。 |
| `indeterminate_domain` | Implemented | 提供候选 transform 返回类型 Mandate、默认 transform、按首次出现顺序去重的 common_type 合并；交换 operands 保持候选集合与 transform 行为，不保证精确类型对称。COMMON-DOMAIN 仅在 common type 的完整默认构造表达式有效时选中该类型。 |
| `inlinable_receiver` | Implemented concept | 检查 receiver 与 noexcept make_receiver_for，允许 incomplete ChildOp；不改变 operation-state 存储。 |
| `sender_adaptor_closure` | Implemented | 按 [`[execution.syn]`](https://eel.is/c++draft/execution.syn) / [`[exec.adapt.obj]`](https://eel.is/c++draft/exec.adapt.obj) 暴露 class-type 约束的公开 CRTP base，允许 incomplete `D`。Pipe 识别要求公开且无歧义的自身 specialization、无其他 specialization base，并排除 sender；所有库内 closures 通过同一机制参与。支持用户 closure 与库 closure 双向组合、按原 cv/ref 类别初始化保存对象，以及四种非 volatile 调用类别的 perfect forwarding、条件 `noexcept` 和 `constexpr`；编译探测覆盖错误 / 多重 / 不可访问派生、非 sender 左操作数与 move-only capture。 |

## 兼容性分类

下表区分 current-draft conformance work 和 Forge convenience extensions。它是 native handoff
risk triage 的事实来源。

| Item | 分类 | 当前状态 | Native-handoff action |
| --- | --- | --- | --- |
| 库 closure 的隐式转换 pipe | 工作草案识别规则 | 按 [`[exec.adapt.obj]/2`](https://eel.is/c++draft/exec.adapt.obj#2)，识别依据是操作数自身去除 cv/ref 后的类型。库 closure 的派生类与 `std::reference_wrapper` 不再仅凭转换参与；`sender \| std::ref(closure)` / `sender \| std::cref(closure)` 不再有效。 | 直接使用原 closure 或 `wrapper.get()`；自定义 closure 使用自身的公开 CRTP specialization，并满足唯一 specialization 要求。 |
| Library adaptor non-copyable lvalue `connect` | 已收敛的 standard-shaped behavior | Standard backport sender 对 move-only lvalue 要求显式 `std::move(sndr)`。 | 保持测试和示例显式 move；不要在标准路径恢复 destructive lvalue connect。 |
| `forge::async_scope::spawn(lvalue)` | Forge extension convenience | 同时接受实际 scope 环境可连接的 awaitable；resource_context 转发相同约束。 对 non-copyable non-const lvalue sender 做 destructive move，作为 Forge runtime convenience。 | 继续文档化为 Forge-only extension；native-friendly 示例写 `scope.spawn(std::move(sndr))`。 |
| `std::execution::ensure_started` | 已移除的 non-WD extension name | `<execution>` backport 不再暴露。 | 保持在 `std::execution` 外；未来如需工具，只能放入 `forge::`。 |
| `std::execution::start_detached` | 已移除的 non-WD extension name | `<execution>` backport 不再暴露；Forge runtime code 使用 `forge::start_detached`。 | 标准路径保持使用 `spawn`；detach utility 保持在 `forge::`。 |
| `std::execution::spawn` | Implemented current-WD subset | Top-level `spawn(sender, token[, env])` 分配 detached state，通过 `token.try_associate()` 关联并 eager start。只接受 `set_value()` / `set_stopped()` senders；awaitable 固定声明 exception_ptr，因此被拒绝。 | 测试继续对齐 current-WD fire-and-forget spelling。 |
| `std::execution::counting_scope::join()` | Implemented current-WD-shaped subset | `simple_counting_scope::join()` 和 `counting_scope::join()` 返回 async senders；空 scope 在 `start()` 调用线程内联完成，非空 scope 的 count drain 在 mutex 外启动 receiver env 的 start-scheduler operation，并在完成前建立 terminal `joined` state。 | 两个 scope 的 destructor 遵循 current-WD：仅允许 joined / unused / unused-and-closed，否则 terminate（包括 naturally-drained-but-unjoined）；在 mutex 下检查 state，与最后一次 decrement critical section 同步。保持 zero-count inline / real-drain 后拒绝 association、start-scheduler handoff、last-decrement vs join-register races 压力测试。 |
| Scope-token `wrap` / `associate` / member `spawn` | 已收敛 surface，语义实现当前目标 | Token-member `associate` / `spawn` 已移除。`simple_counting_scope::token::wrap` 是 identity forwarding；`counting_scope::token::wrap` 给 child env 暴露 fused stop token，scope stop 与下游 receiver/env stop 任一请求都会让 child 观察到 stop。Top-level `associate` / `spawn` / `spawn_future` 拥有 association。 | 继续测试 allocator/env、fused stop-token lifetime 和 async join details；不要在 `std::execution` 恢复 token-member helpers。 |
| Throwing receiver completion callbacks | 工作草案要求 | `set_value`、`set_error`、`set_stopped` 必须不抛异常，receiver 必须不抛异常地移动；negative compile probe 固定这些标准规则。 | 保持拒绝。 |
| Execution domain dispatch | Tested current-WD subset | Public `transform_sender`、`connect` 和 completion-signature computation 应用 sender completion-domain recursion，再应用 receiver start-domain recursion；`apply_sender` 采用 explicit-domain-first、sender-tag fallback；非 default-domain 的 `get_completion_signatures(sender, env)` 使用同一 transformed sender type。 | 保持 recursive transforms、customization order、forwarding/noexcept 和 transformed-signature computation 覆盖，包括 rawless source senders。 |
| `forge::task` stopped completion | Forge local utility | value/error 仍在 final_suspend 完成；stopped 直接从 unhandled_stopped 完成并返回 noop_coroutine，body 不恢复，frame 与等待点局部对象由 owner 析构；两条路径均允许 receiver 在完成内销毁 operation。 | 按 [exec.as.awaitable]/4.3 保持 stopped abandonment；default/strict 测试覆盖 just_stopped、closed channel、nested task，同步及异线程自毁，检查 awaiting expression 后代码不运行。 |
| `forge::any_scheduler` | Forge local utility | 建模 Forge local scheduler concept，使用 shared-state identity equality；scheduler 与 schedule env 以成员提供 value / error / stopped completion-scheduler roundtrip，保持 shared-state identity；progress 显式报告 weakly_parallel。 | 保持在 `forge::`，不声称标准 type-erasure API。 |
| `forge::wait_result`, `wait_result_of` | Forge local utility | 接受实际带 inplace stop token / run_loop scheduler 的 wait 环境兼容的 awaitable；同步保留 value、stopped 与 closed-set typed error outcome，不通过 throw 表达。 | 保持 sender_in 环境约束与普通 awaitable 消费的独立验证。 |
| `forge::erased_sender` | Forge local utility | 接受兼容 awaitable，source signatures 必须是 closed set 的子集；copyable source 仍按 lvalue 借用。Connectable erased sender，支持保留引用类别的 multiple value shapes、closed-set typed errors 和 bounded env/stop-token forwarding。 | 保持在 `forge::`；reference payload 只在同步 completion stack 内借用；不要当作标准 execution surface。 |
| Receiver env stop-token propagation | Forge utilities 的 required behavior | Member `query` 提供 stop token：`forge::__async_scope_detail::__env`、`any_receiver_of` 的 env、`forge::__erased_sender_detail::__receiver::__env`、`forge::__task_detail::__env`、`forge::io::__coro_detail::await_sender_receiver_env` 与 `forge::io::__io_combinator_detail::child_receiver::env`。IO await env 同时提供 scheduler；`task` 主 env 提供 start scheduler。`static_thread_pool`、`strand`、`any_scheduler` 的 schedule env 与 task inline fallback env 通过 member 提供 value completion scheduler；各 scheduler 均通过 member 显式提供 forward progress；schedule env 对实际完成 tags 与 scheduler 保持 completion-query parity。 | `forge_member_queries` 直接断言成员；`forge_strict_*` 禁用 backport query fallback。该测试集合不等于完整 native execution 验证。 |

## 已接受 residuals 与未来风险

### 实际 query 分类

sender-attribute 过滤采用保守、已接受的子集：
FWD-ENV 先遵守 forwarding member 或保留的 tag-invoke 答案，包括显式 false；
没有这两种定制的 `forwarding_query_t` 派生类型不要求默认构造，其余仅在 `forwarding_query(remove_cvref_t<Q>{})` 可常量求值为 true 时转发。
`when_all` 与 let receiver2 的 forwarding-query concept 仍仅使用默认实例。
使用 `enable_if_t` type requirement 将 false 和非恒定表达式留在替换失败路径，
而非产生 nested constraint 的硬错误。member=false 保持拒绝，不以 marker、兼容
tag 定制或无法判断为理由放行。分类不在运行时构造 query 或调用其属性值查询。

WD [`FWD-ENV`](https://eel.is/c++draft/exec.snd.expos#4) 判断实际
`forwarding_query(q)`，而 [`forwarding-query`](https://eel.is/c++draft/execution.syn)
type concept 使用 `forwarding_query(Q{})`。非 constexpr 默认构造但有 constexpr
显式构造的无状态 structural query 可以是合法 CPO；该控制已覆盖。
该合法控制的 raw CPO 调用保持正向验证，但其跨 adaptor 转发尚不支持；不能将拒绝
归因于 CPO 非法。unknown-reference member 扩展已撤回：Clang19 实编未支持该检查，
且 [LLVM 状态表](https://clang.llvm.org/cxx_status.html) 将 P2280 unknown-reference
支持列为 Clang20 起。当前不宣称该扩展可用，也不添加未经验证的构造、静态 flag
或对象表示替代方案。对 default gate 无法确定为 true 的 stateless / stateful flag，
均保守不转发。这是独立的 residual tracking，
不能将未分类 query 重新解释成非法 CPO，也不能作为放宽 nonforwarding 查询的依据。
重新扩大支持前，需要合法 CPO 的独立证据及可移植的 C++23 / C++26 分类方案。

### 查询与 domain 的 WD 措辞问题

- 结果类型 Mandates 按 `remove_cvref_t` 解释；字面要求 allocator / token 的引用或 const
  类型满足 copyable 等约束会否定草案自身的 prop 示例。
- `[exec.sched]/1` 要求 progress 查询，而 `[exec.get.fwd.progress]/2` 又要求 scheduler；
  通过不含 progress 的内部 shape 打破循环。Inline 指定查询列表缺少 progress，仍保留显式成员。
- `[exec.get.compl.sched]/5.2` 使带环境的 scheduler 对所有 completion tags 都可回答，
  `[exec.sched]/6` 要求 attributes parity，而 `/6` 的 signature consistency 又禁止不存在的
  completion；inline 的 error / stopped、run-loop 的 error 无法同时满足。实现 /5.2，
  parity 仅覆盖实际 completion tags，暂不诊断 scheduler / domain 的 signature consistency。
- `[exec.read.env]` 的无环境 check-types 对缺失查询抛 unspecified-exception，与
  make-sender 的 non-dependent/dependent 分类有张力；目前用约束表达可观察的 sender_in 失败。
- RECURSE-QUERY 的“同型”按去 cv/ref 的 scheduler 类型比较；终端借用结果保留引用。
- `[exec.domain.indeterminate]` 对 common_type 的候选 pack 仅给出去重集合语义，没有规定
  顺序；首次出现顺序使两个不同 indeterminate operands 交换后可得到不同 specialization。
  这与 `[meta.trans.other]` 对 common_type 的对称要求冲突；没有可移植的 canonical 类型
  顺序。本实现保留首次出现顺序，只承诺候选集合相同和 transform 行为相同。

### Other Residuals

- `spawn_future` 会为 shared state 和 consumer record 使用 `get_allocator`，并直接按
  receiver env 的具体 stop-token 类型注册 callback。Forge runtime 使用独立的
  `forge::any_stop_token`；其 callback/type-erasure control blocks 按已接受设计保持
  allocator-neutral。见
  [`execution-stop-token-allocator-design.md`](execution-stop-token-allocator-design.md)。
- `spawn_future` 的 consumer record 在 future `connect()` 时分配，因此该路径可以抛出；
  这偏离 current-WD inline/noexcept future-operation shape。Connected consumer 与
  shared state 的 transient strong cycle 由 terminal completion、unstarted-op
  destruction 和 abandon paths 切断；当前保留该 ownership model，除非出现可复现
  retention 或 native-handoff blocker。
- `inplace_stop_callback` 已实现无分配与条件 `noexcept`，遵守标准 source lifetime。
  stop-when 直接注册到两个输入；source keepalive 覆盖整个 stop 调用，
  receiver registration 在 completion 前注销，见
  [`inplace-stop-callback-design.md`](inplace-stop-callback-design.md)。
- Serial `bulk` / `bulk_chunked` / `bulk_unchunked`、`transfer_just` extension，
  以及 `affine` 的通用 make-sender surface 差异 都是已接受
  residuals。只有出现具体 user-visible problem 或 native-handoff blocker 时再回看。
- 常规验证矩阵中尚无稳定主流 native `std::execution` 实现，因此 execution 自身的 native
  handoff 仍是未来 integration risk。Synthetic COMPLETE fixture 会检查当前 probe surface；
  `as_awaitable`、`schedule_from`、`transform_sender` 和 `apply_sender` 等独立 PARTIAL roots 会
  分别验证任一已出现的 native surface 都让 Forge 整体 stand aside。这些 fixture 不能替代
  未来真实 native implementation lane。

## stdexec 可行性状态

stdexec 属于可选参考实现探测，不代表 native handoff 完成。流程、命名检查和跳过规则统一见[测试说明](../testing.md)。

## 后续有价值的检查

1. 继续把 `scripts/verify-native.sh gcc-exec` 作为当前 libstdc++ execution backport lane。
2. 只有本地存在 stdexec checkout 时才运行 `scripts/probe-stdexec-feasibility.sh`；审查每个命名检查结果，不只看最终 `result=passed`。
3. 如果 stdexec 对比变得有价值，adapter layer 设计需负责人另行决定，并明确哪些 examples/tests 必须同时在 Forge 和 stdexec 上 portable。

## 快照范围与待复查事项

这是 2026-08-23 快照，之后的提交未复审；下一次工作草案快照审计时需重新核对整个台账，需负责人另行决定。

两种 counting scope 的 `count()` 和 `is_closed()` 是已弃用的 Forge 诊断扩展，改用 `forge::scope_count` 与 `forge::scope_is_closed`（仅 backport）。`spawn_future` 的 allocator 只覆盖 shared state 和 consumer record，不覆盖溢出子 op 或 callback 控制块。

## 扩展入口弃用与迁移

[执行库迁移表](../backports/execution.md#扩展入口弃用与迁移)列出新弃用入口。
`env_domain_t`、`receiver_domain_t`、`sender_domain_t` 改用查询表达式。
`sender_to`、`queryable` 为 exposition-only 概念，仅文档弃用。
Forge 的 erasure 与 runtime 约束改用由标准词汇构成的内部概念。
`get_completion_signatures_t` 等现有 CPO 类型名留待接口形状调整。
域 transform 与 progress 的内部分支诊断可能被系统头文件模式隐藏。

以下旧入口保留至少一个弃用发布版本；可独立标注的入口使用 `[[deprecated]]`，计划在后续版本移除，具体版本由维护者决定。既有调用的完成行为、异常传播和结果类型保持不变。等待入口的两个 `std::execution` 别名继续通过 using-declaration 引用 `std::this_thread` 中的同一对象，以保留实体身份和同时导入两个命名空间时的非限定查找；它们仅在文档中弃用，不单独添加属性。

| 弃用入口 | 替代入口 |
| --- | --- |
| `std::execution::split(s)` | `forge::split(s)` |
| `std::execution::empty_env` | `std::execution::env<>` 或 `forge::empty_env`（同一类型） |
| `std::execution::transfer_just(sch, values...)` | `forge::transfer_just(sch, values...)` 或 `std::execution::continues_on(std::execution::just(values...), sch)` |
| `std::execution::make_prop(q, value)` | `std::execution::prop(q, value)` |
| `std::execution::sync_wait(s)` | `std::this_thread::sync_wait(s)`；依赖扩展结果形态时使用 `forge::sync_wait(s)` |
| `std::execution::sync_wait_with_variant(s)` | `std::this_thread::sync_wait_with_variant(s)` |
| `std::this_thread::sync_wait(s)` 接受多种 value completion 的扩展 | `forge::sync_wait(s)` 保持原 tuple-or-variant 结果；标准替代为 `std::this_thread::sync_wait_with_variant(s)`，其结果形态不同 |
| `std::this_thread::sync_wait(s)` 接受零种 value completion 的扩展 | `forge::sync_wait(s)`（仅 backport 保证该行为） |
| `simple_counting_scope::count()` / `counting_scope::count()` | `<forge/execution_extensions.hpp>` 的 `forge::scope_count(scope)`（仅 backport） |
| `simple_counting_scope::is_closed()` / `counting_scope::is_closed()` | `forge::scope_is_closed(scope)`（仅 backport） |
| `std::execution::receiver_of`（仅文档弃用） | 使用 `receiver` 和标准完成调用要求。旧概念比工作草案的 exposition-only 概念更严格，在约束中排除可能抛出的完成调用。 |

Forge 入口由 `<forge/execution_extensions.hpp>` 提供，也由 `<forge/execution.hpp>` 汇总。`forge::split` 与原入口共用缓存实现，保持惰性启动、单次执行和 `const&` 重放语义。它依赖 backport 的共享状态，仅在 `FORGE_EXECUTION_BACKPORT_ACTIVE` 已定义时提供；原生 execution 让位时不注入此算法。

`forge::transfer_just` 仅组合标准 `just` 与 `continues_on`，完整原生实现下也可用。`forge::empty_env` 在 backport 与完整原生实现下均为 `std::execution::env<>`，不再保留旧空环境的独立类型身份。`forge::sync_wait` 在 backport 下保留旧的 tuple-or-variant 行为，在完整原生实现下转用标准单一 value completion 的 `std::this_thread::sync_wait`；跨实现消费多种 value completion 应使用标准 `sync_wait_with_variant`。零种 value completion 同样超出草案要求，旧标准拼写已弃用；backport 下改用 `forge::sync_wait`，保留 stopped 返回空 optional、error 抛出的行为，原生让位时不支持此扩展。

依据为 2026-11-13 查阅的 [execution synopsis](https://eel.is/c++draft/execution.syn)、[env](https://eel.is/c++draft/exec.env)、[prop](https://eel.is/c++draft/exec.prop) 与 [sync_wait](https://eel.is/c++draft/exec.sync.wait)。

计数 scope 的观察入口是诊断快照，不提供跨字段一致性或额外同步保证。旧成员保留至少一个带 `[[deprecated]]` 的发布版本，移除版本由维护者决定；reserved observer 仅用于实现与白盒测试，不属于公共 API。Forge 观察入口只接受 backport 的两种 scope 类型，原生让位时不提供。依据为 [counting scopes](https://eel.is/c++draft/exec.counting.scopes) 的成员列表及 [sync_wait](https://eel.is/c++draft/exec.sync.wait) 的恰好一种 value completion 要求。

## Linalg 与 mdspan 的草案对齐

| 范围 | 当前实现 |
| --- | --- |
| `std::linalg` 调用形式 | 移除旧 Triangle 顺序、额外的 rank-2 / rank-2k alpha 调用及历史 SSQ。项目尚未发布，未设弃用期。逐项迁移见 [linalg 说明](../backports/linalg.md#已移除的非草案形式)。 |
| Norm 与 Givens 类型约束 | 两范数检查元素与 `Scalar` 的浮点或复数类型，Givens 检查浮点 `Real`；policy twin 使用相同约束。整数 `matrix_one_norm`、`matrix_inf_norm` 与 `vector_abs_sum` 保留饱和行为。 |
| Forge 扩展 | backport 注入时提供整数范数、有符号整数 Givens 及 SSQ。原生 `<linalg>` 定义特性宏时，范数和 Givens 包装器转发到原生重载。迁移入口见 [Forge 线性代数扩展](../forge-utilities.md#线性代数扩展)。 |
| Policy 重载 | 原生 policy 对象可用时提供 78 个重载。调用线程内顺序运行，元素访问异常触发 terminate；`<linalg>` 使用原生检测。 |
| Mapping 与宏 | `layout_transpose::mapping` 从精确 nested mapping 显式构造；submdspan 测试通过 ADL 调用 mapping customization。注入的 `<linalg>` 定义 `__cpp_lib_linalg = 202511L`，原生探测先于注入。 |

### Adaptor 的 WD 措辞问题

- 调度 hop（schedule-from lowering）在 `[exec.continues.on]/5` 以 `schedule_result_t<sched_t>`（rvalue）命名 operation，却实际 `connect(schedule(sch), ...)`（非 const lvalue），/6 的 check-types 同样使用 rvalue。本实现选择实际调用的类别，starts_on 的直接 lowering 同步采用此规则；ref-qualified schedule 的 completion 集合与 connectability 均以该左值为准。当前草案 `[exec.schedule.from]/1` 是单参数 departure marker，混合类别的具体措辞在前述 continues_on 段落。

- SCHED-ENV 仅转发 `sch.query(get_domain)`，当前 scheduler 通常只回答 completion-domain；字面实现会让目标工作继承 outer domain。与此同时 inline-attrs 使用 `get_scheduler(env)` 发布 completion scheduler，却在 `get_start_scheduler(env)` 的 agent 启动。`starts_on` 保留目标 scheduler/domain 答案，等待两处协调。
- `[exec.starts.on]/4` 的 lambda nothrow 取自整个 OutSndr（含 scheduler），不是仅 child；实现逐字遵循。
- `[exec.let]/9`、/12 用 FWD-ENV 推导 predecessor，/10 却连接未过滤环境；本实现选择 /10，使签名与实际连接一致，stopped_as_error 同样保留未过滤环境。
- `[exec.continues.on]/2` 仅规定二参数 CPO 调用。P2300R10 指定为 pipeable sender adaptor object；
  P3826R3 移入原 schedule_from 措辞，未解释为何省去 pipeability。保留 partial application 和 pipe，
  等待工作草案澄清。
- `[exec.bulk]/6` 的 check-types 误用 unchunked arity；按 complete 的 requires-clause 与 /9.1.1.3 使用 chunked 的两个 Shape 参数。
- `[exec.snd.expos]/37` 将默认 get-state 声明 noexcept，/54–55 的 uses-allocator 重建却可能抛出；本实现逐字遵循 noexcept get-state，分配或重建失败会 terminate。/28 中 allocator 路径返回同类型 prvalue，只计 receiver 移动与 child connect（有效 receiver 的移动项恒为 true）；无 allocator 时还计 data 的相应构造。allocator 存在本身不增加 let/starts_on 的 exception_ptr。

默认 get-state 的 allocator-aware construction 已用于 factories、then/upon、bulk 与 write_env；拥有自有 get-state 的算法不直接套用。Locked when_all、associate、split 与 counting_scope 的 signature SFINAE 改造保留待后续处理。

### Coroutine 桥的 WD 措辞与实现边界

- `[exec.awaitable]/5` 的 `has-as-awaitable` 把 `Promise&` 用作 coroutine promise 类型；实现采用 Promise。
- `[exec.as.awaitable]/1` 的 `awaitable-sender`（要求 unhandled_stopped）未被 /7.4 使用；本实现按 connect 的 receiver-of Mandate，在实际 awaitable receiver env 检查 stopped 能力。
- /7.4 的 class-template Sndr&& 无法按字面从 lvalue CTAD，保留 decay-copy；receiver environment 只规定 forwarding queries 的答案，没有禁止其他查询。
- `connect` 的 coroutine frame 使用全局分配；when_all、split、associate、counting-scope wrap、spawn_future 与 sync_wait 间接接收 awaitable 时会包含这项 child allocation，原来针对不自行分配 children 的保证保持成立。spawn 仍拒绝 exception_ptr completion。
- Forge start_detached、async_scope / resource_context、any_sender_of、split / sync_wait、io::await_sender 接受实际环境及结果契约兼容的 awaitable；any_sender_of 对 copyable source 的 lvalue 借用契约保持，环境不兼容的 scope / IO 调用由约束拒绝。
- GET-AWAITER 的 emulation 保守拒绝同时存在 member / ADL 候选的情况；deleted、inaccessible 或 ambiguous operator call probe 视为缺失，若另有有效 awaiter 方法则满足 sender，而实际 await/connect 为 ill-formed；无法在形成 await expression 前区分它与仅值类别不匹配的 operator（owner 接受的边界）；final promise 上不可调用的 overloaded / template await_transform 无法用继承式 name probe 完整识别。普通 bridge 的既有 member-first acceptance leniency 保留。Constexpr-exception diagnostics 仍属独立未实现项。

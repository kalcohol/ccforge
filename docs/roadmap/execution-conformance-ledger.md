# `std::execution` 一致性台账

这份台账记录最近几轮 runtime hardening 后，Forge execution backport 的当前状态。它刻意与
用户向的 [`std::execution`](../backports/execution.md) 文档分开：后者说明如何使用，本文是为
future native handoff 和 conformance work 保留的工程审计记录。

当前收敛清单见
[`execution-wd-convergence-checklist.md`](execution-wd-convergence-checklist.md)。
Owner 已接受为了贴近当前 working draft 而做 breaking API change，因此下表的
"native-handoff action" 优先标准收敛，而不是保留旧 extension spelling。

## 当前实现状态

本快照在 2026-07-31 对照 live working draft `[exec]`
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
  completion-signature computation 中的 sender-env completion-domain 递归 `transform_sender`。

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
| `just`, `just_error`, `just_stopped` | Implemented | `just.hpp`；MVP/wave 测试覆盖。 |
| `read_env`, `write_env` | Implemented subset | `read_env.hpp`, `write_env.hpp`；一参数 environment/scheduler query 及组合 environment 保持 member-query-first、tag-invoke fallback 和 leftmost-wins。 |
| `then`, `upon_error`, `upon_stopped` | Implemented | `then.hpp`, `upon.hpp`；可支持时 exception fallback 报告 `std::exception_ptr`。 |
| `let_value`, `let_error`, `let_stopped` | Implemented subset | `let.hpp`；lifecycle-sensitive storage 由 execution adaptor 测试覆盖。Inner sender/op 只能在上游 completion 后选择和构造，late-connect 失败因此保留 `set_error(std::exception_ptr)` implementation channel。 |
| `starts_on`, `continues_on` | Implemented subset | `on.hpp`, `continues_on.hpp`；以 CPO object 暴露 direct-call，`continues_on` 也支持标准 scheduler closure pipe。`starts_on` 在 outer `connect` 中预连接 source，connect failure 不扩大 completion signatures；schedule errors 由 scheduler sender 声明。`continues_on` 的动态 schedule-op 构造失败仍形成真实 `exception_ptr` channel。`continues_on` 当前只覆盖单一 value completion shape，value 参数会 decay-copy 到调度 hop；为转移后的 value / stopped completion 发布 destination scheduler，但不为可能在 schedule failure agent 上发生的 error disposition 声称唯一 scheduler；completion domain 以 member-query-first、tag-invoke fallback 从 destination scheduler/schedule sender 派生，不会借用 child domain；其余 child attributes 只转发 `forwarding_query`；多 alternative / 引用 value-signature 的逐位 WD 语义需单独重构。 |
| `on` | Implemented subset | `on.hpp` 暴露 current-WD 两种形式。Closure form 要求 child attributes 通过本 backport 的 one-argument query model 暴露 `get_completion_scheduler<set_value_t>`。 |
| `bulk` | Implemented serial subset | `bulk.hpp`；当底层标准库暴露 execution policy traits/objects 时接受 current-WD policy-shaped calls，并在 completing agent 中串行执行。Policy 不引入并行。 |
| `bulk_chunked`, `bulk_unchunked` | Implemented serial subset | `bulk.hpp`；当底层标准库暴露 execution policy traits/objects 时接受 current-WD policy-shaped calls。`bulk_unchunked` 等价 serial `bulk`，`bulk_chunked` 使用一个非空 `[0, shape)` chunk。 |
| `unstoppable` sender adaptor | Implemented | `unstoppable.hpp`；用 thin `write_env` wrapper 注入 `never_stop_token`。 |
| `stopped_as_optional`, `stopped_as_error` | Implemented | `stopped_as.hpp`；提供 direct-call；`stopped_as_optional` CPO 自身可直接 pipe，nullary closure 保留为 source-compatibility extension；`stopped_as_error(error)` 返回标准 pipe closure。 |
| `into_variant` | Implemented | `into_variant.hpp`；CPO 自身支持 direct-call 和 bare pipe；nullary closure 保留为 source-compatibility extension，并被 `sync_wait` / `when_all` value-shape handling 复用。 |
| `when_all`, `when_all_with_variant` | Implemented subset | `when_all` 按 WD 要求至少一个 child sender；Cartesian value signature support 和 outer stop propagation 已实现；修改 shared state 时保留 lifecycle tests。 |
| `split` | Implemented subset | `split.hpp`；非 WD extension。缓存单一 value completion shape 并以 `const&` 广播，impossible empty result state 会 fail-fast terminate；内部 callback 入链分配失败以 `set_error(std::exception_ptr)` 完成。完整 stop-source/on_done cycle 未实现；abandoned source sender 是已接受 residual。 |
| `sync_wait`, `sync_wait_with_variant` | Implemented subset | 支持多个 value alternatives；同步 typed-error consumption 保持为 Forge `wait_result` extension，而不是 `sync_wait`。 |
| `associate`, `spawn` | Implemented current-WD subset | 已实现 top-level association 和 fire-and-forget spawn；`associate` 在 outer `connect` 中预连接 wrapped sender，connect failure 不成为 completion channel；`spawn` 只接受 `set_value()` / `set_stopped()` senders。 |
| `spawn_future` | Implemented subset | Eager single-consumer future sender；value/error 结果按 decayed types 存储、声明并以 rvalue 交付；shared state 和 consumer records 尊重 `get_allocator(env)`；consumer stop callback 直接使用 receiver env 的具体 token 类型。 |
| `inplace_stop_source/token/callback` | Implemented subset | Callback invocation / deregistration concurrency 和 Forge reentrant self-destroy paths 已测试；registration 当前使用独立 control block，因此会分配且 constructor 未满足 current-WD conditional `noexcept`。见 [`inplace-stop-callback-design.md`](inplace-stop-callback-design.md)。 |
| `simple_counting_scope`, `counting_scope` | Implemented current-WD-shaped subset | Token `wrap`、top-level association/spawn、`counting_scope` fused stop-token injection 和 async sender-returning `join()` 已实现。 |
| `as_awaitable`, `with_awaitable_senders` | Implemented subset | `with_awaitable_senders` 提供 current-WD-shaped continuation / stopped 传播，并保留普通 awaitable；sender bridge 对零值、单值和单一多参数 value completion 分别返回 `void`、该值类型和 `tuple<...>`，并拒绝多 value-alternative sender。 |
| `affine` | Implemented subset | `affine.hpp`；单参数 CPO / bare-pipe 形式从 receiver env 查询 `get_start_scheduler`，通过 unstoppable schedule sender 复用 `continues_on` transfer subset；因此仍受单一 value completion shape 限制。 |
| `get_env` | Implemented subset | Member-first，tag-invoke fallback，默认 `empty_env`。 |
| `sender_tag`, `receiver_tag`, `operation_state_tag`, `scheduler_tag`, `tag_of_t` | Implemented subset | 暴露 current-WD marker spelling 和 basic-sender-shaped `tag_of_t`；旧 `*_t` spelling 保留为 source-compatibility aliases。 |
| `get_scheduler` | Implemented subset | Member `.query` 优先、tag-invoke fallback；组合 environment 保持 leftmost-wins。 |
| `get_start_scheduler` | Implemented subset | Member `.query` 优先、tag-invoke fallback；`make_prop` / `write_env` / `affine` forwarding tests 覆盖。 |
| `get_delegation_scheduler` | Implemented subset | Member `.query` 优先、tag-invoke fallback；`make_prop` / `write_env` forwarding tests 覆盖。 |
| `get_completion_scheduler` | Implemented subset | Member `.query` 优先、tag-invoke fallback；scheduler envs 暴露 roundtrip。 |
| `forwarding_query` | Implemented subset | 暴露 current WD query，支持 member `.query(forwarding_query)` 和 Forge tag-invoke fallback；Forge query objects 按需声明 forwarding。 |
| `get_await_completion_adaptor` | Implemented subset | 为 coroutine environments 暴露 member-first、tag-invoke fallback query object；没有提供 default adaptor。 |
| `get_domain`, `get_completion_domain` | Implemented subset | 一参数/双参数查询均采用 member-query-first、tag-invoke fallback。Recursive `connect` transform model 已存在；非 default-domain `get_completion_signatures(sender, env)` 会先通过 transformed sender type 重算再读取 signatures，包括 rawless source senders 经 transform 获救的情况。 |
| `get_allocator` | Implemented subset | Member `.query` 优先、tag-invoke fallback；用于 `spawn` / `spawn_future` allocator paths。`empty_env` 没有 default allocator query。 |
| `get_stop_token` | Implemented subset | Current-WD member `.query(get_stop_token)` 优先，保留 tag-invoke fallback；两者都缺失时返回 `never_stop_token`。 |
| `get_forward_progress_guarantee` | Implemented subset | Member `.query` 优先、tag-invoke fallback；对 local scheduler-shaped types 提供 `weakly_parallel` fallback，内置 backport schedulers 和 `forge::static_thread_pool` 报告保守值。 |

### 未实现 / 暂不纳入的 current-draft surface

| Surface | 处置 | 原因 / 后续条件 |
| --- | --- | --- |
| `task`, `task_scheduler`, `with_error` | Not implemented | P3552 coroutine task family 尚未 backport。`forge::task` / `forge::io::io_task` 是不同命名空间下的 Forge extensions，不能当作该标准 surface。只有形成独立 taskbook、生命周期模型和 native-handoff 测试后才考虑。 |
| `parallel_scheduler`, `get_parallel_scheduler` | Out of scope | 当前 `bulk` family 是明确的 serial subset；本仓库没有可承诺标准 parallel scheduler 语义的 execution resource。 |
| `dependent_sender_error`, dependent-sender concept surface | Not implemented | 当前 completion-signature diagnostics 不建模这组 dependent sender contract。需要先有可移植编译器/参考实现证据。 |
| `indeterminate_domain` | Not implemented | 当前 domain dispatch 使用本 backport 的 `default_domain` 和显式 scheduler/env customization subset。 |
| `inlinable_receiver` | Not implemented | 当前 operation-state ownership 不对外承诺该优化查询；不能由现有 inline completion 行为推导。 |
| `sender_adaptor_closure` | Out of scope as public type | Adaptors 使用内部 closure machinery 并提供 pipe syntax，但不暴露 current-draft public closure base/type。 |
| `schedule_from` | Not implemented | 当前 `continues_on` 是直接实现；尚无可诚实表达 scheduler/domain customization 的 `schedule_from` CPO。 |
| `apply_sender`, `transform_env` | Not implemented | 当前 domain subset 有 `transform_sender`，但没有这两个 current-draft customization surfaces；只有形成配套 domain tests 后才实现。 |

## 兼容性分类

下表区分 current-draft conformance work 和 Forge convenience extensions。它是 native handoff
risk triage 的事实来源。

| Item | 分类 | 当前状态 | Native-handoff action |
| --- | --- | --- | --- |
| Library adaptor non-copyable lvalue `connect` | 已收敛的 standard-shaped behavior | Standard backport sender 对 move-only lvalue 要求显式 `std::move(sndr)`。 | 保持测试和示例显式 move；不要在标准路径恢复 destructive lvalue connect。 |
| `forge::async_scope::spawn(lvalue)` | Forge extension convenience | 对 non-copyable non-const lvalue sender 做 destructive move，作为 Forge runtime convenience。 | 继续文档化为 Forge-only extension；native-friendly 示例写 `scope.spawn(std::move(sndr))`。 |
| `std::execution::ensure_started` | 已移除的 non-WD extension name | `<execution>` backport 不再暴露。 | 保持在 `std::execution` 外；未来如需工具，只能放入 `forge::`。 |
| `std::execution::start_detached` | 已移除的 non-WD extension name | `<execution>` backport 不再暴露；Forge runtime code 使用 `forge::start_detached`。 | 标准路径保持使用 `spawn`；detach utility 保持在 `forge::`。 |
| `std::execution::spawn` | Implemented current-WD subset | Top-level `spawn(sender, token[, env])` 分配 detached state，通过 `token.try_associate()` 关联并 eager start。只接受 `set_value()` / `set_stopped()` senders。 | 测试继续对齐 current-WD fire-and-forget spelling。 |
| `std::execution::counting_scope::join()` | Implemented current-WD-shaped subset | `simple_counting_scope::join()` 和 `counting_scope::join()` 返回 async senders；空 scope 在 `start()` 调用线程内联完成，非空 scope 的 count drain 在 mutex 外启动 receiver env 的 start-scheduler operation，并在完成前建立 terminal `joined` state。 | Destructor 只诊断 outstanding count，未强制 current-WD 对 naturally-drained-but-unjoined state 的 terminate；保持 zero-count inline / real-drain 后拒绝 association、start-scheduler handoff、last-decrement vs join-register races 压力测试。 |
| Scope-token `wrap` / `associate` / member `spawn` | 已收敛 surface，语义实现当前目标 | Token-member `associate` / `spawn` 已移除。`simple_counting_scope::token::wrap` 是 identity forwarding；`counting_scope::token::wrap` 给 child env 暴露 fused stop token，scope stop 与下游 receiver/env stop 任一请求都会让 child 观察到 stop。Top-level `associate` / `spawn` / `spawn_future` 拥有 association。 | 继续测试 allocator/env、fused stop-token lifetime 和 async join details；不要在 `std::execution` 恢复 token-member helpers。 |
| Throwing receiver completion callbacks | 刻意 unsupported boundary | `set_value`、`set_error`、`set_stopped` 必须 `noexcept`；negative compile probe 强制该边界。 | 除非有聚焦任务重写 completion dispatch，否则保持拒绝。 |
| Execution domain dispatch | Tested current-WD subset | `connect` 应用 sender completion-domain recursion，再应用 receiver start-domain recursion；非 default-domain 的 `get_completion_signatures(sender, env)` 使用同一 transformed sender type。 | 保持 recursive transforms 和 transformed-signature computation 覆盖，包括 rawless source senders。 |
| `forge::any_scheduler` | Forge local utility | 建模 Forge local scheduler concept，使用 shared-state identity equality 和 backport CPO completion-scheduler roundtrip。 | Native member-query scheduler roundtrip 仍是 forward-compat caveat。 |
| `forge::wait_result` | Forge local utility | 同步保留 value、stopped 和 closed-set typed error，不通过 throw 表达。 | typed error 需要跨同步边界时使用；它不是 `std::this_thread::sync_wait`。 |
| `forge::erased_sender` | Forge local utility | Connectable erased sender，支持保留引用类别的 multiple value shapes、closed-set typed errors 和 bounded env/stop-token forwarding。 | 保持在 `forge::`；reference payload 只在同步 completion stack 内借用；不要当作标准 execution surface。 |
| Receiver env stop-token propagation | Forge utilities 的 required behavior | `wait_result`、`erased_sender`、runtime senders 和 IO wrappers 在支持的 env model 中保留 receiver stop-token visibility。 | 修改 type erasure 或 wrapper receivers 时保持回归测试。 |

## 已接受 residuals 与未来风险

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
- `inplace_stop_callback` 的 allocation / exception-spec 偏差是独立 residual，不由上述
  `forge::any_stop_token` 决策覆盖。当前为保留已验证的 reentrant destruction behavior 暂缓
  allocation-free rewrite；见
  [`inplace-stop-callback-design.md`](inplace-stop-callback-design.md)。
- Serial `bulk` / `bulk_chunked` / `bulk_unchunked`、`transfer_just` extension，
  以及 `affine` 的 single-value transfer subset 都是已接受
  residuals。只有出现具体 user-visible problem 或 native-handoff blocker 时再回看。
- 常规验证矩阵中尚无稳定主流 native `std::execution` 实现，因此 execution 自身的 native
  handoff 仍是未来 integration risk。

## stdexec 可行性状态

NVIDIA stdexec 可以作为 sender/receiver 的语义参考实现。它不是本仓库 native-handoff lane
的直接替代：

- Forge 通过 `<execution>` 暴露 `std::execution`；
- stdexec 暴露自己的 `stdexec::` surface 和 `<stdexec/execution.hpp>` 等 header；
- 有意义的 compatibility lane 需要先有 adapter/shim plan，才能测试 Forge `include/forge/`
  utilities 是否能面向 stdexec 工作。

可选脚本 `scripts/probe-stdexec-feasibility.sh` 会检查用户本地提供的 stdexec checkout，以及
一小组命名的 Forge execution facilities。它不会 fetch stdexec，也不属于默认 verification
floor。`STDEXEC_ROOT` 缺失时，它用 skip code 77 退出并打印 `result=skipped`；成功时打印
`result=passed`。

当前命名检查：

- `stdexec_just_smoke`：stdexec headers 能编译一个极小 `stdexec::just` 程序；
- `forge_execution_sync_wait`：Forge `<execution>` backport 能运行 `sync_wait(just(42))`；
- `forge_wait_result_typed_error`：`forge::wait_result` 保留 closed-set typed error；
- `forge_erased_sender_typed_error`：`forge::erased_sender` 让同一 typed error 跨 erased sender boundary；
- `forge_any_scheduler`：`forge::any_scheduler` 建模 local Forge scheduler concept 并能 schedule；
- `forge_receiver_stop_env`：receiver env stop-token propagation 仍可通过 `forge::wait_result` 观察。

这些检查是 feasibility ledger，不是 compatibility proof。它们不会把 Forge `std::execution`
代码适配到 stdexec namespace 上，也不应被当成 native `std::execution` handoff 完成的证据。

当前 convergence round 的本地状态：

- `STDEXEC_ROOT` 缺失：按预期 skip，exit code 77；
- `STDEXEC_ROOT` 指向本地 stdexec checkout：所有命名检查通过；
- 这仍是可选 reference probe，不提升为默认 verification floor。

## 后续有价值的检查

1. 继续把 `scripts/verify-native.sh gcc-exec` 作为当前 libstdc++ execution backport lane。
2. 只有本地存在 stdexec checkout 时才运行 `scripts/probe-stdexec-feasibility.sh`；审查每个命名检查结果，不只看最终 `result=passed`。
3. 如果 stdexec 对比变得有价值，单独写 taskbook 设计 adapter layer，并明确哪些 examples/tests 必须同时在 Forge 和 stdexec 上 portable。

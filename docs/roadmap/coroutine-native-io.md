# Coroutine-native byte IO 路线图

本文记录 Forge 如何跟踪 WG21 Network Endeavor / coroutine-native IO 的 C++29 讨论，
并把其中对本仓库有用的 Stage One 能力吸收到 `forge::` extension layer。它不是 `std::io`、不是
`std::networking` backport，也不是完整网络库计划。

## 当前判断

Forge 可以吸收的是“可替换 backend 的异步字节流支撑层”，而不是 TCP/DNS/UDP/TLS
网络栈本身。优先级从纯 vocabulary 到 backend façade 递进：

1. byte buffer 与 compound IO result vocabulary；
2. memory/scripted streams，用于无 OS handle 的协议测试；
3. read/write stream concepts 和 type-erased stream boundary；
4. coroutine-native awaitable/execution substrate 的小型 Forge extension；
5. sender/coroutine interop bridge；
6. 现有 `forge::io::context` 的 coroutine-facing façade，保持 Linux readiness 和
   Windows IOCP 语义诚实；
7. 小型 IO-aware helper/combinator 和 protocol examples。

这些设施服务 Forge 既有身份：C++ backport 加面向资源型异步系统的组合式支撑层。完整
networking framework、socket option surface、DNS、TLS、Boost.Asio/Capy/Corosio 适配器
和新平台 backend 都需要独立 taskbook 与 owner decision。

## 当前实现状态

截至 Stage 8 收敛，已实现的 public surface 都位于 `forge::io`，并保持 direct-header
使用：

- `<forge/io/result.hpp>`：`io_result<Ts...>` compound result。
- `<forge/io/buffer.hpp>`：borrowed byte buffers 和 buffer sequence helpers。
- `<forge/io/memory_stream.hpp>`：memory/scripted streams。
- `<forge/io/stream.hpp>`：stream concepts、`read_exactly`、`write_all`、`read_until`、
  borrowed `any_read_stream` / `any_write_stream`。
- `<forge/io/coro.hpp>`：`io_env`、`io_task`、`await_sender`、`as_sender`。
- `<forge/io/timer_await.hpp>`：backend-free facade over `forge::timer_context` sender，
  提供 `async_sleep_for` / `async_sleep_until`。
- `<forge/io/context_await.hpp>`：bridge over existing `forge::io::context`
  operations when a backend exists.
- `<forge/io/combinators.hpp>`：two-child `when_all_results` proof helper for
  `io_task<io_result<...>>` composition.

Umbrella policy 保持保守：`<forge/io.hpp>` 继续只暴露 OS backend `context`（在 backend
可用时），不把纯 vocabulary 或 coroutine headers 自动拉入；`<forge/execution.hpp>`
不承载 coroutine-native byte IO track。当前不新增 feature-test-like Forge macro；用户需要
backend 能力时继续使用既有 `FORGE_HAS_FORGE_IO_*` 宏，纯 header 设施以 direct include 和
普通 C++ name detection 为主。

## 提案基线

这些提案仍在移动中。每个实现 stage 开始前必须重新检查官方 WG21 paper index，而不是只依赖
本文或外部文章。

截至 2026-06-26，官方 WG21 2026 paper index 的 2026-05 mailing 显示：

| Paper | Facility | Status in index | Useful Forge capability | Planned stage | Why not `std` in Forge |
| --- | --- | --- | --- | --- | --- |
| P2583R4 | Symmetric transfer and sender composition | Info, All of WG21 | `io_task` 父子 await 采用 symmetric transfer 的设计依据；解释 sender completion 路径为何不承诺 handle 返回 | Stage 4/5 | 提议修改 sender 完成协议，conformant `<execution>` backport 不能吸收 |
| P4003R3 | Minimal coroutine execution model: `IoAwaitable`, `io_env`, executor shape | Ask, LEWG | 实验性 coroutine execution substrate、env propagation、executor adaptation | Stage 4 | 尚非 adopted wording；Forge extension 不向 `namespace std` 加名 |
| P4007R3 | Open issues in `std::execution::task` | Info, All of WG21 | 审计 `io_task`/`as_sender` 的 ownership、completion 和 error/stopped 边界 | Stage 5 | 讨论 `std::execution::task` 的开放问题，不定义 Forge 可注入的标准 surface |
| P4036R0 | Why span is not enough: dedicated buffer vocabulary | Info, All of WG21 | `buffer.hpp` 使用专用 byte-region descriptor 而非裸 `std::span` 的设计依据 | Stage 1 | rationale paper，不定义可 backport 的标准 surface |
| P4088R1 | Coroutine mechanics and rationale | Info, All of WG21 | 设计依据：type erasure、frame state、separate compilation 的取舍 | Stage 0/4 | rationale paper，不定义可 backport 的标准 surface |
| P4089R1 | Diversity of coroutine task types | Info, All of WG21 | `io_task<T>` 保持单模板参数、以协议而非 task 类型作为互操作点的依据 | Stage 4 | survey/rationale，不定义标准 surface |
| P4090R1 | Sender I/O constructed comparison | Info, All of WG21 | `io_result` 不隐式拆 sender 通道、要求显式 adapter 的论据 | Stage 5 | comparison paper，不是 API wording |
| P4091R1 | Error models and the abstraction floor | Info, All of WG21 | compound result 跨 sender 边界的 abstraction-floor 分析，支撑 `as_sender` 单 value 传递策略 | Stage 5 | error-model 分析，不定义标准 surface |
| P4092R1 | Consuming senders from coroutine-native code | Info, All of WG21 | `await_sender` 类 bridge，与现有 `std::execution`/Forge runtime 互通 | Stage 5 | 仍是 bridge sketch；不改变 `<execution>` backport 行为 |
| P4093R1 | Producing senders from coroutine-native code | Info, All of WG21 | `as_sender` 类 bridge，并明确 compound IO result 的通道边界 | Stage 5 | compound result 映射策略未标准化，不能静默进 `std` |
| P4100R1 | Network Endeavor overview | Info, All of WG21 | 分阶段路线：先纯 C++ abstractions，后 platform IO | Stage 0/8 | overview paper；明确 Stage Two 才涉及 platform networking |
| P4123R0 | Cost of senders for coroutine IO | Info, All of WG21 | 性能风险清单，帮助决定哪些 bridge 不应进入 hot path | Stage 5/8 | 证据/rationale，不是 API wording |
| P4124R0 | IO-aware combinators and compound results | Info, All of WG21 | `io_result` 与 domain-aware helper/combinator 设计约束 | Stage 1/7 | helper 语义仍在讨论，先做 `forge::` 实验 |
| P4125R1 | Production field report | Info, All of WG21 | 风险/收益参考，不作为 Forge API 依据 | Stage 0/8 | field report，不定义接口 |
| P4126R1 | Universal continuation model | Info, All of WG21 | 检查 coroutine final continuation、inline completion 和 resume 位置的设计风险 | Stage 5 | continuation model 仍是设计资料，不是稳定 library wording |
| P4127R0 | Coroutine frame allocator timing problem | Info, All of WG21 | 解释为什么 `io_env::memory` 只传播 pointer，不承诺接管 coroutine frame allocation | Stage 4/8 | frame allocation timing 尚无可实现的标准 contract |
| P4166R0 | Frame-visible coroutines for senders | Info, All of WG21 | 长期方向：compound result preservation、frame visibility 与 sender zero-allocation 取舍 | Deferred | 需要语言能力，不是 header-only library 可以 backport 的设施 |
| P4172R1 | IoAwaitable design rationale | Info, All of WG21 | env propagation、executor_ref、frame allocator decision record | Stage 4 | companion rationale，不是稳定标准 header |
| P4178R0 | Async abstraction trade-offs | Info, All of WG21 | 用于 review sender/coroutine 边界是否合理 | Stage 5/8 | trade-off paper，不定义可注入标准名 |

复查 2026-08-19：2026-07 与 2026-08 mailing 没有 Network Endeavor 新论文或修订，上表仍是
最新基线。P4094R1-P4099R1 历史审计块与 P4041R0/P4046R0/P4047R0/P4048R0/P4207R0 等
methodology/process 论文刻意不进入本矩阵：它们不定义可实现的 library surface。

官方索引：

```text
https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/
```

## 与当前 Forge surface 的关系

应复用的既有设施：

- `forge::io::context`：继续作为 Linux `epoll/eventfd` readiness 和 Windows IOCP
  completion proof；后续 façade 只能包装现有语义，不能假装所有平台都是同一种模型。
- `forge::task`：当前是 sender-shaped coroutine task，基于
  `std::execution::with_awaitable_senders`。是否扩展它承载 `io_env` 必须单独审计；默认不要
  破坏现有 final-suspend completion 和 sender awaitability。
- `forge::erased_sender`：已支持 connectable typed-error sender erasure 和 downstream
  stop token forwarding。新 bridge 应优先复用它，而不是扩大 `any_sender_of`。
- `forge::any_sender_of`：保持窄 sync-wait convenience，不作为通用 erased sender 扩展。
- `forge::resource_policy` 与 runtime lifecycle vocabulary：新增 coroutine-native primitive
  必须继续使用 `close()`、`request_stop()`、`shutdown()`、`wait()` 的既有含义。

## 分阶段计划

### Stage 0：recon 与 roadmap

产出本文和 proposal matrix。只做文档和任务拆分，不引入 public library surface。

### Stage 1：纯 byte vocabulary

候选 header：

```text
include/forge/io/result.hpp
include/forge/io/buffer.hpp
```

目标是提供 `io_result<Ts...>`、byte-region descriptors、buffer sequence helpers。它们不依赖
coroutine、sender 或 OS backend。

### Stage 2：memory/scripted streams

目标是让协议代码在没有 socket、pipe、file descriptor 的情况下测试 short read、EOF、错误和
partial progress。它是最直接的工程收益：协议层不必等真实 backend。

### Stage 3：stream concepts 与 type erasure

目标是让 protocol code 写到 `read_some` / `write_some` 概念和 type-erased stream boundary
上。先做 borrowed/ref wrapper；owning erased stream 和 ABI-stable layout 只有在测试与文档证明
足够稳定后再考虑。

### Stage 4：coroutine execution substrate

实验 `io_env`、executor adaptation、stop token propagation 和可选 frame allocator policy。
默认不改 `forge::task`。如确实需要把 `io_task` 扩展成更通用的 public task family，
必须先写设计审计。当前 `io_env` 明确是 borrowed environment：`executor_ref`、
`std::inplace_stop_token` 和 memory resource pointer 不拥有底层对象，调用方必须保证它们
活过父 `io_task` await 链或 `as_sender(io_task<T>, env)` operation。

2026-08 的后续 ergonomics 增量新增 `<forge/io/timer_await.hpp>`：它只把既有
`timer_context` sender 包成 `io_task<io_result<>>`，不新增 timer runtime。正常到期在 timer
worker thread 上恢复；同步 stopped 路径可在启动线程恢复；需要回到业务 executor 时显式
await `io_env.executor.schedule()`。

### Stage 5：sender interop

在不扰动 `<execution>` backport 的前提下验证：

- sender -> coroutine awaitable；
- coroutine awaitable -> sender；
- compound IO result 不被静默折叠到错误通道并丢失 partial progress；EOF 作为
  `io_result` 的独立状态存在，不伪装成 routine error。

`io_task` 的安全 ownership 形态只保留两条：父 `io_task` `co_await` 子 task，或
`as_sender(io_task<T>, env)` 让 sender operation-state 持有 task。裸 fire-and-forget
start 不作为 public API。`await_sender` 当前 inline/source-thread resume；它不自动 hop 回
`io_env.executor`，但 inline completion 会在 `await_suspend` 返回后继续，避免递归 resume。
需要 hop 时由 coroutine body 显式 await `env.executor.schedule()`。Stage-5 proof bridge
会在 `as_sender(io_task<T>, env)` 的 operation-state 内融合 `io_env.stop_token` 与
receiver/env stop token；任一 stop source 请求都会让 coroutine 内的 `await_sender`
观察到 stopped。

### Stage 6：现有 `forge::io` coroutine façade

只包装已有 backend proof：

- Linux readiness：ready 只表示 fd 看起来 ready，真正 syscall 与 EOF/error 仍由用户或
  convenience operation 观察；
- Windows IOCP：completion packet 是已提交 operation 的完成；
- fd、`HANDLE` 和 buffer 仍是 borrowed；
- cancellation 必须 exactly-once，且 completion 不在 backend lock 下运行。

### Stage 7：IO-aware helper 与 examples

小型 helper 可包括 `read_exactly`、`write_all`、memory-backed protocol parser example。
`when_all` 类 domain-aware combinator 只有在 sibling cancellation 可验证时才实现，否则只保留
设计记录。

Stage 7 当前实现选择保持保守：`read_exactly` / `write_all` 已由 Stage 3 提供，
`read_until` 覆盖小型 line/record 场景；`io_result` 已有 value / EOF / error 三态，
从而能保留 EOF 与 partial progress。P4124 风格 combinator 只实现
`when_all_results(io_task<io_result<...>>, io_task<io_result<...>>, io_env)` 这一条
two-child proof helper。它证明了：一个 child error/EOF/stopped 时 sibling 会收到 stop；
已完成 child result 不丢失；error > EOF > stopped > value 的 aggregate priority 明确；
receiver completion 不在 helper mutex 下运行；race cancellation/completion 有 focused
stress test。variadic combinator、任意 sender combinator、policy-based priority 和
owning result storage 都继续 deferred。示例覆盖纯 memory line protocol，以及 memory
stream -> coroutine parse -> strand state update -> response write 的 runtime composition smoke。

### Stage 8：收敛与 deferred decisions

审计 public names、umbrella header policy、docs、verification floor，并写明 deferred items：

- TCP/DNS/UDP/TLS；
- Boost.Asio/Capy/Corosio adapter；
- Linux `io_uring`；
- true ABI-stable `any_stream`；
- variadic or policy-based IO-aware combinators beyond `when_all_results`;
- frame allocator propagation；
- 将来若 WG21 adopted wording 后是否做 standard backport。

当前 deferred decision：

- TCP/DNS/UDP/TLS 仍不是本 track 的目标；需要独立 networking taskbook、backend owner 和
  security/release policy。
- Boost.Asio、Capy、Corosio 或其它 adapter 暂不引入，避免把 Forge 的小型 substrate 变成
  adapter matrix。
- Linux `io_uring` 只有在需要 submission/completion queue 语义时才单独立项；它不是
  `epoll` readiness backend 的替代写法。该重估条件已于 2026-08 触发（byte-stream fabric
  方向确认），启动仍需独立 taskbook，决策记录见 `forge-io-backend-spi.md`。
- `any_read_stream` / `any_write_stream` 保持 borrowed wrapper；true ABI-stable owning
  `any_stream` 需要对象布局、allocation 和 lifetime 设计。
- `io_env::memory` 当前只传播 pointer；coroutine frame allocator propagation 没有实现，
  等能用测试证明 frame allocation timing 后再开。
- 若 WG21 后续 adopted wording，是否做 `<io>` 或 standard-shaped backport 需要重新审计；
  当前不会添加 `backport/io`、`<io>`、`<networking>` 或 `std::io`。

## 验收规则

每个 stage 至少满足：

- 更新 proposal baseline；
- 聚焦测试或文档证据覆盖本 stage；
- 没有 `backport/io`、`<io>`、`<networking>`、`std::io` 或新 `namespace std` 名字；
- 没有引入完整 networking surface；
- 完成的 round 及时 commit，并在 remote/凭据可用时 push。

Stages 3、6、8 结束时应跑本地 baseline：

```sh
cmake -S . -B build/local -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON
cmake --build build/local
ctest --test-dir build/local --output-on-failure
```

如果触碰 runtime lifetime、operation-state ownership、stop callback 或 cancellation path，还应按
`docs/roadmap/forge-stability-baseline.md` 选择 sanitizer/focused lane。

## 明确不做

- 不实现完整 network library。
- 不引入 socket、DNS、UDP、TLS 或 certificate API。
- 不添加 Boost.Asio/Capy/Corosio/OpenSSL/WolfSSL/liburing 依赖。
- 不把 Linux readiness 与 Windows IOCP 包装成同一个虚假的 portable model。
- 不为了这个 extension track 改变标准 backport 的 native handoff 或 `std::execution`
  conformance 行为。

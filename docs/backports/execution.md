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

### Pipe 与 sender adaptor closure

公开 `std::execution::sender_adaptor_closure<D>` 按
[`[execution.syn]`](https://eel.is/c++draft/execution.syn) 与
[`[exec.adapt.obj]`](https://eel.is/c++draft/exec.adapt.obj) 提供 CRTP opt-in。
`D` 必须是无 cv/ref 修饰的 class type，实例化 base 时可以尚未完整定义。
在参与 `operator|` 时，`D` 必须完整且公开、无歧义地派生自
`sender_adaptor_closure<D>`；不得同时具有其他 `sender_adaptor_closure<U>`
base，也不得满足 `sender`。仅提供 `operator()` 不构成 opt-in。
按 `[exec.adapt.obj]`，这类操作数的 `operator|` 若选择程序自行定义的重载，行为未定义。

```cpp
struct twice : std::execution::sender_adaptor_closure<twice> {
    template<std::execution::sender S>
    auto operator()(S&& source) const {
        return std::execution::then(std::forward<S>(source),
            [](int value) noexcept { return value * 2; });
    }
};

const twice multiply{};
auto pipeline = multiply | std::execution::then([](int value) { return value + 1; });
auto work = std::execution::just(20) | pipeline;
// sync_wait(std::move(work)) yields 41.
```

`sender | closure` 与 `closure(sender)` 保留相同的参数转发、返回引用类别及条件
`noexcept`，调用可常量求值时也保留 `constexpr`。`c1 | c2` 按原操作数的类别分别
直接初始化两个按值保存的 closure，调用顺序为 `c2(c1(sender))`；创建组合时只检查
保存对象所需的初始化，实际调用时再检查调用表达式。组合对象的 `&`、`const&`、
`&&`、`const&&` 调用分别以相同类别转发保存的对象，遵循
[`[func.require]`](https://eel.is/c++draft/func.require) 的 perfect forwarding call wrapper
规则。组合对象不提供 volatile 调用；自定义 closure 自身若支持 volatile 调用，
仍可按其调用约束直接 pipe。

所有库内 pipeable closures 均使用该公开机制，包括 `on(scheduler, closure)`、
`associate(token)` 和各 bulk closure。`into_variant`、`stopped_as_optional` 和
`affine` 可直接作为 pipe 右操作数。Native `<execution>` 路径使用原生公开类型。

采用公开识别规则后，库 closure 的派生类及 `std::reference_wrapper` 不再仅凭到库
closure 的隐式转换参与 pipe。按 [`[exec.adapt.obj]/2`](https://eel.is/c++draft/exec.adapt.obj#2)，
识别依据是操作数自身去除 cv/ref 后的类型，而非其可转换到的类型；因此
`sender | std::ref(closure)` 和 `sender | std::cref(closure)` 不再有效。
可直接使用 `sender | closure`，或通过 `wrapper.get()` 显式取出被引用的 closure；
`std::ref(closure)(sender)` 与 `std::cref(closure)(sender)` 仍按直接调用的约束工作。
自定义 closure 应直接派生自自身的 `sender_adaptor_closure<D>` specialization，
并满足上述唯一 specialization 要求。

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
- Forge 扩展：`forge::transfer_just` 及串行 `forge::bulk*`（后者需要原生 execution policy 对象）
- 组合器：`into_variant`、`when_all`（至少一个 child sender；每个 child 至多一个 value completion signature、外层取消传播）、
  `when_all_with_variant`、`associate`、`spawn`、`spawn_future`
- 消费者：`sync_wait`（Mandate 要求在其 run_loop 环境中恰好有一个 value completion
  signature。结果类型按该环境推导，该 env 暴露
  `get_scheduler` / `get_start_scheduler` / `get_delegation_scheduler`）、
  `sync_wait_with_variant`（适配一个或多个 value completion signatures，均通过 `std::this_thread`）
- Stopped 工具：`stopped_as_optional`、`stopped_as_error`
  `stopped_as_optional` 要求恰好一个非 `void` value completion signature；单一签名
  的多个参数形成 `optional<tuple<...>>`。无 value、零参数 value 或多个 value 候选
  不满足该约束。当前实现将 shape 验证延迟到实际环境下的 completion-signature
  查询或 `connect`；创建 adaptor 时不猜测 `empty_env`。这是一项已接受的 creation
  Mandate 延迟偏差，尚未实现 WD `make-sender` 的无环境 / 环境依赖分类。
- 调度器：`inline_scheduler`、`run_loop`（mutex+cv，跨工具链可移植）
- Stop tokens：`inplace_stop_source/token/callback`、`never_stop_token`、stoppable concepts
- Allocator 查询：`std::get_allocator`、`std::get_scheduler_allocator`
- 普通 awaitable 可满足 `sender`：优先 sender marker，保留 `enable_sender` specialization；
  completion signatures 在 domain transform 与 member forms 后从 await-result 合成，
  始终包含 `set_error_t(exception_ptr)` 与 `set_stopped_t()`。无环境查询不使用 promise
  的 `await_transform`；有环境时使用只用于类型计算的 promise。
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
  `FORGE_NATIVE_EXECUTION_POLICY_OBJECTS` 等配置上形成跨 TU 不一致。
- 工作草案 MANDATE-NOTHROW 与 [exec.recv.concepts] 要求完成函数
  `set_value`、`set_error`、`set_stopped` 不抛异常，receiver 可不抛异常地移动。
  这些是标准要求；配置期负面编译探测固定这些规则。
- `inplace_stop_callback` 的 intrusive registration 零分配，constructor 遵循条件
  `noexcept(is_nothrow_constructible_v<CallbackFn, Initializer>)`，只有 callback 初始化
  可以抛出异常。callback 可自毁或注销 pending sibling；另一线程注销执行中的 callback
  会等待 invocation 返回。source 必须活过全部注册和 `request_stop()`，包括 callback
  返回后的 source 访问。协议见
  [`inplace-stop-callback-design.md`](../roadmap/inplace-stop-callback-design.md)。
- `std::unstoppable_token` 保留对静态 `constexpr stop_possible()` 返回 `false` 的判断，
  并在 GCC ≥ 16、AppleClang ≥ 21 和 MSVC ≥ 19.52（不含 clang-cl）上启用实测通过的
  [`[stoptoken.concepts]`](https://eel.is/c++draft/stoptoken.concepts#4) 草案判断。它在未知的
  const token 上测试 `stop_possible()`，也接受不读取对象状态、恒为 `false` 的非静态
  `constexpr` observer。Clang 19 会在定义处报错，因此它和未经验证的编译器只使用静态判断；同样宣称 GNU 或 MSVC 宏的其他前端（NVCC、EDG 系、Intel classic、NVHPC）也按未验证处理。
  不会用默认构造的 token 推断类型是否可停止。标准库提供
  `__cpp_lib_inplace_stop_token >= 202400L` 时，使用其原生 concept。proxy 内部的分类仍只使用静态判断。
- Library-provided sender 的 `connect_t` 提供 rvalue 移动路径和要求 sender 可复制的 lvalue 路径。
  `then`、`upon_*`、bulk、`into_variant`、`write_env` 与 `schedule_from` 的 const-lvalue
  连接直接将 child 作为 const lvalue 连接，不预先复制整个 child；data 按对应类别构造，
  有 allocator 时在 child 自身的 get-state 内重建。non-copyable lvalue sender 需要显式
  `std::move(sndr)`，以保持 native C++26 handoff 时的源码形态一致；const non-copyable lvalue 不可连接。
  `connect` 对自定义 source 也检查原表达式类别是否满足 `sender`。即使自定义成员能借用
  对象，immovable source 和不可复制的左值也不能经 CPO 连接。
- Execution domain 支持仍是 draft 子集：`connect_t` 已按 receiver env 选取 start
  domain，并按 sender env 选取 completion domain，支持 start/completion 两阶段
  recursive `transform_sender`；public `transform_sender(sender, env)`、`connect_t` 与
  completion-signature computation 共用这套顺序。`default_domain` 优先调用 sender tag
  的 tagged transform，`apply_sender` 优先调用显式 domain customization，再回退到
  algorithm tag customization。自定义 domain 在 completion 与 start 两阶段均使用
  `transform_sender(tag, sndr, env)`。Scheduler-derived start domain 由
  `get_completion_domain<set_value_t>` 选择，缺失时使用相应的默认规则。
  `get_domain` 与 `get_completion_domain` 按成员查询及各自的默认规则选择 domain 类型，
  结果由该类型的默认构造产生。FWD-ENV 先遵守 forwarding member 的结果，无此成员时
  `forwarding_query_t` 派生类型直接分类为可转发。所有 FWD-ENV 视图（adaptor 的 child
  receiver environment、signature 输入与 sender attributes）按 query 表达式的 cv/ref
  检查分类结果，并以相同的 cv/ref 把 query 转交给环境，环境本身以 const 左值查询；读取恒定对象状态的
  constexpr 分类成员在带相同 cv/ref 的值初始化实例上求值。
  详细边界见[环境查询分类](../roadmap/execution-conformance-ledger.md#实际-query-分类)。
  `get_completion_signatures(sender, env)` 会先按同一 transform 模型得到最终 sender 类型，
  再读取 completion signatures；非 default-domain 路径允许原 sender 没有 raw
  completion signatures，只要 transformed sender 提供可用 signatures。
- `bulk` / `bulk_unchunked` / `bulk_chunked` 只在原生 `<execution>` 提供 policy 对象时
  声明 current-WD 的 policy 重载。仅有头文件或 policy trait 不足以开启重载。
  Backport wrapper 按翻译单元读取 libc++ 的 PSTL 配置或 libstdc++ / MSVC STL 的
  `__cpp_lib_execution`，并单独检测 C++20 的 `unseq`。Forge 不定义 policy 或标准 feature-test macro。
  普通 libc++ 未提供这些对象时，传入 policy 的 bulk 调用不可用。实验模式需要在编译和链接
  时使用 `-fexperimental-library`，共享 policy 相关 inline 代码的翻译单元必须使用一致配置。
  libc++ 16 及更早一律视为没有 policy 对象（下限）。libc++ 17、18 与 20 及以上的判定取自
  各版本头文件的条件，尚未经验证 lane 运行（derived, not exercised）；libc++ 19、Zig 自带的
  libc++ 和 Apple 的 libc++ 经过运行。
  当前实现为串行 subset。
  `bulk` 与 `bulk_unchunked` 在完成线程逐 index 调用，`bulk_chunked` 对正数 shape 调用一个 `[0, shape)` chunk，零或负数 shape 不调用 callback；
  policy 不引入并行执行。
- Policy 类型是否可复制未规定。libc++ 实验模式中的 policy 对象不可复制，因而
  `bulk(sndr, std::execution::par, 4, f)` 可以使用，但
  `sndr | bulk(std::execution::par, 4, f)` 无法按 [exec.adapt.obj](https://eel.is/c++draft/exec.adapt.obj)
  decay-copy 绑定的 policy。`bulk_chunked` 和 `bulk_unchunked` 同理。需要可移植的
  policy 调用时使用直接形式。
- `on` 当前是 current-WD 的实用 subset：`on(scheduler, sender)` 通过 receiver env 中的
  `get_start_scheduler` 返回原调度器；`on(sender, scheduler, closure)` 在 signatures 与
  connect 中均以 `get_completion_scheduler<set_value_t>(get_env(child), get_env(rcvr))`
  选择原调度器，传入 receiver environment 本身，再通过
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
  `affine` 要求 receiver 的 start scheduler 在该环境满足 infallible-scheduler；即使 child
  提供 `.affine()` 也执行此检查。有效时优先调用按 child 值类别选择的 `.affine()`，否则
  才插入 hop；unstoppable scheduler wrapper 原样转发 scheduler 的所有 query。
  [`continues_on`](https://eel.is/c++draft/exec.continues.on) 的 attributes 仅为实际存在的
  completion tag 发布 destination scheduler/domain，使用所传 receiver environment 的签名；
  unary 查询使用空环境。若目标 schedule sender 不声明 error，则源 error 和缓存构造 error
  也在目标 agent 交付，可发布 error scheduler/domain；目标可调度失败时不作该声明。
  completion-domain 成员优先查询 destination schedule sender attributes，未命中时
  查询 destination scheduler。
  所有 completion-scheduler / completion-domain tags 均从 child 转发入口排除；
  destination 查询不可用时不会回退到 child domain，包括 value、stopped、error 和
  默认 tag。其余 child attributes 只经 `forwarding_query` 过滤后 variadic 转发，
  保留引用答案与条件 `noexcept`。目标 domain 查询的参与条件、异常规格、分派与公开
  CPO 的 completion-scheduler 回退，以及两条内部 receiver 路径与 signatures 推导均使用
  [`FWD-ENV`](https://eel.is/c++draft/exec.snd.expos) 的过滤视图。视图是指针，不持有环境：
  receiver 的按值环境在 adaptor 的 operation state 中物化一次，视图及其副本在该 operation
  存活期间有效，指向环境内部的查询结果同样有效；借用环境保留引用。
  所有转发视图都以 const 左值查询环境，与此前的行为一致；按 receiver environment 自身
  值类别转发原始成员（`[exec.let]/8.2`、`[exec.snd.expos]/4`）留给后续批次。
  过滤视图的具体类型无需等于原环境类型，forwarding query 的引用返回类型与对象身份保持不变。
- `associate` 在 `connect` 阶段构造 child operation，失败由 `connect` 抛出。
  `starts_on` 则在目标 scheduler hop 成功后才移动、连接并启动 child；child 的移动或
  late-connect 失败在目标 agent 上报告 `set_error(std::exception_ptr)`。scheduler 自身的
  连接失败仍可从 outer `connect` 抛出。只有 lowering 中的参数保存、函数调用或 child
  连接可能抛出时才声明 `exception_ptr`；移动函数的 `noexcept` 按整个 starts_on sender
  （包括 scheduler）计算，遵循 `[exec.starts.on]/4`。
  `starts_on` 采用 `let_value(continues_on(just(), sch), ...)` 的 child environment。
  从 predecessor attributes 和 receiver 的 FWD-ENV 选出的 completion scheduler 提供
  值类型的 `get_start_scheduler` 和直接 `get_domain`。其余 query（含 `get_scheduler`）
  遵循下面的 `let_*` 规则。
- [`let_value`、`let_error`、`let_stopped`](https://eel.is/c++draft/exec.let) 的 child
  environment 按对应 completion tag 依次选择：从 predecessor attributes 与 receiver 的
  `FWD-ENV` 查询 completion scheduler，并构造 `SCHED-ENV`；若该查询不可用，则以相同的
  attributes 和 `FWD-ENV` 查询 completion domain；两者均不可用时使用空环境。
  带环境参数的 scheduler 查询优先，只有 unary 查询的 attributes 仍通过 CPO 的
  `TRY-QUERY` 回退提供 scheduler。选中的 scheduler 按值保存，并作为 child 的
  `get_start_scheduler` 纯右值答案（无条件 `noexcept`，scheduler 复制按 `[exec.sched]/3`
  不以异常退出）；直接 `get_domain` 在保存的 scheduler 的 const 左值上查询，指向
  scheduler 内部的答案在该环境存活期间有效；因此 inline schedule predecessor 使用 receiver 环境中的
  scheduler，run-loop predecessor 则保留其所属 loop 的 scheduler。receiver env 缺少
  `get_scheduler` 时，inline predecessor 选择 inline scheduler。Child 查询优先访问选中的
  环境。按 `[exec.let]/9.4` 检查 successor 是否满足 `sender_in` 时，通过 `FWD-ENV`
  按 query 表达式的 cv/ref 分类，operand 是 `declval<Env>()`。`let` 和 `starts_on` 声明的 completion signatures
  则取自 `/8.2` 的连接环境。该环境对 query 类型应用 `forwarding-query` concept
  （`forwarding_query(Q{})` 良构但非常量时为 ill-formed），随后以 const 左值查询
  `get_env(rcvr)` 的结果。
  两个环境的类型只取决于所选环境和去除引用与 const 的 receiver environment 类型，与 receiver
  类型及环境是否被借用无关，对非 volatile 的环境，successor 在签名推导和实际连接中看到
  同一个环境类型（静态 signature 成员收到去除 volatile 的类型）。
  `get_scheduler` 由 receiver environment 提供，选中的 completion scheduler 通过
  `get_start_scheduler` 提供。
- `on(scheduler, sender)` 转发 child 的非 completion forwarding attributes，并屏蔽 child
  completion scheduler/domain，避免泄漏错误的返回资源。转发视图的类型只取决于去除引用与 const 的环境类型，
  对非 volatile 的环境，签名推导与连接因此看到同一个环境视图类型。
  `on(sndr, sch, closure)` 和绑定形式的 closure 实参须满足 pipeable closure 约束。
- `schedule_from` 是 current-WD 的单参数 departure marker sender，child attributes 与 child
  receiver environment 均经 FWD-ENV；借用查询的引用身份不变。`continues_on`
  通过它让 source completion domain 定制离开当前 execution resource 的方式。
  `transform_env` 已不在当前 working draft surface，因此本 backport 不暴露该旧拼写。
- `std::execution::task`、`task_scheduler` 和 `with_error` 按分阶段试点引入，尚未发布
  `__cpp_lib_task`。`task_scheduler` 的普通调度（E1）和 `task` 的 frame 所有权与终止
  交付（F）、frame 分配（G1）以及环境、start scheduler 选择和 stop token（G2）已实现；
  await 路径与类表面的资格验证属于后续批次，逐行状态见
  [conformance ledger](../roadmap/execution-conformance-ledger.md#f-task-frame-and-terminal-notes)。
  `forge::task` 与 `forge::io::io_task` 是不同契约的 Forge extensions，与这组标准类型
  相互独立。标准 `task` 有四条设计约束，它们来自草案本身（[P4007R3](https://wg21.link/P4007R3)
  §3），不是 Forge 偏差：
  - coroutine 调用在 receiver 连接之前选定 frame allocator；
  - receiver 环境的 allocator 可以传到子任务的环境，但每次嵌套的 coroutine 调用各自选定
    frame allocator；
  - 成功用 `co_return`，带类型的错误用 `co_yield with_error{e}`，该 yield 之后的代码不执行；
  - `start` 与完成函数返回 `void`，一般的 sender 组合没有栈深度上界。
  `co_yield with_error{e}` 的 payload 在 frame 销毁前多移动一次（Forge 记录的选择），因此
  payload 必须可移动构造；到目标错误类型的转换在 frame 销毁之后进行，转换抛出时走
  `exception_ptr` 通道，`error_types` 未列出该通道则调用 `terminate`。
  start scheduler 的选中表达式按草案原样求值：prvalue 查询结果直接初始化构造函数参数，没有
  额外的拷贝或移动。精确匹配、`S(s, a)`、`S(s)`、`S(a)` 都把 `get_env(rcvr)` 保留到
  operation 析构，因此引用到环境内部的查询结果在整个异步生命周期内有效；SCHED 构造在已连接的
  operation 内而不是 coroutine frame 内，先于环境析构、晚于 frame 销毁后的错误转换。
  引用类型的 `start_scheduler_type` 被 `static_assert` 拒绝（比草案更严格的偏差）：按字面
  理解，SCHED 会是由选中表达式初始化的引用，prvalue 结果时即悬垂，而何时可以安全地保留引用
  没有通用判据；因此 SCHED 始终是 `start_scheduler_type` 类型的对象，该类型必须是对象类型。
  保留对 prvalue 构造实参的引用的 scheduler 不在契约之内。
  stop token 按草案字面比较 `get_stop_token(get_env(rcvr))` 的表达式类型（含引用）：类型完全
  相同时直接返回 receiver 的 token，不创建任何对象。`prop` 以引用返回 token，因此即使 token
  类型相同也算不匹配。不匹配时，上游类型满足 `unstoppable_token`（receiver 没有 stop token
  查询即属此类）就静态判定为不可能 stop；否则在第一次查询时读取一次 receiver 的 token 并调用
  一次 `stop_possible()`。结果为 true 时惰性创建一个 operation 持有的 source、签发一个 token
  并注册一个转发 `request_stop` 的上游回调；之后每次查询返回同一个已提交答案的拷贝，用户
  自定义 token 的拷贝由 L0 mutex 串行化。转发回调与 operation 共享的 source 单元在
  `connect` 时用 `std::allocator` 分配，不论 token 是否被查询，`connect` 可能因此抛出
  `bad_alloc`；source 本身在 `noexcept` 的查询内构造，source 构造函数、`get_token()` 或回调
  构造函数在那里抛出会调用 `terminate`。转换要求 `stop_source_type()` 得到一个全新的可请求
  状态：查询检查新 source 的两个 observer，不满足时调用 `terminate`；状态与其他 source 相互
  独立是对自定义 source 的语义前提，observer 无法验证。尚未被上游选中的转发回调在任务完成
  时被移除，不再调用。
  上游不可能 stop 时依次取：receiver 自己的 token（类型恰为 task 的 token 类型时）、
  `inplace_stop_token()`（task 的 token 类型为该类型时），然后依次检查候选并以 token 的两个
  observer 实际验证：token 类型的默认值、`Src(nostopstate)`、`Src()`。第一个既不 possible
  也未 requested 的候选即为答案；都没有时，使用 operation 自己构造的第一个 possible 且未
  requested 的 source（已记录的偏差 ST-07.2）；没有任何可用候选时调用 `terminate`。从不伪造
  请求。未被选中的候选先释放 token 再释放 source。
  `stop_token_type` 必须是对象类型：右值 `get_token()` 返回引用的 source 被 `static_assert`
  拒绝（比草案更严格的偏差）；以引用返回上游 token（如 `prop`）不受影响。
  适用范围的五项收窄（负责人批准，标准 source/token 组合不受影响）：
  - source 的左值与右值 `get_token()` 必须返回同一 token 类型，否则不匹配路径以
    `static_assert` 拒绝（ST-07.3）；
  - 需要应用自定义实参才能构造的 disengaged 表示不可达，不计入“可表示”（ST-07.4）；
  - 类型派生自或可转换为 task token 类型的上游 token 不做转换（ST-07.4）；
  - 作为 ST-07.2 答案的 engaged source 必须在其整个可观察生命周期内独立于其他 owner
    （ST-07.5）；
  - token 类型的默认值仅在其默认构造函数为 `noexcept` 时才是候选（ST-07.6）。
- `connect` 的 awaitable fallback 保存 sender 与 receiver 到全局分配的 coroutine frame，
  operation 的 noexcept move 转移 handle 并清空源对象，析构仅销毁非空 frame，
  `start` 恢复 frame。此连接路径可能抛出分配或参数构造异常，
  不使用 receiver allocator；各完成函数都在 frame 已挂起后的 `await_suspend` 内调用，
  receiver 可以在 value、error 或 stopped completion 内销毁 operation。Awaitable 即使
  `await_resume` 不抛异常也声明 `exception_ptr`，因此标准 `spawn` 拒绝它；可用
  `spawn_future` 或 Forge scope 消费。
  内部 completion awaiter 是函数局部类；receiver / value / error 类型的 namespace
  不参与该 await expression 的 ADL，用户 `operator co_await` 不会接管完成路径。
- `as_awaitable` 的 direct member 结果须满足该 promise 的 awaitable Mandate；transformed /
  adapted member 的 `noexcept` 来自实际 transform、adaptor 和 member 表达式。
  Sender bridge 在 awaitable receiver 的实际环境检查 completion signatures：没有
  `unhandled_stopped` 的 promise 仅能等待不会发送 stopped 的 prepared sender，
  不再以私有异常恢复 coroutine。`with_awaitable_senders` 限制为无 cv/ref 的 class type，
  continuation 只要存在 `unhandled_stopped()` 就安装 forwarding handler，错误返回类型
  不会静默退回 terminate handler。
- GET-AWAITER 使用受约束的 member / ADL call probes；member 与 ADL 的候选都可调用时，
  模拟一律按歧义处理，即使语言的 overload resolution 能选出一个最佳候选。
  `as_awaitable` 的普通 awaiter 分支使用同一探测且不执行 promise 的 `await_transform`；
  若该类型也满足 sender bridge 的条件，就会进入 sender bridge。
  final promise 的不可调用 overloaded / template `await_transform` 也无法完整反射。
  deleted、inaccessible 或 ambiguous 的 `operator co_await` call probe 失败时视作缺失：
  若该类型另有有效 awaiter 方法，它仍可满足 `sender`，但实际 await 或 connect 时
  program 为 ill-formed。无法在形成 await expression 前将这些情况与仅值类别不匹配
  区分；保留这个 emulation 边界，避免拒绝 WD-valid awaiters。
- Sender bridge 的 receiver 暴露 promise environment 本身；WD 只规定 forwarding queries
  的行为，没有禁止其他查询，这项 latitude 保留。桥接继续在 `await_suspend` 中 connect，
  用同线程 deferral 与 symmetric transfer 保持 stack safety。
  Transformed/adapted member 与 sender bridge 都按原 sender 表达式类别检查 value
  signatures。即使参数经 decay 后得到同一类型，两种 value signatures 仍是两个 alternatives。
  sender bridge 根据原表达式选择，并按变换和适配后的 sender 的实际值类别
  计算结果类型：无 value 或唯一零参数 value 返回 `void`，唯一单参数 value 返回
  decayed 类型，唯一多参数 value 返回 decayed `tuple<...>`。
  LWG 4547 后，桥接借用符合条件的 prepared 左值，不复制或移动 sender，并以左值连接。
  桥接保留准备链的临时对象，模拟 WD 完整表达式的生命周期。借用外部对象时，调用方须保证
  它活到 awaitable 及其 operation 销毁为止，按值返回的 prepared sender 借用外部对象时也一样。
  若 prepared sender 按其表达式类别声明多种 value signatures，则回退到原表达式；
  若原表达式符合桥接条件而 prepared 左值不能连接（例如 move-only sender 的左值），
  则拒绝桥接，不回退到原表达式。
- Forge `async_scope::spawn`、`resource_context::spawn`、`start_detached`、`any_sender_of`、
  `forge::split`、`sync_wait` 与 `io::await_sender` 接受满足各自环境和 value-shape 契约的 awaitable。
  Scope / IO 入口按实际 stop-token 环境检查；`any_sender_of` 仍要求相同 wait 结果，
  copyable source 仍按 lvalue 借用，只有 rvalue `operator co_await` 的 copyable 类型因此被约束拒绝。
  `erased_sender` 也要求 source signatures 是声明 closed set 的子集；`wait_result` /
  `wait_result_of` 保留其实际带 inplace stop token 的 wait 环境约束。
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
  `std::this_thread::sync_wait_with_variant`。
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
  [spawn_future 协议模型](../../formal/spawn_future/README.md) 在有限边界内检验完成、取消、共享所有权及 association 释放顺序。
- Scope-token surface 已改为 current-WD-shaped：
  `simple_counting_scope::token::wrap(sender)` 是 identity forwarding，
  `counting_scope::token::wrap(sender)` 会给 child env 暴露一个 fused stop token：
  scope stop 和下游 receiver/env stop 任一请求都会让 child 观察到 stop。scope
  association 由 top-level `associate(sender, token)`、`spawn(sender, token[, env])`
  和 `spawn_future(sender, token[, env])` 持有。`spawn` 只接受 completion signatures
  为 `set_value()` / `set_stopped()` 的 sender；会产生 value 或 error completion 的
  sender 需要先转换成无 error、无 value 的形态。fused token 只保存两个输入 token 的副本，
  `read_env(get_stop_token)` 返回的 token 不依赖 wrapper operation 的 lifetime；输入 source
  仍须存活。每个 child callback 直接向两个输入注册，至多调用一次；注册和注销由 child
  的 callback lifetime 决定，注册失败由 child 自己处理。
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
  [counting_scope 协议模型](../../formal/counting_scope/README.md) 在有限边界内检验 join、scope stop 和 fused callback 的生命周期及同步。
- `forge::task` 的完成与取消契约见[工具参考](../forge-utilities.md#两种协程任务)。

Sender、receiver 和 scheduler 通过成员定制 `connect`、`get_env`、`set_value` 和
`schedule` 等 CPO。Environment 和 scheduler 查询在 const 对象上以 const 左值 query
调用成员 `query`。`prop.query(q, args...)` 以 `const Value&` 返回属性且为 `noexcept`。
`env.query(q, args...)` 用 `e.query(QueryTag(), args...)` 检查参与条件，并按声明顺序
选择首个能回答具名左值 `q` 的子环境成员。借用子环境保持 referent 的 cv/ref 限定，
结果保留引用类型，条件 `noexcept` 取自选中的调用。[工作草案](https://eel.is/c++draft/exec.env)
`sync_wait` env 与 `let` / `when_all` helper env 提供 const member
`query`；`when_all` 只转发一参数 forwarding query。
[sync_wait 协议模型](../../formal/sync_wait/README.md) 检查 result 经 finish/run 的 HB 发布与 op、state、loop 的逆序析构。
[`write_env`](https://eel.is/c++draft/exec.write.env) 的 child env 提供一参数 const member
`query`：先以 const 左值查询 overlay，未命中时经 FWD-ENV 视图转发 receiver env 的查询：
按实参的 cv/ref 对 query 对象分类（不要求默认实例），receiver environment 以 const 左值查询。
因此 base 的本地查询不可见，overlay 提供的本地查询仍可见；结果引用与条件 `noexcept` 保留。
[`inline_scheduler`](https://eel.is/c++draft/exec.inline.scheduler) 与 schedule sender attrs
提供相同成员查询：带一个环境参数的 value completion scheduler / domain 分别按
`get_scheduler(env)` / `get_domain(env)` 的规则选择。Inline schedule attributes 在传入环境
没有 scheduler 时，带环境的 value 查询返回 `inline_scheduler{}`；scheduler 自身通过
completion-scheduler CPO 的 scheduler 默认分支取得同一答案。两者均无 unary value 答案，
也不为 inline schedule sender 添加 error / stopped 答案。`let` 因此采用 inline start scheduler。
[`run_loop`](https://eel.is/c++draft/exec.run.loop.types) scheduler 与 schedule attrs 均通过
成员提供 value / stopped completion scheduler，并返回同一 loop 的 scheduler；不提供
unary completion-domain 成员。带环境参数时，attrs 的 completion-domain 成员转发到
scheduler CPO。Forge 线程池、strand、any scheduler 和 task 的 fallback scheduler 采用
相同的成员查询模式，仅对 schedule sender 实际声明的 completion tags 保证 parity。
Forge pool/strand 的 schedule signatures 在所有环境都包含 value 和 stopped，关闭、拒绝或资源
失败仍以 stopped 完成。它们仅在提供 stoppable token 的环境满足 affine 的 infallible-scheduler
要求；在无 stop token 或 never_stop_token 环境，affine 的 sender_in 为 false。
[run_loop 协议模型](../../formal/run_loop/README.md) 检查 queue/wait 与最后 unlock 后的 teardown；调用者须在最终空队列观察前关闭提交，finish 本身不拒绝后续 push。
Completion-scheduler CPO 限制 completion tag 为 value / error / stopped，先执行
`TRY-QUERY`，再递归解析 scheduler 的 value completion scheduler，直到查询不可用或
同型 scheduler 比较相等。环境参数只求值一次。无查询且输入本身满足 scheduler 时，
非空环境参数使 CPO 返回输入 scheduler 的副本，见 `[exec.get.compl.sched]/5.2`。
Completion-domain 的 scheduler 回退经过这条完整 CPO；domain 查询仍只选择类型，
不求值查询返回对象，结果通过不抛异常的默认构造产生。
`get_scheduler(env)` 也经 completion-scheduler CPO 解析环境直接提供的 scheduler，
并传入 `HIDE-SCHED(env)`。`get_domain` 的 scheduler 派生分支采用同一隐藏视图，
屏蔽 `get_scheduler` / `get_domain`，使 inline 及转发其查询的 scheduler 终止递归。
需要在此分支定制 completion domain 时，应使用接受通用环境的 overload。
精确原环境类型的 overload 仍可用于直接查询，不再控制隐藏视图上的派生选择。
`counting_scope` / `spawn_future` 的 stop-when environment 现已提供成员查询，
只转发 `FWD-ENV` queries，并保持融合 stop token 的覆盖优先级。
`continues_on` attributes 与两条内部 receiver environment 均支持成员 `query`；
completion tags 只由 destination 提供，不参与 child attribute 转发。
探针检查目标查询缺失时的
[`get_completion_domain`](https://eel.is/c++draft/exec.get.compl.domain) 回退，避免 child domain 泄漏。
`prop` / `env` 均不可赋值，仍支持各自所需的拷贝和移动构造；借用子环境的赋值不会再写入 referent。
`env` 的参数必须满足 queryable；`prop` 的 callable Mandate 暂缓：在 schedule attributes
自身用 prop 描述 scheduler 时，类体检查会递归触发同一 scheduler concept。
实际 query CPO 仍检查结果 Mandates。`prop(tag, value)` 与 `prop{tag, value}` 均保留
两参数 deduction guide，包括函数模板内的非依赖 CTAD、stop-token 属性、move-only value
与 reference_wrapper 解包。`prop` 仅有 tag holder 和 value 两个 aggregate 元素；
holder 删除拷贝与移动赋值，即使 tag 自身支持 const 赋值也保持不可赋值，空 tag 不增加大小。
新代码建议优先使用成员式定制。原生 P2300 senders 被检测到时让位，详见 [native handoff](../native-handoff.md)。


### 分配与可移植性边界

`let_value` / `let_error` / `let_stopped` 以去重的 `variant` 保存 decayed 参数 tuples，
另一个去重 `variant` 保存 predecessor 与所有可能的 successor operation state 的 typed holders。
参数 variant 初始为 `monostate`，operation variant 初始为 predecessor。holder 以普通数据成员保存 operation state，
由 factory prvalue 直接初始化，避免空且不可移动的类型作为潜在重叠子对象时失去 guaranteed copy elision。
参数与 successor 在对应 completion 到达后原位构造；
先 decay-copy 参数，再销毁 predecessor，然后调用 successor 函数并连接新 operation。
`[exec.async.ops]/7–8` 对所有 sender 都允许 receiver completion 内销毁 operation state；
完成函数调用后的返回路径不能再访问该 operation 或已随之销毁的存储。successor 只可依赖保存后的参数，
不可再依赖 predecessor operation 的存储。
参数 decay-copy、函数调用和 successor `connect` 的潜在异常通过
`set_error(std::exception_ptr)` 报告；签名与运行时使用同一 nothrow 公式，仅在可能抛出时保留该通道。

默认 get-state 的算法在 receiver environment 提供 `get_allocator` 时以 uses-allocator
构造重新创建 sender data：`just` 的各值、`just_error` 的 error、`read_env` 的 query、
`then` / `upon_*` 的 callable、serial bulk 的 callable 及 `write_env` 的 overlay。
无 allocator 时按 sender 的值类别直接复制或移动 data。自有 get-state 的 `let`、`continues_on`、
`into_variant`、`when_all` 和 scope/future 算法不直接重建其 data；它们所包裹的默认算法
仍可观察到传入的 allocator。重建发生在草案 /37 的 noexcept get-state 内，失败会 terminate。
按 /28，allocator 路径的 get-state 返回同类型 prvalue，因此不额外计入 data 的复制或移动异常；
无 allocator 时返回引用，计入对应 data 构造异常。两条路径均计入 receiver 移动及 child connect；有效 receiver 按 `[exec.recv.concepts]` 总是 nothrow-movable，
故 receiver-move 项恒为 true。allocator 的存在本身不会给 let 或 starts_on 增加 exception_ptr。分配与 noexcept 的措辞问题列入台账。

`just*`、`read_env`、`then`、`upon_*`、`bulk_chunked`、`bulk_unchunked` 和 `into_variant`
的 `connect` 根据 receiver/data 构造及 child 连接条件声明 `noexcept`；发生 tag lowering 的
`bulk`、`let_*`、`starts_on`、`on` 等保持 potentially throwing。
`then`、`upon_*`、bulk、`into_variant` 和 `stopped_as_optional` 的 child 与签名均使用
FWD-ENV；`let` predecessor 与 `stopped_as_error` child 按 `[exec.let]/10` 使用未过滤环境。
无效 callable、无法保存的结果或缺少查询在 signature / connect 的约束中拒绝，
使 `sender_in` 可得到 false，而非实例化函数体时产生硬错误。锁定的组合算法尚未全面采用该形状。

`when_all` 在静态类型的 `tuple` 中保存各 child 的 typed holder，
child operation state 在 holder 的构造函数中直接初始化为普通数据成员，
values 按 `[exec.when.all]/13` 保存在由各 child 的 `optional<decayed-tuple<...>>`
组成的 tuple 中，无法形成该类型时使用 `tuple<>`。
errors、stop source 和具体类型的 `optional` stop callback 均在 operation state 内存储。
stop callback 在启动任何 child 前注册，并在向下游发送 completion 前注销；
child `connect` 抛出时，已构造的 children 随 tuple 构造展开而析构。
[when_all 协议模型](../../formal/when_all/README.md) 检查结果仲裁、completion credit 与生命周期；外层 stop callback 成功取得非零 credit 时向内部 source 转发，最后 child 先归零时允许不再转发。
上述 `let_*` / `when_all` 的 operation-state storage 本身不再分配，
不依赖固定容量或 `std::max_align_t` 对齐上限，也不会因 operation state 或 callback
对象的大小、对齐要求而回退到全局分配；所需空间计入具体 operation state 的大小与对齐要求。
在 senders、schedulers、receivers、environment queries、结果类型及用户函数均不分配的前提下，
这些组合的 `connect`、`start` 和 completion 路径无需全局分配。
`when_all` 仅在 receiver token 的 `stop_possible()` 为 `true` 时注册；
嵌套 `when_all` 会在 parent 的 stop source 上注册，这些 inplace registration 也零分配。
自定义 stop token 的 callback 构造若自行分配，须单独计入。intrusive 协议见
[`inplace_stop_callback` 设计记录](../roadmap/inplace-stop-callback-design.md)。

`starts_on` 与 `continues_on` 的调度签名、连接约束、operation 类型及实际调用统一采用
保存的非 const scheduler 左值。ref-qualified `schedule` 可返回不同 sender；affine 的
infallible 检查仍按草案调度 prvalue，实际 hop 则调度 wrapper 的非 const 左值。措辞冲突见台账。

`continues_on` 将 scheduler operation 和 predecessor operation 作为普通成员直接构造，
结果用 tagged decayed tuples 的 `variant` 原位保存；`schedule_from` 只转发 departure
marker 的 child，不增加独立缓冲。`starts_on` 使用 typed holder variant 在 hop 后以 child
operation 替换 scheduler operation；连接时机与 working draft 的 lowering 一致，保留上述 child
environment 偏差。
`on` 的两种形式将其组合 sender 的 operation 作为普通成员从 factory prvalue
直接初始化。这些路径不再使用大小或对齐不足时分配的缓冲，支持大于固定缓冲容量、
over-aligned 以及空的 immovable operation states；空间与对齐需求计入外层 operation。
在组成对象及其查询、结果构造和自定义 token callback 不自行分配时，组合无需全局分配；
`inplace_stop_callback` 的 registration 使用内联 node。

所有 adaptor 均使用 concrete operation type 的原位存储，已移除 `__op_storage`
小缓冲及其按大小、对齐触发的全局 `new` 回退。`associate` 只在 association 成功时
构造 union 中的 typed holder；holder 内的 operation 是普通成员，不依赖向
potentially-overlapping subobject 的 copy elision。`associate` 在 operation 析构时先销毁 inner operation，
再销毁 receiver，最后释放 association；完成本身不释放 association。
[associate 协议模型](../../formal/associate/README.md) 在有限边界内检验这些生命周期与 sender association 转移。`join()` 的 schedule operation、
`spawn` / `spawn_future` state 的 spawned
operation 均为普通成员，支持 immovable、超过 4 KiB 及 over-aligned operation。

`spawn` 和 `spawn_future` 的 allocator 均按以下顺序选择：显式 environment 的
`get_allocator(env)`、`token.wrap(sndr)` 的 sender attributes 中的 allocator、
`std::allocator<void>`。第二种情况还向 spawned environment 注入所选 allocator；
缺省情况不额外添加 allocator query。`spawn` 用 rebound allocator 分配一个 state，
其 operation 无独立分配。
[spawn 协议模型](../../formal/spawn/README.md) 在有限边界内检验双 reference、stop registration 与 deallocate 后释放 association 的顺序。`spawn_future` 用同一所选 allocator 分别分配生命周期 resource、
state 和 state 的 `shared_ptr` control block；连接 consumer 时用 `allocate_shared`
分配合并的 consumer record/control block。resource 持有 association，所有内部 allocator
副本共同保持其存活；consumer 在完成前解除与外层 operation 的存储所有权。
以上存储均不回退至另一 allocator；connect 或分配抛出时销毁已构造对象并回收内存。
state、consumer、全部 control block 以及 resource 自身均销毁并 deallocate 后，才释放
association；已完成的 consumer operation 可以继续存活，不保留上述分配。

`counting_scope::token::wrap` 使用内部 stop-when：operation 内持有 receiver 和 child
operation，不持有融合 source 或输入 callback。不可停止的 receiver token 直接使用 scope
token；其他情况的融合 token 只保存 scope token 和 receiver token 的副本。
融合 token 的 `stop_requested()` 和 `stop_possible()` 分别返回
两个输入查询结果的逻辑或；child 在 connect 时注册的 callback 也可在 start 前收到通知，
且最多调用一次。注册时输入已请求停止，则在注册线程立即调用该 callback，即使输入的
其他 callback 仍在阻塞停止传播。每个 `callback_type<Fn>` 先构造 `Fn` 和 atomic flag，
再查询逻辑 token 并通过两个输入各自的 `stop_callback_for_t` 注册 trampoline；已停止时
在发布 registration 前于构造线程调用，exchange 保证两个输入并发
停止时也只调用一次。调用 `Fn` 后不再访问 callback storage，允许同线程 self-deregistration。
析构先注销两个输入 registration，再销毁 `Fn`；注销等待另一线程正在执行的 invocation，
输掉 exchange 的 trampoline 直接返回；两个输入并发请求停止时，输掉 exchange 的输入请求
可在赢家线程进入 `Fn` 前返回，`Fn` 的 effects 与该返回之间没有 happens-before。
不能在此等待赢家：赢家的 `Fn` 可能析构 callback，而析构又等待输家的 invocation 返回，
从而形成等待环。请求方若需要完成保证，须与 child completion 同步。
child 负责其 callback 的注册、注销和注册失败处理；stop-when 按原 channel 和原参数引用
直接转发，不保存参数、不增加分配，也不合成 stopped completion 或额外 signature。
receiver 可在同线程 stop completion 中销毁 operation，输入 source 的 owner 仍须覆盖
stop 调用。一个 child callback 对应两个输入 registration；输入为 inplace token 时，
二者均零分配，stop-when 本身也不分配。
scope 本身须活过其全部 stop 调用，包括使 join receiver 同步完成的调用。详见
[`inplace_stop_callback` lifetime 设计](../roadmap/inplace-stop-callback-design.md)。
`spawn_future` 的 producer environment 按
`write_env(stop-when(sndr, ssource.get_token()), env)` 组成；stop-when 边界仅转发
`FWD-ENV` queries，融合 token 覆盖原 stop token。consumer stop callback 仍直接使用
receiver environment 的具体 token 类型。producer 完成后，child operation 仍随 future state
保存到结果消费或 future 放弃；child 若保留 callback，用户 environment token 上的注册也可
保留至该 operation 析构，不能将 producer completion 当作注销时点。输入 source 必须覆盖
这些注册的 lifetime；完成后收到 stop 的 callback 不得访问已失效的 operation。
pending producer 若忽略取消，future 放弃或 consumer 收到 stopped 后仍须等 producer
实际完成，才能销毁其 operation 并释放注册。state 构造失败时，已构造的注册随成员展开注销。
consumer 取消路径与工作草案不完全相同：草案在停止已登记的 consumer 时调用 `destroy()`，
本实现保留 producer keepalive，operation 的销毁和注册释放要等 producer 实际完成；
其余时点遵循 [`[exec.spawn.future]`](https://eel.is/c++draft/exec.spawn.future) 的状态生命周期。
对于自身不分配的 sender、scheduler、receiver、所选 allocator、environment queries、
decayed 结果构造及自定义 stop-token callbacks，这些路径除上述 allocator 管理的存储外
没有额外全局分配；inplace registration 不分配。上述用户代码自行分配的内存另计。使用缺省
allocator 时，`spawn` / `spawn_future` 的上述共享或 detached 存储仍会分配。
只有 `schedule_from` 提供结构化访问；其他库 sender 的 `tag_of_t` 无法成立，自定义 domain 不能按工作草案方式匹配它们。
两种 counting scope 均无公共诊断快照接口。使用 `close()` 拒绝新的关联，使用 `join()` 等待已有关联全部释放。


### 查询、scheduler 与类型工具

默认 `get_env` 与无 attributes 的 sender 使用 `env<>`。
`std::forwarding_query`、`std::get_allocator`、`std::get_scheduler_allocator`、
`std::get_stop_token` 及其 `_t` 类型和 `std::stop_token_of_t` 均从 `std` 使用。
P4453R1 的 `get_scheduler_allocator(env)` 优先选择 const 环境上的
`query(std::get_scheduler_allocator)`，仅在该表达式不良构时回退到
`query(std::get_allocator)`；两者均不良构时不可调用，没有默认分配器。
该查询是 forwarding query，并保留所选成员结果的值类别。
`std::get_allocator`、`std::get_stop_token`、`get_scheduler`、`get_start_scheduler`、
`get_delegation_scheduler`、`get_forward_progress_guarantee` 和 `get_await_completion_adaptor`
的成员表达式直接使用全局 query object，即使从 CPO 副本调用也是如此。
`get_completion_scheduler<T>` 的初次查询使用全局 `<T>` 对象，之后每次递归使用全局
`<set_value_t>` 对象。此规则不改变仅选择类型的 domain 查询，也不替换通用转发收到的 tag。
选中的成员查询遵守 MANDATE-NOTHROW，并检查相应 allocator、
stop token、scheduler、queryable 或 progress 结果要求。类型 Mandates 按结果的
`remove_cvref_t` 检查，以支持 `prop` 的借用答案；草案措辞问题见台账。
两种 allocator 查询均按草案字面检查 `simple-allocator`：`*alloc.allocate(n)` 须为
`value_type&`，`alloc.deallocate(alloc.allocate(n), n)` 须良构，并要求可复制构造、可比较相等。
不再要求 `allocate` 返回裸指针或 `deallocate` 返回 `void`。
查询 CPO、`read_env` 与 `write_env` 支持底层表达式允许的常量求值。
`read_env` 按 `Q()(env)` 的左值环境规则检查参与条件，缺失查询、void
结果、不可默认构造的 query 均使 `sender_in` 不成立。实际 value 类型与条件 error channel
取自保存的 `Q&` 对 `get_env(rcvr)` 的调用，保留 receiver environment 的值类别；
尚不实现 dependent-sender 诊断类型。

`scheduler<Sch>` 按原表达式类别调用 `schedule`，要求 copyable、queryable、sender
结果及 forward progress；`schedule_result_t<Sch>` 暴露相应结果类型。Progress CPO
使用不含 progress 的内部 scheduler 形状打破草案定义循环。Scheduler 需要提供
`query(get_forward_progress_guarantee) const noexcept`，结果类型为
`forward_progress_guarantee`。
`start` 只接受左值且要求 void 结果；completion 函数只接受非 const 右值 receiver。
`inlinable_receiver<R, ChildOp>` 仅提供 concept，不承诺库 operation 实际省略 receiver 存储。

`transform_sender` / `apply_sender` 是受约束的函数模板；`default_domain` 的标准成员
同样限制 sender / queryable。默认 transform 的左值结果保留引用；内部 connect 分派仍支持
自定义 source 的借用连接扩展，不要求为 domain identity transform 复制该 source。
`indeterminate_domain<Ds...>` 默认使用 default domain
的 transform，并检查各候选 domain 的返回类型一致性；`common_type` 按首次出现顺序
合并去重后的候选类型。交换两个 indeterminate operands 保持候选集合与 transform 行为，
不保证 specialization 类型相同；与通用 common_type 对称要求的措辞冲突见台账。
COMMON-DOMAIN 检查完整的 common type 默认构造表达式，不可构造时选择 indeterminate。
算法提供对应的公开 `_t` 类型名，包含 `spawn_t` 与 `associate_t`。
`completion_signatures` 限制为有效完成签名，查询结果必须为该模板的 specialization；
`completion_signatures_of_t` 要求 `sender_in`，value/error 收集保留引用并接受固定参数个数的模板。

## 示例

示例门控与所有文件见[示例索引](../examples.md)。只在注入或完整原生时构建；明确依赖扩展者另列限制。验证流程见[测试说明](../testing.md)，测试组为 `FORGE_TEST_ENABLE_EXECUTION`。

- [execution_affine_example.cpp](../../example/execution_affine_example.cpp)：execution affine 的接口组合与完成结果。

- [execution_combinators_example.cpp](../../example/execution_combinators_example.cpp)：用标准接口 when_all 合并结果，并在计数 scope 中 spawn。
- [forge_execution_combinators_example.cpp](../../example/forge_execution_combinators_example.cpp)：用 Forge 扩展 split 复用结果。有原生 execution policy 对象时也演示串行 bulk。

- [execution_coro_example.cpp](../../example/execution_coro_example.cpp)：execution coro 的接口组合与完成结果。

- [execution_example.cpp](../../example/execution_example.cpp)：execution 的接口组合与完成结果。

- [execution_let_example.cpp](../../example/execution_let_example.cpp)：execution let 的接口组合与完成结果。

- [execution_on_example.cpp](../../example/execution_on_example.cpp)：execution on 的接口组合与完成结果。

- [execution_schedulers_example.cpp](../../example/execution_schedulers_example.cpp)：execution schedulers 的接口组合与完成结果。

- [execution_scope_example.cpp](../../example/execution_scope_example.cpp)：scope spawn 与异步 join。

- [execution_spawn_future_example.cpp](../../example/execution_spawn_future_example.cpp)：execution spawn future 的接口组合与完成结果。

- [execution_stop_token_example.cpp](../../example/execution_stop_token_example.cpp)：execution stop token 的接口组合与完成结果。

- [execution_unstoppable_example.cpp](../../example/execution_unstoppable_example.cpp)：execution unstoppable 的接口组合与完成结果。

标记类型使用 `sender_tag`、`receiver_tag`、`operation_state_tag`、`scheduler_tag`。`forge::any_stop_token` 是非标准擦除，分配不遵从 env allocator，不能用作标准 stop-token API 的替代承诺。

## 验证

`scripts/verify-native.sh tsan` 与 `scripts/verify-native.sh asan` 分别覆盖 execution
子集的 ThreadSanitizer 与 ASan+UBSan 路径；`gcc-exec` 覆盖 libstdc++ 上的
execution-only 路径。

## 已移除扩展与迁移

仓库尚未发布。Backport 跟随工作草案与已采纳提案，超出草案范围的接口直接移除，未设弃用期。
Forge 扩展在 `forge::` 命名空间提供。
若明显的起草错误改变返回类型或语义，以至于按字面措辞编写的代码无法在遵循参考实现的原生库上编译，
backport 采用参考实现及草案的明确意图，在符合性台账与 DEVIATIONS 中记录该例外，直至 LWG 解决。
仅使 backport 更严格的措辞问题仍按字面实现。

P3682 将 `split` 从工作草案移除；该接口现由 Forge 头文件提供，execution backport
已完全移除其实现。

表中六个 bulk 迁移形式都需要原生 execution policy 对象。没有这些对象时，
`<forge/bulk.hpp>` 不声明 `forge::bulk`、`forge::bulk_chunked` 或 `forge::bulk_unchunked`。

| 旧用法 | 迁移后的用法 |
| --- | --- |
| `std::execution::sync_wait(s)` | `std::this_thread::sync_wait(s)` |
| `std::this_thread::sync_wait(s)` 或 `forge::sync_wait(s)`（`s` 有多种 value completion） | `std::this_thread::sync_wait_with_variant(s)` 返回 `optional<V>`，其中 `V` 是以各 decayed tuple 为备选项的 variant |
| `std::this_thread::sync_wait(s)` 或 `forge::sync_wait(s)`（`s` 无 value completion） | 用 `auto op = connect(s, receiver)` 和 `start(op)`，由 receiver 处理 stopped 或 error |
| `std::execution::when_all(a, b)`（child 有多种 value completion，结果为笛卡尔积） | `std::execution::when_all_with_variant(a, b)`；每个 child 的 value 以一个 variant 参数交付 |
| `std::execution::connect(s, r)` 借用 immovable source 或不可复制的自定义左值 sender | 可移动的 source 用 `std::execution::connect(std::move(s), r)`。若 source 不能移动，用保存其指针或引用的可移动 sender 连接，并让被借用对象活到 operation 结束 |
| `co_await std::move(s)`，依赖 `set_value_t(int)`、`set_value_t(int&)` 在 decay 后合并 | `co_await std::execution::into_variant(std::move(s))`，按 variant 结果消费。若两种完成本来就应交付同一类型，可令 sender 只声明并交付一种 value signature |
| `co_await s`，依赖 sender bridge 隐式复制左值 | LWG 4547 后可直接等待符合条件的左值，sender bridge 借用该对象并以左值连接。自定义左值 `connect` 可以观察并修改原对象。若要与这些修改隔离，先显式复制再等待副本：`auto copy = s; co_await std::move(copy);`。不可复制但可移动的 sender 仍须用 `co_await std::move(s)` 消费 |
| `as_awaitable(s, p)` 依赖可同时调用的 member 与 ADL `operator co_await` 优先选中 member 分支 | 为该值类别只提供一个可调用的 `operator co_await`，或明确调用所需的成员，例如 `co_await std::move(s).operator co_await()` |
| `std::execution::split(s)` | 包含 `<forge/split.hpp>` 后调用 `forge::split(s)` |
| 两种 counting scope 的 `scope.count()` / `scope.is_closed()`，以及已移除的 `forge::scope_count(scope)` / `forge::scope_is_closed(scope)` | 协调工作时用 `scope.close()` 拒绝新的关联，再用 `std::this_thread::sync_wait(scope.join())` 等待已有关联释放。若应用需要快照，由调用方维护计数和关闭状态，并在并发访问时自行同步。 |
| `std::execution::receiver_t`、`sender_t`、`operation_state_t`、`scheduler_t` | 使用 `std::execution::receiver_tag`、`sender_tag`、`operation_state_tag`、`scheduler_tag` |
| `std::execution::forwarding_query`、`get_stop_token`、`get_allocator` 及各自的 `_t` 类型、`stop_token_of_t` | 使用 `std::forwarding_query`、`std::get_stop_token`、`std::get_allocator`、`std::forwarding_query_t`、`std::get_stop_token_t`、`std::get_allocator_t`、`std::stop_token_of_t` |
| `std::execution::make_env(envs...)` | `std::execution::env{envs...}`；需要保留转发的值类别时显式传入相应的左值或右值引用表达式 |
| `std::execution::queryable<E>` | `std::destructible<E>` |
| `std::execution::sender_to<S, R>` | 用 `receiver<R>`、`sender_in<S, env_of_t<R>>`、各 completion signature 的接收要求与 `connect(s, r)` 的有效性表达所需约束 |
| `std::stoppable_token_for<Token, Callback>` | 用 `std::stoppable_token<Token>`、`std::invocable<Callback>` 与 `std::stop_callback_for_t<Token, Callback>` 的所需构造约束表达兼容性 |
| `std::execution::sync_wait_with_variant(s)` | `std::this_thread::sync_wait_with_variant(s)` |
| `std::execution::empty_env` | `std::execution::env<>` 或 `forge::empty_env`（同一类型） |
| `std::execution::transfer_just(sch, values...)` | `forge::transfer_just(sch, values...)` 或 `std::execution::continues_on(std::execution::just(values...), sch)` |
| `std::execution::make_prop(q, value)` | `std::execution::prop(q, value)` |
| `write_env(env)` | `write_env(sndr, env)` |
| `unstoppable()` | `unstoppable(sndr)` |
| `sndr \| unstoppable` | `unstoppable(sndr)` |
| 用 `unstoppable` 组合 closures | 在自定义 `sender_adaptor_closure` 内调用 `unstoppable(sndr)` |
| `into_variant()` | `into_variant(sndr)` 或 `sndr \| into_variant` |
| `stopped_as_optional()` | `stopped_as_optional(sndr)` 或 `sndr \| stopped_as_optional` |
| `on(sch, closure)` 的非 pipeable closure 实参 | 使用 `then(f)` 等库 closure，或派生自 `sender_adaptor_closure` 的用户 closure |
| `on(sndr, sch, closure)` 的非 pipeable closure 实参 | 使用 `then(f)` 等库 closure，或派生自 `sender_adaptor_closure` 的用户 closure |
| `std::execution::bulk(sndr, shape, f)` | `forge::bulk(sndr, shape, f)` 或传入执行策略 |
| `std::execution::bulk(shape, f)` | `forge::bulk(shape, f)` 或传入执行策略 |
| `std::execution::bulk_chunked(sndr, shape, f)` | `forge::bulk_chunked(sndr, shape, f)` 或传入执行策略 |
| `std::execution::bulk_chunked(shape, f)` | `forge::bulk_chunked(shape, f)` 或传入执行策略 |
| `std::execution::bulk_unchunked(sndr, shape, f)` | `forge::bulk_unchunked(sndr, shape, f)` 或传入执行策略 |
| `std::execution::bulk_unchunked(shape, f)` | `forge::bulk_unchunked(shape, f)` 或传入执行策略 |
| `default_domain::transform_sender(sndr, env)` | `default_domain::transform_sender(tag, sndr, env)` |
| 自定义 domain 的两参数 `transform_sender(sndr, env)` 分支 | 提供 `transform_sender(tag, sndr, env)`。 |
| `env_domain_t<Env>` | `decltype(get_domain(env))` |
| `receiver_domain_t<R>` | `decltype(get_domain(get_env(rcvr)))` |
| `sender_domain_t<S, Env>` | `decltype(get_domain(get_env(sndr)))`。旧别名未使用 `Env`。 |
| `read_env_t` | `std::remove_cvref_t<decltype(read_env)>` |
| `value_types_of<S, Env>::type` | `value_types_of_t<S, Env>` |
| `error_types_of<S, Env>::type` | `error_types_of_t<S, Env>` |
| `sends_stopped_v<S, Env>` | `sends_stopped<S, Env>` |
| `receiver_of` | 用 `receiver` 和各 completion signature 的调用要求表达约束。 |
| `get_forward_progress_guarantee(sch)` 的 `weakly_parallel` 默认分支 | 提供 `sch.query(get_forward_progress_guarantee) const noexcept`。 |
| `sndr \| continues_on(sch)` | `continues_on(sndr, sch)` |
| `tag_invoke(get_env_t, const R&)` | `R::get_env() const noexcept` |
| `tag_invoke(std::get_stop_token_t, const E&)`（其他环境查询同样迁移） | `E::query(std::get_stop_token_t) const noexcept` |
| `tag_invoke(std::forwarding_query_t, Q)` | `Q::query(std::forwarding_query_t) const noexcept -> bool`，常量实参须产生常量结果 |
| `tag_invoke(set_value_t, R&&, V)`（`set_error` / `set_stopped` 同理） | `R::set_value(V) && noexcept` |
| `tag_invoke(start_t, O&)` | `O::start() & noexcept` |
| `tag_invoke(connect_t, S, R)` | 按 sender 的调用类别提供 `S::connect(R)` |
| `tag_invoke(schedule_t, const Sch&)` | `Sch::schedule() const` |
| `tag_invoke(get_completion_signatures_t, S, E)` | `S::get_completion_signatures<Self, Env...>()` 静态成员模板 |

当前 `continues_on` 是二参数 CPO。P3826R3 把原 `schedule_from` 的措辞移入
`[exec.continues.on]` 后只规定了该形式。如后续 LWG 问题或提案恢复 pipeability，
backport 将随工作草案更新。`starts_on(sch, sndr)` 与 `schedule_from(sndr)`
也按各自的 CPO 调用形式提供。[工作草案](https://eel.is/c++draft/exec.continues.on)

`<forge/bulk.hpp>` 的三个入口调用相应标准 adaptor 并传入 `std::execution::seq`。
它们需要原生 execution policy 对象，并要求已注入 execution backport 或有完整原生
senders。Forge 的 pipe closure 只绑定 shape 和 callback，应用时才把 `seq` 传给直接调用，
因此不要求 policy 可复制。Backport 实现按索引顺序执行，`bulk_chunked` 对正数 shape 执行一个 chunk。
`forge::transfer_just` 组合 `just` 与 `continues_on`，可用于完整原生实现。

`forge::sync_wait` 与 `std::this_thread::sync_wait` 是同一个对象，原生与 backport
均要求恰好一个 value completion signature。`set_value_t()` 是一个签名，结果值为
`tuple<>`。`sync_wait_with_variant` 通过 `into_variant` 在 decay 后按 tuple 类型去重。
无 value completion 的 sender 经 `into_variant` 后仍无 value completion，因此
无法形成 `sync_wait_with_variant` 所需的结果类型，调用违反其结果类型的 Mandate。

`forge::empty_env` 在两种模式下均为 `std::execution::env<>`。
`get_completion_signatures_t` 等 CPO 类型名另随接口形状迁移。

### when_all 的 WD 存储环境

`[exec.when.all]/9` 与 /10 用带内部 stop token 的环境检查并连接 child，
/13 则用外层 receiver 环境的 FWD-ENV 计算所有 value slots。若任何 child 在
FWD-ENV 下无法形成单一 value tuple，整个存储类型为 `tuple<>`。
依赖 stop-token 类型或 forwarding query 分类的 child 可能在两个环境中声明不同
value 类型。实现采用 /13 的存储类型与 /15.1 的交付形式。即使某个 child 没有
value completion，空存储下的 started 分支仍包含 `set_value(rcvr)`，因此签名包括 `set_value_t()`。

### sync_wait_with_variant 的 WD 返回类型

`[exec.sync.wait.var]/1` 将 `Sndr` 定义为 `decltype(into_variant(sndr))`。
/2 与 /3 使用 `[exec.sync.wait]/3` 定义的结果类型。若适配后的 sender 在
`sync-wait-env` 中唯一的 value 是 `V`，按字面计算得到 `optional<variant<tuple<V>>>`。
Backport 采用参考实现 stdexec 的扁平类型 `optional<V>`，符合 /4.2.1
直接交付 tuple 的 variant 的意图，也让调用代码在切换到遵循参考实现的原生库时保持源码兼容。
该起草错误例外记录在符合性台账中，并随 LWG 的解决方案更新。
[工作草案](https://eel.is/c++draft/exec.sync.wait.var)
[stdexec 源码](https://github.com/NVIDIA/stdexec/blob/main/include/stdexec/__detail/__sync_wait.hpp)

### Coroutine 桥的 WD 措辞问题

`[exec.awaitable]/5` 的 `has-as-awaitable` 将 promise 写成 `Promise&`，但
`coroutine_handle` 需要 promise 类型；实现使用无引用的 Promise。
`[exec.as.awaitable]/1` 的 `awaitable-sender` 要求 `unhandled_stopped`，/7.4 却不再使用
该 concept；停止的 ill-formedness 因此由 connect 的 receiver-of Mandate 决定。
[LWG 4547](https://cplusplus.github.io/LWG/issue4547) 在 /2 增加 deduction guide，
使 /7.4 按适配后表达式的 cv/ref 类别推导 `Sndr`；符合条件的左值按引用保存和连接。
由于 backport 的 `as_awaitable` 是函数，sender bridge 在构造时依次求值并保留准备链中的
prvalue：promise environment、变换结果、变换后 sender 的 environment、completion adaptor。
引用中间结果不复制；若不适配，变换结果直接作为 prepared sender 保存。即使 prepared sender
按值返回，它也可能借用这些中间对象。桥先销毁 operation，再销毁其保存的 prepared sender，
最后按构造的逆序销毁保留的中间对象，以模拟 WD 完整表达式的生命周期。存储不把这些中间类型
引入 `operator co_await` 的 ADL。外部借用对象仍须由调用方保留到 awaitable 及其 operation 销毁。
与 WD 仅供说明的 `sender-awaitable<Sndr, Promise>` 不同，backport 的 bridge 类型还取决于
内联保留存储的大小和对齐。即使 prepared sender 和 promise 类型相同，若两条准备链的存储布局
不同，在同一个推导返回类型的函数中分别返回两个 bridge 也会因类型不一致而失败。同理，
只接受某个具体 bridge 类型的 `operator co_await` 重载可能对其中一条准备链被选中，而另一条
仍使用 bridge 自身的 awaiter；bridge 自身的连接、完成和销毁路径不依赖存储布局。
[LWG 4357](https://cplusplus.github.io/LWG/issue4357) 要求 `[exec.connect]/5` 用
`is_void_v` 判断结果类型，因此 cv-qualified `void` 也触发零参数 `set_value`。
依赖 constexpr exceptions 的 completion-signature diagnostics
仍以 SFINAE 表达，不宣称完整 C++26 consteval API。

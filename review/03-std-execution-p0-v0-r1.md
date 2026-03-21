# `std::execution` 标准质量审查报告 (p0-v0-r1)

审查对象：
- Wrapper 注入：`backport/execution`
- 实现文件：`backport/cpp26/execution.hpp`
- 测试文件：`test/execution/runtime/test_execution_mvp.cpp`、`test/execution/runtime/test_execution_inline_scheduler.cpp`、`test/execution/runtime/test_execution_stop_token.cpp`

审查基线：
- 当前工作树，基于提交 `c7f065c`
- **当前 C++ 工作草案 `[exec]`**：`https://eel.is/c++draft/exec`
- 辅助参考：`https://en.cppreference.com/w/cpp/execution`

审查视角说明：
- 前两轮（`v0-r0`、追溯为 `p0-v0-r0`，以及 `v1-r0`）以 P2300R10 提案文本为基线，偏重工程功能验证
- 本轮**纠正审查基线**，改用 eel.is 当前工作草案 `[exec]`，按标准库交付质量要求审查
- 本轮审查维度：规范基线对齐、概念定义精确性、定制点机制、异常安全、ADL 隔离、ODR 正确性、测试标准表面锁定

---

## 执行摘要

按标准委员会和标准库交付质量要求看，当前 `std::execution` MVP 存在以下结构性问题：

1. **定制点机制与当前草案根本性偏离**：实现完全基于 `tag_invoke` 分发，而当前工作草案 `[exec]` 已移除 `tag_invoke`，改为基于 `tag_of_t` + 结构化分发的机制。这不仅是内部实现细节，而是直接影响用户可见的定制行为。

2. **概念定义与 `[exec]` 草案存在多处偏差**：`sender`、`receiver`、`operation_state`、`scheduler` 的约束条件与当前草案不精确匹配，其中 `sender` concept 缺少 `get_env` 可查询要求，`operation_state` concept 不检查对象类型以外的析构要求。

3. **`inplace_stop_source` 的 `request_stop()` 在持有锁时调用用户回调**：违反了当前草案的语义要求（回调不应在锁保护下执行），且存在死锁风险。

4. **`sync_wait` 缺少 `run_loop`**，语义与 `[exec.sync.wait]` 不符：当前草案要求 `sync_wait` 通过 `run_loop` 驱动，receiver 环境中必须提供 `get_scheduler` 查询返回 `run_loop` 的 scheduler。

5. **测试未按标准表面锁定**：无编译探针验证概念约束正/反例，无签名类型推导验证，测试仅覆盖运行时行为。

---

## 第一部分：实现问题

### I-1 定制点机制使用 `tag_invoke`，与当前草案 `[exec]` 根本性偏离

- 严重程度：高
- 位置：`backport/cpp26/execution.hpp` 第 231-272 行（`__forge_detail::__tag_invoke`），及所有 CPO 定义
- 相关条文：`[exec.general]`、`[exec.snd]`、`[exec.recv]`、`[exec.connect]`

**问题描述：**

当前实现的所有 CPO（`set_value`、`set_error`、`set_stopped`、`connect`、`start`、`get_env`、`get_completion_signatures`、`schedule`）均通过内部的 `tag_invoke` 协议分发：

```cpp
namespace __forge_detail {
namespace __tag_invoke {
void tag_invoke() = delete;
struct fn {
    template<class Tag, class... Args>
        requires requires(Tag&& tag, Args&&... args) {
            tag_invoke(static_cast<Tag&&>(tag), static_cast<Args&&>(args)...);
        }
    constexpr auto operator()(Tag&& tag, Args&&... args) const ...
};
} // namespace __tag_invoke
} // namespace __forge_detail
```

而当前工作草案 `[exec]` 中，定制点的分发规则已从 `tag_invoke` 转向以下模式：

1. CPO 首先检查对象是否提供对应的成员函数（如 `sndr.connect(rcvr)`）
2. 使用 `tag_of_t<Sndr>` 确定 sender 的"tag"类型，通过 `default_domain` 或用户 domain 的 `transform_sender`/`apply_sender` 完成分发
3. `tag_invoke` 不再作为标准定制协议出现

**影响：**

- 用户如果按当前草案定义自定义 sender（通过成员 `connect`），在 Forge backport 上无法工作
- 当标准库原生提供 `<execution>` 时，基于 `tag_invoke` 的用户定制代码需要全部重写——直接违反"无感过渡"原则
- 这不是可以通过 TODO 标注搁置的问题，而是 MVP 的核心架构决策

**建议处置（两条路线）：**

- **路线 A（严格对齐）**：将 CPO 分发改为成员函数优先 + domain-based fallback。工作量大，但符合无感过渡原则。
- **路线 B（务实过渡）**：保留 `tag_invoke` 作为内部实现机制，但 (a) 将其完全隐藏在 `__forge_detail` 中，不作为用户可见的定制协议；(b) 内置 sender/receiver 类型同时提供 `friend tag_invoke` 和成员函数；(c) 在文档和注释中明确标注偏差。这允许后续逐步迁移。

---

### I-2 `sender` concept 缺少 `get_env` 可查询约束

- 严重程度：高
- 位置：`backport/cpp26/execution.hpp` 第 616-621 行
- 相关条文：`[exec.snd]`

**问题描述：**

当前 `sender` concept 定义为：

```cpp
template<class S>
concept sender =
    std::move_constructible<std::remove_cvref_t<S>> &&
    std::constructible_from<std::remove_cvref_t<S>, std::remove_cvref_t<S>> &&
    requires { typename std::remove_cvref_t<S>::sender_concept; } &&
    std::derived_from<typename std::remove_cvref_t<S>::sender_concept, sender_t>;
```

当前工作草案 `[exec.snd]` 中 `sender` concept 的定义还要求对 `remove_cvref_t<Sndr>` 的 const 左值引用调用 `get_env` 能成功。具体来说，当前草案中 `sender` 的定义大致为：

```
template<class Sndr>
concept sender =
    derived_from<typename remove_cvref_t<Sndr>::sender_concept, sender_t> &&
    move_constructible<remove_cvref_t<Sndr>> &&
    requires (const remove_cvref_t<Sndr>& sndr) {
        { get_env(sndr) } -> queryable;
    };
```

当前实现缺少 `get_env` 约束，导致 concept 过宽。

---

### I-3 `receiver` concept 缺少 `get_env` 返回值的 `queryable` 约束

- 严重程度：中
- 位置：`backport/cpp26/execution.hpp` 第 576-582 行
- 相关条文：`[exec.recv]`

**问题描述：**

当前 `receiver` concept 定义：

```cpp
template<class R>
concept receiver =
    std::move_constructible<std::remove_cvref_t<R>> &&
    std::constructible_from<std::remove_cvref_t<R>, std::remove_cvref_t<R>> &&
    requires { typename std::remove_cvref_t<R>::receiver_concept; } &&
    std::derived_from<typename std::remove_cvref_t<R>::receiver_concept, receiver_t> &&
    requires(const std::remove_cvref_t<R>& r) { std::execution::get_env(r); };
```

当前草案 `[exec.recv]` 要求 `get_env(r)` 的返回类型满足 `queryable` concept（即析构不抛异常的对象类型）。当前实现仅要求表达式有效，未约束返回类型。

此外，当前草案中 `queryable` concept 本身也未被定义。

---

### I-4 缺少 `queryable` concept

- 严重程度：中
- 位置：整个文件缺失
- 相关条文：`[exec.queryable]`

**问题描述：**

当前草案 `[exec.queryable]` 定义了 `queryable` concept，大致为"析构不抛异常的对象类型"。这是 `sender`、`receiver`、`env` 等 concept 链中的基础约束。当前实现完全缺失该 concept。

---

### I-5 `operation_state` concept 缺少析构约束

- 严重程度：中
- 位置：`backport/cpp26/execution.hpp` 第 569-571 行
- 相关条文：`[exec.opstate]`

**问题描述：**

当前定义：

```cpp
template<class O>
concept operation_state =
    std::destructible<O> && std::is_object_v<O> &&
    requires(O& op) { { std::execution::start(op) } noexcept; };
```

当前草案 `[exec.opstate]` 的 `operation_state` concept 还额外要求 `O` 不是 `move_constructible` 的 — 这是一个显式的"不可移动"约束（operation state 一旦构造，不应被移动）。当前实现缺少此负约束。

**注意**：此负约束会影响 `sync_wait` 等内部使用 operation state 的代码，需要确保内部 operation state 类型不提供移动构造。

---

### I-6 `inplace_stop_source::request_stop()` 在持有 mutex 时调用用户回调

- 严重程度：高
- 位置：`backport/cpp26/execution.hpp` 第 85-107 行
- 相关条文：`[stopsource.inplace.mem]`

**问题描述：**

当前 `request_stop()` 实现：

```cpp
bool request_stop() noexcept {
    std::lock_guard lk{mtx_};              // <-- 持有锁
    // ... 设置 stop-requested 标志 ...
    auto* cb = callbacks_;
    callbacks_ = nullptr;
    while (cb) {
        auto* next = cb->next;
        // ...
        if (cb->invoke_fn) {
            cb->invoke_fn(cb);             // <-- 在锁保护下调用用户回调
        }
        cb = next;
    }
    return true;
}
```

用户回调 `cb->invoke_fn(cb)` 在 `lock_guard` 的保护下被调用。这存在两个问题：

1. **死锁风险**：如果用户回调中尝试注册或移除其他回调（调用 `try_add_callback`/`remove_callback`，它们也需要获取 `mtx_`），将产生死锁。当前草案 `[stopsource.inplace.mem]` 的语义描述中，回调的执行不应持有内部同步状态的锁。

2. **并发性能**：锁持有期间执行用户代码，阻塞了其他线程对 stop source 的所有操作。

**建议修复：** 在释放锁后遍历并调用回调链表。需要将回调链表从 `callbacks_` 中"取出"后再释放锁，然后逐个调用。

---

### I-7 `inplace_stop_source` 析构函数在持有锁时清空回调链表

- 严重程度：低
- 位置：`backport/cpp26/execution.hpp` 第 75-78 行
- 相关条文：`[stopsource.inplace.general]`

**问题描述：**

```cpp
~inplace_stop_source() noexcept {
    std::lock_guard lk{mtx_};
    callbacks_ = nullptr;
}
```

当前草案中 `inplace_stop_source` 的析构要求所有 `inplace_stop_callback` 对象在 source 析构前已被销毁（前置条件）。因此析构函数不需要在锁保护下做任何事情——如果前置条件被满足，`callbacks_` 已经为空。当前实现的锁获取虽然无害（前置条件满足时为空操作），但掩盖了对前置条件的依赖，且在前置条件违反时静默丢弃回调而非产生未定义行为/诊断。

---

### I-8 `inplace_stop_token` 缺少 `swap` 成员函数

- 严重程度：低
- 位置：`backport/cpp26/execution.hpp` 第 154-173 行
- 相关条文：`[stoptoken.inplace]`

**问题描述：**

当前草案要求 `inplace_stop_token` 提供 `void swap(inplace_stop_token&) noexcept` 成员函数以及非成员 `friend swap`。当前实现缺少此成员函数。

---

### I-9 缺少 `stoppable_token`、`stoppable_token_for`、`unstoppable_token` concepts

- 严重程度：中
- 位置：整个文件缺失
- 相关条文：`[stoptoken.concepts]`

**问题描述：**

当前草案 `[stoptoken.concepts]` 定义了三个 stop token concepts：

- `stoppable_token` — 要求 `stop_requested()`, `stop_possible()`, copy constructible, equality comparable
- `stoppable_token_for<Token, CallbackFn>` — 要求可用 `CallbackFn` 构造 `stop_callback_for_t<Token, CallbackFn>`
- `unstoppable_token` — 要求 `stoppable_token` 且 `stop_possible()` 编译期为 false

这些 concepts 是 `get_stop_token` 查询和 receiver 环境约束的基础设施。当前实现完全缺失。

---

### I-10 `sync_wait` 缺少 `run_loop`，receiver 环境不符合 `[exec.sync.wait]`

- 严重程度：高
- 位置：`backport/cpp26/execution.hpp` 第 994-1128 行
- 相关条文：`[exec.sync.wait]`

**问题描述：**

当前草案 `[exec.sync.wait]` 要求 `sync_wait` 使用 `run_loop` 作为执行上下文，且 `sync_wait` 内部构造的 receiver 的环境必须满足：

- `get_scheduler(env)` 返回 `run_loop` 的 scheduler
- `get_delegatee_scheduler(env)` 返回 `run_loop` 的 scheduler
- `get_stop_token(env)` 返回 `inplace_stop_token`

当前实现使用 `mutex + condition_variable` 阻塞等待，receiver 环境仅提供 `get_stop_token`，**缺少 `get_scheduler` 和 `get_delegatee_scheduler` 查询**。

这意味着：
- 依赖 `read_env(get_scheduler)` 的 sender 在 `sync_wait` 内部无法获取 scheduler
- `continues_on` / `starts_on` 等算法无法正确工作
- 用户无法在 `sync_wait` 中使用需要调度回调用线程的 sender 链

---

### I-11 `backport/execution` wrapper 的 `#else` 分支直接 `#error`

- 严重程度：中
- 位置：`backport/execution` 第 38-41 行
- 相关条文：无（工程问题）
- 前轮追踪：v1-r0 I-1（未修复）

**问题描述：**

```cpp
#else
#error "Forge backport requires a compiler-specific <execution> include strategy"
#endif
```

在非 MSVC 且不支持 `__has_include_next` 的工具链（如 NVCC、ICC、某些嵌入式编译器）上，会直接触发编译错误。合理做法是静默跳过标准 `<execution>` 的包含，仅提供 P2300 MVP 层。

---

### I-12 `just`/`just_error`/`just_stopped` sender 的 `get_env` 返回 `empty_env`

- 严重程度：低
- 位置：`backport/cpp26/execution.hpp` 第 700、739、772 行
- 相关条文：`[exec.just]`
- 前轮追踪：v1-r0 I-2（未修复）

**问题描述：**

当前草案 `[exec.just]` 中，`just`/`just_error`/`just_stopped` 的 sender 环境应通过 exposition-only 的 `just-env` 类型提供 `get_completion_scheduler<CPO>` 查询。当前实现返回 `empty_env`。

MVP 阶段可暂缓，但应标注 TODO。

---

### I-13 `then_closure::operator|` 与 `[exec.adapt.obj]` sender adaptor closure 机制不符

- 严重程度：中
- 位置：`backport/cpp26/execution.hpp` 第 951-976 行
- 相关条文：`[exec.adapt.obj]`
- 前轮追踪：v1-r0 I-3（未修复）

**问题描述：**

当前草案 `[exec.adapt.obj]` 定义了 `sender_adaptor_closure` CRTP 基类作为 pipe operator 的标准化机制。当前实现通过逐 adaptor 的 `friend operator|` 实现，功能上等价但结构性偏离标准。

每个新 adaptor（`let_value`、`bulk` 等）都需要复制此模式，而标准方式只需继承基类。

---

### I-14 `get_env` 对所有内置 sender/receiver 类型返回值缺少 `queryable` 约束验证

- 严重程度：低
- 位置：所有 `tag_invoke(get_env_t, ...)` 的返回类型
- 相关条文：`[exec.queryable]`

**问题描述：**

当前草案要求 `get_env` 的返回类型满足 `queryable` concept。由于 I-4 中 `queryable` concept 本身缺失，当前代码中所有 `get_env` 返回的类型（`empty_env`、`prop<...>`、`env<...>`、`__forge_inline::env`、sync_wait receiver 的 env）都未经过任何编译期约束验证。

---

### I-15 `request_stop()` 返回值语义与草案微差

- 严重程度：低
- 位置：`backport/cpp26/execution.hpp` 第 85-107 行
- 相关条文：`[stopsource.inplace.mem]`

**问题描述：**

当前草案 `[stopsource.inplace.mem]` 中 `request_stop()` 的返回值语义为"如果本次调用导致了 stop-requested 状态的改变则返回 `true`"。当前实现的逻辑正确（已请求时返回 `false`，首次请求返回 `true`），但 `request_stop()` 在成功路径上返回 `true` 之前应先完成所有回调的调用。当前实现在锁内调用回调，时序正确但存在 I-6 描述的锁问题。

---

## 第二部分：ADL 隔离与 ODR 正确性

### I-16 `__forge_detail` 命名空间的 ADL 阻断不完整

- 严重程度：中
- 位置：`backport/cpp26/execution.hpp` 第 231-273 行
- 相关条文：标准库实现通行规范

**问题描述：**

`__forge_detail::tag_invoke_fn` 是一个 `inline constexpr` 变量（第 252 行），其类型 `__tag_invoke::fn` 的 `operator()` 模板通过 ADL 查找 `tag_invoke`。但 `__forge_detail` 命名空间位于 `std::execution` 内部，意味着 ADL 可能在某些边缘场景下找到用户在 `std::execution` 或 `std` 中定义的无关 `tag_invoke` 重载。

标准库实现通常使用双下划线前缀的"detail"命名空间，并配合 `void tag_invoke() = delete;`（poison pill）来阻断不期望的 ADL。当前实现确实有 poison pill（第 236 行），但位于 `__forge_detail::__tag_invoke` 内部，位置正确。

**评估：** ADL 阻断基本正确。但应确认 `__forge_detail` 本身不会因为位于 `std::execution` 中而产生意外的关联命名空间查找。当前代码中，所有 `tag_invoke` 的 friend 声明都在具体类型的定义内（如 `__forge_just::sender`），通过 friend 注入到类型的关联命名空间，这是正确的 ADL 使用方式。

---

### I-17 ODR 正确性：所有内联定义位置正确

- 严重程度：信息性
- 位置：整个文件
- 相关条文：`[basic.def.odr]`

**评估：**

- 所有 CPO 实例（`set_value`、`connect` 等）均为 `inline constexpr` — ODR 正确
- 所有类模板成员函数在类内定义 — ODR 正确
- `inplace_stop_source::get_token()` 在类外定义为 `inline`（第 175 行）— ODR 正确
- `__forge_inline::tag_invoke(get_scheduler_t, const env&)` 在命名空间内定义（第 1182 行）— 需确认 `inline` 标记。当前为 `inline auto tag_invoke(...)` — ODR 正确

无 ODR 问题。

---

## 第三部分：测试问题

### T-1 缺少编译探针验证概念约束正/反例

- 严重程度：高
- 位置：所有测试文件
- 相关条文：`[exec.snd]`、`[exec.recv]`、`[exec.opstate]`、`[exec.sched]`

**问题描述：**

当前测试中仅有一处编译期断言（`test_execution_inline_scheduler.cpp` 第 9 行）：

```cpp
static_assert(std::execution::scheduler<std::execution::inline_scheduler>);
```

缺少以下关键编译探针：

**正例（应满足）：**
- `static_assert(sender<decltype(just(42))>)`
- `static_assert(sender_in<decltype(just(42)), empty_env>)`
- `static_assert(sender<decltype(just_error(runtime_error{""}))>)`
- `static_assert(sender<decltype(just_stopped())>)`
- `static_assert(sender<decltype(just(42) | then([](int x){ return x; }))>)`

**反例（不应满足）：**
- `static_assert(!sender<int>)` — 普通类型不是 sender
- `static_assert(!receiver<int>)` — 普通类型不是 receiver
- `static_assert(!scheduler<int>)` — 普通类型不是 scheduler

**签名类型推导验证：**
- `value_types_of<just(42), empty_env>` 应为 `variant<tuple<int>>`
- `error_types_of<just_error(e), empty_env>` 应含错误类型
- `sends_stopped_v<just_stopped(), empty_env>` 应为 `true`
- `then` 变换后的签名验证

---

### T-2 缺少 scheduler env roundtrip 运行时验证

- 严重程度：低
- 位置：`test/execution/runtime/test_execution_inline_scheduler.cpp`
- 前轮追踪：v1-r0 T-1（未修复）

**问题描述：**

前轮 v1-r0 已指出缺少验证 `get_scheduler(get_env(schedule(sch))) == sch`。仍未补充。

---

### T-3 缺少 `then` void 返回和多值输入的签名变换测试

- 严重程度：低
- 位置：`test/execution/runtime/test_execution_mvp.cpp`
- 前轮追踪：v1-r0 T-3（未修复）

**问题描述：**

`ThenVoidReturnBecomesEmptyTuple` 已有运行时测试，但缺少编译期 `static_assert` 验证签名类型。多值 `just(1, 2) | then([](int a, int b){...})` 无运行时测试。

---

### T-4 `stop_token` 测试覆盖面不足

- 严重程度：中
- 位置：`test/execution/runtime/test_execution_stop_token.cpp`

**问题描述：**

当前仅 2 个测试（callback 触发、idempotent），缺少：
- 在 stop 已请求后注册 callback 应立即触发
- callback 析构时自动反注册
- `never_stop_token` 的基本行为
- `inplace_stop_token` 默认构造（`stop_possible() == false`）

---

## 第四部分：总结

### 按严重程度汇总

| 严重程度 | 实现问题 | 测试问题 | 合计 |
|----------|----------|----------|------|
| 高       | 4 (I-1, I-2, I-6, I-10) | 1 (T-1) | 5 |
| 中       | 5 (I-3, I-4, I-5, I-11, I-13) + I-9, I-16 = 7 | 1 (T-4) | 8 |
| 低       | 4 (I-7, I-8, I-12, I-15) | 2 (T-2, T-3) | 6 |
| 信息性   | 2 (I-14, I-17) | 0 | 2 |
| 合计     | 17       | 4        | 21   |

### 优先修复建议

1. **最高优先级 — I-6：** `request_stop()` 在持锁时调用用户回调是可触发的死锁 bug。必须在释放锁后调用回调。这是本轮唯一的"当前代码在合理使用场景下会出错"的问题。

2. **高优先级 — I-1（标注）：** `tag_invoke` vs 成员函数是架构级决策，本轮无法完成迁移。但必须在代码注释和文档中明确标注与当前草案的偏差，避免用户误以为 `tag_invoke` 是标准定制协议。

3. **高优先级 — I-2：** `sender` concept 补充 `get_env` 约束。

4. **高优先级 — I-10（标注）：** `sync_wait` 缺少 `run_loop` 是 MVP 阶段的已知限制。补充 `get_scheduler` 和 `get_delegatee_scheduler` 到 receiver env 中（返回 `inline_scheduler` 作为临时替代），并在文档中标注与标准的偏差。

5. **高优先级 — T-1：** 补充编译探针。这是防止概念定义回归的核心防线。

6. **中优先级 — I-3, I-4, I-5, I-9, I-11, I-13：** 概念定义精化和缺失组件补充。

7. **低优先级 — I-7, I-8, I-12, T-2, T-3, T-4：** 细节修复和测试补充。

### 与前轮审查的关系

本轮延续 p0-v0-r0（原 `v0-r0`）和 v1-r0 的审查线。前两轮以 P2300R10 提案为基线，侧重工程功能验证；本轮以 eel.is 当前工作草案 `[exec]` 为基线，增加了标准库质量维度。

前轮 v1-r0 的 6 条未修复项在本轮中的追踪：
- v1-r0 I-1 (`#else #error`) → 本轮 I-11
- v1-r0 I-2 (`just` env) → 本轮 I-12
- v1-r0 I-3 (adaptor closure) → 本轮 I-13
- v1-r0 T-1 (scheduler env roundtrip) → 本轮 T-2
- v1-r0 T-2 (sender 静态断言) → 本轮 T-1（扩展为系统性编译探针）
- v1-r0 T-3 (then void/多值) → 本轮 T-3

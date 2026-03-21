# `std::execution` 标准质量审查报告 (p0-v0-r2)

审查对象：
- Wrapper 注入：`backport/execution`
- 实现文件：`backport/cpp26/execution.hpp`
- 测试文件：`test/execution/runtime/test_execution_mvp.cpp`、`test/execution/runtime/test_execution_inline_scheduler.cpp`、`test/execution/runtime/test_execution_stop_token.cpp`

审查基线：
- 分支 `fix/std-execution-p0-r2`，基于 master 提交 `4ddc049`
- **当前 C++ 工作草案 `[exec]`**：`https://eel.is/c++draft/exec`
- 辅助参考：`https://eel.is/c++draft/stoptoken`

前轮状态：
- `p0-v0-r1` 发现 21 条问题（I-1 至 I-17，T-1 至 T-4）
- `p0-v0-r1-a0` 修复 12 条，暂缓 5 条，无需修复 2 条，随其他问题解决 2 条
- 本轮独立验证前轮修复落地，并查找新问题

---

## 执行摘要

前轮 12 条已修复项全部正确落地，验证通过。本轮在更细粒度地对照 `[exec]` 当前工作草案后，新发现 8 条实现问题和 2 条测试问题，合计 10 条。主要集中在：

1. **概念定义精度不足**：`receiver` 使用 `move_constructible` 而非 `is_nothrow_move_constructible_v`，缺少 `!is_final_v` 检查；`operation_state` 和 `scheduler` 缺少各自的 concept marker tag 要求。
2. **命名空间错误**：`sync_wait` 应位于 `std::this_thread` 而非 `std::execution`，直接违反无感过渡原则。
3. **前轮暂缓项中 `stoppable_token` concepts 现可实现**。

---

## 第零部分：前轮修复验证

对 `p0-v0-r1-a0` 中标记为"已修复"的 12 条逐一验证：

| r1 编号 | 描述 | 验证位置 | 状态 |
|---------|------|----------|------|
| I-2 | sender get_env queryable 约束 | 第 662-670 行 | ✓ 正确落地 |
| I-3 | receiver get_env queryable 约束 | 第 620-628 行 | ✓ 正确落地 |
| I-4 | queryable concept 定义 | 第 594-595 行 | ✓ 正确落地 |
| I-5 | operation_state !move_constructible | 第 611-615 行 | ✓ 正确落地 |
| I-6 | request_stop 释锁后调用回调 | 第 97-127 行 | ✓ 正确落地 |
| I-7 | 析构函数 default | 第 90 行 | ✓ 正确落地 |
| I-8 | inplace_stop_token swap | 第 183-184 行 | ✓ 正确落地 |
| I-11 | wrapper #else 静默回退 | `backport/execution` 第 39-42 行 | ✓ 正确落地 |
| T-1 | 编译探针正/反例 | test_execution_mvp.cpp | ✓ 正确落地 |
| T-2 | scheduler env roundtrip | test_execution_inline_scheduler.cpp | ✓ 正确落地 |
| T-3 | then 多值输入测试 | test_execution_mvp.cpp | ✓ 正确落地 |
| T-4 | stop_token 测试扩展 | test_execution_stop_token.cpp | ✓ 正确落地 |

**结论：全部 12 条修复验证通过，无回归。**

---

## 第一部分：前轮暂缓项状态更新

| r1 编号 | 描述 | 本轮状态 | 说明 |
|---------|------|----------|------|
| I-1 | tag_invoke 架构偏离 | 继续暂缓 | 架构级，phase 1 迁移 |
| I-9 | stoppable_token concepts | **现可实现** | 见本轮 I-5 |
| I-10 | sync_wait run_loop | 继续暂缓 | phase 1 核心交付物 |
| I-12 | just env completion scheduler | 部分可处理 | 依赖本轮 I-8 |
| I-13 | sender_adaptor_closure | 继续暂缓 | phase 1 随 adaptor 批量迁移 |

---

## 第二部分：新发现的实现问题

### I-1 `receiver` concept 使用 `move_constructible` 而非 `is_nothrow_move_constructible_v`，且缺少 `!is_final_v`

- 严重程度：高
- 位置：`backport/cpp26/execution.hpp` 第 620-628 行
- 相关条文：`[exec.recv]`（`https://eel.is/c++draft/exec#recv`）

**问题描述：**

当前 `receiver` concept 定义：

```cpp
template<class R>
concept receiver =
    std::move_constructible<std::remove_cvref_t<R>> &&
    std::constructible_from<std::remove_cvref_t<R>, std::remove_cvref_t<R>> &&
    requires { typename std::remove_cvref_t<R>::receiver_concept; } &&
    std::derived_from<typename std::remove_cvref_t<R>::receiver_concept, receiver_t> &&
    requires(const std::remove_cvref_t<R>& r) {
        { std::execution::get_env(r) } -> queryable;
    };
```

当前工作草案 `[exec.recv]` 要求：

1. **nothrow move constructible**：使用 `is_nothrow_move_constructible_v`，而非 `move_constructible`。这是故意的强化约束——receiver 的移动操作不能抛异常，因为 `connect` 等操作需要依赖移动 receiver 不会失败。
2. **non-final**：`!is_final_v<remove_cvref_t<R>>`。receiver 实现内部可能需要从用户 receiver 派生（wrapped receiver 模式），final class 会阻碍这一模式。

**应修改为：**

```cpp
template<class R>
concept receiver =
    std::is_nothrow_move_constructible_v<std::remove_cvref_t<R>> &&
    !std::is_final_v<std::remove_cvref_t<R>> &&
    requires { typename std::remove_cvref_t<R>::receiver_concept; } &&
    std::derived_from<typename std::remove_cvref_t<R>::receiver_concept, receiver_t> &&
    requires(const std::remove_cvref_t<R>& r) {
        { std::execution::get_env(r) } -> queryable;
    };
```

**影响：** concept 过宽——允许了抛异常的 move 和 final class 的 receiver 类型通过约束检查。当原生 `<execution>` 可用后，这些类型将被拒绝，用户代码无感过渡失败。

---

### I-2 `operation_state` concept 缺少 `operation_state_concept` marker 要求

- 严重程度：高
- 位置：`backport/cpp26/execution.hpp` 第 611-615 行
- 相关条文：`[exec.opstate]`（`https://eel.is/c++draft/exec#opstate`）

**问题描述：**

当前定义：

```cpp
template<class O>
concept operation_state =
    std::destructible<O> && std::is_object_v<O> &&
    !std::move_constructible<O> &&
    requires(O& op) { { std::execution::start(op) } noexcept; };
```

当前工作草案 `[exec.opstate]` 要求 `operation_state` concept 包含 marker tag 检查：

```
derived_from<typename O::operation_state_concept, operation_state_t>
```

这需要：
1. 定义 `operation_state_t` tag 类型（类似已有的 `sender_t` 和 `receiver_t`）
2. 所有内部 operation state 类型（`just_op`、`then_op`、`sync_wait_op`、`inline_scheduler::operation` 等）声明 `using operation_state_concept = operation_state_t;`
3. concept 中添加 marker 约束

**影响：** 当前定义下，任何不可移动、可析构、具有 noexcept `start()` 成员的类型都会错误地满足 `operation_state` concept。marker tag 的缺失导致概念边界过宽。

---

### I-3 `scheduler` concept 缺少 `scheduler_concept` marker 和 `queryable` 约束

- 严重程度：高
- 位置：`backport/cpp26/execution.hpp` 第 708-712 行
- 相关条文：`[exec.sched]`（`https://eel.is/c++draft/exec#sched`）

**问题描述：**

当前定义：

```cpp
template<class S>
concept scheduler =
    std::copy_constructible<std::remove_cvref_t<S>> &&
    std::equality_comparable<std::remove_cvref_t<S>> &&
    requires(std::remove_cvref_t<S>& s) { { std::execution::schedule(s) } -> sender_in; };
```

当前工作草案 `[exec.sched]` 要求：

1. **`scheduler_concept` marker**：`derived_from<typename Sch::scheduler_concept, scheduler_t>`。需要定义 `scheduler_t` tag 类型，`inline_scheduler` 需声明 `using scheduler_concept = scheduler_t;`。
2. **`queryable` 约束**：scheduler 自身需满足 `queryable<Sch>`（即 `destructible`，已隐含满足但应显式约束）。
3. **`schedule` 返回 `sender`**：草案要求 `schedule(sch)` 返回满足 `sender` 的类型，当前实现使用 `sender_in`（更强约束）。虽然 `sender_in` 蕴含 `sender`，但约束强度不应超出标准定义。

**影响：** 与 I-2 类似，marker tag 缺失导致概念边界过宽。使用 `sender_in` 替代 `sender` 在当前场景下无功能影响，但违反"不多不少"的 backport 原则。

---

### I-4 `sync_wait` 位于错误的命名空间

- 严重程度：高
- 位置：`backport/cpp26/execution.hpp` 第 1172 行
- 相关条文：`[exec.sync.wait]`（`https://eel.is/c++draft/exec#sync.wait`）

**问题描述：**

当前 `sync_wait` 定义在 `namespace std::execution` 中（第 1172 行）：

```cpp
template<sender_in S>
auto sync_wait(S&& sndr) {
    // ...
}
```

当前工作草案 `[exec.sync.wait]` 将 `sync_wait` 置于 `namespace std::this_thread`：

```
namespace std::this_thread {
    inline constexpr sync-wait-env = ...;
    struct sync_wait_t { ... };
    inline constexpr sync_wait_t sync_wait{};
}
```

**影响：** 这是**直接违反无感过渡原则**的命名空间错误。用户代码如果写 `std::execution::sync_wait(sndr)`，在原生标准库中将找不到该符号（它在 `std::this_thread` 中）。反之，写 `std::this_thread::sync_wait(sndr)` 的用户代码在 Forge backport 上无法编译。

**建议修复：**

1. 将 `sync_wait` 定义移至 `namespace std::this_thread`
2. 在 `namespace std::execution` 中保留 `using std::this_thread::sync_wait;` 以保持内部 API 兼容（可选的过渡期措施，但需标注非标准）
3. 所有测试和示例改为 `std::this_thread::sync_wait(...)`

---

### I-5 缺少 `stoppable_token`、`stoppable_token_for`、`unstoppable_token` concepts

- 严重程度：中
- 位置：整个文件缺失
- 相关条文：`[stoptoken.concepts]`（`https://eel.is/c++draft/stoptoken#concepts`）
- 前轮追踪：r1 I-9（暂缓）

**问题描述：**

前轮以"MVP 仅使用 `inplace_stop_token`，功能上不受阻"为由暂缓。但这三个 concepts 是 `[stoptoken.concepts]` 中定义的基础设施：

- `stoppable_token<Token>` — 要求 `stop_requested()`、`stop_possible()`、copy constructible、equality comparable
- `stoppable_token_for<Token, CallbackFn>` — 要求可用 `CallbackFn` 构造 `stop_callback_for_t<Token, CallbackFn>`
- `unstoppable_token<Token>` — `stoppable_token` 且 `stop_possible()` 编译期常量 `false`

这些 concepts 不依赖 `run_loop` 或其他 phase 1 组件，可以独立实现。定义它们后可以：
1. 约束 `get_stop_token` 的返回类型
2. 验证 `inplace_stop_token` 和 `never_stop_token` 满足正确的 concept
3. 为 receiver env 的 stop token 约束提供标准化基础

**建议：** 本轮实现。复杂度低，收益明确。

---

### I-6 `sender` concept 使用直接 `sender_concept` 检查而非 `enable_sender` trait

- 严重程度：中
- 位置：`backport/cpp26/execution.hpp` 第 662-670 行
- 相关条文：`[exec.snd]`（`https://eel.is/c++draft/exec#snd`）

**问题描述：**

当前 `sender` concept 直接检查 `sender_concept` 嵌套类型：

```cpp
requires { typename std::remove_cvref_t<S>::sender_concept; } &&
std::derived_from<typename std::remove_cvref_t<S>::sender_concept, sender_t>
```

当前工作草案 `[exec.snd]` 中 `sender` concept 使用 `enable_sender<remove_cvref_t<Sndr>>` trait。该 trait 默认检查 `sender_concept`，但也支持 awaitable 类型（通过特化 `enable_sender`）。

当前实现的直接检查在非 awaitable 场景下功能等价。但缺少 `enable_sender` trait 意味着：
- 用户无法通过特化 `enable_sender` 为 awaitable 类型启用 sender 语义
- 与标准 API 表面不一致

**建议：** MVP 阶段可继续暂缓 awaitable 支持，但应定义 `enable_sender` trait 并在 `sender` concept 中使用它，为后续扩展预留接口。

---

### I-7 `receiver` concept 中 `constructible_from` 约束冗余

- 严重程度：低
- 位置：`backport/cpp26/execution.hpp` 第 623 行
- 相关条文：`[exec.recv]`

**问题描述：**

当前 `receiver` concept 中：

```cpp
std::move_constructible<std::remove_cvref_t<R>> &&
std::constructible_from<std::remove_cvref_t<R>, std::remove_cvref_t<R>> &&
```

`move_constructible<T>` 的定义已蕴含 `constructible_from<T, T>`（即从右值可构造）。第 623 行的 `constructible_from` 约束完全冗余。

**影响：** 无功能影响，但冗余约束增加概念检查开销，且可能给维护者造成"这是额外要求"的误解。

**建议：** 移除冗余行。如果 I-1 修复为 `is_nothrow_move_constructible_v`（不再使用 `move_constructible` concept），此条自动消解。

---

### I-8 缺少 `get_completion_scheduler` CPO

- 严重程度：低
- 位置：整个文件缺失
- 相关条文：`[exec.getcomplsched]`（`https://eel.is/c++draft/exec#getcomplsched`）
- 前轮追踪：r1 I-12（暂缓）

**问题描述：**

`get_completion_scheduler<CPO>` 是 `[exec.getcomplsched]` 定义的 query CPO，用于从 sender 环境中查询指定完成通道对应的 scheduler。它是 `just-env`、scheduler completion invariant 和 domain-based dispatch 的基础设施。

前轮以"MVP 阶段暂不实现 completion scheduler"为由暂缓。本轮认为该 CPO 的 shell 定义（仅声明，不含 domain dispatch 逻辑）可与 I-5 一起实现，为 `just` sender 的 `get_env` 提供正确的 `just-env` 返回类型。

**建议：** 定义 CPO shell，`just` sender 的 env 返回可查询的 completion scheduler。完整的 domain dispatch 逻辑仍可暂缓至 phase 1。

---

## 第三部分：测试问题

### T-1 缺少新概念约束的编译探针

- 严重程度：中
- 位置：所有测试文件
- 相关条文：与 I-1 至 I-5 对应

**问题描述：**

前轮补充了基本编译探针，但本轮新发现的约束缺口需要对应的正/反例探针：

**I-1 对应（receiver nothrow-move + non-final）：**
- 反例：定义一个 move 会抛异常的 receiver 类型 → `static_assert(!receiver<throwing_move_receiver>)`
- 反例：定义一个 `final` 的 receiver 类型 → `static_assert(!receiver<final_receiver>)`

**I-2 对应（operation_state_concept marker）：**
- 反例：定义一个满足其他所有约束但无 `operation_state_concept` 嵌套类型的类型 → `static_assert(!operation_state<no_marker_op>)`

**I-3 对应（scheduler_concept marker）：**
- 反例：定义一个满足其他所有约束但无 `scheduler_concept` 嵌套类型的类型 → `static_assert(!scheduler<no_marker_sched>)`

**I-5 对应（stoppable_token concepts）：**
- 正例：`static_assert(stoppable_token<inplace_stop_token>)`
- 正例：`static_assert(unstoppable_token<never_stop_token>)`
- 反例：`static_assert(!stoppable_token<int>)`

---

### T-2 缺少 `std::this_thread::sync_wait` 命名空间测试

- 严重程度：低
- 位置：测试文件
- 相关条文：与 I-4 对应

**问题描述：**

I-4 修复后，需要一个测试验证 `std::this_thread::sync_wait` 可直接调用：

```cpp
auto result = std::this_thread::sync_wait(std::execution::just(42));
ASSERT_TRUE(result.has_value());
EXPECT_EQ(std::get<0>(*result), 42);
```

确保命名空间迁移后用户可通过标准路径访问 `sync_wait`。

---

## 第四部分：总结

### 按严重程度汇总

| 严重程度 | 实现问题 | 测试问题 | 合计 |
|----------|----------|----------|------|
| 高       | 4 (I-1, I-2, I-3, I-4) | 0 | 4 |
| 中       | 2 (I-5, I-6) | 1 (T-1) | 3 |
| 低       | 2 (I-7, I-8) | 1 (T-2) | 3 |
| 合计     | 8 | 2 | **10** |

### 与前轮对比

| 维度 | r1 | r2 |
|------|----|----|
| 总问题数 | 21 | 10 |
| 高严重度 | 4 (I-1, I-2, I-6, I-10) | 4 (I-1, I-2, I-3, I-4) |
| 运行时 bug | 1 (request_stop 死锁) | 0 |
| 概念定义偏差 | 5 | 6 |
| 命名空间错误 | 0 | 1 |

趋势：运行时 bug 清零，问题集中在概念定义精度和命名空间对齐，整体向标准库质量靠近。

### 前轮暂缓项累计跟踪

| r1 编号 | 描述 | r2 状态 |
|---------|------|---------|
| I-1 | tag_invoke 架构偏离 | 继续暂缓 (phase 1) |
| I-9 | stoppable_token concepts | 本轮 I-5 要求实现 |
| I-10 | sync_wait run_loop | 继续暂缓 (phase 1) |
| I-12 | just env completion scheduler | 本轮 I-8 部分覆盖 |
| I-13 | sender_adaptor_closure | 继续暂缓 (phase 1) |

### 优先修复顺序

1. **I-1** — receiver `nothrow_move` + `non-final`（用户可见的概念语义，直接影响无感过渡）
2. **I-2** — `operation_state_concept` marker（所有内部 op 类型需同步更新）
3. **I-3** — `scheduler_concept` marker（`inline_scheduler` 需同步更新）
4. **I-4** — `sync_wait` 命名空间迁移至 `std::this_thread`（无感过渡违规）
5. **I-5** — `stoppable_token` concepts（前轮暂缓项，现可独立实现）
6. **T-1** — 编译探针补充（锁定 I-1 至 I-5 修复结果）
7. **I-6** — `enable_sender` trait（功能等价，可暂缓但应定义 trait shell）
8. **I-7** — 冗余约束移除（随 I-1 修复自然消解）
9. **I-8** — `get_completion_scheduler` CPO shell
10. **T-2** — `std::this_thread::sync_wait` 命名空间测试

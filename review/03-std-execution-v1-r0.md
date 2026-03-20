# `std::execution` (P2300) Backport 第二轮审查报告 — v1-r0

审查对象：
- Wrapper 注入：`backport/execution`
- 实现文件：`backport/cpp26/execution.hpp`
- 测试文件：`test/test_execution_mvp.cpp`、`test/test_execution_inline_scheduler.cpp`、`test/test_execution_stop_token.cpp`

参考规范：P2300R10 (`std::execution` — C++26 senders/receivers)

日期：2026-03-20

---

## 第零部分：前轮修复验证

本轮首先对 v0-r0-a0 答复文件中声明"已修复"的条目进行代码复核，确认修复均已落地。

| 原始条目 | 答复结论 | 代码复核结果 |
|----------|----------|-------------|
| I-1 命名空间迁移到 `std::execution` | 接受，已修复 | 确认：全部符号在 `namespace std` 或 `namespace std::execution` |
| I-3 `sender` 检查 `sender_concept` 嵌套类型 | 接受，已修复 | 确认：第 616-621 行 `concept sender` 要求 `sender_concept` 且派生自 `sender_t` |
| I-4 `receiver` 检查 `receiver_concept` 嵌套类型 | 接受，已修复 | 确认：第 576-582 行 `concept receiver` 已要求 `receiver_concept` 派生自 `receiver_t` |
| I-5 `receiver_of` 含 `nothrow_tag_invocable` | 接受，已修复 | 确认：第 586-613 行 `receiver_accepts` 特化均要求 `nothrow_tag_invocable` |
| I-6 `inplace_stop_source` mutex 保护 | 接受，已修复 | 确认：第 72-214 行完整实现，`std::mutex` 保护链表 |
| I-7 `sync_wait` 从 `completion_signatures` 推导类型 | 接受，已修复 | 确认：第 974-1104 行从 `get_completion_signatures` 推导值元组类型 |
| I-9 元编程工具 `value_types_of`/`error_types_of`/`sends_stopped_v` | 接受，已修复 | 确认：第 416-553 行完整实现 |
| I-10 `then` sender 签名变换 | 接受，已修复 | 确认：第 782-883 行变换 `set_value_t(Vs...)` 并合并 `set_error_t(exception_ptr)` |
| I-11 `then` 的 pipe `operator\|` | 接受，已修复 | 确认：第 968-974 行 `then_closure` 的 `friend operator\|` 位于 `namespace std::execution` |
| I-12 `inline_scheduler` 携带 scheduler env | 接受，已修复 | 确认：第 1110-1183 行 sender 持有 `inline_scheduler*`，`get_env` 返回含 scheduler 的 env |
| I-13 `set_value/set_error/set_stopped` noexcept + static_assert | 接受，已修复 | 确认：第 371-395 行 CPO 标记 `noexcept`，含 `static_assert(nothrow_tag_invocable<...>)` |
| I-14 移除 Clang 限制，保留 C++20 guard | 接受，已修复 | 确认：第 29-31 行仅保留 `__cplusplus < 202002L` guard |
| I-18 移除 `EXEC_*` 宏污染 | 接受，已修复 | 确认：整个 `backport/cpp26/execution.hpp` 中无 `EXEC_*` 宏定义 |
| I-19 noexcept 合约通过 static_assert 强制 | 驳斥合理 | 确认：CPO 层已强制 noexcept，驳斥理由成立 |
| T-1 测试迁移至 gtest | 接受，已修复 | 确认：三个测试文件均使用 `TEST(...)` 宏，链接 gtest |
| T-7 stop_token 可链接 | 接受，已修复 | 确认：`test_execution_stop_token.cpp` 覆盖 `request_stop` 与 callback 注册 |

**附注 — T-5 描述核查：** v0-r0-a0 答复声称 `test_execution_inline_scheduler.cpp` 覆盖了"scheduler concept 静态断言"。代码复核确认第 9 行确实存在：

```cpp
static_assert(std::execution::scheduler<std::execution::inline_scheduler>);
```

答复描述与实际代码一致，无偏差。

前轮 30 条问题（I-1~I-19、T-1~T-11）中，以"接受/已修复"结论落地的均通过代码复核，无回归。

---

## 第一部分：实现问题

### I-1 `backport/execution` wrapper 的 `#else` 分支直接 `#error`，缺少优雅兜底

- 严重程度：中
- 位置：`backport/execution` 第 35-41 行

**问题描述：** wrapper 文件当前的结构为：

```cpp
#if defined(_MSC_VER)
    #ifndef FORGE_MSVC_EXECUTION_HEADER
    #error "FORGE_MSVC_EXECUTION_HEADER is required ..."
    #endif
    #include FORGE_MSVC_EXECUTION_HEADER
#elif defined(__has_include_next)
    #if __has_include_next(<execution>)
    #include_next <execution>
    #endif
#else
    #error "Forge backport requires a compiler-specific <execution> include strategy"
#endif
```

在 GCC/Clang 上，`__has_include_next` 通常可用，`#if __has_include_next(<execution>)` 能在标准库不提供 `<execution>` 时跳过包含，优雅降级。但当使用 NVCC、ICC 或某些嵌入式工具链（不属于 MSVC 且不支持 `__has_include_next`）时，会直接触发 `#error`，导致整个 `#include <execution>` 失败，无法使用任何 P2300 MVP 功能。

**规范/设计要求：** 项目设计文档（`CLAUDE.md`）中 wrapper 机制说明：backport 注入不应在运行期以外的阶段破坏构建。当工具链无法回退到 `#include_next`，但也不支持该宏时，合理做法是静默跳过标准 `<execution>` 的包含，仅提供 P2300 MVP 层。

**建议修复：** 移除 `#else #error` 分支，改为注释说明：

```cpp
#elif defined(__has_include_next)
#  if __has_include_next(<execution>)
#    include_next <execution>
#  endif
// else: toolchain has neither MSVC nor __has_include_next — skip the standard
//       <execution> header silently; only the P2300 MVP surface below will be
//       available (C++17 execution policies will not be present).
#endif
```

此修改单行，风险极低，能显著改善在非 GCC/Clang/MSVC 工具链上的兼容性。

---

### I-2 `just`/`just_error`/`just_stopped` sender 的 `get_env` 返回 `empty_env`，缺少完成调度器信息

- 严重程度：低
- 位置：`backport/cpp26/execution.hpp` 第 700、739、772 行

**问题描述：** 三类工厂 sender 的 `tag_invoke(get_env_t, ...)` 均返回 `empty_env`：

```cpp
// just_sender（第 700 行）
friend auto tag_invoke(get_env_t, const sender&) noexcept -> empty_env { return {}; }
```

P2300R10 [exec.just] 规定，`just`/`just_error`/`just_stopped` 的 sender env 应暴露完成调度器信息，具体为 `get_completion_scheduler<set_value_t>(env)` 等查询。虽然当前 MVP 阶段不实现 `continues_on` 等算法，但若用户将 `just` 与依赖完成调度器信息的算法组合，将静默获得"无调度器"的 `empty_env`，而不是编译错误，掩盖了缺失的语义。

**规范要求：** P2300R10 [exec.just] 中 `just` 的 sender env 应通过 exposition-only 的 `just-env` 类型提供 `get_completion_scheduler<CPO>` 查询。

**建议修复：** 此问题在 MVP 阶段可记录为 TODO，应在实现 `continues_on`/`on` 等算法前补齐。当前可在三处 `empty_env` 返回点旁各加注释：

```cpp
// TODO(P2300R10 exec.just): expose completion scheduler in env
friend auto tag_invoke(get_env_t, const sender&) noexcept -> empty_env { return {}; }
```

---

### I-3 `then_closure::operator|` 的 `friend` 函数机制与 P2300R10 sender adaptor closure 对象不符

- 严重程度：中
- 位置：`backport/cpp26/execution.hpp` 第 966-975 行

**问题描述：** 当前 `then` 的 pipe 支持通过 `then_closure` 的两个 `friend operator|` 实现：

```cpp
template<std::execution::sender S>
    requires std::copy_constructible<Fn>
friend constexpr auto operator|(S&& s, const then_closure& self) {
    return self(std::forward<S>(s));
}

template<std::execution::sender S>
friend constexpr auto operator|(S&& s, then_closure&& self) {
    return std::move(self)(std::forward<S>(s));
}
```

这两个 `friend` 函数通过 ADL 在 `namespace std::execution` 中被找到，参数约束为 `std::execution::sender`，限制了 `S` 的范围。但该机制与 P2300R10 最终采纳的"sender adaptor closure 对象"（`sender_adaptor_closure`）存在结构性差异：

- **P2300R10 的方式**：sender adaptor closure 通过继承 `sender_adaptor_closure<D>` 基类获得标准化的 `operator|` 语义，是对象本身的能力；
- **当前方式**：`friend operator|` 注入到关联命名空间，是自由函数重载，每个 sender adaptor 均需重复实现此模式，且未来扩展新 adaptor（如 `let_value`、`bulk`）时需逐个复制。

此外，当用户在某些特殊场景定义了兼容 `std::execution::sender` 约束的全局 `operator|` 重载时，可能与当前 `friend` 函数产生重载歧义，错误信息不直观。

**规范要求：** P2300R10 [exec.adapt.obj] 规定 sender adaptor closure 对象应实现为可调用对象，其 `operator|` 应通过基类 `sender_adaptor_closure<D>` 提供。

**建议修复：** 引入 `sender_adaptor_closure` CRTP 基类，将 `operator|` 逻辑移入基类成员，消除逐 adaptor 重复模式。在 MVP 阶段可保持现有实现，但应在代码注释中标明偏差，并在实现后续 adaptor 前统一切换：

```cpp
// TODO(P2300R10 exec.adapt.obj): replace friend operator| pattern with
//   sender_adaptor_closure<then_closure<Fn>> CRTP base when adding more adaptors
```

---

## 第二部分：测试问题

### T-1 缺少 scheduler env 查询的运行时验证

- 严重程度：低
- 涉及文件：`test/test_execution_inline_scheduler.cpp`

**问题描述：** `test_execution_inline_scheduler.cpp` 包含两个测试（`ScheduleRunsInline`、`ScheduleThenChain`）和一个编译期断言。I-12（已修复）为 `inline_scheduler` 的 sender 增加了携带 scheduler 指针的 env（`backport/cpp26/execution.hpp` 第 1181-1183 行），使得 `get_scheduler(get_env(sender))` 可返回原始 scheduler。但目前没有任何测试验证这一新增行为：

```cpp
// 缺少此类验证：
auto snd = std::execution::schedule(sch);
auto env = std::execution::get_env(snd);
auto recovered = std::execution::get_scheduler(env);
EXPECT_EQ(recovered, sch);
```

**建议修复：** 在 `test_execution_inline_scheduler.cpp` 中增加 `SchedulerEnvRoundtrip` 测试，验证从 sender env 中查询回的 scheduler 与原始 scheduler 相等。

---

### T-2 缺少 sender 类型系统的编译期静态断言

- 严重程度：低
- 涉及文件：`test/test_execution_mvp.cpp`

**问题描述：** `test_execution_mvp.cpp` 中的测试均为运行时断言，缺少对 sender 类型系统的编译期验证。例如：

```cpp
// 缺少此类验证：
using just_snd_t = decltype(std::execution::just(42));
static_assert(std::execution::sender<just_snd_t>);
static_assert(std::execution::sender_in<just_snd_t, std::execution::empty_env>);
using value_t = std::execution::value_types_of<just_snd_t,
                    std::execution::empty_env>::type;
static_assert(std::is_same_v<value_t, std::variant<std::tuple<int>>>);
```

这类静态断言能够在实现回归时于编译期捕获类型系统错误，而不仅依赖运行时断言。`value_types_of`/`error_types_of`/`sends_stopped_v` 已在第 538-553 行实现，但测试中未对其结果做 `static_assert` 验证。

**建议修复：** 在 `test_execution_mvp.cpp` 文件作用域增加对 `just`、`just_error`、`just_stopped` 返回类型的 `value_types_of`/`error_types_of`/`sends_stopped_v` 静态断言。

---

### T-3 缺少 `then` void 返回和多值输入的签名变换测试

- 严重程度：低
- 涉及文件：`test/test_execution_mvp.cpp`

**问题描述：** `test_execution_mvp.cpp` 的 `ThenTransformsValue` 测试覆盖了单值 `then` 的运行时行为，但 `then` 的签名变换逻辑（第 898-950 行）还处理了以下未经测试的情形：

1. **void 返回的 `then`**：`then([]{})` 应将 `set_value_t(Ts...)` 变换为 `set_value_t()`，目前无测试；
2. **多值 `just` 与 `then` 的签名推导**：`just(1, 2) | then([](int a, int b){ return a + b; })` 验证多参数情形；
3. **`then` 内抛异常时签名中含 `set_error_t(exception_ptr)` 的编译期验证**：运行时路径已有 `ThenWithExceptionPropagation` 覆盖，但缺少 `static_assert` 验证签名中确实包含该错误类型。

**建议修复：** 为上述三种情形各增加一个测试，其中情形（3）可结合现有运行时测试与 `static_assert` 对签名类型的断言。

---

## 第三部分：总结

### 按条目汇总

| 条目 | 类别 | 严重程度 | 建议优先级 |
|------|------|----------|-----------|
| I-1 `backport/execution` `#else #error` 缺少优雅兜底 | 实现 | 中 | 高 |
| I-2 `just` 系列 sender env 缺少完成调度器信息 | 实现 | 低 | 低（MVP TODO） |
| I-3 `then_closure::operator\|` 与 P2300R10 adaptor closure 机制不符 | 实现 | 中 | 中 |
| T-1 缺少 scheduler env 查询的运行时验证 | 测试 | 低 | 中 |
| T-2 缺少 sender 类型系统的编译期静态断言 | 测试 | 低 | 中 |
| T-3 缺少 `then` void/多值/签名变换测试 | 测试 | 低 | 低 |

### 优先修复建议

1. **最高优先级 — I-1：** 移除 `backport/execution` 中的 `#else #error` 分支，改为静默跳过。此修改涉及单行，风险极低，但能显著改善在非 GCC/Clang/MSVC 工具链上的兼容性。

2. **中优先级 — I-3：** 当前 `friend operator|` 在功能上正确，但与 P2300R10 最终机制存在结构性偏差。建议在引入后续 sender adaptor（如 `let_value`、`bulk`）时统一切换为 `sender_adaptor_closure` CRTP 基类，避免逐个 adaptor 重复实现此模式。

3. **中优先级 — T-1、T-2：** 补充 scheduler env roundtrip 测试和 sender 类型系统静态断言，属于低成本高收益的测试补充，可在下一次迭代中一并完成。

4. **低优先级 — I-2、T-3：** `just` sender env 的完成调度器信息和 `then` 多值/void 签名变换测试，建议在实现 `continues_on`/`on` 等算法时作为前置验证一并补齐。

### 整体评估

相较于 v0-r0 的 30 个问题，本轮仅发现 6 个新问题，全部为中/低严重程度，无高严重程度问题。v0-r0 的核心修复（命名空间、concept 定义、类型变换、stop token 实现）质量良好，前轮修复无回归。当前实现的 MVP 范围（`just`、`then`、`sync_wait`、`inline_scheduler`、stop token）在功能上正确，可支撑后续算法层的扩展。


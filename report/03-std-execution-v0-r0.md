# `std::execution` 参考实现审查报告 (v0-r0)

审查对象：
- 设计文档：`reference/execution/stdexec-design-plan.md`
- 实现文件：`reference/execution/include/exec/` 下全部头文件、`reference/execution/src/contexts/` 下源文件
- 测试文件：`reference/execution/tests/` 下全部测试
- 构建系统：`reference/execution/build.zig`、`reference/execution/build.zig.zon`

参考规范：P2300R10 (C++26 `std::execution`)

---

## 第一部分：实现问题

### I-1 命名空间使用 `exec` 而非 `std::execution`

- 严重程度：高
- 位置：所有实现文件（`tag_invoke.hpp`、`concepts.hpp`、`env.hpp`、`completion_signatures.hpp`、`stop_token.hpp`、`just.hpp`、`then.hpp`、`sync_wait.hpp`、`inline_scheduler.hpp`）

**问题描述：** 所有符号定义在 `namespace exec` 中，而非 P2300R10 规定的 `namespace std::execution`。作为 backport 库，下游用户代码使用 `std::execution::just(...)` 等标准写法将无法编译。

**规范要求：** P2300R10 将所有 sender/receiver 相关 API 定义在 `namespace std::execution` 中（如 `std::execution::just`、`std::execution::then`、`std::execution::sync_wait`），stop token 相关类型定义在 `namespace std` 中（如 `std::inplace_stop_source`）。

**当前实现：** 例如 `just.hpp` 中：
```cpp
namespace exec {
// ...
template<typename... Vs>
[[nodiscard]] auto just(Vs&&... vs) { ... }
} // namespace exec
```
用户需要写 `exec::just(42)` 而非标准的 `std::execution::just(42)`，违反了 Forge 项目"无感过渡"的核心设计原则。

---

### I-2 使用 `tag_invoke` 而非 P2300R10 最终采纳的定制机制

- 严重程度：高
- 位置：`tag_invoke.hpp` 全文、`concepts.hpp`、`env.hpp`、`completion_signatures.hpp`、`just.hpp`、`then.hpp`、`inline_scheduler.hpp`

**问题描述：** 实现基于 `tag_invoke` 协议构建所有 CPO（customization point object），这是 P2300 早期修订版（R0-R7）的设计。P2300R10（最终并入 C++26 IS 的版本）已移除 `tag_invoke`，改为基于 `tag_of_t` + sender/receiver concept 中的 `sender_concept`/`receiver_concept` 嵌套类型 + 结构化 `connect`/`start` 等 CPO 的直接定制。

**规范要求：** P2300R10 中，sender 通过嵌套 `using sender_concept = sender_t;` 标识自身，`connect` 通过成员函数或 `tag_of_t` 分发，不再依赖 `tag_invoke`。

**当前实现：** 所有 sender/receiver 类型通过 `friend void tag_invoke(set_value_t, ...)` 等 friend 函数定制行为。整个 `tag_invoke.hpp`（69 行）是一个在最终标准中不存在的基础设施。这意味着当标准库原生提供 `<execution>` 时，所有基于 `tag_invoke` 的用户定制代码都需要重写。

---

### I-3 `sender` concept 定义不符合 P2300R10

- 严重程度：高
- 位置：`concepts.hpp` 第 61-67 行

**问题描述：** `sender` concept 仅检查 `move_constructible` 和 `constructible_from`，缺少对 `sender_concept` 嵌套类型的检查。

**规范要求：** P2300R10 中 `sender` concept 要求类型提供 `typename remove_cvref_t<S>::sender_concept`，且该类型派生自 `sender_t`。此外还需要 `get_env` 可调用。

**当前实现：**
```cpp
template<typename S>
concept sender =
    std::move_constructible<std::remove_cvref_t<S>> &&
    std::constructible_from<std::remove_cvref_t<S>,
                            std::remove_cvref_t<S>>;
```
虽然各 sender 实现中确实定义了 `using sender_concept = sender_t;`，但 concept 本身并未检查该嵌套类型，导致任何可移动构造的类型都能满足 `sender` concept。

---

### I-4 `receiver` concept 使用标记类型而非嵌套类型检查

- 严重程度：中
- 位置：`concepts.hpp` 第 41-51 行

**问题描述：** 定义了 `receiver_t` 标记类型，但 `receiver` concept 并未检查 `receiver_concept` 嵌套类型，仅检查 `move_constructible` 和 `get_env` 可调用性。

**规范要求：** P2300R10 中 `receiver` concept 要求 `typename remove_cvref_t<R>::receiver_concept` 存在且派生自 `receiver_t`。

**当前实现：**
```cpp
template<typename R>
concept receiver =
    std::move_constructible<std::remove_cvref_t<R>> &&
    std::constructible_from<std::remove_cvref_t<R>,
                            std::remove_cvref_t<R>> &&
    requires(const std::remove_cvref_t<R>& r) {
        { exec::get_env(r) };
    };
```
由于 `get_env` 有 fallback（返回 `empty_env`），实际上任何可移动构造的类型都满足 `receiver` concept。

---

### I-5 `receiver_of` concept 未实现

- 严重程度：中
- 位置：`concepts.hpp` 第 54-55 行

**问题描述：** `receiver_of<R, Completions>` 被定义为 `receiver<R>` 的简单别名，附带 `// full check requires expansion (TODO)` 注释，未实际检查 receiver 是否能接受给定的 completion signatures。

**规范要求：** P2300R10 中 `receiver_of<R, Completions>` 需要验证 R 对 Completions 中每个签名都提供了对应的完成操作（`set_value`、`set_error`、`set_stopped`）。

**当前实现：**
```cpp
template<typename R, typename Completions>
concept receiver_of = receiver<R>;  // full check requires expansion (TODO)
```

---

### I-6 `inplace_stop_source` 关键成员函数缺少定义

- 严重程度：高
- 位置：`stop_token.hpp` 第 38、45、55、56 行

**问题描述：** `inplace_stop_source` 的析构函数 `~inplace_stop_source()`、`request_stop()`、`try_add_callback()` 和 `remove_callback()` 仅有声明，整个代码库中无任何定义。这意味着任何使用 stop token 的代码都会产生链接错误。

**规范要求：** 作为 header-only 库（或至少在某个编译单元中），这些函数必须有完整实现。`request_stop()` 是 stop token 机制的核心，负责原子地设置停止标志并调用所有已注册的回调。

**当前实现：** 仅在头文件中声明：
```cpp
~inplace_stop_source() noexcept;
bool request_stop() noexcept;
bool try_add_callback(_stop_impl::callback_base* cb) noexcept;
void remove_callback(_stop_impl::callback_base* cb) noexcept;
```
无对应的 `.cpp` 文件或 inline 定义。`thread_pool.cpp` 和 `io_uring_scheduler.cpp` 均为空壳，不包含这些定义。

---

### I-7 `sync_wait` 硬编码空值类型，无法处理带值 sender

- 严重程度：高
- 位置：`sync_wait.hpp` 第 112 行

**问题描述：** `sync_wait` 将 `shared_state` 硬编码为 `shared_state<>`（空参数包），无法正确接收带值的 sender 完成结果。注释中标注 `// TODO: derive Vs from S`。

**规范要求：** P2300R10 中 `sync_wait` 应从 sender 的 `completion_signatures` 推导出值类型 `Vs...`，返回 `optional<tuple<Vs...>>`。

**当前实现：**
```cpp
using state_t = _sync_wait::shared_state<>;  // TODO: derive Vs from S
```
这意味着 `sync_wait(just(42))` 实际上将 42 存入 `tuple<>` 而非 `tuple<int>`，返回类型为 `optional<tuple<>>` 而非 `optional<tuple<int>>`。测试中 `std::get<0>(*result)` 的行为在此实现下是未定义的。

---

### I-8 `sync_wait` 未使用 `run_loop` 驱动工作

- 严重程度：中
- 位置：`sync_wait.hpp` 第 24 行注释、第 107-131 行实现

**问题描述：** 注释声称"Internally drives a run_loop to pump work on the calling thread"，但实际实现使用 `mutex` + `condition_variable` 阻塞等待，未实现 `run_loop`。

**规范要求：** P2300R10 中 `sync_wait` 应使用 `run_loop` 作为执行上下文，在调用线程上驱动工作。`run_loop` 是标准要求的组件，`sync_wait` 的 receiver 环境中应通过 `get_scheduler` 返回 `run_loop` 的 scheduler，使得 `continues_on` 等算法能将工作调度回调用线程。

**当前实现：** 简单的 mutex/cv 等待模式，无法支持需要在调用线程上执行后续工作的 sender 链。`run_loop` 未在任何文件中实现。

---

### I-9 `completion_signatures` 元编程工具不完整

- 严重程度：中
- 位置：`completion_signatures.hpp` 第 75-137 行

**问题描述：** `value_types_of`、`error_types_of`、`sends_stopped_v` 仅有前向声明/默认值，无特化实现。`_meta::value_types_impl` 的递归实现丢弃了尾部签名（第 97 行 `using type = Tuple<Vs...>; // placeholder`）。

**规范要求：** P2300R10 中这些元函数是 sender 类型系统的核心，用于在编译期推导 sender 的完成类型。`then` 算法需要用它们来变换 completion signatures，`sync_wait` 需要用它们来推导返回类型。

**当前实现：** `then` 的 `get_completion_signatures` 直接转发上游签名而非变换（第 87 行注释 `// Simplified: forward all upstream signatures`），`sync_wait` 完全跳过类型推导。

---

### I-10 `then` 的 `get_completion_signatures` 未变换签名

- 严重程度：中
- 位置：`then.hpp` 第 82-89 行

**问题描述：** `then` sender 的 `get_completion_signatures` 直接返回上游 sender 的签名，未将 `set_value_t(Vs...)` 变换为 `set_value_t(invoke_result_t<Fn, Vs...>)`。

**规范要求：** P2300R10 中 `then` 必须正确变换 completion signatures：将上游的 `set_value_t(Vs...)` 替换为 `set_value_t(invoke_result_t<Fn, Vs...>)`（若 Fn 返回 void 则为 `set_value_t()`），并添加 `set_error_t(exception_ptr)` 签名。

**当前实现：**
```cpp
friend auto tag_invoke(get_completion_signatures_t,
                       const sender&, auto env)
{
    // Simplified: forward all upstream signatures
    return exec::get_completion_signatures(sndr_, env);
}
```

---

### I-11 `then` 的 pipe operator 模板参数推导有误

- 严重程度：中
- 位置：`then.hpp` 第 132-135 行

**问题描述：** 全局 `operator|` 的第二个参数类型为 `decltype(then(std::declval<Fn>()))` ，其中 `Fn` 是未推导的模板参数，该函数模板无法正确参与重载决议。

**规范要求：** P2300R10 中 pipe operator 通过 sender adaptor closure 机制实现，adaptor closure 对象应满足特定的 `operator|` 语义。

**当前实现：**
```cpp
template<exec::sender S, typename Fn>
[[nodiscard]] auto operator|(S&& s, decltype(then(std::declval<Fn>())) adaptor) {
    return adaptor(std::forward<S>(s));
}
```
`Fn` 出现在非推导上下文中，编译器无法从实参推导 `Fn`，该 operator 实际上不可用。测试中 `exec::just(10) | exec::then([](int x) { return x + 5; })` 能工作是因为 `then_t::operator()` 返回的 lambda 自身可能通过其他机制（如 lambda 的 `operator()` 调用）完成管道操作，而非通过此全局 `operator|`。

---

### I-12 `inline_scheduler` 的 sender 未携带 scheduler 信息

- 严重程度：低
- 位置：`inline_scheduler.hpp` 第 37-54 行

**问题描述：** `_inline::sender` 的 `get_env` 返回 `empty_env`，未通过环境提供 `get_completion_scheduler<set_value_t>` 查询。

**规范要求：** P2300R10 中 `schedule` 返回的 sender 应通过环境提供 `get_completion_scheduler<set_value_t>` 查询，返回对应的 scheduler。这是 `continues_on`/`start_on` 等算法正确工作的前提。

**当前实现：** 虽然定义了 `_inline::env` 结构体（第 21-26 行）并实现了 `get_scheduler` 查询，但 sender 的 `get_env` 返回的是 `empty_env` 而非 `_inline::env`。

---

### I-13 `set_value`/`set_error`/`set_stopped` 未强制 noexcept

- 严重程度：低
- 位置：`completion_signatures.hpp` 第 13-47 行

**问题描述：** `set_value_t::operator()` 的 noexcept 规范依赖于 `tag_invoke` 的 noexcept 性，而非无条件 noexcept。`set_error_t` 和 `set_stopped_t` 同理。

**规范要求：** P2300R10 中 `set_value`、`set_error`、`set_stopped` 的调用表达式必须是 noexcept 的。这是 receiver contract 的一部分。

**当前实现：** CPO 的 noexcept 取决于具体 `tag_invoke` 重载是否为 noexcept，未在 concept 层面强制要求。

---

### I-14 `config.hpp` 硬编码 Clang >= 18 限制

- 严重程度：低
- 位置：`config.hpp` 第 4-6 行

**问题描述：** 编译器检查硬编码为 `__clang_major__ < 18`，排除了 GCC 和 MSVC。

**规范要求：** 作为 Forge 项目的 backport，应支持主项目支持的所有编译器。Forge 的 `forge.cmake` 已有跨编译器检测机制。

**当前实现：**
```cpp
#if !defined(__clang__) || __clang_major__ < 18
#  error "exec requires Clang >= 18 (zig c++ with LLVM 18+)"
#endif
```

---

### I-15 构建系统使用 Zig 而非 CMake

- 严重程度：中
- 位置：`build.zig`、`build.zig.zon`

**问题描述：** 参考实现使用 Zig 构建系统，而 Forge 主项目使用 CMake。若要集成为 backport，需要完全重写构建配置以适配 `forge.cmake` 的注入机制。

**规范要求：** Forge 项目通过 `forge.cmake` 实现 backport 注入，消费者通过 `include(forge.cmake)` 或 `add_subdirectory(forge)` 集成。Zig 构建系统与此机制不兼容。

**当前实现：** `build.zig` 定义了静态库、示例、测试和 ASan 构建目标，但无 `CMakeLists.txt`。

---

### I-16 非 header-only 架构：`thread_pool.cpp` 和 `io_uring_scheduler.cpp` 为编译单元

- 严重程度：低
- 位置：`src/contexts/thread_pool.cpp`、`src/contexts/io_uring_scheduler.cpp`

**问题描述：** 设计将 `thread_pool` 和 `io_uring_scheduler` 放在独立编译单元中，构建为静态库。Forge 项目定位为 header-only 库。

**规范要求：** Forge 项目架构为 header-only，通过 include path 注入实现透明 backport。引入编译单元需要额外的链接步骤，与现有集成模式不一致。

**当前实现：** `build.zig` 将这两个 `.cpp` 文件编译为 `libexec` 静态库。虽然目前两个文件都是空壳（placeholder），但架构设计已确定为非 header-only。

---

### I-17 设计文档规划的大量组件未实现

- 严重程度：低（信息性）
- 位置：`stdexec-design-plan.md` 第 52-65 行 vs 实际实现

**问题描述：** 设计文档规划了 MVP 范围内的大量组件，但当前仅实现了一小部分。

| 规划组件 | 实现状态 |
|----------|----------|
| `just` / `just_error` / `just_stopped` | ✅ 已实现 |
| `then` | ✅ 已实现（部分） |
| `sync_wait` | ✅ 已实现（部分） |
| `inline_scheduler` | ✅ 已实现 |
| `inplace_stop_token` | ⚠️ 声明完整，定义缺失 |
| `upon_*` | ❌ 未实现 |
| `let_value` / `let_error` | ❌ 未实现 |
| `when_all` | ❌ 未实现 |
| `split` | ❌ 未实现 |
| `ensure_started` | ❌ 未实现 |
| `bulk` | ❌ 未实现 |
| `start_on` / `continues_on` | ❌ 未实现 |
| `run_loop` | ❌ 未实现 |
| `static_thread_pool` | ❌ 空壳 |
| `io_uring_scheduler` | ❌ 空壳 |

---

### I-10 `then` 算法的 `completion_signatures` 转换未实现

- 严重程度：中
- 位置：`then.hpp` 第 82-89 行

**问题描述：** `then` 的 sender 的 `get_completion_signatures` 直接转发上游 sender 的签名，未将 `set_value_t(Vs...)` 转换为 `set_value_t(invoke_result_t<Fn, Vs...>)`。注释标注 `// Simplified: forward all upstream signatures`。

**规范要求：** P2300R10 中 `then` 必须正确转换 value completion signatures，将 `Fn` 的返回类型反映在输出签名中。此外，如果 `Fn` 可能抛出异常，还应添加 `set_error_t(exception_ptr)` 到签名中。

**当前实现：**
```cpp
friend auto tag_invoke(get_completion_signatures_t,
                       const sender&, auto env)
{
    // Simplified: forward all upstream signatures
    return exec::get_completion_signatures(sndr_, env);
}
```
这导致下游无法正确推导 `then` 输出的值类型。

---

### I-11 `then` 的 pipe operator 模板参数推导有误

- 严重程度：中
- 位置：`then.hpp` 第 132-135 行

**问题描述：** 全局 `operator|` 的第二个参数类型为 `decltype(then(std::declval<Fn>()))` ，其中 `Fn` 是未推导的模板参数，导致该 operator 实际上无法被正常匹配。

**规范要求：** P2300R10 中 pipe operator 通过 sender adaptor closure 机制实现，adaptor closure 是一个满足特定 concept 的可调用对象，`operator|` 对左侧 sender 和右侧 adaptor closure 进行匹配。

**当前实现：**
```cpp
template<exec::sender S, typename Fn>
[[nodiscard]] auto operator|(S&& s, decltype(then(std::declval<Fn>())) adaptor) {
    return adaptor(std::forward<S>(s));
}
```
`Fn` 出现在 `decltype` 的非推导上下文中，编译器无法推导 `Fn`。测试中 `exec::just(10) | exec::then([](int x) { return x + 5; })` 能工作，可能是因为 `_then::then_t::operator()` 返回的 lambda 直接被调用，而非通过此 `operator|`。

---

### I-12 `inline_scheduler` 的 sender 未携带 scheduler 信息

- 严重程度：低
- 位置：`inline_scheduler.hpp` 第 52-53 行

**问题描述：** `_inline::sender` 的 `get_env` 返回 `empty_env`，未通过 `get_completion_scheduler<set_value_t>` 查询返回关联的 `inline_scheduler`。

**规范要求：** P2300R10 中 `schedule` 返回的 sender 的环境应支持 `get_completion_scheduler<set_value_t>` 查询，返回产生该 sender 的 scheduler。这对于 domain-based 定制和算法优化至关重要。

**当前实现：**
```cpp
friend auto tag_invoke(get_env_t, const sender&) noexcept
    -> empty_env { return {}; }
```

---

### I-13 `inline_scheduler::_inline::env` 存储裸指针且未被使用

- 严重程度：低
- 位置：`inline_scheduler.hpp` 第 21-26 行、第 74-80 行

**问题描述：** `_inline::env` 结构体存储 `inline_scheduler*` 裸指针并定义了 `tag_invoke(get_scheduler_t, ...)`，但该 env 从未被任何 sender 或 operation 使用。`_inline::sender::get_env` 返回 `empty_env` 而非此 env。

**规范要求：** 如果定义了 env 类型，应在 sender 的 `get_env` 中返回它，否则属于死代码。

---

### I-14 `get_env` CPO 的 fallback 过于宽松

- 严重程度：低
- 位置：`env.hpp` 第 30-32 行

**问题描述：** `get_env_t` 提供了一个接受 `const auto&` 的 fallback 重载，对任何类型都返回 `empty_env`。这导致 `receiver` concept 中的 `get_env(r)` 约束形同虚设。

**规范要求：** P2300R10 中 `get_env` 对于不提供环境的类型不应有 fallback。receiver 必须显式提供 `get_env` 定制。

**当前实现：**
```cpp
// Fallback: any type that doesn't provide an env returns empty_env
auto operator()(const auto&) const noexcept -> empty_env {
    return {};
}
```

---

### I-15 设计文档规划的大量组件未实现

- 严重程度：中
- 位置：`stdexec-design-plan.md` 第 50-65 行 vs 实际实现

**问题描述：** 设计文档规划了 MVP 范围内的大量组件，但实际仅实现了极小子集。

| 规划组件 | 实现状态 |
|----------|----------|
| `just` / `just_error` / `just_stopped` | 已实现 |
| `then` | 已实现（不完整） |
| `sync_wait` | 已实现（不完整） |
| `inline_scheduler` | 已实现 |
| `stop_token` | 部分实现（缺函数定义） |
| `upon_*` | 未实现 |
| `let_value` / `let_error` | 未实现 |
| `when_all` | 未实现 |
| `split` | 未实现 |
| `ensure_started` | 未实现 |
| `start_on` / `continues_on` | 未实现 |
| `bulk` | 未实现 |
| `run_loop` | 未实现 |
| `static_thread_pool` | 空壳 |
| `io_uring_scheduler` | 空壳 |

---

### I-16 构建系统使用 Zig 而非 CMake，与主项目不一致

- 严重程度：中
- 位置：`build.zig`、`build.zig.zon`

**问题描述：** 参考实现使用 Zig 构建系统，而 Forge 主项目使用 CMake（`forge.cmake` 是核心注入机制）。如果要将此实现集成为 Forge 的 backport，需要完全重写构建系统。

**规范要求：** Forge 项目通过 `forge.cmake` 的 `check_cxx_source_compiles()` 检测原生支持并通过 `target_include_directories(BEFORE)` 注入 backport。Zig 构建系统无法与此机制集成。

---

### I-17 `config.hpp` 硬编码 Clang >= 18 限制

- 严重程度：低
- 位置：`config.hpp` 第 4-6 行

**问题描述：** 编译器守卫硬编码要求 Clang >= 18，排除了其他可能支持 C++23 的编译器（如 GCC、MSVC）。

**规范要求：** 作为 backport 库，应尽可能支持多种编译器。Forge 主项目的 `forge.cmake` 已处理 MSVC 的特殊路径。

**当前实现：**
```cpp
#if !defined(__clang__) || __clang_major__ < 18
#  error "exec requires Clang >= 18 (zig c++ with LLVM 18+)"
#endif
```

---

### I-18 非标准宏定义污染用户命名空间

- 严重程度：低
- 位置：`config.hpp` 第 19-41 行

**问题描述：** 定义了 `EXEC_EXPORT`、`EXEC_IMPORT`、`EXEC_LIKELY`、`EXEC_UNLIKELY`、`EXEC_ASSERT` 等宏，使用 `EXEC_` 前缀而非标准库实现通常使用的保留前缀（如 `_LIBCPP_`、`__`）。这些宏在 header-only 库中会泄漏到用户代码。

**规范要求：** 标准库实现应使用保留标识符（以 `_` 或 `__` 开头）避免与用户代码冲突。作为 backport，应遵循相同惯例，或在不需要时避免定义这些宏。

---

### I-19 `just` 的 `operation::start` 中 lambda 的 noexcept 标注不正确

- 严重程度：低
- 位置：`just.hpp` 第 25 行

**问题描述：** `start` 中的 lambda 标注为 `noexcept`，但 `set_value` 的调用可能不是 noexcept 的（取决于 receiver 的实现）。

**规范要求：** P2300R10 中 `set_value` 是 noexcept 的（receiver 的完成操作必须是 noexcept），所以此处标注在规范层面是正确的。但如果 receiver 实现不满足 noexcept 要求，此处会导致 `std::terminate`。

**当前实现：**
```cpp
std::apply([&](Vs&&... vs) noexcept {
    exec::set_value(std::move(self.rcvr_),
                    static_cast<Vs&&>(vs)...);
}, std::move(self.values_));
```
由于 `set_value_t::operator()` 已通过 `nothrow_tag_invocable` 约束（`completion_signatures.hpp` 第 17 行），此处 noexcept 在当前实现中不会触发问题，但缺少编译期静态断言验证。

---

## 第二部分：测试问题

### T-1 所有测试使用自定义测试宏而非 Google Test

- 严重程度：中
- 涉及文件：`test_just.cpp`、`test_then.cpp`、`test_sync_wait.cpp`、`test_stop_token.cpp`

**问题描述：** 所有测试文件使用自定义的 `TEST` / `RUN` 宏和 `assert()`，而非 Forge 项目标准的 Google Test 框架（`gtest_main`）。

**期望行为：** Forge 项目在 `3rdparty/googletest/` 中包含 Google Test 子模块，所有运行时测试应使用 `TEST()` / `EXPECT_*` / `ASSERT_*` 宏，链接 `gtest_main`，以获得统一的测试输出格式、失败定位和 CTest 集成。

**当前测试：** 例如 `test_just.cpp`：
```cpp
#define TEST(name) static void name()
#define RUN(name)  do { g_tests_run++; name(); g_tests_passed++; \
                        std::printf("[PASS] " #name "\n"); } while(0)
```
`assert()` 失败时仅输出文件/行号，无法提供 Google Test 的详细失败信息（期望值 vs 实际值）。

---

### T-2 `test_just_multiple_values` 被注释掉，未实际验证

- 严重程度：中
- 涉及文件：`test_just.cpp` 第 23-28 行

**问题描述：** `test_just_multiple_values` 测试创建了多值 sender 但立即 `(void)result;` 丢弃结果，附注 `// TODO: enable when sync_wait carries full type info`。该测试未被 `main()` 调用。

**期望行为：** 应验证 `just(1, std::string{"hello"})` 能正确传递多个值，`sync_wait` 返回 `optional<tuple<int, string>>`。

**当前测试：** 测试存在但被跳过，且 `main()` 中未调用 `RUN(test_just_multiple_values)`，说明开发者已知 `sync_wait` 无法处理带值 sender（对应 I-7）。

---

### T-3 `test_when_all.cpp` 和 `test_thread_pool.cpp` 为空壳占位

- 严重程度：低
- 涉及文件：`test_when_all.cpp`、`test_thread_pool.cpp`

**问题描述：** 两个测试文件仅打印 `[SKIP]` 消息并返回 0，不包含任何实际测试逻辑。

**期望行为：** 占位测试应明确标记为待实现，或从构建系统中排除。当前它们被包含在 `build.zig` 的测试列表中，会给人"测试通过"的错误印象。

**当前测试：**
```cpp
int main() {
    std::printf("[SKIP] when_all tests — Phase 3 not yet implemented\n");
    return 0;
}
```

---

### T-4 构建系统将所有测试编译为单一可执行文件

- 严重程度：中
- 涉及文件：`build.zig` 第 78-95 行

**问题描述：** `build.zig` 将所有 6 个测试 `.cpp` 文件编译链接为单一的 `exec-tests` 可执行文件。由于每个文件都定义了 `main()`、`g_tests_run`、`g_tests_passed` 等全局符号，这会导致多重定义链接错误。

**期望行为：** 每个测试文件应编译为独立的可执行文件，或使用 Google Test 框架统一入口。

**当前构建：**
```zig
for (test_srcs) |src| {
    tests_exe.addCSourceFile(.{
        .file  = b.path(src),
        .flags = cpp_flags,
    });
}
```
6 个 `.cpp` 文件各自定义 `int main()`，链接时必然冲突。

---

### T-5 缺少 `inline_scheduler` 的测试

- 严重程度：中
- 涉及文件：无对应测试文件

**问题描述：** `inline_scheduler` 是唯一完整实现的执行上下文，但没有任何专门的测试文件。其行为仅通过其他测试间接验证（`sync_wait` 内部使用）。

**期望行为：** 应有独立测试验证：
- `inline_scheduler` 满足 `scheduler` concept
- `schedule()` 返回的 sender 满足 `sender_in` concept
- `schedule() | then(fn)` 在调用线程上同步执行
- 两个 `inline_scheduler` 实例相等

---

### T-6 缺少 `receiver` concept 的编译探测

- 严重程度：低
- 涉及文件：无

**问题描述：** 没有编译探测验证 `receiver`、`sender`、`operation_state`、`scheduler` 等核心 concept 的约束正确性。

**期望行为：** 应有编译探测（正例和反例）验证：
- 满足 concept 的类型能通过检查
- 不满足 concept 的类型被正确拒绝（如缺少 `sender_concept` 的类型不应满足 `sender`）

---

### T-7 `stop_token` 测试无法链接

- 严重程度：高
- 涉及文件：`test_stop_token.cpp`

**问题描述：** `test_stop_token.cpp` 测试了 `inplace_stop_source::request_stop()` 和 `inplace_stop_callback`，但如 I-6 所述，`request_stop()`、`try_add_callback()`、`remove_callback()` 和析构函数均无定义。该测试无法通过链接阶段。

**期望行为：** 测试应能编译链接并运行。

---

### T-8 `sync_wait` 测试的断言基于错误的类型假设

- 严重程度：高
- 涉及文件：`test_sync_wait.cpp` 第 11-14 行、`test_just.cpp` 第 18-20 行

**问题描述：** 测试中 `std::get<0>(*r)` 假设 `sync_wait` 返回 `optional<tuple<int>>`，但由于 I-7（`sync_wait` 硬编码 `shared_state<>`），实际返回类型为 `optional<tuple<>>`。`std::get<0>` 在空 tuple 上是编译错误。

**期望行为：** 如果 `sync_wait` 的类型推导正确实现，这些测试应能正常工作。当前状态下，这些测试无法编译。

**当前测试：**
```cpp
auto r = exec::sync_wait(exec::just(99));
assert(r.has_value());
assert(std::get<0>(*r) == 99);  // tuple<> 没有 element 0
```

---

### T-9 `then` 测试未覆盖异常传播

- 严重程度：低
- 涉及文件：`test_then.cpp`

**问题描述：** `then` 的测试仅覆盖了正常值转换和 void 返回，未测试 `fn` 抛出异常时是否正确通过 `set_error(exception_ptr)` 传播。

**期望行为：** 应测试：
```cpp
auto work = exec::just(1) | exec::then([](int) -> int {
    throw std::runtime_error("boom");
});
// sync_wait 应重新抛出
```

---

### T-10 `then` 测试未覆盖 error/stopped 透传

- 严重程度：低
- 涉及文件：`test_then.cpp`

**问题描述：** 未测试 `then` 在上游 sender 以 `set_error` 或 `set_stopped` 完成时是否正确透传。

**期望行为：** 应测试：
- `just_error(e) | then(fn)` → fn 不被调用，error 透传
- `just_stopped() | then(fn)` → fn 不被调用，stopped 透传

---

### T-11 缺少 `env` 和 `prop` 机制的测试

- 严重程度：低
- 涉及文件：无

**问题描述：** `env.hpp` 中定义的 `prop`、`make_prop`、`make_env`、`get_scheduler`、`get_stop_token`、`get_allocator` 等查询机制没有任何测试。

**期望行为：** 应测试 `make_env(make_prop(get_stop_token_t{}, token))` 能正确响应 `get_stop_token` 查询。

---

## 第三部分：总结

### 按严重程度汇总

| 严重程度 | 实现问题 | 测试问题 | 合计 |
|----------|----------|----------|------|
| 高       | 5 (I-1, I-2, I-6, I-7, I-3) | 3 (T-7, T-8, T-4) | 8 |
| 中       | 8 (I-4, I-5, I-8, I-9, I-10, I-11, I-15, I-16) | 4 (T-1, T-2, T-5, T-6) | 12 |
| 低       | 6 (I-12, I-13, I-14, I-17, I-18, I-19) | 4 (T-3, T-9, T-10, T-11) | 10 |
| 合计     | 19       | 11       | 30   |

### 优先修复建议

1. **最高优先级 — I-1、I-2：** 命名空间和定制机制是架构级问题。当前使用 `namespace exec` + `tag_invoke` 的设计与 P2300R10 最终标准存在根本性偏差。如果目标是作为 Forge backport 实现"无感过渡"，必须将所有符号迁移到 `std::execution`（或通过注入机制实现），并将 CPO 定制从 `tag_invoke` 迁移到 P2300R10 的最终机制。这是一次全面重写。

2. **高优先级 — I-6：** `inplace_stop_source` 的核心函数缺少定义，导致所有涉及 stop token 的代码（包括 `sync_wait`）无法链接。必须提供 `request_stop()`、`try_add_callback()`、`remove_callback()` 和析构函数的完整实现。

3. **高优先级 — I-7、T-8：** `sync_wait` 的类型推导硬编码为空 tuple，导致所有带值 sender 的测试实际上无法编译。需要从 sender 的 `completion_signatures` 正确推导值类型。

4. **中优先级 — I-3、I-4、I-5：** 核心 concept 定义过于宽松，无法正确约束类型。应按 P2300R10 要求检查 `sender_concept` / `receiver_concept` 嵌套类型。

5. **中优先级 — T-4：** 构建系统将多个含 `main()` 的测试文件链接为单一可执行文件，必然导致链接错误。应改为每个测试独立编译，或迁移到 Google Test 框架。

6. **中优先级 — I-16：** 如果要集成到 Forge 项目，需要将构建系统从 Zig 迁移到 CMake，并接入 `forge.cmake` 的检测/注入机制。

7. **低优先级：** I-12 至 I-14、I-17 至 I-19 及 T-3、T-9 至 T-11 可在核心架构问题解决后逐步改进。

### 总体评估

当前参考实现处于早期原型阶段，仅完成了设计文档规划的约 20% 功能。已实现的部分存在多个架构级问题（命名空间、定制机制、类型推导），且由于 `inplace_stop_source` 缺少函数定义和 `sync_wait` 类型硬编码，现有代码实际上无法编译链接运行。建议在继续扩展功能前，先解决 I-1、I-2、I-6、I-7 四个根本性问题。

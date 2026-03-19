# `std::execution` (P2300) Backport 设计需求文档

**提案**: P2300R10 — `std::execution`
**状态**: 已并入 C++26 IS (International Standard)
**文档版本**: v0-r0
**日期**: 2026-03-20

---

## 1. 概述

### 1.1 提案简介

P2300R10 定义了 `std::execution`，一个基于 sender/receiver 模型的结构化异步编程框架。该框架取代了传统的回调、future/promise 模式，提供了类型安全、可组合、支持取消的异步操作抽象。核心设计理念：

- **结构化并发 (Structured Concurrency)**：异步操作的生命周期由作用域严格管理，不存在"fire-and-forget"泄漏
- **惰性求值 (Lazy Evaluation)**：sender 描述的是工作的"蓝图"，只有在 `connect` + `start` 后才真正执行
- **三路完成 (Three-way Completion)**：每个异步操作以 `set_value`（成功）、`set_error`（错误）或 `set_stopped`（取消）之一完成
- **执行上下文无关 (Execution Context Agnostic)**：算法与调度器解耦，同一 sender 链可在线程池、io_uring、GPU 等不同上下文上执行

### 1.2 与 Forge 项目的关系

`std::execution` 在 Forge 中承担双重角色：

1. **独立的异步编程基础设施**：为下游用户提供标准兼容的 sender/receiver 框架
2. **linalg 的 execution policy 支撑**：P1673 (`std::linalg`) 的并行重载依赖 execution policy，而 P2300 的 scheduler 概念是 C++26 中 execution policy 的基础（交叉引用：`require/02-std-linalg-require.md`）

### 1.3 目标

- 提供与 P2300R10 最终标准完全一致的 API 表面
- 遵循 Forge 核心原则——**无感过渡**：当编译器/标准库原生支持 `std::execution` 后，下游代码无需任何修改
- Header-only 实现，最低目标 C++23（zig/LLVM 工具链，Clang 19+）
- 所有符号注入 `namespace std::execution`（或 `namespace std`），签名与标准一致

---

## 2. API 表面

基于 P2300R10 最终规范，以下为需要实现的核心组件清单。

### 2.1 核心概念 (Concepts)

| 概念 | 头文件位置 | 说明 |
|------|-----------|------|
| `scheduler` | `<execution>` | 可调度工作的执行上下文抽象 |
| `sender` | `<execution>` | 描述异步操作的惰性值 |
| `sender_in<S, Env>` | `<execution>` | 在给定环境下可查询 completion signatures 的 sender |
| `sender_of<S, Sigs...>` | `<execution>` | 产生特定 completion signatures 的 sender |
| `sender_to<S, R>` | `<execution>` | 可与特定 receiver 连接的 sender |
| `receiver` | `<execution>` | 接收异步操作完成通知的回调抽象 |
| `receiver_of<R, Sigs...>` | `<execution>` | 可接受特定 completion signatures 的 receiver |
| `operation_state` | `<execution>` | connect 产生的操作状态，调用 start 启动执行 |

### 2.2 核心类型与 CPO (Customization Point Objects)

**完成信号 CPO：**

| CPO | 签名 | 说明 |
|-----|------|------|
| `set_value` | `void set_value(R&&, Vs&&...)` | 值完成通道 |
| `set_error` | `void set_error(R&&, E&&)` | 错误完成通道 |
| `set_stopped` | `void set_stopped(R&&)` | 取消完成通道 |

**连接与启动 CPO：**

| CPO | 签名 | 说明 |
|-----|------|------|
| `connect` | `operation_state auto connect(S&&, R&&)` | 将 sender 与 receiver 绑定，产生 operation_state |
| `start` | `void start(O&) noexcept` | 启动 operation_state 的执行 |
| `schedule` | `sender auto schedule(scheduler auto&)` | 从 scheduler 获取一个表示"在此上下文执行"的 sender |

**环境查询 CPO：**

| CPO | 签名 | 说明 |
|-----|------|------|
| `get_env` | `env auto get_env(const T&)` | 获取对象关联的查询环境 |
| `get_completion_signatures` | `completion_signatures auto get_completion_signatures(S&&, Env&&)` | 查询 sender 的完成签名集 |
| `get_scheduler` | `scheduler auto get_scheduler(Env&&)` | 从环境中查询关联的 scheduler |
| `get_delegatee_scheduler` | `scheduler auto get_delegatee_scheduler(Env&&)` | 查询委托 scheduler |
| `get_stop_token` | `stoppable_token auto get_stop_token(Env&&)` | 从环境中查询 stop token |
| `get_allocator` | `allocator auto get_allocator(Env&&)` | 从环境中查询分配器 |

**核心类型：**

| 类型 | 说明 |
|------|------|
| `completion_signatures<Sigs...>` | 编译期完成签名集合 |
| `empty_env` | 空查询环境 |
| `env<Queries...>` | 可组合的查询环境（P2300R10 新增） |
| `run_loop` | 单线程事件循环执行上下文 |
| `run_loop::scheduler` | run_loop 关联的 scheduler 类型 |

### 2.3 Sender 工厂

| 工厂函数 | 签名 | 说明 |
|----------|------|------|
| `just(vs...)` | `sender_of<set_value_t(Vs...)>` | 立即以值完成 |
| `just_error(e)` | `sender_of<set_error_t(E)>` | 立即以错误完成 |
| `just_stopped()` | `sender_of<set_stopped_t()>` | 立即以取消完成 |
| `read_env(tag)` | `sender` | 读取 receiver 环境中的查询结果 |
| `schedule(sch)` | `sender_of<set_value_t()>` | 在指定 scheduler 上调度执行 |

### 2.4 Sender 适配器

| 适配器 | 说明 |
|--------|------|
| `then(fn)` | 变换值完成通道：`set_value(vs...) → set_value(fn(vs...))` |
| `upon_error(fn)` | 变换错误完成通道 |
| `upon_stopped(fn)` | 变换取消完成通道 |
| `let_value(fn)` | 值完成后链接新的 sender：`fn(vs...) → sender` |
| `let_error(fn)` | 错误完成后链接新的 sender |
| `let_stopped(fn)` | 取消完成后链接新的 sender |
| `bulk(shape, fn)` | 批量并行执行：对 `[0, shape)` 中每个索引调用 `fn(i, vs...)` |
| `split(sndr)` | 将单次 sender 转为可多次连接的共享 sender |
| `ensure_started(sndr)` | 立即启动 sender，返回可稍后等待结果的 sender |
| `when_all(sndrs...)` | 等待所有 sender 完成，汇聚结果 |
| `into_variant(sndr)` | 将多种值完成签名合并为 `variant` |
| `stopped_as_optional(sndr)` | 将取消映射为 `std::nullopt` |
| `stopped_as_error(sndr, e)` | 将取消映射为错误 |
| `starts_on(sch, sndr)` | 在指定 scheduler 上启动 sender（替代旧名 `start_on`） |
| `continues_on(sndr, sch)` | 完成后切换到指定 scheduler（替代旧名 `on`） |

> **注意**：P2300 后期修订将 `start_on` / `on` 重命名为 `starts_on` / `continues_on`，实现应跟踪最终标准命名。

### 2.5 Sender 消费者

| 消费者 | 说明 |
|--------|------|
| `sync_wait(sndr)` | 阻塞当前线程等待 sender 完成，返回 `optional<tuple<Vs...>>` |
| `sync_wait_with_variant(sndr)` | 同上，但返回 `optional<variant<tuple<Vs>...>>` |
| `start_detached(sndr)` | 启动 sender 但不等待结果（需谨慎使用） |

### 2.6 停止令牌 (Stop Token)

| 类型 | 说明 |
|------|------|
| `stop_token` | 通用停止令牌（类型擦除） |
| `stop_source` | 通用停止源 |
| `stop_callback<Cb>` | 通用停止回调 |
| `in_place_stop_token` | 非引用计数的轻量停止令牌 |
| `in_place_stop_source` | 非引用计数的轻量停止源 |
| `in_place_stop_callback<Cb>` | 非引用计数的轻量停止回调 |
| `never_stop_token` | 永不停止的哨兵令牌 |
| `stoppable_token` concept | 停止令牌概念 |
| `stoppable_token_for<Cb>` concept | 支持特定回调类型的停止令牌概念 |
| `unstoppable_token` concept | 永不停止的令牌概念 |

> **注意**：`stop_token` / `stop_source` / `stop_callback` 在 C++26 中从 `<stop_token>` 移入 `<execution>` 并成为 P2300 的一部分。C++20 的 `std::stop_token`（`<stop_token>` 头文件）仍然存在但独立于此。

### 2.7 Coroutine 集成

| 组件 | 说明 |
|------|------|
| `as_awaitable(sndr, promise&)` | 将 sender 转换为可 `co_await` 的 awaitable |
| `with_awaitable_senders<Promise>` | promise_type mixin，使 coroutine 中可直接 `co_await` sender |


---

## 3. 依赖分析

### 3.1 C++23 语言特性依赖

P2300 的实现重度依赖以下 C++20/23 语言特性：

| 特性 | 标准 | Clang 19 支持 | 用途 |
|------|------|--------------|------|
| Concepts (`requires`, `concept`) | C++20 | 完整 | 所有核心概念定义 |
| Coroutines (`co_await`, `co_return`) | C++20 | 完整 | `as_awaitable`, `with_awaitable_senders` |
| `std::move_only_function` | C++23 | libc++ 完整 | 类型擦除回调 |
| `std::expected` | C++23 | libc++ 完整 | 内部错误处理（可选） |
| Deducing `this` | C++23 | 完整 | 简化 CPO 实现（可选，可用 CRTP 替代） |
| Pack indexing | C++26 | 完整 | completion_signatures 元编程 |
| `[[no_unique_address]]` | C++20 | 完整 | 零开销存储空类型 |
| Structured bindings in conditions | C++26 | 完整 | 便利语法（非必需） |

**结论**：Clang 19 (zig c++) 的语言特性支持完全满足 P2300 实现需求。

### 3.2 标准库依赖

| 头文件 | 用途 |
|--------|------|
| `<concepts>` | `std::move_constructible`, `std::copy_constructible`, `std::destructible` 等 |
| `<type_traits>` | 类型萃取，SFINAE 辅助 |
| `<tuple>` | `sync_wait` 返回值类型 |
| `<variant>` | `sync_wait_with_variant`, `into_variant` |
| `<optional>` | `sync_wait` 返回值，`stopped_as_optional` |
| `<exception>` | `std::exception_ptr` 传播 |
| `<coroutine>` | coroutine 集成 |
| `<atomic>` | stop_token 实现 |
| `<mutex>`, `<condition_variable>` | `sync_wait`, `run_loop` 实现 |
| `<functional>` | `std::invoke` |

以上均为 C++23 标准库的成熟组件，libc++ (LLVM 19) 全部支持。

### 3.3 与标准库其他组件的关系

| 组件 | 关系 |
|------|------|
| `<stop_token>` (C++20) | P2300 定义了自己的 `in_place_stop_token` 等类型；C++20 的 `std::stop_token` 独立存在 |
| `<execution>` (C++17) | **命名空间冲突**——详见第 5 节 |
| `<thread>` | `run_loop` 内部可能使用；`sync_wait` 阻塞当前线程 |
| `<linalg>` (P1673) | linalg 的 execution policy 重载依赖 P2300 的 scheduler 概念 |

### 3.4 参考实现现状评估

`reference/execution/` 下的骨架实现现状：

| 组件 | 状态 | 评估 |
|------|------|------|
| `tag_invoke` 协议 | ✅ 已实现 | 功能完整，但 P2300R10 最终版已弃用 `tag_invoke`，改用 `tag_dispatch` / member CPO |
| `inplace_stop_token` | ✅ 已实现 | 接口基本正确，缺少 `request_stop` 的完整实现（仅有声明） |
| `completion_signatures` | ⚠️ 骨架 | 元编程工具不完整（`value_types_of` 等仅有占位） |
| `env` / `get_env` | ✅ 已实现 | `prop` / `env` 组合模式可用 |
| `sender` / `receiver` / `operation_state` 概念 | ⚠️ 部分 | 基本约束已定义，`receiver_of` 的完整检查标记为 TODO |
| `just` / `just_error` / `just_stopped` | ✅ 已实现 | 功能正确，测试通过 |
| `then` | ✅ 已实现 | 含管道操作符支持，测试通过 |
| `sync_wait` | ⚠️ 简化版 | 仅支持零参数值完成，类型推导标记为 TODO |
| `inline_scheduler` | ✅ 已实现 | 同步调度器，功能正确 |
| `run_loop` | ❌ 未实现 | 标准要求的核心执行上下文 |
| `let_*` 适配器 | ❌ 未实现 | |
| `bulk` | ❌ 未实现 | |
| `when_all` | ❌ 未实现 | 测试文件为占位符 |
| `split` / `ensure_started` | ❌ 未实现 | |
| coroutine 集成 | ❌ 未实现 | |
| `thread_pool` | ❌ 未实现 | 源文件为占位符 |
| `io_uring_scheduler` | ❌ 未实现 | 源文件为占位符，且为非标准扩展 |

**关键发现**：参考实现使用 `exec` 命名空间而非 `std::execution`，使用 `tag_invoke` 作为定制点机制。P2300R10 最终版已从 `tag_invoke` 转向基于成员函数的定制点（sender/receiver 通过成员 `connect`、`get_env` 等实现定制），这是一个重大架构差异。


---

## 4. 实现策略

### 4.1 分阶段计划

#### Phase 1：核心基础 + 最小可用集

**目标**：建立核心概念框架，实现最简单的端到端 sender 链。

**交付物**：
- 核心概念：`sender`, `receiver`, `operation_state`, `scheduler`, `sender_in`, `sender_to`, `receiver_of`
- 完成信号 CPO：`set_value`, `set_error`, `set_stopped`
- 连接/启动 CPO：`connect`, `start`
- 环境查询：`get_env`, `get_completion_signatures`, `get_stop_token`
- `completion_signatures<Sigs...>` 及完整的元编程工具（`value_types_of`, `error_types_of`, `sends_stopped`）
- 停止令牌：`in_place_stop_token`, `in_place_stop_source`, `in_place_stop_callback`, `never_stop_token`
- 停止令牌概念：`stoppable_token`, `stoppable_token_for`, `unstoppable_token`
- Sender 工厂：`just`, `just_error`, `just_stopped`
- Sender 适配器：`then`
- Sender 消费者：`sync_wait`
- 执行上下文：`run_loop`（`sync_wait` 的内部驱动）

**验收标准**：
```cpp
// 以下代码必须编译并正确运行
auto result = std::execution::sync_wait(
    std::execution::just(42)
    | std::execution::then([](int x) { return x * 2; })
);
assert(result && std::get<0>(*result) == 84);
```

#### Phase 2：核心适配器扩展

**目标**：实现 `let_*` 系列和错误/取消处理适配器。

**交付物**：
- `upon_error`, `upon_stopped`
- `let_value`, `let_error`, `let_stopped`
- `read_env`
- `schedule`（CPO，非工厂）
- `starts_on`, `continues_on`
- `stopped_as_optional`, `stopped_as_error`

**验收标准**：
```cpp
auto sndr = std::execution::just(42)
    | std::execution::let_value([](int x) {
        return std::execution::just(x + 1);
    });
auto result = std::execution::sync_wait(std::move(sndr));
assert(result && std::get<0>(*result) == 43);
```

#### Phase 3：并行与共享

**目标**：实现多 sender 组合和共享语义。

**交付物**：
- `when_all`
- `bulk`
- `split`, `ensure_started`
- `into_variant`
- `sync_wait_with_variant`
- `start_detached`

**验收标准**：
```cpp
auto [a, b] = *std::execution::sync_wait(
    std::execution::when_all(
        std::execution::just(1),
        std::execution::just(2)
    )
);
assert(a == 1 && b == 2);
```

#### Phase 4：Coroutine 集成 + 完整 API

**目标**：完成 coroutine 支持和剩余 API。

**交付物**：
- `as_awaitable`
- `with_awaitable_senders`
- 通用 `stop_token` / `stop_source` / `stop_callback`（类型擦除版本）
- 完整的 `env` 查询协议
- 所有剩余的标准要求 API

**验收标准**：
```cpp
// sender 可在 coroutine 中被 co_await
exec::task<int> example() {
    int x = co_await std::execution::just(42);
    co_return x;
}
```

### 4.2 技术难点

#### 4.2.1 定制点机制：tag_invoke vs 成员函数

P2300 的定制点机制经历了重大演变：

- **早期版本 (R0-R7)**：使用 `tag_invoke` 协议（ADL-based）
- **后期修订 (R8+)**：转向成员函数 + CPO fallback 模式

最终标准采用的机制：
1. CPO 首先检查对象是否有对应的成员函数（如 `s.connect(r)`）
2. 若无成员函数，fallback 到 `tag_invoke`（向后兼容）
3. 某些 CPO 有默认实现（如 `get_env` 默认返回 `empty_env`）

**对 Forge 的影响**：参考实现 (`reference/execution/`) 完全基于 `tag_invoke`，需要重构为成员函数优先的模式。这是最大的架构迁移工作。

#### 4.2.2 completion_signatures 元编程

`completion_signatures` 的类型计算是 P2300 中最复杂的编译期元编程部分：

- 需要从 sender 的 completion signatures 推导出经过适配器变换后的新 signatures
- `then(fn)` 需要计算 `fn` 应用于每种值签名后的返回类型
- `when_all` 需要合并多个 sender 的签名
- `let_value(fn)` 需要处理 `fn` 返回的 sender 的签名

这部分是参考实现中标记为 TODO 的核心缺失。

#### 4.2.3 与 stdexec 参考实现的关系

NVIDIA 的 [stdexec](https://github.com/NVIDIA/stdexec) 是 P2300 的官方参考实现：

- **优势**：生产级质量，覆盖完整 API，持续跟踪标准演进
- **劣势**：非 header-only（部分组件需要链接），依赖较多，代码量大（~50K 行）
- **策略**：不直接移植 stdexec 代码，但作为正确性参考和测试用例来源

#### 4.2.4 Header-only 约束

P2300 的某些组件天然适合 header-only（概念、算法、sender 工厂），但以下组件需要特别处理：

- `run_loop`：包含互斥锁和条件变量，但可以 header-only 实现（inline 定义）
- `sync_wait`：依赖 `run_loop`，同上
- 停止令牌的原子操作：header-only 可行，但需注意 ODR

参考实现中的 `thread_pool` 和 `io_uring_scheduler` 是非标准扩展，不在 backport 范围内。


---

## 5. 与 C++17 `<execution>` 的共存方案

### 5.1 问题描述

C++17 已定义了 `<execution>` 头文件，其中包含：

```cpp
namespace std::execution {
    inline constexpr sequenced_policy      seq{};
    inline constexpr parallel_policy        par{};
    inline constexpr parallel_unsequenced_policy par_unseq{};
    inline constexpr unsequenced_policy     unseq{};  // C++20
}
```

P2300 的 `std::execution` 命名空间与之完全重叠，新增了大量类型和函数。C++26 标准中两者共存于同一命名空间，但分属不同头文件（`<execution>` 同时包含旧的 execution policy 和新的 sender/receiver 框架）。

### 5.2 冲突场景

| 场景 | 风险 |
|------|------|
| 用户 `#include <execution>` 获取 C++17 execution policy | Forge 的 backport wrapper 拦截后，需同时提供旧 policy 和新 sender/receiver |
| 用户同时使用 `std::execution::par` 和 `std::execution::just` | 两者必须在同一命名空间下共存 |
| 编译器已原生支持 C++17 `<execution>` 但不支持 P2300 | backport 必须保留原有 policy，仅注入新增内容 |

### 5.3 共存策略

#### 5.3.1 Wrapper 头文件设计

创建 `backport/execution` 作为 wrapper 头文件，遵循现有 `backport/memory` 和 `backport/simd` 的模式：

```
backport/
  execution                          ← wrapper header
  cpp26/
    execution.hpp                    ← P2300 backport 实现
```

Wrapper 逻辑：

```cpp
// backport/execution
#pragma once

// Step 1: Include the real <execution> (C++17 policies)
#if defined(_MSC_VER)
#include FORGE_MSVC_EXECUTION_HEADER
#elif defined(__has_include_next)
#include_next <execution>
#endif

// Step 2: Inject P2300 backport only if not natively available
#if !defined(__cpp_lib_execution_sender) // 假设的 feature-test macro
#include "cpp26/execution.hpp"
#endif
```

#### 5.3.2 Feature-test Macro

P2300 预期的 feature-test macro 为 `__cpp_lib_sender`（或类似命名，最终标准可能为 `__cpp_lib_execution`）。Forge 的检测逻辑：

1. `forge.cmake` 中使用 `check_cxx_source_compiles()` 检测 `std::execution::just` 等 P2300 特有符号
2. 若不存在，将 `backport/` 加入 include path
3. Wrapper 头文件通过 feature-test macro 做二次守卫

#### 5.3.3 forge.cmake 检测机制

```cmake
# Check for std::execution sender/receiver (P2300 / C++26)
check_cxx_source_compiles("
    #include <execution>
    int main() {
        auto sndr = std::execution::just(42);
        (void)sndr;
        return 0;
    }
" HAS_STD_EXECUTION_SENDER)

if(NOT HAS_STD_EXECUTION_SENDER)
    set(FORGE_NEEDS_BACKPORT TRUE)
    message(STATUS \"Forge: std::execution (P2300) backport enabled\")
endif()
```

同时需要处理 MSVC 的头文件路径解析：

```cmake
if(MSVC)
    # Locate real <execution> header path
    foreach(_forge_include_dir IN LISTS _forge_msvc_include_candidates)
        if(EXISTS "${_forge_include_dir}/execution")
            file(TO_CMAKE_PATH "${_forge_include_dir}/execution"
                 FORGE_MSVC_EXECUTION_HEADER)
            break()
        endif()
    endforeach()
    if(FORGE_MSVC_EXECUTION_HEADER)
        target_compile_definitions(forge INTERFACE
            FORGE_MSVC_EXECUTION_HEADER=\"${FORGE_MSVC_EXECUTION_HEADER}\"
        )
    endif()
endif()
```

#### 5.3.4 命名空间注入

backport 实现必须将所有符号注入 `namespace std::execution`（而非参考实现中的 `namespace exec`）：

```cpp
// backport/cpp26/execution.hpp
namespace std::execution {
    // P2300 backport symbols injected here
    // C++17 execution policies (seq, par, etc.) remain untouched
    // as they come from the real <execution> header
}
```

**关键约束**：backport 不得重新定义 `std::execution::seq` / `par` / `par_unseq` 等已存在的符号。实现中需要用 `#ifndef` 或 concept-based SFINAE 避免重复定义。


---

## 6. 风险与开放问题

### 6.1 规范稳定性风险

| 风险 | 严重度 | 说明 |
|------|--------|------|
| P2300 最终措辞与 R10 存在差异 | 中 | P2300 已投票并入 C++26 IS，但编辑过程中可能有措辞调整。需持续跟踪 [wg21.link/p2300](https://wg21.link/p2300) 和 C++26 DIS |
| 定制点机制最终形态不确定 | 高 | `tag_invoke` vs 成员函数 vs `tag_dispatch` 的最终选择直接影响架构。R10 倾向成员函数优先，但实现细节可能在 DIS 阶段微调 |
| Feature-test macro 命名未最终确定 | 低 | `__cpp_lib_sender` 或 `__cpp_lib_execution` 的具体命名和值需等 DIS 确认 |
| `stop_token` 归属问题 | 低 | C++26 中 `stop_token` 相关类型是否从 `<stop_token>` 迁移到 `<execution>` 的最终决定 |

### 6.2 实现复杂度风险

| 风险 | 严重度 | 说明 |
|------|--------|------|
| 整体实现量极大 | **极高** | P2300 是 C++26 中最大的单一提案之一。stdexec 参考实现约 50K 行代码。即使精简实现，预估也需 10K-15K 行 |
| completion_signatures 元编程复杂度 | 高 | 类型计算涉及大量模板元编程，编译错误信息极难调试 |
| `split` / `ensure_started` 的内存管理 | 高 | 需要引用计数的共享状态，正确处理并发访问和生命周期 |
| `bulk` 的并行语义 | 中 | 需要与 scheduler 协作实现真正的并行执行 |
| Coroutine 集成的 ABI 兼容性 | 中 | 不同编译器的 coroutine ABI 可能不同 |

### 6.3 编译时间风险

Header-only 实现意味着所有模板实例化发生在用户的翻译单元中：

- P2300 的类型计算密集度远超 `unique_resource` 或 `simd`
- 每个 sender 适配器都会生成新的模板实例
- 深层 sender 链（如 `just | then | let_value | then | ...`）会导致类型嵌套爆炸
- **预估影响**：包含完整 `<execution>` backport 可能增加 2-5 秒编译时间（取决于使用深度）

**缓解措施**：
- 使用 `extern template` 对常用实例化进行显式实例化声明
- 将实现拆分为多个子头文件，用户按需包含
- 提供 `<execution_fwd>` 前向声明头文件

### 6.4 命名空间冲突风险

向 `namespace std` 注入用户定义的符号在技术上是未定义行为（[namespace.std] p1），但这是所有 backport 库的通用做法（Abseil、Boost 等均有先例）。风险：

- 与编译器内部实现冲突（如编译器部分实现了 P2300）
- ODR 违规（如果用户同时链接了另一个 P2300 backport）

**缓解措施**：forge.cmake 的检测机制确保仅在原生不支持时注入。

### 6.5 与 stdexec 的关系

| 方面 | 说明 |
|------|------|
| 代码复用 | 不直接复用 stdexec 代码（许可证为 Apache-2.0，与 Forge 的 MIT 兼容，但代码风格和架构差异大） |
| 测试用例 | 可参考 stdexec 的测试用例设计，但需独立编写 |
| 正确性验证 | 可将 stdexec 作为"参考答案"，对比行为一致性 |
| 性能基准 | 后期可与 stdexec 做性能对比 |

### 6.6 开放问题

1. **是否需要实现 Domain 定制？** P2300R10 引入了 `default_domain` 和用户自定义 domain 的机制。初期可仅实现 `default_domain`，但需预留扩展点。

2. **`run_loop` 是否作为公开 API？** 标准要求 `run_loop` 是公开类型，但其主要用途是 `sync_wait` 的内部驱动。是否需要完整暴露其 API？**建议**：是，标准要求公开。

3. **`start_detached` 的安全性策略？** `start_detached` 是"fire-and-forget"语义，与结构化并发理念矛盾。标准仍然包含它，但是否需要额外的安全警告或编译期诊断？

4. **编译器最低版本是否需要提升？** 当前目标 Clang 19。若 P2300 最终版使用了 Clang 19 不支持的 C++26 特性（如 reflection），可能需要提升到 Clang 20+。目前评估无此风险。


---

## 7. 建议

### 7.1 优先级评估

`std::execution` 在 Forge 的 backport 优先级中应排在 **第三位**（在 `unique_resource` 和 `simd` 之后），但需要注意：

- **实现工作量远超前两者之和**：`unique_resource` 约 800 行，`simd` 约 3000 行，而 `execution` 预估 10K-15K 行
- **但战略价值最高**：它是 C++26 异步编程的基石，也是 `linalg` execution policy 的前置依赖
- **建议**：将 Phase 1（最小可用集）作为高优先级目标，Phase 2-4 可与其他 backport 并行推进

### 7.2 参考实现的处置

对于 `reference/execution/` 下的现有代码：

- **不建议直接在其基础上继续开发**，原因：
  1. 使用 `exec` 命名空间而非 `std::execution`，需要全面重命名
  2. 基于 `tag_invoke` 定制点，与 P2300R10 最终版的成员函数优先模式不一致
  3. 使用 zig 构建系统而非 CMake，与 Forge 主项目不一致
  4. `completion_signatures` 元编程不完整，是最核心的基础设施缺失

- **建议保留作为参考**，以下部分有复用价值：
  1. `inplace_stop_token` 的 lock-free 实现思路（需适配命名）
  2. `just` / `then` / `sync_wait` 的基本结构（需重构定制点机制）
  3. 测试用例的场景设计

- **建议的开发路径**：在 `backport/cpp26/` 下从头实现，参考 P2300R10 规范文本和 stdexec 的行为，而非直接迁移 `reference/execution/`

### 7.3 与 linalg 的依赖关系处理

`std::linalg` (P1673) 的 execution policy 重载依赖 P2300：

```cpp
// P1673 中的典型签名
template<class ExecutionPolicy, class in_vector_t, class in_vector_t2, class Scalar>
Scalar dot(ExecutionPolicy&& exec, in_vector_t v1, in_vector_t2 v2, Scalar init);
```

**建议的解耦策略**：

1. **linalg Phase 1 不依赖 P2300**：先实现不带 execution policy 的串行版本
2. **execution Phase 1 独立推进**：实现最小可用集（`just` / `then` / `sync_wait`）
3. **集成阶段**：当 execution Phase 2 完成（含 `scheduler` 和 `bulk`）后，为 linalg 添加并行重载
4. **接口预留**：linalg 的 CMake 配置中预留 `FORGE_HAS_EXECUTION` 开关，有条件地启用并行重载

### 7.4 文件结构建议

```
backport/
  execution                              ← wrapper header (拦截 #include <execution>)
  cpp26/
    execution.hpp                        ← 统一入口
    execution/
      concepts.hpp                       ← sender, receiver, operation_state, scheduler
      completion_signatures.hpp          ← completion_signatures 及元编程工具
      stop_token.hpp                     ← in_place_stop_token 等
      env.hpp                            ← get_env, empty_env, env<>
      just.hpp                           ← just, just_error, just_stopped
      then.hpp                           ← then, upon_error, upon_stopped
      let.hpp                            ← let_value, let_error, let_stopped
      bulk.hpp                           ← bulk
      when_all.hpp                       ← when_all
      split.hpp                          ← split, ensure_started
      sync_wait.hpp                      ← sync_wait, sync_wait_with_variant
      run_loop.hpp                       ← run_loop
      into_variant.hpp                   ← into_variant
      stopped_as.hpp                     ← stopped_as_optional, stopped_as_error
      start_detached.hpp                 ← start_detached
      coroutine.hpp                      ← as_awaitable, with_awaitable_senders
      schedule.hpp                       ← starts_on, continues_on, schedule
      read_env.hpp                       ← read_env
```

### 7.5 测试策略

沿用 Forge 现有的双轨测试模式：

- **Runtime 测试**（GTest）：验证算法行为正确性
  - `test_execution_concepts.cpp` — 概念约束检查
  - `test_execution_just.cpp` — sender 工厂
  - `test_execution_then.cpp` — 值变换适配器
  - `test_execution_let.cpp` — sender 链接适配器
  - `test_execution_sync_wait.cpp` — 同步等待消费者
  - `test_execution_stop_token.cpp` — 停止令牌
  - `test_execution_when_all.cpp` — 多 sender 组合
  - `test_execution_bulk.cpp` — 批量执行
  - `test_execution_run_loop.cpp` — 事件循环

- **Compile probe 测试**：验证 API 表面和类型约束
  - 概念满足性检查（`static_assert(sender<just_sender>)`）
  - completion_signatures 推导正确性
  - 管道操作符可用性

### 7.6 总结

`std::execution` (P2300) 是 Forge 项目中最具挑战性的 backport 目标。建议采取渐进式策略：

1. **短期**（Phase 1）：实现最小可用集，验证架构可行性，为 linalg 预留接口
2. **中期**（Phase 2-3）：扩展核心适配器，实现并行原语，与 linalg 集成
3. **长期**（Phase 4）：完成 coroutine 集成和完整 API 覆盖

不建议在参考实现基础上继续开发，而应在 `backport/cpp26/` 下基于 P2300R10 规范从头实现，以确保与最终标准的一致性和 Forge 无感过渡原则的遵守。


# `std::execution` 参考实现审查答复 (v0-r0-a0)

对应报告：[`report/03-std-execution-v0-r0.md`](/home/i/projects/forward/ccforge/report/03-std-execution-v0-r0.md)

本答复基于当前工作区代码状态（截至 2026-03-20）。

重要说明：`reference/execution/` 仅作为本轮评审草案目录，最终会移除。本轮所有“接受/部分接受”的修复与实现，已迁移并融入 Forge 的 backport 体系：
- wrapper 注入：[`backport/execution`](/home/i/projects/forward/ccforge/backport/execution)（拦截 `#include <execution>`，先 `#include_next <execution>` 保留 execution policies，再按需注入 P2300 MVP）
- C++26 实现头：[`backport/cpp26/execution.hpp`](/home/i/projects/forward/ccforge/backport/cpp26/execution.hpp)
- 测试已融入现有体系：[`test/test_execution_mvp.cpp`](/home/i/projects/forward/ccforge/test/test_execution_mvp.cpp)、[`test/test_execution_inline_scheduler.cpp`](/home/i/projects/forward/ccforge/test/test_execution_inline_scheduler.cpp)、[`test/test_execution_stop_token.cpp`](/home/i/projects/forward/ccforge/test/test_execution_stop_token.cpp)
- 示例已融入现有体系：[`example/execution_example.cpp`](/home/i/projects/forward/ccforge/example/execution_example.cpp)

为避免误导，本答复对每条审查意见给出：
- `结论`：接受 / 部分接受 / 驳斥 / 暂缓
- `理由`
- `落地点`：若接受/部分接受，给出预期改动位置；若当前已修复则标注“已修复，见 …”

---

## 总体口径（本轮共识）

1. 若要实现 Forge 的“无感过渡”，最终对外入口应是 `std::execution::...` + `<execution>` 注入形态，而不是让用户包含 `exec/execution.hpp` 或依赖 `exec::` 命名空间。
2. `reference/execution/` 可以允许阶段性实现（MVP），但必须保证：
   - 关键 API 能编译/链接/运行（至少在该参考实现自己的构建/测试路径中）
   - 测试不“假绿”（占位/skip 必须显式，构建系统不能把多个 `main()` 链到同一可执行）
3. 架构级差异（例如定制机制）需要明确项目决策：是“严格对齐 P2300R10 的最终机制”，还是“以 `tag_invoke` 作为内部手段/过渡层”。在决策前，相关重写项按 `暂缓/TODO` 处理，但仍记录落地点。

---

## 第一部分：实现问题（I-*）

### I-1 命名空间使用 `exec` 而非 `std::execution`
结论：接受，已修复（迁移时一并收敛到 backport 注入形态）。

理由：
- 报告指出的目标成立：Forge backport 的最终形态应允许用户写 `std::execution::just/then/sync_wait/...`。
- 当前参考实现已经在 `exec/execution.hpp` 中提供了 `std::execution::*` 的别名/转发，但这是通过向 `namespace std::execution` 注入符号实现的，存在标准层面的 UB 风险，也不是 Forge 期望的“wrapper 注入 `<execution>`”形态。

落地点：
- 已修复（最终形态）：[`backport/execution`](/home/i/projects/forward/ccforge/backport/execution) + [`backport/cpp26/execution.hpp`](/home/i/projects/forward/ccforge/backport/cpp26/execution.hpp)。
- `forge.cmake` 已加入 P2300 MVP 的 feature probe，并为 MSVC 注入 `FORGE_MSVC_EXECUTION_HEADER`（模式与 `memory` 一致）。

### I-2 使用 `tag_invoke` 而非 P2300R10 最终采纳的定制机制
结论：暂缓（需要项目决策），但接受其为“无感过渡”的核心风险点。

理由：
- 如果最终标准不再以 `tag_invoke` 作为对外定制协议，那么继续把 `tag_invoke` 当成“用户扩展点”会形成未来迁移成本。
- 但在原型阶段，用 `tag_invoke` 快速串起 `connect/start/get_env/get_completion_signatures` 是可理解的工程取舍；是否要在本仓库内“严格对齐最终机制”需要明确目标与投入。

落地点（两条路线二选一）：
- TODO 路线 A（严格对齐）：逐步移除 [`reference/execution/include/exec/core/tag_invoke.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/core/tag_invoke.hpp) 对外可见性，改为 P2300R10 最终机制（概念/分发/定制点）。
- TODO 路线 B（保留 tag_invoke 作为内部手段）：保留 `tag_invoke_fn` 作为实现细节，但对外表面与 `std::execution` 对齐，并在文档中明确“与最终标准的偏差点”。

### I-3 `sender` concept 定义不符合 P2300R10
结论：接受，已修复。

理由：原实现过宽会导致非 sender 类型误入约束，破坏算法与类型系统边界。

落地点：
- 已修复：[`reference/execution/include/exec/core/concepts.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/core/concepts.hpp) 中 `concept sender` 现检查 `sender_concept` 嵌套类型且要求派生自 `sender_t`。

### I-4 `receiver` concept 使用标记类型而非嵌套类型检查
结论：接受，已修复。

理由：原实现配合 `get_env` 的宽松 fallback 会导致 receiver 约束形同虚设。

落地点：
- 已修复：[`reference/execution/include/exec/core/concepts.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/core/concepts.hpp) 中 `concept receiver` 现检查 `receiver_concept` 且要求派生自 `receiver_t`。

### I-5 `receiver_of` concept 未实现
结论：接受，已修复（MVP 覆盖 set_value/set_error/set_stopped）。

理由：`receiver_of` 是 sender/receiver 类型系统的核心约束；缺失会让 `connect`/算法组合失真。

落地点：
- 已修复：[`reference/execution/include/exec/core/concepts.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/core/concepts.hpp) 实现了 `receiver_of_impl<R, completion_signatures<...>>`，并要求完成操作为 `nothrow_tag_invocable`。

### I-6 `inplace_stop_source` 关键成员函数缺少定义
结论：接受，已修复（以“正确性优先、mutex 保护链表”的 MVP 实现）。

理由：缺定义会导致链接失败，直接阻断 stop token 相关功能与测试。

落地点：
- 已修复：[`reference/execution/include/exec/core/stop_token.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/core/stop_token.hpp) 已提供
  - `~inplace_stop_source()` / `request_stop()` / `try_add_callback()` / `remove_callback()` 的 `inline` 定义。
- TODO（性能/一致性）：文件注释仍称 “lock-free”，但当前实现使用 `std::mutex`；后续应统一文档口径或替换为 lock-free 算法。

### I-7 `sync_wait` 硬编码空值类型，无法处理带值 sender
结论：接受，已修复（MVP：支持“至多一个 set_value 签名”，但该签名可携带多个值）。

理由：硬编码 `tuple<>` 会导致类型与行为都错误，且测试无法成立。

落地点：
- 已修复：[`reference/execution/include/exec/algorithms/sync_wait.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/algorithms/sync_wait.hpp)
  - 从 `get_completion_signatures` 推导 value tuple；
  - `static_assert` 限制 `set_value` 签名数量（MVP 约束）；
  - 返回 `optional<tuple<Vs...>>`，stopped 返回 `nullopt`，error rethrow。

### I-8 `sync_wait` 未使用 `run_loop` 驱动工作
结论：暂缓（按 MVP 接受阻塞实现，但要求注释与行为一致）。

理由：
- `run_loop`/调度回调用线程通常与更完整的 scheduler/算法族（如 `continues_on/start_on`）一起推进。
- 当前阶段可先用 `mutex+cv` 保证可用性，但必须避免“声称有 run_loop、实际没有”的误导。

落地点：
- 已修复（文档一致性）：[`reference/execution/include/exec/algorithms/sync_wait.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/algorithms/sync_wait.hpp) 注释已明确 MVP 使用 `mutex+condition_variable`，`run_loop` 作为后续可叠加实现。
- TODO（完整语义）：补齐 `run_loop` 与 scheduler 注入（目录建议：`reference/execution/include/exec/contexts/run_loop.hpp` 或迁移到 backport 目录）。

### I-9 `completion_signatures` 元编程工具不完整
结论：接受，已修复（提供 `value_types_of` / `error_types_of` / `sends_stopped_v`）。

理由：类型推导是 `then` 变换与 `sync_wait` 返回类型的基础设施。

落地点：
- 已修复：[`reference/execution/include/exec/core/completion_signatures.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/core/completion_signatures.hpp) 已补齐元编程实现与公开别名。

### I-10 `then` 的 `get_completion_signatures` 未变换签名（首次出现）
结论：接受，已修复。

理由：不变换会导致下游无法正确推导 `then` 输出值类型，也无法表达 `exception_ptr` error 可能性。

落地点：
- 已修复：[`reference/execution/include/exec/algorithms/then.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/algorithms/then.hpp)
  - 将 `set_value_t(Vs...)` 变换为 `set_value_t(R)` 或 `set_value_t()`（void）；
  - 合并并去重签名；
  - 补充 `set_error_t(std::exception_ptr)`。

### I-11 `then` 的 pipe operator 模板参数推导有误（首次出现）
结论：接受，已修复（以“全局 sender adaptor 管道”作为 MVP）。

理由：原 `operator|` 形态确实不可推导/不可用。

落地点：
- 已修复：[`reference/execution/include/exec/core/concepts.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/core/concepts.hpp) 在全局命名空间提供 `operator|(sender, adaptor)`，要求 `adaptor(sender)` 返回 sender（MVP closure 机制）。

### I-12 `inline_scheduler` 的 sender 未携带 scheduler 信息（首次出现）
结论：接受，已修复。

理由：`schedule()` 返回 sender 的环境应能查询到其 completion scheduler，否则后续算法（如 `continues_on`）无法建立正确语义。

落地点：
- 已修复：[`reference/execution/include/exec/contexts/inline_scheduler.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/contexts/inline_scheduler.hpp)
  - sender 持有 `inline_scheduler*`；
  - `get_env(sender)` 返回可 `get_scheduler` 的 env。

### I-13 `set_value`/`set_error`/`set_stopped` 未强制 noexcept（首次出现）
结论：接受，已修复（以“CPO 内 static_assert + receiver_of 中 nothrow 约束”强制）。

理由：noexcept 是 receiver contract 的关键部分；否则 `start()` 等路径可能在异常传播上产生未定义/terminate 风险。

落地点：
- 已修复：[`reference/execution/include/exec/core/completion_signatures.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/core/completion_signatures.hpp) 中 `set_value/set_error/set_stopped` 的 `operator()` 为 `noexcept`，并 `static_assert(nothrow_tag_invocable<...>)`。
- 已修复：[`reference/execution/include/exec/core/concepts.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/core/concepts.hpp) 的 `receiver_of` 也要求 `nothrow_tag_invocable`。

### I-14 `config.hpp` 硬编码 Clang >= 18 限制（首次出现）
结论：接受，已修复。

理由：作为 backport/参考实现，不应硬编码特定编译器版本；应尽量以语言/特性检测为主。

落地点：
- 已修复：[`reference/execution/include/exec/core/config.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/core/config.hpp) 已移除 Clang 限制与大量宏，当前仅保留 C++20 最低门槛（注：若 Forge 统一要求 C++23，可在“正式 backport”阶段再收紧）。

### I-15 构建系统使用 Zig 而非 CMake（首次出现）
结论：暂缓（`reference/` 允许继续用 Zig，但若要并入 Forge backport 必须补 CMake）。

理由：
- `reference/execution/` 作为独立参考实现可以保留 Zig。
- Forge 主项目的 backport 注入与 CI/ctest 都依赖 CMake；要纳入主线必须提供 CMake 入口与测试 target。

落地点：
- TODO：为 `reference/execution/` 增加 CMake 构建/测试入口，或将其实现迁移到 Forge 现有 `backport/` 体系并由顶层 CMake 驱动。

### I-16 非 header-only 架构：`thread_pool.cpp` 和 `io_uring_scheduler.cpp` 为编译单元（首次出现）
结论：暂缓（需要与 Forge 集成形态一起定夺）。

理由：
- Forge 现有 backport 更偏 header-only 注入；引入 `.cpp` 需要额外链接步骤与开关管理。
- contexts（thread_pool / io_uring）也可以作为“可选链接库”，不必强行与 header-only backport 绑死。

落地点：
- TODO：明确策略
  - 要么 contexts 拆为可选库 target，并在 backport 注入层只提供 header-only 的核心 sender/receiver/算法；
  - 要么将 contexts 也 header-only 化（Linux/io_uring 等会更复杂）。

### I-17 设计文档规划的大量组件未实现（首次出现，信息性）
结论：接受（信息性），暂缓落地实现，但应收敛文档与里程碑表述。

理由：报告表格基本客观；当前实现覆盖面有限，应明确 MVP 范围与后续阶段，避免误导“已实现 P2300”。

落地点：
- TODO：更新 [`reference/execution/stdexec-design-plan.md`](/home/i/projects/forward/ccforge/reference/execution/stdexec-design-plan.md) 的阶段划分与“当前完成度”。

---
## 报告后半部分的重复/补充条目（按报告原编号回应）

### I-10 `then` 算法的 `completion_signatures` 转换未实现（重复）
结论：同 I-10（已修复，见 [`then.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/algorithms/then.hpp)）。

### I-11 `then` 的 pipe operator 模板参数推导有误（重复）
结论：同 I-11（已修复，见全局 `operator|`：[`concepts.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/core/concepts.hpp)）。

### I-12 `inline_scheduler` 的 sender 未携带 scheduler 信息（重复）
结论：同 I-12（已修复，见 [`inline_scheduler.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/contexts/inline_scheduler.hpp)）。

### I-13 `inline_scheduler::_inline::env` 存储裸指针且未被使用（补充条目，与“首次出现 I-13”编号冲突）
结论：接受，已修复。

理由：原实现存在“定义了 env/get_scheduler，但 sender 从不返回该 env”的死代码/误导结构。

落地点：
- 已修复：[`reference/execution/include/exec/contexts/inline_scheduler.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/contexts/inline_scheduler.hpp) 现在 sender 的 `get_env` 返回该 env，并由 sender 持有 scheduler 指针。

### I-14 `get_env` CPO 的 fallback 过于宽松（补充条目，与“首次出现 I-14(config.hpp)”编号冲突）
结论：接受，但暂缓（TODO）。

理由：
- 目前 `get_env(const auto&) -> empty_env` 的兜底会掩盖“缺少 get_env 定制”的错误来源，降低诊断质量。
- 虽然 `receiver`/`sender` 概念已收紧（不再仅靠 `get_env` 判定），但 `get_env` 过宽仍不利于按标准建模与错误定位。

落地点：
- TODO：收敛 [`reference/execution/include/exec/core/env.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/core/env.hpp) 的 fallback：
  - 要么移除对任意类型的 fallback，仅对明确类型（如 `empty_env`）提供默认；
  - 要么把 fallback 限制在“已满足 receiver/sender 概念的类型”范围内（避免完全吞错）。

### I-15 设计文档规划的大量组件未实现（补充条目，与“首次出现 I-15(build system)”编号冲突）
结论：同 I-17（信息性接受；TODO 更新文档与里程碑）。

### I-16 构建系统使用 Zig 而非 CMake，与主项目不一致（补充条目，与“首次出现 I-16(.cpp 架构)”编号冲突）
结论：同 I-15（暂缓；若纳入 Forge backport 必须补 CMake）。

### I-17 `config.hpp` 硬编码 Clang >= 18 限制（补充条目，与“首次出现 I-17(设计文档)”编号冲突）
结论：同 I-14（已修复，见 [`config.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/core/config.hpp)）。

### I-18 非标准宏定义污染用户命名空间
结论：接受，已修复。

理由：公共头中泄漏宏会污染用户命名空间；backport 注入时尤其敏感。

落地点：
- 已修复：[`reference/execution/include/exec/core/config.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/core/config.hpp) 已移除 `EXEC_*` 宏与断言宏（保留最小语言版本 guard）。

### I-19 `just` 的 `operation::start` 中 lambda 的 noexcept 标注不正确
结论：驳斥（在当前 receiver contract 已强制 noexcept 的前提下不是问题）。

理由：
- `set_value/set_error/set_stopped` 的 CPO 在调用处 `noexcept`，并通过 `static_assert(nothrow_tag_invocable<...>)` 强制 receiver 完成操作不抛异常。
- `receiver_of` 也要求 nothrow，可在编译期拒绝不满足 contract 的 receiver。
- 因此 `just` 内部 `start` 路径的 `noexcept` 标注与 contract 一致，不构成“错误标注”。

落地点：
- 已通过 contract 收敛解决根因：[`completion_signatures.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/core/completion_signatures.hpp)、[`concepts.hpp`](/home/i/projects/forward/ccforge/reference/execution/include/exec/core/concepts.hpp)。
- 备注：若后续决定允许非 noexcept 的 receiver（不推荐），才需要回头调整该处 noexcept。

---

## 第二部分：测试问题（T-*）

### T-1 所有测试使用自定义测试宏而非 Google Test
结论：接受，已修复。

落地点：
- 已修复：测试已融入当前 `test/` 体系（gtest + CTest），并通过 `<execution>` wrapper 注入形态覆盖：
  - [`test/test_execution_mvp.cpp`](/home/i/projects/forward/ccforge/test/test_execution_mvp.cpp)
  - [`test/test_execution_inline_scheduler.cpp`](/home/i/projects/forward/ccforge/test/test_execution_inline_scheduler.cpp)
  - [`test/test_execution_stop_token.cpp`](/home/i/projects/forward/ccforge/test/test_execution_stop_token.cpp)

### T-2 `test_just_multiple_values` 被注释掉，未实际验证
结论：接受，已修复。

落地点：
- 已修复：[`test/test_execution_mvp.cpp`](/home/i/projects/forward/ccforge/test/test_execution_mvp.cpp) 覆盖 `just(1, string)` 的 `sync_wait` 结果断言。

### T-3 `test_when_all.cpp` 和 `test_thread_pool.cpp` 为空壳占位
结论：接受，暂缓（TODO）。

理由：占位文件可以存在，但不能进入“默认测试通过”的路径，且不应以自带 `main()` 的方式污染统一测试可执行的链接结构。

落地点：
- TODO：在 backport 体系补齐 `when_all` / `static_thread_pool` 后再加入对应测试；在此之前不提供“占位但会绿”的测试用例。

### T-4 构建系统将所有测试编译为单一可执行文件
结论：接受，但已通过“融入顶层 CMake/CTest”规避该类问题；Zig 自建构建脚本暂不维护。

理由：草案目录的 `build.zig` 不是 Forge 的最终交付形态（且 `reference/` 最终移除）。本轮以“融入现有 CMake/CTest 测试体系”为准。

落地点：
- 已修复（Forge 集成）：执行相关测试已纳入顶层 `ctest`（见 `test/` 新增用例）。
- TODO（Zig build.zig）：若未来需要维护独立 Zig 构建系统，再按 gtest 组织方式重建。

### T-5 缺少 `inline_scheduler` 的测试
结论：接受，已修复（基础覆盖）。

落地点：
- 已修复：[`test/test_execution_inline_scheduler.cpp`](/home/i/projects/forward/ccforge/test/test_execution_inline_scheduler.cpp) 覆盖了：
  - `scheduler` concept 静态断言
  - `schedule -> sync_wait` 基本行为
  - `schedule | then` 的基本链式行为

### T-6 缺少 `receiver` concept 的编译探测
结论：部分接受（已有正例静态断言，但缺少系统性反例/更全面探测）。

落地点：
- 已部分修复：[`test/test_execution_inline_scheduler.cpp`](/home/i/projects/forward/ccforge/test/test_execution_inline_scheduler.cpp) 有 `static_assert(std::execution::scheduler<std::execution::inline_scheduler>)`。
- TODO：补充 compile-probe（正/反例）覆盖 `sender/receiver/receiver_of/operation_state` 的约束边界（建议新增 `tests/test_concepts.cpp`）。

### T-7 `stop_token` 测试无法链接
结论：接受，已修复。

落地点：
- 已修复实现：[`backport/cpp26/execution.hpp`](/home/i/projects/forward/ccforge/backport/cpp26/execution.hpp) 内提供 `std::inplace_stop_source/token/callback` 的 MVP 定义。
- 已有 gtest 测试：[`test/test_execution_stop_token.cpp`](/home/i/projects/forward/ccforge/test/test_execution_stop_token.cpp)。

### T-8 `sync_wait` 测试的断言基于错误的类型假设
结论：接受，已修复（随 I-7 修复同步生效）。

落地点：
- 已修复：[`test/test_execution_mvp.cpp`](/home/i/projects/forward/ccforge/test/test_execution_mvp.cpp) 已按 `optional<tuple<...>>` 断言。

### T-9 `then` 测试未覆盖异常传播
结论：接受，已修复（以 “sync_wait rethrow” 作为可观测行为）。

落地点：
- 已修复：[`test/test_execution_mvp.cpp`](/home/i/projects/forward/ccforge/test/test_execution_mvp.cpp) 覆盖异常传播路径。

### T-10 `then` 测试未覆盖 error/stopped 透传
结论：接受，已修复。

落地点：
- 已补齐 API：[`backport/cpp26/execution.hpp`](/home/i/projects/forward/ccforge/backport/cpp26/execution.hpp) 提供 `just_error` / `just_stopped`。
- 已补齐测试：[`test/test_execution_mvp.cpp`](/home/i/projects/forward/ccforge/test/test_execution_mvp.cpp) 覆盖 error/stopped 透传且不执行回调。

### T-11 缺少 `env` 和 `prop` 机制的测试
结论：部分接受。

理由：
- 当前已有间接覆盖（`sync_wait` receiver env 暴露 stop_token；inline_scheduler sender env 可查询 scheduler）。
- 但对 `make_prop/make_env/get_stop_token/get_allocator` 等的组合行为缺少直接、聚焦的单测。

落地点：
- 已有间接覆盖：
  - stop token：[`test/test_execution_stop_token.cpp`](/home/i/projects/forward/ccforge/test/test_execution_stop_token.cpp)
  - scheduler 基本链式：[`test/test_execution_inline_scheduler.cpp`](/home/i/projects/forward/ccforge/test/test_execution_inline_scheduler.cpp)
- TODO：新增专门测试（建议 `tests/test_env.cpp`）覆盖 `make_env(make_prop(...), ...)` 的查询分发与 fallback 语义。

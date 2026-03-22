# Forge C++26 Full Backport Plan

## TL;DR

> **Quick Summary**: 完成 Forge 项目的四个 C++26 backport 目标——将 `std::execution` 从 MVP 推进到 Phase 1，并从零实现 `std::linalg` 全部 BLAS Level 1/2/3。
> 
> **Deliverables**:
> - `std::execution` Phase 1 完成（run_loop, let_*, upon_*, starts_on/continues_on 等）
> - `std::linalg` Phase 0-3 完成（辅助组件 + BLAS Level 1/2/3）
> - forge.cmake 检测 + 4 工具链验证
> 
> **Estimated Effort**: XL（预计 25-35 个独立任务）
> **Parallel Execution**: YES - 4 waves
> **Critical Path**: run_loop → sync_wait 重写 → let_* → linalg Phase 2/3

---

## Context

### Original Request
用户要求从核心动机出发（允许使用最新 gcc/clang/zig 的下游用户使用已进入标准但未正式进入标准库的实现），实现一个允许未来标准正式落地后用户完全无感的标准 backport。四个主力目标：unique_resource（✅完成）、simd（✅完成）、execution（MVP→Phase 1）、linalg（全新实现）。

### Interview Summary
**Key Discussions**:
- submdspan 作为 linalg 内部基础设施保留，不作为独立公开目标
- execution 优先级高于 linalg
- 计划需要能自行迭代到完成，不反复要求人类启动
- 细粒度提交 + 恰当时机推送

**Research Findings**:
- NVIDIA stdexec: 成员函数优先 CPO dispatch，run_loop 使用 intrusive task queue
- kokkos/stdBLAS: 三重载模式（inline_exec/policy/default），accessor wrapping
- kokkos/mdspan: standalone submdspan 实现（已作为 Forge 参考）

### Metis Review
**Identified Gaps** (addressed):
- tag_invoke 迁移策略：保留现有 MVP 类型 tag_invoke，新类型用成员函数
- run_loop 线程安全：使用 mutex+cv（非 atomic::wait），确保 zig/libc++ 兼容
- linalg execution policy：先纯串行，解耦 linalg 与 execution
- sync_wait 重写时机：实现 run_loop 后立即重写
- 单文件大小：execution.hpp 超过 ~2500 LOC 时拆分为内部 include

---

## Work Objectives

### Core Objective
将 Forge 的 `std::execution` 从 MVP 推进到 Phase 1（覆盖核心适配器和 run_loop），并从零实现完整的 `std::linalg`（BLAS Level 1/2/3），所有 backport 遵循无感过渡原则。

### Concrete Deliverables
- `backport/cpp26/execution.hpp`（或拆分为 `execution/*.hpp`）— Phase 1 完成
- `backport/linalg` — wrapper header
- `backport/cpp26/linalg.hpp`（或拆分为 `linalg/*.hpp`）— 完整实现
- `forge.cmake` — 新增 linalg 检测
- `test/execution/` — Phase 1 测试
- `test/linalg/` — Level 1/2/3 测试

### Definition of Done
- [ ] `cmake --build build-gcc -j && ctest --test-dir build-gcc --output-on-failure` 全部通过
- [ ] `CC="zig cc" CXX="zig c++"` 构建 + 测试全部通过
- [ ] Podman Clang + Podman GCC 全部通过
- [ ] 所有新增符号在 `namespace std::execution` 或 `namespace std::linalg` 中
- [ ] 当标准库原生支持时，backport 自动禁用（通过 feature-test macro 守卫）

### Must Have
- run_loop 实现（sync_wait 的标准驱动）
- let_value / let_error / let_stopped
- upon_error / upon_stopped
- starts_on / continues_on
- stopped_as_optional / stopped_as_error
- read_env
- get_completion_scheduler CPO
- enable_sender trait
- linalg 辅助组件（scaled_accessor, conjugated_accessor, layout_transpose 等）
- linalg BLAS Level 1 全部 12 个操作
- linalg BLAS Level 2 核心操作
- linalg BLAS Level 3 核心操作

### Must NOT Have (Guardrails)
- ❌ 不实现 when_all, split, ensure_started, bulk（execution Phase 2+）
- ❌ 不实现 coroutine/awaitable sender 支持
- ❌ 不链接系统 BLAS——纯 C++ 模板实现
- ❌ 不实现 linalg execution policy 重载（Phase 4）
- ❌ 不修改已完成的 unique_resource / simd backport
- ❌ 不向 `namespace std` 注入非标准扩展 API
- ❌ 不使用 `std::atomic::wait/notify_one`（可移植性风险）

---

## Verification Strategy

> **ZERO HUMAN INTERVENTION** — ALL verification is agent-executed.

### Test Decision
- **Infrastructure exists**: YES (GTest + compile probes)
- **Automated tests**: YES (Tests-after, 每个任务包含测试)
- **Framework**: GTest (gtest_main)

### QA Policy
Every task MUST include agent-executed QA scenarios.
Evidence saved to `.sisyphus/evidence/task-{N}-{scenario-slug}.{ext}`.

- **C++ Library**: Use Bash (cmake + ctest) — Build, run tests, assert pass
- **Compile Probes**: Use Bash (cmake --build --target) — Build specific probe target
- **Multi-toolchain**: Use Bash (GCC + Zig + Podman) — Full verification at phase boundaries

---

## Execution Strategy

### Parallel Execution Waves

```
Wave 1 (Start Immediately — execution foundations):
├── Task 1: get_completion_scheduler CPO + enable_sender trait [quick]
├── Task 2: execution.hpp file splitting infrastructure [quick]
├── Task 3: run_loop implementation (intrusive queue + scheduler) [deep]
└── Task 4: run_loop tests (single-thread + multi-thread) [unspecified-high]

Wave 2 (After Wave 1 — execution core adaptors + linalg skeleton):
├── Task 5: sync_wait rewrite to use run_loop [deep]
├── Task 6: read_env sender factory [quick]
├── Task 7: upon_error + upon_stopped [unspecified-high]
├── Task 8: forge.cmake linalg detection + backport/linalg wrapper [quick]
├── Task 9: linalg auxiliary types (tags, layout_blas_packed) [unspecified-high]
└── Task 10: linalg accessor wrappers (scaled, conjugated, transposed) [unspecified-high]

Wave 3 (After Wave 2 — execution complex adaptors + linalg Level 1):
├── Task 11: let_value implementation [deep]
├── Task 12: let_error + let_stopped [unspecified-high]
├── Task 13: starts_on + continues_on [unspecified-high]
├── Task 14: stopped_as_optional + stopped_as_error [unspecified-high]
├── Task 15: sender_adaptor_closure CRTP base [quick]
├── Task 16: linalg BLAS Level 1 — basic ops (copy, scale, swap, add) [unspecified-high]
├── Task 17: linalg BLAS Level 1 — dot products (dot, dotc) [unspecified-high]
└── Task 18: linalg BLAS Level 1 — norms + reductions (nrm2, asum, iamax, givens) [unspecified-high]

Wave 4 (After Wave 3 — linalg Level 2/3 + final verification):
├── Task 19: linalg BLAS Level 2 — gemv (matrix_vector_product) [deep]
├── Task 20: linalg BLAS Level 2 — triangular (trmv, trsv) [unspecified-high]
├── Task 21: linalg BLAS Level 2 — symmetric/hermitian + rank updates [unspecified-high]
├── Task 22: linalg BLAS Level 3 — gemm (matrix_product) [deep]
├── Task 23: linalg BLAS Level 3 — triangular (trmm, trsm) [unspecified-high]
├── Task 24: linalg BLAS Level 3 — symmetric/hermitian + rank-k updates [unspecified-high]
└── Task 25: execution Phase 1 known-deviation doc update [quick]

Wave FINAL (After ALL tasks — 4 parallel reviews, then user okay):
├── Task F1: Plan compliance audit (oracle)
├── Task F2: Code quality review (unspecified-high)
├── Task F3: Real manual QA — full 4-toolchain verification (unspecified-high)
└── Task F4: Scope fidelity check (deep)
-> Present results -> Get explicit user okay
```

### Dependency Matrix

| Task | Depends On | Blocks |
|------|-----------|--------|
| 1 | — | 5, 6, 7 |
| 2 | — | 3, 11 |
| 3 | 2 | 4, 5 |
| 4 | 3 | 5 |
| 5 | 3, 4 | 11, 13, 14 |
| 6 | 1 | — |
| 7 | 1 | 14 |
| 8 | — | 9, 10, 16 |
| 9 | 8 | 16, 19 |
| 10 | 8 | 16, 19 |
| 11 | 5 | 12, 14 |
| 12 | 11 | — |
| 13 | 5 | — |
| 14 | 7, 11 | — |
| 15 | 7, 11 | — |
| 16 | 9, 10 | 17, 18, 19 |
| 17 | 16 | 19 |
| 18 | 16 | 19 |
| 19 | 17, 18 | 20, 21 |
| 20 | 19 | 22 |
| 21 | 19 | 22 |
| 22 | 20, 21 | 23, 24 |
| 23 | 22 | — |
| 24 | 22 | — |
| 25 | 15 | — |

### Agent Dispatch Summary

- **Wave 1**: 4 tasks — T1→`quick`, T2→`quick`, T3→`deep`, T4→`unspecified-high`
- **Wave 2**: 6 tasks — T5→`deep`, T6→`quick`, T7→`unspecified-high`, T8→`quick`, T9→`unspecified-high`, T10→`unspecified-high`
- **Wave 3**: 8 tasks — T11→`deep`, T12→`unspecified-high`, T13→`unspecified-high`, T14→`unspecified-high`, T15→`quick`, T16→`unspecified-high`, T17→`unspecified-high`, T18→`unspecified-high`
- **Wave 4**: 7 tasks — T19→`deep`, T20→`unspecified-high`, T21→`unspecified-high`, T22→`deep`, T23→`unspecified-high`, T24→`unspecified-high`, T25→`quick`
- **FINAL**: 4 tasks — F1→`oracle`, F2→`unspecified-high`, F3→`unspecified-high`, F4→`deep`

---

## TODOs

- [x] 1. get_completion_scheduler CPO + enable_sender trait

  **What to do**:
  - 定义 `get_completion_scheduler<CPO>` query CPO（`[exec.getcomplsched]`）
  - 定义 `enable_sender<T>` trait（`[exec.snd]`），默认检查 `sender_concept` 嵌套类型
  - 更新 `sender` concept 使用 `enable_sender` 而非直接检查 `sender_concept`
  - 为 `just` / `just_error` / `just_stopped` sender 的 env 添加 completion scheduler 返回
  - 添加正/反例编译探针

  **Must NOT do**:
  - 不实现 awaitable sender 的 enable_sender 特化（Phase 2）
  - 不实现 domain-based dispatch

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: 约 100-150 LOC 的 trait/CPO 定义，模式已有先例
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1 (with Tasks 2, 3)
  - **Blocks**: Tasks 5, 6, 7
  - **Blocked By**: None

  **References**:
  - `backport/cpp26/execution.hpp:368-377` — get_scheduler_t CPO 模式（复制此模式为 get_completion_scheduler）
  - `backport/cpp26/execution.hpp:707-715` — 当前 sender concept 定义（需修改为使用 enable_sender）
  - `https://eel.is/c++draft/exec#getcomplsched` — 标准 get_completion_scheduler 定义
  - `https://eel.is/c++draft/exec#snd` — 标准 enable_sender trait 定义

  **Acceptance Criteria**:

  ```
  Scenario: get_completion_scheduler 正确查询 just sender env
    Tool: Bash (cmake + ctest)
    Steps:
      1. cmake -S . -B build-t1 -DFORGE_BUILD_TESTS=ON && cmake --build build-t1 -j
      2. ctest --test-dir build-t1 -R execution --output-on-failure
    Expected Result: 全部 execution 测试通过，无回归
    Evidence: .sisyphus/evidence/task-1-completion-scheduler.txt

  Scenario: enable_sender trait 正反例
    Tool: Bash (cmake + ctest)
    Steps:
      1. 新增 static_assert(sender<just_sender>) 正例
      2. 新增 static_assert(!sender<non_sender_type>) 反例（无 sender_concept 且 enable_sender 未特化）
      3. 构建并运行测试
    Expected Result: 编译通过，static_assert 正确
    Evidence: .sisyphus/evidence/task-1-enable-sender-probe.txt
  ```

  **Commit**: YES
  - Message: `feat(execution): add get_completion_scheduler CPO and enable_sender trait`
  - Files: `backport/cpp26/execution.hpp`, `test/execution/runtime/test_execution_mvp.cpp`
  - Pre-commit: `cmake --build build-t1 -j && ctest --test-dir build-t1 -R execution --output-on-failure`

- [x] 2. execution.hpp 文件拆分基础设施

  **What to do**:
  - 将 `backport/cpp26/execution.hpp` 拆分为模块化内部头文件
  - 创建 `backport/cpp26/execution/` 目录
  - 拆分为：`stop_token.hpp`（stop token 相关）、`concepts.hpp`（核心概念）、`env.hpp`（环境查询）、`just.hpp`（just 工厂）、`then.hpp`（then 适配器）、`sync_wait.hpp`（sync_wait）、`inline_scheduler.hpp`
  - `execution.hpp` 变为协调头文件，按序 include 各子文件
  - 确保拆分后所有现有测试不变、不回归

  **Must NOT do**:
  - 不改变任何公开 API
  - 不改变命名空间结构
  - 不引入新功能

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: 纯机械拆分，不改变逻辑
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1 (with Tasks 1, 3)
  - **Blocks**: Tasks 3, 11
  - **Blocked By**: None

  **References**:
  - `backport/cpp26/execution.hpp` — 当前 1325 行单文件，需拆分
  - `backport/cpp26/simd/` — simd 已有的多文件拆分模式（base.hpp, types.hpp 等）
  - `backport/cpp26/simd.hpp` — simd 的协调头文件模式

  **Acceptance Criteria**:

  ```
  Scenario: 拆分后全部测试通过
    Tool: Bash (cmake + ctest)
    Steps:
      1. cmake -S . -B build-t2 -DFORGE_BUILD_TESTS=ON && cmake --build build-t2 -j
      2. ctest --test-dir build-t2 --output-on-failure
    Expected Result: 全部测试通过，与拆分前数量一致
    Evidence: .sisyphus/evidence/task-2-split-verify.txt

  Scenario: 无功能变更验证
    Tool: Bash (diff)
    Steps:
      1. 对比拆分前后的预处理输出（cpp -E），确认符号集一致
    Expected Result: 公开符号集无变化
    Evidence: .sisyphus/evidence/task-2-symbol-diff.txt
  ```

  **Commit**: YES
  - Message: `refactor(execution): split execution.hpp into modular internal headers`
  - Files: `backport/cpp26/execution.hpp`, `backport/cpp26/execution/*.hpp`
  - Pre-commit: `cmake --build build-t2 -j && ctest --test-dir build-t2 --output-on-failure`

- [x] 3. run_loop 实现

  **What to do**:
  - 实现 `std::execution::run_loop`（`[exec.run.loop]`）
  - 使用 mutex+cv 的 intrusive task queue（不用 atomic::wait，确保 zig/libc++ 兼容）
  - 实现 `run_loop::scheduler` 类型（满足 `scheduler` concept）
  - 实现 `run_loop::run()`、`run_loop::finish()`、`run_loop::get_scheduler()`
  - scheduler 的 `schedule()` 返回的 sender connect 后产生的 operation_state，start 时入队 run_loop
  - run_loop 析构时若有挂起任务调用 `std::terminate()`
  - 放置在 `backport/cpp26/execution/run_loop.hpp`

  **Must NOT do**:
  - 不使用 `std::atomic<T>::wait()` / `notify_one()`
  - 不实现线程池（run_loop 是单线程事件循环）
  - run_loop::run() 不支持多线程调用（UB per spec）

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: 线程安全的 intrusive queue + 状态机，需要仔细处理 finish/run 交互
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES (with Task 1)
  - **Parallel Group**: Wave 1
  - **Blocks**: Tasks 4, 5
  - **Blocked By**: Task 2 (需要拆分后的文件结构)

  **References**:
  - `https://eel.is/c++draft/exec#run.loop` — 标准 run_loop 规范
  - NVIDIA stdexec `__run_loop.hpp` — intrusive queue + 状态机参考（研究报告中已分析）
  - `backport/cpp26/execution.hpp:85-172` — inplace_stop_source 的 mutex+callback_list 模式（可复用 intrusive list 思路）
  - `backport/cpp26/execution.hpp:1266-1323` — inline_scheduler 模式（scheduler+sender+operation 三件套）

  **Acceptance Criteria**:

  ```
  Scenario: run_loop 基本执行流
    Tool: Bash (cmake + ctest)
    Steps:
      1. 创建 run_loop，获取 scheduler
      2. schedule(sch) | then([] { return 42; }) 连接并 start
      3. 另一线程调用 finish()，主线程调用 run()
      4. 验证 receiver 收到 set_value(42)
    Expected Result: 正确执行并完成
    Evidence: .sisyphus/evidence/task-3-run-loop-basic.txt

  Scenario: run_loop finish 后 run 返回
    Tool: Bash (cmake + ctest)
    Steps:
      1. 创建 run_loop，不入队任何任务
      2. 调用 finish()
      3. 调用 run()
      4. 验证 run() 立即返回
    Expected Result: run() 不阻塞
    Evidence: .sisyphus/evidence/task-3-run-loop-finish.txt

  Scenario: run_loop 空析构安全
    Tool: Bash (cmake + ctest)
    Steps:
      1. 创建 run_loop，不使用
      2. 析构
    Expected Result: 不 terminate，正常析构
    Evidence: .sisyphus/evidence/task-3-run-loop-dtor.txt
  ```

  **Commit**: YES
  - Message: `feat(execution): implement run_loop with intrusive task queue`
  - Files: `backport/cpp26/execution/run_loop.hpp`, `backport/cpp26/execution.hpp`
  - Pre-commit: `cmake --build build-t3 -j && ctest --test-dir build-t3 -R execution --output-on-failure`

- [x] 4. run_loop 测试（含多线程）

  **What to do**:
  - 为 run_loop 编写完整的 GTest runtime 测试
  - 测试场景：单任务执行、多任务队列、finish 后 run 返回、stop_token 集成、scheduler concept 满足性
  - 多线程测试：从 4 个线程并发 push 任务到 run_loop（通过 schedule+start），主线程 run()
  - 编译探针：`static_assert(scheduler<run_loop::scheduler>)`

  **Must NOT do**:
  - 不测试多线程 run()（UB）
  - 不依赖 TSAN（可选增强，非必须）

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: 多线程测试需要仔细设计同步
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: NO
  - **Parallel Group**: Sequential after Task 3
  - **Blocks**: Task 5
  - **Blocked By**: Task 3

  **References**:
  - `test/execution/runtime/test_execution_mvp.cpp` — 现有测试模式
  - `test/execution/runtime/test_execution_inline_scheduler.cpp` — scheduler 测试模式

  **Acceptance Criteria**:

  ```
  Scenario: 全部 run_loop 测试通过
    Tool: Bash (cmake + ctest)
    Steps:
      1. cmake --build build-t4 -j
      2. ctest --test-dir build-t4 -R run_loop --output-on-failure
    Expected Result: 全部通过
    Evidence: .sisyphus/evidence/task-4-run-loop-tests.txt

  Scenario: 多线程推送不崩溃
    Tool: Bash (cmake + ctest)
    Steps:
      1. 运行 ConcurrentPush 测试 10 次
    Expected Result: 0 次失败
    Evidence: .sisyphus/evidence/task-4-concurrent-push.txt
  ```

  **Commit**: YES
  - Message: `test(execution): add run_loop runtime and thread-safety tests`
  - Files: `test/execution/runtime/test_execution_run_loop.cpp`, `test/execution/CMakeLists.txt`
  - Pre-commit: `ctest --test-dir build-t4 -R execution --output-on-failure`

- [x] 5. sync_wait 重写（使用 run_loop）

  **What to do**:
  - 重写 `sync_wait` 实现，使用 `run_loop` 作为内部驱动（`[exec.sync.wait]`）
  - sync_wait receiver 的 env 中提供 `get_scheduler` → run_loop::scheduler
  - sync_wait receiver 的 env 中提供 `get_delegatee_scheduler` → run_loop::scheduler
  - 移除旧的 mutex+cv 实现
  - 确保 `std::this_thread::sync_wait` 和 `std::execution::sync_wait` 两条调用路径仍可用
  - 所有现有 sync_wait 测试必须继续通过

  **Must NOT do**:
  - 不改变 sync_wait 的公开 API 签名
  - 不改变返回类型

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: 需要理解 run_loop 的 run/finish 交互，正确设置 receiver env
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: NO
  - **Parallel Group**: Sequential after Task 4
  - **Blocks**: Tasks 11, 13, 14
  - **Blocked By**: Tasks 3, 4

  **References**:
  - `https://eel.is/c++draft/exec#sync.wait` — 标准 sync_wait 规范
  - `backport/cpp26/execution/sync_wait.hpp`（拆分后）— 当前 mutex+cv 实现
  - `backport/cpp26/execution/run_loop.hpp`（Task 3 产出）— run_loop 实现

  **Acceptance Criteria**:

  ```
  Scenario: 现有 sync_wait 测试无回归
    Tool: Bash (cmake + ctest)
    Steps:
      1. cmake --build build-t5 -j
      2. ctest --test-dir build-t5 -R execution --output-on-failure
    Expected Result: 全部通过
    Evidence: .sisyphus/evidence/task-5-sync-wait-no-regression.txt

  Scenario: sync_wait receiver env 提供 scheduler
    Tool: Bash (cmake + ctest)
    Steps:
      1. 新增测试：在 then 回调中通过 read_env(get_scheduler) 查询 scheduler
      2. 验证返回的 scheduler 是 run_loop::scheduler 类型
    Expected Result: 编译通过，运行正确
    Evidence: .sisyphus/evidence/task-5-sync-wait-env.txt
  ```

  **Commit**: YES
  - Message: `refactor(execution): rewrite sync_wait to use run_loop per [exec.sync.wait]`
  - Files: `backport/cpp26/execution/sync_wait.hpp`
  - Pre-commit: `ctest --test-dir build-t5 -R execution --output-on-failure`

- [x] 6. read_env sender factory

  **What to do**:
  - 实现 `std::execution::read_env(tag)` sender factory（`[exec.read.env]`）
  - `read_env(tag)` 返回一个 sender，connect+start 后从 receiver env 中查询 tag 并以 set_value 发送结果
  - 实现 completion_signatures 推导
  - 支持 pipe operator
  - 编写 runtime 测试 + 编译探针

  **Must NOT do**:
  - 不实现自定义 tag 支持（仅标准 query tag）

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: 简单的 sender factory，模式与 just 类似
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2 (with Tasks 5, 7, 8, 9, 10)
  - **Blocks**: None
  - **Blocked By**: Task 1

  **References**:
  - `https://eel.is/c++draft/exec#read.env` — 标准规范
  - `backport/cpp26/execution/just.hpp`（拆分后）— just sender 模式参考

  **Acceptance Criteria**:

  ```
  Scenario: read_env 读取 stop_token
    Tool: Bash (cmake + ctest)
    Steps:
      1. sync_wait(read_env(get_stop_token))
      2. 验证返回有效的 stop_token
    Expected Result: 返回 inplace_stop_token，stop_possible() == true
    Evidence: .sisyphus/evidence/task-6-read-env.txt
  ```

  **Commit**: YES
  - Message: `feat(execution): implement read_env sender factory`
  - Files: `backport/cpp26/execution/read_env.hpp`, tests
  - Pre-commit: `ctest --test-dir build-t6 -R execution --output-on-failure`

- [x] 7. upon_error + upon_stopped 适配器

  **What to do**:
  - 实现 `upon_error(fn)` — 变换错误完成通道（`[exec.upon]`）
  - 实现 `upon_stopped(fn)` — 变换取消完成通道
  - 复用 then 的实现模式（then 变换 set_value，upon_error 变换 set_error，upon_stopped 变换 set_stopped）
  - completion_signatures 推导
  - pipe operator 支持
  - 编写 runtime 测试 + 编译探针

  **Must NOT do**:
  - 不改变 then 的实现

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: 与 then 结构对称，但 completion signature 变换逻辑不同
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2
  - **Blocks**: Task 14
  - **Blocked By**: Task 1

  **References**:
  - `https://eel.is/c++draft/exec#upon` — 标准规范
  - `backport/cpp26/execution/then.hpp`（拆分后）— then 适配器模式

  **Acceptance Criteria**:

  ```
  Scenario: upon_error 变换错误
    Tool: Bash (cmake + ctest)
    Steps:
      1. just_error(42) | upon_error([](int e) { return e * 2; }) | sync_wait
      2. 验证返回 84
    Expected Result: set_value(84)
    Evidence: .sisyphus/evidence/task-7-upon-error.txt

  Scenario: upon_stopped 变换取消
    Tool: Bash (cmake + ctest)
    Steps:
      1. just_stopped() | upon_stopped([] { return -1; }) | sync_wait
      2. 验证返回 -1
    Expected Result: set_value(-1)
    Evidence: .sisyphus/evidence/task-7-upon-stopped.txt
  ```

  **Commit**: YES
  - Message: `feat(execution): implement upon_error and upon_stopped adaptors`
  - Files: `backport/cpp26/execution/upon.hpp`, tests
  - Pre-commit: `ctest --test-dir build-t7 -R execution --output-on-failure`

- [x] 8. forge.cmake linalg 检测 + backport/linalg wrapper

  **What to do**:
  - 在 `forge.cmake` 添加 `std::linalg` 特性检测（`check_cxx_source_compiles` 检测 `std::linalg::dot`）
  - 创建 `backport/linalg` wrapper header
  - wrapper 使用 `__has_include(<linalg>)` 检测原生头文件是否存在
  - 若不存在，直接提供 backport（无需 `include_next`，因为 `<linalg>` 是新头文件）
  - 若存在，`include_next` 后检查 `__cpp_lib_linalg` 决定是否注入 backport
  - 创建 `backport/cpp26/linalg.hpp` 骨架文件（仅 license + guard + namespace）
  - MSVC 路径处理：在 forge.cmake 中添加 `<linalg>` 路径搜索（优雅处理不存在的情况）

  **Must NOT do**:
  - 不实现任何 linalg 功能（仅骨架）
  - 不要求 `<linalg>` 头文件必须存在

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: 沿用已验证的 forge.cmake + wrapper 模式
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2
  - **Blocks**: Tasks 9, 10, 16
  - **Blocked By**: None

  **References**:
  - `forge.cmake:84-103` — submdspan 检测模式（可复用）
  - `backport/simd` — wrapper header 模式（有/无原生头文件的处理）
  - `backport/execution` — wrapper 的 `__has_include_next` 优雅处理模式
  - `require/02-std-linalg-require.md:283-313` — linalg wrapper 设计

  **Acceptance Criteria**:

  ```
  Scenario: forge.cmake 正确检测 linalg 缺失
    Tool: Bash (cmake)
    Steps:
      1. cmake -S . -B build-t8 -DFORGE_BUILD_TESTS=ON 2>&1 | grep linalg
    Expected Result: 输出 "Forge: std::linalg backport enabled"
    Evidence: .sisyphus/evidence/task-8-linalg-detect.txt

  Scenario: #include <linalg> 编译通过
    Tool: Bash (编译简单测试)
    Steps:
      1. 编写 #include <linalg> 的空 main，构建
    Expected Result: 编译通过（即使 linalg.hpp 还是骨架）
    Evidence: .sisyphus/evidence/task-8-linalg-include.txt
  ```

  **Commit**: YES
  - Message: `feat(linalg): add forge.cmake detection and backport skeleton`
  - Files: `forge.cmake`, `backport/linalg`, `backport/cpp26/linalg.hpp`
  - Pre-commit: `cmake -S . -B build-t8 -DFORGE_BUILD_TESTS=ON`

- [x] 9. linalg 辅助类型（标记类型 + layout_blas_packed）

  **What to do**:
  - 在 `backport/cpp26/linalg.hpp` 中实现 `namespace std::linalg` 下的标记类型：
    - `column_major_t` / `column_major`
    - `row_major_t` / `row_major`
    - `upper_triangle_t` / `upper_triangle`
    - `lower_triangle_t` / `lower_triangle`
    - `implicit_unit_diagonal_t` / `implicit_unit_diagonal`
    - `explicit_diagonal_t` / `explicit_diagonal`
  - 实现 `layout_blas_packed<Triangle, StorageOrder>` layout mapping
  - 编写编译探针验证类型存在性

  **Must NOT do**:
  - 不实现 accessor wrappers（Task 10）
  - 不实现算法函数

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: layout_blas_packed 的 mapping 需要仔细实现
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2
  - **Blocks**: Tasks 16, 19
  - **Blocked By**: Task 8

  **References**:
  - `https://eel.is/c++draft/linalg` — 标准 linalg 规范
  - `require/02-std-linalg-require.md:38-48` — 标记类型清单
  - kokkos/stdBLAS `layout_tags.hpp` — 参考实现中的标记类型

  **Acceptance Criteria**:

  ```
  Scenario: 标记类型编译探针
    Tool: Bash (cmake + ctest)
    Steps:
      1. static_assert(std::is_empty_v<std::linalg::upper_triangle_t>)
      2. 验证所有 6 个标记类型存在且为 empty 类型
    Expected Result: 编译通过
    Evidence: .sisyphus/evidence/task-9-tags.txt
  ```

  **Commit**: YES
  - Message: `feat(linalg): implement tag types and layout_blas_packed`
  - Files: `backport/cpp26/linalg.hpp`
  - Pre-commit: `cmake --build build-t9 -j && ctest --test-dir build-t9 --output-on-failure`

- [x] 10. linalg accessor wrappers（scaled, conjugated, transposed）

  **What to do**:
  - 实现 `scaled_accessor<ScalingFactor, NestedAccessor>`
  - 实现 `conjugated_accessor<NestedAccessor>`
  - 实现 `layout_transpose` layout mapping
  - 实现工具函数：`scaled(alpha, x)`, `conjugated(x)`, `transposed(x)`, `conjugate_transposed(x)`
  - 所有返回带自定义 accessor/layout 的 `std::mdspan`
  - `conjugated_accessor` 对非复数类型应为恒等操作
  - 编写 runtime 测试

  **Must NOT do**:
  - 不实现算法函数
  - 不直接使用 `<complex>`（仅通过 traits 检测）

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: accessor wrapping 需要理解 mdspan accessor 协议
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2
  - **Blocks**: Tasks 16, 19
  - **Blocked By**: Task 8

  **References**:
  - `require/02-std-linalg-require.md:52-60` — accessor 清单
  - kokkos/stdBLAS `scaled.hpp`, `conjugated.hpp`, `transposed.hpp` — 参考实现
  - `backport/cpp26/submdspan.hpp` — mdspan 类型交互模式

  **Acceptance Criteria**:

  ```
  Scenario: scaled 返回正确值
    Tool: Bash (cmake + ctest)
    Steps:
      1. double data[] = {1.0, 2.0, 3.0}; mdspan v(data, 3);
      2. auto sv = std::linalg::scaled(2.0, v);
      3. EXPECT_EQ(sv[0], 2.0); EXPECT_EQ(sv[1], 4.0);
    Expected Result: scaled view 正确乘以标量
    Evidence: .sisyphus/evidence/task-10-scaled.txt

  Scenario: transposed 交换行列
    Tool: Bash (cmake + ctest)
    Steps:
      1. double data[6]; mdspan<double, extents<int,2,3>> m(data);
      2. auto mt = std::linalg::transposed(m);
      3. EXPECT_EQ(mt.extent(0), 3); EXPECT_EQ(mt.extent(1), 2);
    Expected Result: 维度正确交换
    Evidence: .sisyphus/evidence/task-10-transposed.txt
  ```

  **Commit**: YES
  - Message: `feat(linalg): implement accessor wrappers (scaled, conjugated, transposed)`
  - Files: `backport/cpp26/linalg.hpp`, `test/linalg/`
  - Pre-commit: `ctest --test-dir build-t10 -R linalg --output-on-failure`

- [x] 11. let_value 实现

  **What to do**:
  - 实现 `let_value(fn)` 适配器（`[exec.let]`）
  - `fn` 接收前序 sender 的值参数，返回一个新的 sender
  - 需要在 operation state 中存储：fn 调用的参数副本 + 第二 sender 的 connect 结果
  - 使用 `std::variant` 存储第二 operation state（因为 fn 可能返回不同类型的 sender）
  - completion_signatures = 第二 sender 的 completion_signatures ∪ 前序的 error/stopped
  - 第二 receiver 的 env 应 join 前序 receiver 的 env
  - pipe operator 支持

  **Must NOT do**:
  - 不实现 let_error / let_stopped（Task 12）
  - 不支持多值 completion signature（初始版本仅支持单个 set_value 签名）

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: 最复杂的适配器——需要二段式 connect、variant storage、completion signature 合并
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: NO
  - **Parallel Group**: Wave 3
  - **Blocks**: Tasks 12, 14
  - **Blocked By**: Task 5

  **References**:
  - `https://eel.is/c++draft/exec#let` — 标准 let 规范
  - NVIDIA stdexec `__let.hpp` — 参考实现（研究报告中已详细分析）
  - `backport/cpp26/execution/then.hpp` — then 模式（let_value 是 then 的泛化）

  **Acceptance Criteria**:

  ```
  Scenario: let_value 链接新 sender
    Tool: Bash (cmake + ctest)
    Steps:
      1. auto sndr = just(42) | let_value([](int x) { return just(x + 1); });
      2. auto result = sync_wait(std::move(sndr));
      3. EXPECT_EQ(std::get<0>(*result), 43);
    Expected Result: 正确链接
    Evidence: .sisyphus/evidence/task-11-let-value.txt

  Scenario: let_value 错误透传
    Tool: Bash (cmake + ctest)
    Steps:
      1. auto sndr = just_error(42) | let_value([](int) { return just(0); });
      2. 验证 sync_wait 抛出异常（错误直接透传，fn 不被调用）
    Expected Result: 异常抛出，fn 未调用
    Evidence: .sisyphus/evidence/task-11-let-value-error.txt
  ```

  **Commit**: YES
  - Message: `feat(execution): implement let_value adaptor`
  - Files: `backport/cpp26/execution/let.hpp`, tests
  - Pre-commit: `ctest --test-dir build-t11 -R execution --output-on-failure`

- [x] 12. let_error + let_stopped

  **What to do**:
  - 实现 `let_error(fn)` — 在错误完成时链接新 sender
  - 实现 `let_stopped(fn)` — 在取消完成时链接新 sender
  - 复用 let_value 的内部基础设施（参数化 completion tag）
  - 编写 runtime 测试

  **Must NOT do**:
  - 不修改 let_value 的实现

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: 基础设施已由 let_value 建立，这里是参数化扩展
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES (after Task 11)
  - **Parallel Group**: Wave 3
  - **Blocks**: None
  - **Blocked By**: Task 11

  **References**:
  - `backport/cpp26/execution/let.hpp`（Task 11 产出）

  **Acceptance Criteria**:

  ```
  Scenario: let_error 恢复错误
    Tool: Bash (cmake + ctest)
    Steps:
      1. just_error(42) | let_error([](int e) { return just(e * 2); }) | sync_wait
      2. EXPECT_EQ(std::get<0>(*result), 84)
    Expected Result: 错误被恢复为值
    Evidence: .sisyphus/evidence/task-12-let-error.txt
  ```

  **Commit**: YES
  - Message: `feat(execution): implement let_error and let_stopped adaptors`
  - Files: `backport/cpp26/execution/let.hpp`, tests
  - Pre-commit: `ctest --test-dir build-t12 -R execution --output-on-failure`

- [x] 13. starts_on + continues_on

  **What to do**:
  - 实现 `starts_on(sch, sndr)` — 在指定 scheduler 上启动 sender（`[exec.starts.on]`）
  - 实现 `continues_on(sndr, sch)` — 完成后切换到指定 scheduler（`[exec.continues.on]`）
  - starts_on 等价于 `schedule(sch) | let_value([sndr] { return sndr; })`
  - continues_on 在 set_value 时 schedule 到目标 scheduler 再完成
  - pipe operator 支持

  **Must NOT do**:
  - 不实现 domain-based 路由

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: 需要组合 schedule + let_value，但逻辑相对直接
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3
  - **Blocks**: None
  - **Blocked By**: Task 5

  **References**:
  - `https://eel.is/c++draft/exec#starts.on` — 标准规范
  - `https://eel.is/c++draft/exec#continues.on` — 标准规范

  **Acceptance Criteria**:

  ```
  Scenario: starts_on 在指定 scheduler 执行
    Tool: Bash (cmake + ctest)
    Steps:
      1. run_loop loop; auto sch = loop.get_scheduler();
      2. starts_on(sch, just(42)) | then([](int x) { return x; })
      3. 验证执行在 run_loop 上
    Expected Result: 正确执行
    Evidence: .sisyphus/evidence/task-13-starts-on.txt
  ```

  **Commit**: YES
  - Message: `feat(execution): implement starts_on and continues_on`
  - Files: `backport/cpp26/execution/on.hpp`, tests
  - Pre-commit: `ctest --test-dir build-t13 -R execution --output-on-failure`

- [x] 14. stopped_as_optional + stopped_as_error

  **What to do**:
  - 实现 `stopped_as_optional(sndr)` — 将 stopped 映射为 `std::nullopt`（`[exec.stopped.opt]`）
  - 实现 `stopped_as_error(sndr, err)` — 将 stopped 映射为 error（`[exec.stopped.err]`）
  - stopped_as_optional: set_value(v...) → set_value(optional(v...)), set_stopped → set_value(nullopt)
  - stopped_as_error: set_stopped → set_error(err)

  **Must NOT do**:
  - 不处理多值 completion signatures

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: 需要 upon_stopped 的基础，加上 optional wrapping
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3
  - **Blocks**: None
  - **Blocked By**: Tasks 7, 11

  **References**:
  - `https://eel.is/c++draft/exec#stopped.opt` — stopped_as_optional 规范
  - `https://eel.is/c++draft/exec#stopped.err` — stopped_as_error 规范

  **Acceptance Criteria**:

  ```
  Scenario: stopped_as_optional 将 stopped 映射为 nullopt
    Tool: Bash (cmake + ctest)
    Steps:
      1. just_stopped() | stopped_as_optional | sync_wait
      2. 验证返回值是 optional(nullopt)
    Expected Result: 返回包含 nullopt 的 optional
    Evidence: .sisyphus/evidence/task-14-stopped-optional.txt
  ```

  **Commit**: YES
  - Message: `feat(execution): implement stopped_as_optional and stopped_as_error`
  - Files: `backport/cpp26/execution/stopped_as.hpp`, tests
  - Pre-commit: `ctest --test-dir build-t14 -R execution --output-on-failure`

- [x] 15. sender_adaptor_closure CRTP 基类

  **What to do**:
  - 实现 `sender_adaptor_closure<Derived>` CRTP 基类（`[exec.adapt.obj]`）
  - 提供通用的 `operator|` 支持
  - 将现有 then_closure 迁移为继承 sender_adaptor_closure
  - 将 upon_error/upon_stopped/let_*/stopped_as_* 的 closure 类型也迁移
  - 支持 closure 组合：`closure1 | closure2` 产生新的组合 closure

  **Must NOT do**:
  - 不改变任何适配器的语义

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: CRTP 模式相对标准，主要是代码整理
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3
  - **Blocks**: None
  - **Blocked By**: Tasks 7, 11

  **References**:
  - `https://eel.is/c++draft/exec#adapt.obj` — 标准规范
  - `backport/cpp26/execution/then.hpp` — 现有 then_closure 模式

  **Acceptance Criteria**:

  ```
  Scenario: closure 组合
    Tool: Bash (cmake + ctest)
    Steps:
      1. auto composed = then([](int x) { return x + 1; }) | then([](int x) { return x * 2; });
      2. auto result = sync_wait(just(10) | composed);
      3. EXPECT_EQ(std::get<0>(*result), 22);
    Expected Result: (10+1)*2 = 22
    Evidence: .sisyphus/evidence/task-15-closure-compose.txt
  ```

  **Commit**: YES
  - Message: `refactor(execution): add sender_adaptor_closure CRTP base`
  - Files: `backport/cpp26/execution/*.hpp`
  - Pre-commit: `ctest --test-dir build-t15 -R execution --output-on-failure`

- [x] 16. linalg BLAS Level 1 — 基础操作（copy, scale, swap, add）

  **What to do**:
  - 实现 `std::linalg::copy(x, y)` — 复制向量/矩阵
  - 实现 `std::linalg::scale(alpha, x)` — 原地缩放
  - 实现 `std::linalg::swap_elements(x, y)` — 交换元素
  - 实现 `std::linalg::add(x, y, z)` — 向量加法
  - 所有函数仅实现串行版本（无 execution policy）
  - 参数类型为 `std::mdspan`

  **Must NOT do**:
  - 不实现 execution policy 重载
  - 不使用 SIMD 加速（后续可选优化）

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: 4 个简单操作，但需要正确处理 mdspan 模板参数
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3
  - **Blocks**: Tasks 17, 18, 19
  - **Blocked By**: Tasks 9, 10

  **References**:
  - `https://eel.is/c++draft/linalg` — 标准规范
  - kokkos/stdBLAS `blas1_linalg_copy.hpp`, `blas1_scale.hpp`, `blas1_linalg_swap.hpp`, `blas1_linalg_add.hpp`
  - `require/02-std-linalg-require.md:62-78` — Level 1 函数清单

  **Acceptance Criteria**:

  ```
  Scenario: copy 正确复制
    Tool: Bash (cmake + ctest)
    Steps:
      1. double src[] = {1,2,3}; double dst[3] = {};
      2. std::linalg::copy(mdspan(src,3), mdspan(dst,3));
      3. EXPECT_EQ(dst[0], 1.0); EXPECT_EQ(dst[2], 3.0);
    Expected Result: dst 内容与 src 一致
    Evidence: .sisyphus/evidence/task-16-copy.txt

  Scenario: scale 原地缩放
    Tool: Bash (cmake + ctest)
    Steps:
      1. double data[] = {1,2,3};
      2. std::linalg::scale(2.0, mdspan(data,3));
      3. EXPECT_EQ(data[0], 2.0); EXPECT_EQ(data[2], 6.0);
    Expected Result: data 乘以 2
    Evidence: .sisyphus/evidence/task-16-scale.txt
  ```

  **Commit**: YES
  - Message: `feat(linalg): implement BLAS Level 1 basic ops (copy, scale, swap, add)`
  - Files: `backport/cpp26/linalg.hpp`, `test/linalg/`
  - Pre-commit: `ctest --test-dir build-t16 -R linalg --output-on-failure`

- [x] 17. linalg BLAS Level 1 — dot products

  **What to do**:
  - 实现 `std::linalg::dot(x, y, init)` — 点积
  - 实现 `std::linalg::dotc(x, y, init)` — 共轭点积
  - 支持 float, double, complex<float>, complex<double>
  - 非 init 版本默认 init = 0

  **Must NOT do**:
  - 不实现 SIMD 加速
  - 不实现 execution policy 重载

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3
  - **Blocks**: Task 19
  - **Blocked By**: Task 16

  **References**:
  - kokkos/stdBLAS `blas1_dot.hpp`
  - `require/02-std-linalg-require.md:72-73`

  **Acceptance Criteria**:

  ```
  Scenario: dot 计算正确
    Tool: Bash (cmake + ctest)
    Steps:
      1. double x[] = {1,2,3}; double y[] = {4,5,6};
      2. auto result = std::linalg::dot(mdspan(x,3), mdspan(y,3), 0.0);
      3. EXPECT_DOUBLE_EQ(result, 32.0); // 1*4+2*5+3*6
    Expected Result: 32.0
    Evidence: .sisyphus/evidence/task-17-dot.txt
  ```

  **Commit**: YES
  - Message: `feat(linalg): implement BLAS Level 1 dot products`
  - Files: `backport/cpp26/linalg.hpp`, tests
  - Pre-commit: `ctest --test-dir build-t17 -R linalg --output-on-failure`

- [x] 18. linalg BLAS Level 1 — norms + reductions + givens

  **What to do**:
  - 实现 `vector_two_norm(x)` — 二范数
  - 实现 `vector_abs_sum(x)` — L1 范数
  - 实现 `vector_idx_abs_max(x)` — 最大绝对值索引
  - 实现 `vector_sum_of_squares(x, init)` — 平方和
  - 实现 `givens_rotation_setup(a, b)` + `givens_rotation_apply(x, y, c, s)`

  **Must NOT do**:
  - 不实现矩阵范数（matrix norms 属于 Level 2+）

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3
  - **Blocks**: Task 19
  - **Blocked By**: Task 16

  **References**:
  - kokkos/stdBLAS `blas1_vector_norm2.hpp`, `blas1_vector_abs_sum.hpp`, `blas1_vector_idx_abs_max.hpp`, `blas1_givens.hpp`

  **Acceptance Criteria**:

  ```
  Scenario: vector_two_norm 正确
    Tool: Bash (cmake + ctest)
    Steps:
      1. double x[] = {3.0, 4.0};
      2. EXPECT_DOUBLE_EQ(std::linalg::vector_two_norm(mdspan(x,2)), 5.0);
    Expected Result: sqrt(9+16) = 5.0
    Evidence: .sisyphus/evidence/task-18-nrm2.txt
  ```

  **Commit**: YES
  - Message: `feat(linalg): implement BLAS Level 1 norms, reductions, givens`
  - Files: `backport/cpp26/linalg.hpp`, tests
  - Pre-commit: `ctest --test-dir build-t18 -R linalg --output-on-failure`

- [x] 19. linalg BLAS Level 2 — matrix_vector_product (GEMV)

  **What to do**:
  - 实现 `matrix_vector_product(A, x, y)` — y = A*x
  - 实现更新形式 `matrix_vector_product(A, x, y, z)` — z = y + A*x
  - 支持 layout_left, layout_right, layout_stride
  - 支持 scaled/transposed view 组合
  - 需要 mdspan 的多维索引

  **Must NOT do**:
  - 不实现 execution policy 重载
  - 不实现对称/Hermitian 变体（Task 21）

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: GEMV 是 Level 2 核心，需要处理多种 layout + accessor 组合
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: NO
  - **Parallel Group**: Wave 4
  - **Blocks**: Tasks 20, 21
  - **Blocked By**: Tasks 17, 18

  **References**:
  - kokkos/stdBLAS `blas2_matrix_vector_product.hpp`
  - `require/02-std-linalg-require.md:85-86`

  **Acceptance Criteria**:

  ```
  Scenario: GEMV 正确计算
    Tool: Bash (cmake + ctest)
    Steps:
      1. double A[] = {1,2,3,4}; // 2x2 row-major
      2. double x[] = {1,1}; double y[2] = {};
      3. matrix_vector_product(mdspan<double,extents<int,2,2>>(A), mdspan(x,2), mdspan(y,2));
      4. EXPECT_EQ(y[0], 3.0); EXPECT_EQ(y[1], 7.0);
    Expected Result: [1+2, 3+4] = [3, 7]
    Evidence: .sisyphus/evidence/task-19-gemv.txt
  ```

  **Commit**: YES
  - Message: `feat(linalg): implement BLAS Level 2 matrix_vector_product`
  - Files: `backport/cpp26/linalg.hpp`, tests
  - Pre-commit: `ctest --test-dir build-t19 -R linalg --output-on-failure`

- [x] 20. linalg BLAS Level 2 — 三角操作（trmv, trsv）

  **What to do**:
  - 实现 `triangular_matrix_vector_product` — 三角矩阵-向量乘
  - 实现 `triangular_matrix_vector_solve` — 三角求解 A*x=b
  - 支持 upper/lower triangle + implicit/explicit diagonal
  - 原地和非原地版本

  **Must NOT do**:
  - 不实现 execution policy 重载

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES (after Task 19)
  - **Parallel Group**: Wave 4
  - **Blocks**: Task 22
  - **Blocked By**: Task 19

  **References**:
  - kokkos/stdBLAS `blas2_matrix_vector_solve.hpp`

  **Acceptance Criteria**:

  ```
  Scenario: triangular solve 正确
    Tool: Bash (cmake + ctest)
    Steps:
      1. 构造已知的上三角矩阵 + 右端向量
      2. 求解后验证结果
    Expected Result: 解向量正确
    Evidence: .sisyphus/evidence/task-20-trsv.txt
  ```

  **Commit**: YES
  - Message: `feat(linalg): implement BLAS Level 2 triangular ops`
  - Files: `backport/cpp26/linalg.hpp`, tests

- [x] 21. linalg BLAS Level 2 — symmetric/hermitian + rank updates

  **What to do**:
  - 实现 `symmetric_matrix_vector_product`, `hermitian_matrix_vector_product`
  - 实现 `matrix_rank_1_update`, `matrix_rank_1_update_c`
  - 实现 `symmetric_matrix_rank_1_update`, `hermitian_matrix_rank_1_update`
  - 实现 `symmetric_matrix_rank_2_update`, `hermitian_matrix_rank_2_update`

  **Must NOT do**:
  - 不实现 execution policy 重载

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES (after Task 19)
  - **Parallel Group**: Wave 4
  - **Blocks**: Task 22
  - **Blocked By**: Task 19

  **References**:
  - kokkos/stdBLAS `blas2_matrix_rank_1_update.hpp`, `blas2_matrix_rank_2_update.hpp`

  **Acceptance Criteria**:

  ```
  Scenario: rank-1 update 正确
    Tool: Bash (cmake + ctest)
    Steps:
      1. 构造矩阵 A, 向量 x, y
      2. matrix_rank_1_update(x, y, A) → A += x * y^T
      3. 验证结果
    Expected Result: A 正确更新
    Evidence: .sisyphus/evidence/task-21-rank1.txt
  ```

  **Commit**: YES
  - Message: `feat(linalg): implement BLAS Level 2 symmetric/hermitian and rank updates`
  - Files: `backport/cpp26/linalg.hpp`, tests

- [x] 22. linalg BLAS Level 3 — matrix_product (GEMM)

  **What to do**:
  - 实现 `matrix_product(A, B, C)` — C = A*B
  - 实现更新形式 `matrix_product(A, B, E, C)` — C = E + A*B
  - 支持 layout_left, layout_right
  - 支持 scaled/transposed view 输入
  - 使用简单的三重循环（header-only，不做缓存分块优化）

  **Must NOT do**:
  - 不实现缓存分块优化（标准库未来可用硬件 BLAS 替代）
  - 不实现 execution policy 重载
  - 不实现 SIMD 加速

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: GEMM 是最核心的 Level 3 操作，需要正确处理 layout 和 accessor 组合
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: NO
  - **Parallel Group**: Wave 4 (after Tasks 20, 21)
  - **Blocks**: Tasks 23, 24
  - **Blocked By**: Tasks 20, 21

  **References**:
  - kokkos/stdBLAS `blas3_matrix_product.hpp`
  - `require/02-std-linalg-require.md:104-105`

  **Acceptance Criteria**:

  ```
  Scenario: GEMM 正确计算
    Tool: Bash (cmake + ctest)
    Steps:
      1. 构造 2x3 矩阵 A, 3x2 矩阵 B
      2. matrix_product(A, B, C) → C = A*B (2x2)
      3. 验证结果与手算一致
    Expected Result: C 正确
    Evidence: .sisyphus/evidence/task-22-gemm.txt

  Scenario: GEMM + scaled 输入
    Tool: Bash (cmake + ctest)
    Steps:
      1. matrix_product(scaled(2.0, A), B, C)
      2. 验证 C = 2*A*B
    Expected Result: 正确缩放
    Evidence: .sisyphus/evidence/task-22-gemm-scaled.txt
  ```

  **Commit**: YES
  - Message: `feat(linalg): implement BLAS Level 3 matrix_product (GEMM)`
  - Files: `backport/cpp26/linalg.hpp`, tests

- [x] 23. linalg BLAS Level 3 — 三角操作（trmm, trsm）

  **What to do**:
  - 实现 `triangular_matrix_product` — 三角矩阵乘
  - 实现 `triangular_matrix_left_product` / `triangular_matrix_right_product` — 原地三角乘
  - 实现 `triangular_matrix_matrix_left_solve` / `triangular_matrix_matrix_right_solve`

  **Must NOT do**:
  - 不实现 execution policy 重载

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES (after Task 22)
  - **Parallel Group**: Wave 4
  - **Blocks**: None
  - **Blocked By**: Task 22

  **References**:
  - kokkos/stdBLAS `blas3_triangular_matrix_matrix_solve.hpp`

  **Acceptance Criteria**:

  ```
  Scenario: TRSM 正确求解
    Tool: Bash (cmake + ctest)
    Steps:
      1. 构造已知的三角矩阵和右端矩阵
      2. triangular_matrix_matrix_left_solve 求解
      3. 验证解矩阵
    Expected Result: 解正确
    Evidence: .sisyphus/evidence/task-23-trsm.txt
  ```

  **Commit**: YES
  - Message: `feat(linalg): implement BLAS Level 3 triangular ops`
  - Files: `backport/cpp26/linalg.hpp`, tests

- [x] 24. linalg BLAS Level 3 — symmetric/hermitian + rank-k updates

  **What to do**:
  - 实现 `symmetric_matrix_product`, `hermitian_matrix_product`
  - 实现 `matrix_rank_k_update`, `symmetric_matrix_rank_k_update`, `hermitian_matrix_rank_k_update`
  - 实现 `matrix_rank_2k_update`, `symmetric_matrix_rank_2k_update`, `hermitian_matrix_rank_2k_update`

  **Must NOT do**:
  - 不实现 execution policy 重载

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES (after Task 22)
  - **Parallel Group**: Wave 4
  - **Blocks**: None
  - **Blocked By**: Task 22

  **References**:
  - kokkos/stdBLAS `blas3_matrix_rank_k_update.hpp`, `blas3_matrix_rank_2k_update.hpp`

  **Acceptance Criteria**:

  ```
  Scenario: rank-k update 正确
    Tool: Bash (cmake + ctest)
    Steps:
      1. 构造矩阵 A, C
      2. symmetric_matrix_rank_k_update(1.0, A, C) → C += A * A^T
      3. 验证 C 正确更新且对称
    Expected Result: C 正确且对称
    Evidence: .sisyphus/evidence/task-24-rank-k.txt
  ```

  **Commit**: YES
  - Message: `feat(linalg): implement BLAS Level 3 symmetric/hermitian and rank-k updates`
  - Files: `backport/cpp26/linalg.hpp`, tests

- [x] 25. execution Phase 1 known-deviation 文档更新 + 4-toolchain push 验证

  **What to do**:
  - 更新 `backport/cpp26/execution.hpp`（或 `execution/` 子文件）顶部的 KNOWN DEVIATION 注释
  - 移除已修复的偏差项（stoppable_token concepts 已实现、sync_wait 已使用 run_loop、sender_adaptor_closure 已实现）
  - 保留仍未完成的偏差（tag_invoke 内部使用、when_all/bulk/split/coroutine 未实现）
  - 更新 README.md 的 `std::execution` 说明
  - 运行完整的 4-toolchain 验证

  **Must NOT do**:
  - 不改变任何功能代码

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 4
  - **Blocks**: None
  - **Blocked By**: Task 15

  **References**:
  - `backport/cpp26/execution.hpp:26-39` — 当前 KNOWN DEVIATION 注释
  - `README.md:71-74` — 当前 execution 说明

  **Acceptance Criteria**:

  ```
  Scenario: 4-toolchain 全部通过
    Tool: Bash (4 组命令)
    Steps:
      1. GCC: cmake + build + ctest
      2. Zig: CC="zig cc" CXX="zig c++" cmake + build + ctest
      3. Podman Clang: podman run ... cmake + build + ctest
      4. Podman GCC: podman run ... cmake + build + ctest
    Expected Result: 全部通过
    Evidence: .sisyphus/evidence/task-25-4toolchain.txt
  ```

  **Commit**: YES
  - Message: `docs(execution): update known deviations after Phase 1 completion`
  - Files: `backport/cpp26/execution.hpp`, `README.md`

---

## Final Verification Wave

> 4 review agents run in PARALLEL. ALL must APPROVE. Present consolidated results to user and get explicit "okay" before completing.

- [x] F1. **Plan Compliance Audit** — `oracle`
  Read the plan end-to-end. For each "Must Have": verify implementation exists (read file, run command). For each "Must NOT Have": search codebase for forbidden patterns — reject with file:line if found. Check evidence files exist in .sisyphus/evidence/. Compare deliverables against plan.
  Output: `Must Have [N/N] | Must NOT Have [N/N] | Tasks [N/N] | VERDICT: APPROVE/REJECT`

- [x] F2. **Code Quality Review** — `unspecified-high`
  Run `ctest --test-dir build-gcc --output-on-failure`. Review all changed files for: `as any`/`@ts-ignore` equivalents, empty catches, commented-out code, unused includes. Check AI slop: excessive comments, over-abstraction, generic names.
  Output: `Build [PASS/FAIL] | Tests [N pass/N fail] | Files [N clean/N issues] | VERDICT`

- [x] F3. **Real Manual QA** — `unspecified-high` 
  Start from clean state. Run full 4-toolchain verification (GCC, Zig, Podman Clang, Podman GCC). Save output to `.sisyphus/evidence/final-qa/`.
  Output: `GCC [PASS/FAIL] | Zig [PASS/FAIL] | Clang [PASS/FAIL] | GCC-container [PASS/FAIL] | VERDICT`

- [x] F4. **Scope Fidelity Check** — `deep`
  For each task: read "What to do", read actual diff. Verify 1:1 — everything in spec was built, nothing beyond spec was built. Check "Must NOT do" compliance. Detect cross-task contamination.
  Output: `Tasks [N/N compliant] | Contamination [CLEAN/N issues] | Unaccounted [CLEAN/N files] | VERDICT`

---

## Commit Strategy

### Execution Phase 1
- `feat(execution): add get_completion_scheduler CPO and enable_sender trait` — execution.hpp
- `refactor(execution): split execution.hpp into modular internal headers` — execution/*.hpp
- `feat(execution): implement run_loop with intrusive task queue` — execution/run_loop.hpp
- `test(execution): add run_loop runtime and thread-safety tests` — test files
- `refactor(execution): rewrite sync_wait to use run_loop per [exec.sync.wait]` — execution/sync_wait.hpp
- `feat(execution): implement read_env sender factory` — execution.hpp
- `feat(execution): implement upon_error and upon_stopped adaptors` — execution.hpp
- `feat(execution): implement let_value, let_error, let_stopped` — execution/let.hpp
- `feat(execution): implement starts_on and continues_on` — execution.hpp
- `feat(execution): implement stopped_as_optional and stopped_as_error` — execution.hpp
- `refactor(execution): add sender_adaptor_closure CRTP base` — execution.hpp
- → **PUSH** (execution Phase 1 milestone)

### linalg Phase 0-1
- `feat(linalg): add forge.cmake detection and backport skeleton` — forge.cmake, backport/linalg, backport/cpp26/linalg.hpp
- `feat(linalg): implement auxiliary types and accessor wrappers` — linalg.hpp
- `feat(linalg): implement BLAS Level 1 operations` — linalg.hpp
- `test(linalg): add Level 1 runtime tests` — test/linalg/
- → **PUSH** (linalg Phase 0-1 milestone)

### linalg Phase 2
- `feat(linalg): implement BLAS Level 2 operations` — linalg.hpp
- `test(linalg): add Level 2 runtime tests` — test/linalg/
- → **PUSH** (linalg Phase 2 milestone)

### linalg Phase 3
- `feat(linalg): implement BLAS Level 3 operations` — linalg.hpp
- `test(linalg): add Level 3 runtime tests` — test/linalg/
- → **PUSH** (linalg Phase 3 milestone)

---

## Success Criteria

### Verification Commands
```bash
# GCC
cmake -S . -B build-gcc -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON
cmake --build build-gcc -j
ctest --test-dir build-gcc --output-on-failure

# Zig
CC="zig cc" CXX="zig c++" cmake -S . -B build-zig -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON
cmake --build build-zig -j
ctest --test-dir build-zig --output-on-failure

# Podman Clang
podman run --rm --userns=keep-id -v "$PWD:/work:Z" -w /work docker.io/silkeh/clang:latest bash -lc \
  'cmake -S . -B build-podman-clang -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON && cmake --build build-podman-clang -j && ctest --test-dir build-podman-clang --output-on-failure'

# Podman GCC
podman run --rm --userns=keep-id --user 0 -v "$PWD:/work:ro,Z" -w /work docker.io/gcc:latest bash -lc \
  'apt-get update -qq && apt-get install -y --no-install-recommends -qq cmake ninja-build >/dev/null && cmake -S /work -B /tmp/build-gcc -G Ninja -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON && cmake --build /tmp/build-gcc -j && ctest --test-dir /tmp/build-gcc --output-on-failure'
```

### Final Checklist
- [ ] All "Must Have" present
- [ ] All "Must NOT Have" absent
- [ ] All tests pass on 4 toolchains
- [ ] Feature-test macro guards in place for all backports
- [ ] forge.cmake detects native support and disables backport

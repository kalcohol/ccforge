# Forge Phase 2 — 深化与补完工作计划

## TL;DR

> **Quick Summary**: 完成 std::execution Phase 3+4（when_all/bulk/split/coroutine 等全部剩余算法）、linalg SIMD 加速（全部非复数标准类型）、测试体系重组、文档更新。
>
> **Deliverables**:
> - execution 完整 P2300R10 覆盖（Phase 3: 并行+共享 / Phase 4: coroutine 集成）
> - linalg BLAS Level 1 SIMD 加速路径（float/double/整型，4 架构验证）
> - execution + linalg 测试体系重组为 compile/ + runtime/ + configure_probes/ 三层
> - 新增 examples + README 更新
>
> **Estimated Effort**: XL（~30 个任务）
> **Parallel Execution**: YES - 5 waves
> **Critical Path**: SBO storage → CPO bridge → transform_completion_signatures → when_all → coroutine bridge

---

## Context

### Original Request
用户要求 6 个维度的深化工作：
1. execution 全面推进到标准级别（Phase 3+4，下游需要结构化并发+协程协作）
2. linalg SIMD 加速（非复数全部标准类型，zig cross-compile + qemu 4 架构验证）
3. 测试体系化（不零散）
4. mdspan 不支持（代价大，GCC 14+ 已原生提供）
5. 适当更新 example 和 README
6. 适当时机提交推送

### Metis Review — Key Risks Addressed
- **1024B buffer 溢出**：when_all 需同时存 N 个子 op-state → 实现 SBO+堆回退混合
- **双 CPO 分发共存**：Phase 1/2 tag_invoke + Phase 3/4 成员函数优先 → 构建分发桥
- **when_all completion_signatures**：需要完整笛卡尔积（用户明确要求，非约束版）
- **split/ensure_started 线程安全**：stdexec 多次 TSAN 发现竞态 → 需 TSAN CI
- **SIMD + accessor 组合**：仅对 default_accessor + 连续 layout 启用 SIMD

### Decisions Made
- Q1: SBO+堆回退混合 ✅
- Q2: CPO 分发桥（成员函数优先 → tag_invoke 回退）✅
- Q3: when_all 完整笛卡尔积 completion_signatures ✅
- Q4: SIMD 加速全部非复数标准类型（float/double + 全部整型）✅
- Q5: Coroutine 用 `__cpp_impl_coroutine` 守卫，不可用时跳过 ✅
- Q6: 数值正确性即可，单架构失败不阻塞 ✅

---

## Work Objectives

### Core Objective
将 Forge 的 execution backport 从 Phase 1+2 推进到完整 P2300R10 覆盖（Phase 3 并行+共享 / Phase 4 coroutine），并为 linalg BLAS Level 1 添加 std::simd 加速路径。

### Concrete Deliverables
- execution 新增 ~10 个算法文件 + 基础设施重构
- linalg.hpp 内联 SIMD 加速路径
- execution 测试重组为 compile/ + runtime/ 三层结构
- linalg 测试按 Level 拆分
- 新增 example 文件
- README 全面更新

### Must Have
- SBO+堆回退存储抽象 (detail/storage.hpp)
- CPO 分发桥（成员函数优先 + tag_invoke 回退）
- transform_completion_signatures 元函数
- when_all（完整笛卡尔积 completion_signatures）
- bulk（串行回退）
- split + ensure_started（原子引用计数 shared_state）
- into_variant + sync_wait_with_variant
- continues_on + schedule_from
- start_detached
- as_awaitable + with_awaitable_senders（coroutine 守卫）
- 类型擦除 stop_token (any_stop_token)
- linalg BLAS Level 1 SIMD 路径（float/double/整型，default_accessor + 连续 layout）
- 4 架构交叉编译验证（x86_64/aarch64/riscv64/loongarch64）

### Must NOT Have (Guardrails)
- ❌ async_scope / counting_scope（不在范围）
- ❌ task<T> 协程类型（仅桥接原语）
- ❌ complex<T> SIMD 加速（本阶段不做）
- ❌ BLAS Level 2/3 SIMD 加速（仅 Level 1）
- ❌ intrinsics 后端（仅 std::simd 抽象层）
- ❌ mdspan backport
- ❌ 修改已完成的 unique_resource / simd backport 公共 API
- ❌ 拆分 linalg.hpp 为多文件（用 if constexpr 内联 SIMD 路径）

---

## Verification Strategy

> **ZERO HUMAN INTERVENTION** — ALL verification is agent-executed.

### Test Decision
- **Infrastructure exists**: YES (GTest + compile probes)
- **Automated tests**: YES (Tests-after)
- **Framework**: GTest (gtest_main) + compile probes + configure probes
- **TSAN**: 新增 TSAN 构建目标用于 split/when_all/ensure_started

### QA Policy
- **execution**: 每个新算法必须有 compile probe + runtime test + 取消测试
- **linalg SIMD**: 标量 vs SIMD 数值等价测试（double 1e-12, float 1e-5, int 精确）
- **跨架构**: zig cross-compile + qemu-static 执行测试

---

## Execution Strategy

### Parallel Execution Waves

```
Wave 0 (Infrastructure — unblocks everything):
├── T1: SBO+堆回退存储抽象 (detail/storage.hpp) [deep]
├── T2: CPO 分发桥 (connect_t/start_t 成员函数优先→tag_invoke) [deep]
├── T3: transform_completion_signatures 元函数 [deep]
├── T4: 测试体系重组 — execution (compile/runtime/configure_probes) [unspecified-high]
└── T5: 测试体系重组 — linalg (按 Level 拆分) [quick]

Wave 1 (Simple algorithms — no shared state):
├── T6: into_variant [unspecified-high]
├── T7: sync_wait_with_variant [unspecified-high]
├── T8: schedule_from + continues_on [deep]
├── T9: bulk (串行回退) [deep]
├── T10: start_detached [unspecified-high]
├── T11: linalg SIMD 检测基础设施 (accessor/layout type traits) [quick]
└── T12: linalg SIMD — dot + vector_two_norm 路径 [unspecified-high]

Wave 2 (Shared state algorithms — most complex):
├── T13: split [ultrabrain]
├── T14: when_all (完整笛卡尔积) [ultrabrain]
├── T15: ensure_started [deep]
├── T16: linalg SIMD — copy + scale + add 路径 [unspecified-high]
└── T17: linalg SIMD — vector_abs_sum + vector_idx_abs_max 路径 [quick]

Wave 3 (Coroutine bridge + cross-arch verification):
├── T18: 类型擦除 stop_token (any_stop_token) [deep]
├── T19: as_awaitable [deep]
├── T20: with_awaitable_senders [unspecified-high]
├── T21: 跨架构 SIMD 验证 (zig + qemu, 4 arch) [unspecified-high]
└── T22: TSAN 验证 (split/when_all/ensure_started) [unspecified-high]

Wave 4 (Documentation + final verification):
├── T23: execution examples (when_all, split, bulk, coroutine) [writing]
├── T24: linalg SIMD benchmark example [writing]
├── T25: README 全面更新 [writing]
└── T26: execution Phase 1/2 KNOWN DEVIATION 更新 [quick]

Wave FINAL (4 parallel reviews):
├── F1: Plan compliance audit (oracle)
├── F2: Code quality + TSAN review (unspecified-high)
├── F3: 4-toolchain + 4-architecture QA (unspecified-high)
└── F4: Scope fidelity check (deep)
-> Present results -> Get explicit user okay
```

### Dependency Matrix

| Task | Depends On | Blocks |
|------|-----------|--------|
| T1 | — | T8, T9, T13, T14, T15 |
| T2 | — | T6-T10, T13-T15, T18-T20 |
| T3 | — | T6, T7, T14 |
| T4 | — | T6-T10 (tests) |
| T5 | — | T12, T16, T17 (tests) |
| T6 | T2, T3, T4 | T7 |
| T7 | T6 | — |
| T8 | T1, T2 | — |
| T9 | T1, T2 | — |
| T10 | T2 | — |
| T11 | — | T12, T16, T17 |
| T12 | T5, T11 | T21 |
| T13 | T1, T2 | T14, T15, T22 |
| T14 | T1, T2, T3, T13 | T22 |
| T15 | T13 | T22 |
| T16 | T11 | T21 |
| T17 | T11 | T21 |
| T18 | T2 | T19 |
| T19 | T18 | T20 |
| T20 | T19 | — |
| T21 | T12, T16, T17 | — |
| T22 | T13, T14, T15 | — |
| T23-T26 | all impl tasks | — |

---

## TODOs

- [x] 1. SBO+堆回退存储抽象 (detail/storage.hpp)

  **What to do**:
  - 创建 `backport/cpp26/execution/detail/storage.hpp`
  - 实现 `__sbo_storage<SmallSize, Align>` 模板类：小对象走栈内 buffer，大对象走 `new`
  - 接口：`template<class T, class... Args> T* emplace(Args&&...)` — 构造对象到 buffer 或 heap
  - 接口：`void destroy()` — 调用析构 + 释放 heap（如果使用了）
  - 接口：`template<class T> T* get()` — 获取存储的对象指针
  - SmallSize 默认 256 字节（单个简单 op-state），复合场景自动 heap fallback
  - 替换 let.hpp / on.hpp 中的 `kBufSize = 1024` + `unsigned char buf[]` 模式
  - 保持 `alignas(std::max_align_t)` 对齐

  **Must NOT do**:
  - 不使用 `std::any`（性能开销）
  - 不使用 `std::function`（类型擦除开销）

  **Recommended Agent Profile**:
  - **Category**: `deep`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 0 (with T2, T3, T4, T5)
  - **Blocks**: T8, T9, T13, T14, T15
  - **Blocked By**: None

  **References**:
  - `backport/cpp26/execution/let.hpp:43-45` — 当前 1024B buffer 模式
  - `backport/cpp26/execution/on.hpp:34-36` — 同样的 buffer 模式
  - NVIDIA stdexec `__intrusive_slist.hpp` — 类似 SBO 模式参考

  **Acceptance Criteria**:
  ```
  Scenario: 小对象走栈内 buffer
    Tool: Bash (cmake + ctest)
    Steps:
      1. 创建 32 字节的 trivial 对象，emplace 到 storage
      2. 验证 get() 返回的指针在 storage 对象地址范围内（栈）
    Expected Result: 指针在 buffer 范围内
    Evidence: .sisyphus/evidence/task-1-sbo-small.txt

  Scenario: 大对象走堆
    Tool: Bash (cmake + ctest)
    Steps:
      1. 创建 2048 字节的对象，emplace 到 SmallSize=256 的 storage
      2. 验证 get() 返回非 null 且不在 buffer 范围内
    Expected Result: 堆分配成功
    Evidence: .sisyphus/evidence/task-1-sbo-heap.txt
  ```

  **Commit**: YES
  - Message: `feat(execution): add SBO+heap storage abstraction`
  - Files: `backport/cpp26/execution/detail/storage.hpp`, tests
  - Pre-commit: `ctest --test-dir build -R execution --output-on-failure`

- [x] 2. CPO 分发桥 (connect_t/start_t 成员函数优先→tag_invoke 回退)

  **What to do**:
  - 修改 `concepts.hpp` 中的 `connect_t::operator()` — 先尝试 `sndr.connect(rcvr)`（成员函数），若不可用则回退 `tag_invoke(connect_t{}, sndr, rcvr)`
  - 同样修改 `start_t::operator()` — 先尝试 `op.start()`，回退 `tag_invoke(start_t{}, op)`
  - 同样修改 `get_completion_signatures_t` — 先尝试成员 `.get_completion_signatures(env)`，回退 tag_invoke
  - 使用 `if constexpr` + `requires` 实现优先级分发
  - 所有现有 Phase 1/2 测试必须继续通过（tag_invoke 路径不受影响）

  **Must NOT do**:
  - 不修改现有 sender/receiver 类型的实现（just/then/let 等保持 tag_invoke friend）
  - 不引入运行时开销

  **Recommended Agent Profile**:
  - **Category**: `deep`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 0
  - **Blocks**: T6-T10, T13-T15, T18-T20
  - **Blocked By**: None

  **References**:
  - `backport/cpp26/execution/concepts.hpp:320-330` — 当前 connect_t 定义
  - `https://eel.is/c++draft/exec#domain.default` — 标准 default_domain 分发逻辑
  - NVIDIA stdexec `__domain.hpp` — 成员函数优先分发参考

  **Acceptance Criteria**:
  ```
  Scenario: tag_invoke 路径仍然工作（回归测试）
    Tool: Bash (cmake + ctest)
    Steps:
      1. 运行全部现有 execution 测试
    Expected Result: 43/43 通过
    Evidence: .sisyphus/evidence/task-2-cpo-regression.txt

  Scenario: 成员函数路径优先
    Tool: Bash (cmake + ctest)
    Steps:
      1. 创建带 `auto connect(auto rcvr)` 成员函数的 sender
      2. 同时提供 tag_invoke(connect_t, ...) friend
      3. 验证 connect() 调用成员函数版本（通过 side effect 区分）
    Expected Result: 成员函数版本被调用
    Evidence: .sisyphus/evidence/task-2-cpo-member-first.txt
  ```

  **Commit**: YES
  - Message: `feat(execution): add CPO dispatch bridge (member-first + tag_invoke fallback)`
  - Files: `backport/cpp26/execution/concepts.hpp`, tests

- [x] 3. transform_completion_signatures 元函数

  **What to do**:
  - 在 `concepts.hpp`（或新文件 `meta.hpp`）中实现 `transform_completion_signatures<InputCS, Env, SetValueTransform, SetErrorTransform, SetStoppedTransform>`
  - 支持签名变换：将输入 completion_signatures 中的每个签名通过对应 transform 映射到新签名集
  - 实现辅助元函数：`__concat_completion_signatures`（合并多个 completion_signatures）
  - 实现 when_all 需要的笛卡尔积：`__cartesian_product_sigs<CS1, CS2, ...>` — 将多个 sender 的 set_value 签名做笛卡尔积
  - 全部编译期计算，零运行时开销

  **Must NOT do**:
  - 不引入运行时类型信息

  **Recommended Agent Profile**:
  - **Category**: `deep`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 0
  - **Blocks**: T6, T7, T14
  - **Blocked By**: None

  **References**:
  - `backport/cpp26/execution/concepts.hpp:272-310` — 现有 __forge_meta 命名空间
  - `https://eel.is/c++draft/exec#utils.tfxcmplsigs` — 标准 transform_completion_signatures
  - NVIDIA stdexec `__completion_signatures.hpp` — 参考实现

  **Acceptance Criteria**:
  ```
  Scenario: 基本签名变换
    Tool: Bash (cmake + ctest)
    Steps:
      1. transform_completion_signatures<CS<set_value_t(int)>, empty_env, ValueTransform, ...>
      2. static_assert 结果为 CS<set_value_t(double)>（ValueTransform: int→double）
    Expected Result: 编译通过
    Evidence: .sisyphus/evidence/task-3-transform-basic.txt

  Scenario: 笛卡尔积
    Tool: Bash (cmake + ctest)
    Steps:
      1. __cartesian_product_sigs<CS<set_value_t(int)>, CS<set_value_t(double)>>
      2. static_assert 结果为 CS<set_value_t(int, double)>
    Expected Result: 编译通过
    Evidence: .sisyphus/evidence/task-3-cartesian.txt
  ```

  **Commit**: YES
  - Message: `feat(execution): add transform_completion_signatures and Cartesian product`
  - Files: `backport/cpp26/execution/concepts.hpp` 或新文件

- [x] 4. 测试体系重组 — execution

  **What to do**:
  - 创建 `test/execution/compile/` 目录 + CMakeLists.txt
  - 将 `test_execution_mvp.cpp` 中的 static_assert 移入 `compile/test_execution_api_core.cpp`
  - 新增 compile probes: `test_execution_api_sender.cpp`（sender concept 正反例）、`test_execution_api_receiver.cpp`
  - 按功能拆分 `test_execution_adaptors.cpp` 为独立文件：`test_execution_upon.cpp`、`test_execution_let.cpp`、`test_execution_stopped_as.cpp`、`test_execution_read_env.cpp`、`test_execution_starts_on.cpp`
  - 保留 `test_execution_mvp.cpp` 作为冒烟测试
  - 更新 `test/execution/CMakeLists.txt` 注册新测试
  - 所有现有测试场景保留，仅重组文件结构

  **Must NOT do**:
  - 不删除任何测试场景
  - 不改变测试逻辑

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 0
  - **Blocks**: T6-T10 (test registration)
  - **Blocked By**: None

  **References**:
  - `test/simd/CMakeLists.txt` — 成熟的三层测试结构参考
  - `test/simd/compile/` — compile probe 组织模式

  **Acceptance Criteria**:
  ```
  Scenario: 测试数量不减少
    Tool: Bash (cmake + ctest)
    Steps:
      1. 重组前记录测试数量
      2. 重组后运行 ctest，比较数量
    Expected Result: 测试数量 >= 原始数量
    Evidence: .sisyphus/evidence/task-4-test-count.txt
  ```

  **Commit**: YES
  - Message: `test(execution): reorganize into compile/runtime structure`
  - Files: `test/execution/`

- [x] 5. 测试体系重组 — linalg

  **What to do**:
  - 将 `test/linalg/runtime/test_linalg_core.cpp` 拆分为：`test_linalg_level1.cpp`（BLAS L1 测试）、`test_linalg_level2.cpp`（BLAS L2）、`test_linalg_level3.cpp`（BLAS L3）、`test_linalg_auxiliary.cpp`（tags + accessors）
  - 创建 `test/linalg/compile/` + compile probes（`test_linalg_api_tags.cpp`、`test_linalg_api_level1.cpp`）
  - 删除遗留的 `test/linalg_test.cpp`
  - 更新 CMakeLists.txt

  **Must NOT do**:
  - 不删除任何测试场景

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 0
  - **Blocks**: T12, T16, T17
  - **Blocked By**: None

  **Acceptance Criteria**:
  ```
  Scenario: linalg 测试仍通过（在有 mdspan 的工具链上）
    Tool: Bash (zig + ctest)
    Steps:
      1. CC="zig cc" CXX="zig c++" cmake + build + ctest -R linalg
    Expected Result: 所有 linalg 测试通过
    Evidence: .sisyphus/evidence/task-5-linalg-reorg.txt
  ```

  **Commit**: YES
  - Message: `test(linalg): reorganize into per-level test files`
  - Files: `test/linalg/`

- [x] 6. into_variant

  **What to do**:
  - 创建 `backport/cpp26/execution/into_variant.hpp`
  - `into_variant(sndr)` 将 sender 的多个 set_value 签名包装为单个 `variant<tuple<Vs...>...>` 值
  - 使用 `transform_completion_signatures`（T3 产出）计算输出类型
  - pipe operator 支持

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1
  - **Blocks**: T7
  - **Blocked By**: T2, T3, T4

  **References**:
  - `https://eel.is/c++draft/exec#into.variant` — 标准规范

  **Acceptance Criteria**:
  ```
  Scenario: 单签名 sender 产出 variant<tuple<int>>
    Tool: Bash (cmake + ctest)
    Steps:
      1. into_variant(just(42)) | sync_wait
      2. 验证结果类型正确
    Expected Result: variant 包含 tuple{42}
    Evidence: .sisyphus/evidence/task-6-into-variant.txt
  ```

  **Commit**: YES
  - Message: `feat(execution): add into_variant`

- [x] 7. sync_wait_with_variant

  **What to do**:
  - 在 `sync_wait.hpp` 中添加 `sync_wait_with_variant(sndr)` — 等价于 `sync_wait(into_variant(sndr))`
  - 支持多签名 sender

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: NO
  - **Parallel Group**: After T6
  - **Blocks**: —
  - **Blocked By**: T6

  **Acceptance Criteria**:
  ```
  Scenario: sync_wait_with_variant 基本使用
    Tool: Bash (cmake + ctest)
    Steps:
      1. sync_wait_with_variant(just(42))
    Expected Result: 返回 optional<variant<tuple<int>>>
    Evidence: .sisyphus/evidence/task-7-swv.txt
  ```

  **Commit**: YES (grouped with T6)
  - Message: `feat(execution): add into_variant and sync_wait_with_variant`

- [x] 8. schedule_from + continues_on

  **What to do**:
  - 创建 `backport/cpp26/execution/continues_on.hpp`
  - `schedule_from(sch, sndr)` — 当 sndr 完成后在 sch 上继续执行
  - `continues_on(sndr, sch)` — pipe 友好版本，等价于 `schedule_from(sch, sndr)`
  - 使用 SBO storage（T1 产出）
  - pipe operator 支持

  **Recommended Agent Profile**:
  - **Category**: `deep`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1
  - **Blocks**: —
  - **Blocked By**: T1, T2

  **References**:
  - `https://eel.is/c++draft/exec#continues.on` — 标准规范
  - `backport/cpp26/execution/on.hpp` — starts_on 模式参考

  **Acceptance Criteria**:
  ```
  Scenario: continues_on 切换调度器
    Tool: Bash (cmake + ctest)
    Steps:
      1. run_loop loop; auto sch = loop.get_scheduler();
      2. just(42) | continues_on(sch) | then([](int x) { return x; }) | sync_wait
    Expected Result: 在 run_loop 上执行完成
    Evidence: .sisyphus/evidence/task-8-continues-on.txt
  ```

  **Commit**: YES
  - Message: `feat(execution): add schedule_from and continues_on`

- [x] 9. bulk (串行回退)

  **What to do**:
  - 创建 `backport/cpp26/execution/bulk.hpp`
  - `bulk(sndr, shape, fn)` — 对 shape 个索引执行 fn(index, values...)
  - 初始实现为串行 for 循环（serial fallback）
  - Shape 为整数类型
  - completion_signatures: 透传上游值签名 + 可能的 exception_ptr 错误
  - pipe operator 支持

  **Recommended Agent Profile**:
  - **Category**: `deep`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1
  - **Blocks**: —
  - **Blocked By**: T1, T2

  **References**:
  - `https://eel.is/c++draft/exec#bulk` — 标准规范

  **Acceptance Criteria**:
  ```
  Scenario: bulk 串行执行 N 次
    Tool: Bash (cmake + ctest)
    Steps:
      1. just(0) | bulk(10, [](int idx, int& sum) { sum += idx; }) | sync_wait
      2. 验证 sum == 45 (0+1+...+9)
    Expected Result: sum == 45
    Evidence: .sisyphus/evidence/task-9-bulk.txt
  ```

  **Commit**: YES
  - Message: `feat(execution): add bulk (serial fallback)`

- [x] 10. start_detached

  **What to do**:
  - 创建 `backport/cpp26/execution/start_detached.hpp`
  - `start_detached(sndr)` — fire-and-forget，堆分配 operation_state
  - set_error 时调用 `std::terminate()`
  - set_stopped 时静默忽略

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1
  - **Blocks**: —
  - **Blocked By**: T2

  **Acceptance Criteria**:
  ```
  Scenario: start_detached 执行并完成
    Tool: Bash (cmake + ctest)
    Steps:
      1. atomic<int> counter{0};
      2. start_detached(just() | then([&]{ counter++; }));
      3. 等待 + 验证 counter == 1
    Expected Result: counter == 1
    Evidence: .sisyphus/evidence/task-10-detached.txt
  ```

  **Commit**: YES
  - Message: `feat(execution): add start_detached`

- [x] 11. linalg SIMD 检测基础设施

  **What to do**:
  - 在 `linalg.hpp` 中添加 SIMD 检测 type traits:
    - `__is_simd_accelerable_v<T>` — `is_arithmetic_v<T> && !is_same_v<T, bool> && !__detail::is_complex_v<T>`
    - `__is_contiguous_layout_v<Layout>` — `is_same_v<Layout, layout_right> || is_same_v<Layout, layout_left>`
    - `__is_default_accessor_v<Accessor>` — 检测 `default_accessor<T>`
    - `__can_simd_v<T, Layout, Accessor>` — 组合上述三个条件
  - 添加 `#include <simd>` 的条件包含（`#if __has_include(<simd>)`）

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1
  - **Blocks**: T12, T16, T17
  - **Blocked By**: None

  **Acceptance Criteria**:
  ```
  Scenario: SIMD 可用性检测
    Tool: Bash (cmake + ctest)
    Steps:
      1. static_assert(__can_simd_v<double, layout_right, default_accessor<double>>)
      2. static_assert(!__can_simd_v<complex<double>, layout_right, default_accessor<complex<double>>>)
    Expected Result: 编译通过
    Evidence: .sisyphus/evidence/task-11-simd-traits.txt
  ```

  **Commit**: YES (grouped with T12)

- [x] 12. linalg SIMD — dot + vector_two_norm 路径

  **What to do**:
  - 为 `dot()` 添加 SIMD 加速路径：当 `__can_simd_v` 为 true 时，使用 `std::simd` 批量加载 + 归约
  - 处理尾部不对齐元素（remainder loop）
  - 为 `vector_two_norm()` 添加类似的 SIMD 路径
  - `if constexpr` 选择 SIMD vs 标量路径
  - 保留原始标量路径作为 fallback

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1
  - **Blocks**: T21
  - **Blocked By**: T5, T11

  **References**:
  - `backport/cpp26/simd/types.hpp` — simd::vec<T> 接口
  - `backport/cpp26/simd/reductions.hpp` — reduce() 归约操作

  **Acceptance Criteria**:
  ```
  Scenario: SIMD dot 数值等价
    Tool: Bash (cmake + ctest)
    Steps:
      1. 创建 1000 元素 double 数组
      2. 比较 SIMD dot 结果与标量 dot 结果
      3. EXPECT_NEAR(simd_result, scalar_result, 1e-12)
    Expected Result: 差异 < 1e-12
    Evidence: .sisyphus/evidence/task-12-simd-dot.txt
  ```

  **Commit**: YES
  - Message: `feat(linalg): add SIMD acceleration for dot and vector_two_norm`

- [x] 13. split

  **What to do**:
  - 创建 `backport/cpp26/execution/split.hpp`
  - `split(sndr)` 返回可多次 connect 的 sender（共享状态）
  - 使用 `std::atomic<int>` 引用计数管理 shared_state 生命周期
  - shared_state 堆分配（不用 SBO，因为多个 receiver 共享同一 state）
  - 第一次 start 时启动原始 sender，后续 start 等待结果
  - 支持取消传播
  - 使用 mutex+cv（不用 atomic::wait）保护等待者列表

  **Recommended Agent Profile**:
  - **Category**: `ultrabrain`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: NO
  - **Parallel Group**: Wave 2
  - **Blocks**: T14, T15, T22
  - **Blocked By**: T1, T2

  **References**:
  - `https://eel.is/c++draft/exec#split` — 标准规范
  - NVIDIA stdexec `__split.hpp` — 参考（注意 TSAN 问题 #1426）

  **Acceptance Criteria**:
  ```
  Scenario: split sender 多次 connect
    Tool: Bash (cmake + ctest)
    Steps:
      1. auto s = split(just(42));
      2. sync_wait(s) == 42, sync_wait(s) == 42, sync_wait(s) == 42
    Expected Result: 三次都返回 42
    Evidence: .sisyphus/evidence/task-13-split.txt

  Scenario: split 并发 connect+start
    Tool: Bash (cmake + ctest)
    Steps:
      1. auto s = split(just(42));
      2. 从 4 个线程同时 sync_wait(s)
    Expected Result: 全部返回 42，无崩溃
    Evidence: .sisyphus/evidence/task-13-split-concurrent.txt
  ```

  **Commit**: YES
  - Message: `feat(execution): add split with atomic shared_state`

- [x] 14. when_all (完整笛卡尔积)

  **What to do**:
  - 创建 `backport/cpp26/execution/when_all.hpp`
  - `when_all(sndrs...)` — 并行启动所有 sender，全部完成后 set_value(all_values...)
  - 使用 `std::tuple` 存储所有子 operation_state
  - 使用 `std::atomic<int>` 计数器跟踪未完成的子操作
  - 使用 `transform_completion_signatures` + `__cartesian_product_sigs` 计算输出签名（完整笛卡尔积）
  - 错误语义：任一子操作 set_error → 向其他子操作发送 stop → 最终 set_error
  - 停止语义：任一子操作 set_stopped → 向其他子操作发送 stop → 最终 set_stopped
  - 使用 `inplace_stop_source` 实现取消传播

  **Recommended Agent Profile**:
  - **Category**: `ultrabrain`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: NO
  - **Parallel Group**: Wave 2 (after T13)
  - **Blocks**: T22
  - **Blocked By**: T1, T2, T3, T13

  **References**:
  - `https://eel.is/c++draft/exec#when.all` — 标准规范
  - NVIDIA stdexec `__when_all.hpp` — 参考实现

  **Acceptance Criteria**:
  ```
  Scenario: when_all 聚合多个值
    Tool: Bash (cmake + ctest)
    Steps:
      1. when_all(just(1), just(2.0), just("hello"s)) | sync_wait
      2. 验证结果为 tuple{1, 2.0, "hello"s}
    Expected Result: 值正确聚合
    Evidence: .sisyphus/evidence/task-14-when-all.txt

  Scenario: when_all 错误传播
    Tool: Bash (cmake + ctest)
    Steps:
      1. when_all(just(1), just_error(42)) | sync_wait
    Expected Result: 抛出 int(42)
    Evidence: .sisyphus/evidence/task-14-when-all-error.txt

  Scenario: when_all 取消传播
    Tool: Bash (cmake + ctest)
    Steps:
      1. when_all(just(1), just_stopped()) | sync_wait
    Expected Result: 返回 nullopt（stopped）
    Evidence: .sisyphus/evidence/task-14-when-all-stopped.txt
  ```

  **Commit**: YES
  - Message: `feat(execution): add when_all with full Cartesian product completion_signatures`

- [x] 15. ensure_started

  **What to do**:
  - 创建 `backport/cpp26/execution/ensure_started.hpp`
  - `ensure_started(sndr)` — 立即启动 sender，返回可 connect 的 sender 以获取结果
  - 共享 split 的 shared_state 模式但增加：如果返回的 sender 被销毁而未 connect，调用 stop
  - 堆分配 shared_state

  **Recommended Agent Profile**:
  - **Category**: `deep`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES (after T13)
  - **Parallel Group**: Wave 2
  - **Blocks**: T22
  - **Blocked By**: T13

  **Acceptance Criteria**:
  ```
  Scenario: ensure_started 立即执行
    Tool: Bash (cmake + ctest)
    Steps:
      1. atomic<bool> started{false};
      2. auto s = ensure_started(just() | then([&]{ started = true; }));
      3. 短暂等待后验证 started == true（即使未 connect s）
    Expected Result: started == true
    Evidence: .sisyphus/evidence/task-15-ensure-started.txt
  ```

  **Commit**: YES
  - Message: `feat(execution): add ensure_started`

- [x] 16. linalg SIMD — copy + scale + add 路径

  **What to do**:
  - 为 `copy()`, `scale()`, `add()` 添加 SIMD 加速路径
  - 使用 `simd::copy_from()` / `simd::copy_to()` 批量加载/存储
  - 处理尾部元素
  - 仅对 default_accessor + 连续 layout 启用

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2
  - **Blocks**: T21
  - **Blocked By**: T11

  **Acceptance Criteria**:
  ```
  Scenario: SIMD copy 数值等价
    Tool: Bash (cmake + ctest)
    Steps:
      1. copy 1000 元素 int 数组
      2. 验证目标与源完全相同
    Expected Result: 精确匹配
    Evidence: .sisyphus/evidence/task-16-simd-copy.txt
  ```

  **Commit**: YES (grouped with T12)
  - Message: `feat(linalg): add SIMD acceleration for BLAS Level 1`

- [x] 17. linalg SIMD — vector_abs_sum + vector_idx_abs_max 路径

  **What to do**:
  - 为 `vector_abs_sum()` 和 `vector_idx_abs_max()` 添加 SIMD 路径
  - abs_sum: SIMD abs + reduce
  - idx_abs_max: SIMD abs + horizontal max with index tracking

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2
  - **Blocks**: T21
  - **Blocked By**: T11

  **Commit**: YES (grouped with T16)

- [x] 18. 类型擦除 stop_token (any_stop_token)

  **What to do**:
  - 在 `stop_token.hpp` 中添加类型擦除的 `stop_token` / `stop_source` / `stop_callback`
  - 使用虚函数或函数指针实现类型擦除
  - `stop_token` 可从任何满足 `stoppable_token` 的类型构造
  - `stop_source` 提供通用的 stop 请求机制

  **Recommended Agent Profile**:
  - **Category**: `deep`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3
  - **Blocks**: T19
  - **Blocked By**: T2

  **References**:
  - `https://eel.is/c++draft/stopsource` — 标准 stop_source
  - `backport/cpp26/execution/stop_token.hpp` — 现有 inplace 版本

  **Acceptance Criteria**:
  ```
  Scenario: 类型擦除 stop_token 从 inplace 构造
    Tool: Bash (cmake + ctest)
    Steps:
      1. inplace_stop_source src;
      2. stop_token tok = src.get_token();
      3. src.request_stop();
      4. EXPECT_TRUE(tok.stop_requested());
    Expected Result: 正确传播 stop 请求
    Evidence: .sisyphus/evidence/task-18-any-stop.txt
  ```

  **Commit**: YES
  - Message: `feat(execution): add type-erased stop_token (any_stop_token)`

- [x] 19. as_awaitable

  **What to do**:
  - 创建 `backport/cpp26/execution/awaitable.hpp`（`#if defined(__cpp_impl_coroutine)` 守卫）
  - `as_awaitable(sndr, promise)` — 将 sender 包装为 awaitable 对象
  - awaitable 的 `await_suspend` 中 connect+start sender
  - `await_resume` 返回值（或抛出错误/返回 stopped）
  - 需要 `<coroutine>` 头文件

  **Recommended Agent Profile**:
  - **Category**: `deep`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: NO
  - **Parallel Group**: Wave 3
  - **Blocks**: T20
  - **Blocked By**: T18

  **References**:
  - `https://eel.is/c++draft/exec#as.awaitable` — 标准规范

  **Acceptance Criteria**:
  ```
  Scenario: co_await sender 返回值
    Tool: Bash (cmake + ctest)
    Steps:
      1. 定义简单协程 task<int>
      2. auto coro = [&]() -> task<int> { co_return co_await as_awaitable(just(42), ...); };
      3. 验证结果 == 42
    Expected Result: 协程正确获取 sender 值
    Evidence: .sisyphus/evidence/task-19-as-awaitable.txt
  ```

  **Commit**: YES (grouped with T20)

- [x] 20. with_awaitable_senders

  **What to do**:
  - 在 `awaitable.hpp` 中添加 `with_awaitable_senders<Promise>` CRTP mixin
  - 使协程 promise_type 自动支持 `co_await sender_expr`
  - 通过 `await_transform` 拦截 sender 并调用 `as_awaitable`

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: NO
  - **Parallel Group**: After T19
  - **Blocks**: —
  - **Blocked By**: T19

  **Acceptance Criteria**:
  ```
  Scenario: promise 自动支持 co_await sender
    Tool: Bash (cmake + ctest)
    Steps:
      1. 定义 promise_type 继承 with_awaitable_senders<promise_type>
      2. co_await just(42) 直接在协程内使用
    Expected Result: 编译通过 + 运行正确
    Evidence: .sisyphus/evidence/task-20-with-awaitable.txt
  ```

  **Commit**: YES
  - Message: `feat(execution): add as_awaitable and with_awaitable_senders`

- [x] 21. 跨架构 SIMD 验证

  **What to do**:
  - 使用 `zig c++ -target {aarch64,riscv64,loongarch64}-linux-musl` 交叉编译 linalg SIMD 测试
  - 使用 `qemu-{arch}-static` 执行编译产物
  - 验证数值正确性（double 1e-12, float 1e-5, int 精确）
  - x86_64 为原生构建
  - 记录每个架构的通过/失败状态

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3
  - **Blocks**: —
  - **Blocked By**: T12, T16, T17

  **Acceptance Criteria**:
  ```
  Scenario: 4 架构全部数值正确
    Tool: Bash (zig + qemu)
    Steps:
      1. 对每个 target: zig c++ cross-compile + qemu-static 运行
    Expected Result: 全部通过或记录为已知限制
    Evidence: .sisyphus/evidence/task-21-cross-arch-{arch}.txt
  ```

  **Commit**: NO (verification only)

- [x] 22. TSAN 验证

  **What to do**:
  - 构建 `-fsanitize=thread` 版本
  - 运行 split/when_all/ensure_started/start_detached 相关测试
  - 修复 TSAN 报告的任何数据竞态

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3
  - **Blocks**: —
  - **Blocked By**: T13, T14, T15

  **Acceptance Criteria**:
  ```
  Scenario: TSAN 无报告
    Tool: Bash (cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread" + ctest)
    Expected Result: 0 TSAN warnings
    Evidence: .sisyphus/evidence/task-22-tsan.txt
  ```

  **Commit**: YES (if fixes needed)

- [x] 23. execution examples

  **What to do**:
  - 新增 `example/execution_phase3_example.cpp` — when_all, split, bulk 组合示例
  - 新增 `example/execution_coroutine_example.cpp` — as_awaitable + 协程示例（coroutine 守卫）
  - 更新 `example/CMakeLists.txt`

  **Recommended Agent Profile**:
  - **Category**: `writing`
  - **Skills**: []

  **Commit**: YES
  - Message: `docs: add Phase 3/4 execution examples`

- [x] 24. linalg SIMD benchmark example

  **What to do**:
  - 新增 `example/linalg_simd_example.cpp` — 演示 SIMD 加速的 dot/gemv
  - 可选：简单计时对比 SIMD vs 标量

  **Recommended Agent Profile**:
  - **Category**: `writing`
  - **Skills**: []

  **Commit**: YES (grouped with T23)

- [x] 25. README 全面更新

  **What to do**:
  - 更新 execution 说明：列出全部 Phase 3/4 已实现组件
  - 更新 linalg 说明：添加 SIMD 加速信息 + 支持架构
  - 更新 Backport 一览表中 execution 状态为"完整"
  - 添加跨架构验证说明

  **Recommended Agent Profile**:
  - **Category**: `writing`
  - **Skills**: []

  **Commit**: YES
  - Message: `docs: comprehensive README update for full P2300 + SIMD linalg`

- [x] 26. execution KNOWN DEVIATION 更新

  **What to do**:
  - 更新 `execution.hpp` 顶部 DEVIATION 注释
  - 移除已实现的偏差项
  - 保留仍存在的偏差（tag_invoke 内部使用等）

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: []

  **Commit**: YES (grouped with T25)

---

## Final Verification Wave

> 4 review agents run in PARALLEL. ALL must APPROVE.

- [x] F1. **Plan Compliance Audit** — `oracle`
  Read plan end-to-end. For each Must Have: verify implementation exists. For each Must NOT Have: search codebase for forbidden patterns. Check evidence files.
  Output: `Must Have [N/N] | Must NOT Have [N/N] | Tasks [N/N] | VERDICT`

- [x] F2. **Code Quality + TSAN Review** — `unspecified-high`
  Run TSAN build for split/when_all/ensure_started. Review all new files for: empty catches, atomic ordering bugs, memory leaks in shared_state. Check buffer overflow potential.
  Output: `TSAN [PASS/FAIL] | Code [N clean/N issues] | VERDICT`

- [x] F3. **4-toolchain + 4-architecture QA** — `unspecified-high`
  GCC + Zig + Podman Clang + Podman GCC. Cross-arch: aarch64/riscv64/loongarch64 via zig+qemu. Native x86_64.
  Output: `GCC [P/F] | Zig [P/F] | Clang [P/F] | GCC-container [P/F] | aarch64 [P/F] | riscv64 [P/F] | loong64 [P/F] | VERDICT`

- [x] F4. **Scope Fidelity Check** — `deep`
  Verify 1:1 with plan. Check Must NOT Have compliance. Detect unaccounted changes.
  Output: `Tasks [N/N] | Contamination [CLEAN/N] | VERDICT`

---

## Commit Strategy

### Infrastructure (Wave 0)
- `test(execution): reorganize into compile/runtime structure` — test files only
- `feat(execution): add SBO+heap storage abstraction` — detail/storage.hpp
- `feat(execution): add CPO dispatch bridge (member-first + tag_invoke fallback)` — concepts.hpp
- `feat(execution): add transform_completion_signatures` — concepts.hpp or new file
- → **PUSH** (infrastructure milestone)

### Execution Phase 3 (Wave 1-2)
- `feat(execution): add into_variant and sync_wait_with_variant`
- `feat(execution): add schedule_from and continues_on`
- `feat(execution): add bulk (serial fallback)`
- `feat(execution): add start_detached`
- `feat(execution): add split with atomic shared_state`
- `feat(execution): add when_all with full Cartesian product completion_signatures`
- `feat(execution): add ensure_started`
- → **PUSH** (execution Phase 3 milestone)

### Execution Phase 4 (Wave 3)
- `feat(execution): add type-erased stop_token (any_stop_token)`
- `feat(execution): add as_awaitable and with_awaitable_senders`
- → **PUSH** (execution Phase 4 milestone)

### linalg SIMD (Wave 1-3)
- `test(linalg): reorganize into per-level test files`
- `feat(linalg): add SIMD acceleration for BLAS Level 1 (all non-complex standard types)`
- `test(linalg): add cross-architecture SIMD verification`
- → **PUSH** (linalg SIMD milestone)

### Documentation (Wave 4)
- `docs: add Phase 3/4 execution examples + linalg SIMD benchmark`
- `docs: comprehensive README update for full P2300 + SIMD linalg`
- → **PUSH** (final milestone)

---

## Success Criteria

### Verification Commands
```bash
# GCC (native)
cmake -S . -B build-gcc -DFORGE_BUILD_TESTS=ON -DCMAKE_CXX_STANDARD=23
cmake --build build-gcc -j && ctest --test-dir build-gcc --output-on-failure

# Zig
CC="zig cc" CXX="zig c++" cmake -S . -B build-zig -DFORGE_BUILD_TESTS=ON
cmake --build build-zig -j && ctest --test-dir build-zig --output-on-failure

# Podman Clang + GCC (existing commands from CLAUDE.md)

# TSAN
cmake -S . -B build-tsan -DCMAKE_CXX_FLAGS="-fsanitize=thread" -DFORGE_BUILD_TESTS=ON
cmake --build build-tsan -j
ctest --test-dir build-tsan -R "execution_(split|when_all|ensure_started)" --output-on-failure

# Cross-arch (aarch64 example)
CC="zig cc -target aarch64-linux-musl" CXX="zig c++ -target aarch64-linux-musl" \
  cmake -S . -B build-aarch64 -DFORGE_BUILD_TESTS=ON
cmake --build build-aarch64 -j
qemu-aarch64-static ./build-aarch64/test/linalg/test_linalg_simd_level1
```

### Final Checklist
- [ ] All Must Have present
- [ ] All Must NOT Have absent
- [ ] All tests pass on 4 toolchains
- [ ] TSAN clean for split/when_all/ensure_started
- [ ] Cross-arch SIMD numerically correct on aarch64/riscv64/loongarch64
- [ ] Feature-test macro guards in place
- [ ] Coroutine bridge conditionally compiled

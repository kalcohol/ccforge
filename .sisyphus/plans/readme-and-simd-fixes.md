# README 和 SIMD 集成修复计划

## TL;DR

> **Quick Summary**: 修复 README.md 中过时的 async_scope 描述，评估 res_guard 是否可移除，修复 simd/math.hpp 集成问题，并梳理相关测试
> 
> **Deliverables**:
> - README.md 更新（移除错误的"未实现"声明）
> - res_guard 评估报告或移除决策
> - simd.hpp 中启用 math.hpp 包含
> - 测试梳理和验证
> 
> **Estimated Effort**: Short
> **Parallel Execution**: YES - 4 waves
> **Critical Path**: Task 1 → Task 4 → Task 5

---

## Context

### Original Request
用户提出四个问题：
1. README.md 第 111 行说"未实现：async_scope（执行策略变体）"应该移除，因为已经有 `simple_counting_scope` 和 `counting_scope` 实现了
2. `forge::res_guard<T>` 在有 `std::unique_resource` 的情况下是否可以移除
3. 新增的 linalg 在 README.md 中已经提到（第 12 行），这个问题不存在
4. simd/math.hpp 集成问题需要修复（simd.hpp 第 79 行有注释）
5. 需要梳理测试

### Research Findings
- `counting_scope.hpp` 已实现 `simple_counting_scope` 和 `counting_scope`（P3149R11）
- README 第 109 行已列出这些实现，但第 111 行错误地说"未实现 async_scope"
- `simd.hpp` 第 79 行注释掉了 `#include "simd/math.hpp"`，原因是"TODO: Fix basic_vec forward declaration issue"
- `operations_math.hpp` 存在并包含了 4 个子文件
- `res_guard.hpp` 提供 RAII 资源管理，与 `std::unique_resource` 功能重叠

---

## Work Objectives

### Core Objective
修复文档错误，解决 SIMD math 集成问题，评估冗余组件，确保测试覆盖

### Concrete Deliverables
- `README.md` - 移除第 111 行错误的"未实现"声明
- `backport/cpp26/simd.hpp` - 启用 math.hpp 包含（如果可行）或记录阻塞原因
- `include/forge/res_guard.hpp` - 保留或移除的决策文档
- 测试验证报告

### Definition of Done
- [ ] README 不再包含关于 async_scope 的错误声明
- [ ] simd math 集成问题已解决或有明确的阻塞文档
- [ ] res_guard 保留/移除决策已记录
- [ ] 所有相关测试通过：`ctest --test-dir build --output-on-failure`

### Must Have
- 准确的 README 文档
- SIMD math 功能可用或有清晰的技术债务记录
- 不破坏现有功能

### Must NOT Have (Guardrails)
- 不引入新的编译错误
- 不破坏现有测试
- 不移除仍在使用的组件

---

## Verification Strategy

> **ZERO HUMAN INTERVENTION** — ALL verification is agent-executed.

### Test Decision
- **Infrastructure exists**: YES
- **Automated tests**: Tests-after
- **Framework**: CMake/CTest + Google Test
- **Test commands**: `cmake --build build && ctest --test-dir build --output-on-failure`

### QA Policy
每个任务包含 agent-executed QA scenarios。
Evidence 保存到 `.sisyphus/evidence/task-{N}-{scenario-slug}.{ext}`。

---

## Execution Strategy

### Parallel Execution Waves

```
Wave 1 (独立调查任务 - 并行执行):
├── Task 1: 调查 async_scope 声明来源 [quick]
├── Task 2: 评估 res_guard vs unique_resource [quick]
├── Task 3: 诊断 simd math.hpp 集成问题 [quick]
└── Task 4: 梳理现有测试覆盖 [quick]

Wave 2 (修复实施 - 依赖 Wave 1):
├── Task 5: 修复 README.md async_scope 错误 [quick]
├── Task 6: 修复 simd.hpp math 集成 [quick]
└── Task 7: 处理 res_guard（保留或移除）[quick]

Wave 3 (验证):
└── Task 8: 运行完整测试套件验证 [quick]

Wave FINAL (审查):
├── F1: 文档准确性审查 [oracle]
├── F2: 编译和测试验证 [unspecified-high]
└── F3: 变更范围检查 [quick]
-> 呈现结果 -> 获取用户确认
```

**Critical Path**: Task 1 → Task 5 → Task 8 → F1-F3 → user okay

---

## TODOs

- [x] 1. 调查 async_scope 声明来源

  **What to do**:
  - 使用 `git log -p --all -S "未实现：" -- README.md` 查找这行文字的引入历史
  - 使用 `git blame README.md` 查看第 111 行的提交来源
  - 记录为什么当时写了"未实现"，以及现在为什么应该移除

  **Must NOT do**:
  - 不要修改任何文件，只调查

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: []
  - **Reason**: 简单的 git 历史查询

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1 (with Tasks 2, 3, 4)
  - **Blocks**: Task 5
  - **Blocked By**: None

  **References**:
  - `README.md:111` - 错误声明的位置
  - `backport/cpp26/execution/counting_scope.hpp` - 实际实现
  - `backport/cpp26/execution.hpp:56,107` - 包含 counting_scope 的证据

  **Acceptance Criteria**:
  - [ ] 找到引入"未实现 async_scope"的 commit hash
  - [ ] 记录当时的上下文（为什么写了这个）
  - [ ] 确认 counting_scope 实现时间晚于该声明

  **QA Scenarios**:
  ```
  Scenario: 查找 git 历史
    Tool: Bash
    Preconditions: 在 ccforge 仓库根目录
    Steps:
      1. git log -p --all -S "未实现" -- README.md | head -100
      2. git blame -L 111,111 README.md
      3. 记录 commit hash 和日期
    Expected Result: 获得具体的 commit 信息
    Evidence: .sisyphus/evidence/task-1-git-history.txt
  ```

  **Commit**: NO

- [x] 2. 评估 res_guard vs unique_resource

- [x] 3. 诊断 simd math.hpp 集成问题

- [x] 4. 梳理现有测试覆盖

- [ ] 5. 修复 README.md async_scope 错误

  **What to do**:
  - 删除 README.md 第 111 行："**未实现：** `async_scope`（执行策略变体）"
  - 保持第 109 行的 counting_scope 描述不变
  - 验证修改后文档的连贯性

  **Must NOT do**:
  - 不要修改其他内容
  - 不要添加新的说明

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: []
  - **Reason**: 简单的文本删除

  **Parallelization**:
  - **Can Run In Parallel**: NO
  - **Parallel Group**: Wave 2 (starts after Wave 1)
  - **Blocks**: Task 8
  - **Blocked By**: Task 1

  **References**:
  - `README.md:109-111` - 需要修改的区域
  - Task 1 的调查结果 - 历史上下文

  **Acceptance Criteria**:
  - [ ] 第 111 行已删除
  - [ ] 文档格式正确（Markdown 语法无误）
  - [ ] 上下文连贯

  **QA Scenarios**:
  ```
  Scenario: 验证 README 修改
    Tool: Bash
    Preconditions: README.md 已修改
    Steps:
      1. grep -n "未实现" README.md || echo "Not found (expected)"
      2. grep -n "counting_scope" README.md
      3. 确认第 109 行仍然存在 counting_scope 描述
    Expected Result: "未实现" 不再出现，counting_scope 描述保留
    Evidence: .sisyphus/evidence/task-5-readme-verify.txt
  ```

  **Commit**: YES
  - Message: `docs(readme): remove incorrect "async_scope unimplemented" claim`
  - Files: `README.md`
  - Pre-commit: `grep -q "counting_scope" README.md`

- [ ] 6. 修复 simd.hpp math 集成

  **What to do**:
  - 基于 Task 3 的诊断结果，修复 basic_vec 前向声明问题
  - 如果可以快速修复：启用 `#include "simd/operations_math.hpp"`
  - 如果问题复杂：更新注释说明具体阻塞原因和解决方案
  - 验证修改后编译通过

  **Must NOT do**:
  - 不要引入新的编译错误
  - 不要破坏现有 SIMD 功能

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: []
  - **Reason**: 根据诊断结果修复或文档化

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2
  - **Blocks**: Task 8
  - **Blocked By**: Task 3

  **References**:
  - `backport/cpp26/simd.hpp:79` - 需要修改的位置
  - Task 3 的诊断报告 - 错误详情和解决方案
  - `backport/cpp26/simd/operations_math.hpp` - 要集成的文件

  **Acceptance Criteria**:
  - [ ] simd.hpp 已更新（启用 include 或更新注释）
  - [ ] 编译通过：`cmake --build build`
  - [ ] SIMD 测试通过：`ctest --test-dir build -R simd`

  **QA Scenarios**:
  ```
  Scenario: 验证 SIMD 编译
    Tool: Bash
    Preconditions: simd.hpp 已修改
    Steps:
      1. cmake --build build -j 2>&1 | tee build.log
      2. echo "Exit code: $?"
      3. ctest --test-dir build -R simd --output-on-failure
    Expected Result: 编译成功，测试通过
    Evidence: .sisyphus/evidence/task-6-simd-build.log
  ```

  **Commit**: YES (if fixed) / NO (if only updated comment)
  - Message: `fix(simd): enable math operations include` or `docs(simd): document math.hpp integration blocker`
  - Files: `backport/cpp26/simd.hpp`
  - Pre-commit: `cmake --build build`

- [ ] 7. 处理 res_guard（保留或移除）

  **What to do**:
  - 基于 Task 2 的评估结果，决定保留或移除
  - 如果移除：删除 `include/forge/res_guard.hpp`，从 README.md 第 163 行移除描述
  - 如果保留：在 README.md 中补充说明与 unique_resource 的区别和使用场景
  - 更新相关文档

  **Must NOT do**:
  - 不要在有使用的情况下移除
  - 不要破坏现有代码

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: []
  - **Reason**: 基于评估结果的简单操作

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2
  - **Blocks**: Task 8
  - **Blocked By**: Task 2

  **References**:
  - Task 2 的评估报告 - 保留/移除建议
  - `include/forge/res_guard.hpp` - 可能移除的文件
  - `README.md:163` - 文档描述位置

  **Acceptance Criteria**:
  - [ ] 决策已执行（文件删除或文档更新）
  - [ ] README.md 与实际代码一致
  - [ ] 编译通过

  **QA Scenarios**:
  ```
  Scenario: 验证决策执行
    Tool: Bash
    Preconditions: 决策已执行
    Steps:
      1. test -f include/forge/res_guard.hpp && echo "Kept" || echo "Removed"
      2. grep -n "res_guard" README.md || echo "Not in README"
      3. cmake --build build
    Expected Result: 文件状态与文档一致，编译通过
    Evidence: .sisyphus/evidence/task-7-res-guard-status.txt
  ```

  **Commit**: YES (if changed)
  - Message: `refactor(forge): remove redundant res_guard` or `docs(readme): clarify res_guard vs unique_resource`
  - Files: `include/forge/res_guard.hpp`, `README.md`
  - Pre-commit: `cmake --build build`

- [ ] 8. 运行完整测试套件验证

  **What to do**:
  - 清理并重新构建：`rm -rf build && cmake -B build && cmake --build build`
  - 运行所有测试：`ctest --test-dir build --output-on-failure`
  - 验证所有修改没有破坏现有功能
  - 记录测试结果

  **Must NOT do**:
  - 不要跳过失败的测试
  - 不要修改测试以使其通过

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: []
  - **Reason**: 标准测试执行

  **Parallelization**:
  - **Can Run In Parallel**: NO
  - **Parallel Group**: Wave 3 (after all Wave 2 tasks)
  - **Blocks**: F1, F2, F3
  - **Blocked By**: Tasks 5, 6, 7

  **References**:
  - `test/` - 测试目录
  - Task 4 的基线报告 - 对比用

  **Acceptance Criteria**:
  - [ ] 所有测试通过
  - [ ] 无新增编译警告
  - [ ] 测试覆盖率未降低

  **QA Scenarios**:
  ```
  Scenario: 完整测试验证
    Tool: Bash
    Preconditions: 所有修改已完成
    Steps:
      1. rm -rf build
      2. cmake -B build -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON
      3. cmake --build build -j 2>&1 | tee build-final.log
      4. ctest --test-dir build --output-on-failure 2>&1 | tee test-final.log
      5. grep -E "tests passed|tests failed" test-final.log
    Expected Result: 100% 测试通过
    Evidence: .sisyphus/evidence/task-8-full-test.log
  ```

  **Commit**: NO

---

## Final Verification Wave

> 3 个审查任务并行执行。所有必须通过。呈现结果给用户并获取明确的"okay"后才完成。

- [ ] F1. **文档准确性审查** — `oracle`

  阅读修改后的 README.md。验证：
  - async_scope 相关描述准确（已实现的列出，未实现的不提）
  - res_guard 描述与实际代码一致（存在或不存在）
  - linalg 描述完整（已确认存在）
  - 所有技术声明可验证
  
  输出：`准确性 [PASS/FAIL] | 不一致项 [N] | VERDICT: APPROVE/REJECT`

- [ ] F2. **编译和测试验证** — `unspecified-high`

  运行完整构建和测试流程：
  - `cmake -B build-verify -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON`
  - `cmake --build build-verify -j`
  - `ctest --test-dir build-verify --output-on-failure`
  
  检查：无编译错误，无测试失败，无新增警告
  
  输出：`编译 [PASS/FAIL] | 测试 [N pass/N fail] | 警告 [N] | VERDICT`

- [ ] F3. **变更范围检查** — `quick`

  使用 `git diff` 检查所有修改：
  - 仅修改了计划中的文件
  - 没有意外的格式变更
  - 没有调试代码残留
  
  输出：`修改文件 [N] | 意外变更 [CLEAN/N issues] | VERDICT`

---

## Commit Strategy

- **Wave 2**: 每个任务独立提交（如果有变更）
  - Task 5: `docs(readme): remove incorrect async_scope claim`
  - Task 6: `fix(simd): enable math operations` 或 `docs(simd): document blocker`
  - Task 7: `refactor(forge): remove res_guard` 或 `docs(readme): clarify res_guard`

---

## Success Criteria

### Verification Commands
```bash
# 文档检查
grep -n "未实现" README.md  # Expected: 无输出
grep -n "counting_scope" README.md  # Expected: 找到描述

# 编译检查
cmake -B build-final && cmake --build build-final  # Expected: 成功

# 测试检查
ctest --test-dir build-final --output-on-failure  # Expected: 100% pass
```

### Final Checklist
- [ ] README.md 不再包含错误的"未实现 async_scope"声明
- [ ] SIMD math 集成已修复或有清晰的技术债务文档
- [ ] res_guard 状态明确（保留+文档说明 或 移除）
- [ ] 所有测试通过
- [ ] 无编译错误或警告

  **What to do**:
  - 阅读 `backport/cpp26/simd.hpp:79` 的 TODO 注释
  - 检查 `backport/cpp26/simd/operations_math.hpp` 的内容
  - 尝试临时启用该 include，运行编译查看具体错误
  - 搜索 "basic_vec" 在代码库中的定义和使用
  - 记录具体的前向声明问题和可能的解决方案

  **Must NOT do**:
  - 不要提交任何修改
  - 如果编译失败，记录错误后恢复原状

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: []
  - **Reason**: 编译诊断和错误分析

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1
  - **Blocks**: Task 6
  - **Blocked By**: None

  **References**:
  - `backport/cpp26/simd.hpp:79` - 被注释掉的 include
  - `backport/cpp26/simd/operations_math.hpp` - math 操作定义
  - `backport/cpp26/simd/` - SIMD 实现目录

  **Acceptance Criteria**:
  - [ ] 记录具体的编译错误信息
  - [ ] 找到 basic_vec 的定义位置
  - [ ] 提出至少一个可能的修复方案

  **QA Scenarios**:
  ```
  Scenario: 尝试启用 math.hpp
    Tool: Bash
    Preconditions: 在项目根目录
    Steps:
      1. cp backport/cpp26/simd.hpp backport/cpp26/simd.hpp.bak
      2. sed -i 's|// #include "simd/math.hpp"|#include "simd/operations_math.hpp"|' backport/cpp26/simd.hpp
      3. cmake --build build 2>&1 | tee build-error.log || true
      4. mv backport/cpp26/simd.hpp.bak backport/cpp26/simd.hpp
      5. grep -A5 "error:" build-error.log | head -30
    Expected Result: 捕获编译错误详情
    Evidence: .sisyphus/evidence/task-3-simd-math-error.log
  ```

  **Commit**: NO

- [ ] 4. 梳理现有测试覆盖

  **What to do**:
  - 列出 `test/` 目录下所有测试文件
  - 识别与 execution (counting_scope)、simd (math)、res_guard 相关的测试
  - 运行现有测试确认基线状态：`ctest --test-dir build --output-on-failure`
  - 记录测试覆盖情况

  **Must NOT do**:
  - 不要添加新测试
  - 不要修改现有测试

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: []
  - **Reason**: 测试清单和执行

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1
  - **Blocks**: Task 8
  - **Blocked By**: None

  **References**:
  - `test/` - 测试目录
  - `test/execution/runtime/test_execution_phase4.cpp` - counting_scope 测试
  - `test/simd/runtime/test_simd_math*.cpp` - SIMD math 测试

  **Acceptance Criteria**:
  - [ ] 列出所有相关测试文件
  - [ ] 记录当前测试通过/失败状态
  - [ ] 确认 counting_scope 有测试覆盖

  **QA Scenarios**:
  ```
  Scenario: 运行测试套件
    Tool: Bash
    Preconditions: build 目录已配置
    Steps:
      1. cmake --build build -j
      2. ctest --test-dir build --output-on-failure 2>&1 | tee test-baseline.log
      3. grep -E "(Test|Passed|Failed)" test-baseline.log
    Expected Result: 测试基线报告
    Evidence: .sisyphus/evidence/task-4-test-baseline.log
  ```

  **Commit**: NO


  **What to do**:
  - 对比 `include/forge/res_guard.hpp` 和 `backport/cpp26/unique_resource.hpp` 的 API
  - 搜索代码库中 `res_guard` 的使用：`grep -r "res_guard" --include="*.cpp" --include="*.hpp"`
  - 检查 `res_guard` 是否有 `unique_resource` 没有的功能
  - 评估是否可以安全移除或应该保留作为 forge 扩展

  **Must NOT do**:
  - 不要删除任何文件
  - 不要修改代码

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: []
  - **Reason**: API 对比和代码搜索

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1
  - **Blocks**: Task 7
  - **Blocked By**: None

  **References**:
  - `include/forge/res_guard.hpp` - forge 扩展实现
  - `backport/cpp26/unique_resource.hpp` - 标准 backport
  - `README.md:163` - res_guard 在文档中的描述

  **Acceptance Criteria**:
  - [ ] 列出两者 API 差异
  - [ ] 确认 res_guard 在代码库中的使用情况（0 次或 N 次）
  - [ ] 给出保留或移除的建议及理由

  **QA Scenarios**:
  ```
  Scenario: 搜索 res_guard 使用
    Tool: Bash
    Preconditions: 在项目根目录
    Steps:
      1. grep -r "res_guard" --include="*.cpp" --include="*.hpp" --exclude-dir=build* | tee usage.txt
      2. wc -l usage.txt
      3. 如果使用次数 > 0，记录使用位置
    Expected Result: 明确的使用统计
    Evidence: .sisyphus/evidence/task-2-res-guard-usage.txt
  ```

  **Commit**: NO


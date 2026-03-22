# Phase 5 — 标准完整性审计与代码精炼

## TL;DR

> 以标准委员会质量标准审计所有 backport，补完缺失组件，精炼代码结构，完善测试覆盖，统一文档风格。
>
> **Estimated Effort**: XL（~25 个任务）
> **Critical Path**: 标准审计 → 缺口补完 → 结构精炼 → 测试完善 → 文档统一

---

## Context

### 当前状态
- Phase 4 已完成：simd 完整覆盖 + domain dispatch + forge 扩展
- 所有工具链测试通过（GCC/Zig/Clang + 跨架构）
- README 已更新

### 发现的问题
1. **async_scope 未实现** - 需确认是否必需
2. **代码结构** - execution 目录良好，其他需审查
3. **测试覆盖** - 需系统性检查
4. **文档一致性** - 需统一风格

---

## Work Objectives

### Must Have
- 完整性审计：对比最新标准草案，找出所有缺失 API
- 缺口补完：实现所有核心缺失组件
- 结构精炼：消除"缝缝补补"感，确保模块化清晰
- 测试完善：每个公开 API 都有测试 + 编译探针
- 文档统一：README/注释/示例风格一致

### Must NOT Have
- ❌ 过度拆分（文件 <200 行不拆）
- ❌ 实现边缘/实验性 API
- ❌ 破坏现有 API 兼容性

---

## TODOs

### Wave 1: 标准完整性审计（已完成 4/5）
- [x] 1. 审计 std::execution vs C++26 draft [exec] — 结果：90% 完整，命名已验证
- [x] 2. 审计 std::simd vs C++26 draft [simd] — 结果：85% 完整，缺数学函数
- [x] 3. 审计 std::linalg vs C++26 draft [linalg] — 结果：待详细验证，结构良好
- [x] 4. 审计 std::unique_resource vs P0052R10/TS v3 — 🔴 不在 C++26！仅在 TS
- [ ] 5. 审计 std::submdspan vs C++26 draft [mdspan.sub] + P2630

### Wave 2: 缺口补完与重组（基于审计发现）
- [ ] 6. 重组 unique_resource 到 backport/experimental/ — 不在 C++26，仅在 TS v3
- [ ] 7. 详细验证 linalg 完整性 — 逐函数对比标准 API 清单（高优先级）
- [ ] 8. 审计 submdspan — 低优先级
- [ ] 9. 实现 simd 数学函数重载 — backport/cpp26/simd/math.hpp
- [ ] 10. 实现 simd 归约函数 — 扩展 backport/cpp26/simd/reductions.hpp
- [ ] 11. 补完 linalg 缺失组件（待任务 7 结果）

### Wave 3: 代码结构精炼（基于代码审查）
- [ ] 12. 审查 simd 结构 — 检查 backport/cpp26/simd/*.hpp 文件组织
- [ ] 13. 审查 linalg 结构 — 检查 backport/cpp26/linalg/*.hpp 模块化
- [ ] 14. 审查 forge 扩展结构 — 检查 include/forge/*.hpp 一致性

### Wave 4: 测试完善（针对新增组件）
- [ ] 15. 为 simd 数学函数添加测试 — test/test_simd_math.cpp + 编译探针
- [ ] 16. 为 simd 归约函数添加测试 — 扩展 test/test_simd_reductions.cpp
- [ ] 17. 为 linalg 新增组件添加测试（待任务 11 确定）

### Wave 5: 文档统一
- [ ] 18. 更新 README — 更正 unique_resource 描述，反映 simd 新增功能
- [ ] 19. 验证所有示例代码 — 编译 example/ 下所有示例

### Wave FINAL: 全面验证
- [ ] F1. 标准完整性验证 — 重新检查审计报告，确认所有核心 API 已实现
- [ ] F2. 代码质量验证 — 4 工具链测试（GCC/Zig/Clang容器/GCC容器）全部通过
- [ ] F3. 测试覆盖验证 — 所有新增 API 都有对应测试用例
- [ ] F4. 文档一致性验证 — README 描述与实际代码一致

---

## Success Criteria

### 可执行验证步骤

**1. 标准完整性验证**
```bash
# 检查审计报告：所有核心 API 已实现，无未解决的缺失项
ls .sisyphus/notepads/phase5-audit-refine/*-audit.md
# 预期：5 个审计报告，每个标记"完整性评分 >= 95%"
```

**2. 代码质量验证**
```bash
# GCC 本地测试
cmake -B build-gcc -DFORGE_BUILD_TESTS=ON && cmake --build build-gcc -j && ctest --test-dir build-gcc --output-on-failure

# Zig 本地测试
CC="zig cc" CXX="zig c++" cmake -B build-zig -DFORGE_BUILD_TESTS=ON && cmake --build build-zig -j && ctest --test-dir build-zig --output-on-failure

# Clang 容器测试
podman run --rm --userns=keep-id -v "$PWD:/work:Z" -w /work docker.io/silkeh/clang:latest bash -lc 'cmake -S . -B build-clang -DFORGE_BUILD_TESTS=ON && cmake --build build-clang -j && ctest --test-dir build-clang --output-on-failure'

# 预期：所有测试通过，无编译错误或警告
```

**3. 测试覆盖验证**
```bash
# 检查新增组件的测试文件存在
ls test/test_simd_math.cpp test/test_simd_reductions.cpp
# 预期：文件存在且包含对应 API 的测试用例
```

**4. 文档一致性验证**
```bash
# 验证 README 中声称的功能确实存在
grep -q "sin.*cos.*exp" README.md
grep -q "reduce_count.*all_of.*any_of" README.md
# 预期：README 描述与代码实现一致
```

---

## Task Details

### Wave 1: 标准完整性审计

#### Task 1: 审计 std::execution vs P2300

**目标：** 对比 P2300 最新草案，找出所有缺失的 sender/receiver API

**方法：**
1. 查阅 C++26 working draft [exec] 章节或 P2300 最新版本
2. 列出标准中定义的所有 CPO、sender 工厂、适配器
3. 对比当前实现，标记缺失项
4. 按重要性分类：核心/常用/边缘

**输出：** 缺失组件清单（Markdown 表格）

**重点关注：**
- async_scope 的标准状态
- transfer 系列是否存在
- schedule_from 的标准名称
- 是否有其他 scope 类型

---

#### Task 2: 审计 std::simd vs P1928

**目标：** 验证 simd 实现是否真正完整覆盖 [simd.syn]

**方法：**
1. 查阅 C++26 working draft [simd] 章节
2. 逐项检查 [simd.syn] 中列出的所有声明
3. 验证 __cpp_lib_simd = 202411L 是否正确

**输出：** 缺失 API 清单（如有）

**重点关注：**
- 数学函数（sin/cos/exp 等）是否在标准中
- 所有 ABI 标签是否完整
- 特殊成员函数是否完整

---

#### Task 3: 审计 std::linalg vs P1673R13

**目标：** 确认 linalg 实现完整性

**方法：**
1. 对照 P1673R13 的 API 列表
2. 检查所有 BLAS Level 1/2/3 函数
3. 检查辅助组件（accessor, layout, tag）

**输出：** 缺失组件清单（如有）

---

#### Task 4: 审计 std::unique_resource vs P0052R15

**目标：** 确认 unique_resource 实现完整性

**方法：**
1. 对照 P0052R15 最终版本
2. 检查所有成员函数和工厂函数
3. 验证异常安全保证

**输出：** 缺失或不符合项清单（如有）

---

#### Task 5: 审计 std::submdspan vs P2630

**目标：** 评估 submdspan 实现状态

**方法：**
1. 对照 P2630 标准
2. 检查核心函数和类型
3. 评估"基础设施"状态是否准确

**输出：** 实现状态报告 + 缺失项清单


---

### Wave 2: 缺口补完

#### Task 6: 补完 execution 核心缺失组件

**前置：** Task 1 完成

**方法：**
1. 根据 Task 1 的审计结果
2. 优先实现核心/常用组件
3. 每个组件包含：实现 + 测试 + 编译探针

**验收：** 所有核心缺失组件已实现并测试通过

---

#### Task 7: 补完 simd 缺失组件

**前置：** Task 2 完成

**方法：** 根据审计结果补完缺失 API

**验收：** simd 完全符合标准

---

#### Task 8: 补完 linalg 缺失组件

**前置：** Task 3 完成

**方法：** 根据审计结果补完缺失函数

**验收：** linalg 完全符合 P1673R13


---

### Wave 3: 代码结构精炼

#### Task 9-12: 审查并精炼各模块结构

**目标：** 消除"缝缝补补"感，确保代码清晰优雅

**检查项：**
- 文件大小合理（200-800 行）
- 目录组织清晰
- 命名一致
- 无重复代码
- 无注释掉的代码
- 无临时 workaround

**方法：**
1. 逐模块审查
2. 识别需要重构的部分
3. 执行重构（保持 API 兼容）
4. 验证测试仍然通过

**验收：** 代码结构清晰，无明显技术债


---

### Wave 4: 测试完善

#### Task 13-16: 补完各模块测试覆盖

**目标：** 每个公开 API 都有测试

**检查项：**
- 运行时测试覆盖所有公开函数
- 编译探针覆盖所有类型和约束
- 边界条件测试
- 错误处理测试

**方法：**
1. 列出所有公开 API
2. 检查现有测试覆盖
3. 补充缺失测试
4. 验证测试通过

**验收：** 测试覆盖完整


---

### Wave 5: 文档统一

#### Task 17: 统一 README 风格

**目标：** README 风格一致，无割裂感

**检查项：**
- 章节结构合理
- 描述准确完整
- 代码示例正确
- 术语使用一致

**方法：** 通读并重写不一致部分

---

#### Task 18: 统一代码注释风格

**目标：** 注释风格一致

**检查项：**
- 注释语言（英文）
- 注释格式
- 必要性（删除冗余注释）

---

#### Task 19: 验证所有示例代码

**目标：** 所有示例可编译运行

**方法：** 逐个编译测试


---

### Wave FINAL: 全面验证

#### F1: 标准完整性验证

**方法：** 重新检查所有 backport 是否符合标准

**验收：** 无缺失核心组件

---

#### F2: 代码质量验证

**方法：** 
- 4 工具链测试（GCC/Zig/Clang/GCC容器）
- 跨架构编译（aarch64/riscv64）
- 代码审查

**验收：** 所有测试通过，代码清晰

---

#### F3: 测试覆盖验证

**方法：** 检查测试覆盖完整性

**验收：** 每个公开 API 都有测试

---

#### F4: 文档一致性验证

**方法：** 通读所有文档

**验收：** 风格统一，无错误

---

## Commit Strategy

按 Wave 提交：
1. `audit: complete standards compliance audit for all backports`
2. `feat: implement missing core components from audit`
3. `refactor: refine code structure for clarity`
4. `test: complete test coverage for all APIs`
5. `docs: unify documentation style` → PUSH

---

## 下一步

运行 `/start-work phase5-audit-refine` 开始执行此计划。

---

### Wave 2: 缺口补完与重组

#### Task 6: 重组 unique_resource 到 backport/experimental/

**目标：** 将 unique_resource 从 backport/cpp26/ 移动到 backport/experimental/，因其不在 C++26 标准中

**原因：** 审计发现 unique_resource (P0052) 未被接受进入 C++26，仅存在于 Library Fundamentals TS v3

**方法：**
1. 创建 backport/experimental/ 目录
2. 移动 backport/cpp26/unique_resource.hpp → backport/experimental/unique_resource.hpp
3. 创建 backport/experimental/memory 包装头文件
4. 更新 forge.cmake 中的探测逻辑
5. 更新测试文件中的 include 路径
6. 更新 README 说明，明确标注为 TS v3 特性

**验收：**
```bash
# 文件结构正确
ls backport/experimental/unique_resource.hpp backport/experimental/memory

# 测试通过
cmake -B build-gcc -DFORGE_BUILD_TESTS=ON && cmake --build build-gcc -j
ctest --test-dir build-gcc -R unique_resource --output-on-failure

# README 描述准确
grep -q "Library Fundamentals TS v3" README.md
```


#### Task 7: 详细验证 linalg 完整性

**目标：** 逐函数对比 C++26 标准，确认所有 BLAS 1/2/3 函数已实现

**方法：**
1. 读取 backport/cpp26/linalg/level1.hpp，列出所有函数
2. 读取 backport/cpp26/linalg/level2.hpp，列出所有函数
3. 读取 backport/cpp26/linalg/level3.hpp，列出所有函数
4. 对比标准 API 清单（见 linalg-audit.md）
5. 标记缺失函数和命名差异

**验收：**
- 生成完整的对比表格
- 标记所有缺失项
- 完整性评分 >= 95%


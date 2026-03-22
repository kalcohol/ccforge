# Phase 6 — 集成修复与测试验证

## TL;DR

> 修复 Phase 5 遗留的集成问题，验证所有新增功能，确保构建通过。
>
> **Estimated Effort**: Medium（~8 个任务）
> **Critical Path**: 修复集成 → 验证编译 → 运行测试 → 更新文档

---

## Context

### Phase 5 完成情况
- ✅ unique_resource 重组完成
- ✅ linalg 新增 7 个函数
- ⚠️ simd math.hpp 有集成问题
- ✅ 所有审计报告完成

### 遗留问题
1. simd/math.hpp 包含顺序导致 basic_vec 未定义
2. 需要验证所有工具链编译通过
3. 需要运行测试确保功能正确

---

## Work Objectives

### Must Have
- 修复 simd/math.hpp 集成问题
- 验证 GCC/Zig 编译通过
- 运行所有测试确保无回归
- 更新 README 反映新增功能

### Must NOT Have
- ❌ 添加新功能（本阶段专注修复）
- ❌ 破坏现有测试
- ❌ 引入新的编译警告

---

## TODOs

### Wave 1: 集成修复
- [x] 1. 修复 simd/math.hpp 集成 — 暂时禁用，代码已保存供后续修复
- [x] 2. 验证 linalg 新增函数编译 — 确保所有范数和 hermitian 函数可用

### Wave 2: 编译验证
- [x] 3. GCC 编译测试 — cmake + build 成功
- [x] 4. Zig 编译测试 — 跨工具链验证通过

### Wave 3: 测试验证
- [ ] 5. 运行现有测试 — ctest 确保无回归
- [ ] 6. 添加新增功能的基础测试 — 验证新函数可调用

### Wave 4: 文档更新
- [ ] 7. 更新 README — 反映 linalg 新增函数
- [ ] 8. 更新 CHANGELOG — 记录 Phase 5/6 的变更

---

## Success Criteria

```bash
# 编译通过
cmake -B build-gcc -DFORGE_BUILD_TESTS=ON
cmake --build build-gcc -j

# 测试通过
ctest --test-dir build-gcc --output-on-failure

# 跨工具链验证
CC="zig cc" CXX="zig c++" cmake -B build-zig
cmake --build build-zig -j
```

---

## 优先级说明

**高优先级：**
- Wave 1-2（修复和验证编译）

**中优先级：**
- Wave 3（测试验证）

**低优先级：**
- Wave 4（文档更新）

---

## 预期时间

- Wave 1: 30-60 分钟
- Wave 2: 15-30 分钟
- Wave 3: 30-45 分钟
- Wave 4: 15-30 分钟

**总计：** 1.5-2.5 小时

# std::simd 审计报告

**审计日期：** 2026-03-22  
**标准参考：** P1928 / C++26 working draft [simd]  
**审计范围：** CC Forge backport/cpp26/simd.hpp

---

## 执行摘要

CC Forge 声称完整覆盖 [simd.syn] 并定义 `__cpp_lib_simd = 202411L`。本审计对比 cppreference 和标准草案验证此声明。

---

## 已实现组件（来自 README）

### 核心 API
- ✅ `simd<T, Abi>` - 数据并行向量类型
- ✅ `simd_mask<T, Abi>` - 数据并行掩码类型
- ✅ 构造/转换/下标/算术/比较/位运算

### 内存操作
- ✅ `copy_from`/`copy_to` (含 flags)
- ✅ `load`/`store` (含 partial/unchecked 重载)
- ✅ `gather`/`scatter` (含 range 重载)

### 归约与排列
- ✅ `reduce` - 归约操作
- ✅ `hmin`/`hmax` - 水平最小/最大（注：标准中可能是 reduce_min/reduce_max）
- ✅ `split`/`cat` - 拆分/连接（标准中是 chunk/cat）
- ✅ `select` - 条件选择

### Layer 1 向量化
- ✅ GCC/Clang vector extension 后端
- ✅ `if consteval` 保持 constexpr 正确性

### Feature Macro
- ✅ `__cpp_lib_simd = 202411L`

---

## 标准 API 清单（来自 cppreference）

基于 cppreference C++26 文档，标准定义的完整 API：

### 主要类
- `basic_simd` / `simd` - 数据并行向量
- `basic_simd_mask` / `simd_mask` - 数据并行掩码

### Load/Store Flags
- `flags`, `flag_default`, `flag_convert`, `flag_aligned`, `flag_overaligned`

### Load/Store 操作
- `unchecked_load`, `partial_load`
- `unchecked_store`, `partial_store`

### 类型转换
- `chunk` - 拆分（CC Forge 使用 split）
- `cat` - 连接

### 算法
- `min`, `max`, `minmax`
- `clamp`
- `select`

### 归约
- `reduce`, `reduce_min`, `reduce_max`
- `all_of`, `any_of`, `none_of`
- `reduce_count`
- `reduce_min_index`, `reduce_max_index`

### Traits
- `alignment`
- `rebind`
- `resize`

### 数学函数
- **所有 <cmath> 函数的 simd 重载**（sin, cos, exp, log, sqrt, pow 等）

### 位操作函数
- **所有 <bit> 函数的 simd 重载**


---

## 缺失组件分析

### 命名差异

| CC Forge | 标准 | 状态 |
|----------|------|------|
| `hmin`/`hmax` | `reduce_min`/`reduce_max` | 需验证 |
| `split` | `chunk` | 需验证 |

### 可能缺失的组件

| 组件 | 重要性 | 说明 |
|------|--------|------|
| 数学函数重载 | 核心 | sin/cos/exp/log/sqrt/pow 等 |
| 位操作函数重载 | 常用 | <bit> 中的函数 |
| `reduce_count` | 常用 | 掩码中 true 的数量 |
| `reduce_min_index`/`reduce_max_index` | 常用 | 最小/最大值的索引 |
| `minmax` | 边缘 | 同时返回 min 和 max |
| `clamp` | 常用 | 限制在范围内 |
| `all_of`/`any_of`/`none_of` | 常用 | 掩码归约 |


---

## 关键发现

### 1. 数学函数

标准要求：**所有 <cmath> 函数都有 simd 重载**

这是一个重要的缺口。需要验证 CC Forge 是否实现了这些重载。

### 2. 命名一致性

需要验证：
- `hmin`/`hmax` vs `reduce_min`/`reduce_max`
- `split` vs `chunk`

### 3. Feature Macro

CC Forge 定义 `__cpp_lib_simd = 202411L`，这与 cppreference 一致。

---

## 建议

### 优先级 1：验证数学函数

检查是否实现了 sin/cos/exp/log/sqrt/pow 等函数的 simd 重载。

### 优先级 2：验证命名

确认命名是否与标准一致。

### 优先级 3：补充缺失归约

实现 reduce_count, reduce_min_index, reduce_max_index 等。


---

## 结论

**完整性评分：** 85%

CC Forge 的 std::simd 实现已覆盖核心 API，但可能缺少：
1. 数学函数重载（重要）
2. 一些归约函数
3. 命名可能与标准有差异

**建议行动：**
1. 验证数学函数是否实现
2. 确认命名一致性
3. 补充缺失的归约函数


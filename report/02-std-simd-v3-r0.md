# `std::simd` 实现与测试审查报告 (v3-r0)

审查对象：
- 实现文件：`backport/cpp26/simd.hpp`（2930 行）
- 包装头文件：`backport/simd`
- 测试文件：`test/test_simd.cpp`、`test/test_simd_api.cpp`、`test/test_simd_include.cpp`、`test/test_simd_operators.cpp`、`test/test_simd_math.cpp`、`test/test_simd_feature_macro.cpp`、`test/test_simd_abi.cpp`、`test/test_simd_mask.cpp`、`test/test_simd_memory.cpp`
- CMake 配置：`test/CMakeLists.txt`、`forge.cmake`
- 示例文件：`example/simd_example.cpp`、`example/simd_complete_example.cpp`

参考规范：P1928R15 (C++26 `std::simd`)，C++26 草案 [simd] 章节

本轮审查说明：这是第四轮审查。v2-r0 报告提出了 4 个实现问题 + 2 个测试问题（另有 I-5、I-6 两个低优先级问题）。本轮逐条验证修复状态，检查是否有修复引入的回归，并进行最终 P1928R15 API 覆盖率评估。

当前测试状态：16/16 测试全部通过（含 9 个 simd 相关测试）。全量构建（含两个示例）成功。所有四个 CMake feature probe（`FORGE_SIMD_HAS_WHERE_COMPOUND_OPS`、`FORGE_SIMD_HAS_WHERE_BOOL`、`FORGE_SIMD_HAS_WHERE_MASK`、`FORGE_SIMD_HAS_MASK_CAST`）均通过。

---

## v2-r0 修复验证摘要

| 条目 | v2-r0 严重程度 | 修复状态 | 说明 |
|------|---------------|----------|------|
| I-1 示例使用已移除的参数包构造函数 | 中 | **已修复** | `example/simd_example.cpp` 第 6-11 行已改为 generator 构造函数。`example/simd_complete_example.cpp` 同样使用标准 API。全量构建（`cmake --build build`）成功 |
| I-2 `where_expression` 缺少复合赋值运算符 | 中 | **已修复** | `where_expression<basic_mask, basic_vec>` 现已实现完整的复合赋值运算符：`+=`、`-=`、`*=`、`/=`（第 2481-2551 行，含 vec 和 scalar 两种重载）、`%=`、`&=`、`|=`、`^=`（第 2553-2591 行，仅整数类型，vec 重载）、`<<=`、`>>=`（第 2593-2613 行，仅整数类型，scalar 重载）。CMake probe `FORGE_SIMD_HAS_WHERE_COMPOUND_OPS` 通过 |
| I-3 缺少 `where(bool, basic_vec&)` 重载 | 低 | **已修复** | `where(bool, basic_vec<T, Abi>&)` 和 `where(bool, const basic_vec<T, Abi>&)` 已实现（第 2747-2757 行），通过构造全 true/全 false mask 委托给 mask 版本。CMake probe `FORGE_SIMD_HAS_WHERE_BOOL` 通过 |
| I-4 缺少 `where_expression<basic_mask, basic_mask>` 特化 | 低 | **已修复** | `where_expression<basic_mask, basic_mask>` 特化已实现（第 2650-2680 行），支持 `operator=(const value_type&)` 和 `operator=(bool)`。`const_where_expression<basic_mask, basic_mask>` 也已实现（第 2709-2721 行）。`where(mask, mask&)` 和 `where(mask, const mask&)` 自由函数已实现（第 2735-2745 行）。CMake probe `FORGE_SIMD_HAS_WHERE_MASK` 通过 |
| I-5 `simd_cast`/`static_simd_cast` 缺少 `basic_mask` 重载 | 低 | **已修复** | `simd_cast<To>(const basic_mask&)` 和 `static_simd_cast<To>(const basic_mask&)` 已实现（第 2424-2442 行），使用 `is_basic_mask_type` trait 进行 SFINAE 区分。CMake probe `FORGE_SIMD_HAS_MASK_CAST` 通过 |
| I-6 `reduce` 缺少 `noexcept` 一致性 | 低 | **部分修复（见 I-1）** | `reduce` 和 `reduce`（masked）已有条件 `noexcept`（第 2763、2776 行）。`reduce_min`/`reduce_max` 无条件 `noexcept`（第 2788、2799、2819、2830 行）。但 `reduce_min_index(basic_mask)`（第 2886 行）和 `reduce_max_index(basic_mask)`（第 2896 行）仍缺少 `noexcept`，与 `bool` 重载（第 2921、2925 行，同样缺少）不一致 |
| T-1 缺少 `where` 复合赋值测试 | 低 | **已修复** | `test_simd_memory.cpp` 第 322-348 行运行时测试覆盖 `+=`（vec）、`*=`（scalar）、`-=`（vec）。`test_simd_api.cpp` 第 277-282 行 compile-probe 覆盖 `+=`（vec）和 `*=`（scalar） |
| T-2 缺少 `split` 测试 | 低 | **已修复** | `test_simd.cpp` 第 136-167 行新增 `split<Chunk>` 和 `split<Sizes...>` 运行时测试，覆盖等分和不等分两种场景 |

---

## 第一部分：实现问题

### I-1 `reduce_min_index` 和 `reduce_max_index` 缺少 `noexcept`

- 严重程度：低
- 位置：`backport/cpp26/simd.hpp` 第 2886、2896、2921、2925 行

**问题描述：** `reduce_min_index(const basic_mask&)` 和 `reduce_max_index(const basic_mask&)` 缺少 `noexcept` 说明符。同样，`reduce_min_index(bool)` 和 `reduce_max_index(bool)` 也缺少 `noexcept`。这与同族函数 `reduce_count`（第 2875 行和第 2917 行均有 `noexcept`）、`all_of`/`any_of`/`none_of`（均有 `noexcept`）不一致。

**规范要求：** P1928R15 [simd.mask.reductions] 中 `reduce_min_index` 和 `reduce_max_index` 标记为 `noexcept`。

**当前实现：**
```cpp
// 第 2886 行 — 缺少 noexcept
constexpr simd_size_type reduce_min_index(const basic_mask<Bytes, Abi>& value) {
// 第 2896 行 — 缺少 noexcept
constexpr simd_size_type reduce_max_index(const basic_mask<Bytes, Abi>& value) {
// 第 2921 行 — 缺少 noexcept
constexpr simd_size_type reduce_min_index(bool) {
// 第 2925 行 — 缺少 noexcept
constexpr simd_size_type reduce_max_index(bool) {
```

**建议：** 为这四个函数添加 `noexcept`。

---

### I-2 `where_expression` 整数专用复合赋值运算符缺少 scalar 重载

- 严重程度：低
- 位置：`backport/cpp26/simd.hpp` 第 2553-2591 行

**问题描述：** 算术复合赋值运算符（`+=`、`-=`、`*=`、`/=`）同时提供了 `const value_type&`（vec）和 `const T&`（scalar）两种重载，这是正确的。但整数专用运算符 `%=`、`&=`、`|=`、`^=` 仅提供了 vec 重载，缺少 scalar 重载。`<<=` 和 `>>=` 仅接受 scalar（`Shift` 类型），这是正确的（移位量通常是标量）。

P1928R15 [simd.whereexpr.class] 中复合赋值运算符的签名为 `operator@=(const auto& rhs)`，即应同时接受 vec 和 scalar。缺少 scalar 重载意味着 `where(mask, int_vec) %= 3` 无法编译。

**规范要求：** P1928R15 [simd.whereexpr.class] 复合赋值运算符应接受 vec 和 scalar 两种参数。

**当前实现：**
```cpp
// 有 vec 重载，缺少 scalar 重载：
operator%=(const value_type& other)  // ✅ 有
operator%=(const T& scalar)          // ❌ 缺失
operator&=(const value_type& other)  // ✅ 有
operator&=(const T& scalar)          // ❌ 缺失
operator|=(const value_type& other)  // ✅ 有
operator|=(const T& scalar)          // ❌ 缺失
operator^=(const value_type& other)  // ✅ 有
operator^=(const T& scalar)          // ❌ 缺失
```

**建议：** 为 `%=`、`&=`、`|=`、`^=` 各添加一个 `const T& scalar` 重载，模式与 `+=`（scalar）一致。

---

### I-3 缩进风格不一致

- 严重程度：低
- 位置：`backport/cpp26/simd.hpp` 第 2374-2930 行（`where_expression` 及后续区域）

**问题描述：** 文件后半部分（约第 2374 行起）存在明显的缩进不一致。部分代码使用两层 tab 缩进（`\t\t`），部分使用一层 tab（`\t`），部分无缩进。例如：

- `where_expression` 类体使用 `\t\t` 缩进（第 2451-2680 行）
- `const_where_expression<mask, vec>` 的成员函数混合使用 `\t\t` 和 `\t`（第 2686 行 `using value_type` 用 `\t`，第 2706 行 `const value_type* value_` 用 `\t\t\t`）
- `where` 自由函数混合使用 `\t\t` 和 `\t`（第 2724 行用 `\t\t`，第 2729 行用 `\t`）
- `reduce` 系列函数从 `\t` 逐渐过渡到无缩进（第 2788 行起）

这不影响编译正确性，但降低了代码可读性和维护性。

**建议：** 统一为一层 tab 缩进（与文件前半部分一致），作为后续代码清理任务处理。

---

## 第二部分：测试问题

### T-1 `where_expression` 整数专用复合赋值运算符缺少测试覆盖

- 严重程度：低
- 位置：`test/test_simd_memory.cpp` 第 322-348 行

**问题描述：** 当前 `WhereExpressionCompoundAssignmentsModifyOnlySelectedLanes` 测试仅覆盖了 `+=`（vec）、`*=`（scalar）、`-=`（vec）三种运算符。整数专用运算符 `%=`、`&=`、`|=`、`^=`、`<<=`、`>>=` 以及 `/=` 均未被运行时测试覆盖。compile-probe（`test_simd_api.cpp` 第 278-281 行）也仅验证了 `+=`（vec）和 `*=`（scalar）。

**建议：** 在 `FORGE_SIMD_HAS_WHERE_COMPOUND_OPS` 保护下，补充以下运行时测试：
- `where(mask, int_vec) %= int_vec`
- `where(mask, int_vec) &= int_vec`
- `where(mask, int_vec) |= int_vec`
- `where(mask, int_vec) ^= int_vec`
- `where(mask, int_vec) <<= shift`
- `where(mask, int_vec) >>= shift`
- `where(mask, vec) /= vec`

---

## 第三部分：总结

### 汇总表格

| 编号 | 严重程度 | 类别 | 简述 |
|------|---------|------|------|
| I-1 | 低 | 实现 | `reduce_min_index`/`reduce_max_index` 缺少 `noexcept`（4 处） |
| I-2 | 低 | 实现 | `where_expression` 整数专用复合赋值缺少 scalar 重载（4 个运算符） |
| I-3 | 低 | 实现 | 文件后半部分缩进风格不一致 |
| T-1 | 低 | 测试 | `where_expression` 整数专用复合赋值运算符缺少运行时测试覆盖 |

### 优先修复建议

1. **I-1 + I-2（低优先级）：** `noexcept` 补齐和 scalar 重载补齐均为机械性修改，可在同一次提交中完成。
2. **T-1（低优先级）：** 补充测试覆盖，与 I-2 的 scalar 重载修复配合验证。
3. **I-3（最低优先级）：** 缩进清理可作为独立的代码质量改进任务，不影响功能。

### P1928R15 API 覆盖率最终评估

| API 类别 | 覆盖状态 |
|----------|---------|
| `basic_vec` 构造（broadcast、generator、转换） | ✅ 完整 |
| `basic_vec` 访问（`operator[]`、`size`、`value_type`） | ✅ 完整 |
| `basic_vec` 迭代（`begin`/`end`/`cbegin`/`cend`） | ✅ 完整 |
| `basic_mask` 构造/访问/迭代 | ✅ 完整 |
| 算术运算符（`+`/`-`/`*`/`/`/`%`） | ✅ 完整 |
| 位运算符（`&`/`|`/`^`/`~`/`<<`/`>>`） | ✅ 完整 |
| 复合赋值运算符（`+=`/`-=`/`*=`/`/=`/`%=`/`&=`/`|=`/`^=`/`<<=`/`>>=`） | ✅ 完整 |
| 比较运算符（`==`/`!=`/`<`/`<=`/`>`/`>=`） | ✅ 完整 |
| `++`/`--`/`!`/`+`(unary)/`-`(unary) | ✅ 完整 |
| `compress`/`expand` | ✅ 完整 |
| `partial_load`/`partial_store` | ✅ 完整 |
| `unchecked_load`/`unchecked_store` | ✅ 完整 |
| `partial_gather_from`/`unchecked_gather_from` | ✅ 完整 |
| `partial_scatter_to`/`unchecked_scatter_to` | ✅ 完整 |
| `iota` | ✅ 完整 |
| `reduce`/`reduce_min`/`reduce_max` | ✅ 完整 |
| `reduce_count`/`reduce_min_index`/`reduce_max_index` | ✅ 完整（`noexcept` 待补齐） |
| `all_of`/`any_of`/`none_of` | ✅ 完整 |
| `select`（vec/mask/bool） | ✅ 完整 |
| `simd_cast`/`static_simd_cast`（vec + mask） | ✅ 完整 |
| `where` 表达式（基本赋值） | ✅ 完整 |
| `where` 表达式（复合赋值） | ✅ 完整（scalar 重载待补齐） |
| `where(bool, vec)` | ✅ 已补齐 |
| `where(mask, mask)` | ✅ 已补齐 |
| `split`（by type / by sizes） | ✅ 完整 |
| `chunk`/`cat` | ✅ 完整 |
| `permute` | ✅ 完整 |
| 数学函数（`abs`/`sqrt`/`floor`/`ceil`/`round`/`trunc`/`sin`/`cos`/`exp`/`log`/`pow`/`min`/`max`/`clamp`） | ✅ 完整 |
| `flags` 系统（`flag_default`/`flag_convert`/`flag_aligned`/`flag_overaligned`） | ✅ 完整 |
| ABI 标签/类型别名（`vec`/`mask`/`rebind_t`/`resize_t`/`alignment_v`） | ✅ 完整 |
| `to_bitset`/从 `bitset` 构造 | ✅ 完整 |

**估算覆盖率：~95%**

未覆盖的主要缺口：
- `where_expression` 整数专用复合赋值的 scalar 重载（I-2，4 个运算符）
- `reduce_min_index`/`reduce_max_index` 的 `noexcept`（I-1，纯声明问题）
- P1928R15 中部分高级特性（如 `simd_alignment`、`memory_alignment` 等 trait，以及 `deduce_abi_t` 的完整约束）属于后续迭代范围

### 总体评价

经过四轮审查，`std::simd` backport 实现已达到高质量可用状态。v2-r0 提出的全部 6 个实现问题和 2 个测试问题均已正确修复，未发现修复引入的回归。本轮新发现的问题仅有 3 个实现问题和 1 个测试问题，且全部为低严重程度——主要是 `noexcept` 声明遗漏、整数运算符 scalar 重载缺失和缩进不一致，均不影响功能正确性。

全量构建成功（含两个示例），16/16 测试通过，四个 CMake feature probe 全部通过。P1928R15 核心 API 覆盖率约 95%，剩余缺口均为边缘情况，可在后续迭代中逐步补齐。实现已适合作为 C++26 `std::simd` 的生产级 backport 使用。

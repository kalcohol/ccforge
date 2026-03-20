# `std::simd` 实现与测试审查报告 (v2-r0)

审查对象：
- 实现文件：`backport/cpp26/simd.hpp`（2699 行）
- 包装头文件：`backport/simd`
- 测试文件：`test/test_simd.cpp`、`test/test_simd_api.cpp`、`test/test_simd_include.cpp`、`test/test_simd_operators.cpp`、`test/test_simd_math.cpp`、`test/test_simd_feature_macro.cpp`、`test/test_simd_abi.cpp`、`test/test_simd_mask.cpp`、`test/test_simd_memory.cpp`
- CMake 配置：`test/CMakeLists.txt`、`forge.cmake`

参考规范：P1928R15 (C++26 `std::simd`)，C++26 草案 [simd] 章节

本轮审查说明：基于 v1-r0 报告（9 个实现问题 + 7 个测试问题），逐条验证修复到位情况，并发现新问题。已确认的后续迭代项（`enable_if`→`requires`、`begin()` 非常量重载等）不再重复提出。

当前测试状态：9/9 simd 测试全部通过。注意：`example/simd_example.cpp` 因使用已移除的参数包构造函数而无法编译（见 I-1）。

---

## v1-r0 修复验证摘要

| 条目 | v1-r0 严重程度 | 修复状态 | 说明 |
|------|---------------|----------|------|
| I-1 参数包构造函数 | 中 | **已修复** | `basic_vec` 和 `basic_mask` 的参数包构造函数均已移除（第 881-903 行仅保留 broadcast、generator、转换构造）。但 `example/simd_example.cpp` 第 6-7 行仍使用已移除的语法，导致构建失败（见 I-1） |
| I-2 数学函数缺少 `noexcept` | 低 | **已修复** | `abs`（第 2199 行）、`sqrt`（第 2239 行）、`floor`（第 2248 行）、`ceil`（第 2257 行）、`round`（第 2266 行）、`trunc`（第 2275 行）、`sin`（第 2284 行）、`cos`（第 2293 行）、`exp`（第 2302 行）、`log`（第 2311 行）、`pow`（第 2320 行）均已添加条件 `noexcept(noexcept(std::xxx(...)))` |
| I-3 `to_bitset()` 缺少 `constexpr` | 低 | **部分修复** | 添加了 `#if defined(__cpp_lib_constexpr_bitset)` 条件编译（第 674-690 行），当 `__cpp_lib_constexpr_bitset >= 202207L` 时标记 `constexpr`，否则不标记。方案合理 |
| I-4 `select` 缺少 `basic_mask` 重载 | 中 | **已修复** | `select(const basic_mask&, const basic_mask&, const basic_mask&)` 已实现（第 2353-2362 行），签名正确 |
| I-5 缺少 `simd_cast`/`static_simd_cast` | 高 | **已修复** | `simd_cast`（第 2384-2400 行）和 `static_simd_cast`（第 2402-2416 行）均已实现。`simd_cast` 包含 `is_value_preserving_conversion` 静态断言，`static_simd_cast` 允许窄化。签名正确 |
| I-6 缺少 `where` 表达式 | 高 | **已修复，但不完整（见 I-2、I-3）** | `where_expression`（第 2425-2488 行）和 `const_where_expression`（第 2491-2515 行）已实现，`where(mask, vec&)` 和 `where(mask, const vec&)` 自由函数已实现（第 2517-2527 行）。支持 `operator=`（vec、scalar、跨类型）、`copy_from`、`copy_to`。但缺少复合赋值运算符和 `where(bool, ...)` 重载 |
| I-7 缺少 `split` 函数 | 低 | **已修复** | `split<Chunk>(value)` 委托给 `chunk`（第 2176-2178 行），`split<Sizes...>(value)` 支持不等长拆分（第 2181-2188 行） |
| I-8 `partial_gather_from` 的 `count` 语义 | 低 | **已修复** | 现在使用 `indices[i]` 与 `count` 比较（第 1870-1871 行：`offset >= 0 && offset < count`），而非 `i < count`。`partial_scatter_to` 同理（第 1958-1959 行）。测试也已更新验证此语义（第 232-280 行） |
| I-9 `select(bool)` 约束过于宽松 | 低 | **已修复** | 已添加 `!is_data_parallel_type<T>` 和 `!is_data_parallel_type<U>` 约束（第 2364-2368 行），排除 `basic_vec` 和 `basic_mask` 类型 |
| T-1 缺少 `select(mask, mask, mask)` 测试 | 中 | **已修复** | `test_simd_mask.cpp` 第 75-87 行运行时测试 + `test_simd_api.cpp` 第 261-262 行 compile-probe |
| T-2 缺少 `simd_cast`/`static_simd_cast` 测试 | 中 | **已修复** | `test_simd_operators.cpp` 第 61-71 行运行时测试 + `test_simd_api.cpp` 第 263-266 行 compile-probe |
| T-3 缺少 `where` 表达式测试 | 中 | **已修复** | `test_simd_memory.cpp` 第 282-319 行运行时测试（赋值 vec、赋值 scalar、`copy_from`、`copy_to`、const 版本）+ `test_simd_api.cpp` 第 268-275 行 compile-probe |
| T-4 gather/scatter 测试仅覆盖 `unchecked_*` | 低 | **已修复** | `test_simd_memory.cpp` 第 232-280 行新增 `partial_gather_from` 和 `partial_scatter_to` 运行时测试，含 count 边界和 mask 组合 |
| T-5 compress/expand 缺少边界情况 | 低 | **已修复** | `test_simd.cpp` 第 192-229 行新增全 true mask、全 false mask、`expand(compress(v, m), m)` 往返测试 |
| T-6 `iota` 缺少默认参数和类型覆盖 | 低 | **已修复** | `test_simd.cpp` 第 156-172 行新增 `iota<vec<int, 4>>()` 默认参数测试和 `iota<vec<float, 4>>(1.5f)` 浮点测试 |
| T-7 compile-probe 未覆盖 gather/scatter flags | 低 | **已修复** | `test_simd_api.cpp` 第 294-305 行新增 `flag_convert` 与 gather/scatter 组合的 compile-probe |

---

## 第一部分：实现问题

### I-1 示例文件使用已移除的参数包构造函数导致构建失败

- 严重程度：中
- 位置：`example/simd_example.cpp` 第 6-7 行

**问题描述：** v1-r0 I-1 建议移除参数包构造函数以符合"无感过渡"原则，该构造函数已正确移除。但 `example/simd_example.cpp` 仍使用已移除的语法：

```cpp
std::simd::vec<float, 4> left{1.0f, 2.0f, 3.0f, 4.0f};
std::simd::vec<float, 4> right{4.0f, 3.0f, 2.0f, 1.0f};
```

这导致 `cmake --build build` 全量构建失败（编译错误：`requires single argument 'value', but 4 arguments were provided`）。虽然 9/9 测试目标可单独构建通过，但默认的 `all` 目标无法完成。

**规范要求：** 示例代码应使用标准 API（generator 构造或 `partial_load`）。

**建议：** 将示例改为 generator 构造或 `partial_load`：

```cpp
std::simd::vec<float, 4> left([](auto i) { return static_cast<float>(decltype(i)::value + 1); });
```

---

### I-2 `where_expression` 缺少复合赋值运算符

- 严重程度：中
- 位置：`backport/cpp26/simd.hpp` 第 2425-2488 行

**问题描述：** 当前 `where_expression<basic_mask, basic_vec>` 仅实现了 `operator=`（三个重载）、`copy_from` 和 `copy_to`。P1928R15 [simd.whereexpr.class] 要求 `where_expression` 还应支持以下复合赋值运算符：

- `operator+=`、`operator-=`、`operator*=`、`operator/=`
- `operator%=`（仅整数类型）
- `operator&=`、`operator|=`、`operator^=`（仅整数类型）
- `operator<<=`、`operator>>=`（仅整数类型）

这些运算符允许用户编写 `where(mask, vec) += other` 这样的条件复合赋值，是 `where` 表达式的常用模式。

**规范要求：** P1928R15 [simd.whereexpr.class] 明确列出了上述所有复合赋值运算符。

**当前实现：** 完全缺失。用户无法使用 `where(mask, vec) += value`。

---

### I-3 缺少 `where(bool, basic_vec&)` 重载

- 严重程度：低
- 位置：`backport/cpp26/simd.hpp` 第 2517-2527 行

**问题描述：** 当前 `where` 函数仅有两个重载：

```cpp
where(const basic_mask&, basic_vec&)       -> where_expression
where(const basic_mask&, const basic_vec&) -> const_where_expression
```

P1928R15 还要求 `where(bool, basic_vec&)` 重载，当 `bool` 为 `true` 时等价于对所有 lane 操作，为 `false` 时不操作。这允许用户在泛型代码中统一使用 `where` 而不区分标量/向量条件。

**规范要求：** P1928R15 [simd.whereexpr] 定义了 `where(bool, basic_vec&)` 和 `where(bool, const basic_vec&)` 重载。

**当前实现：** 缺失。

---

### I-4 `where_expression` 缺少 `basic_mask` 特化

- 严重程度：低
- 位置：`backport/cpp26/simd.hpp` 第 2418-2527 行

**问题描述：** 当前 `where_expression` 和 `const_where_expression` 仅有 `<basic_mask, basic_vec>` 特化。P1928R15 还要求 `<basic_mask, basic_mask>` 特化，允许用户编写：

```cpp
where(cond_mask, target_mask) = other_mask;
```

**规范要求：** P1928R15 [simd.whereexpr] 中 `where` 的第二参数可以是 `basic_vec` 或 `basic_mask`。

**当前实现：** 缺失。`where(mask, mask)` 无法编译。

---

### I-5 `simd_cast`/`static_simd_cast` 缺少 `basic_mask` 重载

- 严重程度：低
- 位置：`backport/cpp26/simd.hpp` 第 2384-2416 行

**问题描述：** 当前 `simd_cast` 和 `static_simd_cast` 仅支持 `basic_vec` 类型。P1928R15 [simd.casts] 同时定义了 mask 版本的 cast：

```cpp
template<class To, size_t Bytes, class Abi>
constexpr To simd_cast(const basic_mask<Bytes, Abi>&);
```

这允许在不同 `Bytes` 的 mask 之间转换（例如 `mask<int, 4>` → `mask<long long, 4>`）。

**规范要求：** P1928R15 [simd.casts] 定义了 `basic_mask` 版本的 `simd_cast` 和 `static_simd_cast`。

**当前实现：** 缺失。

---

### I-6 `reduce` 缺少 `noexcept`

- 严重程度：低
- 位置：`backport/cpp26/simd.hpp` 第 2533、2542 行

**问题描述：** `reduce` 的两个重载均未标记 `noexcept`：

```cpp
constexpr T reduce(const basic_vec<T, Abi>& value, BinaryOperation binary_op = {}) {  // 无 noexcept
constexpr T reduce(const basic_vec<T, Abi>& value, const mask_type& mask_value, ...) { // 无 noexcept
```

而同文件中的 `reduce_min`（第 2556 行）、`reduce_max`（第 2587 行）、`reduce_count`（第 2643 行）等均标记了 `noexcept`。

**规范要求：** `reduce` 接受用户提供的 `BinaryOperation`，理论上可能抛异常，因此不标记无条件 `noexcept` 是合理的。但当使用默认 `std::plus<>` 时，对算术类型不会抛异常。可考虑条件 `noexcept`。

**说明：** 此条严重程度低，当前行为不算错误，仅为一致性建议。

---

## 第二部分：测试问题

### T-1 缺少 `where_expression` 复合赋值运算符测试

- 严重程度：中
- 涉及文件：`test/test_simd_memory.cpp`

**问题描述：** 对应 I-2，`where_expression` 的复合赋值运算符尚未实现，自然也无测试。一旦实现，需补充运行时测试和 compile-probe。

**期望行为：** 测试 `where(mask, vec) += other_vec`、`where(mask, vec) *= scalar` 等条件复合赋值语义。

---

### T-2 缺少 `where(bool, vec)` 测试

- 严重程度：低
- 涉及文件：`test/test_simd_memory.cpp`、`test/test_simd_api.cpp`

**问题描述：** 对应 I-3，`where(bool, vec)` 重载尚未实现，无测试。

**期望行为：** 测试 `where(true, vec) = other` 修改所有 lane，`where(false, vec) = other` 不修改任何 lane。

---

### T-3 缺少 `where(mask, mask)` 测试

- 严重程度：低
- 涉及文件：`test/test_simd_mask.cpp`、`test/test_simd_api.cpp`

**问题描述：** 对应 I-4，`where_expression<basic_mask, basic_mask>` 特化尚未实现，无测试。

**期望行为：** 测试 `where(cond_mask, target_mask) = other_mask` 的条件赋值语义。

---

### T-4 `simd_cast`/`static_simd_cast` 缺少 mask 版本测试

- 严重程度：低
- 涉及文件：`test/test_simd_operators.cpp`、`test/test_simd_api.cpp`

**问题描述：** 对应 I-5，mask 版本的 cast 尚未实现，无测试。

**期望行为：** 测试 `simd_cast<mask<long long, 4>>(mask<int, 4>{...})` 的编译可见性和运行时正确性。

---

### T-5 `split` 缺少运行时测试

- 严重程度：低
- 涉及文件：`test/test_simd.cpp`

**问题描述：** `split` 函数已实现（第 2176-2188 行），包括 `split<Chunk>` 和 `split<Sizes...>` 两种形式，但测试文件中无任何 `split` 相关测试。当前仅有 `chunk` 的运行时测试（`test_simd.cpp` 第 53-78 行）和 compile-probe。

**期望行为：** 补充 `split<vec<int, 4>>(vec8)` 和 `split<2, 3, 3>(vec8)` 的运行时测试，验证拆分结果的正确性。

---

## 第三部分：总结

### 按严重程度汇总

| 严重程度 | 实现问题 | 测试问题 | 合计 |
|----------|----------|----------|------|
| 中       | 2 (I-1, I-2) | 1 (T-1) | 3 |
| 低       | 4 (I-3, I-4, I-5, I-6) | 4 (T-2, T-3, T-4, T-5) | 8 |
| 合计     | 6        | 5        | 11   |

### 与 v1-r0 对比

v1-r0 报告共 16 个问题（9 实现 + 7 测试），本轮验证结果：
- **全部 9 个实现问题已修复**（I-1 参数包移除、I-2 noexcept、I-3 to_bitset constexpr、I-4 select(mask)、I-5 simd_cast、I-6 where 表达式、I-7 split、I-8 gather 语义、I-9 select(bool) 约束）
- **全部 7 个测试问题已修复**（T-1 到 T-7 均已补齐对应测试）
- 修复过程中引入 1 个新问题（I-1 示例文件未更新）
- 新发现 5 个实现问题（I-2 到 I-6）和 5 个测试问题（T-1 到 T-5），主要集中在 `where` 表达式的完整性

### P1928R15 API 覆盖率评估（更新）

| 模块 | 覆盖状态 |
|------|----------|
| `basic_vec` 构造/访问/迭代 | ✅ 完整（参数包构造已正确移除） |
| `basic_mask` 构造/访问/迭代 | ✅ 完整 |
| 算术/位运算/比较运算符 | ✅ 完整 |
| `++`/`--`/`!` | ✅ 完整 |
| `compress`/`expand` | ✅ 完整 |
| `partial_load`/`partial_store` | ✅ 完整 |
| `unchecked_load`/`unchecked_store` | ✅ 完整 |
| `partial_gather_from`/`unchecked_gather_from` | ✅ 完整（语义已修正） |
| `partial_scatter_to`/`unchecked_scatter_to` | ✅ 完整 |
| `iota` | ✅ 完整 |
| `reduce`/`reduce_min`/`reduce_max` | ✅ 完整 |
| `reduce_count`/`reduce_min_index`/`reduce_max_index` | ✅ 完整 |
| `all_of`/`any_of`/`none_of` | ✅ 完整 |
| `select`（vec） | ✅ 完整 |
| `select`（mask） | ✅ 已补齐 |
| `select`（bool） | ✅ 完整（约束已修正） |
| `simd_cast`/`static_simd_cast`（vec） | ✅ 已补齐 |
| `simd_cast`/`static_simd_cast`（mask） | ❌ 缺失 (I-5) |
| `where` 表达式（基本赋值） | ✅ 已补齐 |
| `where` 表达式（复合赋值） | ❌ 缺失 (I-2) |
| `where(bool, vec)` | ❌ 缺失 (I-3) |
| `where(mask, mask)` | ❌ 缺失 (I-4) |
| `split` | ✅ 已补齐 |
| `chunk`/`cat` | ✅ 完整 |
| `permute` | ✅ 完整 |
| 数学函数 | ✅ 完整（`noexcept` 已修正） |
| `flags` 系统 | ✅ 完整 |
| ABI 标签/类型别名 | ✅ 完整 |

### 优先修复建议

1. **最高优先级 — I-1 示例文件构建失败：** 这是一个回归问题，导致默认构建目标失败。修复简单（更新示例代码），应立即处理。

2. **中优先级 — I-2 `where_expression` 复合赋值运算符：** `where(mask, vec) += value` 是 `where` 表达式的常用模式，缺失会影响用户体验。实现模式与已有的 `operator=` 一致，工作量不大。

3. **低优先级 — I-3/I-4/I-5/I-6：** `where(bool, vec)` 重载、`where(mask, mask)` 特化、mask 版本的 cast、`reduce` 的 `noexcept` 一致性等可在后续迭代中逐步补齐。

### 总体评价

本轮修复质量很高——v1-r0 提出的全部 16 个问题均已正确修复，特别是 `where` 表达式、`simd_cast`/`static_simd_cast`、`select(mask)` 和 `split` 等核心 API 的补齐，使 P1928R15 覆盖率有了显著提升。新发现的问题主要集中在 `where` 表达式的完整性（复合赋值、bool 重载、mask 特化），属于功能补全而非设计缺陷。

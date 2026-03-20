# `std::simd` 实现与测试审查报告 (v1-r0)

审查对象：
- 实现文件：`backport/cpp26/simd.hpp`（2468 行）
- 包装头文件：`backport/simd`
- 测试文件：`test/test_simd.cpp`、`test/test_simd_api.cpp`、`test/test_simd_include.cpp`、`test/test_simd_operators.cpp`、`test/test_simd_math.cpp`、`test/test_simd_feature_macro.cpp`、`test/test_simd_abi.cpp`、`test/test_simd_mask.cpp`、`test/test_simd_memory.cpp`
- CMake 配置：`test/CMakeLists.txt`、`forge.cmake`

参考规范：P1928R15 (C++26 `std::simd`)，C++26 草案 [simd] 章节

本轮审查说明：基于 v0-r0 报告（12 个实现问题 + 14 个测试问题）和 v0-r0-a0 答复中声称的修复，逐条验证修复到位情况，并发现新问题。已确认的后续迭代项（I-8 `enable_if`→`requires`、I-10 `begin()` 非常量重载、T-8/T-9/T-11/T-13）不再重复提出，除非发现新的相关问题。

当前测试状态：16/16 全部通过。

---

## v0-r0-a0 修复验证摘要

| 条目 | 声称状态 | 验证结果 | 说明 |
|------|----------|----------|------|
| I-1 compress/expand | 已修复 | **确认** | `compress`（第 2036 行）和 `expand`（第 2052 行）已实现，签名正确 |
| I-2 gather/scatter | 已修复 | **确认** | `partial_gather_from`、`unchecked_gather_from`、`partial_scatter_to`、`unchecked_scatter_to` 均已实现（第 1860-2001 行），API 命名符合 P1928R15 |
| I-3 iota | 已修复 | **确认** | `iota<V>(start)` 已实现（第 2131 行） |
| I-4 operator++/-- | 已修复 | **确认** | 前缀/后缀 `++/--` 均已实现（第 960-984 行） |
| I-5 operator! | 已修复 | **确认** | `operator!` 返回 `mask_type`（第 952 行） |
| I-6 initializer_list | 已修复 | **确认，但见 I-1** | 已移除 `initializer_list` 构造，改为精确参数包构造（第 893-898 行） |
| I-7 broadcast explicit | 已修复 | **确认** | `basic_vec(T)` 不再 `explicit`（第 880 行） |
| I-9 数学函数 constexpr | 已修复 | **确认，但见 I-2** | 数学函数已标记 `constexpr`，但缺少 `noexcept` |
| I-11 select(bool) | 已修复 | **确认** | `select(bool, T, U)` 已实现（第 2294 行） |
| T-1 compress/expand 测试 | 已补齐 | **确认** | `test_simd.cpp` 第 139-154 行 |
| T-2 gather/scatter 测试 | 已补齐 | **确认** | `test_simd_memory.cpp` 第 219-269 行 |
| T-3 operator++/-- 测试 | 已补齐 | **确认** | `test_simd_operators.cpp` 第 38-57 行 |
| T-4 位运算测试 | 已补齐 | **确认** | `test_simd_operators.cpp` 第 59-80 行 |
| T-5 select 测试 | 已补齐 | **确认** | `test_simd_math.cpp` 第 57-70 行 |
| T-7 minmax 测试 | 已补齐 | **确认** | `test_simd_math.cpp` 第 44-55 行 |
| T-10 broadcast 隐式转换测试 | 已补齐 | **确认** | `test_simd_api.cpp` 第 138-139 行 |
| T-12 flag_convert 测试 | 已补齐 | **确认** | `test_simd_memory.cpp` 第 257-269 行（gather/scatter 含 flag_convert） |
| T-14 compile-probe 补齐 | 部分补齐 | **确认** | `test_simd_api.cpp` 新增 gather/scatter/compress/expand/iota/select/abs/sqrt 探测 |

---

## 第一部分：实现问题

### I-1 参数包构造函数是非标准扩展

- 严重程度：中
- 位置：`backport/cpp26/simd.hpp` 第 893-898 行（`basic_vec`）、第 643-648 行（`basic_mask`）

**问题描述：** v0-r0 的 I-6 指出 `initializer_list` 构造函数存在静默截断问题，答复中将其移除并替换为精确参数包构造函数：

```cpp
template<class... U,
         typename enable_if<
             sizeof...(U) == static_cast<size_t>(simd_size<T, Abi>::value) &&
                 conjunction<is_convertible<U, T>...>::value,
             int>::type = 0>
constexpr basic_vec(U... values) noexcept : data_{static_cast<T>(values)...} {}
```

P1928R15 中 `basic_vec` 的构造函数列表为：broadcast 构造（`basic_vec(T)`）、generator 构造（`basic_vec(G&&)`）、转换构造（`basic_vec(const basic_vec<U, OtherAbi>&, flags<convert_flag>)`）。标准中不存在接受 N 个标量参数的构造函数。

**规范要求：** P1928R15 [simd.ctor] 仅定义上述三种构造形式。用户初始化多 lane 值应通过 generator 构造或 `iota` + 运算组合。

**当前实现：** 参数包构造虽然消除了静默截断风险，但仍属于非标准扩展。下游用户若依赖 `vec<int, 4>{1, 2, 3, 4}` 语法，在标准库原生实现后将无法编译，违反"无感过渡"原则。

**建议：** 考虑移除此构造函数，或至少标记为 `[[deprecated]]` 并在文档中明确说明其非标准性质。`basic_mask` 的同类参数包构造（第 643-648 行）同理。

---

### I-2 数学函数缺少 `noexcept`

- 严重程度：低
- 位置：`backport/cpp26/simd.hpp` 第 2139-2265 行

**问题描述：** `abs`、`sqrt`、`floor`、`ceil`、`round`、`trunc`、`sin`、`cos`、`exp`、`log`、`pow` 均已标记 `constexpr`（v0-r0 I-9 修复），但均未标记 `noexcept`。而同文件中的 `min`、`max`、`minmax`、`clamp`、`select` 均标记了 `noexcept`。

**规范要求：** P1928R15 [simd.math] 中的数学函数签名均为 `constexpr` 且不抛异常。虽然标准措辞中部分数学函数未显式标注 `noexcept`，但实现应保持一致性——同类函数的异常规范应统一。

**当前实现：** `min`/`max`/`minmax`/`clamp`/`select` 有 `noexcept`，`abs`/`sqrt`/`floor` 等没有。这种不一致可能导致下游在 `noexcept` 上下文中使用时出现意外的编译差异。

---

### I-3 `basic_mask` 的 `to_bitset()` 缺少 `constexpr`

- 严重程度：低
- 位置：`backport/cpp26/simd.hpp` 第 681 行

**问题描述：** `to_bitset()` 未标记 `constexpr`，而同类的 `to_ullong()` 已标记 `constexpr`（第 689 行）。

```cpp
bitset<abi_lane_count<Abi>::value> to_bitset() const noexcept {  // 缺少 constexpr
```

**规范要求：** P1928R15 [simd.mask.overview] 中 `to_bitset()` 应为 `constexpr`。

**当前实现：** C++23 中 `std::bitset` 的构造函数和 `set()` 已为 `constexpr`，因此此处可以且应该标记 `constexpr`。

---

### I-4 `select` 缺少 `basic_mask` 重载

- 严重程度：中
- 位置：`backport/cpp26/simd.hpp` 第 2268-2296 行

**问题描述：** 当前 `select` 仅支持 `basic_vec` 类型的 true/false 分支：

```cpp
select(const mask_type&, const basic_vec<T, Abi>&, const basic_vec<T, Abi>&)
```

P1928R15 同时要求 `select` 支持 `basic_mask` 作为 true/false 分支：

```cpp
select(const basic_mask&, const basic_mask&, const basic_mask&)
```

**规范要求：** P1928R15 [simd.mask.select] 定义了 `select(const basic_mask<Bytes, Abi>&, const basic_mask<Bytes, Abi>&, const basic_mask<Bytes, Abi>&)` 返回 `basic_mask<Bytes, Abi>`。这允许用户对 mask 本身进行条件选择。

**当前实现：** 完全缺失。`select(mask, mask_a, mask_b)` 无法编译。

---

### I-5 缺少 `simd_cast` 和 `static_simd_cast`

- 严重程度：中
- 位置：整个文件缺失

**问题描述：** P1928R15 定义了两个类型转换函数：
- `simd_cast<To>(const basic_vec<T, Abi>&)` — 值保持转换
- `static_simd_cast<To>(const basic_vec<T, Abi>&)` — 允许窄化转换

当前实现仅有转换构造函数 `basic_vec(const basic_vec<U, OtherAbi>&, flags<convert_flag>)`，但缺少独立的 cast 函数。

**规范要求：** P1928R15 [simd.casts] 明确定义了这两个函数模板，它们是标准 API 的一部分。

**当前实现：** grep 搜索 `simd_cast` 和 `static_simd_cast` 无任何匹配。

---

### I-6 缺少 `where` 表达式

- 严重程度：中
- 位置：整个文件缺失

**问题描述：** P1928R15 定义了 `where` 表达式机制，允许通过 mask 进行条件赋值：

```cpp
where(mask, vec) = other_vec;  // 仅对 mask 为 true 的 lane 赋值
```

这是 P1928R15 的核心编程模型之一，提供了比 `select` 更自然的条件写入语法。

**规范要求：** P1928R15 [simd.whereexpr] 定义了 `where_expression` 和 `const_where_expression` 类模板，以及 `where(const basic_mask&, basic_vec&)` 等自由函数。

**当前实现：** 完全缺失。grep 搜索 `where` 无任何匹配。

---

### I-7 缺少 `split` 函数

- 严重程度：低
- 位置：整个文件缺失

**问题描述：** P1928R15 定义了 `split` 函数，将一个大向量拆分为多个小向量的 `tuple`。当前实现有 `chunk` 函数（功能类似），但 `split` 是标准命名。

**规范要求：** P1928R15 [simd.creation] 定义了 `split<Sizes...>(const basic_vec&)` 和 `split<V>(const basic_vec&)` 等重载。

**当前实现：** 仅有 `chunk`（第 2084-2105 行），无 `split`。`chunk` 的语义与 `split` 有重叠但不完全等价——`split` 支持不等长拆分（通过 `Sizes...` 模板参数），而 `chunk` 仅支持等宽拆分。

**说明：** 如果 `chunk` 是 P1928R15 的正式命名（某些草案版本使用 `chunk` 而非 `split`），则此条可降级。需确认最终采纳的命名。

---

### I-8 `partial_gather_from` 的 `count` 参数语义与标准不一致

- 严重程度：低
- 位置：`backport/cpp26/simd.hpp` 第 1868-1912 行

**问题描述：** `partial_gather_from` 接受 `count` 参数，当 `i >= count` 时将对应 lane 置零。但 P1928R15 中 `partial_gather_from` 的 `count` 参数表示"可安全访问的内存范围大小"，而非"要 gather 的 lane 数"。具体来说，标准要求：对于 `i < size`，如果 `indices[i] < count` 则执行 gather，否则该 lane 置零。

**当前实现：**

```cpp
if (i < count) {
    // gather from *(first + indices[i])
} else {
    // zero
}
```

这里用 `i < count` 判断，但标准语义应为 `indices[i] < count`（即 count 是内存范围的边界，不是 lane 索引的边界）。

**规范要求：** P1928R15 [simd.memory.gather] 中 `partial_gather_from` 的前置条件是 `0 <= indices[i] < count` 对所有活跃 lane 成立，count 表示从 `first` 开始的合法内存范围。

**影响：** 当索引不是 `{0, 1, ..., N-1}` 的简单递增序列时，当前实现的行为与标准不同。

---

### I-9 `select(bool, T, U)` 的约束过于宽松

- 严重程度：低
- 位置：`backport/cpp26/simd.hpp` 第 2293-2296 行

**问题描述：** 当前 `select(bool, T, U)` 的签名为：

```cpp
template<class T, class U>
constexpr common_type_t<T, U> select(bool cond, const T& true_value, const U& false_value) noexcept;
```

此模板没有任何约束，会匹配任意类型（包括 `basic_vec`、`basic_mask` 等），可能与其他 `select` 重载产生歧义或意外匹配。

**规范要求：** P1928R15 中 `select(bool, const T&, const U&)` 要求 `T` 和 `U` 为算术类型或至少不是 data-parallel 类型。

**建议：** 添加 SFINAE 约束，排除 `basic_vec` 和 `basic_mask` 类型。

---

## 第二部分：测试问题

### T-1 缺少 `select(mask, mask, mask)` 测试

- 严重程度：中
- 涉及文件：`test/test_simd_mask.cpp`、`test/test_simd_math.cpp`

**问题描述：** 对应 I-4，`select` 的 `basic_mask` 重载尚未实现，自然也无测试。一旦实现，需补充运行时测试和 compile-probe。

**期望行为：** 测试 `select(mask_cond, mask_true, mask_false)` 的逐 lane 选择语义。

---

### T-2 缺少 `simd_cast`/`static_simd_cast` 测试

- 严重程度：中
- 涉及文件：`test/test_simd_api.cpp`、`test/test_simd_operators.cpp`

**问题描述：** 对应 I-5，cast 函数尚未实现，无测试。

**期望行为：** 测试值保持转换（`simd_cast`）和窄化转换（`static_simd_cast`）的编译可见性和运行时正确性。

---

### T-3 缺少 `where` 表达式测试

- 严重程度：中
- 涉及文件：所有测试文件

**问题描述：** 对应 I-6，`where` 表达式尚未实现，无测试。

**期望行为：** 测试 `where(mask, vec) = value` 的条件赋值语义，包括 `copy_to`/`copy_from` 等 where 表达式成员。

---

### T-4 gather/scatter 测试仅覆盖 `unchecked_*` 变体

- 严重程度：低
- 涉及文件：`test/test_simd_memory.cpp`

**问题描述：** 当前 gather/scatter 测试（第 219-269 行）仅测试了 `unchecked_gather_from` 和 `unchecked_scatter_to`，未测试 `partial_gather_from` 和 `partial_scatter_to`。

**期望行为：** 补充 `partial_gather_from`/`partial_scatter_to` 的运行时测试，特别是 `count` 参数小于 `size` 时的边界行为。

---

### T-5 compress/expand 测试缺少边界情况

- 严重程度：低
- 涉及文件：`test/test_simd.cpp`

**问题描述：** 当前 compress/expand 测试（第 139-154 行）仅覆盖了"部分 lane 选中"的基本场景。缺少以下边界情况：
- 全部 lane 选中（`compress` 应返回原值）
- 无 lane 选中（`compress` 应返回全零）
- `expand(compress(v, m), m)` 的往返一致性

**期望行为：** 补充上述边界测试以提高覆盖率。

---

### T-6 `iota` 测试缺少默认参数和类型覆盖

- 严重程度：低
- 涉及文件：`test/test_simd.cpp`

**问题描述：** 当前 `iota` 测试（第 130-137 行）仅测试了 `iota<vec<int, 4>>(3)`。缺少：
- 默认参数 `iota<V>()` 应从 0 开始
- 浮点类型 `iota<vec<float, 4>>(1.5f)`

**期望行为：** 补充默认参数和浮点类型的测试。

---

### T-7 compile-probe 未覆盖新增 gather/scatter 的 `flags` 组合

- 严重程度：低
- 涉及文件：`test/test_simd_api.cpp`

**问题描述：** `test_simd_api.cpp` 中新增的 gather/scatter compile-probe（第 254-269 行）仅验证了默认 flags 和 mask 变体的签名可编译性，未覆盖 `flag_convert` 与 gather/scatter 的组合。运行时测试 `test_simd_memory.cpp` 第 257-269 行覆盖了 `flag_convert` 的 gather/scatter，但 compile-probe 层面缺失。

**期望行为：** 补充 `partial_gather_from<int4>(float_ptr, count, indices, flag_convert)` 等带 flags 的编译探测。

---

## 第三部分：总结

### 按严重程度汇总

| 严重程度 | 实现问题 | 测试问题 | 合计 |
|----------|----------|----------|------|
| 中       | 4 (I-1, I-4, I-5, I-6) | 3 (T-1, T-2, T-3) | 7 |
| 低       | 5 (I-2, I-3, I-7, I-8, I-9) | 4 (T-4, T-5, T-6, T-7) | 9 |
| 合计     | 9        | 7        | 16   |

### 与 v0-r0 对比

v0-r0 报告共 26 个问题（12 实现 + 14 测试），其中：
- 10 个已确认修复（I-1/I-2/I-3/I-4/I-5/I-7/I-9/I-11 + T-1/T-2/T-3/T-4/T-5/T-7/T-10/T-12/T-14 部分）
- 4 个明确保留为后续迭代（I-8/I-10/T-8/T-9/T-11/T-13）
- I-6 修复引入了新问题 I-1（参数包构造的非标准性）

本轮新发现 16 个问题（9 实现 + 7 测试），其中 I-4/I-5/I-6 属于之前遗漏的 P1928R15 API 缺口。

### P1928R15 API 覆盖率评估（更新）

| 模块 | 覆盖状态 |
|------|----------|
| `basic_vec` 构造/访问/迭代 | ✅ 基本完整 |
| `basic_mask` 构造/访问/迭代 | ✅ 基本完整 |
| 算术/位运算/比较运算符 | ✅ 完整 |
| `++`/`--`/`!` | ✅ 已补齐 |
| `compress`/`expand` | ✅ 已补齐 |
| `partial_load`/`partial_store` | ✅ 完整 |
| `unchecked_load`/`unchecked_store` | ✅ 完整 |
| `partial_gather_from`/`unchecked_gather_from` | ✅ 已补齐（语义见 I-8） |
| `partial_scatter_to`/`unchecked_scatter_to` | ✅ 已补齐 |
| `iota` | ✅ 已补齐 |
| `reduce`/`reduce_min`/`reduce_max` | ✅ 完整 |
| `reduce_count`/`reduce_min_index`/`reduce_max_index` | ✅ 完整 |
| `all_of`/`any_of`/`none_of` | ✅ 完整 |
| `select`（vec） | ✅ 完整 |
| `select`（mask） | ❌ 缺失 (I-4) |
| `select`（bool） | ✅ 已补齐 |
| `simd_cast`/`static_simd_cast` | ❌ 缺失 (I-5) |
| `where` 表达式 | ❌ 缺失 (I-6) |
| `split` | ❌ 缺失 (I-7)，`chunk`/`cat` 部分覆盖 |
| `permute` | ✅ 完整 |
| 数学函数 | ✅ 完整（`noexcept` 见 I-2） |
| `flags` 系统 | ✅ 完整 |
| ABI 标签/类型别名 | ✅ 完整 |

### 优先修复建议

1. **最高优先级 — I-6 `where` 表达式：** 这是 P1928R15 编程模型的核心组件，缺失意味着用户无法使用 `where(mask, vec) = value` 这一最常见的条件写入模式。

2. **高优先级 — I-5 `simd_cast`/`static_simd_cast`：** 标准类型转换 API，用户代码中高频使用。

3. **中优先级 — I-4 `select(mask, mask, mask)`：** 补齐 mask 版本的 select，实现简单。

4. **中优先级 — I-1 参数包构造的非标准性：** 需要决策——保留（接受非标准扩展风险）还是移除（要求用户使用 generator 构造）。这直接影响"无感过渡"承诺。

5. **低优先级 — I-2/I-3/I-7/I-8/I-9：** `noexcept` 一致性、`to_bitset` constexpr、`split` 命名、gather 语义、select 约束等可在后续迭代中逐步改进。

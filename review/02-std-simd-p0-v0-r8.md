# `std::simd` 独立审查报告 (p0-v0-r8)

审查对象：
- 实现文件：`backport/cpp26/simd.hpp`
- 包装头文件：`backport/simd`
- 测试文件：`test/test_simd*.cpp`、`test/CMakeLists.txt`
- 示例文件：`example/simd_example.cpp`、`example/simd_complete_example.cpp`

审查基线：
- 当前代码状态截至 `2026-03-21`
- 参考口径：当前草案 `[simd]` 与标准库交付质量要求
- 本文为新的独立 review 文档，不特别复用既有 review 结论

---

## 执行摘要

当前 `std::simd` backport 相比前几轮已经补齐了一批核心表面，但如果按标准委员会和 STD 标准库的质量要求审视，仍不能认为已经“仅剩低优先级问题”。

本轮重新确认的剩余问题主要集中在三类：

1. 标准表面仍有成组缺口，尤其是 complex 和一整批算法接口
2. 若干公开构造和 gather/scatter 约束仍然与草案不一致
3. 某些约束被实现得过宽，导致错误接受本不应进入重载集的调用

这些问题里，前四项已经足以阻止把当前实现视为“标准级可交付 backport”。

---

## 第一部分：已确认的高优先级问题

### I-1 complex `simd` 整体未支持，标准表面存在系统性缺口

- 严重程度：高
- 位置：`backport/cpp26/simd.hpp:84-87`、`backport/cpp26/simd.hpp:961`

**问题描述：**

当前 `is_supported_value` 只接受 arithmetic 类型和扩展整数：

```cpp
template<class T>
struct is_supported_value
    : integral_constant<bool,
        (is_arithmetic<remove_cvref_t<T>>::value || is_extended_integer<remove_cvref_t<T>>::value) &&
        !is_same<remove_cvref_t<T>, bool>::value> {};
```

因此 `basic_vec<std::complex<T>, Abi>` 会在类模板实例化时直接触发 `static_assert`。这不是单个算法缺失，而是 complex lane 整条标准路径都被整体排除。

**影响：**

- `std::simd::vec<std::complex<float>, N>` / `std::simd::vec<std::complex<double>, N>` 无法实例化
- complex 专属算法和访问接口不可能补齐到标准表面
- 当前实现仍明显停留在“只做标量 arithmetic 回退”的子集状态

**定性：**

- 这是标准表面缺口，不是测试覆盖问题
- 如果目标是“transparent backport”，这一项不能继续回避

### I-2 标准算法面仍未补齐，缺口不是零散而是成组存在

- 严重程度：高
- 位置：`backport/cpp26/simd.hpp`

**问题描述：**

当前实现虽然已有 `abs`、`sqrt`、`sin`、`cos`、`exp`、`log`、`pow` 等基础算法，但仍缺失整组标准接口。本轮复核中至少确认未实现：

- bit family：`byteswap`、`popcount`、`countl_zero`、`countl_one`、`countr_zero`、`countr_one`、`bit_width`、`has_single_bit`、`bit_floor`、`bit_ceil`、`rotl`、`rotr`
- math family：`log10`、`frexp`、`modf`、`remquo`、`asin`、`acos`、`atan`、`sinh`、`cosh`、`tanh`、`asinh`、`acosh`、`atanh`
- complex family 相关接口在 I-1 前提下整体不可达

**影响：**

- 公开 `<simd>` surface 仍明显不完整
- 用户可以通过已有运行时测试，但无法获得标准期望的算法集合
- 这类缺口不能通过“小修几个约束”收口

**定性：**

- 当前状态仍应视为“部分实现”
- 不能把仓库口径提升为“除了文档和边角 case 外都已完成”

### I-3 `basic_vec` 的 contiguous range 构造仍不符合草案

- 严重程度：高
- 位置：`backport/cpp26/simd.hpp:987-997`

**问题描述：**

当前 `basic_vec` 只有：

```cpp
template<class R, class... Flags>
constexpr explicit(!detail::is_value_preserving_conversion<range_value_t<R>, T>::value)
    basic_vec(R&& r, flags<Flags...> f = {}) noexcept;
```

这里至少有两个问题：

1. 缺少带 `mask_type` 的 range 构造重载
2. `explicit` 被实现成条件式，导致 value-preserving 的 range 构造会意外变成隐式

本地最小探针已确认：

- `std::is_constructible_v<vec<int, 4>, std::span<const int, 4>, mask<int, 4>> == false`
- `std::is_convertible_v<std::span<const int, 4>, vec<long long, 4>> == true`

**影响：**

- 标准构造表面仍不完整
- 现有实现会把不该存在的隐式转换放入重载集
- 这类偏差会直接影响用户代码的重载决议和可移植性

**修复要求：**

- 补齐 masked range constructor
- 按草案收紧为始终 `explicit`
- 为正向和负向行为各加 compile-time probe

### I-4 masked gather/scatter 的接口签名仍然错误

- 严重程度：高
- 位置：
  - `backport/cpp26/simd.hpp:2047-2065`
  - `backport/cpp26/simd.hpp:2093-2094`
  - `backport/cpp26/simd.hpp:2147-2162`
  - `backport/cpp26/simd.hpp:2222-2227`

**问题描述：**

当前实现把 masked gather/scatter 的掩码参数写成了 `typename V::mask_type`，而不是与 `indices` 绑定的 mask 类型；同时，range 版本的参数顺序仍然是：

```cpp
partial_gather_from(range, indices, mask)
partial_scatter_to(value, range, indices, mask)
```

而不是草案要求的标准顺序。

本地最小探针已确认：

- 当 `indices` 的 value type 与 `V` 的 value type 不同时，合法的 indices-mask 调用会被拒绝
- range 版按标准顺序书写的调用无法匹配现有重载

**影响：**

- 这是公开函数签名层面的标准不一致
- 合法调用会被错误拒绝
- 用户一旦写出面向标准接口的代码，将直接在 backport 上编译失败

**修复要求：**

- masked gather/scatter 统一改为按 `Indices::mask_type` 约束
- range overload 的参数顺序与草案完全对齐
- 增加 indices/value lane 类型不同场景的 compile probe

---

## 第二部分：约束仍然过宽的实现问题

### I-5 `basic_mask` 的构造和 generator 约束仍然过松

- 严重程度：中
- 位置：`backport/cpp26/simd.hpp:656-678`、`backport/cpp26/simd.hpp:706-711`

**问题描述：**

当前 `basic_mask` 有两个约束放宽问题：

1. `basic_mask(bool)` 作为普通构造存在，会让 `mask<int, 4>(1)` 这类直接初始化也成立
2. generator 构造使用的是 `is_simd_generator<G, bool, ...>`，而该判定只要求返回值可构造成 `bool`

本地最小探针已确认：

- `std::is_constructible_v<mask<int, 4>, int> == true`
- 返回 `int` 的 generator 可用于构造 `mask<int, 4>`

**影响：**

- `basic_mask` 公开构造表面比草案更宽
- 会把本应被拒绝的调用悄悄纳入重载集
- 这类问题在标准库实现中通常会被视为约束不严

**修复要求：**

- 收紧布尔广播构造的参与条件
- 收紧 mask generator 的 lane 结果约束，不再接受“凡是能转成 bool 都行”

### I-6 masked `reduce` 对任意二元操作都默认给 identity，约束不可靠

- 严重程度：中
- 位置：`backport/cpp26/simd.hpp:3100-3113`

**问题描述：**

当前 masked `reduce` 写成：

```cpp
template<class T, class Abi, class BinaryOperation = plus<>>
constexpr T reduce(const basic_vec<T, Abi>& value,
                   const typename basic_vec<T, Abi>::mask_type& mask_value,
                   BinaryOperation binary_op = {},
                   T identity_element = detail::reduction_identity<T, BinaryOperation>::value()) noexcept;
```

配合通用 `reduction_identity` 回退，这会错误接受很多本不该有默认 identity 的操作，例如 `std::minus<>`。

本地最小探针已确认 `reduce(v, m, std::minus<>{})` 当前可以参与重载匹配。

**影响：**

- API 看似通用，实际只是在“拿 `T{}` 顶上去”
- 这会制造语义上站不住的默认行为
- 与标准库对 reduction operation 的约束口径不一致

**修复要求：**

- 收紧默认 identity 的可用范围
- 对无标准 identity 的 binary operation，不提供该默认参数路径

### I-7 `constexpr-wrapper-like` 相关构造要求尚未覆盖

- 严重程度：中
- 位置：`backport/cpp26/simd.hpp:972-985`

**问题描述：**

当前 `basic_vec` 的标量构造和 generator 约束只认：

- `detail::is_supported_value<U>`
- 直接 `static_cast<T>` 的 generator 结果

这意味着 `std::integral_constant<int, 1>` 这类 `constexpr-wrapper-like` 值并不会被视为可参与标准要求的相关构造路径。

本地最小探针确认 `std::is_constructible_v<vec<int, 4>, std::integral_constant<int, 1>> == false`。

**影响：**

- 构造约束仍然过于“按内部实现方便来写”
- 与草案围绕 wrapper-like 值建立的显式性/可构造性规则不一致

**修复要求：**

- 明确补齐 wrapper-like 值的构造判定
- 把标量构造、generator 构造和隐式性判定统一到同一套规则

---

## 第三部分：本轮结论

当前 `std::simd` backport 的剩余问题，已经不再主要是“漏了几个测试”或“单个 overload 没补完”，而是：

1. 标准 surface 仍是明显子集
2. 某些公开签名仍与草案不一致
3. 某些构造/算法约束仍然过宽

因此，本轮结论是：

- 当前实现仍不能视为标准级完整 backport
- 当前状态不宜上升为“只剩 review 清理和小幅打磨”
- 后续修复应优先处理 I-1 到 I-4，再处理 I-5 到 I-7

---

## 参考口径

- C++ 工作草案 `<simd>`：`https://eel.is/c++draft/simd`

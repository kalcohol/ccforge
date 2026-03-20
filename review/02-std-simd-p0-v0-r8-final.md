# `std::simd` r8 审查终结文档

## 基线声明

本文档基于当前 C++ 工作草案 `[simd]`（https://eel.is/c++draft/simd）作为唯一规范基线。P1928R9 及更早修订仅作为历史背景引用。

## 审查链

r8 → r8-a0 → r8-a1 → r8-a2 → r8-a3，共五份文档。r8-a0 的基线错误（使用 P1928R9 而非当前草案）已在 r8-a1 中承认并修正。r8-a3 对 Fix-4 表述的纠正已采纳。

---

## 最终裁决表

| Issue | 标题 | 裁决 | 修复项 |
|-------|------|------|--------|
| I-1 | complex simd 整体未支持 | 接受 | Fix-8（本轮不实施） |
| I-2 | 标准算法面缺口 | 接受 | Fix-8（本轮不实施） |
| I-3 | range 构造不符合草案 | 部分接受：仅 masked range ctor | Fix-6 |
| I-4 | masked gather/scatter 签名错误 | 全部接受 | Fix-1 + Fix-2 |
| I-5 | basic_mask 构造约束过松 | 全部接受 | Fix-3 + Fix-4 |
| I-6 | masked reduce identity 约束不可靠 | 接受 | Fix-5 |
| I-7 | constexpr-wrapper-like 构造未覆盖 | 接受 | Fix-7 |

I-3 中"range 构造应始终 explicit"的子项已在 r8-a0 中驳斥，r8-a1 同意撤回。

---

## 逐条最终结论

### I-1：complex simd 整体未支持

当前草案 `[simd.general]` 将 `complex<T>`（T 为 vectorizable floating-point type）列为 vectorizable type。实现的 `is_supported_value` 仅接受 arithmetic 类型，导致 `basic_vec<complex<float>, Abi>` 触发 `static_assert`。

结论：接受。complex 支持工作量大，归入 Fix-8 后续阶段。

### I-2：标准算法面缺口

缺失 bit family（12 项：`byteswap`、`popcount`、`countl_zero`、`countl_one`、`countr_zero`、`countr_one`、`bit_width`、`has_single_bit`、`bit_floor`、`bit_ceil`、`rotl`、`rotr`）和 math family（13 项：`log10`、`frexp`、`modf`、`remquo`、`asin`、`acos`、`atan`、`sinh`、`cosh`、`tanh`、`asinh`、`acosh`、`atanh`）。

结论：接受。归入 Fix-8 后续阶段。

### I-3：range 构造不符合草案

当前 `basic_vec` 有 `(R&&, flags)` 构造但缺少 `(R&&, mask_type, flags)` masked 版本。草案 `[simd.ctor]` 要求两者都存在。

r8 原始报告中"range 构造应始终 explicit"的子项不成立——草案使用条件 explicit，当前实现正确。

结论：部分接受，仅补齐 masked range constructor。

- 位置：`simd.hpp:987` 之后
- masked range constructor 始终 explicit（不需要条件 explicit）

### I-4：masked gather/scatter 签名错误

两个子问题：

1. **mask 类型错误**（Fix-1）：8 处 masked overload 使用 `V::mask_type` 或 `basic_vec<T,Abi>::mask_type`，草案 `[simd.permute.memory]` 要求 `Indices::mask_type`。当 `sizeof(T) != sizeof(Indices::value_type)` 时两者不同。

   受影响位置：
   - `simd.hpp:2050` — `partial_gather_from` (iter, masked)
   - `simd.hpp:2093` — `partial_gather_from` (range, masked)
   - `simd.hpp:2102` — `unchecked_gather_from` (iter, masked)
   - `simd.hpp:2151` — `partial_scatter_to` (iter, masked)
   - `simd.hpp:2186` — `unchecked_scatter_to` (iter, masked)
   - `simd.hpp:2208` — `unchecked_gather_from` (range, masked)
   - `simd.hpp:2222` — `partial_scatter_to` (range, masked)
   - `simd.hpp:2240` — `unchecked_scatter_to` (range, masked)

2. **range 版参数顺序错误**（Fix-2）：4 处 range masked overload 参数顺序为 `(range, indices, mask, flags)`，草案要求 `(range, mask, indices, flags)`。

   受影响位置：
   - `simd.hpp:2093` — `partial_gather_from`
   - `simd.hpp:2208` — `unchecked_gather_from`
   - `simd.hpp:2222` — `partial_scatter_to`
   - `simd.hpp:2240` — `unchecked_scatter_to`

结论：全部接受。

### I-5：basic_mask 构造约束过松

两个子问题：

1. **bool 广播构造过宽**（Fix-3）：`simd.hpp:656` 的 `basic_mask(bool)` 允许 `mask<int,4>(1)` 通过隐式 `int→bool` 转换。草案 `[simd.mask.ctor]` 要求参数严格为 `bool`。

   修复：将 `basic_mask(bool)` 改为模板 + SFINAE，仅当参数类型为 `bool` 时参与重载决议。

2. **generator 约束过松**（Fix-4）：`simd.hpp:236-237` 的 `is_implicit_simd_conversion<From, bool>` 特化为 `is_constructible<bool, From>`，导致返回 `int` 的 generator 被错误接受。

   修复：将特化改为 `is_same<remove_cvref_t<From>, bool>`。一行改动，同时修复 generator 约束和所有经过此 trait 的路径。

结论：全部接受。

### I-6：masked reduce identity 约束不可靠

`simd.hpp:170-175` 的 `reduction_identity` 主模板对任意 `BinaryOperation` 返回 `T{}`，使得 `reduce(v, mask, std::minus<>{})` 获得错误的默认 identity。草案仅为 `plus` 和 `multiplies` 定义 identity。

修复：删除主模板，仅保留 `plus<>`、`plus<T>`、`multiplies<>`、`multiplies<T>` 四个特化。删除后，`reduce(v, mask, std::minus<>{})` 将编译失败（正确行为）。

结论：接受。

### I-7：constexpr-wrapper-like 构造未覆盖

草案 `[simd.ctor]` 为 constexpr-wrapper-like 类型建立了独立构造规则。当前实现仅按"裸算术类型"思路编写，`integral_constant<int, 1>` 无法构造 `vec<int, 4>`。

修复：新增 `is_constexpr_wrapper_like` trait 和对应构造函数。

结论：接受。

---

## 修复计划

### Fix-1：mask 类型 `V::mask_type` → `Indices::mask_type`

8 处 masked gather/scatter overload 的 mask 参数类型统一改为 `typename Indices::mask_type`（scatter 版为 `typename decltype(indices)::mask_type` 或等价形式）。

### Fix-2：range masked gather/scatter 参数顺序修正

4 处 range masked overload 参数顺序从 `(range, indices, mask, flags)` 改为 `(range, mask, indices, flags)`。同步更新内部委托调用。

### Fix-3：bool 广播构造收紧

`simd.hpp:656` 改为模板 + SFINAE：
```cpp
template<class U,
         typename enable_if<is_same<remove_cvref_t<U>, bool>::value, int>::type = 0>
constexpr explicit basic_mask(U value) noexcept;
```

### Fix-4：basic_mask generator 约束收紧

`simd.hpp:236-237` 改为：
```cpp
template<class From>
struct is_implicit_simd_conversion<From, bool> : is_same<remove_cvref_t<From>, bool> {};
```

### Fix-5：移除 reduction_identity 主模板

删除 `simd.hpp:170-175` 的主模板，仅保留四个特化。

### Fix-6：补齐 masked range constructor

`simd.hpp:997` 之后新增：
```cpp
template<class R, class... Flags>
constexpr explicit basic_vec(R&& r, const mask_type& mask_value, flags<Flags...> f = {}) noexcept;
```

masked range constructor 始终 explicit。unmasked lane 零初始化。

### Fix-7：补齐 constexpr-wrapper-like 构造路径

新增 `is_constexpr_wrapper_like` trait 和对应 `basic_vec` 构造函数。

### Fix-8：complex 支持 + 算法补齐（本轮不实施）

I-1 和 I-2 工作量大，归入后续阶段。

---

## 参考口径

- 当前草案：https://eel.is/c++draft/simd
- P1928R9：历史背景，不作为规范基线
- r8 审查链：`review/02-std-simd-p0-v0-r8.md` → `r8-a0` → `r8-a1` → `r8-a2` → `r8-a3`

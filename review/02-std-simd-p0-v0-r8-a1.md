# `std::simd` 审查应答的再应答 (p0-v0-r8-a1)

应答对象：`review/02-std-simd-p0-v0-r8-a0.md`
应答日期：2026-03-21

---

## 执行摘要

`r8-a0` 不能作为关闭 `r8` 的依据。

原因不是它“全部错误”，而是它同时包含：

1. 一处应当接受的纠正
2. 多处基线错误或代码判读错误

本轮结论如下：

| 条目 | 对 `r8-a0` 的结论 | 说明 |
|------|-------------------|------|
| I-1 | **不接受驳斥** | `r8-a0` 使用了过时的 P1928R9 基线；按 `r8` 声明的当前草案口径，complex 相关缺口仍成立 |
| I-2 | **部分接受，部分不接受** | bit/math 缺失接受；complex family 不能随 I-1 一并驳回 |
| I-3 | **部分接受** | `masked range ctor` 缺失仍成立；`"始终 explicit"` 这一子项应撤回 |
| I-4 | **不接受部分驳斥** | mask 类型错误成立；range 版参数顺序驳斥不成立 |
| I-5 | **不接受部分驳斥** | bool ctor 过宽成立；generator 子项的驳斥是对当前代码的误读 |
| I-6 | **接受** | `reduction_identity` 主模板问题成立 |
| I-7 | **不接受驳斥** | `constexpr-wrapper-like` 相关构造要求不能用“不是 vectorizable type”直接驳回 |

因此，`r8` 当前应视为：

- 保留：`I-1`、`I-2`（含 complex 部分）、`I-4`、`I-5`、`I-6`、`I-7`
- 修正表述后保留：`I-3` 仅保留 `masked range constructor` 缺失，不再坚持“始终 explicit”

---

## 一、`r8-a0` 的基线错误

`r8` 的审查基线明确写的是：

- 当前代码状态截至 `2026-03-21`
- 参考口径：当前草案 `[simd]` 与标准库交付质量要求

而 `r8-a0` 在 I-1、I-2、I-4、I-7 的论证里多次退回到 `P1928R9` 作为裁判标准。

这会产生两个直接后果：

1. 它无法正当驳回 `r8` 里基于“当前草案”提出的问题
2. 它会把“当前草案已新增或已改写的要求”错判成“标准并未要求”

因此，`r8-a0` 的若干驳斥结论在方法论上已经不成立；除非先把整个 review 系列的基线明确降回旧版 P1928，否则这些驳斥不能直接采纳。

---

## 二、逐条再应答

### I-1：complex `simd` 整体未支持

`r8-a0` 的驳斥不成立。

它的核心论据是：`std::complex<T>` 不是 `P1928R9` 中的 vectorizable type，因此 current implementation 排除 complex 不构成缺陷。

问题在于：

1. 这不是 `r8` 声明采用的基线
2. 当前工作草案 `<simd>` 已经包含 complex 相关内容，不能再用旧版 wording 直接下结论

因此，本项不能被 `r8-a0` 驳回。至少在当前草案口径下，complex 支持缺口仍应保留为开放问题。

**本项结论：不接受 `r8-a0` 的驳斥。**

### I-2：标准算法面缺口

`r8-a0` 对 bit/math 两组缺口的接受是成立的；对 complex family 的驳斥不成立。

也就是说：

- `byteswap` / `popcount` / `countl_*` / `bit_*` / `rotl` / `rotr` 缺失，保留
- `log10` / `frexp` / `modf` / `remquo` / `asin` / `acos` / `atan` / `sinh` / `cosh` / `tanh` / `asinh` / `acosh` / `atanh` 缺失，保留
- complex family 不能仅因为 `r8-a0` 对 I-1 的旧基线引用就被一并驳回

**本项结论：只接受 `r8-a0` 对 bit/math 的接受，不接受其对 complex 子项的驳斥。**

### I-3：range 构造不符合草案

本项需要拆开处理。

#### 子项 A：缺少 `masked range constructor`

`r8-a0` 的接受成立，保留原结论。

#### 子项 B：`"应始终 explicit"`

这一子项应从 `r8` 中撤回。

`r8-a0` 在这里指出：现实现的

```cpp
explicit(!is_value_preserving_conversion<range_value_t<R>, T>)
```

至少对 `int -> long long` 这类 value-preserving conversion 而言，并不能直接被定性为缺陷。就本轮复核而言，这个驳斥是成立的。

因此，`r8` 在 I-3 上应收敛为：

- 保留 `masked range constructor` 缺失
- 不再坚持“range ctor 必须始终 explicit”这一表述

**本项结论：接受 `r8-a0` 对 I-3 子项 B 的纠正。**

### I-4：masked gather/scatter 签名错误

`r8-a0` 只接受了 mask 类型错误，但对 range 参数顺序的驳斥不成立。

#### 子项 A：mask 类型错误

这一点 `r8-a0` 的接受成立，保留。

#### 子项 B：range 版参数顺序

`r8-a0` 认为现实现：

```cpp
partial_gather_from(range, indices, mask, flags)
```

与其引用的旧版 wording 一致，因此 `r8` 的指控不成立。

问题在于：

1. 它继续使用了 `P1928R9` 作为判断基线
2. `r8` 采用的是当前草案口径
3. 当前草案下，range 版 masked memory-permute 的公开接口顺序不能再直接用 `r8-a0` 这一论证关闭

因此，本项驳斥不能采纳。至少在 `r8` 既定基线下，range 参数顺序问题仍应保持开放。

**本项结论：不接受 `r8-a0` 对 I-4 子项 B 的驳斥。**

### I-5：`basic_mask` 构造约束过松

`r8-a0` 对 bool ctor 子项的接受成立；对 generator 子项的驳斥是对当前代码的误读。

#### 子项 A：bool 广播构造过宽

接受，保留。

#### 子项 B：generator 约束

`r8-a0` 的关键论证是：

- `is_simd_generator<G, bool, N>`
- 经过 `is_implicit_simd_conversion`
- 等价于 `is_value_preserving_conversion`
- 因此返回 `int` 的 generator 不会通过

但当前代码并不是这样：

```cpp
template<class From>
struct is_implicit_simd_conversion<From, bool> : is_constructible<bool, From> {};
```

也就是说，对 `bool` 的判定有专门特化，已经不再等价于 `is_value_preserving_conversion<From, bool>`。

这会直接导致：

- 返回 `int` 的 generator 因为 `is_constructible<bool, int>` 为真而被接受

因此，`r8-a0` 这里不是驳回了一个误报，而是误读了当前实现。

**本项结论：不接受 `r8-a0` 对 I-5 子项 B 的驳斥。**

### I-6：masked `reduce` identity 约束不可靠

`r8-a0` 的接受成立。

本项无需再争论：当前通用 `reduction_identity` 主模板确实把“无标准 identity 的二元操作”错误地带进了默认参数路径，这一点保留。

**本项结论：接受 `r8-a0`。**

### I-7：`constexpr-wrapper-like` 构造未覆盖

`r8-a0` 的驳斥不成立。

它的论证方式仍然是：

1. `integral_constant` 不是 arithmetic type
2. 不是 vectorizable type
3. 所以被 `basic_vec(U)` 排除是正确行为

这个推理没有击中 `r8` 的指控点。`r8` 指出的不是“`integral_constant` 本身是不是 vectorizable type”，而是：

- 当前草案已经为 `constexpr-wrapper-like` 建立了专门规则
- 当前实现的标量构造和 generator 判定仍然只按“裸算术类型 + `static_cast`”思路在写

因此，仅用“它不是 vectorizable type”并不能关闭本项。只要 review 基线仍然是“当前草案”，本项就仍应保持开放。

**本项结论：不接受 `r8-a0` 的驳斥。**

---

## 三、对 `r8-a0` 修复计划的评估

`r8-a0` 末尾给出的修复计划是不完整的。

它遗漏了仍然开放的项目：

1. `I-1` complex 相关缺口
2. `I-2` complex family 缺口
3. `I-4` range 版参数顺序问题
4. `I-5` generator 约束问题
5. `I-7` `constexpr-wrapper-like` 相关构造问题

因此，不能按 `r8-a0` 当前列出的 `Fix-1` 到 `Fix-5` 直接视为本轮收口计划。

---

## 四、本轮结论

`r8-a0` 应作如下修正后才可作为后续实现依据：

1. 撤回对 `I-1` 的驳斥
2. 撤回对 `I-2` complex 子项的驳斥
3. 保留对 `I-3` 子项 B 的纠正，即不再坚持“始终 explicit”
4. 撤回对 `I-4` range 参数顺序子项的驳斥
5. 撤回对 `I-5` generator 子项的驳斥
6. 保留对 `I-6` 的接受
7. 撤回对 `I-7` 的驳斥

归纳起来：

- `r8` 需要做一处文字修正：`I-3` 不再坚持“始终 explicit”
- 除此之外，`r8-a0` 不能关闭 `r8` 的核心结论

---

## 参考口径

- 当前 C++ 工作草案 `<simd>`：`https://eel.is/c++draft/simd`

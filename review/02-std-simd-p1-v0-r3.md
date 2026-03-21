# `std::simd` 独立审查报告 (p1-v0-r3)

审查对象：

- 实现文件：`backport/cpp26/simd.hpp`、`backport/cpp26/simd/*.hpp`
- 测试文件：`test/test_simd*.cpp`、`test/simd_configure_probes/*.cpp`、`test/CMakeLists.txt`

审查基线：

- 当前工作树，基于提交 `524c817`（`simd: add complex and extended math surface`）
- 当前 C++ 工作草案 `[simd]`：`https://eel.is/c++draft/simd`
- 本文不特别复用既有 review 结论，按当前工作树重新独立核查

---

## 执行摘要

在 `r1` / `r2` 收口之后，当前 `std::simd` 不再是“复杂类型和基础算法整片缺失”的状态，但按标准委员会和 STD 标准库质量要求看，仍有三组不能放过的 P1 问题：

1. `[simd.bit]` 的公开契约仍未收口到当前草案：若干 bit 算法被错误向 signed `simd` 打开，计数类算法返回类型也仍不对
2. `[simd.math]` 中 `frexp` / `modf` / `remquo` 的公开签名仍然不是当前草案要求的 out-parameter 形式，而测试还把错误签名锁成了“正确行为”
3. 即使经历了上一轮补面，`[simd.math]` 仍明显只是当前草案的一个子集，仍缺一批核心数学函数和整组 special math surface

这三项说明：当前实现已经进入“不能只看是否有函数名，而要看签名、参与条件和剩余标准表面是否真正对齐”的阶段。

---

## 主要问题

### I-1 `[simd.bit]` 的约束与返回类型仍不符合当前草案

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/operations_bit.hpp:95-223`
  - `test/test_simd_api_math.cpp:33-58`
- 相关条文：
  - 当前工作草案 `[simd.bit]`
  - 其中 `popcount` / `countl_zero` / `countl_one` / `countr_zero` / `countr_one` / `bit_width` / `has_single_bit` / `bit_floor` / `bit_ceil` / `rotl` / `rotr`

**问题描述：**

当前实现把上述 bit 算法统一写成了 `requires(is_integral<T>::value)`，并在实现内部通过 `make_unsigned_t<T>` 规避底层 `<bit>` 的 unsigned 限制。这带来了两类标准偏差：

1. **参与条件被错误放宽**
   - 当前草案要求这些接口仅对 unsigned integer lane 类型开放
   - 但当前实现会接受 signed `basic_vec`
   - 本轮复现：`std::simd::bit_floor(std::simd::vec<int, 4>{})` 当前可以通过编译
2. **返回类型仍不正确**
   - 当前草案对 `popcount` / `countl_*` / `countr_*` / `bit_width` 要求返回 `rebind_t<make_signed_t<typename V::value_type>, V>`
   - 当前实现固定返回 `rebind_t<int, V>`
   - 这在 `vec<unsigned, 4>` 上恰好“碰巧对”，因为 `make_signed_t<unsigned>` 就是 `int`
   - 但在 `vec<unsigned char, 4>` 等窄类型上会直接错型；本轮复现的静态断言已失败

此外，`rotl` / `rotr` 的逐 lane 版本目前也被写成：

```cpp
rotl(const basic_vec<T, Abi>&, const basic_vec<T, Abi>&)
```

而当前草案允许第二参数是“同 lane 数、同 `sizeof`、value_type 为 integral 的另一个 simd 类型”。因此标准允许的调用例如：

```cpp
std::simd::rotl(std::simd::vec<unsigned, 4>{}, std::simd::vec<int, 4>{})
```

当前实现会直接失配。本轮已复现该失配。

**影响：**

- 标准要求应当被拒绝的 signed bit 调用会错误进入候选集
- 标准要求的返回类型在窄 unsigned lane 上错误
- 标准允许的 mixed shift-vector 调用会被错误拒绝
- 当前测试只覆盖 `uint4`，把“碰巧对”的情形误当成“契约已对齐”

### I-2 `frexp` / `modf` / `remquo` 的公开签名仍然不是当前草案要求的形式

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/operations_math.hpp:260-307`
  - `test/test_simd_api_math.cpp:98-103`
  - `test/test_simd_math_ext.cpp:114-137`
  - `test/simd_configure_probes/math_surface.cpp:11-25`
- 相关条文：
  - 当前工作草案 `[simd.math]`
  - 其中：
    - `frexp(const V& x, rebind_t<int, deduced-vec-t<V>>* exp)`
    - `modf(const V& x, deduced-vec-t<V>* iptr)`
    - `remquo(const V0& x, const V1& y, rebind_t<int, math-common-simd-t<V0, V1>>* quo)`

**问题描述：**

当前实现提供的是：

```cpp
frexp(const V&) -> pair<V, rebind_t<int, V>>
modf(const V&) -> pair<V, V>
remquo(const A&, const B&) -> pair<math-common-simd-t<A, B>, rebind_t<int, ...>>
```

也就是 pair-return 形式，而不是当前草案要求的 out-parameter 形式。

这不是“额外提供一个便捷重载”的问题，而是当前公开表面直接与草案不一致，因为标准调用会失配：

- `std::simd::frexp(float4{}, &exp)` 当前失配
- `std::simd::modf(float4{}, &integral)` 当前失配
- `std::simd::remquo(float4{}, 2.0f, &quo)` 当前失配

本轮都已直接复现。

更严重的是，现有测试并没有暴露这个问题，反而把错误契约锁成了“正确行为”：

- compile probe 在 `test/test_simd_api_math.cpp:98-103` 直接断言它们应当返回 `pair`
- runtime test 在 `test/test_simd_math_ext.cpp:119-137` 也按 pair-return 使用
- configure probe `test/simd_configure_probes/math_surface.cpp:11-25` 同样按 `.first/.second` 使用

**影响：**

- 按当前草案书写的用户代码会直接无法编译
- 现有测试不会发现该问题，反而会阻止正确签名的修复
- 这类“测试锁错契约”的问题比单纯漏测更危险

### I-3 `[simd.math]` 仍明显只是当前草案的一个子集，缺失函数族不少

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/operations_math.hpp:174-257`
  - `backport/cpp26/simd/operations_math.hpp:290-307`
  - `test/test_simd_api_math.cpp:60-103`
  - `test/test_simd_math_ext.cpp:13-139`
- 相关条文：
  - 当前工作草案 `[simd.math]`

**问题描述：**

上一轮已经补进了一批浮点数学接口，但本轮重新按当前草案复核时，`[simd.math]` 仍然明显不是“基本齐备，只剩边角”状态。

当前实现中仍可直接确认缺失、且测试中也没有覆盖痕迹的接口至少包括：

- 基础数学：
  - `tan`
  - `atan2`
  - `exp2`
  - `expm1`
  - `ilogb`
  - `ldexp`
  - `scalbn`
  - `scalbln`
  - 三参 `hypot`
  - `erf`
  - `erfc`
  - `lgamma`
  - `tgamma`
  - `lerp`
- 分类/比较：
  - `fpclassify`
  - `isgreater`
  - `isgreaterequal`
  - `isless`
  - `islessequal`
  - `islessgreater`
  - `isunordered`
- 当前草案同页给出的 special math family 也整体未进入实现面

本轮最小复现里，`std::simd::atan2(float4{}, 1.0f)` 直接报错 “`atan2` is not a member of `std::simd`”。

与此同时，当前测试只覆盖了已经实现的那一小批接口：

- `test/test_simd_api_math.cpp:60-103`
- `test/test_simd_math_ext.cpp:13-139`

并没有形成针对剩余 `[simd.math]` 表面的系统性探针。

**影响：**

- 当前 `<simd>` 的数学表面仍明显只是当前草案子集
- 用户按当前草案调用这些接口时会直接遇到缺失，而不是轻微行为差异
- review 容易因为上一轮已经补了一批函数而高估当前完成度

---

## 结论

当前 `std::simd` 在 `r1` / `r2` 之后，确实已经补上了 complex 类型面和一批重要算法，但按当前工作草案 `[simd]` 继续独立核查后，仍可确认至少还有三组 P1 问题：

1. `[simd.bit]` 的参与条件、返回类型和 mixed shift-vector 支持仍未对齐
2. `frexp` / `modf` / `remquo` 的签名仍是非标准 pair-return 形式，而且测试正在把错误契约锁死
3. `[simd.math]` 仍缺一批核心函数和整组 special math surface

因此，下一轮修复不应只做零散补丁，而应至少绑定以下动作一起处理：

- 按 `[simd.bit]` 收紧 unsigned 约束并修正计数类返回类型
- 将 `frexp` / `modf` / `remquo` 改为当前草案要求的 out-parameter 公开签名，并同步修正测试
- 对 `[simd.math]` 进行一次完整 surface audit，把缺失函数族与对应 probe / runtime tests 成组补齐

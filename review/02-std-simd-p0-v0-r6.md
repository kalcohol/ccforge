# `std::simd` 独立审查报告 (p0-v0-r6)

审查对象：
- 实现文件：`backport/cpp26/simd.hpp`
- 包装头文件：`backport/simd`
- 测试文件：`test/test_simd*.cpp`、`test/CMakeLists.txt`
- 示例文件：`example/simd_example.cpp`、`example/simd_complete_example.cpp`

审查基线：
- 当前代码状态截至 `2026-03-20`
- 参考口径：P1928R15 / C++26 `[simd]`
- 本文作为新的 review 文档，不是答复文档

---

## 执行摘要

当前 `std::simd` backport 在现有 GCC/Zig 测试矩阵下处于“核心公开表面可用、主要 case 通过”的状态，但按标准委员会和标准库交付质量要求，仍未收口。

本轮复核后的结论是：

- 不能把当前实现视为“完整标准支持”
- 不能把当前状态上升为“仅剩文档和低优先级测试问题”
- 需要按三轮连续修复推进，其中前两轮包含真实代码修复，第三轮完成质量闭环

当前问题分为三类：

1. **已确认实现缺陷**
2. **高风险标准一致性缺口**
3. **标准库质量与验证闭环不足**

---

## 第一部分：已确认实现缺陷

### I-1 `basic_mask` 的一元 `+/-/~` 对 16-byte 值类型不可用

- 严重程度：高
- 位置：`backport/cpp26/simd.hpp`

**问题描述：**

当前实现把 `basic_mask` 一元 `+/-/~` 的返回类型写成：

```cpp
basic_vec<typename detail::integer_from_size<Bytes>::type, Abi>
```

但 `detail::integer_from_size` 目前只覆盖 `1/2/4/8` 字节，未覆盖 `16` 字节。因此 `mask<long double, 2>` 这类 `sizeof(T) == 16` 的合法实例，一旦涉及比较或一元掩码运算，就会在类实例化阶段失败。

**影响：**

- `vec<long double, N>` 的比较结果类型为 `mask<long double, N>`
- `mask<long double, N>` 的公开表面并不完整可用
- 当前实现对“支持所有 arithmetic 非 `bool`”的口径不成立

**修复要求：**

- 第一轮必须修复
- 修复后 `mask<long double, 2>` 至少应能完成构造、比较和一元 `+/-/~`
- 不接受“简单把 `long double` 排除出 supported value”作为本轮收口方式

### I-2 native-width `mask<float/double/unsigned>` 的一元 `+/-/~` 返回类型错误

- 严重程度：高
- 位置：`backport/cpp26/simd.hpp`

**问题描述：**

当前 `basic_mask` 一元运算直接复用原 mask 的 `Abi`。这在 `mask<int>` 上通常可工作，但对：

- `mask<float>`
- `mask<double>`
- `mask<unsigned int>`

这类 mask，会生成类似 `basic_vec<int, native_abi<float>>` 的返回类型；而当前 `simd_size` 只定义了 `simd_size<T, native_abi<T>>`，没有覆盖“值类型与 abi 绑定类型不同”的组合，因此 native-width 场景会编译失败。

**影响：**

- `mask<float>` / `mask<double>` / `mask<unsigned int>` 的一元掩码运算在 native-width 路径不完整
- 当前实现对 mask 公开表面的支持具有类型族缺口

**修复要求：**

- 第一轮必须修复
- 修复方案必须同时覆盖 fixed-size 和 native-width
- 不能仅靠“测试中避免这些类型”规避

---

## 第二部分：高风险标准一致性缺口

### I-3 `basic_vec` 转换构造默认放宽了 `flag_convert`

- 严重程度：中
- 位置：`backport/cpp26/simd.hpp`

**问题描述：**

当前实现中：

```cpp
constexpr explicit basic_vec(const basic_vec<U, OtherAbi>& other, flags<convert_flag> = {}) noexcept
```

由于 `flags<convert_flag>` 带默认参数，`vec<float, 4>(vec<int, 4>{})` 这类未显式传 `flag_convert` 的构造仍可直接编译。

这与仓库此前对 P1928 构造表面的标准口径不一致，也扩大了“非值保持转换必须显式选择”的边界。

**本轮定性：**

- 这是高风险标准一致性缺口
- 第二轮需要收紧，不再保留默认 `convert_flag`
- 允许保留显式 `flag_convert` 构造入口
- `simd_cast` / `static_simd_cast` 继续作为公开转换入口存在

**修复要求：**

- 第二轮修复构造签名
- 增加 compile-fail probe，锁定无 `flag_convert` 的构造不得通过
- 同时保留带 `flag_convert` 的正向 probe

---

## 第三部分：质量与验证闭环问题

### Q-1 当前状态仍不能宣称完整标准支持

- 严重程度：高
- 位置：`README.md`、`test/test_simd_feature_macro.cpp`

**问题描述：**

项目当前明确写明：

- 仅验证核心公开表面
- 在完整 wording 覆盖继续外扩前，不主动定义 `__cpp_lib_simd`

因此当前状态仍应视为“backport 可用但未完整收口”，而不是“仅剩杂项优化”。

**处理方式：**

- 第三轮不把 `__cpp_lib_simd` 打开
- 继续保持保守口径
- 通过更严格测试矩阵，为后续是否定义 feature macro 建立前提

### Q-2 `alignment_v` 与 `alignof(vec)` 脱节

- 严重程度：中
- 位置：`backport/cpp26/simd.hpp`

**问题描述：**

当前 `alignment_v<V>` 按 `alignof(T) * lane_count` 计算，但对象本体存储只是 `std::array<T, N>`，因此 `alignof(vec<T, N>)` 往往只等于 `alignof(T)`。

这意味着：

- `alignment_v` 更接近“内存接口/对齐元信息”
- 而不是“对象实际布局对齐”

**本轮定性：**

- 这里优先视为口径和测试问题，而不是立即强行把对象对齐改到 `alignment_v`
- 原因是 `alignof(T) * lane_count` 并不总是合法的对象对齐值，强改对象布局风险高

**处理方式：**

- 第三轮不以“强制 `alignof(vec) == alignment_v`”为修复目标
- 将 configure probe 从观察项调整为质量门槛：若未来决定要求对象对齐匹配，则需显式实现并验证；若不要求，则测试必须反映当前 intended contract，而不是把不成立的等式继续当作潜在目标

### Q-3 可选 probe 过多，标准表面尚未全部升级为硬约束

- 严重程度：中
- 位置：`test/CMakeLists.txt`

**问题描述：**

当前测试体系中，若干标准表面仍采用“先 probe，再决定是否编入测试”的方式维持绿色。这适合作为增量开发阶段策略，但不适合作为标准库成品形态。

需要收紧的方向包括：

- native mask unary
- vector converting constructor
- `native_abi` / `deduce_abi_t`
- alignment 相关口径

**处理方式：**

- 第三轮把本轮已修好的表面从“观察项”升级为“失败即报错的质量门槛”
- 不再允许 defect 已知场景只停留在 probe 结果中

### Q-4 ABI / handoff / example smoke coverage 不足

- 严重程度：中

**问题描述：**

- `native_abi` / `deduce_abi_t` 的 ABI 测试未完全常开
- 尚缺模拟“原生 `<simd>` 接管后 backport 退场”的 handoff 验证
- example 当前仅 build，未做 smoke run

**处理方式：**

- 第三轮补常开 ABI coverage
- 补最小 handoff 验证
- 补 example smoke run

### Q-5 最近一轮修复的验证仍未达到仓库要求的完整矩阵

- 严重程度：中
- 位置：`CLAUDE.md`

**问题描述：**

仓库要求 backport 相关修改使用：

- GCC 全量
- Zig 全量
- Podman 容器验证

而此前 `std::simd` 的 r5 修复主要以 `build-gcc` 的分目标、分子集验证为主。

**处理方式：**

- 三轮结束后必须执行 GCC + Zig + Podman 的完整验证
- Podman 需重新配置，不复用已有失效 cache

---

## 三轮执行安排

### 第一轮：修 confirmed defects

范围：

- `I-1`
- `I-2`

目标：

- 让 16-byte value type 路径可实例化
- 让 native-width `mask<float/double/unsigned>` 的一元 `+/-/~` 正常工作
- 补最小 compile/runtime coverage

本轮验收：

- `FORGE_SIMD_HAS_LONG_DOUBLE_MASK` 为真
- `FORGE_SIMD_HAS_NATIVE_MASK_UNARY` 为真
- 新增相关 runtime / compile assertions 通过

### 第二轮：收紧 `flag_convert` 语义

范围：

- `I-3`

目标：

- 去掉转换构造对默认 `convert_flag` 的放宽
- 锁定无 `flag_convert` 的构造不能通过
- 保留显式 `flag_convert` 正向入口

本轮验收：

- `FORGE_SIMD_HAS_IMPLICIT_VEC_CONVERTER` 必须为假
- `FORGE_SIMD_HAS_FLAG_CONVERT_CONSTRUCTOR` 必须为真
- 相关测试与示例按新构造约束全部通过

### 第三轮：质量闭环与验证收口

范围：

- `Q-1`
- `Q-2`
- `Q-3`
- `Q-4`
- `Q-5`

目标：

- 收紧已修表面的测试门槛
- 打开 ABI coverage
- 补 handoff / example smoke coverage
- 明确 `alignment_v` 的 intended contract，不把当前不成立的“对象实际对齐等于 `alignment_v`”继续当成隐含目标
- 跑完整工具链矩阵

本轮验收：

- GCC 全量通过
- Zig 全量通过
- Podman 验证通过
- R6 列出的质量项均有对应代码、测试或口径收口

---

## 最终关闭条件

R6 只有在以下条件同时满足后才能关闭：

1. 第一轮两项 confirmed defect 已修复并补测
2. 第二轮 `flag_convert` 构造语义已收紧并锁定负向 probe
3. 第三轮质量闭环项已落实，不再停留在“观察项”
4. GCC / Zig / Podman 三套验证全部通过
5. 临时分支三轮提交全部完成，并最终合回 `master`

在上述条件达成前，当前 `std::simd` 仍应维持“核心公开表面可用，但未达完整标准库交付水位”的口径。

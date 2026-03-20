# `std::simd` 实现与测试独立审查报告 (p0-v0-r6)

审查对象：

- 实现文件：`backport/cpp26/simd.hpp`
- 包装头文件：`backport/simd`
- 测试文件：`test/` 下全部 `simd` 相关测试

审查基线：

- 当前工作区代码状态（`simd-r6-fixes` 分支切出前的 `master`）
- 已完成的 `r5` 答复与三轮修复结果
- 2026-03-20 本地复核：`build-gcc` / `build-zig` 全量测试均通过，但标准库级缺陷和质量闭环仍未收口

## 执行摘要

当前 `std::simd` backport 已具备稳定的核心公开表面，且现有 GCC/Zig 测试集为绿色；但按标准委员会和标准库交付质量要求，仍存在以下未收口问题：

1. 已确认实现缺陷：
   - `basic_mask` 的一元 `+/-/~` 对 `sizeof(T) == 16` 的值类型不成立
   - native-width `mask<float>` / `mask<double>` / `mask<unsigned int>` 的一元 `+/-/~` 不成立
2. 高风险标准一致性项：
   - `basic_vec` 转换构造当前默认带 `convert_flag`，疑似错误放宽非 value-preserving 转换入口
3. 标准库质量闭环缺口：
   - `alignment_v` 与对象实际对齐关系未澄清
   - 一部分公开表面仍依赖 configure-time probe 决定是否测试
   - `native_abi` / `deduce_abi_t` 覆盖未常开
   - typed matrix、native handoff、example smoke run 不足

本轮不再把问题拆成“答复文档”；而直接作为新的 review 文档立项，并按三轮顺序连续修复。

## 当前结论

- 不接受“当前 `std::simd` 已达到可宣称完整标准支持”的结论
- 接受“当前实现已具备可用核心表面且回归风险可控”的结论
- 接受以下两项为已确认 defect，必须直接修复：
  - `I-7`：16-byte 值类型掩码一元运算失效
  - `I-8`：native-width 浮点/无符号掩码一元运算失效
- 将以下项列为高风险一致性复核项，第二轮收口：
  - `I-9`：转换构造默认放宽 `flag_convert`
- 将以下项列为第三轮质量闭环项：
  - `Q-1`：`alignment_v` 口径未收口
  - `Q-2`：probe-gated 标准表面仍过多
  - `Q-3`：ABI / type matrix / handoff / example 验证不足

## 已确认问题

### I-7 `sizeof(T) == 16` 的 mask 一元运算失效

现象：

- `detail::is_supported_value<T>` 接受所有 arithmetic 非 `bool` 类型
- 但 `detail::integer_from_size` 仅为 `1/2/4/8` 字节提供映射
- `basic_mask` 的一元 `+/-/~` 直接依赖 `integer_from_size<Bytes>::type`

影响：

- 当前平台 `sizeof(long double) == 16`
- `std::simd::mask<long double, 2>` 会因一元运算声明阶段实例化失败

处置：

- 第一轮必须修实现
- 同时补最小 compile-pass 覆盖，锁定 `mask<long double, 2>` 基本可用

### I-8 native-width `mask<float/double/unsigned>` 一元运算失效

现象：

- `basic_mask` 一元 `+/-/~` 返回 `basic_vec<integer_from_size<Bytes>::type, Abi>`
- 当 `Abi == native_abi<float>` / `native_abi<double>` 等时，会构造出 `basic_vec<int, native_abi<float>>` 这类未定义组合

影响：

- `+std::simd::mask<float>{}`、`-std::simd::mask<double>{}`、`~std::simd::mask<unsigned int>{}` 当前编译失败

处置：

- 第一轮必须修实现
- 补 native-width 与 fixed-size 的 compile-pass coverage

## 待复核问题

### I-9 转换构造默认放宽 `flag_convert`

现象：

- 当前 `basic_vec` converting constructor 形如：
  - `basic_vec(const basic_vec<U, OtherAbi>& other, flags<convert_flag> = {})`
- 导致 `vec<float, 4> f(vec<int, 4>{});` 可直接编译

当前判断：

- 这是高风险标准一致性问题
- 第二轮应先明确项目最终口径，再决定是否收紧签名

处置：

- 第二轮优先收口
- 无论最终结论如何，都必须补负向/正向 compile probes 锁定行为

## 质量闭环问题

### Q-1 `alignment_v` 与对象实际对齐关系未澄清

现象：

- `alignment_v<V>` 当前按 `alignof(T) * lane_count` 给出
- `basic_vec` / `basic_mask` 实际存储仅为 `std::array`
- 当前 `alignof(vec<int, 4>)` 与 `alignment_v<vec<int, 4>>` 不一致

处置：

- 第三轮必须做决策
- 二选一：
  - 修对象对齐，使其与公开元信息一致
  - 明确 `alignment_v` 仅为算法/内存接口元信息，并补测试/文档口径

### Q-2 probe-gated 标准表面仍过多

现象：

- `where` 的若干入口、mask cast 等仍通过 `check_cxx_source_compiles()` 决定是否打开测试

处置：

- 第三轮应将已稳定支持的公开表面转成硬性测试要求

### Q-3 验证与覆盖仍不足

现象：

- `native_abi` / `deduce_abi_t` 测试未常开
- type matrix 仍然偏窄，未系统覆盖 `signed/unsigned`、`float/double`、`long double`
- native handoff 未实测
- examples 仅 build 未 smoke run

处置：

- 第三轮统一收口

## 三轮执行计划

### 第一轮：修 confirmed defects

范围：

- `I-7`
- `I-8`
- 与这两项直接相关的 compile/runtime coverage

验收：

- `mask<long double, 2>` 不再因一元运算声明失败
- native-width `mask<float/double/unsigned>` 的 `+/-/~` 编译通过
- 现有 simd 测试无回归

### 第二轮：收口高风险标准一致性项

范围：

- `I-9`
- `flag_convert` 正/反向 compile probes

验收：

- 转换构造行为与最终口径一致
- 负向 probe 精确锁定非 value-preserving 转换是否必须显式 `flag_convert`

### 第三轮：质量闭环

范围：

- `Q-1`
- `Q-2`
- `Q-3`

验收：

- `alignment_v` 口径明确并被测试锁定
- 关键 probe-gated 表面转为硬性覆盖
- ABI / typed matrix / handoff / example smoke run 补齐到标准库交付可接受水位

## 最终验收要求

三轮完成后，必须执行：

1. `build-gcc` 全量构建与 `ctest --output-on-failure`
2. `build-zig` 全量构建与 `ctest --output-on-failure`
3. Podman 全量验证，且不得复用路径失效的旧 `build-podman-clang` cache

只有在三套验证均通过后，本轮修复才允许合回 `master`。

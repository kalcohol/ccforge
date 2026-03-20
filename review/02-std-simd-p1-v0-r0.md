# `std::simd` 独立审查报告 (p1-v0-r0)

审查对象：
- 实现文件：`backport/cpp26/simd.hpp`
- 测试文件：`test/CMakeLists.txt`、`test/test_simd*.cpp`

审查基线：
- 当前工作树，基于提交 `871dd63`（`simd: close r7 conformance gaps`）之上的未提交状态
- 当前 C++ 工作草案 `[simd]`：`https://eel.is/c++draft/simd`
- 本文不特别复用既有 review 结论，按当前工作树重新审视

---

## 执行摘要

如果按标准委员会和标准库交付质量要求看，当前 `std::simd` 已不再是大量 P0 缺口阶段，但仍有一批不能继续放过的 P1 问题。

本轮确认的核心问题有三组：

1. `basic_vec` 的标量广播构造与 `constexpr-wrapper-like` 约束仍偏离 `[simd.ctor]`
2. range 构造的参与条件仍然过宽，且 `basic_mask` 仍暴露了非标准公开构造
3. `reduce` 语义模型和 masked `reduce` 默认 identity 仍不符合 `[simd.reductions]`

与此同时，测试与文件结构也已经开始影响继续修复的效率和准确性：

4. `test/CMakeLists.txt` 内联 configure-time probe 过多，`test/test_simd_api.cpp` 与 `test/test_simd.cpp` 明显膨胀，当前测试组织不足以稳定锁住这些 wording-level 约束

这几项已经足以支撑本轮“结构先行、补测后修义”的修复计划。

---

## 主要问题

### I-1 `basic_vec` 标量广播构造仍过窄，`constexpr-wrapper-like` 仍是近似实现

- 严重程度：高
- 位置：
  - `backport/cpp26/simd.hpp:369-379`
  - `backport/cpp26/simd.hpp:982-999`
- 相关条文：`[simd.ctor]`

**问题描述：**

当前 `basic_vec(U&&)` 被拆成两套本地规则：

1. 仅接受 `is_supported_value<U>`
2. 或接受本地近似的 `is_constexpr_wrapper_like<U>`

这与当前草案按 `explicitly-convertible-to<value_type>` 和 `constexpr-wrapper-like` 组织构造参与条件的方式并不一致。

结果是：

- 合法的“仅显式可转为 `T`”的标量广播源仍会被拒绝
- `bool` 这类可显式转换到算术 lane 的标量也被当前 `is_supported_value<U>` 规则挡在外面
- 本地 `is_constexpr_wrapper_like` 仅靠 `T::value` 和 `is_convertible` 判定，仍然只是近似，不是当前草案语义

**影响：**

- 公开构造表面仍会错误拒绝合法调用
- `constexpr-wrapper-like` 的判断继续漂移，后续 review 仍容易反复

### I-2 range 构造参与条件仍然过宽，`basic_mask` 仍有非标准公开 range 构造

- 严重程度：高
- 位置：
  - `backport/cpp26/simd.hpp:705-714`
  - `backport/cpp26/simd.hpp:1009-1033`
- 相关条文：
  - `[simd.mask.ctor]`
  - `[simd.ctor]`

**问题描述：**

当前 `basic_vec` 的 range 构造仅要求 contiguous+sized range，然后在函数体内用 `assert(range_size(r) == size)` 做运行时检查。`basic_mask` 则还额外暴露了一个公开的 contiguous range 构造。

与当前草案相比，这里至少有两处偏差：

1. `basic_vec` range 构造的参与条件应在编译期锁定 lane 数匹配，而不是先放进重载集再靠运行时 `assert`
2. `basic_mask` 当前草案并没有这个公开 range 构造

**影响：**

- 动态 extent 的 `std::span` 等本不应参与的调用会被错误接受
- `basic_mask` 的公开表面比标准更宽
- 这些问题会直接改变重载决议，不是“仅实现细节不同”

### I-3 `reduce` 仍按标量二元操作实现，masked `reduce` 默认 identity 仍不完整

- 严重程度：高
- 位置：
  - `backport/cpp26/simd.hpp:170-199`
  - `backport/cpp26/simd.hpp:3127-3148`
- 相关条文：`[simd.reductions]`

**问题描述：**

当前 `reduce` 的实现仍按：

- `BinaryOperation(T, T) -> T`

的标量模型折叠，而不是按当前草案的 `vec<T, 1>` 归约模型组织 `BinaryOperation`。这会让只接受 `vec<T, 1>` 的合法 reducer 在函数体实例化时失败。

同时，masked `reduce` 的默认 identity 目前只补了 `plus` / `multiplies`，对 `bit_and` / `bit_or` / `bit_xor` 仍不完整。

**影响：**

- 合法自定义 reducer 仍无法使用
- 掩码归约对标准要求的默认操作集合仍不完整
- 这会持续迫使测试写成“只能覆盖标量 plus/multiplies 的子集”

### I-4 测试与文件结构已开始妨碍后续修复质量

- 严重程度：中
- 位置：
  - `test/CMakeLists.txt:11-318`
  - `test/test_simd_api.cpp:1-533`
  - `test/test_simd.cpp:31-443`
  - `backport/cpp26/simd.hpp`

**问题描述：**

当前测试和实现结构已经出现两个明显信号：

1. configure-time probe 大量内联在 `test/CMakeLists.txt` 中，维护成本偏高
2. `test/test_simd_api.cpp`、`test/test_simd.cpp` 与单体 `backport/cpp26/simd.hpp` 都在持续膨胀

更关键的是，本轮正是靠额外探针才重新确认：

- 显式可转换标量广播仍被拒绝
- 动态 extent range 构造仍被错误接受
- `basic_mask` 非标准 range 构造仍存在
- `reduce` / masked `reduce` 的约束与 identity 仍缺测

这说明测试不是“只有少量空白”，而是组织方式已经难以稳定支撑 wording-level 回归防护。

**影响：**

- 修一个 wording 约束，容易在别处重新漂移
- review 结论与实现状态更容易再次脱节

---

## 修复计划

### Phase 1 先整理结构与测试

1. 将 configure-time simd probe 从 `test/CMakeLists.txt` 拆出到独立源文件，并用统一 CMake helper 管理
2. 将 `test/test_simd_api.cpp` 按主题拆成多个 compile probe 文件
3. 将 `test/test_simd.cpp` 按核心构造、扩展/置换、归约等主题拆分
4. 为本轮问题补足正向/负向 compile probe 与 runtime coverage

### Phase 2 再拆实现文件

1. 保持 `backport/simd` 与 `backport/cpp26/simd.hpp` 入口不变
2. 将 `backport/cpp26/simd.hpp` 拆为 `backport/cpp26/simd/` 下的内部子头
3. 先按依赖顺序做 contiguous split，再在子头层面形成更清晰的职责分区

### Phase 3 修正当前 P1 语义问题

1. 按当前草案重写 `basic_vec` 标量广播构造参与条件
2. 收紧 `constexpr-wrapper-like` 判定
3. 收紧 range 构造参与条件并移除 `basic_mask` 的非标准公开 range 构造
4. 按 `[simd.reductions]` 修正 `reduce` 的 reducer 模型与 masked identity

### Phase 4 全量验证并收口

1. 先跑新增 probe 与相关 simd 子集测试
2. 再跑完整 simd 测试
3. 完成提交后，将本地修复分支合回本地 `master`，仅推送 `master`

---

## 结论

当前 `std::simd` 已经进入“要把 wording-level 偏差、测试组织和实现结构一起收紧”的阶段，不能再只靠在单体头文件和单个大测试文件里继续堆补丁。

本轮最合理的执行顺序是：

1. 先拆测试与内部结构
2. 再补本轮缺测
3. 然后修正构造与 reduction 语义
4. 最后以完整验证和正式合并收尾

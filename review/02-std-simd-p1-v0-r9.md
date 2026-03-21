# `std::simd` 独立审查报告 (p1-v0-r9)

审查对象：

- 实现文件：`backport/cpp26/simd/base.hpp`
- 实现文件：`backport/cpp26/simd/types.hpp`
- 测试文件：`test/simd/compile/test_simd_api_memory_load_store.cpp`
- 测试文件：`test/simd/compile/test_simd_api_memory_gather_scatter.cpp`
- 测试文件：`test/simd/runtime/test_simd_memory_load_store.cpp`
- 测试文件：`test/simd/runtime/test_simd_memory_gather_scatter.cpp`
- 探针与注册：`test/simd/configure_probes/*.cpp`

审查基线：

- 隔离 worktree：`fix/std-simd-p1-r9`
- 基线提交：`25a6339`
- 当前 C++ 工作草案 `[simd.syn]`：`https://eel.is/c++draft/simd.syn`
- 当前 C++ 工作草案 `[simd.general]`：`https://eel.is/c++draft/simd.general`

执行方式：

- 在 `25a6339` 基线上做最后一轮独立复核
- 继续以当前工作草案为唯一规范基线
- 除 GCC 外，额外用 `zig c++` 与 Podman 容器复核，专门检查“GCC 绿测但跨工具链不稳”的尾部问题

---

## 执行摘要

当前 `25a6339` 基线上的 `std::simd` 已没有前几轮那种明显的公开标准违约点，但最后一轮复核仍然发现三类不应带着过线的问题：

1. `partial_load` / `unchecked_load` 的 masked contiguous-range 默认入口，以及 masked contiguous-range gather/scatter，虽然实现已具备，但测试体系还没有正式锁住
2. 一旦把这些入口补成正式测试，立刻暴露出测试代码把默认向量宽度硬编码成 `4` 的问题；这在 GCC 上碰巧成立，但在 `zig c++`/Clang 路径下会直接失真
3. 实现已经支持 `complex` 和扩展整数，但几处核心 `static_assert` 诊断文本仍然写着“only supports arithmetic non-bool”，会继续误导后续审查

换言之，最后这轮的主要工作不再是补大块功能，而是把**最后的测试盲点、跨工具链可移植性问题和误导性诊断**一起收口。

---

## 主要问题

### I-1 `[simd.syn]` 里的 masked contiguous-range memory 入口仍缺正式覆盖

- 严重程度：中
- 位置：
  - `test/simd/compile/test_simd_api_memory_load_store.cpp`
  - `test/simd/runtime/test_simd_memory_load_store.cpp`
  - `test/simd/compile/test_simd_api_memory_gather_scatter.cpp`
  - `test/simd/runtime/test_simd_memory_gather_scatter.cpp`
  - `test/simd/configure_probes/*.cpp`

**问题描述：**

当前工作草案在 `[simd.syn]` 中已经给出：

- masked contiguous-range `partial_load` / `unchecked_load`
- masked contiguous-range `partial_store` / `unchecked_store`
- masked contiguous-range `partial_gather_from` / `unchecked_gather_from`
- masked contiguous-range `partial_scatter_to` / `unchecked_scatter_to`

但 `25a6339` 基线上的正式测试仍主要锁在 pointer/iterator 形态和 unmasked range 形态，导致这组 range+mask 入口更多是“实现看起来有”，而不是“测试明确钉住”。

对标准库质量来说，这仍然不够。公开入口只要已经暴露，就应该有 compile/configure/runtime 三层覆盖，而不是靠一次性的临时探针确认。

**影响：**

- 这组入口未来仍可能在重构或 ABI/默认类型调整中悄悄退化
- 缺少正式覆盖时，绿测不能证明这部分公共契约真的稳定

---

### I-2 默认 range 入口的测试仍带有 GCC 宽度假设，跨工具链不稳

- 严重程度：中
- 位置：
  - `test/simd/compile/test_simd_api_memory_load_store.cpp`
  - `test/simd/runtime/test_simd_memory_load_store.cpp`
  - `test/simd/configure_probes/range_masked_load_store.cpp`

**问题描述：**

这轮在把 masked contiguous-range load/store 正式化后，`zig c++`/Clang 立刻暴露出一个测试层面的真实问题：

- 默认 `partial_load(range, mask)` / `unchecked_load(range, mask, ...)` 的 mask 类型必须跟随默认推导出来的 `V::mask_type`
- 但测试若把它硬写成 `mask<int, 4>`，本质上就是把“默认 native 宽度等于 4”当成前提

这个前提并不由标准保证，也不该由 backport 测试假定。对默认 `basic_vec<int>` 而言，正确写法必须从默认向量类型本身派生 `mask_type`，而不是从 GCC/SSE2 的碰巧结果反推。

`zig c++` 运行时还进一步暴露出 related issue：

- `unchecked_load(range, ...)` 需要 `range_size >= V::size`
- 因此 runtime 测试若固定把 range 长度写死为 `4`，同样会在非 GCC 默认宽度上变成伪测试

**影响：**

- 测试会把“GCC 默认宽度”误当作“标准要求”
- 绿测的可移植性证明失真，无法支撑委员会/标准库级质量判断

---

### I-3 核心诊断文本仍把支持类型错误描述为 arithmetic-only

- 严重程度：中
- 位置：
  - `backport/cpp26/simd/base.hpp`
  - `backport/cpp26/simd/types.hpp`

**问题描述：**

前几轮修复后，当前实现已经明确支持：

- 非 `bool` arithmetic
- `std::complex<T>`（其中 `T` 为当前实现接受的 floating-point）
- `__int128` / `unsigned __int128`（平台可用时）

但几处核心 `static_assert` 诊断文本仍然写着：

- `only supports arithmetic non-bool value types`
- `supported non-bool arithmetic type`

这已经不符合实现真实状态，也会继续误导 review、设计判断和用户侧报错理解。

**影响：**

- 诊断会把本来受支持的类型说成“不应支持”
- 后续审查人员更容易被旧措辞带偏，重复做出错误 accept/reject 结论

---

## 结论

按 `25a6339` 基线做最后一轮独立复核，当前 `std::simd` 还剩下的主要问题已经不再是大块标准缺口，而是最后几项质量尾差：

1. masked contiguous-range memory/gather/scatter 入口缺少正式 compile/configure/runtime 覆盖
2. 默认 range 入口的测试还夹带 GCC 宽度假设，跨工具链不稳
3. 核心诊断文本仍停留在 arithmetic-only 时代，和实现真实支持集合不一致

这三类问题都应该在本轮一起收口，否则“看起来已经差不多了”的结论仍然缺少最后的工程质量支撑。

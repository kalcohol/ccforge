# `std::simd` 独立审查报告 (p1-v0-r7)

审查对象：

- 实现文件：`backport/cpp26/simd/memory.hpp`
- 测试文件：`test/simd/compile/test_simd_api_memory.cpp`
- 测试文件：`test/simd/runtime/test_simd_memory.cpp`
- 探针与注册：`test/simd/configure_probes/*.cpp`、`test/simd/CMakeLists.txt`

审查基线：

- 隔离 worktree：`fix/std-simd-p1-r7`
- 基线提交：`4ddc049`
- 当前 C++ 工作草案 `[simd]`：`https://eel.is/c++draft/simd`
- 重点条文：
  - `[simd.syn]`：`https://eel.is/c++draft/simd.syn`

执行方式：

- 不特别复用既有 review 结论，只以 `4ddc049` 的 `std::simd` 现状为对象复核
- 只在独立 `std::simd` worktree 中核查，避免触碰并干扰另一条 `std::execution` 修复线
- 结合源码直读、最小编译探针与现有测试资产一致性检查给出结论

---

## 执行摘要

当前 `std::simd::memory` 这一片在 `4ddc049` 基线上至少还有两组不应放过的问题：

1. `partial_load` / `unchecked_load` 以及 range 版 gather 的标准默认 `V` 入口缺失，公开接口仍然要求调用方处处显式写模板实参
2. masked gather/scatter 的公开参数顺序仍固化为旧的 `indices, mask`，与当前工作草案的 `mask, indices` 不一致，而且编译/运行测试与探针都在强化这个错误接口

这两组问题的共同点是：**实现和测试一起把“非标准形状”锁死了**。如果只看当时的绿测，很容易误判为 memory 面已经稳定；但按当前工作草案和标准库质量要求，这一片其实仍有明确公开契约漂移。

---

## 主要问题

### I-1 `[simd.syn]` 默认 `V` 入口缺失，标准写法不可达

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/memory.hpp`
  - `test/simd/compile/test_simd_api_memory.cpp`
  - `test/simd/configure_probes/range_load_store.cpp`
  - `test/simd/configure_probes/range_gather_scatter.cpp`

**问题描述：**

当前工作草案在 `[simd.syn]` 中为以下入口给出了默认 `V = see below`：

- `partial_load`
- `unchecked_load`
- `partial_gather_from`
- `unchecked_gather_from`

其中 range 版 gather 的默认结果类型还应按索引向量宽度形成 `vec<range_value_t<R>, I::size()>`。

但 `4ddc049` 基线上的实现仍然只提供显式 `V` 版本，导致下列标准写法不可达：

- `std::simd::partial_load(input_view)`
- `std::simd::unchecked_load(input_view, std::simd::flag_default)`
- `std::simd::partial_gather_from(input_view, indices)`
- `std::simd::unchecked_gather_from(input_view, indices)`

现有编译测试和 configure probe 也几乎全都显式写 `<int4>` / `<vec<int, 4>>`，因此没有把这类标准入口真正钉住。

**影响：**

- 公开接口仍偏向“实现内部方便”的显式模板调用，而不是标准要求的默认入口
- 下游如果按当前草案直接写标准形式，会在 backport 上编译失败
- 现有测试无法及时发现默认结果类型和默认入口是否退化

---

### I-2 `[simd.syn]` masked gather/scatter 仍暴露旧的 `indices, mask` 顺序

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/memory.hpp`
  - `test/simd/compile/test_simd_api_memory.cpp`
  - `test/simd/runtime/test_simd_memory.cpp`

**问题描述：**

当前工作草案在 `[simd.syn]` 中对 masked gather/scatter 的参数顺序要求是：

- `mask, indices`

但 `4ddc049` 基线上的实现仍公开暴露：

- `partial_gather_from(..., indices, mask, ...)`
- `unchecked_gather_from(..., indices, mask, ...)`
- `partial_scatter_to(..., indices, mask, ...)`
- `unchecked_scatter_to(..., indices, mask, ...)`

更严重的是，现有 compile/runtime tests 也全部按这一旧顺序编写，因此整套测试事实上在给错误接口“背书”。

**影响：**

- backport 公开表面与当前工作草案不一致
- 既有测试不能防止旧顺序继续回流，反而会把错误参数顺序持续固化
- 即使未来 range 重载表面看似正确，底层转发和扩展重载仍可能保持错误形状

---

## 结论

按 `4ddc049` 基线独立复核，当前 `std::simd::memory` 仍至少有以下两组 P1 问题：

1. 默认 `V` 入口缺失，`partial_load` / `unchecked_load` 与 range gather 的标准写法不可达
2. masked gather/scatter 仍暴露旧的 `indices, mask` 顺序，且测试在强化这一错误接口

如果要一次性收口，这一轮不能只改实现，必须同时补三类测试资产：

1. 编译期正向断言：锁定默认 `V` 和标准顺序
2. configure probe：锁定默认 range 入口可用
3. 负向 probe：锁定旧的 masked 参数顺序必须被拒绝

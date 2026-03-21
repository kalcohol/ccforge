# `std::simd` 独立审查报告 (p1-v0-r10)

审查对象：

- 实现文件：`backport/cpp26/simd.hpp`
- 实现文件：`backport/cpp26/simd/base.hpp`
- 实现文件：`backport/cpp26/simd/types.hpp`
- 相关测试：`test/simd/**/*`

审查基线：

- 当前分支：`master`
- 基线提交：`bc98644`
- 当前 C++ 工作草案 `[simd.syn]`：`https://eel.is/c++draft/simd.syn`
- 当前 C++ 工作草案 `[simd.general]`：`https://eel.is/c++draft/simd.general`
- 当前 C++ 工作草案 `[simd.overview]`：`https://eel.is/c++draft/simd.overview`
- 当前 C++ 工作草案 `[simd.mask.overview]`：`https://eel.is/c++draft/simd.mask.overview`
- 当前 C++ 工作草案 `[simd.traits]`：`https://eel.is/c++draft/simd.traits`

---

## 执行摘要

`bc98644` 之后的 `std::simd` 已经没有前几轮那种大块 memory/gather/scatter 缺口，但按 current draft 重新逐条核对公开模板边界后，仍然有四类不能带着过线的问题：

1. **vectorizable type 集合仍然放宽**：`long double` 和 `__int128`/`unsigned __int128` 还被实现和测试一起当成受支持类型
2. **enabled/disabled specialization 模型不对**：不支持的 `basic_vec`/`basic_mask` 不是按标准要求保持完整类型并删除默认/复制/赋值，而是混合了过宽可用和类内报错两种状态
3. **公开 trait/辅助模板边界仍不标准**：`flags<...>` 未限制参数包；`alignment` 对 `basic_mask` 越界暴露，同时缺少 `alignment<basic_vec<...>, U>` 的跨 value_type 支持
4. **非标准 `where` API 仍然公开暴露**：current draft 的 `<simd>` synopsis 中没有这组接口，但实现和测试仍把它当成公共表面

此外，测试体系本身还在给这些非标准行为背书，所以这轮不只是改实现，还必须同步重写 probe/runtime/compile 基线。

---

## 主要问题

### I-1 非标准 value type 仍被当成受支持类型

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/base.hpp`
  - `test/simd/runtime/test_simd_abi.cpp`
  - `test/simd/runtime/test_simd_mask.cpp`
  - `test/simd/compile/test_simd_api_memory_load_store.cpp`
  - `test/simd/configure_probes/default_supported_load_store.cpp`

**问题描述：**

current draft `[simd.overview]` 的 vectorizable type 只覆盖：

- 除 `bool` 外的标准整数/字符类型
- `float` / `double`
- 实现定义时存在的 `std::float16_t` / `std::float32_t` / `std::float64_t`
- 上述 vectorizable floating-point 的 `std::complex<T>`

当前实现却继续把：

- `long double`
- `__int128` / `unsigned __int128`

纳入支持集，导致 `vec<long double, 2>`、`basic_vec<__int128>`、默认 load/store 推导和相关测试都沿着旧的扩展方向工作。

这已经不是“实现选择不同”，而是公开 API 超出 current draft。

### I-2 disabled specialization 与 fixed-size 上限没有按标准收死

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/base.hpp`
  - `backport/cpp26/simd/types.hpp`
  - `test/simd/runtime/test_simd_abi.cpp`
  - `test/simd/runtime/test_simd_mask.cpp`

**问题描述：**

current draft `[simd.overview]` / `[simd.mask.overview]` 要求：

- disabled specialization 仍然是完整类型
- 但要删除默认构造、析构、复制构造、复制赋值
- `fixed_size<N>` 的实现定义最大值不得小于 `64`

当前实现的问题是双向的：

- 一边继续让 `vec<int, 65>`、`mask<signed char, 128>` 这类超范围 fixed-size 直接可用
- 另一边对不支持类型又依赖类体内部 `static_assert` 或成员缺失，未形成“完整但 deleted”的稳定模型

这会让错误既不符合标准语义，也不利于后续 trait/探针稳定判断。

### I-3 `flags` 与 `alignment` 的公开边界不符合 current draft

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/base.hpp`
  - `backport/cpp26/simd/types.hpp`
  - `test/simd/configure_probes/*.cpp`

**问题描述：**

按 `[simd.syn]` / `[simd.traits]`：

- `flags<...>` 的参数包只能是 `convert_flag`、`aligned_flag`、`overaligned_flag<N>`
- `alignment<T, U>` 的 `value` 只对 `T` 为 `basic_vec` specialization、且 `U` 为 vectorizable type 时存在

当前实现仍然有两个问题：

1. `flags<int>` 一类非法参数包仍可形成实例
2. `alignment` 一边错误暴露到 `basic_mask`，另一边又缺少 `alignment_v<vec<int, 4>, float>` 这样的跨 `value_type` 查询

这会直接影响公开 trait 语义和 memory 相关契约表达。

### I-4 整组 `where` API 属于 current draft 之外的公开扩展

- 严重程度：高
- 位置：
  - `backport/cpp26/simd.hpp`
  - `backport/cpp26/simd/where.hpp`
  - `test/simd/compile/test_simd_api_where.cpp`
  - `test/simd/runtime/test_simd_memory_where.cpp`
  - `test/simd/configure_probes/where_*.cpp`

**问题描述：**

这轮重新按 current draft `<simd>` synopsis 核对后，可以确认当前工作草案中并没有 `where_expression` / `where(...)` 这组公开接口。

因此当前实现的问题不只是 `where(bool, vec)` 这种单点偏差，而是整组 `where` API 本身就是 extra public surface。  
这与仓库的“无额外 API、未来可无缝切换原生标准库”原则直接冲突。

测试还持续把它们当成“必须支持”的标准功能，进一步放大了偏离。

---

## 结论

`bc98644` 之后的 `std::simd` 仍然存在 4 类 current-draft 级别问题，需要在同一轮一起收口：

1. 收紧 vectorizable type，只保留 current draft 允许的类型集合
2. 把 `basic_vec` / `basic_mask` 调整为标准要求的 enabled/disabled specialization 语义，并确定实现上限
3. 修正 `flags` 与 `alignment` 的公开约束
4. 移除整组非标准 `where` 公开接口，并同步清理错误测试基线

如果这四类问题不一起修，`std::simd` 虽然在功能上“能跑很多东西”，但仍然不满足标准库/委员会质量要求下最关键的公开契约准确性。

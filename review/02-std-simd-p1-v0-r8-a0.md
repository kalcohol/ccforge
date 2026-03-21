# `std::simd` 审查答复 (p1-v0-r8-a0)

审查基线：

- 当前 C++ 工作草案 `[simd.syn]`：`https://eel.is/c++draft/simd.syn`
- 当前 C++ 工作草案 `[simd.general]`：`https://eel.is/c++draft/simd.general`

应答对象：

- `review/02-std-simd-p1-v0-r8.md`

答复口径：

- 本文件只记录本轮已落地并通过验证的修正
- 继续以当前工作草案为唯一标准基线，同时保持实现内部已声明支持类型集合自洽
- 不回写既有答复时间线

---

## 总结

`p1-v0-r8` 提出的 supported-type 默认 load 入口问题，本轮已经连同测试补完和文件拆解一起收口：

1. 默认 pointer load-vector 推导改为使用实现真实支持集合
2. 标准要求的 `std::complex<T>` 默认 `partial_load` / `unchecked_load` 入口恢复可达
3. 平台可用时，库内已声明支持的 `__int128` 默认入口也恢复自洽
4. `memory` 源码与测试按 load/store、gather/scatter、where、supported types 拆分，减少单文件继续膨胀

---

## 逐条答复

### I-1 默认 pointer/count load 入口对 supported type 的推导门槛错误

裁决：**已修复。**

本轮把默认 pointer load-vector 的门槛从“arithmetic 非 bool”收敛为实现真实支持集合：

- `backport/cpp26/simd/memory_common.hpp`
  - `default_pointer_load_vector` 现在直接使用 `is_supported_value<remove_cvref_t<T>>`：`29-38`

这样修完后：

1. `[simd.general]` 允许的 `std::complex<float>` / `std::complex<double>` 默认 load 入口恢复可用
2. 对实现内部已声明支持的 `__int128` / `unsigned __int128`，默认入口行为也与 `basic_vec<T>` 支持集合重新保持一致

为了避免这一问题继续藏在单个大文件里，本轮同时把 memory 实现拆分为：

- `backport/cpp26/simd/memory_common.hpp`
- `backport/cpp26/simd/memory_load_store.hpp`
- `backport/cpp26/simd/memory_gather_scatter.hpp`
- `backport/cpp26/simd/memory.hpp` 仅保留聚合入口：`1-3`

### 测试补完

裁决：**已补齐。**

本轮把原先的单个 memory compile/runtime 文件拆成按功能分组的子文件，并补上 supported-type 默认入口覆盖：

- compile 期断言：
  - `test/simd/compile/test_simd_api_memory_load_store.cpp`
    - 新增 `std::complex<float>` 默认 `partial_load` / `unchecked_load` 断言：`63-92`
    - 新增 `__int128` 默认 load 断言（平台可用时）：`94-112`
- configure probe：
  - `test/simd/configure_probes/default_supported_load_store.cpp`
    - 固化 complex 默认 pointer/count、pointer/sentinel、range 入口：`7-29`
    - 固化 `__int128` 默认 pointer/count 入口（平台可用时）：`31-43`
  - `test/simd/configure_probes/CMakeLists.txt`
    - 注册新 probe：`76-80`
- runtime：
  - `test/simd/runtime/test_simd_memory_supported_types.cpp`
    - 新增 complex 默认 load/store round-trip 覆盖：`12-37`
  - `test/simd/runtime/CMakeLists.txt`
    - 注册 `test_simd_memory_supported_types`：`15-20`

辅助测试别名也一并补入：

- `test/simd/support/simd_test_common.hpp`
  - `default_complexf` / `default_complexd`：`38-39`
  - `default_int128` / `default_uint128`（平台可用时）：`40-45`

### 测试与构建结构整理

裁决：**已完成。**

本轮顺手把之前已经开始膨胀的 memory 相关源码和测试结构拆开，保持入口和兼容性不变：

- `test/simd/CMakeLists.txt`
  - 收敛为公共 helper 与子目录分发：`1-46`
- `test/simd/compile/CMakeLists.txt`
  - compile probes 独立注册：`1-26`
- `test/simd/runtime/CMakeLists.txt`
  - runtime tests 独立注册：`1-27`
- `test/simd/configure_probes/CMakeLists.txt`
  - configure probes 独立注册：`1-140`

原来的单文件已拆分为：

- compile：
  - `test/simd/compile/test_simd_api_memory_load_store.cpp`
  - `test/simd/compile/test_simd_api_memory_gather_scatter.cpp`
- runtime：
  - `test/simd/runtime/test_simd_memory_load_store.cpp`
  - `test/simd/runtime/test_simd_memory_gather_scatter.cpp`
  - `test/simd/runtime/test_simd_memory_where.cpp`
  - `test/simd/runtime/test_simd_memory_supported_types.cpp`

---

## 验证

本轮在隔离 worktree 内完成构建验证；`googletest` 仅复制到该 worktree 作为本地测试依赖，不纳入提交。

已执行：

```bash
cmake -S . -B build -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_EXAMPLES=OFF -DFORGE_BUILD_TESTS=ON
cmake --build build --parallel --target \
  test_simd_core test_simd_expansion test_simd_reductions test_simd_abi test_simd_mask \
  test_simd_memory_load_store test_simd_memory_gather_scatter test_simd_memory_where \
  test_simd_memory_supported_types test_simd_operators test_simd_math test_simd_bit \
  test_simd_math_ext test_simd_math_special test_simd_complex
ctest --test-dir build --output-on-failure -R simd
```

结果：

- `simd` 子集 `31/31` 全部通过
- configure 阶段新增 `FORGE_SIMD_HAS_DEFAULT_SUPPORTED_LOAD_STORE` probe 通过

---

## 结论

`p1-v0-r8` 提出的这一组问题，本轮已经完成实现修复、测试补完和结构整理：

1. `std::complex<T>` 默认 load 入口恢复为标准可达
2. 实现已声明支持的扩展整数默认入口恢复自洽
3. compile/configure/runtime 三层覆盖已经补上
4. memory 源码与测试拆分后，后续继续扩展时更容易保持质量和覆盖完整性

因此，`p1-v0-r8` 所列问题在当前工作树上已收口。

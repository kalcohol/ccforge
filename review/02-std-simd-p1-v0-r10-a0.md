# `std::simd` 审查答复 (p1-v0-r10-a0)

审查基线：

- 当前 C++ 工作草案 `[simd.syn]`：`https://eel.is/c++draft/simd.syn`
- 当前 C++ 工作草案 `[simd.general]`：`https://eel.is/c++draft/simd.general`
- 当前 C++ 工作草案 `[simd.overview]`：`https://eel.is/c++draft/simd.overview`
- 当前 C++ 工作草案 `[simd.mask.overview]`：`https://eel.is/c++draft/simd.mask.overview`
- 当前 C++ 工作草案 `[simd.traits]`：`https://eel.is/c++draft/simd.traits`

应答对象：

- `review/02-std-simd-p1-v0-r10.md`

---

## 总结

`p1-v0-r10` 提出的四类问题，本轮已经一起收口：

1. 移除了 `long double` 与 `__int128`/`unsigned __int128` 的非标准 `std::simd` 支持面
2. 把 `basic_vec` / `basic_mask` 调整为 current draft 要求的 enabled/disabled specialization 模型，并把固定宽度上限收敛到实现定义的 `64`
3. 收紧了 `flags<...>` 与 `alignment` 的公开边界
4. 移除了整组非标准 `where` 公共 API 和相应测试基线

本轮不是在补一块新功能，而是在把 `std::simd` 的公开模板边界和测试口径重新拉回 current draft。

---

## 逐条答复

### I-1 非标准 value type 仍被当成受支持类型

裁决：**已修复。**

本轮把 `std::simd` 支持集收紧为 current draft 的 vectorizable type：

- 保留：标准整数/字符类型、`float`、`double`
- 新增 current draft 对应的 `std::float16_t` / `std::float32_t` / `std::float64_t`（实现定义时）
- 保留：上述 vectorizable floating-point 的 `std::complex<T>`
- 移除：`long double`
- 移除：`__int128` / `unsigned __int128`

对应修正：

- 实现：
  - `backport/cpp26/simd.hpp`
  - `backport/cpp26/simd/base.hpp`
- 测试：
  - 删除 `long double`、`__int128` 的正向 probe/runtime 断言
  - 新增负向 probe：
    - `test/simd/configure_probes/reject_long_double_value.cpp`
    - `test/simd/configure_probes/reject_extended_integer.cpp`
  - 新增 `stdfloat` 正向 probe：
    - `test/simd/configure_probes/stdfloat_value_types.cpp`

### I-2 disabled specialization 与 fixed-size 上限没有按标准收死

裁决：**已修复。**

本轮把 `basic_vec` / `basic_mask` 改成两套 specialization：

- enabled specialization：保留现有完整功能面
- disabled specialization：仅保留 current draft 要求的类型成员，并删除默认构造、析构、复制构造、复制赋值

同时把实现定义的 fixed-size 上限定为 `64`：

- `vec<int, 64>` 仍可用
- `vec<int, 65>` 通过 `vec` / `mask` / `resize_t` 等公共 alias 不再形成可用标准类型
- `basic_vec<int, fixed_size_abi<65>>` 仍是完整类型，但处于 deleted 状态

对应修正：

- 实现：
  - `backport/cpp26/simd/base.hpp`
  - `backport/cpp26/simd/types.hpp`
- 测试：
  - `test/simd/runtime/test_simd_abi.cpp` 新增 disabled specialization 静态断言
  - `test/simd/configure_probes/reject_oversized_fixed_size.cpp`
  - `test/simd/runtime/test_simd_mask.cpp` 不再把 `128` lane 当成必须支持的固定前提

### I-3 `flags` 与 `alignment` 的公开边界不符合 current draft

裁决：**已修复。**

本轮做了两类收口：

1. `flags<...>` 现在只接受：
   - `convert_flag`
   - `aligned_flag`
   - `overaligned_flag<N>`
2. `alignment` 现在：
   - 不再为 `basic_mask` 暴露 `value`
   - 为 `basic_vec` 补齐跨 `value_type` 的 `alignment_v<V, U>`

对应修正：

- 实现：
  - `backport/cpp26/simd/base.hpp`
  - `backport/cpp26/simd/types.hpp`
- 测试：
  - `test/simd/configure_probes/reject_invalid_flags.cpp`
  - `test/simd/configure_probes/reject_mask_alignment.cpp`
  - `test/simd/configure_probes/cross_type_alignment.cpp`
  - `test/simd/runtime/test_simd_abi.cpp` 补齐跨类型 `alignment_v` 断言

### I-4 整组 `where` API 属于 current draft 之外的公开扩展

裁决：**已修复。**

这轮重新按 current draft `<simd>` synopsis 复核后，确认整组 `where` API 不属于当前标准公共表面。  
因此本轮不是只删 `where(bool, vec)`，而是把整组 `where` 扩展从公开 `<simd>` 接口中移除。

对应修正：

- 实现：
  - `backport/cpp26/simd.hpp` 不再包含 `where.hpp`
  - 删除 `backport/cpp26/simd/where.hpp`
- 测试：
  - 删除 `test/simd/compile/test_simd_api_where.cpp`
  - 删除 `test/simd/runtime/test_simd_memory_where.cpp`
  - 删除 `test/simd/configure_probes/where_*.cpp`
  - 新增负向 probe：
    - `test/simd/configure_probes/reject_nonstandard_where.cpp`

---

## 验证

本轮完成了四套完整验证：

1. 本地 GCC

```bash
cmake -S . -B build-gcc-r10b -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON
cmake --build build-gcc-r10b --parallel
ctest --test-dir build-gcc-r10b --output-on-failure
```

结果：`41/41` 全通过。

2. 本地 Zig/Clang

```bash
CC='zig cc' CXX='zig c++' cmake -S . -B build-zig-r10b -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON
cmake --build build-zig-r10b --parallel
ctest --test-dir build-zig-r10b --output-on-failure
```

结果：`44/44` 全通过。

3. Podman Clang

```bash
podman run --rm --userns=keep-id -v "$PWD:/work:Z" -w /work docker.io/silkeh/clang:latest bash -lc \
  'cmake -S . -B build-podman-clang-r10 -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON && cmake --build build-podman-clang-r10 -j && ctest --test-dir build-podman-clang-r10 --output-on-failure'
```

结果：`41/41` 全通过。

4. Podman GCC

```bash
podman run --rm --userns=keep-id --user 0 -v "$PWD:/work:ro,Z" -w /work docker.io/gcc:latest bash -lc \
  'apt-get update -qq && apt-get install -y --no-install-recommends -qq cmake ninja-build >/dev/null && cp -a /work /tmp/work && cmake -S /tmp/work -B /tmp/build-gcc-r10 -G Ninja -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON && cmake --build /tmp/build-gcc-r10 -j && ctest --test-dir /tmp/build-gcc-r10 --output-on-failure'
```

结果：`41/41` 全通过。

---

## 结论

`p1-v0-r10` 识别出的 current-draft 级别边界问题，本轮已经全部落地修复：

1. 非标准 value type 支持面已收紧
2. disabled specialization 与 fixed-size 上限已按标准模型实现
3. `flags` / `alignment` 的公开边界已纠正
4. 非标准 `where` 公共 API 已移除

到这一轮为止，`std::simd` 的主要收口工作已经从“补功能”转向“维持这套 current-draft 契约和测试纪律”。本轮所列问题已闭环。

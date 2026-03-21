# `std::simd` 审查答复 (p1-v0-r9-a0)

审查基线：

- 当前 C++ 工作草案 `[simd.syn]`：`https://eel.is/c++draft/simd.syn`
- 当前 C++ 工作草案 `[simd.general]`：`https://eel.is/c++draft/simd.general`

应答对象：

- `review/02-std-simd-p1-v0-r9.md`

答复口径：

- 本文件只记录本轮已落地并通过验证的修正
- 继续以当前工作草案为唯一规范基线
- 不回写已有答复时间线

---

## 总结

`p1-v0-r9` 提出的最后三类问题，本轮已经一起收口：

1. masked contiguous-range load/store 与 gather/scatter 已补齐正式 compile/configure/runtime 覆盖
2. 默认 range 入口测试不再硬编码 GCC 的 4-lane 假设，改为从默认向量类型推导 `mask_type` 和合法 unchecked extent
3. `base.hpp` / `types.hpp` 的核心诊断文本已改成与当前真实支持集合一致

这一轮最大的价值，不是再补一块新功能，而是把 `std::simd` 剩余的测试盲点和跨工具链假设彻底扫掉。

---

## 逐条答复

### I-1 masked contiguous-range memory 入口缺正式覆盖

裁决：**已修复。**

本轮为 masked contiguous-range memory / gather / scatter 入口补齐了三层覆盖：

- compile：
  - `test/simd/compile/test_simd_api_memory_load_store.cpp`
    - `partial_load(range, mask)` / `unchecked_load(range, mask, ...)`：`34-45`、`140-160`
  - `test/simd/compile/test_simd_api_memory_gather_scatter.cpp`
    - masked range gather/scatter：`12-35`
- configure probe：
  - `test/simd/configure_probes/range_masked_load_store.cpp`
  - `test/simd/configure_probes/range_masked_gather_scatter.cpp`
  - `test/simd/configure_probes/CMakeLists.txt` 注册：`71-100`
- runtime：
  - `test/simd/runtime/test_simd_memory_load_store.cpp`
    - `PartialLoadAndStoreSupportMaskedContiguousRangeOverloads`：`144-162`
    - `UncheckedLoadAndStoreSupportMaskedContiguousRangeOverloads`：`289-310`
  - `test/simd/runtime/test_simd_memory_gather_scatter.cpp`
    - `PartialGatherAndScatterSupportMaskedContiguousRanges`：`82-101`
    - `UncheckedMaskedGatherAndScatterSupportContiguousRanges`：`176-195`

### I-2 默认 range 入口测试带 GCC 宽度假设

裁决：**已修复。**

这轮新增测试后，`zig c++` 立即把一个此前被 GCC 绿测掩盖的问题暴露出来：

- 默认 range load/store 的 `mask` 必须来自默认推导出的 `basic_vec<int>::mask_type`
- `unchecked_load(range, ...)` 的 runtime 覆盖必须保证 `range_size >= default_int::size`

本轮已把这些测试全部改成从默认向量类型派生，而不再写死 `mask4` 或固定 `4` 元素 unchecked range：

- `test/simd/compile/test_simd_api_memory_load_store.cpp`
  - 改为使用 `typename std::simd::basic_vec<int>::mask_type`：`38-41`、`142-146`
- `test/simd/runtime/test_simd_memory_load_store.cpp`
  - masked partial range 测试改用 `default_int::mask_type`：`149-162`
  - masked unchecked range 测试改用 `default_int::size` 自适应 extent：`289-310`
- `test/simd/configure_probes/range_masked_load_store.cpp`
  - 改为 `using default_mask = typename default_int::mask_type`

这不是“为了让某个编译器过”，而是把测试重新改成标准语义和默认类型语义本来就要求的样子。

### I-3 诊断文本仍错误描述为 arithmetic-only

裁决：**已修复。**

本轮把几处核心 `static_assert` 文本统一改成与当前实现真实支持集合一致：

- `backport/cpp26/simd/base.hpp`
  - `convert_or_copy` 相关诊断：`271-279`
  - `native_abi` 诊断：`291-294`
- `backport/cpp26/simd/types.hpp`
  - `basic_vec` 诊断：`298-304`

现在这些诊断不再错误暗示“只支持 arithmetic non-bool”，而是统一表述为“supported std::simd value type”。

### 补齐 supported-type default gather 覆盖

裁决：**已补齐。**

为了不让“supported-type blind spot”从 load/store 转移到 gather，这轮顺手把默认 complex range gather 也正式锁住了：

- compile：
  - `test/simd/compile/test_simd_api_memory_gather_scatter.cpp`：`57-64`
- configure probe：
  - `test/simd/configure_probes/range_masked_gather_scatter.cpp`
- runtime：
  - `test/simd/runtime/test_simd_memory_gather_scatter.cpp`
    - `DefaultComplexGatherFromRangeUsesSupportedType`：`103-126`

---

## 验证

本轮完成了四套完整验证：

1. 本地 GCC

```bash
cmake -S . -B build-gcc-final2 -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON
cmake --build build-gcc-final2 --parallel
ctest --test-dir build-gcc-final2 --output-on-failure
```

结果：`43/43` 全通过。

2. 本地 Zig/Clang

```bash
CC='zig cc' CXX='zig c++' cmake -S . -B build-zig-final2 -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON
cmake --build build-zig-final2 --parallel
ctest --test-dir build-zig-final2 --output-on-failure
```

结果：`43/43` 全通过。

3. Podman Clang

```bash
podman run --rm --userns=keep-id -v "$PWD:/work:Z" -w /work docker.io/silkeh/clang:latest bash -lc \
  'cmake -S . -B build-podman-clang-final -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON && cmake --build build-podman-clang-final -j && ctest --test-dir build-podman-clang-final --output-on-failure'
```

结果：`43/43` 全通过。

4. Podman GCC

```bash
podman run --rm --userns=keep-id --user 0 -v "$PWD:/work:ro,Z" -w /work docker.io/gcc:latest bash -lc \
  'apt-get update -qq && apt-get install -y --no-install-recommends -qq cmake ninja-build >/dev/null && cp -a /work /tmp/work && cmake -S /tmp/work -B /tmp/build-gcc-final -G Ninja -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON && cmake --build /tmp/build-gcc-final -j && ctest --test-dir /tmp/build-gcc-final --output-on-failure'
```

结果：`43/43` 全通过。

---

## 结论

`p1-v0-r9` 提出的最后三类质量尾差，本轮已经完成修复并做了跨工具链验证：

1. masked contiguous-range memory/gather/scatter 已有正式三层覆盖
2. 默认 range 入口测试已去掉 GCC-only 宽度假设
3. 核心诊断文本已与真实支持集合一致

到这一步，`std::simd` 剩余的主要风险已经不再是“还藏着明显标准缺口”，而是后续是否还能继续保持这套覆盖和纪律。当前工作树上，`p1-v0-r9` 所列问题已收口。

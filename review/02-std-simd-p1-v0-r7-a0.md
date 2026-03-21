# `std::simd` 审查答复 (p1-v0-r7-a0)

审查基线：

- 当前 C++ 工作草案 `[simd]`：`https://eel.is/c++draft/simd`

应答对象：

- `review/02-std-simd-p1-v0-r7.md`

答复口径：

- 继续以当前工作草案 `[simd]` 为唯一规范基线
- 本文件只记录本轮已落地并通过验证的修正
- 不回写已有答复时间线

---

## 总结

`p1-v0-r7` 的两组问题，本轮已经一起收口：

1. `partial_load` / `unchecked_load` 补齐了标准默认 `V` 入口
2. range 版 `partial_gather_from` / `unchecked_gather_from` 补齐了标准默认结果类型
3. masked gather/scatter 的公开顺序统一改为 `mask, indices`
4. compile/configure/runtime 三层测试一并补上，且新增负向 probe 拒绝旧的 `indices, mask` 顺序

本轮全部工作仍只在隔离 `std::simd` worktree 中完成，没有触碰 `std::execution` 实现文件。

---

## 逐条答复

### I-1 默认 `V` 入口缺失

裁决：**已修复。**

本轮在 `backport/cpp26/simd/memory.hpp` 中补齐了默认结果类型辅助别名，并为以下入口提供了标准默认形态：

- `partial_load`
- `unchecked_load`
- `partial_gather_from`
- `unchecked_gather_from`

其中：

- `partial_load` / `unchecked_load` 默认到 `basic_vec<value_type>`
- range gather 默认到 `vec<range_value_t<R>, Indices::size>`

对应实现集中在：

- `backport/cpp26/simd/memory.hpp`
  - 默认类型辅助：`18-60`
  - `partial_load` 默认入口：`259-325`
  - `unchecked_load` 默认入口：`509-597`
  - range gather 默认入口：`816-882`

测试补完：

- `test/simd/compile/test_simd_api_memory.cpp`
  - 新增默认 `V` 的编译断言：`7-34`、`115-134`
- `test/simd/configure_probes/range_load_store.cpp`
  - 改为直接探测 `partial_load(range)` / `unchecked_load(range)`：`6-16`
- `test/simd/configure_probes/range_gather_scatter.cpp`
  - 改为直接探测 `partial_gather_from(range, indices)` / `unchecked_gather_from(range, mask, indices)`：`6-22`
- `test/simd/runtime/test_simd_memory.cpp`
  - 新增 range gather 默认返回类型断言与运行覆盖：`278-298`

### I-2 masked gather/scatter 旧顺序仍公开暴露

裁决：**已修复。**

本轮把 masked gather/scatter 统一改为当前草案要求的 `mask, indices` 顺序，并同步修正 range 转发：

- `partial_gather_from`
- `unchecked_gather_from`
- `partial_scatter_to`
- `unchecked_scatter_to`

对应实现集中在：

- `backport/cpp26/simd/memory.hpp`
  - gather：`748-882`
  - scatter：`884-1013`

测试补完：

- `test/simd/compile/test_simd_api_memory.cpp`
  - 改为正向断言标准顺序：`59-110`
- `test/simd/runtime/test_simd_memory.cpp`
  - 运行时改为标准顺序，并新增 unchecked masked gather/scatter 覆盖：`266-324`、`527-544`
- `test/simd/configure_probes/reject_legacy_masked_gather_scatter_order.cpp`
  - 新增负向 probe，确保旧的 `indices, mask` 顺序不能编译：`1-26`
- `test/simd/CMakeLists.txt`
  - 注册新的负向 probe：`118-127`

---

## 验证

本轮使用本地隔离 worktree 构建验证；`gtest` 通过未纳管的本地 `3rdparty` 链接引入，仅用于构建，不纳入提交。

已执行：

```bash
cmake -S . -B build-simd-r7-make2 -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=/usr/bin/c++
cmake --build build-simd-r7-make2 -j 4 --target \
  test_simd_api_core test_simd_api_memory test_simd_memory \
  test_simd_core test_simd_mask test_simd_operators test_simd_math \
  test_simd_bit test_simd_math_ext test_simd_math_special test_simd_complex \
  test_simd_reductions test_simd_expansion test_simd_abi \
  simd_example simd_complete_example
ctest --test-dir build-simd-r7-make2 -R simd --output-on-failure
```

结果：

- `simd` 子集 `29/29` 全部通过

---

## 结论

`p1-v0-r7` 提出的两组问题，本轮已经完成实现修复、测试补完和本地完整 `simd` 子集回归：

1. 默认 `V` 入口已恢复为标准可达
2. masked gather/scatter 参数顺序已恢复为标准顺序
3. 旧顺序现在由负向 probe 明确拒绝

因此，`p1-v0-r7` 所列问题在当前工作树上已收口。

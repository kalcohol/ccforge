# `std::simd` 审查答复 (p1-v0-r6-a0)

审查基线：当前 C++ 工作草案 `[simd]`（https://eel.is/c++draft/simd）

应答对象：

- `review/02-std-simd-p1-v0-r6.md`

答复口径：

- 继续以当前工作草案 `[simd]` 为唯一规范基线
- 本文件只记录本轮已经落地并验证过的修正
- 不回写或改写已有答复时间线

---

## 总结

`p1-v0-r6` 指出的三组问题，本轮已全部修复并补上对应测试资产：

1. `[simd.flags.oper]` `flags` 的 `operator|` 已改为 `consteval`
2. `[simd.iterator]` `simd_iterator` 已补齐标准要求的 `operator<=>`，并移除旧的手写顺序比较族
3. `[simd.mask.overview]` `basic_mask(bitset)` 构造函数已改为 `constexpr`

同时，本轮没有把新增断言继续堆回单一 `core` 文件，而是把 iterator 合同与 mask/bitset constexpr 合同拆成独立 compile probe 文件，避免测试继续膨胀。

本轮未触碰 `std::execution` 实现文件；全部修复都在独立 `simd` worktree / 本地修复分支中完成。

---

## 逐条答复

### I-1 `[simd.flags.oper]` `flags` 的 `operator|` 仍错误地允许运行期求值

裁决：**已修复。**

本轮已将：

```cpp
constexpr flags<...> operator|(flags<...>, flags<...>) noexcept
```

改为：

```cpp
consteval flags<...> operator|(flags<...>, flags<...>) noexcept
```

实现位置：

- `backport/cpp26/simd/base.hpp`

测试收口：

- `test/simd/compile/test_simd_api_core.cpp`
  - 新增正向立即上下文断言，锁定 `flag_default | flag_convert` 的结果类型
- `test/simd/configure_probes/flags_rejects_address.cpp`
  - 新增负向 probe，验证不能获取 `std::simd::operator|` 的函数地址，从而锁定其“立即函数”属性

这里本轮顺手澄清了一个实现/测试细节：

- 直接用“运行期 flags 值参与组合应失败”做负向 probe 并不稳定，因为 `flags` 是空类型，编译器可能把结果纯按类型级立即求值
- 因此最终采用“禁止取立即函数地址”的负向 probe 来锁定 `consteval` 属性，这比值级探针更严格也更稳定

### I-2 `[simd.iterator]` `simd_iterator` 仍缺少 `operator<=>`

裁决：**已修复。**

本轮在：

- `backport/cpp26/simd/iterator.hpp`

中完成以下收口：

1. `operator==` 改为默认比较
2. 新增默认化的 `operator<=>`
3. 移除旧的手写 `!=` / `<` / `<=` / `>` / `>=`

这样 `simd_iterator` 的比较接口不再停留在自定义的 `index_`-only 逻辑，而是按标准要求的比较形状建模。

测试收口：

- 新增 `test/simd/compile/test_simd_api_iterator.cpp`
  - 检查 `iterator <=> iterator` 存在
  - 检查返回类型为 `std::strong_ordering`
  - 检查 `operator<=>` 可在常量求值上下文使用
  - 保留 iterator 与 sentinel 的现有差值/返回类型合同
- 原先放在 `test_simd_api_core.cpp` 的 iterator 相关断言已拆出，避免继续堆积

### I-3 `[simd.mask.overview]` `basic_mask(bitset)` 构造函数仍不是 `constexpr`

裁决：**已修复。**

本轮已将：

- `backport/cpp26/simd/types.hpp`

中的 `basic_mask(const bitset<N>&)` 提升为 `constexpr`。

这使得 `basic_mask` 与 `bitset` 之间的编译期 round-trip 现在真正闭环：

1. `basic_mask -> bitset`
2. `bitset -> basic_mask`
3. `bitset -> basic_mask -> bitset`

测试收口：

- 新增 `test/simd/compile/test_simd_api_mask.cpp`
  - 锁定 `constexpr basic_mask(bitset)`
  - 锁定 `constexpr to_bitset()`
  - 锁定 `constexpr` round-trip 一致性
  - 同时承接原先散落在 `core` 中的 mask 类型/转换/一元与比较断言
- `test_simd_api_core.cpp` 中相应 mask 断言已拆出，避免继续膨胀

---

## 测试与验证

本轮新增/调整的测试资产：

1. 新增 compile probe：
   - `test/simd/compile/test_simd_api_iterator.cpp`
   - `test/simd/compile/test_simd_api_mask.cpp`
2. 新增 configure probe：
   - `test/simd/configure_probes/flags_rejects_address.cpp`
3. 调整测试注册：
   - `test/simd/CMakeLists.txt`
4. 对 `test/simd/compile/test_simd_api_core.cpp` 做适度瘦身，把 iterator / mask 相关断言拆出

本轮已通过以下验证：

- 本地 GCC：
  - `cmake -S . -B build-r6-gcc -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON`
  - `cmake --build build-r6-gcc -j`
  - `ctest --test-dir build-r6-gcc -R simd --output-on-failure`
- 本地 Zig：
  - `CC='zig cc' CXX='zig c++' cmake -S . -B build-r6-zig -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON`
  - `cmake --build build-r6-zig -j`
  - `ctest --test-dir build-r6-zig -R simd --output-on-failure`
- Podman clang：
  - `cmake -S . -B build-r6-podman-clang -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON`
  - `cmake --build build-r6-podman-clang -j`
  - `ctest --test-dir build-r6-podman-clang -R simd --output-on-failure`
- Podman gcc：
  - `cmake -S . -B /tmp/build-r6-gcc -G Ninja -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON`
  - `cmake --build /tmp/build-r6-gcc -j`
  - `ctest --test-dir /tmp/build-r6-gcc -R simd --output-on-failure`

验证结果：

- 四套环境下 `simd` 子集均为 `29/29` 通过

---

## 结论

`p1-v0-r6` 指出的三组问题，本轮已经完成实现修复、测试补完与跨工具链验证：

1. `flags` 的 `operator|` 已恢复为标准要求的 `consteval` 契约
2. `simd_iterator` 已提供标准要求的 `operator<=>`
3. `basic_mask(bitset)` 已恢复为 `constexpr` 构造

因此，`p1-v0-r6` 所列问题在当前工作树上已收口。

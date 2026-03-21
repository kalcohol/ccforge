# `std::simd` 审查答复 (p1-v0-r5-a0)

审查基线：当前 C++ 工作草案 `[simd]`（https://eel.is/c++draft/simd）

应答对象：

- `review/02-std-simd-p1-v0-r5.md`

答复口径：

- 继续以当前工作草案 `[simd]` 为唯一规范基线
- 本文件只记录本轮已经落地并验证过的修正
- 不回写或改写已有答复时间线

---

## 总结

`p1-v0-r5` 提出的两组问题，本轮都已完成修复并补上长期测试约束：

1. `[simd.math]` special math family 不再依赖宿主 `__cpp_lib_math_special_functions` 才公开接口；现在无论宿主标量库是否自带 special math，`std::simd` 公开表面都会稳定存在
2. `[simd.mask.mem]` `basic_mask::to_bitset()` 现在无条件保持 `constexpr`，并补上编译期测试把该契约锁住

另外，在补 special math 标量后备实现并跑矩阵验证时，还顺手发现并修正了一处真实实现错误：`assoc_legendre` / `sph_legendre` 的相位处理原先不正确，现已一并收口。

本轮未触碰 `std::execution` 实现文件；`backport/cpp26/execution*` 与 `test/execution/runtime/*` 保持不变。

---

## 逐条答复

### I-1 `[simd.math]` special math family 仍是宿主条件公开接口

裁决：**已修复。**

本轮调整后：

1. `backport/cpp26/simd/operations_math_special.hpp`
   - 去掉整头 `#ifdef __cpp_lib_math_special_functions` 门控
   - 所有逐 lane special math 调用统一改为转发到 `detail::special_math::*`
2. 新增 `backport/cpp26/simd/operations_math_special_scalar.hpp`
   - 为 `beta`、`expint`、`riemann_zeta`、`comp_ellint_*`、`cyl_bessel_*`、`ellint_*`、`hermite`、`laguerre`、`legendre`、`assoc_*`、`sph_*` 提供标量后备路径
   - 宿主支持 `__cpp_lib_math_special_functions` 时，仍优先转发到宿主 `std::*`
   - 宿主不支持时，改走 backport 内部标量后备实现，不再让名字直接消失
3. `test/simd/CMakeLists.txt`
   - 移除基于宿主 special math 能力的测试注册门控
   - 删除 `test/simd/configure_probes/scalar_math_special.cpp`
   - `math_special_surface`、`test_simd_api_math_special`、`test_simd_math_special` 现均始终注册
4. `test/simd/runtime/test_simd_math_special.cpp`
   - 宿主支持 special math 时继续逐 lane 对比 `std::*`
   - 宿主不支持时，对比预计算常量，避免测试再次被“静默跳过”

因此，`std::simd::beta`、`std::simd::expint`、`std::simd::riemann_zeta` 等 special math 名字现在不再因宿主标量库缺口而从公开表面消失。

补充说明：

- 在后备实现验证过程中，发现 `assoc_legendre_fallback` 错把 Condon-Shortley phase 放进了关联勒让德多项式本体，导致 `sph_legendre_fallback` 再乘一次相位时符号错误
- 现已改为：
  - `assoc_legendre_fallback` 不再内含该相位
  - `sph_legendre_fallback` 在球谐归一化层显式施加相位

### I-2 `[simd.mask.mem]` `basic_mask::to_bitset()` 的 `constexpr` 契约仍然依赖宿主库宏

裁决：**已修复。**

本轮调整后：

1. `backport/cpp26/simd/types.hpp`
   - `basic_mask::to_bitset()` 现无条件声明为 `constexpr`
   - 对 `size <= 64` 的 mask，优先走 `std::bitset<N>(to_ullong())` 路径，以兼容老版本 libstdc++/clang 环境中 `bitset::set` 仍非 `constexpr` 的现实
   - 对更宽的 mask，仍保留逐 bit 填充路径
2. `test/simd/compile/test_simd_api_core.cpp`
   - 新增 `static_assert`，直接锁定 `basic_mask::to_bitset()` 的常量求值能力

这样处理后，`to_bitset()` 的 `constexpr` 公开契约不再受宿主 `__cpp_lib_constexpr_bitset` 是否定义所摆布。

---

## 验证

本轮修复完成后，已通过以下矩阵验证：

- `build-p1`：35/35 通过
- `build-gcc`：37/37 通过
- `build-zig`：37/37 通过
- `build-podman-clang`：37/37 通过
- Podman GCC `/tmp/build-gcc`：37/37 通过

验证结果说明两点：

1. special math 测试不再因宿主能力不足而被跳过，Zig/libc++ 现在也会真正编译并运行这组 `simd` special math 覆盖
2. `basic_mask::to_bitset()` 的 `constexpr` 路径已经兼容旧 libstdc++12 环境，不再因 `bitset::set` 非 `constexpr` 而掉回运行期-only

---

## 结论

`p1-v0-r5` 指出的两组问题，本轮已经完成实现修复、测试补完和跨工具链验证：

1. `[simd.math]` special math family 已从“宿主条件公开接口”改为由 backport 稳定提供的标准公开表面
2. `[simd.mask.mem]` `basic_mask::to_bitset()` 已恢复为稳定的 `constexpr` 契约，并由编译期测试锁住

因此，`p1-v0-r5` 所列问题在当前工作树上已收口。

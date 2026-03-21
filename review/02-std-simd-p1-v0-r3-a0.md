# `std::simd` 审查答复 (p1-v0-r3-a0)

审查基线：当前 C++ 工作草案 `[simd]`（https://eel.is/c++draft/simd）

应答对象：

- `review/02-std-simd-p1-v0-r3.md`

答复口径：

- 继续以当前工作草案 `[simd]` 为唯一规范基线
- 本文件只记录本轮已经落地并验证过的修正
- 不回写或改写已有 `a0/a1/...` 时间线

---

## 总结

`p1-v0-r3` 指出的三组问题，本轮已经完成对应修复，并把测试与源码结构一起收口：

1. `[simd.bit]` 的 unsigned 约束、计数类返回类型、mixed shift-vector 支持已按当前草案修正
2. `frexp` / `modf` / `remquo` 已改为当前草案要求的 out-parameter 公开签名，旧的 pair-return 形式已移除并加负向探针锁住
3. `[simd.math]` 的核心缺口已补面，special math family 也已补齐到当前实现范围，并补上对应 compile probe / configure probe / runtime tests

同时，本轮顺手处理了两个结构性问题：

- 数学实现继续从单头拆分为 `common` / `basic` / `transcendental` / `special`
- `simd` 测试重组为 `test/simd/{support,runtime,compile,configure_probes}`，`test/CMakeLists.txt` 也改成按功能注册的过程函数

另有一项新增发现并已收口：宿主标量库若缺少 `__cpp_lib_math_special_functions`（本轮复现于 Zig/libc++），现在不会再导致整个 `<simd>` 头因 special math 名字查找而失效；非 special `[simd.math]` 面保持可用，special math 子面与其测试改为按宿主能力启用。

---

## 逐条答复

### I-1 `[simd.bit]` 的约束与返回类型仍不符合当前草案

裁决：**已修复。**

本轮已按当前草案收口：

1. unsigned-only 约束
   - `has_single_bit`
   - `bit_floor`
   - `bit_ceil`
   - `popcount`
   - `countl_zero` / `countl_one`
   - `countr_zero` / `countr_one`
   - `bit_width`
2. 计数类返回类型
   - `popcount` / `countl_*` / `countr_*` / `bit_width`
   - 现在返回 `rebind_t<make_signed_t<typename V::value_type>, V>`
   - 已覆盖 `uint4` 与 `uchar4` 这类窄 unsigned lane 情形
3. `rotl` / `rotr` 的公开契约
   - 标量 shift 形参改为 `int`
   - 向量 shift 版本允许“同 lane 数、同 `sizeof`、右侧为 integral lane 类型”的 mixed shift-vector 形式

测试方面：

- `test/simd/runtime/test_simd_bit.cpp`
- `test/simd/compile/test_simd_api_math.cpp`
- `test/simd/configure_probes/bit_surface.cpp`
- `test/simd/configure_probes/bit_rejects_signed.cpp`

已经把返回类型、正向 surface 和 signed 负向约束一起锁住。

### I-2 `frexp` / `modf` / `remquo` 的公开签名仍然不是当前草案要求的形式

裁决：**已修复。**

本轮已将这三组接口改为当前草案要求的 out-parameter 公开签名：

- `frexp(const V&, rebind_t<int, V>*)`
- `modf(const basic_vec<T, Abi>&, basic_vec<T, Abi>*)`
- `remquo(const A&, const B&, rebind_t<int, result_t>*)`

同时同步做了两件关键收口：

1. 所有原先把 pair-return 错签名当成“正确行为”的测试都已改写
2. 新增负向探针，明确禁止旧的 pair-return 形式继续暴露在公开表面

对应测试与探针：

- `test/simd/runtime/test_simd_math_ext.cpp`
- `test/simd/compile/test_simd_api_math.cpp`
- `test/simd/configure_probes/math_surface.cpp`
- `test/simd/configure_probes/math_rejects_pair_decompose.cpp`

因此，这条问题不再只是“实现改了”，而是旧错误契约也已经从测试里移除。

### I-3 `[simd.math]` 仍明显只是当前草案的一个子集，缺失函数族不少

裁决：**已修复到本轮实现范围，并补齐对应测试体系。**

本轮新增并覆盖的公开表面包括：

1. 基础/分类/比较
   - `fabs`
   - `fpclassify`
   - `isgreater`
   - `isgreaterequal`
   - `isless`
   - `islessequal`
   - `islessgreater`
   - `isunordered`
2. 常规数学
   - `tan`
   - `atan2`
   - `exp2`
   - `expm1`
   - `ilogb`
   - `ldexp`
   - `scalbn`
   - `scalbln`
   - 三参 `hypot`
   - `erf`
   - `erfc`
   - `lgamma`
   - `tgamma`
   - `lerp`
3. special math family
   - `comp_ellint_1/2/3`
   - `beta`
   - `cyl_bessel_i/j/k`
   - `cyl_neumann`
   - `ellint_1/2/3`
   - `expint`
   - `hermite`
   - `laguerre`
   - `legendre`
   - `riemann_zeta`
   - `sph_bessel`
   - `sph_legendre`
   - `sph_neumann`
   - `assoc_laguerre`
   - `assoc_legendre`

测试方面，本轮不再只靠零散 probe，而是成组补齐：

- `test/simd/runtime/test_simd_math_ext.cpp`
- `test/simd/runtime/test_simd_math_special.cpp`
- `test/simd/compile/test_simd_api_math.cpp`
- `test/simd/compile/test_simd_api_math_special.cpp`
- `test/simd/configure_probes/math_surface.cpp`
- `test/simd/configure_probes/math_special_surface.cpp`

### 新增收口：宿主标量 special math 能力边界

这不是 `r3` 原文中的问题点，但它是在本轮按仓库验证策略跑 Zig 时新暴露出来的真实缺陷。

问题现象：

- Zig/libc++ 的 `<cmath>` 缺少 `__cpp_lib_math_special_functions`
- 之前 `operations_math_special.hpp` 把普通超越函数与真正的 special math 混在一起，并在模板体中无条件写死 `std::comp_ellint_*` / `std::beta` / `std::laguerre` 等名字
- 结果是用户即使根本不调用这些 API，只要 `#include <simd>` 就会因名字查找失败而整头不可用

本轮的处理：

1. 数学实现进一步拆分
   - `operations_math_basic.hpp`
   - `operations_math_transcendental.hpp`
   - `operations_math_special.hpp`
2. 普通 `[simd.math]` 与真正的 C++17 special math 分离
   - `atan2` / `exp2` / `erf` / `lerp` 等不再被 special math 能力门控连带吞掉
3. 新增宿主标量 special math 探针
   - `test/simd/configure_probes/scalar_math_special.cpp`
4. `test/CMakeLists.txt` 改为按能力注册
   - 宿主支持 special math 时，注册 `math_special_surface`、`test_simd_api_math_special`、`test_simd_math_special`
   - 宿主不支持时，不再让 `<simd>` 头或无关测试整片失效

这项修正的目标不是“降低要求”，而是把宿主标量库缺口从“整头不可用”收缩为“special math 子面按宿主能力启用”，避免把无关 `[simd]` 面一起拖垮。

---

## 结构性改进

除直接修复 review 问题外，本轮还完成了计划中的结构整理。

源码侧：

- `operations_math.hpp` 改为统一入口头
- 具体实现拆到：
  - `operations_math_common.hpp`
  - `operations_math_basic.hpp`
  - `operations_math_transcendental.hpp`
  - `operations_math_special.hpp`

测试侧：

- 公共支持放入 `test/simd/support/`
- 运行时测试放入 `test/simd/runtime/`
- 编译期 API probe 放入 `test/simd/compile/`
- configure probe 放入 `test/simd/configure_probes/`
- `test/CMakeLists.txt` 增加：
  - `forge_add_simd_runtime_test`
  - `forge_add_simd_compile_probe`
  - `forge_add_simd_configure_probe`
  - `forge_register_simd_runtime_tests`
  - `forge_register_simd_compile_probes`
  - `forge_register_simd_configure_probes`

这使得后续继续扩展 `simd` surface 时，不必再把实现和测试继续堆回单一大文件。

---

## 验证

本轮已完成并通过：

- `cmake -S . -B build-p1 && cmake --build build-p1 -j && ctest --test-dir build-p1 --output-on-failure`
- `cmake -S . -B build-gcc -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON && cmake --build build-gcc -j && ctest --test-dir build-gcc --output-on-failure`
- `CC='zig cc' CXX='zig c++' cmake -S . -B build-zig -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON && cmake --build build-zig -j && ctest --test-dir build-zig --output-on-failure`
- `podman run --rm --userns=keep-id -v "$PWD:/work:Z" -w /work docker.io/silkeh/clang:latest ...`
- `podman run --rm --userns=keep-id --user 0 -v "$PWD:/work:ro,Z" -w /work docker.io/gcc:latest ...`

验证结果：

- `build-p1`：35/35 通过
- `build-gcc`：37/37 通过
- `build-zig`：35/35 通过
- `build-podman-clang`：37/37 通过
- Podman GCC `/tmp/build-gcc`：37/37 通过

说明：

- configure 阶段若某些负向探针显示 `Failed`，这是“按预期编译失败”，属于成功而不是回归
- Zig/libc++ 因宿主标量库缺少 `__cpp_lib_math_special_functions`，special math 相关测试不会被注册；普通 `[simd.math]`、bit、complex 等其余面仍已完整通过

结论：`p1-v0-r3` 指出的三组问题已在本轮完成实现修正、测试补完和多工具链验证；同时，新增暴露的宿主 special math 兼容问题也已被结构化收口，不再影响整个 `<simd>` 头和无关测试路径。

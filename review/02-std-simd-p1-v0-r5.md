# `std::simd` 独立审查报告 (p1-v0-r5)

审查对象：

- 实现文件：`backport/cpp26/simd.hpp`、`backport/cpp26/simd/*.hpp`
- 测试文件：`test/simd/**/*.cpp`、`test/simd/CMakeLists.txt`、`test/CMakeLists.txt`

审查基线：

- 当前工作树（包含本轮测试 CMake 子目录化调整，未特别复用既有 review 结论）
- 当前 C++ 工作草案 `[simd]`：`https://eel.is/c++draft/simd`
- 重点复核条文：
  - `[simd.math]`
  - `[simd.mask.mem]`

---

## 执行摘要

本轮在当前工作草案 `[simd]` 基线下重新独立核查，并结合本地 GCC、本地 Zig，以及 podman clang / gcc 四套环境复核后，可以确认当前 `std::simd` 仍至少还有两组不能放过的 P1 问题：

1. `[simd.math]` special math family 仍然是“依赖宿主标量库能力的条件公开接口”，在 Zig/libc++ 环境里整组标准名字会直接消失，而测试目标也会同步不注册
2. `[simd.mask.mem]` `basic_mask::to_bitset()` 仍然按 `__cpp_lib_constexpr_bitset` 条件降级为非 `constexpr`，其公开契约继续随宿主标准库漂移，而现有测试只检查返回类型，没有锁住 `constexpr` 要求

这两组问题有一个共同特征：**当前矩阵测试仍可全部通过，但通过的原因不是契约已经对齐标准，而是测试在关键点上仍然允许宿主能力把标准公开表面“裁掉”或“降级”**。

---

## 主要问题

### I-1 `[simd.math]` special math family 仍是宿主条件公开接口

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/operations_math_special.hpp:1-141`
  - `test/simd/CMakeLists.txt:8-10`
  - `test/simd/CMakeLists.txt:142-149`
  - `test/simd/CMakeLists.txt:195-196`
  - `test/simd/CMakeLists.txt:211-213`
- 相关条文：
  - 当前工作草案 `[simd.math]`
  - 其中 `beta`、`expint`、`riemann_zeta`、`comp_ellint_*`、`cyl_bessel_*`、`ellint_*`、`hermite`、`laguerre`、`legendre`、`assoc_*`、`sph_*`

**问题描述：**

当前整组 `std::simd` special math 仍包在：

```cpp
#ifdef __cpp_lib_math_special_functions
...
#endif
```

也就是只有宿主标量库先暴露 `__cpp_lib_math_special_functions` 时，这组 `std::simd` 名字才进入公开表面。

测试侧也仍按同一条件同步跳过：

1. `test/simd/CMakeLists.txt:8-10` 先用 `scalar_math_special.cpp` 探测宿主标量 special math
2. 如果探测失败：
   - `test/simd/CMakeLists.txt:142-149` 不再注册 `math_special_surface.cpp`
   - `test/simd/CMakeLists.txt:195-196` 不再注册 `test_simd_math_special.cpp`
   - `test/simd/CMakeLists.txt:211-213` 不再注册 `test_simd_api_math_special.cpp`

因此当前不是“接口还在，但实现细节稍有偏差”，而是**整组标准公开接口会在部分宿主上直接消失，同时测试也一起静默跳过**。

本轮直接复现：

```cpp
#include <simd>
using float4 = std::simd::vec<float, 4>;
auto value = std::simd::beta(float4(0.25f), 0.5f);
```

在本地 Zig/libc++ 下直接报错：

```text
error: no member named 'beta' in namespace 'std::simd'
```

同时，`build-zig` 重新配置时仍会明确打印：

```text
Skipping simd special math configure probes: host scalar library lacks C++ special math support
```

这说明当前 special math 问题在本轮结束时仍然原样成立。

**影响：**

- `std::simd` special math public surface 不是稳定契约，而是随宿主库能力漂移
- Zig/libc++ 这类环境里，标准要求存在的 `std::simd::beta` / `std::simd::expint` / `std::simd::riemann_zeta` 等接口会直接消失
- 当前测试矩阵之所以仍然全绿，是因为目标根本没有被注册，不是因为实现已经满足标准
- 这会让下游代码出现“GCC/libstdc++ 可编译、Zig/libc++ 直接找不到名字”的公开表面断裂

---

### I-2 `[simd.mask.mem]` `basic_mask::to_bitset()` 的 `constexpr` 契约仍然依赖宿主库宏

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/types.hpp:92-108`
  - `test/simd/compile/test_simd_api_core.cpp:113-116`
- 相关条文：
  - 当前工作草案 `[simd.mask.mem]`
  - `constexpr bitset<size> to_bitset() const noexcept;`

**问题描述：**

当前实现仍将 `basic_mask::to_bitset()` 写成：

```cpp
#if defined(__cpp_lib_constexpr_bitset) && __cpp_lib_constexpr_bitset >= 202207L
    constexpr bitset<...> to_bitset() const noexcept;
#else
    bitset<...> to_bitset() const noexcept;
#endif
```

也就是说，`to_bitset()` 是否为 `constexpr` 仍然不是 backport 自身提供的稳定契约，而是继续依赖宿主 `std::bitset` 宏。

但按当前工作草案 `[simd.mask.mem]`，`basic_mask::to_bitset()` 是 `std::simd` 公开表面的一部分，其 `constexpr` 性质不应随着宿主库特性宏漂移。

本轮复核中：

- 本地 GCC 13 预定义宏中没有 `__cpp_lib_constexpr_bitset`
- 本地 Zig/libc++ 预定义宏中也没有 `__cpp_lib_constexpr_bitset`

因此当前实现在这两套本地环境里都会落到非 `constexpr` 分支。下面的最小探针在本地 GCC 上可直接失败：

```cpp
#include <simd>
using mask4 = std::simd::mask<int, 4>;
constexpr auto bits = [] {
    constexpr mask4 m(true);
    return m.to_bitset();
}();
```

报错核心是：

```text
error: call to non-‘constexpr’ function ‘...::to_bitset() const’
```

与此同时，当前测试只在 `test/simd/compile/test_simd_api_core.cpp:113-116` 检查：

- `to_bitset()` 返回 `std::bitset<4>`
- `to_ullong()` 返回 `unsigned long long`

并没有任何测试去锁定 `to_bitset()` 的 `constexpr` 要求。

**影响：**

- `basic_mask::to_bitset()` 的公开契约继续依赖宿主 `std::bitset` 宏，而不是由 backport 自身稳定提供
- 依赖编译期 `bitset` 转换的代码在部分工具链上会直接退化为运行期-only 行为
- 当前测试矩阵同样会全绿，因为它只检查返回类型，没有检查标准要求的 `constexpr` 表面
- 这属于与 I-1 同类的“标准公开契约被宿主能力静默降级，但现有测试没有把它钉住”的问题

---

## 结论

按当前工作草案 `[simd]`、当前工作树，以及本轮 GCC / Zig / podman clang / podman gcc 四套环境复核，当前 `std::simd` 至少仍有以下两组 P1 问题未收口：

1. `[simd.math]` special math family 仍是宿主条件公开接口；在 Zig/libc++ 下可直接复现名字缺失，测试目标也会同步不注册
2. `[simd.mask.mem]` `basic_mask::to_bitset()` 的 `constexpr` 契约仍按 `__cpp_lib_constexpr_bitset` 漂移；本地 GCC / Zig 均可复现常量求值失败，而现有测试没有覆盖这一点

这说明当前剩余问题已经不主要是“某个单独实现分支写错”，而是**backport 是否真正独立于宿主库能力、向下游暴露稳定标准契约**。如果下一轮要继续收口，至少需要同时处理两件事：

- 对仍然依赖宿主宏的公开表面，给出真正稳定的 backport 级提供方式，或正式收缩支持矩阵并把限制上升为仓库级明确约束
- 对所有这类“需要临时探针才能确认”的点，补成长期测试资产，而不是继续让矩阵全绿掩盖标准契约漂移

在当前状态下，这两组问题仍然成立。

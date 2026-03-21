# `std::simd` 独立审查报告 (p1-v0-r4)

审查对象：

- 实现文件：`backport/cpp26/simd.hpp`、`backport/cpp26/simd/*.hpp`
- 测试文件：`test/simd/**/*.cpp`、`test/CMakeLists.txt`

审查基线：

- 当前工作树，基于提交 `bf9314b`（`simd: fix r3 bit and math conformance gaps`）继续核查
- 当前 C++ 工作草案 `[simd]`：`https://eel.is/c++draft/simd`
- 本文不特别复用既有 review 结论，按当前工作树重新独立核查

---

## 执行摘要

在 `r3` 收口后，当前 `std::simd` 的 bit、基础 math、complex 与测试组织已经明显比上一轮完整；本轮继续按当前工作草案 `[simd]` 和多工具链实际构建复核后，仍可确认至少还有一组不能放过的 P1 问题：

1. `[simd.math]` special math public surface 仍被错误地做成“依赖宿主标量库能力的条件接口”，在 Zig/libc++ 这类环境里会直接从 `std::simd` 公开表面消失，而测试还会同步跳过，导致缺口被掩盖

这说明当前实现已经不再主要卡在“某个单独签名写错”，而是进入了“某一整组标准接口是否在所有受支持宿主上真正构成稳定公开契约”的阶段。

---

## 主要问题

### I-1 `[simd.math]` special math surface 仍被错误做成条件公开接口

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/operations_math_special.hpp:1-141`
  - `test/CMakeLists.txt:66-68`
  - `test/CMakeLists.txt:199-207`
  - `test/simd/configure_probes/scalar_math_special.cpp:1-38`
- 相关条文：
  - 当前工作草案 `[simd.math]`
  - 其中 `comp_ellint_*`、`beta`、`cyl_bessel_*`、`ellint_*`、`expint`、`hermite`、`laguerre`、`legendre`、`riemann_zeta`、`sph_*`、`assoc_*`

**问题描述：**

当前实现把整组 `std::simd` special math 算法放在：

```cpp
#ifdef __cpp_lib_math_special_functions
...
#endif
```

也就是只有当宿主标量库先暴露 `__cpp_lib_math_special_functions` 时，这组 `std::simd` 接口才会进入公开表面。

与此同时，测试体系也按同一条件同步跳过：

1. configure-time 先跑 `scalar_math_special.cpp`
2. 如果宿主不支持标量 special math：
   - `math_special_surface.cpp` 不再注册
   - `test_simd_api_math_special.cpp` 不再注册
   - `test_simd_math_special.cpp` 不再注册

这意味着当前实现不是“有接口但行为差一点”，而是**整组标准接口会在部分受支持工具链上直接从 `std::simd` 公开表面消失**，同时测试也不会再报错。

本轮在 Zig/libc++ 下直接复现：

```cpp
using float4 = std::simd::vec<float, 4>;
auto value = std::simd::beta(float4(0.25f), 0.5f);
```

当前直接报错：

```text
error: no member named 'beta' in namespace 'std::simd'
```

而按当前工作草案 `[simd.math]`，这些 special math 名字属于 `<simd>` 的标准公开表面，不应因为宿主 `<cmath>` 缺某个 feature-test macro 就整体失踪。

**影响：**

- 使用 Zig/libc++ 这类宿主时，标准要求存在的 `std::simd` special math 接口会直接缺失
- 当前测试不会揭示这个缺口，反而会因为同条件跳过而掩盖问题
- 这破坏了本仓库最核心的 backport 目标：对下游暴露稳定、透明、与当前标准一致的公开契约
- 用户代码在 GCC/libstdc++ 上能写、在 Zig/libc++ 上却因 `std::simd::beta` / `std::simd::expint` 等不存在而无法编译，这不是轻微实现差异，而是公开表面不完整

---

## 结论

按当前工作草案 `[simd]` 和本轮多工具链复核，当前 `std::simd` 至少仍有一组 P1 问题没有真正收口：

1. `[simd.math]` special math family 仍是“宿主能力条件公开接口”，而不是稳定的标准公开表面；Zig/libc++ 下可直接复现缺失，测试还会同步跳过并掩盖该缺口

因此，下一轮如果要继续收口，不应只停留在“再补几个 probe”，而应至少做出其中一种明确选择并落实：

- 为缺失 special math 的宿主提供 backport 级标量实现或等价支撑，使 `std::simd` special math surface 始终存在
- 或者正式收缩支持矩阵，并把该限制写成仓库级明确约束，而不是让公开接口按工具链静默漂移

在当前状态下，这组问题仍然成立。

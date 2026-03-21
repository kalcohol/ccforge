# `std::simd` 审查答复 (p1-v0-r2-a0)

审查基线：当前 C++ 工作草案 `[simd]`（https://eel.is/c++draft/simd）

应答对象：

- `review/02-std-simd-p1-v0-r2.md`

答复口径：

- 继续以当前工作草案 `[simd]` 为唯一规范基线
- 本文件只记录本轮已经落地并验证过的应答
- 不覆盖或改写先前 `a0/a1/...` 时间线

---

## 总结

`p1-v0-r2` 的四项问题，本轮已全部完成收口：

1. `simd-complex` 路径已真正打开，不再停留在“未来再补”的状态
2. `p1-v0-r2` 点名的 bit / math / complex 算法缺口已补齐到当前审查范围
3. 已实现数学接口的参与条件和 overload set 已按当前草案收紧并扩展
4. 测试体系已补齐对复杂类型面、算法完整性和负向约束的系统锁定

此外，本轮顺手解决了实现膨胀问题：把 bit / math / complex 算法从单个 `operations.hpp` 中拆分出去，保持入口不变但降低单文件复杂度，也让测试按功能面拆分，避免继续向“大一统测试文件”回归。

---

## 逐条答复

### I-1 `simd-complex` 路径仍整体不可达

裁决：**已修复。**

本轮已从类型闸门、类型接口和算法入口三个层面同时打开 complex `simd`：

1. 类型闸门
   - `backport/cpp26/simd/base.hpp` 允许当前草案支持的 complex lane 类型进入 `basic_vec`
2. 类型接口
   - `backport/cpp26/simd/types.hpp` 为 complex `basic_vec` 提供 `real_type`
   - 新增 `(real, imag)` 构造
   - 新增成员 `real()` / `imag()` 读写接口
3. 算法入口
   - `backport/cpp26/simd/operations_complex.hpp` 提供 `real` / `imag` / `abs` / `arg` / `norm` / `conj` / `proj` / `polar` 以及 complex 数学函数

因此，`vec<complex<float>, 4>`、`real(values)`、`log10(values)`、complex `pow(values, values)` 均已可用，并有探针和运行时测试覆盖。

### I-2 标准算法表面仍明显不完整

裁决：**已修复到本轮审查范围。**

`p1-v0-r2` 点名的三组缺口，本轮已补齐核心公开表面：

1. bit family
   - `byteswap`
   - `popcount`
   - `countl_zero` / `countl_one`
   - `countr_zero` / `countr_one`
   - `bit_width`
   - `has_single_bit`
   - `bit_floor` / `bit_ceil`
   - `rotl` / `rotr`
2. math family
   - `log10` / `log1p` / `log2` / `logb`
   - `cbrt`
   - `hypot`
   - `fmod` / `remainder`
   - `copysign` / `nextafter` / `fdim`
   - `fmin` / `fmax` / `fma`
   - `isfinite` / `isinf` / `isnan` / `isnormal` / `signbit`
   - `frexp` / `remquo` / `modf`
   - `asin` / `acos` / `atan`
   - `sinh` / `cosh` / `tanh`
   - `asinh` / `acosh` / `atanh`
3. complex family
   - 已在 I-1 中展开，公开表面已可达并有测试

### I-3 已实现数学接口的约束与重载集合仍不符合当前草案

裁决：**已修复。**

本轮对这条问题做了两类收口。

第一类是参与条件收口：

- `sqrt`、`floor`、`ceil`、`round`、`trunc`
- `sin`、`cos`、`exp`、`log`
- 以及本轮新增的浮点数学函数

现在都只对 floating `simd` 开放，不再让 integral `vec` 错误进入候选集。

第二类是 overload set 补齐：

- `pow`
- `hypot`
- `fmin` / `fmax`
- `copysign`
- `nextafter`
- `fdim`
- `fmod` / `remainder`
- `fma`
- `remquo`

这些接口现在支持草案要求的 vec/scalar 混合参数形式，并基于公共原型和 `common_type` 结果类型构建返回值，而不是只接受“同类型 vec + vec”。

`abs` 也已按公开契约收口：

- 浮点 `simd` 版本与 signed integral `simd` 版本分开
- 不再把 unsigned `vec` 误放进候选集中导致模板体内硬错误
- complex `simd` 的 `abs` 返回 `real_type`，不再被旧的无约束版本错误截获

本轮新增的负向探针 `math_rejects_integral.cpp` 和 `test_simd_api_math.cpp` 已把这些约束锁住。

### I-4 现有测试仍未把复杂类型面、算法完整性和负向约束系统锁住

裁决：**已修复。**

本轮补了三层测试体系：

1. configure-time probes
   - `bit_surface.cpp`
   - `math_surface.cpp`
   - `math_rejects_integral.cpp`
   - `complex_surface.cpp`
2. compile probes
   - `test_simd_api_math.cpp`
   - `test_simd_api_complex.cpp`
3. runtime tests
   - `test_simd_bit.cpp`
   - `test_simd_math_ext.cpp`
   - `test_simd_complex.cpp`

覆盖重点包括：

- API 是否存在
- 返回类型是否正确
- complex `simd` 是否可实例化与访问
- vec/scalar 混合数学重载是否存在
- integral `vec` 是否会被正确拒绝在浮点数学接口之外

因此，本轮发现的问题已经从“人工探针确认”转为“测试常态锁定”。

---

## 结构性改进

除功能修复外，本轮还处理了实现与测试的膨胀问题。

实现侧：

- `operations.hpp` 保留统一入口
- bit、math、complex 算法拆分到独立头文件

测试侧：

- 按功能面拆成 `bit` / `math_ext` / `complex`
- compile probe 也拆成 `api_math` / `api_complex`
- configure probe 补到与标准表面一一对应

这样做的目的不是“整理风格”，而是降低继续遗漏标准表面的概率，让后续审查和回归更直接。

---

## 验证

本轮已完成：

- `cmake --build build-p1 -j`
- `ctest --test-dir build-p1 --output-on-failure`
- 本机 GCC 全量构建与测试
- 本机 Zig 全量构建与测试
- Podman Clang latest 全量构建与测试
- Podman GCC latest 全量构建与测试

验证结果：

- `build-p1`：33/33 通过
- `build-gcc`：35/35 通过
- `build-zig`：35/35 通过
- `build-podman-clang`：35/35 通过
- Podman GCC `/tmp/build-gcc`：35/35 通过

说明：

- configure 阶段的若干负向探针采用“应当编译失败”的断言方式，因此日志中对应 probe 显示为 `Failed` 属于预期成功，不是回归

结论：`p1-v0-r2` 所指出的问题已在本轮完成实现收口、测试补完和多工具链验证。

# `std::simd` 独立审查报告 (p1-v0-r2)

审查对象：
- 实现文件：`backport/cpp26/simd.hpp`、`backport/cpp26/simd/*.hpp`
- 测试文件：`test/test_simd*.cpp`、`test/simd_configure_probes/*.cpp`、`test/CMakeLists.txt`

审查基线：
- 当前工作树，基于提交 `bb56ae9`
- 当前 C++ 工作草案 `[simd]`：`https://eel.is/c++draft/simd`
- 本文不特别复用既有 review 结论，按当前工作树重新审视

---

## 执行摘要

按标准委员会和 STD 标准库的交付质量要求看，当前 `std::simd` 仍有四组不能继续视为“边角问题”的 P1 缺口：

1. `simd-complex` 入口仍被值类型闸门整体切断，complex `simd` 及其配套算法仍不可达
2. 标准算法表面仍是明显子集，bit / math / complex 三组接口成片缺失
3. 已实现的数学接口，其约束和重载集合仍未对齐当前草案；有的被错误放宽，有的则缺少草案要求的混合参数重载
4. 测试体系仍未把这些标准表面完整性与负向约束锁住，本轮仍需要额外探针才能确认缺口

其中，第 3 项是本轮独立核查新增确认的问题：当前不是只有“没补函数”，而是已有函数的参与条件和 overload set 也还没有收口到当前草案要求。

---

## 主要问题

### I-1 `simd-complex` 路径仍整体不可达，complex 类型面和 complex 算法面都没有真正打开

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/base.hpp:27-31`
  - `backport/cpp26/simd/types.hpp:304-309`
  - `backport/cpp26/simd/operations.hpp`
- 相关条文：
  - 当前工作草案 `[simd.general]`：vectorizable type 已包含 `complex<T>`
  - 当前工作草案 `[simd.expos]`：存在 `simd-complex` 概念
  - 当前工作草案 `[simd.complex.math]`

**问题描述：**

当前实现仍把值类型总闸门写成：

```cpp
template<class T>
struct is_supported_value
    : integral_constant<bool,
        (is_arithmetic<remove_cvref_t<T>>::value || is_extended_integer<remove_cvref_t<T>>::value) &&
        !is_same<remove_cvref_t<T>, bool>::value> {};
```

而 `basic_vec<T, Abi>` 继续在类模板层面对此做 `static_assert`。结果是：

- `std::simd::vec<std::complex<float>, 4>` 无法实例化
- 当前草案中的 `simd-complex` 路径没有真正打开
- `real` / `imag` / `conj` / `proj` / `arg` / complex `pow` 等 complex 算法没有落脚点

本轮探针可直接复现这一点：`std::simd::vec<std::complex<float>, 4>` 仍在 `types.hpp:309` 触发静态断言失败。

与此同时，当前实现中也完全找不到 `real`、`imag`、`conj`、`proj`、`arg`、`polar` 等 complex `simd` 入口。

**影响：**

- 当前 backport 仍不能承载当前草案的 complex `simd` 类型面
- complex 算法缺失不只是“函数还没补”，而是整条标准路径仍不可达
- 任何按当前草案使用 `simd-complex` 的代码都会在入口处失败

### I-2 标准算法表面仍明显不完整，bit / math / complex 三组接口成片缺失

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/operations.hpp`
  - `test/test_simd_math.cpp`
  - `test/simd_configure_probes/*.cpp`
- 相关条文：
  - 当前工作草案 `[simd.alg]`
  - 当前工作草案 `[simd.math]`
  - 当前工作草案 `[simd.complex.math]`

**问题描述：**

当前实现只有少量基础算法，例如 `abs`、`sqrt`、`floor`、`ceil`、`round`、`trunc`、`sin`、`cos`、`exp`、`log`、`pow`。但与当前草案相比，公开算法表面仍是明显子集。

本轮重新核查时，至少确认当前实现和测试中都没有下列现行接口的实现或覆盖痕迹：

- bit family：`byteswap`、`popcount`、`countl_zero`、`countl_one`、`countr_zero`、`countr_one`、`bit_width`、`has_single_bit`、`bit_floor`、`bit_ceil`、`rotl`、`rotr`
- math family：`log10`、`log1p`、`log2`、`logb`、`cbrt`、`hypot`、`fmod`、`remainder`、`copysign`、`nextafter`、`fdim`、`fmin`、`fmax`、`fma`、`isfinite`、`isinf`、`isnan`、`isnormal`、`signbit`、`frexp`、`remquo`、`modf`、`asin`、`acos`、`atan`、`sinh`、`cosh`、`tanh`、`asinh`、`acosh`、`atanh`
- complex family：`real`、`imag`、`abs`、`arg`、`norm`、`conj`、`proj`、`polar`、complex `exp/log/log10/sqrt/sin/cos/tan/...`

本轮探针中：

- `std::simd::log10(std::simd::vec<float, 4>{})` 直接报 “`log10` is not a member of `std::simd`”
- `std::simd::hypot(vec, 2.0f)`、`std::simd::fmin(vec, vec)`、`std::simd::copysign(vec, vec)`、`std::simd::isfinite(vec)` 均不存在

这说明当前 `<simd>` 公开算法面仍只覆盖了当前草案的一个子集，而不是“只差少量边缘补丁”。

**影响：**

- 用户一旦按当前草案调用这些算法，将直接遇到缺失而不是轻微行为差异
- review 很容易因为已有少数数学函数可用而高估当前完成度
- 后续即使只补测试，不先补齐公开算法表面，也无法达到标准库质量门槛

### I-3 已实现数学接口的约束与重载集合仍不符合当前草案

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/operations.hpp:193-321`
  - `test/test_simd_math.cpp:47-108`
- 相关条文：
  - 当前工作草案 `[simd.math]`

**问题描述：**

本轮独立核查确认，当前数学接口不只是“缺项”，还存在已实现接口的约束和重载集合不符合当前草案的问题。

#### 子项 A：浮点数学函数约束被错误放宽

当前实现把 `sqrt`、`floor`、`ceil`、`round`、`trunc`、`sin`、`cos`、`exp`、`log` 等全部写成了对任意 `basic_vec<T, Abi>` 的无约束模板。

但当前草案 `[simd.math]` 对这些接口要求的是 `math-floating-point V`，也就是公开表面只应向浮点 `simd` 打开。

本轮探针确认：

- `std::simd::sin(std::simd::vec<int, 4>{})` 当前可以通过编译
- `std::simd::sqrt(std::simd::vec<int, 4>{})` 当前可以通过编译
- `std::simd::floor(std::simd::vec<int, 4>{})` 当前可以通过编译

这不是实现细节差异，而是公开 API 参与条件被放宽了。

#### 子项 B：`abs` 约束没有按标准收口，导致错误类型进入候选集

当前草案对 `abs` 的公开表面区分：

- `signed integral` `basic_vec`
- `math-floating-point` `simd`

但当前实现把 `abs` 也写成了任意 `basic_vec<T, Abi>` 的无约束模板。结果是 `vec<unsigned, N>` 会错误进入候选集，最终在模板体内因为 `std::abs(unsigned)` 歧义而硬错误，而不是在接口层面被正确排除。

这说明当前实现没有把标准要求的负向约束体现在 overload participation 上。

#### 子项 C：已实现函数的重载集合仍是子集

当前草案对不少数学函数要求混合参数重载，例如：

- `pow(const V0&, const V1&) -> math-common-simd-t<V0, V1>`
- `hypot(const V0&, const V1&)`
- `fmin` / `fmax` / `copysign` / `nextafter` / `fdim` 等也是按 `math-common-simd-t` 建模

但当前实现里的 `pow` 只提供了：

```cpp
template<class T, class Abi>
constexpr basic_vec<T, Abi> pow(const basic_vec<T, Abi>&, const basic_vec<T, Abi>&);
```

也就是仅支持“同类型同 ABI 的 vec + vec”。本轮探针中：

- `std::simd::pow(vec<float, 4>{}, 2.0f)` 失配

这说明就算是已经实现的 `pow`，其公开 overload set 也仍然只是当前草案要求的子集。

**影响：**

- 用户代码可能在本不该可见的整型数学接口上静默通过编译，得到错误的公开契约
- 用户也可能在草案要求应当支持的混合参数场景中直接失配
- 这类问题不会表现成“函数缺失”，而是更难被注意到的 API 参与条件错误

### I-4 现有测试仍未把复杂类型面、算法完整性和负向约束系统锁住

- 严重程度：中
- 位置：
  - `test/CMakeLists.txt:42-138`
  - `test/CMakeLists.txt:202-214`
  - `test/test_simd_math.cpp:47-108`
- 相关条文：
  - 当前工作草案 `[simd]`

**问题描述：**

当前测试结构虽然已经比早期规整，但仍没有把本轮发现的标准表面问题系统锁住。

本轮可见：

1. configure-time probes 只覆盖了 `where`、转换、load/store、range gather/scatter、compress/expand 等修复链；没有 complex、bit family、math family 的表面探针
2. compile probe 列表里没有独立的 `test_simd_api_math.cpp` / `test_simd_api_complex.cpp`
3. `test/test_simd_math.cpp` 只有少量 float 正向运行时用例，没有：
   - 缺失算法的 API 存在性检查
   - 整型误用 `sin/sqrt/floor` 等的负向约束检查
   - `pow(vec, scalar)`、`fmin`、`copysign`、`isfinite`、`frexp`、`remquo`、`modf` 等标准表面的检查
   - complex `simd` 的类型与算法检查

换句话说，本轮之所以仍需要临时探针确认 `sin(int4)` 会错误通过、`pow(vec, scalar)` 会错误失配，本身就说明测试口径还没有覆盖“标准要求必须锁住的公开契约”。

**影响：**

- 标准表面缺失和约束错误都可以长期潜伏
- 后续修复如果只补实现、不补 compile-time 负向测试，很容易再次出现“人工探针才发现”的问题
- 这会持续拉低 review 的确定性和回归防护强度

---

## 结论

当前 `std::simd` 的剩余问题，不只是“还有一些函数没补”，而是仍同时存在：

1. complex 类型面没有打开
2. bit / math / complex 算法面仍明显不完整
3. 已实现数学接口的参与条件和 overload set 仍未收口到当前草案
4. 测试尚未把这些标准表面和负向约束系统锁住

因此，本轮最合理的后续动作应是把以下内容绑定为同一轮修复目标：

- 打开 complex 值类型与 `simd-complex` 路径
- 按当前草案补齐 bit / math / complex 算法面
- 同步修正已实现数学函数的约束与混合参数 overload set
- 增补对应的 configure probe、compile probe、runtime tests，尤其是负向约束测试

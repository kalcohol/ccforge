# `std::simd` 独立审查报告 (p1-v0-r1)

审查对象：
- 实现文件：`backport/cpp26/simd.hpp`、`backport/cpp26/simd/*.hpp`
- 测试文件：`test/test_simd*.cpp`、`test/simd_configure_probes/*.cpp`、`test/CMakeLists.txt`

审查基线：
- 当前工作树，基于提交 `eeec5e0`
- 当前 C++ 工作草案 `[simd]`：`https://eel.is/c++draft/simd`
- 本文不特别复用既有 review 结论，按当前工作树重新审视

---

## 执行摘要

按标准委员会和 STD 标准库的交付质量要求看，当前 `std::simd` 仍有几组不能继续视为“边角问题”的 P1 缺口。

本轮独立确认的重点问题有四项：

1. 当前实现仍把 `basic_vec` 限定在“算术非 `bool` + 扩展整数”子集，导致现行工作草案中的 `simd-complex` 路径整体不可用
2. 标准算法表面仍有成组缺失，不是零散漏掉一两个函数，而是 bit family / math family / complex family 仍然明显不完整
3. integral `basic_vec` 的位移运算符集合仍不完整，当前只实现了“按标量位移”，没有实现草案要求的“按向量逐 lane 位移”
4. 测试虽然比前一轮规整，但仍没有把这些标准表面缺口锁死；本轮仍需要额外探针/检索才能确认，说明覆盖体系还不够

这几项已经足以说明：当前 `std::simd` 不能仅按“修 wording 细节”的节奏继续前进，仍需补齐标准表面并同步补完测试。

---

## 主要问题

### I-1 `simd-complex` 路径整体未打开，现行草案中的 complex `simd` 仍不可用

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/base.hpp:28-33`
  - `backport/cpp26/simd/base.hpp:243`
  - `backport/cpp26/simd/types.hpp:309`
- 相关条文：
  - 当前工作草案 `[simd]`
  - 其中关于 `simd-complex`、`basic_vec(const rebind_t<complex<value_type>, basic_vec>&)`、`real` / `imag` 等现行接口要求

**问题描述：**

当前实现的值类型总闸门仍是：

```cpp
template<class T>
struct is_supported_value
    : integral_constant<bool,
        (is_arithmetic<remove_cvref_t<T>>::value || is_extended_integer<remove_cvref_t<T>>::value) &&
        !is_same<remove_cvref_t<T>, bool>::value> {};
```

同时，`basic_vec<T, Abi>` 在类模板层面继续以该条件做 `static_assert`。这意味着：

- `basic_vec<std::complex<float>, Abi>` / `basic_vec<std::complex<double>, Abi>` 无法实例化
- 与 complex lane 相关的构造、访问器和算法路径都在入口处被整体切断

这已经不是“某个 complex 算法还没补”，而是现行草案里 complex `simd` 整条标准路径仍不可达。

**影响：**

- 任何面向现行草案 `simd-complex` 的代码都无法在当前 backport 上建立基本类型
- 后续即使零散补 `real` / `imag` / `conj` 等算法，也无法形成可用表面
- 当前实现仍然明显停留在“算术标量回退版 simd”，还没有进入现行草案要求的完整类型面

### I-2 标准算法表面仍是明显子集，bit / math / complex 三组接口缺口成片存在

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/operations.hpp`
  - `test/test_simd_*.cpp`
  - `test/simd_configure_probes/*.cpp`
- 相关条文：
  - 当前工作草案 `[simd]` 中的 bit / math / complex 算法族

**问题描述：**

当前实现已有少量基础算法，例如 `abs`、`sqrt`、`sin`、`cos`、`exp`、`log`、`pow`，但与现行草案相比，算法表面仍有成组缺失。

本轮重新检查时，至少确认当前实现和测试中都没有出现下列现行草案算法族的实现或覆盖痕迹：

- bit family：`byteswap`、`popcount`、`countl_zero`、`countl_one`、`countr_zero`、`countr_one`、`bit_width`、`has_single_bit`、`bit_floor`、`bit_ceil`、`rotl`、`rotr`
- math family：`log10`、`frexp`、`modf`、`remquo`、`asin`、`acos`、`atan`、`sinh`、`cosh`、`tanh`、`asinh`、`acosh`、`atanh`
- complex family：在 I-1 前提下整体不可达，同时实现和测试中也没有 complex 相关入口

这里的问题不是“还有若干 TODO”，而是 `<simd>` 的公开算法表面仍然只覆盖了现行草案中的一个子集。

**影响：**

- 当前 backport 不能被视为“接口基本齐，只差少量边缘约束”
- 用户一旦按现行草案使用这些算法，将直接发现缺失，而不是只遇到轻微行为差异
- review 和测试很容易因为已有基础算法能跑通而高估完成度

### I-3 integral `basic_vec` 位移运算符集合不完整，缺少逐 lane 向量位移重载

- 严重程度：高
- 位置：
  - `backport/cpp26/simd/types.hpp:523-531`
  - `backport/cpp26/simd/types.hpp:593-599`
  - `backport/cpp26/simd/where.hpp:192-204`
- 相关条文：
  - 当前工作草案 `[simd]` 中 integral `basic_vec` 的位移运算符集合
  - 现行草案可见 `operator<<=(const basic_vec& x, const basic_vec& y)` / `operator>>=(...)` 这一类逐 lane 重载

**问题描述：**

当前实现只提供了：

- `basic_vec& operator<<=(Shift shift)`
- `basic_vec& operator>>=(Shift shift)`
- `operator<<(basic_vec, Shift)`
- `operator>>(basic_vec, Shift)`

也就是“整向量统一按一个标量位移量进行位移”。但现行草案还要求支持“位移量本身也是一个 `basic_vec`”的逐 lane 位移重载。

同样地，`where_expression` 目前也只支持：

- `where(mask, v) <<= scalar`
- `where(mask, v) >>= scalar`

没有对应的逐 lane 向量位移赋值。

**影响：**

- 标准代码 `v <<= counts`、`auto r = v >> counts`、`where(m, v) <<= counts` 在当前 backport 上会直接失配
- 这不是实现策略差异，而是公开运算符集合缺口
- 当前测试只覆盖了标量位移，因此这个标准缺口会长期潜伏

### I-4 现有测试仍未把“标准表面完整性”锁住，本轮仍需要额外探针才能确认缺口

- 严重程度：中
- 位置：
  - `test/CMakeLists.txt`
  - `test/test_simd_api_*.cpp`
  - `test/test_simd_*.cpp`
- 相关条文：
  - 当前工作草案 `[simd]`

**问题描述：**

本轮之所以还需要额外检索/探针来确认问题，本身就说明当前测试体系没有把这些公共契约稳定锁住。

当前测试的主要覆盖重心仍然是：

- arithmetic `vec` / `mask` 基本构造与运算
- memory / gather / scatter / `where`
- reductions / permute / chunk / expand

但对下列标准表面，目前没有形成成体系的 compile-time / runtime 防线：

- complex `simd` 能否实例化、转换、访问
- bit family 是否存在、返回类型是否正确、约束是否正确
- math family 扩展表面是否存在
- integral 向量位移重载是否可见，`where(mask, v)` 的向量位移赋值是否可见

换句话说，这一轮“只要有怀疑点就还得写探针”并不是偶发现象，而是测试覆盖口径仍偏向“当前实现已经做了什么”，而不是“现行草案要求必须锁住什么”。

**影响：**

- 标准表面缺失可以在较长时间内不被测试直接暴露
- 后续修复如果只补实现、不补测试，很容易再次出现“本轮靠人工探针才发现”的情况
- 这会持续拉低 review 的确定性和回归防护强度

---

## 结论

当前 `std::simd` 的剩余问题，已经不只是局部 wording 偏差，而是仍然存在：

1. 现行草案类型面没有补齐，尤其是 `simd-complex`
2. 现行草案算法面仍是明显子集
3. 某些公开运算符集合仍缺项
4. 测试还没有把这些“标准表面完整性”问题系统地锁住

因此，本轮最合理的后续动作不应只是继续零散修补，而应把：

- complex 类型面
- bit / math / complex 算法面
- integral 向量位移重载
- 与上述表面一一对应的 compile probe / runtime tests

作为同一轮修复与补测的绑定目标处理。

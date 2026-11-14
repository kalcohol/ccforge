# `std::linalg` backport 说明

当前为 P1673R13 风格的实验性 backport，提供一个不依赖外部 BLAS 库的实用 BLAS 子集。

## 如何使用

包含 `<linalg>` 并链接 `forge::std`（需要扩展时使用 `forge::forge`），采用 C++23 或更新模式。

```cpp
#include <linalg>
#include <mdspan>
float data[] = {1, 2, 3};
std::mdspan v(data, std::extents<int, 3>{});
auto sum = std::linalg::dot(v, v);
```

## 特性宏

包含 `<linalg>` 后不定义 `__cpp_lib_linalg`，因为实现仍是工作草案子集。

## Native 让位

统一策略见 [native handoff](../native-handoff.md)。`FORGE_FORCE_LINALG_BACKPORT` 只用于诊断，具有 ODR 风险。

## 覆盖范围

### 覆盖范围

以下列出已实现的函数族，不表示包含当前 WD 的每个重载；具体缺口见下方限制。

**BLAS Level 1：** `copy`、`scale`、`swap_elements`、`add`、`dot`、`dotc`、
`vector_two_norm`、`vector_abs_sum`、`vector_idx_abs_max`、
`setup_givens_rotation`、`apply_givens_rotation`

**历史草案兼容接口：** `sum_of_squares_result` / `vector_sum_of_squares`。
它们已由 [LWG 4302](https://cplusplus.github.io/LWG/issue4302) 从标准草案移除。
本 backport 仍提供旧接口，但它们不属于当前 WD 的标准接口；不能依赖它们切换到原生
`<linalg>`。整数实例化已弃用并提供 Forge 迁移入口；浮点历史入口继续保留。

**矩阵范数：** `matrix_frob_norm`、`matrix_one_norm`、`matrix_inf_norm`。

**BLAS Level 2：** `matrix_vector_product`、`triangular_matrix_vector_product`、
`triangular_matrix_vector_solve`、`symmetric_matrix_vector_product`、
`hermitian_matrix_vector_product`、`matrix_rank_1_update` / `_c`、
`symmetric_matrix_rank_1/2_update`、`hermitian_matrix_rank_1/2_update`

**BLAS Level 3：** `matrix_product`、`triangular_matrix_left_product`、
`triangular_matrix_right_product`、`triangular_matrix_matrix_left_solve`、
`symmetric_matrix_product`、`hermitian_matrix_product`、`symmetric_matrix_rank_k/2k_update`、
`hermitian_matrix_rank_k_update`

**辅助组件：** `scaled` / `conjugated` / `transposed` / `conjugate_transposed` 视图函数、
`scaled_accessor`、`conjugated_accessor`、`layout_transpose`、`layout_blas_packed`、
标记类型（`upper_triangle` / `lower_triangle` / `column_major` / `row_major` 等）

## 限制与偏差

### 语义和限制

- 依赖 C++23 `<mdspan>`，在无 `<mdspan>` 的工具链上（如 GCC 13）优雅跳过。
- 当原生 `<linalg>` 可用时，backport 自动禁用。
- Forge 当前不定义标准 feature-test macro `__cpp_lib_linalg`，因为这仍是实验性 draft
  子集；下游不应把它当作完整 C++26 `<linalg>` 宣告。
- 未实现 execution policy 重载，不链接系统 BLAS；SIMD 是 Forge 自身的可选实现细节。
- Level 1、Level 2、Level 3 与辅助视图有直接回归测试。
- Level 2/3 rank-update 采用当前 draft 的 overwrite/update 分离：不带输入矩阵 `E` 的重载覆盖输出矩阵，带 `E` 的重载计算 `A = E + update`。
- 有意偏离（整型范数/SSQ）：当前 WD 的 `vector_two_norm` / `matrix_frob_norm`
  要求浮点或复数元素与 `Scalar`；本 backport 额外接受整型实例化，切换到原生
  `std::linalg` 的代码不应依赖这一扩展。对实数整型元素及整型访问值、整型 init，
  两个范数入口在平方和（包含 `|init|²`）不超过 2^53 时精确累计整数平方和，
  然后在 double 中开方；超出预算时保留原 double scaled sum-of-squares 递推。
  该预算只保证平方和精确，不承诺任意非平方数的数学精确整数平方根；最终仍对
  浮点平方根结果向零截断，并在超出结果类型表示域时饱和，不做四舍五入或误差补偿。
  浮点/复数访问值或非整型 init 不使用此精确路径。`vector_sum_of_squares` 的
  整型结果仍在 double 中累计原始平方和，以 `scaling_factor == 1` 报告，并在
  回转超范围时饱和；平方项或累计和超过 2^53 后允许 double 舍入，并非任意精度精确。
- 有符号整型的 magnitude（`vector_abs_sum`、`vector_idx_abs_max`、范数与 SSQ
  的逐项绝对值）在对应无符号类型中计算，最小负值（如 `INT_MIN`）有良定义的
  幅值 2^31 而不是 `abs()` 未定义行为。无 init 的 `vector_abs_sum` 按当前 WD
  返回输入 `value_type`；标准整型元素的 `vector_abs_sum`、`matrix_one_norm` 与
  `matrix_inf_norm` 在 `uintmax_t` 中精确累计 magnitude，并在超出结果类型表示域时
  饱和。显式 wider floating `Scalar` 的复数 magnitude 在该 `Scalar` 精度中计算，
  不先在较窄元素类型中溢出。
- Level 2 的 `triangular_matrix_vector_product` 仅实现 in-place 和 out-of-place
  版本，未实现带输入向量 `y` 的 updating 版本；`triangular_matrix_vector_solve`
  仅实现 in-place 版本，未实现 out-of-place 或自定义 `BinaryDivideOp` 重载。
- Level 3 的 in-place triangular product 使用
  `triangular_matrix_left_product` / `triangular_matrix_right_product`；当前 WD
  的 out-of-place `triangular_matrix_product` 左、右两侧版本及其 updating 重载均未实现。
  旧的带 `Side` 参数的非标准 wrapper 不等同于这些标准重载，也不再暴露。
- Level 3 的 symmetric / Hermitian matrix product 仅实现左侧 overwrite 版本；
  右侧版本及带输入矩阵 `E` 的 updating 版本未实现。
- Level 3 triangular solve 仅实现左侧 in-place 矩阵版本，未实现右侧、out-of-place
  或自定义 `BinaryDivideOp` 重载；Hermitian rank-2k 仍未实现。


### SIMD 加速

BLAS Level 1 归约操作（`dot`、`vector_abs_sum`）以及 `copy`、`scale` 在 Forge
`std::simd` 可用时自动使用 SIMD 加速路径；`vector_two_norm` 和
`matrix_frob_norm` 使用 scaled sum-of-squares，避免有限范数在逐项平方时先溢出。
`matrix_vector_product`（GEMV）内层循环也已 SIMD 化。

支持非复数标准算术类型中的 SIMD-friendly 子集；实际启用路径要求 contiguous
layout/default accessor。验证入口见[测试说明](../testing.md)；跨架构支持须以对应工具链实际运行结果为准。


### 性能特征

Level 3 的 `matrix_product`（含 updating 重载）、已实现的 symmetric / Hermitian
product 和 in-place triangular product 按操作数布局选择循环顺序。
`layout_right` / `layout_right_padded` 优先沿列方向遍历，
`layout_left` / `layout_left_padded` 在适用时沿行方向遍历；混合布局选择可连续
访问的操作数。小块独立累加器保持各输出元素的 `k` 递增顺序，updating 版本在
归约结束后才加上 `E`。`layout_stride`、未知布局及自定义 accessor 使用标量路径；
所有元素访问均经过 mdspan accessor，支持 scaled、conjugated 和 transposed view。
三角矩阵的 in-place product 无需动态分配。对于 default accessor，右乘在 A 与 C
均为 `layout_right` / `layout_right_padded` 时逐行执行 scaled row update：
下三角按源列 `k` 递增，上三角递减，内层连续访问 A 与 C 的行。左乘在两者均为
`layout_left` / `layout_left_padded` 时逐列执行对应的 scaled column update：
上三角按源行 `k` 递增，下三角递减，内层连续访问两者的列。每步先保存尚未覆盖的
源元素，再处理对角项并更新已消费源元素的位置；implicit unit diagonal 不读取 A
的对角项。这两条路径可改变浮点累加顺序。其余布局组合保留按依赖顺序计算输出的
dot product 路径；`layout_stride`、packed layout 和非 default accessor 保留原有路径。
实现不依赖 external BLAS，也不使用手写 intrinsics kernel。

独立基准及同一 CPU 上的配对运行命令见
[`test/linalg/benchmark/MEASUREMENT_PROTOCOL.txt`](../../test/linalg/benchmark/MEASUREMENT_PROTOCOL.txt)。
基准不默认注册为 CTest；性能取决于布局、尺寸和工具链。


### OpenMP 并行

无 execution policy 的 `std::linalg` overload 默认保持顺序执行；Forge 当前不再因为
翻译单元带 `-fopenmp` 就隐式并行化 GEMM/GEMV。未来若补并行路径，会放在显式 opt-in
或 execution policy overload 下。


### 非标准重载

`std::linalg` 下将 Triangle 放在第一个参数、以及带额外 alpha 的兼容重载不是工作草案形态，原生让位后可能无法编译。整数范数也属于非工作草案扩展，参见 [LWG 4315](https://cplusplus.github.io/LWG/issue4315)，原生实现上无法依赖。上述 Triangle 首参入口、整数范数与整数 Givens 标量入口已弃用，迁移方法见下表。

## 示例

示例门控与所有文件见[示例索引](../examples.md)。只在注入或完整原生时构建；明确依赖扩展者另列限制。验证流程见[测试说明](../testing.md)，测试组为 `FORGE_TEST_ENABLE_LINALG`。

- [linalg_level2_example.cpp](../../example/linalg_level2_example.cpp)：linalg level2 的接口组合与完成结果。

- [linalg_level3_example.cpp](../../example/linalg_level3_example.cpp)：linalg level3 的接口组合与完成结果。

- [linalg_simd_example.cpp](../../example/linalg_simd_example.cpp)：linalg simd 的接口组合与完成结果。

- [linalg_views_example.cpp](../../example/linalg_views_example.cpp)：linalg views 的接口组合与完成结果。

## 验证

验证流程见[测试说明](../testing.md)，使用对应测试组及 `ctest -R '^example_'` 运行示例。

## 扩展重载弃用与迁移

以下旧入口保留至少一个带 `[[deprecated]]` 的发布版本；计划在后续版本移除，具体版本由维护者决定。旧入口的计算结果保持不变。

| 弃用入口 | 替代入口 |
| --- | --- |
| `hermitian_matrix_rank_1_update(t, x, A)` | `hermitian_matrix_rank_1_update(Scalar{1}, x, A, t)` |
| `hermitian_matrix_rank_2_update(t, x, y, A)` | `hermitian_matrix_rank_2_update(x, y, A, t)` |
| `hermitian_matrix_product(t, A, B, C)` | `hermitian_matrix_product(A, t, B, C)` |
| `hermitian_matrix_rank_k_update(t, A, C)` | `hermitian_matrix_rank_k_update(Scalar{1}, A, C, t)` |
| `std::linalg::vector_two_norm(v[, init])` 的整数元素或整数 `Scalar` 实例化 | `<forge/linalg.hpp>` 中的 `forge::vector_two_norm(v[, init])`，或显式转换为符合草案的浮点/复数输入及 `Scalar` |
| `std::linalg::matrix_frob_norm(A[, init])` 的整数元素或整数 `Scalar` 实例化 | `<forge/linalg.hpp>` 的 `forge::matrix_frob_norm(A[, init])` |
| `std::linalg::vector_sum_of_squares(v, init)` 的整数元素或整数结果标量实例化 | `forge::vector_sum_of_squares(v, init)`；结果类型可用 `forge::sum_of_squares_result<T>` |
| `std::linalg::setup_givens_rotation(a, b)` 的整数 `Real` 实例化 | `forge::setup_givens_rotation(a, b)` |
| `std::linalg::apply_givens_rotation(x, y, c, s)` 的整数 `Real` 实例化 | `forge::apply_givens_rotation(x, y, c, s)` |

上表矩阵算法位于 `std::linalg`；`Scalar{1}` 应采用原输出元素对应的标量类型。浮点和复数的标准范数调用不产生弃用诊断。`forge::vector_two_norm` 共用原范数实现，整数结果仍向零截断并饱和；原生 linalg 让位时转用标准入口，整数扩展仅在 `FORGE_LINALG_BACKPORT_ACTIVE` 已定义时受支持。上述整数入口均已弃用，保留至少一个弃用发布版本，移除版本由维护者决定。Forge 入口共用原计算体，保持截断、饱和、累计顺序及返回类型。`forge::matrix_frob_norm` 与 Givens 入口在原生让位时转用标准入口，仅 backport 保证整数扩展；`forge::vector_sum_of_squares` 与结果别名仅在 backport 下提供。

依据为 2026-11-13 查阅的 [linalg synopsis](https://eel.is/c++draft/linalg.syn) 与 [vector_two_norm Mandates](https://eel.is/c++draft/linalg.algs.blas1.nrm2)。整数实例化超出当前草案要求；[LWG 4315](https://cplusplus.github.io/LWG/issue4315) 仅作为相关问题线索，不将未来可能的修改视为当前标准保证。

[矩阵 Frobenius 范数 Mandates](https://eel.is/c++draft/linalg.algs.blas1.matfrobnorm) 要求元素与 `Scalar` 为浮点或复数；[按参数名规定的算法要求](https://eel.is/c++draft/linalg.algs.reqs) 还限制 Givens 的 `Real`。`matrix_one_norm`、`matrix_inf_norm`、`vector_abs_sum` 与 `dot` 没有相同的类型限制，其整数调用保持标准入口。`vector_sum_of_squares` 已从当前草案移除，本次只对整数兼容实例化增加弃用诊断；浮点历史入口仍按既有兼容策略保留。

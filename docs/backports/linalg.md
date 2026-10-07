# `std::linalg` backport 说明

当前为 P1673R13 风格的实验性 backport，提供一个不依赖外部 BLAS 库的实用 BLAS 子集。

## 覆盖范围

以下列出已实现的函数族，不表示包含当前 WD 的每个重载；具体缺口见下方限制。

**BLAS Level 1：** `copy`、`scale`、`swap_elements`、`add`、`dot`、`dotc`、
`vector_two_norm`、`vector_abs_sum`、`vector_idx_abs_max`、
`setup_givens_rotation`、`apply_givens_rotation`

**历史草案兼容接口：** `sum_of_squares_result` / `vector_sum_of_squares`。
它们已由 [LWG 4302](https://cplusplus.github.io/LWG/issue4302) 从标准草案移除。
本 backport 仍提供旧接口，但它们不属于当前 WD 的标准接口；不能依赖它们切换到原生
`<linalg>`。此处仅标明遗留状态，不移除或迁移现有接口。

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

## 语义和限制

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

## SIMD 加速

BLAS Level 1 归约操作（`dot`、`vector_abs_sum`）以及 `copy`、`scale` 在 Forge
`std::simd` 可用时自动使用 SIMD 加速路径；`vector_two_norm` 和
`matrix_frob_norm` 使用 scaled sum-of-squares，避免有限范数在逐项平方时先溢出。
`matrix_vector_product`（GEMV）内层循环也已 SIMD 化。

支持非复数标准算术类型中的 SIMD-friendly 子集；实际启用路径要求 contiguous
layout/default accessor。已在 x86_64（原生）、aarch64、riscv64、loongarch64 四个架构上
通过 zig 交叉编译 + qemu 验证。

## OpenMP 并行

无 execution policy 的 `std::linalg` overload 默认保持顺序执行；Forge 当前不再因为
翻译单元带 `-fopenmp` 就隐式并行化 GEMM/GEMV。未来若补并行路径，会放在显式 opt-in
或 execution policy overload 下。

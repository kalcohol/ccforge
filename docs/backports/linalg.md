# `std::linalg` backport 说明

当前为 P1673R13 风格的实验性 backport，提供一个不依赖外部 BLAS 库的实用 BLAS 子集。

## 覆盖范围

**BLAS Level 1：** `copy`、`scale`、`swap_elements`、`add`、`dot`、`dotc`、
`vector_two_norm`、`vector_abs_sum`、`vector_idx_abs_max`、`vector_sum_of_squares`、
`setup_givens_rotation`、`apply_givens_rotation`

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
- 有意偏离（整型范数/SSQ）：WD 的参考实现按浮点/复数元素设计，整型元素下
  scaled 递归的比值除法会截断（`{3, 4}` 的 two-norm 得 4）；原生实现可能拒绝
  整型实例化或给出错误值。本 backport 接受整型元素：`vector_two_norm`、
  `matrix_frob_norm`、`vector_sum_of_squares` 对整型在 double 中间精度累加
  （SSQ 以 `scaling_factor == 1` 报告原始平方和），回转结果超出元素类型表示域
  时饱和到边界而不是未定义行为。它们的精度边界即 double 的 53-bit 整数精度：
  平方项或累计和超过 2^53（元素绝对值约 9.5e7 起）后按 double 舍入，结果是
  "double-exact" 而非任意精度精确。切换到原生 `std::linalg` 的代码不应依赖
  整型实例化。整型 `vector_two_norm` / `matrix_frob_norm` 的平方根结果先按普通
  浮点到整型转换向零截断，再在超出结果类型表示域时饱和。
- 有符号整型的 magnitude（`vector_abs_sum`、`vector_idx_abs_max`、范数与 SSQ
  的逐项绝对值）在对应无符号类型中计算，最小负值（如 `INT_MIN`）有良定义的
  幅值 2^31 而不是 `abs()` 未定义行为。无 init 的 `vector_abs_sum` 按当前 WD
  返回输入 `value_type`；标准整型元素的 `vector_abs_sum`、`matrix_one_norm` 与
  `matrix_inf_norm` 在 `uintmax_t` 中精确累计 magnitude，并在超出结果类型表示域时
  饱和。显式 wider floating `Scalar` 的复数 magnitude 在该 `Scalar` 精度中计算，
  不先在较窄元素类型中溢出。
- Triangular matrix-matrix product 使用当前 draft 的
  `triangular_matrix_left_product` / `triangular_matrix_right_product` 拼写；旧的
  `triangular_matrix_product(..., Side, ...)` 非标准 wrapper 不再暴露。
- Level 3 目前覆盖左右两侧 triangular product；triangular solve 仍只覆盖左侧矩阵版本，
  Hermitian rank-2k 仍未实现。

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

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

**BLAS Level 3：** `matrix_product`、`triangular_matrix_product`、
`triangular_matrix_matrix_left_solve`、`symmetric_matrix_product`、
`hermitian_matrix_product`、`symmetric_matrix_rank_k/2k_update`、
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
- 旧参数顺序仍作为兼容 wrapper 保留，但文档化 API 以后续 draft 拼写为准。
- Level 3 目前只覆盖左侧 triangular product/solve；右侧 triangular overload 与 Hermitian rank-2k 仍未实现。

## SIMD 加速

BLAS Level 1 归约操作（`dot`、`vector_two_norm`、`vector_abs_sum`）以及 `copy`、
`scale` 在 Forge `std::simd` 可用时自动使用 SIMD 加速路径；
`matrix_vector_product`（GEMV）内层循环也已 SIMD 化。

支持非复数标准算术类型中的 SIMD-friendly 子集；实际启用路径要求 contiguous
layout/default accessor。已在 x86_64（原生）、aarch64、riscv64、loongarch64 四个架构上
通过 zig 交叉编译 + qemu 验证。

## OpenMP 并行

无 execution policy 的 `std::linalg` overload 默认保持顺序执行；Forge 当前不再因为
翻译单元带 `-fopenmp` 就隐式并行化 GEMM/GEMV。未来若补并行路径，会放在显式 opt-in
或 execution policy overload 下。

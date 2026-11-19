# `std::linalg` backport 说明

Forge 的 backport 不依赖外部 BLAS 库，覆盖工作草案 synopsis 的算法与辅助接口。

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

注入 backport 时，`<linalg>` 按工作草案定义 `__cpp_lib_linalg = 202511L`。强制注入时也使用该值，包括原生头已公布旧值的情况。原生探测先于 backport 注入，让位时由标准库提供宏。[version.syn](https://eel.is/c++draft/version.syn)
`FORGE_LINALG_HAS_EXECUTION_POLICY_OVERLOADS` 为 `1` 时提供 policy 重载，为 `0` 时不声明它们。该标记只描述 backport；原生让位时不定义。

## Native 让位

原生探测使用特性宏和实际 API 声明作为让位依据。没有特性宏且仅声明空 `std::linalg` 命名空间时仍注入 backport。`FORGE_FORCE_LINALG_BACKPORT` 只用于诊断，具有 ODR 风险。[native handoff](../native-handoff.md)

## 覆盖范围

以下函数族包含当前 [synopsis](https://eel.is/c++draft/linalg.syn) 的 84 个非 policy 函数模板和 78 个 policy 函数模板；policy 入口受原生 policy 对象可用性门控。

**BLAS Level 1：** `copy`、`scale`、`swap_elements`、`add`、`dot`、`dotc`、
`vector_two_norm`、`vector_abs_sum`、`vector_idx_abs_max`、
`setup_givens_rotation`、`apply_givens_rotation`

**矩阵范数：** `matrix_frob_norm`、`matrix_one_norm`、`matrix_inf_norm`。

**BLAS Level 2：** `matrix_vector_product`、`triangular_matrix_vector_product`、
`triangular_matrix_vector_solve`、`symmetric_matrix_vector_product`、
`hermitian_matrix_vector_product`、`matrix_rank_1_update` / `_c`、
`symmetric_matrix_rank_1/2_update`、`hermitian_matrix_rank_1/2_update`

**BLAS Level 3：** `matrix_product`、`triangular_matrix_left_product`、
`triangular_matrix_right_product`、`triangular_matrix_product`、
`triangular_matrix_matrix_left_solve` / `triangular_matrix_matrix_right_solve`、
`symmetric_matrix_product`、`hermitian_matrix_product`、`symmetric_matrix_rank_k/2k_update`、
`hermitian_matrix_rank_k/2k_update`

**辅助组件：** `scaled` / `conjugated` / `transposed` / `conjugate_transposed` 视图函数、
`scaled_accessor`、`conjugated_accessor`、`layout_transpose`、`layout_blas_packed`、
标记类型（`upper_triangle` / `lower_triangle` / `column_major` / `row_major` 等）

## 限制与偏差

### 语义和限制

- `transposed` 对 padded layouts 按草案的预期映射语义实现，偏离
  [linalg.transp.transposed](https://eel.is/c++draft/linalg.transp.transposed)
  p4.2/p4.3 直接将 source stride 作为 padding 参数的字面写法。
  对有效源 mapping，padded stride 为正时，以交换后的 extents 和 strides 构造
  `layout_stride::mapping`，再显式转换为结果 padded mapping，保留精确 stride。
  此路径也适用于转换构造产生的零 padded extent、正 stride，以及完整 padded product
  超出 `index_type`、但 `required_span_size()` 可表示的映射。
  stride 为零时，padded extent 必为零；静态 padding 传入 `padding_value`，
  动态 padding 传入 `1`，均重建零 stride。由此保留交换后的 extents、strides、
  required span size 与元素映射；不要求源映射可由 `(extents, padding)` 重建。
  静态 padding 的零 padded extent 要求 stride 为零，故不包含正 stride 情形。
  构造器前置条件见
  [mdspan.layout.leftpad.cons](https://eel.is/c++draft/mdspan.layout.leftpad.cons) 与
  [mdspan.layout.rightpad.cons](https://eel.is/c++draft/mdspan.layout.rightpad.cons) 与
  [mdspan.layout.stride.cons](https://eel.is/c++draft/mdspan.layout.stride.cons)。
- 依赖 C++23 `<mdspan>`，在无 `<mdspan>` 的工具链上（如 GCC 13）优雅跳过。
- 当原生 `<linalg>` 可用时，backport 自动禁用。
- 不链接系统 BLAS；SIMD 是 Forge 自身的可选实现细节。policy 行为见下节。
- Level 1、Level 2、Level 3 与辅助视图有直接回归测试。
- Level 2/3 rank-update 采用当前 draft 的 overwrite/update 分离：不带输入矩阵 `E` 的重载覆盖输出矩阵，带 `E` 的重载计算 `A = E + update`。
- 有符号整型的 magnitude（`vector_abs_sum`、`vector_idx_abs_max`、`matrix_one_norm`、
  `matrix_inf_norm` 的逐项绝对值）在对应无符号类型中计算，最小负值（如 `INT_MIN`）有良定义的
  幅值 2^31 而不是 `abs()` 未定义行为。无 init 的 `vector_abs_sum` 按当前 WD
  返回输入 `value_type`；标准整型元素的 `vector_abs_sum`、`matrix_one_norm` 与
  `matrix_inf_norm` 在 `uintmax_t` 中精确累计 magnitude，并在超出结果类型表示域时
  饱和。显式 wider floating `Scalar` 的复数 magnitude 在该 `Scalar` 精度中计算，
  不先在较窄元素类型中溢出。
- 草案的 Mandates 以 requires-clause 表达，违反时从候选集合移除，因此可由
  requires-expression / SFINAE 检测。额外静态 extent 检查只拒绝必定违反运行期
  Preconditions 的调用。参数按可推导的 `std::mdspan` specialization 拼写，因而也接受
  mdspan 派生类并按基类值复制；草案的 exposition-only 概念要求精确 specialization。
- 输出约束同时检查 reference 对 element_type 的可赋值性与 mapping 的静态唯一性；
  structured rank-update 输出保留 packed layout 例外。所有 solve 支持仅可赋值、
  可读为元素值的 proxy，不要求在 output reference 上提供 `-=` 或 `/=`。
- triangular vector solve、左右 matrix solve 均有 in-place / out-of-place 和
  自定义 `BinaryDivideOp` 版本。不提供 divide 的版本等效于使用 `std::divides<void>{}`。
  草案的一般 divide 要求描述右逆，与 left-trsm note 要求的左逆在非交换类型上冲突。
  本实现按该 note 选择：divide 的首参为分子，次参为对角元素；左侧非交换类型须提供
  左逆语义，右侧须提供右逆语义，参见 [trsm](https://eel.is/c++draft/linalg.algs.blas3.trsm)。不保证 divide 的调用
  顺序、次数或某一次分子的形式。implicit unit diagonal 不读取对角存储。
- updating triangular vector product 允许 `z` alias `y`；左右 symmetric / Hermitian /
  triangular matrix product 的 updating 版本均允许 `C` alias `E`；Hermitian rank-2k
  updating 同样允许 `C` alias `E`。这些许可指草案定义的 alias；solve 不允许输出与其它
  mdspan 参数重叠。仅访问指定 triangle，Hermitian 输入对角只使用实部。
- Hermitian product 的混合类型 factor 先在自身类型中参与乘法，再转换输出；当前顺序
  kernel 的特定浮点舍入回归标记为 Forge expectation，不作为草案要求。
- Hermitian rank 输出仍写入严格实数对角。这是 Forge 在舍入结果上的 BLAS 兼容选择，
  超出草案仅要求数学结果的措辞；通过独立赋值实现，不要求实部与元素值之间隐式转换。
- `layout_transpose::mapping` 保留公开的 reserved nested type alias，以及部分比草案
  更强的 `noexcept`。heterogeneous equality
  比较 nested mappings；packed mapping 的索引模板检查可转换性与不抛出构造。
- substitution 的实际复杂度是 `O(n² × nrhs)`。草案部分矩形 left-trsm、in-place
  left/right-trsm 以及 left-trmm 的复杂度式使用了不对应的维度；实现不承诺这些式子在
  任意矩形尺寸下都成立，参见 [inplacetrsm](https://eel.is/c++draft/linalg.algs.blas3.inplacetrsm)。

### Execution policies

仅在原生 `<execution>` 提供 policy 对象时声明全部 78 个 policy 重载。共享的
native-only header detection 从 backport include root 加载原生头，并在加载后检查
`__cpp_lib_execution`，不会定义任何标准 feature-test macro，也不会因为包含 `<linalg>`
而加载 Forge P2300 backport。`<execution>` 在 `<linalg>` 前后包含的结果一致；同一
工具链配置的不同翻译单元具有相同入口集合。

接受 `std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>>` 为真的原生
标准或 implementation-defined policy。所有 policy 均在调用线程顺序执行，不分配
并行工作存储；元素访问函数逸出的异常调用 `std::terminate`。公开函数不声明 `noexcept`，
进入函数前参数复制产生的异常仍按调用方规则处理。`setup_givens_rotation` 按草案没有
policy 版本。

[linalg.algs.reqs](https://eel.is/c++draft/linalg.algs.reqs) 的字面措辞对未去引用的
`ExecutionPolicy` 检查 trait，会拒绝 `std::execution::seq` 的 const lvalue。
这里采用 [algorithms.parallel.overloads](https://eel.is/c++draft/algorithms.parallel.overloads)
的去 cv/ref 规则，以接受这些标准 policy 对象。


### SIMD 加速

BLAS Level 1 归约操作（`dot`、`vector_abs_sum`）以及 `copy`、`scale` 在 Forge
`std::simd` 可用时自动使用 SIMD 加速路径；`vector_two_norm` 和
`matrix_frob_norm` 使用 scaled sum-of-squares，避免有限范数在逐项平方时先溢出。
`matrix_vector_product`（GEMV）内层循环也已 SIMD 化。

支持非复数标准算术类型中的 SIMD-friendly 子集；实际启用路径要求 contiguous
layout/default accessor。验证入口见[测试说明](../testing.md)；跨架构支持须以对应工具链实际运行结果为准。


### 性能特征

Level 3 的 `matrix_product`（含 updating 重载）、symmetric / Hermitian product 的左侧 overwrite 版本 和 in-place triangular product 按操作数布局选择循环顺序。
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

所有 `std::linalg` overload（包括 policy 版本）均保持顺序执行；翻译单元带
`-fopenmp` 不会隐式并行化 GEMM/GEMV。


## 已移除的非草案形式

下表列出已移除的调用形式。表中的矩阵算法位于 `std::linalg`，
`Scalar{1}` 使用输出元素对应的标量类型。

| 移除的形式 | 草案调用或 Forge 迁移入口 |
| --- | --- |
| `vector_two_norm(v[, init])` 的整数或自定义元素、`Scalar` | 整数元素或整数初值调用 `forge::vector_two_norm(v[, init])`；自定义类型转换为浮点或复数后调用标准入口 |
| `matrix_frob_norm(A[, init])` 的整数或自定义元素、`Scalar` | 整数元素或整数初值调用 `forge::matrix_frob_norm(A[, init])`；自定义类型转换为浮点或复数后调用标准入口 |
| `setup_givens_rotation(a, b)` 的整数或自定义 `Real` | 有符号整数调用 `forge::setup_givens_rotation(a, b)`；无符号整数和自定义 `Real` 转换为浮点后调用标准入口 |
| `apply_givens_rotation(x, y, c, s)` 的整数系数 | 有符号整数系数使用 `forge::apply_givens_rotation(x, y, c, s)`；无符号整数系数转换为浮点后调用标准入口 |
| Givens setup 或 application（含 policy application）显式指定带 cv 限定的系数类型，例如 `const double` | 省略显式模板实参，由调用推导 `double`，或显式指定不带 cv 限定的 `double` |
| `std::linalg::vector_sum_of_squares(v, init)` | `forge::vector_sum_of_squares(v, init)`；初值对平方和的贡献为零、只需范数且类型符合草案时，可用 `std::linalg::vector_two_norm(v)` |
| `std::linalg::sum_of_squares_result<T>` | `forge::sum_of_squares_result<T>` |
| `hermitian_matrix_rank_1_update(t, x, A)` | `hermitian_matrix_rank_1_update(Scalar{1}, x, A, t)` |
| `hermitian_matrix_rank_2_update(t, x, y, A)` | `hermitian_matrix_rank_2_update(x, y, A, t)` |
| `hermitian_matrix_product(t, A, B, C)` | `hermitian_matrix_product(A, t, B, C)` |
| `hermitian_matrix_rank_k_update(t, A, C)` | `hermitian_matrix_rank_k_update(Scalar{1}, A, C, t)` |
| `symmetric_matrix_rank_1_update(alpha, t, x, A)` | `symmetric_matrix_rank_1_update(alpha, x, A, t)` |
| `symmetric_matrix_rank_k_update(alpha, t, A, C)` | `symmetric_matrix_rank_k_update(alpha, A, C, t)` |
| `symmetric_matrix_rank_2_update(alpha, t, x, y, A)` | `symmetric_matrix_rank_2_update(x, y, A, t)`；需要原缩放语义时，按原乘法顺序显式计算 `alpha * (x[i] * y[j] + y[i] * x[j])` |
| `symmetric_matrix_rank_2k_update(alpha, t, A, B, C)` | `symmetric_matrix_rank_2k_update(A, B, C, t)`；需要原缩放语义时，用输出元素类型累计每项 `A[i, k] * B[j, k] + B[i, k] * A[j, k]`，再乘以 `alpha` 后赋值 |
| `layout_transpose<Layout>::mapping<Extents>` 的默认构造 | 先构造具有转置前 extents 的 nested mapping，再显式传入 |
| `layout_transpose<Layout>::mapping<Extents>` 从只能显式转换为精确 nested mapping 类型的其他 extents mapping 直接构造 | 先显式转换为精确的 nested mapping 类型，再传入 |

范数与 Givens 的类型域见 [nrm2](https://eel.is/c++draft/linalg.algs.blas1.nrm2)、
[matfrobnorm](https://eel.is/c++draft/linalg.algs.blas1.matfrobnorm) 与
[linalg.algs.reqs](https://eel.is/c++draft/linalg.algs.reqs)。矩阵重载顺序见
[linalg synopsis](https://eel.is/c++draft/linalg.syn)，转置 mapping 构造见
[linalg.transp.layout.transpose](https://eel.is/c++draft/linalg.transp.layout.transpose)。

[Forge linalg 扩展](../forge-utilities.md#线性代数扩展)在 backport 注入时提供整数入口与 SSQ。
[LWG 4302](https://cplusplus.github.io/LWG/issue4302)
从工作草案移除了 SSQ 函数及结果类型。

## 示例

示例门控与所有文件见[示例索引](../examples.md)。只在注入或完整原生时构建；明确依赖扩展者另列限制。验证流程见[测试说明](../testing.md)，测试组为 `FORGE_TEST_ENABLE_LINALG`。

- [linalg_level2_example.cpp](../../example/linalg_level2_example.cpp)：linalg level2 的接口组合与完成结果。

- [linalg_level3_example.cpp](../../example/linalg_level3_example.cpp)：linalg level3 的接口组合与完成结果。

- [linalg_simd_example.cpp](../../example/linalg_simd_example.cpp)：linalg simd 的接口组合与完成结果。

- [linalg_views_example.cpp](../../example/linalg_views_example.cpp)：linalg views 的接口组合与完成结果。

## 验证

验证流程见[测试说明](../testing.md)，使用对应测试组及 `ctest -R '^example_'` 运行示例。

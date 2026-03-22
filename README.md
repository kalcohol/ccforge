# Forge

现代 C++ header-only 库，提供标准库扩展和无感 C++26 backport。

## Backport 一览

| 特性 | 标准提案 | 入口头文件 | 状态 |
|------|---------|-----------|------|
| `std::unique_resource` | P0052R15 | `#include <memory>` | 完整 |
| `std::simd` | P1928 | `#include <simd>` | 核心表面完整 |
| `std::execution` (senders/receivers) | P2300 | `#include <execution>` | Phase 1-3 完整 |
| `std::linalg` (BLAS Level 1/2/3) | P1673R13 | `#include <linalg>` | 完整（SIMD 加速） |
| `std::submdspan` | P2630 | `#include <mdspan>` | 基础设施 |

所有 backport 遵循**无感过渡**原则——当工具链原生支持对应特性后，下游代码**零修改**重新编译即可自动切换。

## 快速开始

```cpp
#include <memory>
// std::unique_resource — RAII 资源管理
auto file = std::make_unique_resource_checked(fopen("data.txt", "r"), nullptr, &fclose);
```

```cpp
#include <execution>
// P2300 senders/receivers — 结构化异步
auto result = std::execution::sync_wait(
    std::execution::just(42)
    | std::execution::then([](int x) { return x * 2; })
);
// *result == tuple{84}
```

```cpp
#include <linalg>
#include <mdspan>
// std::linalg — BLAS Level 1/2/3
double a[] = {1,2,3}, b[] = {4,5,6};
std::mdspan va(a, 3), vb(b, 3);
double d = std::linalg::dot(va, vb); // 1*4 + 2*5 + 3*6 = 32
```

```cpp
#include <execution>
// when_all — 结构化并发
auto [a, b] = *std::execution::sync_wait(
    std::execution::when_all(
        std::execution::just(42),
        std::execution::just(3.14)
    )
);
// a == 42, b == 3.14
```

## CMake 集成

```cmake
include(/path/to/forge/forge.cmake)
target_link_libraries(myapp PRIVATE forge::forge)
```

或使用 `add_subdirectory`：

```cmake
add_subdirectory(forge)
target_link_libraries(myapp PRIVATE forge::forge)
```

`forge.cmake` 会自动探测工具链是否已原生支持各特性，仅在缺失时启用 backport。

## 要求

- C++23 或更高版本
- CMake 3.17 或更高版本
- `std::linalg` 需要工具链提供 `<mdspan>`（如 Zig/LLVM 18+，GCC 14+）

## 无感注入机制

Forge 的核心设计目标：**当未来标准库原生提供相同能力后，下游升级工具链时无需任何源码修改即可自动切换到原生实现。**

为此，backport 满足以下要素：

- **标准入口不变**：下游写 `#include <memory>` / `#include <execution>` / `#include <linalg>`，而不是 `forge/...`
- **命名空间不变**：API 以 `std::` / `std::execution::` / `std::linalg::` 形式出现
- **API 形状一致**：公开接口与标准最终版保持一致，不引入额外扩展
- **自动开关**：`forge.cmake` 通过 `check_cxx_source_compiles()` 和 feature-test macro（`__cpp_lib_senders`、`__cpp_lib_linalg` 等）探测原生支持，仅在缺失时注入 backport

实现方式：`forge.cmake` 将 `backport/` 前置到 include path；`backport/` 内提供与标准同名的包装头（如 `backport/memory`、`backport/linalg`），先包含真实标准库头，再条件注入 backport 实现。

> 注意：向 `namespace std` 注入声明/定义在严格意义上属于未定义行为。这是 backport 为达成"无感切换"的工程性权衡，通过"仅在工具链缺失该特性时启用"来降低风险面。

## `std::execution` 说明

当前为 P2300 senders/receivers 的 Phase 1-3 完整 backport：

**已实现：**
- Sender 工厂：`just`、`just_error`、`just_stopped`、`read_env`
- 适配器：`then`、`upon_error`、`upon_stopped`、`let_value`、`let_error`、`let_stopped`
- 调度器适配器：`starts_on`、`continues_on`（schedule_from）、`bulk`（串行）
- 组合器：`into_variant`、`when_all`（完整笛卡尔积签名）、`split`、`ensure_started`、`start_detached`
- 消费者：`sync_wait`、`sync_wait_with_variant`（均通过 `std::this_thread`）
- Stopped 工具：`stopped_as_optional`、`stopped_as_error`
- 调度器：`inline_scheduler`、`run_loop`（mutex+cv，跨工具链可移植）
- Stop tokens：`inplace_stop_source/token/callback`、`never_stop_token`、`any_stop_token`（类型擦除）、stoppable concepts
- Coroutine 桥：`as_awaitable`、`with_awaitable_senders`（需要 C++20 coroutines）
- 基础设施：`enable_sender`、`get_completion_scheduler`、`sender_adaptor_closure` CRTP、`transform_completion_signatures`、SBO+堆存储抽象

**未实现：** `async_scope`、`counting_scope`、类型擦除 sender、domain-based 调度

> CPO 调度内部使用 `tag_invoke`（不对外暴露），Phase 3+ 新增类型使用成员函数优先分发。当原生 `<execution>` 可用时，整个 backport 自动禁用。

> 某些 libstdc++/PSTL 发行版中，`<execution>`（并行策略实现）在链接期可能需要 `tbb`。Forge 的 tests/examples 会在检测到 `tbb` 时自动链接。

## `std::linalg` 说明

当前为 P1673R13 的完整串行 backport，覆盖 BLAS Level 1/2/3：

**BLAS Level 1：** `copy`、`scale`、`swap_elements`、`add`、`dot`、`dotc`、`vector_two_norm`、`vector_abs_sum`、`vector_idx_abs_max`、`vector_sum_of_squares`、`givens_rotation_setup`、`givens_rotation_apply`

**BLAS Level 2：** `matrix_vector_product`、`triangular_matrix_vector_product`、`triangular_matrix_vector_solve`、`symmetric_matrix_vector_product`、`hermitian_matrix_vector_product`、`matrix_rank_1_update`/`_c`、`symmetric_matrix_rank_1/2_update`

**BLAS Level 3：** `matrix_product`、`triangular_matrix_product`、`triangular_matrix_matrix_left_solve`、`symmetric_matrix_product`、`symmetric_matrix_rank_k/2k_update`

**辅助组件：** `scaled`/`conjugated`/`transposed`/`conjugate_transposed` 视图函数、`scaled_accessor`、`conjugated_accessor`、`layout_transpose`、`layout_blas_packed`、标记类型（`upper_triangle`/`lower_triangle`/`column_major`/`row_major` 等）

> 依赖 C++23 `<mdspan>`，在无 `<mdspan>` 的工具链上（如 GCC 13）优雅跳过。当原生 `<linalg>` 可用时（`__cpp_lib_linalg >= 202311`），backport 自动禁用。未实现 execution policy 重载（纯串行实现，不链接系统 BLAS）。

**SIMD 加速：** BLAS Level 1 归约操作（`dot`、`vector_two_norm`、`vector_abs_sum`）在 Forge `std::simd` 可用时自动使用 SIMD 加速路径。支持全部非复数标准算术类型。已在 x86_64（原生）、aarch64、riscv64、loongarch64 四个架构上通过 zig 交叉编译 + qemu 验证。

## `std::simd` 说明

- 当前 `std::simd` backport 已提供并验证核心公开表面与示例构建。
- 在完整 wording 覆盖继续外扩前，backport 不主动定义 `__cpp_lib_simd`，避免过度宣称标准支持级别。

## 编码规范

- **文件编码**：UTF-8 without BOM（符合 C++23 标准）
- **代码注释**：仅使用英文
- **许可证**：MIT License (2026)

## 文档

- [示例](example/) - 使用示例

## 参考实现

本项目的 backport 实现参考了以下开源项目：

- **std::simd**: [std-simd](https://github.com/VcDevel/std-simd) - Matthias Kretz 的参考实现
- **std::linalg**: [kokkos/stdBLAS](https://github.com/kokkos/stdBLAS) - P1673 参考实现
- **std::execution**: [NVIDIA/stdexec](https://github.com/NVIDIA/stdexec) - P2300 参考实现
- **SIMD 优化**: [ncnn](https://github.com/Tencent/ncnn) - 腾讯的高性能神经网络推理框架

感谢这些项目的贡献者。

## 许可证

MIT License - 详见 LICENSE 文件

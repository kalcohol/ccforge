# `std::linalg` (P1673R13) Backport 设计需求文档

---

## 1. 概述

### 1.1 提案简介

P1673R13 "A free function linear algebra interface based on the BLAS" 为 C++26 标准库引入了一套基于 `std::mdspan` 的线性代数自由函数接口。该提案将经典 BLAS (Basic Linear Algebra Subprograms) 的三个级别操作映射为类型安全的 C++ 模板函数，定义于头文件 `<linalg>` 中，所有符号位于 `namespace std::linalg`。

核心设计理念：
- 以 `std::mdspan` 作为统一的多维数组视图，替代传统 BLAS 的裸指针 + 维度参数
- 通过 accessor 和 layout 的组合表达 scaled、conjugated、transposed 等变换，零开销抽象
- 支持 `std::execution` 并行执行策略重载
- 不拥有数据，不分配内存——纯计算接口

### 1.2 与 Forge 项目的关系

Forge 已提供 `std::simd` (P1928) backport，`std::linalg` 是其自然延伸——两者共同构成 C++26 数值计算基础设施。`std::linalg` 的内层循环可利用 `std::simd` 进行向量化加速，形成协同效应。

此外，`std::linalg` 强依赖 `std::mdspan`（C++23 已标准化）和 `std::submdspan`（P2630，Forge 计划中的 backport，参见 `require/01-std-submdspan-require.md`），三者构成完整的多维数组 + 线性代数技术栈。

### 1.3 目标

- 提供完整的 BLAS Level 1/2/3 操作的 C++26 标准接口 backport
- 符合 Forge 核心原则——无感过渡：API 与 P1673R13 最终标准完全一致
- Header-only 实现，最低要求 C++23
- 通过 `forge.cmake` 注入机制透明集成

---

## 2. API 表面

所有函数定义于 `namespace std::linalg`，头文件 `<linalg>`。

### 2.1 辅助组件

#### 2.1.1 布局与三角标记

| 组件 | 说明 |
|------|------|
| `layout_blas_packed` | 打包存储布局（对称/三角矩阵的紧凑存储） |
| `column_major_t` / `column_major` | 列主序标记 |
| `row_major_t` / `row_major` | 行主序标记 |
| `upper_triangle_t` / `upper_triangle` | 上三角标记 |
| `lower_triangle_t` / `lower_triangle` | 下三角标记 |
| `implicit_unit_diagonal_t` / `implicit_unit_diagonal` | 隐式单位对角线标记 |
| `explicit_diagonal_t` / `explicit_diagonal` | 显式对角线标记 |

#### 2.1.2 变换 Accessor 与工具

| 组件 | 说明 |
|------|------|
| `scaled_accessor<ScalingFactor, NestedAccessor>` | 缩放访问器，元素读取时乘以标量 |
| `conjugated_accessor<NestedAccessor>` | 共轭访问器，元素读取时取共轭 |
| `layout_transpose` | 转置布局映射 |
| `scaled(alpha, x)` | 返回带 `scaled_accessor` 的 `mdspan` |
| `conjugated(x)` | 返回带 `conjugated_accessor` 的 `mdspan` |
| `conjugate_transposed(x)` | 返回共轭转置的 `mdspan` |
| `transposed(x)` | 返回转置的 `mdspan` |

### 2.2 BLAS Level 1（向量-向量操作）

| 函数 | 签名概要 | 说明 |
|------|----------|------|
| `givens_rotation_setup` | `(a, b) -> {c, s, r}` | 计算 Givens 旋转参数 |
| `givens_rotation_apply` | `(x, y, c, s)` | 应用 Givens 旋转 |
| `swap_elements` | `(x, y)` | 交换两个向量/矩阵的元素 |
| `scale` | `(alpha, x)` | 原地缩放：`x = alpha * x` |
| `copy` | `(x, y)` | 复制：`y = x` |
| `add` | `(x, y, z)` | 向量加法：`z = x + y` |
| `dot` | `(x, y) -> scalar` | 点积：`result = x^T * y` |
| `dotc` | `(x, y) -> scalar` | 共轭点积：`result = conj(x)^T * y` |
| `vector_sum_of_squares` | `(x, init) -> {scale, sumsq}` | 平方和的缩放表示 |
| `vector_two_norm` | `(x) -> scalar` | 二范数：`||x||_2` |
| `vector_abs_sum` | `(x) -> scalar` | 绝对值之和：`||x||_1` |
| `vector_idx_abs_max` | `(x) -> index` | 绝对值最大元素的索引 |

所有函数均有带 `ExecutionPolicy&&` 首参数的并行重载。

### 2.3 BLAS Level 2（矩阵-向量操作）

| 函数 | 说明 |
|------|------|
| `matrix_vector_product(A, x, y)` | 一般矩阵-向量乘：`y = A * x` |
| `matrix_vector_product(A, x, y, z)` | 更新形式：`z = y + A * x` |
| `symmetric_matrix_vector_product` | 对称矩阵-向量乘 |
| `hermitian_matrix_vector_product` | Hermitian 矩阵-向量乘 |
| `triangular_matrix_vector_product` | 三角矩阵-向量乘（原地和非原地） |
| `triangular_matrix_vector_solve` | 三角求解：`A * x = b` |
| `matrix_rank_1_update(x, y, A)` | 秩-1 更新：`A += x * y^T` |
| `matrix_rank_1_update_c(x, y, A)` | 共轭秩-1 更新：`A += x * conj(y)^T` |
| `symmetric_matrix_rank_1_update` | 对称秩-1 更新 |
| `hermitian_matrix_rank_1_update` | Hermitian 秩-1 更新 |
| `symmetric_matrix_rank_2_update` | 对称秩-2 更新 |
| `hermitian_matrix_rank_2_update` | Hermitian 秩-2 更新 |

每个函数均有 `Triangle` 模板参数（`upper_triangle_t` / `lower_triangle_t`）和可选的 `DiagonalStorage` 参数。所有函数均有 `ExecutionPolicy&&` 并行重载。

### 2.4 BLAS Level 3（矩阵-矩阵操作）

| 函数 | 说明 |
|------|------|
| `matrix_product(A, B, C)` | 一般矩阵乘：`C = A * B` |
| `matrix_product(A, B, E, C)` | 更新形式：`C = E + A * B` |
| `symmetric_matrix_product` | 对称矩阵乘（左乘/右乘） |
| `hermitian_matrix_product` | Hermitian 矩阵乘（左乘/右乘） |
| `triangular_matrix_product` | 三角矩阵乘 |
| `triangular_matrix_left_product` | 原地左乘三角矩阵 |
| `triangular_matrix_right_product` | 原地右乘三角矩阵 |
| `matrix_rank_k_update` | 秩-k 更新：`C += A * A^T` |
| `symmetric_matrix_rank_k_update` | 对称秩-k 更新 |
| `hermitian_matrix_rank_k_update` | Hermitian 秩-k 更新 |
| `matrix_rank_2k_update` | 秩-2k 更新：`C += A * B^T + B * A^T` |
| `symmetric_matrix_rank_2k_update` | 对称秩-2k 更新 |
| `hermitian_matrix_rank_2k_update` | Hermitian 秩-2k 更新 |
| `triangular_matrix_matrix_left_solve` | 三角矩阵左求解：`A * X = B` |
| `triangular_matrix_matrix_right_solve` | 三角矩阵右求解：`X * A = B` |

所有函数均有 `ExecutionPolicy&&` 并行重载。

### 2.5 完整函数计数

| 类别 | 函数数量 | 含并行重载后 |
|------|----------|-------------|
| 辅助组件 | 7 类型 + 4 函数 | — |
| BLAS Level 1 | 12 | ~24 |
| BLAS Level 2 | 12 | ~24 |
| BLAS Level 3 | 13 | ~26 |
| **合计** | **37+ 函数** | **~74 重载** |

---

## 3. 依赖分析

### 3.1 `std::mdspan` (C++23, P0009)

`std::linalg` 的所有函数以 `std::mdspan` 作为参数类型，这是最核心的依赖。

- **状态**：C++23 已标准化，主流编译器（GCC 14+、Clang 17+、MSVC 17.10+）均已提供 `<mdspan>`
- **Forge 影响**：zig/LLVM 工具链（Clang-based）应已支持。若目标编译器缺失，需额外 backport 或依赖第三方实现（如 [kokkos/mdspan](https://github.com/kokkos/mdspan)）
- **关键类型**：`mdspan`, `extents`, `layout_right`, `layout_left`, `layout_stride`, `default_accessor`

### 3.2 `std::submdspan` (P2630, C++26)

BLAS Level 2/3 的实现中，子矩阵/子向量提取依赖 `std::submdspan`。

- **状态**：C++26 提案，尚未被主流编译器实现
- **Forge 计划**：已列入 backport 计划，需求文档见 `require/01-std-submdspan-require.md`
- **依赖程度**：中等——Level 1 操作不需要，Level 2/3 的分块算法和内部实现需要
- **替代方案**：初期可通过手动索引计算绕过，但会增加实现复杂度

### 3.3 `std::execution` 并行策略

P1673R13 的所有计算函数均提供带 `ExecutionPolicy&&` 首参数的并行重载。

- **状态**：`std::execution::seq`, `par`, `par_unseq` 自 C++17 起可用（`<execution>` 头文件）
- **编译器支持**：
  - GCC：通过 TBB 后端支持
  - Clang/libc++：部分支持（`seq` 可用，`par` 支持有限）
  - MSVC：完整支持
- **Forge 影响**：zig/LLVM 工具链使用 libc++，并行策略支持可能不完整
- **策略**：串行实现为必须，并行重载可作为可选功能，在检测到 `<execution>` 可用时启用

### 3.4 `std::simd` (P1928, Forge 现有 backport)

虽然 P1673R13 标准文本不直接依赖 `std::simd`，但 backport 实现可利用 SIMD 加速内层循环。

- **状态**：Forge 已有 backport（`backport/cpp26/simd.hpp`）
- **集成方式**：实现内部可选使用，不影响 API 表面
- **适用场景**：`dot`, `vector_two_norm`, `scale`, `matrix_vector_product`, `matrix_product` 等热点函数

### 3.5 依赖关系图

```
std::linalg (P1673)
├── std::mdspan (C++23)        ← 核心依赖，必须可用
├── std::submdspan (P2630)     ← Level 2/3 实现需要，Forge 计划 backport
├── std::execution (C++17)     ← 并行重载可选
├── std::complex (C++98)       ← Hermitian/共轭操作需要
└── std::simd (P1928)          ← 可选性能加速，Forge 已有 backport
```

---

## 4. 实现策略

### 4.1 分阶段计划

#### Phase 0：基础设施（预计 1-2 周）

- 实现辅助组件：`scaled_accessor`, `conjugated_accessor`, `layout_transpose`, `layout_blas_packed`
- 实现标记类型：`column_major_t`, `row_major_t`, `upper_triangle_t`, `lower_triangle_t`, `implicit_unit_diagonal_t`, `explicit_diagonal_t`
- 实现工具函数：`scaled()`, `conjugated()`, `transposed()`, `conjugate_transposed()`
- 建立测试框架和编译探测

#### Phase 1：BLAS Level 1（预计 2-3 周）

- 实现全部 12 个向量-向量操作
- 优先级排序：
  1. `copy`, `scale`, `swap_elements`（基础操作）
  2. `dot`, `dotc`, `add`（算术操作）
  3. `vector_two_norm`, `vector_abs_sum`, `vector_idx_abs_max`（归约操作）
  4. `vector_sum_of_squares`（辅助归约）
  5. `givens_rotation_setup`, `givens_rotation_apply`（旋转操作）
- 此阶段不依赖 `submdspan`，可独立推进

#### Phase 2：BLAS Level 2（预计 3-4 周）

- 实现全部 12 个矩阵-向量操作
- 依赖 Phase 0 的辅助组件和 Phase 1 的基础操作
- 需要 `submdspan` 支持（或手动索引替代）
- 优先级排序：
  1. `matrix_vector_product`（最常用）
  2. `triangular_matrix_vector_product`, `triangular_matrix_vector_solve`
  3. `symmetric_matrix_vector_product`, `hermitian_matrix_vector_product`
  4. 秩更新操作

#### Phase 3：BLAS Level 3（预计 4-6 周）

- 实现全部 13 个矩阵-矩阵操作
- 这是实现量最大、性能最敏感的阶段
- 优先级排序：
  1. `matrix_product`（核心操作，GEMM）
  2. `triangular_matrix_left_product`, `triangular_matrix_right_product`
  3. `symmetric_matrix_product`, `hermitian_matrix_product`
  4. 秩-k/2k 更新操作
  5. 三角求解操作

#### Phase 4：并行策略与优化（预计 2-3 周）

- 添加 `ExecutionPolicy&&` 重载
- 集成 `std::simd` 加速（可选）
- 性能基准测试

### 4.2 Header-Only 约束下的性能考量

Header-only 实现意味着所有代码在用户编译单元中实例化，这带来几个挑战：

1. **编译时间**：大量模板实例化会显著增加编译时间。缓解策略：
   - 使用 `if constexpr` 而非 SFINAE 减少实例化数量
   - 合理拆分内部实现头文件，用户只需包含 `<linalg>`

2. **代码膨胀**：每个翻译单元可能生成重复的实例化。缓解策略：
   - 标记内部函数为 `inline`（header-only 已隐含）
   - 利用编译器的 COMDAT folding

3. **优化机会**：header-only 反而有利于内联和 LTO：
   - 编译器可以看到完整实现，有更多优化空间
   - 内层循环可被自动向量化

### 4.3 与 `std::simd` Backport 的可选集成

集成策略：在实现内部通过编译期检测决定是否使用 SIMD 加速。

```cpp
// 内部实现示例（非 API 表面）
namespace std::linalg::__detail {

template<class T>
constexpr bool __use_simd_v =
    std::is_arithmetic_v<T> && requires { typename std::simd<T>; };

template<class T>
auto __dot_impl(mdspan<T, ...> x, mdspan<T, ...> y) {
    if constexpr (__use_simd_v<T>) {
        // SIMD accelerated path
    } else {
        // Scalar fallback
    }
}

} // namespace std::linalg::__detail
```

适合 SIMD 加速的函数：
- `dot` / `dotc`：归约操作，SIMD 友好
- `vector_two_norm` / `vector_abs_sum`：归约操作
- `scale` / `copy` / `add`：逐元素操作
- `matrix_vector_product`：内层循环可向量化
- `matrix_product`：微内核可用 SIMD 实现

### 4.4 与 `forge.cmake` 注入机制的集成

遵循现有模式：

1. 在 `forge.cmake` 中添加 `std::linalg` 的特性检测：
```cmake
check_cxx_source_compiles("
    #include <linalg>
    int main() {
        double data[] = {1.0, 2.0, 3.0};
        std::mdspan v(data, 3);
        auto n = std::linalg::vector_two_norm(v);
        return static_cast<int>(n);
    }
" HAS_STD_LINALG)
```

2. 创建 backport wrapper 头文件 `backport/linalg`：
```cpp
#pragma once
// Include real <linalg> if available, then conditionally include backport
#if defined(_MSC_VER)
    // MSVC path resolution
#elif defined(__has_include_next)
    #include_next <linalg>
#endif

#ifndef __cpp_lib_linalg
#include "cpp26/linalg.hpp"
#endif
```

3. 实现文件放置于 `backport/cpp26/linalg.hpp`（可拆分为多个内部头文件）

### 4.5 实现规模估算

| 组件 | 估算代码行数 |
|------|-------------|
| 辅助组件（accessor、layout、标记） | 800-1200 |
| BLAS Level 1 | 600-900 |
| BLAS Level 2 | 1500-2500 |
| BLAS Level 3 | 2500-4000 |
| 并行策略重载 | 300-500 |
| 内部工具（类型萃取、概念约束） | 400-600 |
| **合计** | **6100-9700** |

测试代码预计与实现代码量相当或更多。

---

## 5. 风险与开放问题

### 5.1 Header-Only 实现的性能上限

**风险等级：高**

`std::linalg` 的核心价值之一是性能。硬件优化的 BLAS 库（OpenBLAS、MKL、BLIS）经过数十年调优，在 GEMM 等操作上接近硬件理论峰值。Header-only 的纯 C++ 实现在以下方面存在性能差距：

- **GEMM 微内核**：硬件 BLAS 使用手写汇编微内核，利用特定 CPU 的寄存器文件大小和缓存层次。纯 C++ 实现依赖编译器自动向量化，性能通常为硬件 BLAS 的 30-70%
- **缓存分块**：最优分块参数依赖具体硬件，header-only 实现只能使用启发式值
- **多线程**：并行策略的实际效果取决于运行时线程池实现

**缓解措施**：
- P1673R13 的设计目标是提供标准接口，而非替代硬件 BLAS。标准库实现可以在内部委托给硬件 BLAS
- Forge backport 的目标是 API 正确性和可移植性，性能为次要目标
- 对于性能敏感场景，用户可在标准库原生支持后自动切换到厂商优化实现

### 5.2 Execution Policy 依赖

**风险等级：中**

- libc++（zig/LLVM 工具链）对 `<execution>` 的支持不完整，`std::execution::par` 可能不可用
- C++26 可能引入基于 P2300 (std::execution) 的新并行模型，与 C++17 execution policy 的关系尚不明确

**缓解措施**：
- 串行实现为默认，不依赖 `<execution>`
- 并行重载通过 `#if __has_include(<execution>)` 条件编译
- 预留 P2300 sender/receiver 集成接口的扩展点（但不暴露为 API）

### 5.3 复数类型支持

**风险等级：中**

Hermitian 操作和 `dotc`、`conjugated_accessor` 等组件需要复数类型支持。

- `std::complex<T>` 的 `constexpr` 支持在 C++23 中仍有限制
- `conjugated_accessor` 需要对 `std::complex` 进行特化
- 某些编译器对 `std::complex` 的 SIMD 支持不完整

**缓解措施**：
- 优先实现实数类型路径，复数类型作为第二优先级
- `conjugated_accessor` 对非复数类型应为恒等操作（P1673R13 已规定）

### 5.4 `std::mdspan` 可用性

**风险等级：低**

Forge 目标为 C++23，`std::mdspan` 是 C++23 标准组件。但：

- 某些编译器版本可能提供不完整的 `mdspan` 实现
- `layout_stride` 的某些边界情况可能存在编译器 bug

**缓解措施**：
- 在 `forge.cmake` 中添加 `mdspan` 可用性检测
- 必要时可依赖 kokkos/mdspan 参考实现作为 fallback

### 5.5 与硬件 BLAS 库的关系

**风险等级：低（设计层面）**

P1673R13 明确设计为可委托给硬件 BLAS 的接口。Forge backport 不打算委托给硬件 BLAS（这会破坏 header-only 约束），但需确保：

- API 签名与标准完全一致，使得标准库实现可以无缝替换
- 不引入任何阻止硬件 BLAS 委托的设计决策

### 5.6 `<linalg>` 头文件注入的特殊性

**风险等级：中**

与 `<memory>` 不同，`<linalg>` 是全新头文件，在 C++23 编译器中不存在。这意味着：

- `#include_next <linalg>` 在没有原生支持时会编译失败
- backport wrapper 需要处理"原生头文件不存在"的情况

**缓解措施**：
- 使用 `__has_include(<linalg>)` 检测原生头文件是否存在
- 若不存在，backport wrapper 直接提供完整实现，无需 `include_next`
- 这与 `<memory>` 的注入模式不同（`<memory>` 总是存在，只是缺少 `unique_resource`），需要在 `forge.cmake` 中特殊处理

---

## 6. 建议

### 6.1 优先级评估

`std::linalg` 是 Forge 目前规模最大的潜在 backport，实现量约为 `unique_resource` 的 10-15 倍。建议按以下优先级推进：

1. **先完成 `std::submdspan` backport**——`std::linalg` 的 Level 2/3 操作强依赖 `submdspan`，且 `submdspan` 本身规模较小、价值独立
2. **Phase 0 + Phase 1 可与 `submdspan` 并行开发**——辅助组件和 BLAS Level 1 不依赖 `submdspan`
3. **Phase 2/3 在 `submdspan` 就绪后启动**

### 6.2 是否建议立即开始

**建议：暂不立即全面启动，但可开始 Phase 0。**

理由：
- `submdspan` backport 尚未完成，是关键前置依赖
- Phase 0（辅助组件）不依赖 `submdspan`，可立即开始，且能验证 `<linalg>` 头文件注入机制
- Phase 1（BLAS Level 1）也不依赖 `submdspan`，可在 Phase 0 完成后紧接推进
- 全面启动（Phase 2/3）应等待 `submdspan` backport 至少达到可用状态

### 6.3 参考实现

建议参考以下现有实现：
- [kokkos/stdBLAS](https://github.com/kokkos/stdBLAS)——P1673 提案作者维护的参考实现，与标准文本最为一致
- [DALG (Data-parallel Algorithms)](https://github.com/mattkretz/stdx-simd)——展示了 `simd` 与线性代数的集成方式

### 6.4 测试策略

- **正确性测试**：对每个函数使用已知结果的小规模矩阵验证（与手算或 NumPy 结果对比）
- **编译探测**：验证所有函数签名、约束条件、SFINAE/concepts 行为
- **类型覆盖**：`float`, `double`, `std::complex<float>`, `std::complex<double>`, 以及整数类型
- **布局覆盖**：`layout_right`, `layout_left`, `layout_stride`, `layout_blas_packed`
- **边界情况**：零维向量、1x1 矩阵、非方阵、非连续 stride

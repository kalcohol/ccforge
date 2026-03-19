# `std::submdspan` (P2630) Backport 设计需求文档

## 1. 概述

### 1.1 提案简介

P2630R4 "Submdspan" 定义了 `std::submdspan()` 函数模板，用于从现有 `std::mdspan` 中提取子视图（subview）。该功能在 C++23 标准化 `mdspan` 时被有意推迟，最终纳入 C++26 工作草案。

`submdspan` 是多维数组编程的基础操作——类似于一维 `std::span` 的 `subspan()`，但支持在每个维度上独立指定切片方式（整数索引、范围、步进范围、全范围）。LAPACK 等线性代数库的分块算法（Cholesky、LU、QR 分解）高度依赖子视图复用来实现高性能。

### 1.2 相关提案演进

| 提案 | 内容 | 状态 |
|------|------|------|
| P2630R4 | submdspan 核心规范 | 已纳入 C++26 WD |
| P3355R1/R2 | 修复 submdspan 布局保持行为（unit-stride slice） | 已纳入 C++26 WD |
| P3982R0 | 修复 `strided_slice::extent` 语义（从输入跨度改为输出元素数） | 进行中 |
| P3663R3 | submdspan_mapping 前向兼容（slice 规范化） | LEWG 已转发至 LWG |

### 1.3 与 Forge 项目的关系

- `std::linalg`（P1673R13）是 C++26 的线性代数库，其内部大量使用 `submdspan` 进行矩阵分块操作。若 Forge 未来计划 backport `std::linalg`，则 `submdspan` 是必要前置条件。
- `submdspan` 本身也是独立有用的——任何使用 `mdspan` 的数值计算代码都可能需要子视图提取。

## 2. API 表面

所有符号位于 `<mdspan>` 头文件中，`namespace std` 下。

### 2.1 核心函数模板

```cpp
// 从现有 mdspan 提取子视图
template<class ElementType, class Extents, class LayoutPolicy,
         class AccessorPolicy, class... SliceSpecifiers>
constexpr auto submdspan(
    const mdspan<ElementType, Extents, LayoutPolicy, AccessorPolicy>& src,
    SliceSpecifiers... slices) -> /* see below */;

// 计算子视图的 extents
template<class IndexType, class... Extents, class... SliceSpecifiers>
constexpr auto submdspan_extents(
    const extents<IndexType, Extents...>& e,
    SliceSpecifiers... slices) -> /* see below */;
```

### 2.2 Slice 指定器类型

```cpp
// 全范围标记（已在 C++23 mdspan 中定义）
struct full_extent_t { explicit full_extent_t() = default; };
inline constexpr full_extent_t full_extent{};

// 步进切片
template<class OffsetType, class ExtentType, class StrideType>
struct strided_slice {
    [[no_unique_address]] OffsetType offset{};
    [[no_unique_address]] ExtentType extent{};
    [[no_unique_address]] StrideType stride{};
};
```

合法的 slice 指定器包括：

| 类型 | 效果 | 对结果秩的影响 |
|------|------|----------------|
| 整数值 | 选取单个索引 | 秩 -1 |
| `pair<IndexType, IndexType>` 或 tuple-like | 连续范围 `[first, second)` | 秩不变 |
| `full_extent_t` | 整个维度 | 秩不变 |
| `strided_slice<O, E, S>` | 步进范围 | 秩不变 |

### 2.3 辅助类型

```cpp
// submdspan_mapping 的返回类型
template<class LayoutMapping>
struct submdspan_mapping_result {
    [[no_unique_address]] LayoutMapping mapping = LayoutMapping();
    size_t offset{};
};
```

### 2.4 自定义扩展点

```cpp
// 通过 ADL 查找，为自定义布局提供 submdspan 支持
// 标准布局（layout_left, layout_right, layout_stride）已内置实现
template<class... SliceSpecifiers>
friend constexpr auto submdspan_mapping(
    const mapping& src, SliceSpecifiers... slices)
    -> submdspan_mapping_result</* deduced */>;
```

标准库为以下布局提供内置 `submdspan_mapping`：
- `layout_left::mapping`
- `layout_right::mapping`
- `layout_stride::mapping`
- `layout_left_padded<PaddingValue>::mapping`（C++26）
- `layout_right_padded<PaddingValue>::mapping`（C++26）

### 2.5 特性测试宏

```cpp
#define __cpp_lib_submdspan 202411L  // <mdspan>
```

### 2.6 与 mdspan 核心类型的交互

`submdspan` 依赖以下 C++23 已标准化的类型：
- `std::mdspan<ElementType, Extents, LayoutPolicy, AccessorPolicy>`
- `std::extents<IndexType, Extents...>`
- `std::dextents<IndexType, Rank>`
- `std::layout_left`, `std::layout_right`, `std::layout_stride`
- `std::default_accessor<ElementType>`

## 3. 依赖分析

### 3.1 `std::mdspan` 可用性

| 组件 | 标准版本 | libc++ 状态 | 特性测试宏 |
|------|----------|-------------|------------|
| `std::mdspan` (P0009R18) | C++23 | libc++ 18 已完成 | `__cpp_lib_mdspan` = 202406L |
| `std::submdspan` (P2630R4) | C++26 | 截至 libc++ 22 **未实现** | `__cpp_lib_submdspan` = 202411L |

关键结论：`std::mdspan` 在 C++23 中已标准化且 libc++ 18+ 已完整实现。Forge 的目标工具链（zig/LLVM 21）已包含完整的 `mdspan` 支持，**无需同时 backport mdspan**。

### 3.2 `std::submdspan` 可用性

经查证，截至 libc++ 22（LLVM 22，2026 年 3 月），`std::submdspan` **尚未在 libc++ 中实现**。libc++ 21 和 22 的 Release Notes 中均无 P2630 相关条目。

这意味着 Forge 的目标用户在可预见的未来（至少到 LLVM 23）无法通过标准库获得 `submdspan`，backport 具有实际价值。

### 3.3 与 `forge.cmake` 检测机制的集成

现有检测模式可直接复用：

```cmake
# 检测 std::submdspan
check_cxx_source_compiles("
    #include <mdspan>
    int main() {
        int data[12]{};
        std::mdspan<int, std::extents<int, 3, 4>> m(data);
        auto sub = std::submdspan(m, 1, std::full_extent);
        (void)sub;
        return 0;
    }
" HAS_STD_SUBMDSPAN)
```

注入方式与现有 backport 不同——`submdspan` 位于 `<mdspan>` 头文件中，而非独立头文件。需要新建 `backport/mdspan` wrapper：

```
backport/
├── mdspan              ← 新增 wrapper header
├── cpp26/
│   └── submdspan.hpp   ← backport 实现
├── memory
├── simd
└── ...
```

`backport/mdspan` 的逻辑：
1. `#include_next <mdspan>`（获取标准库的 mdspan 实现）
2. 检查 `__cpp_lib_submdspan`，若未定义则 `#include "cpp26/submdspan.hpp"`

### 3.4 padded 布局依赖

C++26 新增了 `layout_left_padded` 和 `layout_right_padded`（P2642）。`submdspan` 的完整规范包含对这两种布局的 `submdspan_mapping` 支持。

| 布局 | 标准版本 | 是否必须支持 |
|------|----------|-------------|
| `layout_left` | C++23 | 是（核心） |
| `layout_right` | C++23 | 是（核心） |
| `layout_stride` | C++23 | 是（核心） |
| `layout_left_padded` | C++26 | 可延后 |
| `layout_right_padded` | C++26 | 可延后 |

建议：第一阶段仅实现三种 C++23 标准布局的支持，padded 布局作为后续扩展。

## 4. 实现策略

### 4.1 参考实现

[kokkos/mdspan](https://github.com/kokkos/mdspan) 是 `mdspan` 和 `submdspan` 的官方参考实现（由提案作者维护）。该仓库包含完整的 `submdspan` 实现，可作为主要参考。

### 4.2 分阶段计划

**第一阶段：核心功能**
- `submdspan()` 函数模板
- `submdspan_extents()` 函数模板
- `strided_slice` 类模板
- `submdspan_mapping_result` 类模板
- `layout_left`, `layout_right`, `layout_stride` 的 `submdspan_mapping`
- 特性测试宏 `__cpp_lib_submdspan`

**第二阶段：扩展布局**
- `layout_left_padded`, `layout_right_padded` 的 `submdspan_mapping`
- 需要同时 backport 这两种布局类型（P2642）

**第三阶段：规范修正**
- P3355 unit-stride slice 优化（布局保持）
- P3982 `strided_slice::extent` 语义修正（若最终纳入标准）
- P3663 slice 规范化（前向兼容）

### 4.3 技术难点

**4.3.1 编译期维度计算**

`submdspan_extents` 需要在编译期根据 slice 指定器类型推导结果 `extents`。每个维度的静态/动态性取决于 slice 类型：
- 整数索引 → 该维度消失（降秩）
- `full_extent_t` → 保持原始静态 extent
- `pair` → 动态 extent（除非两端都是 `integral_constant`）
- `strided_slice` → 动态 extent（除非所有成员都是 `integral_constant`）

这需要大量 `constexpr` 和模板元编程，是实现中最复杂的部分。

**4.3.2 布局映射推导**

`submdspan_mapping` 的返回布局类型取决于 slice 组合：

| 输入布局 | slice 组合 | 输出布局 |
|----------|-----------|----------|
| `layout_left` | 前缀全为 range/full_extent | `layout_left` |
| `layout_left` | 其他 | `layout_stride` |
| `layout_right` | 后缀全为 range/full_extent | `layout_right` |
| `layout_right` | 其他 | `layout_stride` |
| `layout_stride` | 任意 | `layout_stride` |

P3355 进一步细化：`strided_slice` 若步长为编译期 1（unit-stride），等价于 range slice，可保持原始布局。

**4.3.3 namespace std 注入**

与现有 backport 一致，所有符号必须注入 `namespace std`。由于 `submdspan` 的类型（如 `strided_slice`）是全新的，不存在与标准库已有符号冲突的风险。但 `full_extent_t` 已在 C++23 `<mdspan>` 中定义，backport 必须检测并避免重复定义。

### 4.4 实现规模估算

| 组件 | 估算行数 | 复杂度 |
|------|----------|--------|
| `strided_slice` | ~30 | 低 |
| `submdspan_mapping_result` | ~15 | 低 |
| `submdspan_extents` | ~120 | 高 |
| `submdspan_mapping`（三种标准布局） | ~250 | 高 |
| `submdspan` 主函数 | ~60 | 中 |
| 辅助 traits / 内部工具 | ~150 | 中 |
| **合计** | **~625** | |

相比 `unique_resource`（~400 行），`submdspan` 的实现规模更大，模板元编程密度更高。

## 5. 风险与开放问题

### 5.1 标准仍在演进

`submdspan` 的规范在 C++26 定稿前仍有活跃修正：
- **P3982R0**（2026 年 1 月）重新定义了 `strided_slice::extent` 的语义，从"输入跨度"改为"输出元素数"。这是一个破坏性变更，若最终纳入标准，将影响 backport 的 API 行为。
- **P3663R3**（2025 年 10 月）提议对 slice 进行规范化以实现前向兼容。该提案已通过 LEWG 审查，但尚未确认纳入 C++26。

**风险等级：中**。在 C++26 正式发布（预计 2026 年底）前，API 语义可能还会调整。

### 5.2 libc++ 版本碎片化

不同 LLVM 版本的 `<mdspan>` 实现可能存在细微差异：
- libc++ 17：`mdspan` 部分实现
- libc++ 18+：`mdspan` 完整实现
- `__cpp_lib_mdspan` 宏值可能因版本不同而不同

backport 应以 `__cpp_lib_mdspan >= 202207L`（C++23 基线值）作为 mdspan 可用性的最低要求。

### 5.3 自定义布局的 submdspan_mapping 扩展点

`submdspan_mapping` 通过 ADL 查找，允许用户为自定义布局提供实现。backport 需要确保：
- ADL 机制正确工作（符号在 `namespace std` 中）
- 不阻止用户自定义扩展
- `submdspan_mapping_result` 模板可与用户自定义 mapping 类型配合

### 5.4 与 MSVC 的兼容性

MSVC 的 `<mdspan>` 实现进度与 libc++ 不同。Forge 现有的 MSVC 头文件路径解析机制（`FORGE_MSVC_MDSPAN_HEADER`）需要扩展以支持 `<mdspan>` wrapper。但鉴于 Forge 当前主要目标是 zig/LLVM 工具链，MSVC 支持可作为低优先级。

### 5.5 测试覆盖复杂度

`submdspan` 的测试矩阵非常大：
- 3 种标准布局 × 多种 slice 组合 × 静态/动态 extent × 不同秩
- 需要验证返回布局类型的正确性
- 需要验证数据指针偏移的正确性
- 需要验证编译期 extent 推导的正确性

建议参考 kokkos/mdspan 仓库的测试用例。

## 6. 建议

### 6.1 是否建议立即开始 backport

**建议：暂缓，等待标准稳定后再启动。**

理由：
1. **标准仍在演进**——P3982（strided_slice 语义变更）和 P3663（slice 规范化）均为活跃提案，可能在 2026 年内纳入 C++26 最终草案。过早实现可能导致返工。
2. **实现复杂度高**——模板元编程密集，约 625 行核心代码，测试矩阵庞大。
3. **当前无紧迫需求**——除非 Forge 立即启动 `std::linalg` backport，否则 `submdspan` 的独立使用场景有限。

### 6.2 建议的启动时机

- C++26 CD（Committee Draft）发布后（预计 2026 年中），核心 API 基本冻结时启动
- 或者当 Forge 确认启动 `std::linalg` backport 时，作为前置依赖启动

### 6.3 优先级评估

| 因素 | 评分 | 说明 |
|------|------|------|
| 用户需求 | 中 | 数值计算用户需要，但非通用需求 |
| 标准稳定性 | 低 | 仍有活跃修正提案 |
| 实现难度 | 高 | 模板元编程密集 |
| 前置依赖价值 | 高 | linalg backport 的必要前置 |
| **综合优先级** | **中** | 建议在标准稳定后作为 linalg 前置启动 |

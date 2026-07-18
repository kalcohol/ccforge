# `std::submdspan` / `std::constant_wrapper` backport 说明

当前对齐 2026-05 C++26 working draft 的 `submdspan` surface；
`constant_wrapper` 采用 P2781 operator algebra、P3978 call/subscript 语义与
P4206 DR 后的直接 `template<auto>` 形状。

## 覆盖范围

Forge 的 `<mdspan>` backport 扩展覆盖：

- `full_extent`
- `extent_slice`
- `range_slice`
- `submdspan_mapping_result`
- `canonical_slices`
- `subextents`
- `layout_left` / `layout_right` / `layout_stride` / `layout_left_padded` /
  `layout_right_padded` 的 `submdspan_mapping`
- `submdspan()` 本体

Forge 同时提供 `std::constant_wrapper`（`<utility>`）和 C++26 padded mdspan layouts
作为 `submdspan` foundation。这些 foundation 由 `forge.cmake` 单独探测，检测到原生或
部分原生声明时会主动让位，避免 ODR 冲突。

对应示例：

- `example/constant_wrapper_example.cpp`：`std::constant_wrapper` / `std::cw` 的
  mixed-wrapper algebra 与 call/subscript constant preservation；
- `example/padded_mdspan_layout_example.cpp`：`layout_left_padded` /
  `layout_right_padded` 的 stride 与 full-slice layout 保留。

Padded layout mapping 的 converting constructor 只在不会改变 mapping 唯一性的形状下
接受相反 layout：rank-1 mapping 可互转，rank>1 的 left-padded 与 right-padded mapping
不会互转。静态 padding stride 也会参与 `is_always_exhaustive()` 判定；动态 padding
仍保守返回 false。

## Feature macros（特性宏）

当 Forge 注入 backport 时定义：

- `__cpp_lib_constant_wrapper = 202606L`
- `__cpp_lib_submdspan = 202603L`

GCC 16 当前提供 `202603L` 的 pre-P4206 native surface。它属于 partial native：
Forge 会警告并让位，不会在同一 `namespace std` 中叠加声明。专用
`constant_wrapper` conformance tests 只在 Forge 注入或检测到完整 `202606L`
native surface 时运行。

## `constant_wrapper` conformance ledger

- 已实现：P4206 的 `constant_wrapper<auto X, class T = decltype(X)>` / `cw<X>`
  类型形状、显式第二类型一致性要求、P2781 运算符与伪变异运算、P3978
  constant/runtime call 和 subscript 分支。
- 已验证：C++23 injected path、Clang/libc++ injected path、MSVC injected path，
  以及 GCC 16 `202603L` partial-native stand-aside。
- 有意不提供：被 P4206 撤回的 `cw-fixed-value` 与字符串字面量兼容 surface。
- native stdlib 只要已经声明任意 partial surface，Forge 就不会覆盖注入；这是
  项目的 ODR 安全策略，不代表该 native 实现满足上述完整 baseline。

## 旧拼写

Forge 不保留早期 P2630-era 的 `strided_slice` / `submdspan_extents` 兼容拼写；新代码应
直接使用当前 draft 的 `extent_slice` / `range_slice` 与 `subextents`。

`forge.cmake` 仍保留 legacy `strided_slice` probe，仅用于检测旧式 partial-native stdlib
并让位；这不是 Forge 公开 API。

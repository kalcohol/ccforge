# `std::submdspan` / `std::constant_wrapper` backport 说明

当前对齐 2026-05 C++26 working draft 的 `submdspan` surface。

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

## Feature macros（特性宏）

当 Forge 注入 backport 时定义：

- `__cpp_lib_constant_wrapper = 202603L`
- `__cpp_lib_submdspan = 202603L`

## 旧拼写

Forge 不保留早期 P2630-era 的 `strided_slice` / `submdspan_extents` 兼容拼写；新代码应
直接使用当前 draft 的 `extent_slice` / `range_slice` 与 `subextents`。

`forge.cmake` 仍保留 legacy `strided_slice` probe，仅用于检测旧式 partial-native stdlib
并让位；这不是 Forge 公开 API。

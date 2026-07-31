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

Portable code 应优先调用 `std::submdspan(...)`。需要直接检查 mapping customization
时，按 working draft 的 hidden-friend 模型使用不限定名
`submdspan_mapping(mapping, slices...)`，让 ADL 找到实现；不要写
`std::submdspan_mapping(...)`。Forge backport 当前保留 namespace-scope overload
作为内部 dispatch extension，但 qualified spelling 不是 native-handoff contract，
原生标准库可以只提供 hidden friends。

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
不会互转；rank-0/rank-1 mapping 也接受相反的未 padded layout。rank>1 的
padded-to-padded 转换还受 extents 的隐式可转换性约束。rank-0/rank-1 在 extents 可隐式
转换时不因 padding 形状而额外收紧；rank>1 只有从 static padding 放宽到 dynamic
padding 时可以 implicit，其余 static-to-static、dynamic-to-dynamic 和
dynamic-to-static 均为 explicit。
同时支持 padded mapping 向对应的 `layout_left` / `layout_right` 以及
`layout_stride` mapping 转换；向紧凑 layout 转换时，调用者必须满足
实际 padding stride 等于相应 extent 的前置条件。
静态 padding stride 也会参与 `is_always_exhaustive()` 判定；动态 padding 仍保守返回
false。

当前 working draft 对 rank-1 padded source 无条件返回紧凑的 `layout_left` /
`layout_right` submapping。这在输入为非单位 `extent_slice` 时无法满足
sliceable-mapping 的逐元素地址等式。Forge 在这一格有意返回 `layout_stride`，保留实际
slice stride；若后续 wording 修正该冲突，再按新的标准结果收敛。

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
- MSVC 当前不能形成以数组左值表达式为模板实参的 `constant_wrapper`；这类
  `operator()` / `operator[]` 结果按 wording 的无效 constant-call 分支回退为
  `constexpr` 数组引用，其他结果仍形成 `constant_wrapper`。
- 有意不提供：被 P4206 撤回的 `cw-fixed-value` 与字符串字面量兼容 surface。
- native stdlib 只要已经声明任意 partial surface，Forge 就不会覆盖注入；这是
  项目的 ODR 安全策略，不代表该 native 实现满足上述完整 baseline。

## 旧拼写

Forge 不保留早期 P2630-era 的 `strided_slice` / `submdspan_extents` 兼容拼写；新代码应
直接使用当前 draft 的 `extent_slice` / `range_slice` 与 `subextents`。

`forge.cmake` 仍保留 legacy `strided_slice` probe，仅用于检测旧式 partial-native stdlib
并让位；这不是 Forge 公开 API。

# `<mdspan>` 扩展与 `std::constant_wrapper` backport 说明

当前对齐 2026-05 C++26 working draft 的 `submdspan` surface；
按 2026-11-26 的版本政策，另将已采纳的 LWG 决议作为 C++26 缺陷报告（DR）处理；
`constant_wrapper` 采用 P2781 operator algebra、P3978 call/subscript 语义与
P4206 DR 后的直接 `template<auto>` 形状；伪变更运算符跟进 LWG4383/4500。

## 如何使用

包含 `<mdspan>` 并链接 `forge::std`（需要扩展时使用 `forge::forge`），采用 C++23 或更新模式。

```cpp
#include <mdspan>
int data[6]{};
std::mdspan matrix(data, std::extents<int, 2, 3>{});
auto row = std::submdspan(matrix, 0, std::full_extent);
// row views three elements of the same storage.
```

## 特性宏

当 Forge 注入 backport 时定义：

- `__cpp_lib_constant_wrapper = 202606L`
- `__cpp_lib_submdspan = 202603L`
- padded layouts 不提升 `__cpp_lib_mdspan` 至 `202406L`，原有 `<mdspan>` 宏保持由标准库提供

GCC 16 的 `std::constant_wrapper` 提供 `202603L` pre-P4206 native surface；`submdspan` 已通过完整 probe。它属于 partial native：
Forge 会警告并让位，不会在同一 `namespace std` 中叠加声明。专用
`constant_wrapper` conformance tests 只在 Forge 注入或检测到完整 `202606L`
native surface 时运行。

## Native 让位

统一策略见 [native handoff](../native-handoff.md)。`FORGE_FORCE_SUBMDSPAN_BACKPORT` 只用于诊断，具有 ODR 风险。

其他入口：`<utility>` 提供 constant_wrapper；诊断开关为 `FORGE_FORCE_CONSTANT_WRAPPER_BACKPORT` 和 `FORGE_FORCE_MDSPAN_PADDED_LAYOUTS_BACKPORT`。

## 覆盖范围

### 覆盖范围

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
`submdspan_mapping(mapping, slices...)`，让 ADL 找到实现。

Forge 同时提供 `std::constant_wrapper`（`<utility>`）和 C++26 padded mdspan layouts
作为 `submdspan` foundation。这些 foundation 由 `forge.cmake` 单独探测，检测到原生或
部分原生声明时会主动让位，避免 ODR 冲突。

对应示例：

- `example/constant_wrapper_example.cpp`：`std::constant_wrapper` / `std::cw` 的
  mixed-wrapper algebra 与 call/subscript constant preservation；
- `example/padded_mdspan_layout_example.cpp`：`layout_left_padded` /
  `layout_right_padded` 的 stride 与 full-slice layout 保留。

## Búzios 缺陷报告

以下四项来自 Búzios Motion 3，按 C++26 DR 处理。
[P4485R0](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p4485r0.html) ·
[working draft 编辑 PR](https://github.com/cplusplus/draft/pull/9434)

| 决议 | Forge 处理与集成点 |
| --- | --- |
| [LWG 4588](https://cplusplus.github.io/LWG/issue4588) | Forge 已符合。`submdspan.hpp` 的切片验证接受静态轴上的 `E::index_type` 与 `full_extent_t`；运行时索引仍须在轴的范围内。 |
| [LWG 4562](https://cplusplus.github.io/LWG/issue4562) | `layout_stride::mapping::is_exhaustive()` 由宿主库提供，按跨度大小是否等于 extent 乘积判断。Forge 的 `submdspan`、padded 转换及 linalg 不依赖旧的 stride 链；`layout_transpose` 仅转发底层的 observer。Padded mapping 自身的 `is_exhaustive()` 和 `is_always_exhaustive()` 规则不变。 |
| [LWG 4603](https://cplusplus.github.io/LWG/issue4603) | 宿主 `layout_stride` 的数组构造允许空索引空间中的零 stride；mapping 转换构造也不再要求所有 stride 为正。`submdspan` 将计算出的 stride 交给宿主；padded mapping 使用 `layout_stride` 的 mapping 转换构造，保留各轴转换至目标 index type 后的值，包括未使用轴上收窄后可能出现的负值。反向转换不额外要求正 stride。Linalg 在 padded extent 和 padding stride 都为零时仅用 extents 构造转置，否则经 mapping 转换原样保留交换后的 stride。 |
| [LWG 4606](https://cplusplus.github.io/LWG/issue4606) | 非空索引空间的构造条件先排除 singleton 轴。若仍有轴，第一个 stride 为正，每个后续 stride 大于前面各轴最大偏移之和。Forge 已符合：构造 `layout_stride` 子映射时不检查旧排列规则。 |

`backport/mdspan` 先包含宿主 `<mdspan>`，不替换基础 mapping。默认验证配置下，
libc++ 19.1.7（llvm/tsan/asan，不启用加固）与 libstdc++ 16.2（启用或禁用
`_GLIBCXX_ASSERTIONS`）的实测结果一致：extent `(1, 4)`、stride `(100, 1)` 的 mapping
是 exhaustive；空索引空间接受零 stride；extent `(2, 2)`、stride `(2, 3)` 的 mapping
可构造且 unique；`layout_left` 的 `(0, 3)` 转为 `layout_stride` 后的 stride 为 `(1, 0)`。
GCC 16 使用原生 padded layouts 和 `submdspan`，Forge 让位。

libc++ 的加固模式仍检查旧的 `layout_stride` 前置条件：各加固模式都在数组构造函数和转换
构造函数中拒绝零或负 stride，extensive 与 debug 模式还在数组构造函数中检查旧的排列规则。
zig lane 使用 zig 0.14 自带的 libc++，默认启用 `_LIBCPP_HARDENING_MODE_DEBUG`；portable
test 和 linalg transpose test 在这类宿主上跳过会构造此类 mapping 的用例，这些用例由不启用
加固的 llvm lane 覆盖。即使不启用加固，libc++ 19.1.7 的 `layout_stride::is_exhaustive()` 也可能
对空 mapping 返回 `false`，例如 extent `(3, 0)`、stride `(4, 1)`。Portable test 使用
外轴末端对齐的空切片，得到 extent `(0, 4)`、stride `(4, 1)`，保留对尾后 handle、
零跨度和 exhaustiveness 的检查。
[libc++ 19.1.7 observer 源码](https://github.com/llvm/llvm-project/blob/llvmorg-19.1.7/libcxx/include/__mdspan/layout_stride.h#L303-L320)

未实测 MSVC STL；其公开源码在 iterator-debug 模式下仍包含旧的构造检查，
二维 `is_exhaustive()` 也仍按旧规则判断。Portable tests 跳过受影响的用例或断言，
其余检查照常进行。[MSVC STL `<mdspan>` 源码](https://github.com/microsoft/STL/blob/main/stl/inc/mdspan)


## Padded layouts

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


### `constant_wrapper` conformance ledger

- 已实现：P4206 的 `constant_wrapper<auto X, class T = decltype(X)>` / `cw<X>`
  类型形状、显式第二类型一致性要求、P2781 运算符与伪变异运算、P3978
  constant/runtime call 和 subscript 分支。
- 伪变更与赋值作用于 wrapped const value 的表达式，并在返回类型处检查
  常量表达式与结果类型；普通整数的 `++`、复合赋值和不同值间赋值不可用，
  不再对临时副本模拟修改。同类型 wrapper 的普通复制/移动赋值仍然可用。
- 已验证：C++23 injected path、Clang/libc++ injected path、MSVC injected path，
  以及 GCC 16 `202603L` partial-native stand-aside。
- MSVC 当前不能形成以数组左值表达式为模板实参的 `constant_wrapper`；这类
  `operator()` / `operator[]` 结果按 wording 的无效 constant-call 分支回退为
  `constexpr` 数组引用，其他结果仍形成 `constant_wrapper`。
- 有意不提供：被 P4206 撤回的 `cw-fixed-value` 与字符串字面量兼容 surface。
- native stdlib 只要已经声明任意 partial surface，Forge 就不会覆盖注入；这是
  项目的 ODR 安全策略，不代表该 native 实现满足上述完整 baseline。

## 限制与偏差

`forge.cmake` 通过旧式 `strided_slice` probe 检测 partial-native 实现并让位。

## 示例

示例门控与所有文件见[示例索引](../examples.md)。只在注入或完整原生时构建；明确依赖扩展者另列限制。验证流程见[测试说明](../testing.md)，测试组为 `FORGE_TEST_ENABLE_SUBMDSPAN`。

- [constant_wrapper_example.cpp](../../example/constant_wrapper_example.cpp)：constant wrapper 的接口组合与完成结果。

- [padded_mdspan_layout_example.cpp](../../example/padded_mdspan_layout_example.cpp)：padded mdspan layout 的接口组合与完成结果。

- [submdspan_example.cpp](../../example/submdspan_example.cpp)：行列、步长切片与边界单元。

## 验证

验证流程见[测试说明](../testing.md)，使用对应测试组及 `ctest -R '^example_'` 运行示例。

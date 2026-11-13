# `std::simd` backport 说明

当前 `std::simd` backport 已覆盖 [simd.syn] 公开表面。

## 如何使用

包含 `<simd>` 并链接 `forge::std`（需要扩展时使用 `forge::forge`），采用 C++23 或更新模式。

```cpp
#include <simd>
using V = std::simd::vec<float, 4>;
V values(2.0f);
auto doubled = values + values;
// doubled contains four lanes with value 4.
```

## 特性宏

包含 `<simd>` 后定义 `__cpp_lib_simd = 202606L`、`__cpp_lib_simd_bitops = 202607L`、`__cpp_lib_simd_complex = 202502L`、`__cpp_lib_simd_permutations = 202506L`。

## Native 让位

统一策略见 [native handoff](../native-handoff.md)。`FORGE_FORCE_SIMD_BACKPORT` 只用于诊断，具有 ODR 风险。

## 覆盖范围

### 公开 API

- **核心 API**：`std::simd::basic_vec<T, Abi>`、`std::simd::vec<T, N>`、`std::simd::mask<T, N>`、构造/转换/下标/算术/比较/位运算
- **内存操作**：`partial_load`/`partial_store`、`unchecked_load`/`unchecked_store`
  （含 flags、mask、range 重载），以及 `partial_gather_from` /
  `unchecked_gather_from` / `partial_scatter_to` / `unchecked_scatter_to`
- **归约与排列**：scalar/vector `reduce`、`reduce_min`/`reduce_max`、
  `reduce_min_index`/`reduce_max_index`、`chunk`/`cat`、`permute`、`select`
- **创建与可见性**：`iota<T>` variable template、两个 `basic_vec` deduction guides，
  以及 `[simd.syn]` 要求加入顶层 `std` overload set 的 math/bit/complex declarations
- **参考实现**：逐元素运算依赖编译器自动向量化；mask/select、浮点归约和数学函数不保证生成向量代码
- **Feature macros**：定义 `__cpp_lib_simd = 202606L`、
  `__cpp_lib_simd_bitops = 202607L`、`__cpp_lib_simd_complex = 202502L` 和
  `__cpp_lib_simd_permutations = 202506L`

special-math 向量重载在有效域内逐 lane 遵循对应的标量特殊函数语义。
[simd.math] 对标量域错误所对应的 lane 不规定结果，也不保证通过 `errno` 报告。
因此非法实参不承诺跨标准库统一的返回值或错误报告方式：Forge fallback 返回 NaN，
转发原生标量库的路径保留其可能抛出的异常，不额外捕获或归一化。
标准把 Bessel 的
`nu >= 128`，以及球 Bessel / 球 Neumann / 球谐的阶数 `>= 128` 明确定为
implementation-defined。Forge 的多项式 fallback（Hermite、Laguerre、Legendre、
associated Laguerre / Legendre 和 spherical Legendre）在 degree `>= 1024` 时
直接返回 NaN；associated Laguerre 的 order `>= 1024` 同样返回 NaN，不进入递推。
associated Legendre 的 `m > l` 零值规则仍优先保留。
Hermite、普通/associated Laguerre 和 associated Legendre 的 fallback 使用
缩放递推，保留超出中间标量范围但最终可表示的项；不依赖 `long double` 的扩展指数范围。
球 Bessel / Neumann fallback 同样在 degree `>= 1024` 时返回 NaN，不进入递推。
球 Neumann fallback 的种子与递推项使用缩放表示，避免中间溢出污染可表示的最终值。
这些预算只描述 Forge fallback，不覆盖转发给原生标量库的实现；
超高阶数值质量不计入 portable conformance 承诺。
定义范围内的 fallback 会在 LLVM/libc++ lane 通过
直接调用与 public `std::simd` 调用共同验证；不能用会转发原生标量 special math 的
libstdc++ lane 充当 fallback oracle。
`riemann_zeta` 的极点邻域使用 Laurent 局部展开，并在其余 Hasse 路径用
`expm1` 计算解析延拓分母，避免 `s` 接近 1 时由相近数相减造成无穷或错号。
`ellint_3` / `comp_ellint_3` 在一般模数路径使用 Carlson `RF` / `RJ`，
负 characteristic 通过保留 complement 的解析变换处理；极端完整端点的
退避由相对误差界控制。零模数路径使用闭式表达式并显式区分极点两侧。

## 限制与偏差

### 性能特征与归约顺序

逐元素算术、连续 load/store 与 `select` 的实现便于编译器自动向量化；
实际代码生成取决于工具链、ISA、向量宽度与调用上下文，不保证使用特定指令。
mask 的存储布局与类型的 size/alignment 由实现决定。
当前公开接口用 `select` 表达条件选择，没有 `where` 接口。

当前实现的 `reduce`、`reduce_min`、`reduce_max` 与 `reduce_count` 按 lane 索引顺序
逐项扫描，总工作量为 O(N)。`reduce` 从首个选中 lane 开始累积，
masked 形式跳过未选中 lane，不向非空归约注入 identity；空 mask 返回指定的 identity。
masked `reduce_min`/`reduce_max` 在空 mask 时分别返回 `numeric_limits<T>::max()` /
`numeric_limits<T>::lowest()`。`all_of`/`any_of` 使用短路扫描，
`reduce_min_index`/`reduce_max_index` 保留按索引扫描，以返回要求的最小/最大索引。

[simd.reductions](https://eel.is/c++draft/simd.reductions) 的 `reduce` 使用
[numerics.defns](https://eel.is/c++draft/numerics.defns) 定义的 `GENERALIZED_SUM`，
不规定固定归约顺序，允许改变分组和排列；
[reduction-binary-operation](https://eel.is/c++draft/simd.expos.defn) 要求运算具有
commutative 的逐元素语义，没有显式的 associative 要求。
当前顺序扫描是实现选择，调用者不能将从左至右的执行顺序视为可移植保证。
浮点 `plus` 的分组变化不仅影响末位舍入，
还可能改变对消以及中间结果的 overflow/underflow，导致明显不同的结果。
浮点 `multiplies` 也可能因舍入或中间范围而变化；验证浮点和时应采用适合输入范围的容差，
并单独检查对消与范围边界情形。
min/max 对相等候选保留较早的选中元素，保留 signed zero；当前实现也保留首个
选中元素为 NaN 时返回该 NaN、忽略后续 NaN 的既有行为。

### 数学函数与 errno

[simd.math](https://eel.is/c++draft/simd.math) 按函数组规定精度：部分函数组要求
element-wise equal，包含 `sqrt` 的函数组则只要求
[element-wise approximately equal](https://eel.is/c++draft/simd.math#10)。
本实现以有效域内逐 lane 与对应标量函数一致为更强的精度目标。
草案不规定是否访问 `errno`；标量 domain/pole/range error 对应的 lane 结果未指定。
因此不能用 `errno` 检查 SIMD 数学函数的可移植错误状态。
`sqrt` 通过通用逐元素变换调用对应标量函数，常量求值使用既有 constexpr 路径。
标量库调用可能访问 `errno`，这里不保证 errno-free 执行。
`sin`、`exp`、`fma` 等其他函数保留现有标量库路径，不引入近似多项式或全局
`fast-math` 选项，也不改变调用者的浮点异常模式。
草案没有把浮点异常状态与 `errno` 一并豁免；实现不额外清除或屏蔽异常，
实际异常行为受工具链的浮点环境支持和编译选项约束。
NaN、无穷、signed zero 与 subnormal 按既有标量语义处理；不提供 FTZ/DAZ 模式切换。

独立基准及同核配对构建/运行命令见
[SIMD benchmark protocol](../../test/simd/benchmark/MEASUREMENT_PROTOCOL.txt)。
基准不注册到默认 CTest；使用同一源码、工具链与 ISA 构建基线和候选，
通过 `taskset` 绑到同一可用核心并交错采样。结果仅反映记录的环境和负载。
性能优化仅在配对测量证实目标负载受益后采纳；需要同时检查不同类型、
向量宽度与 mask 分布，不能由单个 kernel 的收益推断整体收益。

### Native ABI 的类型标识

默认 native ABI 的 lane 数由当前 translation unit 的 ISA 选项决定，且宽度是 ABI
tag 类型标识的一部分。同一宽度的显式 native tag 在不同 translation unit 中保持
一致的 lane 数和布局。通常仍应使用一致的 ISA 编译选项；这个类型标识保证不等于
整个 header-only 实现对任意混合 ISA 编译都提供 ODR 保证。


### Mask 变换的 ABI

mask 的 `permute`、`chunk`、`cat` 和 SIMD 下标操作不保证保留输入的 ABI
类型，也不保证结果与同宽向量的 `mask_type` 是同一类型。
[simd.traits](https://eel.is/c++draft/simd.traits) 和
[simd.creation](https://eel.is/c++draft/simd.creation) 要求结果满足对应的 lane 数
与元素字节大小约束，但允许实现选择具体 ABI。Forge 使用代表类型推导 mask
变换结果的 ABI；这是符合草案的选择。保留原 ABI identity 属于 QoI，不是可移植保证。

需要把变换后的 mask 传给 `select` 等操作时，在 lane 数匹配的前提下先显式构造
`V::mask_type`；[mask 转换构造](https://eel.is/c++draft/simd.mask.ctor) 逐 lane 保留布尔值：

```cpp
using V = std::simd::vec<float, 4>;
const V values([](auto i) { return static_cast<float>(i) - 1.0f; });
const auto parts = std::simd::chunk<2>(values > V(0.0f));
const auto transformed = std::simd::cat(parts[1], parts[0]);
const V::mask_type selector(transformed);
const V result = std::simd::select(selector, values, V(0.0f));
```


### 限制与偏差

`std::simd` 下 `native_abi`、`simd_size`、`fixed_size_abi`、`deduce_abi_t`、`abi_lane_count`、`simd_size_type` 以及实现提供的额外标志类型是 Forge 独有公开名，不可移植；原生让位后不能依赖这些名字。存储对齐也不等同于 ISA 向量对齐，调用者应遵守实际 load/store 的对齐要求。

## 示例

示例门控与所有文件见[示例索引](../examples.md)。只在注入或完整原生时构建；明确依赖扩展者另列限制。验证流程见[测试说明](../testing.md)，测试组为 `FORGE_TEST_ENABLE_SIMD`。

- [simd_complete_example.cpp](../../example/simd_complete_example.cpp)：SIMD 内存、mask 与排列（Forge 扩展拼写）。

- [simd_example.cpp](../../example/simd_example.cpp)：simd 的接口组合与完成结果。

## 验证

验证入口见[测试说明](../testing.md)。跨架构支持须以对应工具链实际运行结果为准，不承诺所有架构均已验证。

Windows smoke 默认增加两个独立的 C++23 SIMD-only build：MSVC x64 默认 ISA
（SSE2）和 `CMAKE_CXX_FLAGS="/EHsc /arch:AVX2"`，均编译 runtime targets、运行 SIMD
runtime tests 和 CTest compile probes，并执行 configure 阶段的正反向 probes。
已在 VS 2026 Build Tools / MSVC 19.51、x64、Debug、Ninja 配置验证：主 smoke
91/91，SIMD 默认 ISA 69/69，SIMD `/arch:AVX2` 69/69，install-package smoke 通过。
MSVC 使用逐 lane 标准 C++ 算术路径；GCC/Clang 继续使用原有 vector extension 后端。
native ABI 对 MSVC x64 默认取 16 bytes，对 `__AVX__` / `__AVX2__` 取 32 bytes，
对 `__AVX512F__` 取 64 bytes，ARM64 NEON 默认取 16 bytes。
`/arch:AVX`、`/arch:AVX512`、ARM64、x86 32-bit 和 clang-cl 尚未验证；默认 smoke 的 AVX2
运行要求主机支持 AVX2。现有 feature guards 跳过无 `__int128` 时的 extended-integer
rejection probe，且 mixed-ISA fixture 仅在使用 `cpuid.h` / `-m` flags 的 GNU/Clang
非 MSVC 平台注册；其余 SIMD tests 不设 MSVC 专属排除。

当前 GCC 16 在 C++26 提供 partial-native `std::simd`，Forge 会严格让位并给出警告；
同一标准库在 C++23 虽可安装 `<simd>` 文件但不暴露声明，此时声明级 probe 会启用
backport。检测到 partial-native 声明时不注入 backport，以避免 ODR 冲突。

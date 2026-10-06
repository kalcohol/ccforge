# `std::simd` backport 说明

当前 `std::simd` backport 已覆盖 [simd.syn] 公开表面。

## 公开 API

- **核心 API**：`std::simd::basic_vec<T, Abi>`、`std::simd::vec<T, N>`、`std::simd::mask<T, N>`、构造/转换/下标/算术/比较/位运算
- **内存操作**：`partial_load`/`partial_store`、`unchecked_load`/`unchecked_store`
  （含 flags、mask、range 重载），以及 `partial_gather_from` /
  `unchecked_gather_from` / `partial_scatter_to` / `unchecked_scatter_to`
- **归约与排列**：scalar/vector `reduce`、`reduce_min`/`reduce_max`、
  `reduce_min_index`/`reduce_max_index`、`chunk`/`cat`、`select`
- **创建与可见性**：`iota<T>` variable template、两个 `basic_vec` deduction guides，
  以及 `[simd.syn]` 要求加入顶层 `std` overload set 的 math/bit/complex declarations
- **Layer 1 向量化**：GCC/Clang vector extension 后端，`if consteval` 保持 constexpr 正确性
- **Feature macros**：定义 `__cpp_lib_simd = 202606L`、
  `__cpp_lib_simd_bitops = 202607L`、`__cpp_lib_simd_complex = 202502L` 和
  `__cpp_lib_simd_permutations = 202506L`

special-math 向量重载逐 lane 遵循对应的标量特殊函数语义。标准把 Bessel 的
`nu >= 128`，以及球 Bessel / 球 Neumann / 球谐的阶数 `>= 128` 明确定为
implementation-defined；Forge 在该范围之外保留有界失败策略，不把超高阶数值质量
计入 portable conformance 承诺。定义范围内的 fallback 会在 LLVM/libc++ lane 通过
直接调用与 public `std::simd` 调用共同验证；不能用会转发原生标量 special math 的
libstdc++ lane 充当 fallback oracle。
`riemann_zeta` 的极点邻域使用 Laurent 局部展开，并在其余 Hasse 路径用
`expm1` 计算解析延拓分母，避免 `s` 接近 1 时由相近数相减造成无穷或错号。
`comp_ellint_3` 在 characteristic 从下方接近 1 时使用缩放变量变换，避免窄峰
超过自适应求积的细分深度；零模数路径使用闭式表达式并显式区分极点两侧。

## Mask 变换的 ABI

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

## 验证

已在 x86_64、aarch64、riscv64、loongarch64 四架构验证。

当前 GCC 16 在 C++26 提供 partial-native `std::simd`，Forge 会严格让位并给出警告；
同一标准库在 C++23 虽可安装 `<simd>` 文件但不暴露声明，此时声明级 probe 会启用
backport。检测到 partial-native 声明时不注入 backport，以避免 ODR 冲突。

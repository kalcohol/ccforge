# `std::simd` backport 说明

当前 `std::simd` backport 已覆盖 [simd.syn] 公开表面。

## 公开 API

- **核心 API**：`std::simd::basic_vec<T, Abi>`、`std::simd::vec<T, N>`、`std::simd::mask<T, N>`、构造/转换/下标/算术/比较/位运算
- **内存操作**：`partial_load`/`partial_store`、`unchecked_load`/`unchecked_store`（含 flags、mask、range 重载）、`gather`/`scatter`（含 range 重载）
- **归约与排列**：`reduce`、`reduce_min`/`reduce_max`、`reduce_min_index`/`reduce_max_index`、`split`/`cat`、`select`
- **Layer 1 向量化**：GCC/Clang vector extension 后端，`if consteval` 保持 constexpr 正确性
- **Feature macro**：定义 `__cpp_lib_simd = 202411L`，表明完整 [simd.syn] 覆盖

## 验证

已在 x86_64、aarch64、riscv64、loongarch64 四架构验证。

当前 GCC 16 在 C++26 提供完整原生 `std::simd`，Forge 会严格让位；同一标准库在
C++23 虽可安装 `<simd>` 文件但不暴露声明，此时声明级 probe 会启用 backport。真正检测到
partial-native 声明时 Forge 仍会让位，避免 ODR 冲突。

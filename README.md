# Forge

现代 C++ header-only 库，提供标准库扩展和无感 backport。

## 特性

- **forge/** - 标准库之外的扩展功能（类似 Boost 的实用工具）
- **backport/** - 为旧编译器提供透明的 C++26 backport

## 快速开始

```cpp
#include <memory>

// 无感使用 C++26 特性
auto file = std::make_unique_resource_checked(fopen("test.txt", "r"), nullptr, &fclose);
```

## CMake 集成

```cmake
include(/path/to/forge/forge.cmake)
target_link_libraries(myapp PRIVATE forge::forge)
```

或使用 add_subdirectory：

```cmake
add_subdirectory(forge)
target_link_libraries(myapp PRIVATE forge::forge)
```

## 要求

- C++23 或更高版本
- CMake 3.17 或更高版本

## 编码规范

- **文件编码**: UTF-8 without BOM（符合 C++23 标准）
- **代码注释**: 仅使用英文
- **许可证**: MIT License (2026)

## 文档

- [示例](example/) - 使用示例

## `std::simd` 说明

- 当前 `std::simd` backport 已提供并验证核心公开表面与示例构建。
- 在完整 wording 覆盖继续外扩前，backport 不主动定义 `__cpp_lib_simd`，避免过度宣称标准支持级别。

## 参考实现

本项目的 backport 实现参考了以下开源项目：

- **std::simd**: [std-simd](https://github.com/VcDevel/std-simd) - Matthias Kretz 的参考实现
- **SIMD 优化**: [ncnn](https://github.com/Tencent/ncnn) - 腾讯的高性能神经网络推理框架

感谢这些项目的贡献者。

## 许可证

MIT License - 详见 LICENSE 文件

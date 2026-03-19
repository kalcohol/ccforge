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

## 无感注入机制与必要要素

Forge 的 backport 目标是: 当未来标准库原生提供相同能力后, 下游升级工具链时无需任何源码修改即可自动切换到原生实现.

为了实现这一点, backport 必须满足以下必要要素:

- 标准入口不变: 下游依旧 `#include <memory>` / `#include <simd>`, 而不是包含某个 `forge/...` 头.
- 命名空间不变: 标准 API 以 `std::` 形式出现, 下游不需要改成 `forge::` 或加宏分支.
- API 形状一致: 公开接口与标准最终版保持一致, 避免额外扩展导致未来不兼容.
- 自动开关: `forge.cmake` 会探测当前工具链是否已原生支持对应特性; 仅在缺失时启用 backport, 避免与未来原生实现冲突.

实现方式概览:

- 当需要 backport 时, `forge.cmake` 会把 `backport/` 目录放到 include path 前面.
- `backport/` 内提供与标准同名的包装头(例如 `backport/memory`), 它先包含真实标准库头, 再在缺失时引入 backport 实现.

注意: 将声明/定义放入 `namespace std` 在严格意义上属于标准未定义行为. 这里是 backport 为达成 "无感切换" 的工程性权衡, 并且通过 "仅在工具链缺失该特性时启用" 来降低风险面.

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

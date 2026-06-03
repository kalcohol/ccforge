# `std::unique_resource` backport 说明

`std::unique_resource` 当前仅在 Library Fundamentals TS v3 中，尚未进入 C++26 标准。
Forge 提供 `<memory>` 入口下的实验性 backport。

## 示例

```cpp
#include <memory>

auto file = std::make_unique_resource_checked(
    fopen("data.txt", "r"),
    nullptr,
    &fclose);
```

## 注入边界

Forge 会先包含真实标准库 `<memory>`，然后在缺少
`__cpp_lib_unique_resource >= 202311L` 时注入 experimental backport。

因为该设施尚未进入 C++26，原生工具链的可用性和 feature-test macro 形态可能继续变化。
若未来标准库提供完整实现，Forge wrapper 会让位。

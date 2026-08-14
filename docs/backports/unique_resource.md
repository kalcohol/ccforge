# `std::unique_resource` backport 说明

`unique_resource` 当前仅在 Library Fundamentals TS v3 中，尚未进入 C++26 标准。
TS 的原始入口是 `<experimental/scope>` 和
`std::experimental::fundamentals_v3`；Forge 提供的是有意提升到 `<memory>` /
`std` 的扩展拼写，方便与候选标准库表面共同探测。它不是 TS 源码级入口的完整
backport。

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

`__cpp_lib_unique_resource` 同样是 Forge 用于 promoted spelling 的探测约定，
不是 Library Fundamentals TS v3 的 `__cpp_lib_experimental_scope` 宏。

因为该设施尚未进入 C++26，原生工具链的可用性和 feature-test macro 形态可能继续变化。
若未来标准库提供完整实现，Forge wrapper 会让位。

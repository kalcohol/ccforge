# `std::hive` backport

`std::hive` 是分块存储的无序序列容器：元素地址稳定，通常以 O(1) 插入、删除，并复用已删除槽。
`<hive>` 以 header-only 方式提供 C++26 接口，可从下面的快速示例开始，再按示例表选择场景。

```cpp
#include <hive>

int main() {
    std::hive<int> pool;
    auto retained = pool.insert(17);
    auto transient = pool.insert(28);
    pool.erase(transient);
    pool.insert(39);
    return *retained == 17 && pool.size() == 2 ? 0 : 1;
}
```

## 何时选择 hive

| 容器 | 适用需求与取舍 |
| --- | --- |
| `vector` | 连续存储、随机访问；增长和中间删除可能移动元素 |
| `deque` | 随机访问、两端增长；句柄稳定性依操作而定 |
| `list` | 链接节点、按位置插入；每个节点有链接开销 |
| `hive` | 经常生成和回收元素、需要保存存活元素地址；不需要随机访问或插入顺序 |

遍历顺序由容器决定，空槽会被复用；`emplace_hint` 忽略提示。
容器本身没有 `==` 和 `<=>`，需要比较内容时应自行选择顺序或集合语义。

## 核心概念

活动块承载存活元素及已删除槽；保留块暂不承载存活元素，可供后续插入使用。
`capacity()` 表示现有块可容纳的元素总数，包含可复用的槽，始终不小于 `size()`。
`hive_limits` 的 `min`、`max` 控制每块容量范围，不是容器总大小上限。
迭代器是双向迭代器；其 `<=>` 与遍历顺序一致，但不支持随机访问或迭代器相减。

## 失效与稳定性

下表的句柄指元素的指针、引用和迭代器；规则按标准接口描述。

| 操作 | 失效规则 |
| --- | --- |
| `insert`、`emplace`、`insert_range` | 只使 `end()` 失效 |
| `erase`、`erase_if`、`unique` | 被删除元素的句柄失效；删到最后一个元素时 `end()` 也失效 |
| `reserve`、`trim_capacity` | 不使句柄或 `end()` 失效 |
| `shrink_to_fit`、`reshape` | 发生重新分配时全部失效，元素顺序也可能改变；否则不失效 |
| `splice` | 成功调用时：被转移元素的指针和引用保持有效并属于目标容器；元素迭代器仍有效，改为指向目标容器中的元素；两边的 `end()` 都失效；若抛出任何异常则没有效果，两边容器均保持不变 |
| `sort` | 可能使全部元素句柄及 `end()` 失效；排序后须重新获取元素句柄和 `end()` |
| `swap` | 元素句柄保持有效，元素改属另一容器；不要继续使用旧的 `end()`；须满足分配器交换前提 |
| `clear` | 所有元素句柄及 `end()` 失效 |
| 赋值 | 不依赖目标容器的旧句柄；移动赋值仅在分配器允许转移存储时保留源元素句柄，改属目标；逐元素移动时不保证保留，旧 `end()` 应重新获取 |

## 复杂度

插入和删除通常为 O(1)；删除导致块被释放时，最坏与块数成线性。
遍历每步为 O(1)，`get_iterator` 与活动块数成线性，`splice` 与两边块数之和成线性。
`sort` 进行 O(N log N) 次比较；非空容器的 `unique` 恰好调用 `size()-1` 次谓词。
`reserve` 和 `trim_capacity` 与涉及的保留块数成线性。

## 块容量限制

用 `block_capacity_limits()` 查询实例范围；静态函数 `block_capacity_default_limits()`、
`block_capacity_hard_limits()` 分别查询默认范围和硬边界。
构造容器或调用 `reshape(limits)` 前，先用 `is_within_hard_limits(limits)` 验证。
`reshape` 安装新的范围，必要时重新分配；不要假定它一定重新分配。
`reserve(n)` 预留至少 n 个槽，`trim_capacity()` 释放保留块；`shrink_to_fit()` 只是非约束性请求。

Backport 默认范围为 `{8, 4096}`，受硬上限钳制；硬上限每块最多 65535 个槽，
还受分配器与结构限制。这些取值仅属于 backport，原生让位后不可依赖。
非法范围属于错误行为；backport 选择抛出 `length_error`，这是实现定义的选择。
可移植代码应先校验范围，不能依赖捕获该异常来处理非法上限。

## sort、unique 与 splice

`sort(comp)` 不稳定，比较器必须满足严格弱序；操作可能分配内存，也可能使句柄失效。
作为项目工程决定，`sort` 抛异常后保证容器有效、`size()` 不变且不泄漏；元素顺序
和个别值未指定。这不是对标准问题的裁定，任何更强的观察行为都不是公开承诺（D1）。

`unique(pred)` 按遍历顺序删除相邻等价段，每段保留第一个元素，返回删除个数。
若要按键整体去重，应先按与等价关系一致的顺序 `sort`。

`splice(source)` 要求两边分配器相等。源活动块超出目标块容量限制时抛出
`length_error`。成功调用时转移源容器的活动块，源容器的保留块不转移；句柄及
`end()` 的变化见失效规则表。若 `splice` 抛出任何异常，则没有任何效果，
两边容器均保持不变。自 `splice` 是错误行为。

## get_iterator 与平面地址模型

前提是指针指向本容器的存活元素。`get_iterator` 不是查找或成员判断接口；
违反前提时 backport 返回 `end()` 仅为诊断行为，不能依赖，也不应以非法输入试探。
Backport 支持平面地址模型：指针到 `uintptr_t` 的映射保序，且同一分配内连续；
非平面地址模型不在支持范围内。

## CMake、native 让位与特性宏

链接 `forge::std`（需要扩展时链接 `forge::forge`），包含 `<hive>`。
需要 C++23 及以上，且标准库须提供 `std::from_range`；下游消费者不需要 GoogleTest。

完整 native 让位，partial native 警告并让位；没有 native 声明时注入 backport。
探测细节见 [native 让位](../native-handoff.md)。
`FORGE_FORCE_HIVE_BACKPORT` 仅供诊断，与 native 声明混用有重定义和 ODR 风险。
包含 backport `<hive>` 后 `__cpp_lib_hive == 202502L`；没有 `<version>` 包装，
不承诺仅包含 `<version>` 时该宏可见。原生实现的宏由标准库提供。
`FORGE_TEST_ENABLE_HIVE` 控制测试组；原生让位时跳过 backport 专用测试。

## 示例

共同门控：只在注入 backport 或完整 native 时构建；启用测试和示例时，每项注册为
`example_<target>_smoke`。target 除快速示例为 `hive_example` 外，均为文件名去掉 `_example.cpp`。

| 文件 | 场景 | 关键 API |
| --- | --- | --- |
| [hive_example.cpp](../../example/hive_example.cpp) | 快速开始 | `insert`、`erase` |
| [hive_stable_handles_example.cpp](../../example/hive_stable_handles_example.cpp) | 粒子池与长寿命发射器 | `emplace`、`erase_if` |
| [hive_capacity_limits_example.cpp](../../example/hive_capacity_limits_example.cpp) | 突发连接表 | `reserve`、`trim_capacity`、`reshape` |
| [hive_sort_unique_example.cpp](../../example/hive_sort_unique_example.cpp) | 遥测排序去重 | `sort`、`unique` |
| [hive_get_iterator_example.cpp](../../example/hive_get_iterator_example.cpp) | 回调表回收实体 | `get_iterator` 的可变及 const 重载 |
| [hive_splice_example.cpp](../../example/hive_splice_example.cpp) | 流水线结果转移 | `splice`、`is_within_hard_limits` |

## 支持与验证范围

截至 2026-11-12，已验证 Linux Clang 19/libc++、GCC 16/libstdc++、Zig 0.14，
macOS AppleClang 21 及 Windows MSVC 19.51 的 hive 与消费者路径；Linux 两种标准库均有 ASan+UBSan 验证。
Windows 配置为 C++23，实际使用 `/std:c++latest`；其结果限于 hive、探测及消费者，
不代表全仓库 MSVC 验证。Linux 与 macOS 覆盖 C++23/C++26，zig 覆盖 C++23。

已知缺口：没有 hive TSan 或 Windows sanitizer 结果；libstdc++ sanitizer 仅覆盖排序
内核和四项 hive 核心控制，没有额外压力程序。Windows 迭代器专项的额外压力
程序没有运行结果；Windows 排序专项的压力验证不覆盖该项。
上述真实工具链验证使用 backport；合成 native fixture 只验证探测与让位机制，
不证明生产原生 hive 的完整性。项目性能对比只是特定主机上的测量，不构成性能保证。

实现仍有宽位序计数器与 epoch 最终耗尽的边界；不承诺无限次数的结构变更，也不提供并发容器保证。

## 实现说明

空 allocator 不增加容器大小：allocator 成员使用 `[[no_unique_address]]`；
MSVC ABI 下优先使用 `[[msvc::no_unique_address]]`，因为标准拼写在该 ABI 下会被忽略。
不支持这两种属性的编译器保留普通成员布局。

## 可移植测试

`hive_portable_runtime` 选取仅依赖 [hive](https://eel.is/c++draft/hive) 与
[sequence.reqmts](https://eel.is/c++draft/sequence.reqmts) 所保证语义的测试，
在 backport、完整或部分原生 `<hive>` 下均注册。覆盖插入与存活元素地址稳定性、
双向遍历、范围删除返回值、自引用插入、`erase` / `erase_if` / `swap`，以及
`reserve`、`trim_capacity`、`shrink_to_fit`、`reshape` 的内容与容量限制契约。
测试不假定插入位置、块大小或增长策略，也不要求压缩或重塑后的精确容量与元素顺序。
另覆盖 allocator 传播与存储释放、单元素插入异常的无效果保证、输入与代理范围、
元素类型要求、splice 后的迭代器归属，以及双向迭代器的复制、转换和存活元素遍历。
仅依赖标准要求的编译检查也在上述配置下注册；实现专属测试仅在启用 backport 时注册。

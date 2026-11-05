# `std::hive` backport

`<hive>` 提供 C++26 `std::hive` 的 header-only backport。它以分块存储支持
插入和删除，未被删除的元素可保留迭代器和地址；具体操作的失效规则仍须遵守
标准接口的要求。示例见 [hive_example.cpp](../../example/hive_example.cpp)。

Consumer 可链接 `forge::std`（或需要扩展时链接 `forge::forge`），然后包含
`<hive>`。正常 source/install consumer 不需要 GoogleTest。CMake 根据当前语言
模式探测：完整 native 让位，partial native 警告并让位，无 native 声明才注入
backport。`FORGE_FORCE_HIVE_BACKPORT` 仅用于诊断；与已有 native 声明混用有
重定义和 ODR 风险。`FORGE_TEST_ENABLE_HIVE` 控制项目的 Hive 测试组。

包含 `<hive>` 后，backport 定义 `__cpp_lib_hive` 为 `202502L`。不提供共享的
`<version>` wrapper，也不承诺仅包含 `<version>` 时该宏可见。

## 限制与异常

Backport 的 `get_iterator` 支持平面地址模型：指针到 `uintptr_t` 的映射保序，且
同一分配内连续。查找对每个活动块做常数次工作，总工作量与活动块数成线性；
非平面地址模型不在支持范围内。输入必须指向该容器的存活元素。

作为项目工程决定，`sort` 抛异常后保证容器有效、`size()` 不变且不泄漏；元素顺序
和个别值未指定。这不是对标准问题的裁定。比较函数抛异常时曾观察到更强的值保留
行为，并保留回归测试，但它不是公开保证。

所有元素类型的默认块容量上限均为 4096，下限为 8；两者各自受静态 hard maximum
钳制。实际分配还受 allocator 和结构上限约束。

R5 的 Linux、macOS、Windows Hive 集成及无 GoogleTest consumer 门禁已通过，
具体工具链、测试范围和限制见 [准入记录](../../test/hive/R5_ADMISSION.txt)。
合成 native fixture 验证的是探测与让位机制，不代表某个原生库完整实现了 Hive。
R2.1 迭代器优化经独立复核，于 2026-11-09 **获负责人准入**，见
[准入记录](../../test/hive/R21_ITERATION.txt)。依据为 `CLAUDE_R21_REVIEW.md`
的正确性复核、补充成对测量及负责人批准的 P1 区间流程。原三批主性能测量仍为
噪声作废，不追溯改为通过；额外 Windows 压力测试是已知验证缺口。
新成对性能规则从 R4.1 起生效。R4/R4.1 排序加速仍需独立验证和准入，当前主线
保持原有堆排序算法。
R4 原排序候选已完成实施方的跨平台验证和同批 P0/P2
数值检查，见 [R4 记录](../../test/hive/R4_SORT.txt)；该记录注明共享主机的测量
限制。Claude 独立复核、仓库外的 P1 对比及负责人合入批准仍待完成。

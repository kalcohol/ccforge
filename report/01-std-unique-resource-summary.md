# `std::unique_resource` 开发到 Review 再到收敛总结 (v0)

日期: 2026-03-19

本文件总结 `std::unique_resource` backport 从初版实现、第一轮审阅、到最终收敛落地的全过程, 作为后续迭代的基线记录.

## 背景与目标

Forge 的定位不是“类似功能替代品”, 而是透明 backport: 下游代码在未来标准库原生提供 `std::unique_resource` 后, 升级工具链时无需源码修改即可自动切换到原生实现.

为了达成“无感切换”, 该 backport 的关键要求是:

- 标准入口不变: 下游通过 `#include <memory>` 获得能力.
- 命名空间不变: 以 `std::` 形式暴露标准 API.
- API 形状与约束/重载参与尽量一致: 避免额外扩展导致未来不兼容.
- 自动开关: 仅在工具链缺失该特性时启用 backport.

## 第一轮 Review 输入

审阅文件:

- 问题清单: `report/01-std-unique-resource-v0-r0.md`
- 答复与优先级: `report/01-std-unique-resource-v0-r0-a0.md`, `report/01-std-unique-resource-v0-r0-a1.md`

审阅关注点覆盖三类核心风险:

- 非标准额外 API (未来切换时直接破坏兼容)
- 约束/重载参与与标准 wording 偏离 (会改变编译行为)
- 异常失败路径语义偏离 (资源泄漏/状态不一致)

同时要求补齐测试, 尤其是通过 `forge::forge` + `<memory>` 的注入路径验证.

## 收敛过程与关键决策

收敛遵循 a1 的优先级, 先修“正常使用路径兼容性破坏与异常安全”, 再修约束一致性与测试覆盖, 最后做整理类修正.

其中两点“原则性决策”被明确下来:

- 非标准 API (例如默认构造、`make_unique_resource`) 必须移除, 否则会诱导下游形成未来不兼容依赖.
- 约束表达不是风格问题: `requires` / `Mandates` / SFINAE 的差异可能改变重载选择与报错形式, 属于兼容性问题.

## 实现项对照 (I-1..I-9)

- I-1: 约束从 C++17 SFINAE 改为 `requires`.
- I-2: 移除默认构造函数 (标准最终版无此构造).
- I-3: 移除 `make_unique_resource` (最终标准仅保留 `make_unique_resource_checked`).
- I-4: move assignment 的 `noexcept` 与分支判断按标准语义收敛为基于 `R`/`D` (不再基于内部包装类型).
- I-5: move ctor 中 deleter 构造失败时, 必须无条件用 `other` 的 deleter 清理已构造资源并 `other.release()`, 避免泄漏.
- I-6: `reset(RR&&)` 约束按标准 Mandates 处理为硬错误 (以 `static_assert` 体现), 不再用 SFINAE 静默移除重载.
- I-7: `swap` 增加 `is_swappable_v` 约束 (成员与自由函数), 并保持 `noexcept` 与 `is_nothrow_swappable_v` 一致.
- I-8: 构造异常路径不再使用额外“是否可调用”的宽松分支; 且为避免对 noexcept 路径实例化异常清理代码导致不必要的编译失败, 将构造逻辑按 `noexcept`/copy-fallback 分流.
- I-9: 注入 `namespace std` 是 backport 的必要工程权衡, 需要在文档中明确动机/必要要素/风险控制.

## 测试项对照 (T 系列)

运行时补齐:

- T-1: `release()` 后析构不调用 deleter.
- T-2: move assignment 基本语义 (转移所有权 + 清理目标原资源).
- T-3: 值语义 `reset(R)` (先清理旧值, 再持有新值, 析构清理新值).
- T-4: `operator*` / `operator->` 的运行时行为验证.
- T-8: ADL `swap` 通过 `using std::swap; swap(a,b);` 可用.
- T-10: `reset(R)` 赋值抛异常时调用 `d(r)` 清理新资源.
- T-12: `make_unique_resource_checked` 构造出“无效状态”后可通过 `reset(valid)` 重新激活并在析构时清理.

编译探测/注入路径:

- T-5: compile probe 使用 `requires` 表达式进行 API/约束探测.
- T-7: CTAD 推导指引验证.
- T-9: wording 测试避免硬编码 copy 次数, 仅断言标准语义需要的“至少一次”.
- T-11: 不可拷贝 (copy ctor/assign 禁用).
- T-13: 通过 `forge::forge` + `<memory>` 注入路径编译验证.

## 工程配置与探测修正 (额外发现)

收敛过程中发现 2 个会直接阻断目标/验证的问题并已修正:

1. 顶层 CMake 强制设置为 C++17 会导致 `requires` 无法编译.
   处理: 要求至少 C++23, 且不覆盖用户显式指定的更高标准.

2. `forge.cmake` 的原生特性探测曾使用已移除的 `std::make_unique_resource`, 会导致探测永远失败,
   从而即使未来标准库已原生支持也仍然启用 backport, 违背“自动开关”目标.
   处理: 探测改为使用 `std::unique_resource` + `std::make_unique_resource_checked`.

## 文档收敛

README 增加“无感注入机制与必要要素”段落, 明确:

- 为什么必须保持标准入口(`<#include <memory>>`)与 `std::` 命名空间.
- 为什么必须避免额外 API 扩展.
- `forge.cmake` 如何自动探测并仅在缺失时启用 backport, 以降低注入风险面.
- 注入 `namespace std` 的未定义行为属性与工程性权衡.

## 最终验证状态

在 GCC 13.3.0 + CMake 构建环境下:

- 全量构建通过.
- `ctest --test-dir build` 全量测试通过 (包含运行时与 compile probe).

## 残余风险与后续注意点

- `namespace std` 注入在标准层面属于未定义行为, 该风险无法完全消除; 当前策略是:
  仅在探测到“标准库缺失该特性”时启用 backport, 并尽量与最终标准 API/约束一致以降低迁移风险.
- 未来若标准库对 `__cpp_lib_unique_resource` 的定义/语义发生变化, 需要确认 wrapper 头的开关条件仍然正确.


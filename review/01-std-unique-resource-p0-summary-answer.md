# `std::unique_resource` 审查答复 (p0-summary)
对应报告：`review/01-std-unique-resource-p0-summary.md`

## 总体结论

本轮对 `p0-summary` 的处理结论如下：

- 已接受并修复：`C-1`、`W-1`、`W-2`、`Q-1`、`F-1`、`F-2`
- 已接受并补齐测试：`T-1`、`T-2`、`T-3`、`T-4`
- 已确认但不修改：`Q-2`

处理原则保持不变：以未来标准库无感切换为最高目标，优先修正会影响接口形状、约束参与、异常/所有权语义和编译期可观察行为的差异。

## 条目答复

### C-1 `reset(RR&&)` 约束方式

接受，已修复。

- `reset(RR&&)` 已从 `static_assert` 改为 `requires` 约束，使不满足条件的重载按标准从 overload resolution 中移除。
- 已在编译期测试中补负向 probe，验证不可赋值资源不会触发硬错误，而是正确地使约束失败。

### W-1 move assignment `noexcept`

接受，已修复。

- move assignment 的 `noexcept` 与分支判断已统一基于 stored `resource_storage`，不再直接基于 `R`。
- 已补相应的 `noexcept` 编译期断言。

### W-2 `operator*` 的 `noexcept`

接受，已修复。

- `operator*` 已改为无条件 `noexcept`，与报告引用的 wording 对齐。
- 已补 `operator*` / `operator->` 的 `noexcept` 编译期验证。

### Q-1 未使用死代码

接受，已修复。

- 未被调用的 `construct_resource` / `construct_deleter` helper 已删除。
- 当前构造路径仅保留实际使用的 input / move helper，避免误导维护者。

### Q-2 free `swap` 中重复 `resource_storage` 计算

认可该观察，但本轮不修改。

- 这是库内部 free `swap` 为了匹配私有 `resource_storage` 语义而进行的实现重复，不影响下游用户的公开使用方式。
- 报告已将其定性为“信息项、设计约束导致、可接受”；本轮维持该结论，不为此引入额外公开 trait 或内部重构。

### F-1 `constexpr` 支持

接受，已修复。

- 构造、移动、`reset`、`release`、`get`、`get_deleter`、指针观察器、`swap` 以及 `make_unique_resource_checked` 已补齐 `constexpr`。
- 已增加编译期回归，覆盖默认构造、普通构造、move、reset/release、swap、factory 和 pointer observers 的常量表达式使用路径。
- 已在本机 GCC、本机 Zig/Clang，以及 podman 最新 GCC / Clang 环境下验证通过。

### F-2 默认构造函数

接受，已修复。

- 该项不再视为“待确认”。根据当前可查的官方 wording，`unique_resource()` 仍然存在，且要求在 `R` 与 `D` 均可默认构造时参与重载。
- 实现中已补受约束的 `constexpr unique_resource()`，语义为：value-initialize stored resource handle 和 deleter，并令 `execute_on_reset_ = false`。
- 已补测试验证：
  - 默认可构造的 `R/D` 组合应可默认构造
  - 引用资源与不可默认构造 deleter 组合不应默认构造
  - 默认构造对象初始不拥有资源，析构不调用 deleter
  - 默认构造对象可通过 `reset(valid)` 重新激活

### T-1 / T-2 self-move / self-swap

接受，已补测试。

- 已增加 self-move assignment 和 self-swap 的运行时回归，确认二者保持 no-op 语义。

### T-3 `noexcept` 编译期验证

接受，已补测试。

- 已为 `release`、`get`、`get_deleter`、`reset()`、`operator*`、`operator->` 和 move assignment 补 `static_assert(noexcept(...))`。

### T-4 move assignment 部分失败状态

接受，已补测试。

- 已补“resource 赋值成功、deleter 赋值抛出”路径的回归，验证目标对象保持 released，源对象继续负责最终清理。

## 最终结论

`p0-summary` 中需要直接落地的实现/测试项现已全部收敛完成。

剩余未改项仅 `Q-2`，且其性质为信息项，不影响标准接口可替换性，也不影响下游用户使用方式；本轮按“确认成立但不修改”关闭。

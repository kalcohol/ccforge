# `std::execution` 审查答复 (p0-v0-r1-a0)

审查基线：当前 C++ 工作草案 `[exec]`（https://eel.is/c++draft/exec）

答复对象：`03-std-execution-p0-v0-r1`（21 条问题：I-1 至 I-17，T-1 至 T-4）

答复口径：

- 以 `p0-v0-r1` 审查报告为准，基线为 eel.is 当前工作草案 `[exec]`
- 本轮修复范围：概念定义对齐、deadlock 修复、编译探针补充、已知偏差标注
- 架构级偏离（tag_invoke、run_loop）明确标注为已知偏差，不在本轮完成迁移

---

## 处置汇总

| 编号 | 严重程度 | 结论 | 落地提交 |
|------|----------|------|----------|
| I-1  | 高 | 暂缓，已标注偏差 | `425ee7e` |
| I-2  | 高 | 接受，已修复 | `15b3170` |
| I-3  | 中 | 接受，已修复 | `15b3170` |
| I-4  | 中 | 接受，已修复 | `15b3170` |
| I-5  | 中 | 接受，已修复 | `15b3170` |
| I-6  | 高 | 接受，已修复 | `6f18091` |
| I-7  | 低 | 接受，已修复 | `15b3170` |
| I-8  | 低 | 接受，已修复 | `15b3170` |
| I-9  | 中 | 暂缓，已标注 | `425ee7e` |
| I-10 | 高 | 暂缓，已标注偏差 | `425ee7e` |
| I-11 | 中 | 接受，已修复 | `6f18091` |
| I-12 | 低 | 暂缓，已标注 | `425ee7e` |
| I-13 | 中 | 暂缓，已标注 | `425ee7e` |
| I-14 | 低 | 已随 I-4 解决 | `15b3170` |
| I-15 | 低 | 已随 I-6 解决 | `6f18091` |
| I-16 | 中 | 无需修复 | — |
| I-17 | 信息性 | 无需修复 | — |
| T-1  | 高 | 接受，已修复 | `cccccfd` |
| T-2  | 低 | 接受，已修复 | `cccccfd` |
| T-3  | 低 | 接受，已修复 | `cccccfd` |
| T-4  | 中 | 接受，已修复 | `cccccfd` |

统计：已修复 12 条，暂缓/标注 5 条，无需修复 2 条，随其他问题解决 2 条。

---

## 逐条答复

### I-1 定制点机制使用 `tag_invoke`，与当前草案 `[exec]` 根本性偏离

结论：**暂缓，已标注偏差。**

理由：

`tag_invoke` 到成员函数优先 + domain-based 分发的迁移是架构级变更，涉及所有 CPO、所有内置 sender/receiver 类型和用户定制协议。本轮采用审查报告建议的路线 B（务实过渡）：

1. `tag_invoke` 保留为**内部实现机制**，不作为用户可见的定制协议暴露
2. 在头文件注释块中明确标注与当前草案 `[exec]` 的偏差
3. 后续在添加更多 adaptor 时逐步迁移到成员函数优先分发

落地点：

- 提交 `425ee7e`，`backport/cpp26/execution.hpp` 头部偏差文档块
- 标注内容包括：tag_invoke 作为内部机制而非用户定制协议，迁移计划指向 phase 1

---

### I-2 `sender` concept 缺少 `get_env` 可查询约束

结论：**接受，已修复。**

理由：

审查判断完全正确。当前草案 `[exec.snd]` 要求 `sender` concept 包含 `{ get_env(sndr) } -> queryable` 约束。已补充。

落地点：

- 提交 `15b3170`，`backport/cpp26/execution.hpp` 第 662-670 行
- `sender` concept 现在包含 `requires(const std::remove_cvref_t<S>& s) { { std::execution::get_env(s) } -> queryable; }`

---

### I-3 `receiver` concept 缺少 `get_env` 返回值的 `queryable` 约束

结论：**接受，已修复。**

理由：

审查判断正确。当前草案 `[exec.recv]` 要求 `get_env(r)` 返回类型满足 `queryable`。已补充约束。

落地点：

- 提交 `15b3170`，`backport/cpp26/execution.hpp` 第 620-628 行
- `receiver` concept 中 `get_env` 调用现在约束返回类型为 `queryable`

---

### I-4 缺少 `queryable` concept

结论：**接受，已修复。**

理由：

审查判断正确。`queryable` 是 `[exec.queryable]` 定义的基础 concept，是 sender/receiver 概念链的基石。

落地点：

- 提交 `15b3170`，`backport/cpp26/execution.hpp` 第 594-595 行
- 定义：`template<class T> concept queryable = std::destructible<T>;`
- I-2、I-3 中的 `get_env` 约束已引用此 concept

---

### I-5 `operation_state` concept 缺少 `!move_constructible` 负约束

结论：**接受，已修复。**

理由：

审查判断正确。当前草案 `[exec.opstate]` 明确要求 operation state 不可移动。已补充负约束并确保所有内部 operation state 类型继承不可移动基类。

落地点：

- 提交 `15b3170`，`backport/cpp26/execution.hpp`
  - 第 611-615 行：`operation_state` concept 添加 `!std::move_constructible<O>`
  - 第 261-264 行：`__immovable` mixin 基类定义
  - 所有 operation state 类型（`just_op`、`then_op`、`sync_wait_op` 等）继承 `__immovable`

---

### I-6 `inplace_stop_source::request_stop()` 在持有 mutex 时调用用户回调

结论：**接受，已修复。**

理由：

审查判断完全正确，这是本轮唯一的"当前代码在合理使用场景下会出错"的运行时 bug。用户回调在锁保护下执行会导致死锁。

修复方案：采用"持锁摘链、释锁调用"模式——在锁保护下将回调链表从 `callbacks_` 中摘出，释放锁后逐个调用回调。

落地点：

- 提交 `6f18091`，`backport/cpp26/execution.hpp` 第 97-127 行
- `request_stop()` 现在先在 `lock_guard` 作用域内设置标志并摘出回调链表，释放锁后遍历调用

---

### I-7 `inplace_stop_source` 析构函数不必要地持锁

结论：**接受，已修复。**

理由：

审查判断正确。草案前置条件要求析构前所有 callback 已销毁，无需在锁保护下清空链表。

落地点：

- 提交 `15b3170`，`backport/cpp26/execution.hpp` 第 90 行
- 改为 `~inplace_stop_source() noexcept = default;`

---

### I-8 `inplace_stop_token` 缺少 `swap` 成员函数

结论：**接受，已修复。**

理由：

审查判断正确。`[stoptoken.inplace]` 要求成员 `swap` 和 `friend swap`。

落地点：

- 提交 `15b3170`，`backport/cpp26/execution.hpp` 第 183-184 行
- 添加 `void swap(inplace_stop_token& other) noexcept` 成员函数和 `friend void swap(...)` 非成员函数

---

### I-9 缺少 `stoppable_token`、`stoppable_token_for`、`unstoppable_token` concepts

结论：**暂缓。**

理由：

审查判断成立。这三个 concepts 是 `[stoptoken.concepts]` 定义的基础设施，是 `get_stop_token` 查询和 receiver 环境约束的前提。但它们主要服务于后续 phase 的泛化 stop token 支持，当前 MVP 仅使用 `inplace_stop_token`，功能上不受阻。

落地点：

- 提交 `425ee7e`，偏差文档块中添加 TODO 标注
- 计划在 phase 1 随 `run_loop` 一起补充

---

### I-10 `sync_wait` 缺少 `run_loop`，receiver 环境不符合 `[exec.sync.wait]`

结论：**暂缓，已标注偏差。**

理由：

审查判断完全正确。`[exec.sync.wait]` 要求通过 `run_loop` 驱动，receiver 环境需提供 `get_scheduler` 和 `get_delegatee_scheduler` 返回 `run_loop` 的 scheduler。

当前 MVP 使用 `mutex + condition_variable` 阻塞等待，这是已知的架构级偏差。`run_loop` 的实现涉及完整的执行上下文和 scheduler 生命周期管理，属于 phase 1 的核心交付物。

落地点：

- 提交 `425ee7e`，头文件偏差文档块明确标注
- 标注内容：MVP 使用 mutex+cv，`run_loop` 计划 phase 1 交付，届时 `sync_wait` receiver env 将提供完整的 scheduler 查询

---

### I-11 `backport/execution` wrapper 的 `#else` 分支直接 `#error`

结论：**接受，已修复。**

理由：

审查判断正确。对不支持 `__has_include_next` 的工具链直接 `#error` 过于激进，应静默回退。

落地点：

- 提交 `6f18091`，`backport/execution`
- `#else` 分支改为注释说明，不再触发编译错误

---

### I-12 `just`/`just_error`/`just_stopped` sender 的 `get_env` 返回 `empty_env`

结论：**暂缓，已标注。**

理由：

审查判断成立。`[exec.just]` 要求 sender 环境通过 `just-env` 提供 `get_completion_scheduler<CPO>` 查询。MVP 阶段暂不实现 completion scheduler，但已在每个 sender factory 处添加 TODO 标注。

落地点：

- 提交 `425ee7e`，`backport/cpp26/execution.hpp` 中各 sender factory 处添加 TODO 注释

---

### I-13 `then_closure::operator|` 与 `[exec.adapt.obj]` sender adaptor closure 机制不符

结论：**暂缓，已标注。**

理由：

审查判断成立。当前草案 `[exec.adapt.obj]` 定义了 `sender_adaptor_closure` CRTP 基类。当前实现的逐 adaptor `friend operator|` 功能等价但结构性偏离。当仅有 `then` 一个 adaptor 时偏差影响有限，但后续添加 `let_value`、`bulk` 等时需要迁移。

落地点：

- 提交 `425ee7e`，`backport/cpp26/execution.hpp` 中添加 TODO 注释
- 计划在添加更多 adaptor 时统一迁移到 `sender_adaptor_closure` 基类

---

### I-14 `get_env` 返回类型缺少 `queryable` 约束验证

结论：**已随 I-4 解决。**

理由：

I-4 补充了 `queryable` concept 定义后，I-2 和 I-3 中 `sender`/`receiver` concept 已约束 `get_env` 返回类型满足 `queryable`。所有内置类型的 `get_env` 返回值（`empty_env`、`prop<...>`、`env<...>` 等）现在通过 concept 约束被编译期检查。

落地点：

- 提交 `15b3170`，随 I-4 的 `queryable` concept 和 I-2/I-3 的约束一起落地

---

### I-15 `request_stop()` 返回值语义与草案微差

结论：**已随 I-6 解决。**

理由：

I-6 的修复采用"释锁后调用回调"模式，确保所有回调在 `request_stop()` 返回 `true` 之前完成调用，返回值时序语义现在与 `[stopsource.inplace.mem]` 一致。

落地点：

- 提交 `6f18091`，随 I-6 修复一起落地

---

### I-16 `__forge_detail` 命名空间的 ADL 阻断

结论：**无需修复。**

理由：

审查报告本身已得出结论：ADL 阻断基本正确。`__forge_detail::__tag_invoke` 中的 poison pill（`void tag_invoke() = delete;`）位置正确，所有 `tag_invoke` friend 声明都在具体类型定义内通过 friend 注入到关联命名空间，这是正确的 ADL 使用方式。

---

### I-17 ODR 正确性：所有内联定义位置正确

结论：**无需修复。**

理由：

审查报告确认所有 CPO 实例为 `inline constexpr`，类模板成员函数在类内定义，类外定义标记 `inline`，无 ODR 问题。

---

### T-1 缺少编译探针验证概念约束正/反例

结论：**接受，已修复。**

理由：

审查判断完全正确。编译探针是防止概念定义回归的核心防线，之前仅有一处 `static_assert`。

落地点：

- 提交 `cccccfd`，`test/execution/runtime/test_execution_mvp.cpp`
- 新增 `static_assert` 覆盖：
  - 正例：`sender<decltype(just(...))>`、`sender_in<...>`、`receiver<...>`、`scheduler<...>`、`operation_state<...>`
  - 反例：`!sender<int>`、`!receiver<int>`、`!scheduler<int>`
  - completion-signature 类型恒等检查

---

### T-2 缺少 scheduler env roundtrip 运行时验证

结论：**接受，已修复。**

理由：

v1-r0 T-1 遗留项，本轮已补充。

落地点：

- 提交 `cccccfd`，`test/execution/runtime/test_execution_inline_scheduler.cpp`
- 新增 `ScheduleEnvRoundtrip` 测试：验证 `get_scheduler(get_env(schedule(sch))) == sch`

---

### T-3 缺少 `then` 多值输入测试

结论：**接受，已修复。**

理由：

v1-r0 T-3 遗留项，本轮已补充多值输入的运行时测试。

落地点：

- 提交 `cccccfd`，`test/execution/runtime/test_execution_mvp.cpp`
- 新增 `ThenMultiValueInput` 测试：`just(1, 2) | then(add)` 验证多值参数传递

---

### T-4 `stop_token` 测试覆盖面不足

结论：**接受，已修复。**

理由：

审查判断正确，原先仅 2 个测试覆盖面明显不足。

落地点：

- 提交 `cccccfd`，`test/execution/runtime/test_execution_stop_token.cpp`
- 新增 4 个测试：
  - `PostStopCallbackImmediateInvocation`：stop 已请求后注册 callback 应立即触发
  - `CallbackAutoDeregisterOnDestruction`：callback 析构时自动反注册
  - `NeverStopTokenBehavior`：`never_stop_token` 基本行为
  - `DefaultConstructedToken`：默认构造 `inplace_stop_token` 的 `stop_possible() == false`

---

## 遗留问题清单

本轮修复后，以下问题处于"暂缓"状态，需在后续轮次处理：

| 编号 | 内容 | 计划阶段 |
|------|------|----------|
| I-1  | tag_invoke → 成员函数优先分发迁移 | phase 1 |
| I-9  | stoppable_token concepts 补充 | phase 1 |
| I-10 | run_loop 实现 + sync_wait 对齐 | phase 1 |
| I-12 | just sender env 补充 completion scheduler | phase 1 |
| I-13 | sender_adaptor_closure CRTP 基类迁移 | phase 1 |

---

## 提交序列

| 顺序 | 提交 | 覆盖问题 |
|------|------|----------|
| 1 | `9298895` review: add p0-v0-r1 standards-quality review for std::execution | 审查报告 |
| 2 | `6f18091` execution: fix request_stop() deadlock and wrapper #else error | I-6, I-11 |
| 3 | `15b3170` execution: tighten concept definitions and operation_state conformance | I-2, I-3, I-4, I-5, I-7, I-8 (+ I-14) |
| 4 | `425ee7e` execution: document known deviations from [exec] draft wording | I-1, I-10, I-12, I-13 (+ I-9) |
| 5 | `cccccfd` test: add compile probes and expand execution test coverage | T-1, T-2, T-3, T-4 |

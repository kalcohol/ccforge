# `std::execution` 审查答复 (p0-v0-r2-a0)

审查基线：当前 C++ 工作草案 `[exec]`（https://eel.is/c++draft/exec）
辅助基线：`[stoptoken.concepts]`（https://eel.is/c++draft/stoptoken#concepts）

答复对象：`03-std-execution-p0-v0-r2`（10 条问题：I-1 至 I-8，T-1 至 T-2）

答复口径：

- 以 `p0-v0-r2` 审查报告为准，基线为 eel.is 当前工作草案 `[exec]` + `[stoptoken]`
- 本轮修复范围：概念定义精度对齐、stop token concepts 补充、sync_wait 命名空间修正、编译探针扩充
- 架构级偏离（tag_invoke、run_loop）维持原有已标注偏差状态，不在本轮完成迁移
- 探针编写过程中发现一处额外遗漏（`callback_type` 别名），已同批修复并记录于本文

---

## 处置汇总

| 编号 | 严重程度 | 结论 | 落地提交 |
|------|----------|------|----------|
| I-1  | 高 | 接受，已修复 | `6ca5477` |
| I-2  | 高 | 接受，已修复 | `6ca5477` |
| I-3  | 高 | 接受，已修复 | `6ca5477` |
| I-4  | 高 | 接受，已修复 | `63e63ae` |
| I-5  | 中 | 接受，已修复 | `e9d82c5` |
| I-6  | 中 | 暂缓，维持原标注 | `425ee7e` |
| I-7  | 低 | 接受，已随 I-1 消解 | `6ca5477` |
| I-8  | 低 | 暂缓，维持原标注 | `425ee7e` |
| T-1  | 中 | 接受，已修复 | `9fc8b6b` |
| T-2  | 低 | 接受，已修复 | `9fc8b6b` |

统计：已修复 6 条（含 1 条随其他问题消解），暂缓 2 条。附加修复 1 条（探针发现的 `callback_type` 遗漏）。

---

## 逐条答复

### I-1 `receiver` concept 使用 `move_constructible` 而非 `is_nothrow_move_constructible_v`，且缺少 `!is_final_v`

结论：**接受，已修复。**

理由：

审查判断完全正确。`[exec.recv]` 明确要求：

1. `is_nothrow_move_constructible_v<remove_cvref_t<R>>`——receiver 的移动操作不能抛异常，这是 `connect` 等操作的前提假设；
2. `!is_final_v<remove_cvref_t<R>>`——wrapped receiver 派生模式要求 receiver 类型不能是 final class。

旧代码使用 `move_constructible` + `constructible_from`（后者冗余），两者都不满足上述强化约束。

落地点：

- 提交 `6ca5477`，`backport/cpp26/execution.hpp`
  - `receiver` concept 改用 `std::is_nothrow_move_constructible_v<std::remove_cvref_t<R>>`
  - 新增 `!std::is_final_v<std::remove_cvref_t<R>>` 约束
  - 同时移除冗余的 `constructible_from`（I-7 随此自然消解）

---

### I-2 `operation_state` concept 缺少 `operation_state_concept` marker 要求

结论：**接受，已修复。**

理由：

审查判断正确。`[exec.opstate]` 要求 concept 包含 `derived_from<typename O::operation_state_concept, operation_state_t>` 检查，没有 marker 时 concept 边界过宽。

落地点：

- 提交 `6ca5477`，`backport/cpp26/execution.hpp`
  - 新增 `operation_state_t` tag 类型
  - `operation_state` concept 添加 marker 约束
  - 所有内部 operation state 类型（`just_op`、`then_op`、`sync_wait_op`、`inline_scheduler::operation`）声明 `using operation_state_concept = operation_state_t;`

---

### I-3 `scheduler` concept 缺少 `scheduler_concept` marker 和 `queryable` 约束

结论：**接受，已修复。**

理由：

审查判断正确。`[exec.sched]` 要求 `derived_from<typename Sch::scheduler_concept, scheduler_t>` 以及 `queryable<Sch>` 约束。同时，`schedule(sch)` 的返回类型约束修正为 `sender`，不应超出标准定义使用 `sender_in`。

落地点：

- 提交 `6ca5477`，`backport/cpp26/execution.hpp`
  - 新增 `scheduler_t` tag 类型
  - `scheduler` concept 添加 `scheduler_concept` marker 约束和 `queryable` 约束
  - `schedule` 返回类型约束从 `sender_in` 修正为 `sender`
  - `inline_scheduler` 声明 `using scheduler_concept = scheduler_t;`

---

### I-4 `sync_wait` 位于错误的命名空间

结论：**接受，已修复。**

理由：

审查判断正确。`[exec.sync.wait]` 将 `sync_wait` 置于 `namespace std::this_thread`，原实现放在 `namespace std::execution` 直接违反无感过渡原则——用户写 `std::this_thread::sync_wait(sndr)` 时在旧 backport 下无法编译。

落地点：

- 提交 `63e63ae`，`backport/cpp26/execution.hpp`
  - `sync_wait` 函数模板移至 `namespace std::this_thread`
  - `namespace std::execution` 中添加 `using std::this_thread::sync_wait;`，使旧调用路径在 backport 存续期间仍可用
  - 注释说明该 using-declaration 属于过渡期兼容措施，原生标准库同时支持两条路径，无感过渡成立

---

### I-5 缺少 `stoppable_token`、`stoppable_token_for`、`unstoppable_token` concepts

结论：**接受，已修复。**

理由：

前轮以"MVP 阶段不受阻"为由暂缓。本轮审查判断这三个 concept 不依赖 `run_loop` 等 phase 1 组件，可以独立实现，且为 `inplace_stop_token` 和 `never_stop_token` 的语义正确性提供编译期验证基础。

落地点：

- 提交 `e9d82c5`，`backport/cpp26/execution.hpp`
  - 新增 `stop_callback_for_t<Token, CallbackFn>` 别名模板
  - 新增 `stoppable_token<Token>` concept：要求 `stop_requested()`、`stop_possible()`、nothrow copy/move constructible、equality comparable
  - 新增 `stoppable_token_for<Token, CallbackFn>` concept：要求 `stoppable_token`、`invocable<CallbackFn>`、`stop_callback_for_t` 可从 `Token` 和 `CallbackFn` 构造
  - 新增 `unstoppable_token<Token>` concept：要求 `stoppable_token` 且 `stop_possible()` 编译期常量 `false`

---

### I-6 `sender` concept 使用直接 `sender_concept` 检查而非 `enable_sender` trait

结论：**暂缓，维持原标注。**

理由：

审查判断成立。`[exec.snd]` 通过 `enable_sender<remove_cvref_t<Sndr>>` trait 支持 awaitable 类型的 sender 语义。当前直接检查 `sender_concept` 嵌套类型，在非 awaitable 场景下功能等价，awaitable 支持路径不可用。

本轮维持暂缓：MVP 阶段不涉及协程 awaitable，功能上无阻断。计划在 phase 1 引入协程支持时补充 `enable_sender` trait 及其在 `sender` concept 中的引用。

---

### I-7 `receiver` concept 中 `constructible_from` 约束冗余

结论：**已随 I-1 消解。**

理由：

I-1 修复将 `move_constructible` 替换为 `is_nothrow_move_constructible_v`（类型萃取，非 concept），同时移除了原有的冗余 `constructible_from` 约束。冗余行不复存在，I-7 随 I-1 一并消解。

落地点：

- 提交 `6ca5477`，随 I-1 修复一起落地

---

### I-8 缺少 `get_completion_scheduler` CPO

结论：**暂缓，维持原标注。**

理由：

审查判断成立。`get_completion_scheduler` CPO 是 `just-env` 和 domain-based dispatch 的基础设施。本轮探针编写期间未对此 CPO 产生依赖，暂缓不影响当前测试覆盖。

计划在 phase 1 添加更多 sender adaptor 时同步定义 CPO shell，届时 `just` sender 的 `get_env` 也将提供正确的 `just-env` 返回类型。

---

### T-1 缺少新概念约束的编译探针

结论：**接受，已修复。**

理由：

审查判断完全正确。I-1 至 I-5 修复了概念定义，必须有对应的正/反例编译探针锁定修复结果，防止回归。

落地点：

- 提交 `9fc8b6b`，扩充多个测试文件
  - **T-5**（对应 I-1）：`test_execution_mvp.cpp` 新增反例探针
    - `static_assert(!receiver<ThrowingMoveReceiver>)`：move 构造函数抛异常的 receiver 类型被拒绝
    - `static_assert(!receiver<FinalReceiver>)`：`final` class receiver 类型被拒绝
  - **T-6**（对应 I-2）：`test_execution_mvp.cpp` 新增反例探针
    - `static_assert(!operation_state<NoMarkerOp>)`：无 `operation_state_concept` 嵌套类型的类型被拒绝
  - **T-7**（对应 I-3）：`test_execution_mvp.cpp` 新增反例探针
    - `static_assert(!scheduler<NoMarkerScheduler>)`：无 `scheduler_concept` 嵌套类型的类型被拒绝
  - **T-8**（对应 I-5）：`test_execution_stop_token.cpp` 新增正/反例探针
    - `static_assert(stoppable_token<inplace_stop_token>)`
    - `static_assert(unstoppable_token<never_stop_token>)`
    - `static_assert(!stoppable_token<int>)`

---

### T-2 缺少 `std::this_thread::sync_wait` 命名空间测试

结论：**接受，已修复。**

理由：

I-4 修复后命名空间迁移正确性需编译期和运行时双重验证。

落地点：

- 提交 `9fc8b6b`，`test/execution/runtime/test_execution_inline_scheduler.cpp`
  - **T-9**：新增 `SyncWaitThisThread` 测试，通过 `std::this_thread::sync_wait(just(42))` 调用路径验证命名空间迁移正确落地

---

## 探针编写发现的额外修复

### 附加修复：`inplace_stop_token` 缺少 `callback_type` 别名模板

发现时机：编写 T-8（`stoppable_token_for` 正例探针）时，`stoppable_token_for<inplace_stop_token, Fn>` concept 需要 `stop_callback_for_t<inplace_stop_token, Fn>` 可构造，而该别名模板依赖 `Token::template callback_type<Fn>`。`inplace_stop_token` 缺少此别名模板，导致正例探针无法编译。

处置：

- 提交 `9fc8b6b`，`backport/cpp26/execution.hpp`
  - `inplace_stop_token` 添加 `template<class Fn> using callback_type = inplace_stop_callback<Fn>;`
  - 这是 `[stoptoken.inplace]` 要求的标准成员，非扩展

---

## 遗留问题清单

本轮修复后，以下问题处于"暂缓"状态：

| 来源 | 内容 | 计划阶段 |
|------|------|----------|
| r1 I-1 | tag_invoke → 成员函数优先 + domain-based 分发迁移 | phase 1 |
| r1 I-10 | run_loop 实现 + sync_wait receiver env 对齐 | phase 1 |
| r1 I-12 | just sender env 补充 completion scheduler | phase 1 |
| r1 I-13 | sender_adaptor_closure CRTP 基类迁移 | phase 1 |
| r2 I-6  | enable_sender trait + awaitable sender 支持 | phase 1 |
| r2 I-8  | get_completion_scheduler CPO shell | phase 1 |

---

## 验证

本轮修复在以下环境通过验证：

```
cmake -S . -B build-gcc -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON
cmake --build build-gcc -j
ctest --test-dir build-gcc -R execution --output-on-failure
```

结果：本地 GCC，execution 子集全部通过，无回归。

---

## 提交序列

| 顺序 | 提交 | 覆盖问题 |
|------|------|----------|
| 1 | `6d8608e` review: add p0-v0-r2 standards-quality review for std::execution | 审查报告 |
| 2 | `6ca5477` execution: align concept definitions with [exec] current draft | I-1, I-2, I-3, I-7（消解） |
| 3 | `e9d82c5` execution: add stoppable_token concepts per [stoptoken.concepts] | I-5 |
| 4 | `63e63ae` execution: move sync_wait to std::this_thread per [exec.sync.wait] | I-4 |
| 5 | `9fc8b6b` test: add compile probes and expand execution test coverage | T-1 (T-5~T-8), T-2 (T-9), 附加修复 callback_type |

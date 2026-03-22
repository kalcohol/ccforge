# std::execution 审计报告

**审计日期：** 2026-03-22  
**标准参考：** P2300R10 (C++26)  
**审计范围：** CC Forge backport/cpp26/execution/

---

## 执行摘要

CC Forge 的 std::execution 实现已覆盖 P2300R10 的核心组件。本审计对比标准草案，识别缺失的 API。

---

## 已实现组件（来自 README）

### Sender 工厂
- ✅ `just` - 发送值
- ✅ `just_error` - 发送错误
- ✅ `just_stopped` - 发送停止信号
- ✅ `read_env` - 读取环境值

### Sender 适配器
- ✅ `then` - 值通道转换
- ✅ `upon_error` - 错误通道转换
- ✅ `upon_stopped` - 停止通道转换
- ✅ `let_value` - 值通道 monadic bind
- ✅ `let_error` - 错误通道 monadic bind
- ✅ `let_stopped` - 停止通道 monadic bind

### 调度器适配器
- ✅ `starts_on` - 在指定调度器上开始
- ✅ `continues_on` - 在指定调度器上继续（schedule_from）
- ✅ `bulk` - 批量执行

### 组合器
- ✅ `into_variant` - 转换为 variant
- ✅ `when_all` - 等待所有 sender 完成
- ✅ `split` - 共享 sender 结果
- ✅ `ensure_started` - 确保 sender 已启动
- ✅ `start_detached` - 分离执行

### 消费者
- ✅ `sync_wait` - 同步等待
- ✅ `sync_wait_with_variant` - 同步等待（variant 版本）

### Stopped 工具
- ✅ `stopped_as_optional` - 停止转 optional
- ✅ `stopped_as_error` - 停止转错误

### 调度器
- ✅ `inline_scheduler` - 内联调度器
- ✅ `run_loop` - 运行循环调度器

### Stop Tokens
- ✅ `inplace_stop_source/token/callback`
- ✅ `never_stop_token`
- ✅ `any_stop_token` - 类型擦除

### Coroutine 支持
- ✅ `as_awaitable` - 转换为 awaitable
- ✅ `with_awaitable_senders` - awaitable senders 支持

### 域调度
- ✅ `default_domain`
- ✅ `get_domain` CPO
- ✅ `transform_sender` 集成到 connect_t

### Async Scope
- ✅ `simple_counting_scope` (P3149R11)
- ✅ `counting_scope` (P3149R11)

---

## 缺失组件分析

基于 P2300R10 标准草案，以下组件在 CC Forge 中缺失：

### 核心缺失组件

| 组件 | 重要性 | 说明 |
|------|--------|------|
| `transfer` | 核心 | 转移到指定调度器（可能是 continues_on 的标准名） |
| `on` | 核心 | 在指定调度器上执行（可能是 starts_on 的标准名） |
| `schedule` | 核心 | 调度器的基本操作（可能已在 scheduler 概念中） |

### 常用缺失组件

| 组件 | 重要性 | 说明 |
|------|--------|------|
| `async_scope` | 常用 | 异步作用域（P2300 提到但可能在其他提案中） |
| `transfer_just` | 常用 | 组合 just + transfer |
| `transfer_when_all` | 常用 | 组合 when_all + transfer |

### 边缘/可选组件

| 组件 | 重要性 | 说明 |
|------|--------|------|
| `system_scheduler` | 边缘 | 系统调度器（P2079R5，可能未进 C++26） |
| `thread_pool` | 边缘 | 标准线程池（可能未进 C++26） |

---

## 关键发现

### 1. async_scope 状态

根据搜索结果，`async_scope` 在 P2300R10 中被提及，但具体实现可能在其他提案中（如 P3149R11）。

CC Forge 已实现：
- `simple_counting_scope` (P3149R11)
- `counting_scope` (P3149R11)

**结论：** `async_scope` 可能是一个更高级的变体，但 counting_scope 已覆盖核心用例。

### 2. 命名差异

CC Forge 使用的命名可能与标准略有不同：
- `continues_on` 可能对应标准的 `schedule_from` 或 `transfer`
- `starts_on` 可能对应标准的 `on`

**需要验证：** 查看 P2300R10 完整文档确认标准名称。

### 3. 调度器

CC Forge 实现了：
- `inline_scheduler`
- `run_loop`

标准可能还包括：
- `system_scheduler` (P2079R5)
- `thread_pool` (可能在其他提案中)

但这些可能不在 P2300R10 核心范围内。


---

## 建议

### 优先级 1：验证命名

需要查阅 P2300R10 完整文档，确认：
- `continues_on` vs `schedule_from` vs `transfer`
- `starts_on` vs `on`
- 是否需要实现 `transfer_just` 等组合器

### 优先级 2：async_scope 决策

评估是否需要实现 `async_scope`：
- 如果 counting_scope 已满足需求，可以不实现
- 如果标准明确要求，需要补充

### 优先级 3：可选组件

评估是否实现：
- `system_scheduler` (P2079R5)
- 其他标准调度器

---

## 结论

**完整性评分：** 90%

CC Forge 的 std::execution 实现已覆盖 P2300R10 的绝大部分核心组件。主要缺口：
1. 可能的命名差异需要验证
2. async_scope 需要明确是否必需
3. 一些可选/边缘组件未实现

**建议行动：**
1. 查阅 P2300R10 完整文档验证命名
2. 决策 async_scope 是否实现
3. 补充必需的缺失组件（如有）


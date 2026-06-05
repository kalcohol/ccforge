# `forge::erased_sender` 设计与限制

`forge::erased_sender<CompletionSignatures>` 是 Forge 扩展设施，不是标准库 backport。它提供 connectable sender 类型擦除，并刻意与较窄的 `forge::any_sender_of` 分离。

## 当前状态

已实现 v1：

```cpp
#include <forge/erased_sender.hpp>

namespace forge {

template<class CompletionSignatures>
class erased_sender;

} // namespace forge
```

`<forge/execution.hpp>` 也会包含该头。

## 与 `any_sender_of` 的关系

`forge::any_sender_of<CompletionSignatures>` 继续保持窄语义：

- 单一 value 形状；
- error 通过 `sync_wait()` 异常路径折叠；
- 提供 `sync_wait()` 便利路径；
- 不提供通用 connectable erased sender 接口。

`forge::erased_sender` 是独立类型，不会静默扩大 `any_sender_of` 的行为。

## 支持范围

支持的 completion signatures：

- 任意数量的唯一 `set_value_t(Vs...)` value 形状；
- 任意数量显式声明的 `set_error_t(E)` error 形状；
- 可选 `set_stopped_t()`。

不支持：

- allocator-aware erased storage；
- semantic equality；
- SBO；
- 任意自定义 receiver env query 透传；
- 改变 `any_sender_of`。

Error erasure 是闭集模型：source sender 声明的每个 `set_error_t(E)` 都必须出现在
`erased_sender<CompletionSignatures>` 的目标签名里，未声明 error type 会在构造或
connectability 检查时被拒绝。不会提供“任意 error 动态兜底”。

## 所有权模型

v1 是 heap-first 实现：

- erased sender 持有 erased source state；
- `connect(erased_sender, receiver)` 创建 heap-owned erased operation；
- erased operation 拥有 concrete operation state；
- operation 额外持有 source state，避免 concrete operation 引用 source sender 时悬垂；
- `start()` 只转发到 concrete operation state。

`erased_sender` 本身是 move-only。具体 source sender 需要能以存储后的 lvalue 形式连接到 erased receiver；move-only source sender 可以工作，只要它的 `connect` 支持这种用法。

## Value dispatch（值分发）

实现使用 `include/forge/detail/completion_meta.hpp` 中的 Forge-local value-shape meta：

- value shape 以 decayed `tuple<...>` 归一；
- 多 value shape 通过生成的 erased receiver 分发表按实际 completion 分派；
- 这套 meta 只依赖 public `<execution>` surface，避免 `include/forge/` 继续耦合
  backport 私有 detail。

## Error、stopped 与 env

`set_error` 会按目标 `CompletionSignatures` 中声明的 error type 分派并原样交给下游
receiver。`std::exception_ptr` 只是其中一种普通 error type，不再是唯一支持类型。

`set_stopped_t()` 只有在 `CompletionSignatures` 声明时才属于有效契约。

receiver env v1 只保证 stop token 传播：erased receiver 会把下游 receiver 的 stop token 擦成 `std::any_stop_token` 并通过 `get_stop_token` 暴露给源 sender。任意自定义 env query 不在 v1 范围内。

## 测试覆盖

当前测试覆盖：

- 单 value shape；
- 多 value shape 分派；
- 零参数 `set_value_t()`；
- `set_error_t(std::exception_ptr)`；
- 多 typed error 形状；
- `set_stopped_t()`；
- undeclared typed error 编译期拒绝；
- downstream stop token 传播；
- move-only source sender；
- sanitizer gate 下的 erased operation/receiver 生命周期。

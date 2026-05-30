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

## V1 支持范围

支持的 completion signatures：

- 任意数量的唯一 `set_value_t(Vs...)` value 形状；
- 可选 `set_error_t(std::exception_ptr)`；
- 可选 `set_stopped_t()`。

不支持：

- typed error，例如 `set_error_t(std::error_code)`；
- allocator-aware erased storage；
- semantic equality；
- SBO；
- 任意自定义 receiver env query 透传；
- 改变 `any_sender_of`。

typed error 是有意拒绝的：任意 error alternative 会需要更大的 receiver vtable 矩阵，后续如确有需要应作为单独扩展设计。

## 所有权模型

v1 是 heap-first 实现：

- erased sender 持有 erased source state；
- `connect(erased_sender, receiver)` 创建 heap-owned erased operation；
- erased operation 拥有 concrete operation state；
- operation 额外持有 source state，避免 concrete operation 引用 source sender 时悬垂；
- `start()` 只转发到 concrete operation state。

`erased_sender` 本身是 move-only。具体 source sender 需要能以存储后的 lvalue 形式连接到 erased receiver；move-only source sender 可以工作，只要它的 `connect` 支持这种用法。

## Value Dispatch

实现复用 `backport/cpp26/execution/detail/value_result.hpp` 里的 value-shape meta：

- value shape 以 decayed `tuple<...>` 归一；
- 多 value shape 通过生成的 erased receiver 分发表按实际 completion 分派；
- 不为 `include/forge/` 再造一套独立 value typelist 规则。

## Error、Stopped 与 Env

`set_error` 只支持 `std::exception_ptr`。源 sender 如果声明其它 typed error，构造 `erased_sender` 会在编译期被拒绝。

`set_stopped_t()` 只有在 `CompletionSignatures` 声明时才属于有效契约。

receiver env v1 只保证 stop token 传播：erased receiver 会把下游 receiver 的 stop token 擦成 `std::any_stop_token` 并通过 `get_stop_token` 暴露给源 sender。任意自定义 env query 不在 v1 范围内。

## 测试覆盖

当前测试覆盖：

- 单 value shape；
- 多 value shape 分派；
- 零参数 `set_value_t()`；
- `set_error_t(std::exception_ptr)`；
- `set_stopped_t()`；
- typed error 编译期拒绝；
- downstream stop token 传播；
- move-only source sender；
- sanitizer gate 下的 erased operation/receiver 生命周期。

# Forge 原语贡献指南

本文面向新增或维护 `forge::` 原语的贡献者。用户契约见 [runtime](forge-runtime.md) 和 [IO](forge-io.md)。

## 取消层级

Forge primitive 应优先提供小而明确的 guarantee，而不是半正确的大 guarantee。

对 pending operation cancellation，v1 primitive 可以选择以下层级之一：

- 只在 pre-start 观察 stop token；
- 只通过 primitive-owned close/shutdown wakeup 取消；
- full per-operation stop-callback cancellation。只有在 callback lifetime 和 exactly-once
  completion 已证明并在 sanitizer 下测试后，才应选择这个层级。

如果没有实现 full callback cancellation，文档必须明确写出缺失的 case。

## Shutdown checklist（关闭检查表）

写一个拥有型 runtime 或 session 时，按这个顺序检查：

1. 新入口是否有 `close()` 或 `shutdown()` 后拒绝策略。
2. Pending operation 是否会被 close/stop 唤醒。
3. Receiver completion 是否在内部锁外调用。
4. Receiver stop-token 的 callback 生命周期是否短于 receiver completion 生命周期。
5. `wait()` 是否只承诺有限 drain，还是需要无界 loop-until-idle。
6. 析构是否可能阻塞；如果会，文档是否写清楚。
7. Examples 是否展示了正常完成和停机路径。


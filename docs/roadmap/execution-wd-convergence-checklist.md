# `std::execution` 当前 WD 收敛清单

这份清单记录 execution backport 从早期 Forge/stdexec-era 混合 surface 收敛到当前
working-draft-shaped subset 的执行口径。它与更完整的
[`std::execution` 一致性台账](execution-conformance-ledger.md)配套使用。

本轮主要核对的 draft reference：

- [`execution::spawn`](https://eel.is/c++draft/exec.spawn)
- [`execution::spawn_future`](https://eel.is/c++draft/exec.spawn.future)
- [Counting scopes](https://eel.is/c++draft/exec.counting.scopes)
- [`simple_counting_scope::token`](https://eel.is/c++draft/exec.simple.counting.token)
- [`counting_scope`](https://eel.is/c++draft/exec.scope.counting)

## 目标状态

| 区域 | 当前 WD 目标 | 当前 Forge 状态 | 操作 |
| --- | --- | --- | --- |
| `spawn` | `std::execution::spawn(sndr, token, env)` 是 `void` CPO，分配 detached operation，通过 `token.try_associate()` 关联并 eager start。 | 已实现 `spawn(sndr, token[, env])`；只接受 `set_value()` / `set_stopped()` sender。 | 示例和测试保持 top-level `spawn`；不要让标准路径回到 token-member helper。 |
| `spawn_future` | 使用 `token.wrap(sndr)`，从 `env` 或 wrapped sender env 取 allocator，eager state，abandon 时取消，consumer stop callback。 | eager state、shared-state allocator 和 consumer record allocator 已实现；未消费 sender 与 unstarted connected operation 都会 abandon producer；consumer callback 直接使用 receiver env 的具体 stop-token 类型。 | `spawn_future` 不依赖非标准 erased-token vocabulary；Forge runtime 的 type erasure 独立留在 `forge::any_stop_token`。 |
| `simple_counting_scope::token::wrap` | 返回 `std::forward<Sender>(snd)`，不创建 association。 | 已实现为 identity forwarding。 | Association 保留在 top-level algorithms。 |
| `counting_scope::token::wrap` | 返回带 stop-aware child env 的 sender。 | 已实现 fused stop-token env injection：scope stop 与下游 receiver/env stop 会融合；不拥有 association。 | Association 保留在 top-level algorithms，继续覆盖 fused stop callback lifetime。 |
| `scope_token::associate` | 当前目标 surface 没有 token-member `associate`。 | 已从 scope token 移除；top-level `associate(sender, token)` 已实现。 | 保持 token surface 精简。 |
| `scope_token::spawn` | 当前目标 surface 没有 token-member `spawn`。 | 已从 scope token 移除；top-level `spawn(sender, token[, env])` 已实现。 | 示例和测试保持 top-level `spawn`。 |
| `simple_counting_scope::join` / `counting_scope::join` | 返回 scope-join sender shape；空 scope 内联完成，非空 scope 排空后在 receiver env 的 start scheduler 上完成。 | 已实现 async sender-returning join；`start()` 登记非空 join operation 后立即返回，排空路径启动预连接的 scheduler operation。 | 保持 zero-count inline、registered waiter、start-scheduler handoff、single-thread composition 和 completion self-destroy 测试。 |
| `ensure_started` | 不是当前 WD `[exec]` surface。 | 已从 `<execution>` backport 移除。 | 不回到 `std::execution`；未来如需工具，只能放在 `forge::`。 |
| `start_detached` | 不是当前 WD `[exec]` surface。 | 已从 `<execution>` backport 移除；utility 行为由 `forge::start_detached` 承接。 | 标准路径使用 `spawn`；detach utility 保持在 `forge::`。 |
| Non-copyable lvalue sender convenience | Native-shaped code 要求显式 `std::move(sndr)`。 | 标准形状 backport 路径对 non-copyable lvalue sender 要求显式 move；`forge::async_scope` 保留已文档化的 Forge-only convenience。 | 示例和测试显式写 `std::move`；不要在标准 adaptor 里恢复 destructive lvalue connect。 |
| Domain dispatch | 完整递归 current-WD model。 | `connect` 先应用 sender completion-domain recursion，再应用 receiver start-domain recursion；非 default-domain 的 completion-signature query 会先重算 transformed sender。 | 保持 `connect` 和 completion-signature 测试，包括 rawless source sender 经 transform 获救的路径。 |

## 标准 surface 清理顺序

1. 先实现足够的 `spawn`，使标准路径能一致地替代 `start_detached`。
2. 示例和测试使用 top-level `spawn` / `associate` spelling，并保持 `join()` 的 sender-consuming 形态。
3. `ensure_started` 和 `start_detached` 不再进入 `std::execution` public surface。
4. 标准 backport adaptor 不提供 destructive-move lvalue convenience。
5. Token `wrap` 语义变化后重跑 `spawn_future`，因为当前 draft 会在 allocator/env selection 前调用 `token.wrap(sndr)`。

## 聚焦验证映射

| 改动 | 聚焦检查 |
| --- | --- |
| `spawn` / scope / join | `execution_counting_scope`, `execution_spawn_future`, `execution_api_core`, `gcc-exec`, `tsan`, `asan` |
| Non-WD surface 移除或迁移 | `execution_wave1`, `execution_wave2`, `execution_api_core`, 受影响的 `forge_` 测试 |
| Lvalue move 清理 | `execution_adaptors`, `execution_wave1`, `execution_spawn_future`, `forge_async_scope`, `forge_task` |
| Domain recursion | `execution_domain`, `execution_adaptors`, `execution_wave1`, `gcc-exec` |
| `spawn_future` allocator audit | `execution_spawn_future`, `gcc-exec`, `tsan`, `asan` |

## 隐私和平台说明

这轮不依赖私有硬件、私有 Windows 路径或 vendor SDK。若触碰
Windows-sensitive 脚本或 IOCP 代码，按公开文档运行 Windows matrix，不要提交主机名或本地安装路径。

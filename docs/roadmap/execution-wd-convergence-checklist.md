# `std::execution` 当前 WD 收敛清单

这份清单记录 execution backport 从早期 Forge/stdexec-era 混合 surface 收敛到当前
working-draft-shaped subset 的执行口径。它与更完整的
[`std::execution` 一致性台账](execution-conformance-ledger.md)配套使用。

本清单在 2026-08-23 对照 2026-08-22 working-draft snapshot
`2f5924ff8ee5` 复核。主要 draft reference：

- [`as_awaitable`](https://eel.is/c++draft/exec.as.awaitable)
- [`schedule_from`](https://eel.is/c++draft/exec.schedule.from)
- [`transform_sender`](https://eel.is/c++draft/exec.snd.transform)
- [`apply_sender`](https://eel.is/c++draft/exec.snd.apply)
- [`execution::spawn`](https://eel.is/c++draft/exec.spawn)
- [`execution::spawn_future`](https://eel.is/c++draft/exec.spawn.future)
- [Counting scopes](https://eel.is/c++draft/exec.counting.scopes)
- [`simple_counting_scope::token`](https://eel.is/c++draft/exec.simple.counting.token)
- [`counting_scope`](https://eel.is/c++draft/exec.scope.counting)
- [Task family](https://eel.is/c++draft/exec.task)

## 目标状态

| 区域 | 当前 WD 目标 | 当前 Forge 状态 | 操作 |
| --- | --- | --- | --- |
| `as_awaitable` | 按 direct member、transformed/adapted member、ordinary awaiter、transformed/adapted sender bridge、identity fallback 的顺序分派。 | 已实现该五段分派；sender bridge 覆盖零值、单值与单一多参数 value completion。 | 保持分派优先级、普通 awaiter 解析和同步完成生命周期测试；COMPLETE/PARTIAL 探针要求 CPO 本体。 |
| `schedule_from` | 单参数 departure marker sender，由 completion domain 识别 source resource departure。 | 已实现 current-shaped marker；`continues_on` 经它连接 source sender。 | 不恢复旧 scheduler-plus-sender 形状；保持 product access、attributes、signatures 和 operation forwarding 测试。 |
| `transform_sender` / `apply_sender` | 公开 CPO，执行 completion-domain 与 start-domain 递归，并提供 explicit-domain-first / algorithm-tag fallback。 | 已实现 public current-shaped CPO、recursive dispatch、forwarding 和条件 `noexcept`；旧两参数 domain transform 仅作兼容扩展。 | COMPLETE/PARTIAL 探针必须覆盖两个入口；保持 raw sender 经 transform 获救和 customization order 测试。 |
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
| `task`, `task_scheduler`, `with_error` | 已进入 current working draft。 | 暂不实现。P3552R3 已进入 working paper，但 P4007R3 仍记录包含发布后难以兼容修正的开放问题；稳定性 gate 未通过。 | 只有独立 taskbook 固定 wording revision、frame/operation ownership、allocator、stop/error、self-destroy 和 handoff probes 后才重开。 |
| `inplace_stop_callback` construction | Current WD 要求 callback storage 就地持有，并带条件 `noexcept`。 | 为保持已验证的 request-stop / deregistration / reentrant self-destroy 行为，当前 control block 会分配，构造也不是该条件 `noexcept`。 | 作为已接受 conformance deviation 保留；没有 correctness bug、consumer requirement 或完整 ownership proof 时不重写。 |

## 标准 surface 清理顺序

1. `as_awaitable` 按 current dispatch order 收敛，并用普通 awaiter、sender bridge 和同步完成测试固定顺序。
2. `schedule_from` 采用单参数 departure marker；`continues_on` 经 marker 接入 completion-domain customization。
3. `transform_sender` / `apply_sender` 公开 current-shaped CPO，并使 connect、completion signatures 和 awaitable bridge 共用递归模型。
4. task family 先过稳定性与 ownership gate；未通过时只记录 deferred，不占用 `std::execution` 名字。
5. `inplace_stop_callback` 的零分配改写必须先证明 reentrant ownership；否则保留已接受偏差。
6. `spawn`、scope、join、token wrap 和 non-WD surface 清理继续保持既有 current-shaped 结果。

## 聚焦验证映射

| 改动 | 聚焦检查 |
| --- | --- |
| `as_awaitable` dispatch | `execution_awaitable`, fragment includes, `execution_api_core`, COMPLETE/PARTIAL handoff roots, `gcc-exec`, `asan` |
| `schedule_from` marker | `execution_schedule_from`, `execution_continues_on`, fragment includes, COMPLETE/PARTIAL handoff roots |
| `transform_sender` / `apply_sender` | `execution_domain`, `execution_adaptors`, `execution_wave1`, COMPLETE/PARTIAL handoff roots, `gcc-exec`, `asan` |
| `spawn` / scope / join | `execution_counting_scope`, `execution_spawn_future`, `execution_api_core`, `gcc-exec`, `tsan`, `asan` |
| Non-WD surface 移除或迁移 | `execution_wave1`, `execution_wave2`, `execution_api_core`, 受影响的 `forge_` 测试 |
| Lvalue move 清理 | `execution_adaptors`, `execution_wave1`, `execution_spawn_future`, `forge_async_scope`, `forge_task` |
| Domain recursion | `execution_domain`, `execution_adaptors`, `execution_wave1`, `gcc-exec` |
| `spawn_future` allocator audit | `execution_spawn_future`, `gcc-exec`, `tsan`, `asan` |

常规矩阵尚无稳定的主流 native `std::execution` implementation。当前 handoff 证据因此由两类
synthetic fixture 提供：COMPLETE fixture 必须满足整套 probe；每个独立 PARTIAL root 都必须让
Forge 整体 stand aside。两类 fixture 只验证探测边界，不替代未来真实 native integration lane。

## 隐私和平台说明

这轮不依赖私有硬件、私有 Windows 路径或 vendor SDK。若触碰
Windows-sensitive 脚本或 IOCP 代码，按公开文档运行 Windows matrix，不要提交主机名或本地安装路径。

# Forge 稳定性基线

这份文档记录 `include/forge/` 工作的当前交付基线，以及判断是否继续扩展 runtime/IO
工作的自循环规则。它是维护指南，不承诺每个 optional proof backend 都达到 production
complete。

## 范围

稳定目标是：

- C++ backport library，加可选 `forge::` runtime support；
- ownership、cancellation、drain 和 typed-error 边界清晰；
- IO 和 coroutine-native byte stream facilities 有 portable proof surface；
- examples 能教会用户安全组合。

稳定目标不是：

- 完整 networking framework；
- tensor 或 model-serving framework；
- NVIDIA stdexec/exec 克隆；
- 因为 `forge::` 工作而顺手改变标准 backport behavior。

## 当前交付 surface

Core runtime utilities：

- `forge::static_thread_pool`
- `forge::single_thread_context`
- `forge::system_context`
- `forge::timer_context`
- `forge::runtime_context`
- `forge::async_scope`
- `forge::bounded_channel`
- `forge::resource_context`
- `forge::strand`
- `forge::task`

Resource 与 type-erasure utilities：

- `forge::resource_policy`
- `forge::any_scheduler`
- `forge::erased_sender<CompletionSignatures>`
- 窄 `forge::any_sender_of` / `forge::any_receiver_of`

Platform/proof surfaces：

- `forge::io` Linux epoll/eventfd readiness backend
- `forge::io` Windows IOCP proof backend
- coroutine-native byte IO helpers under `forge::io`
- `forge::io` opt-in typed-error variants

## Verification floor（验证基线）

行为变更默认使用 self-hosted verification floor 作为证据集：

```sh
scripts/verify-selfhosted-floor.sh
```

默认 floor 顺序运行 Linux/container lanes，然后运行 install-package smoke。它刻意保持为
local/self-hosted，不绑定 GitHub hosted CI。Windows 主机可用时，通过
`FORGE_VERIFY_FLOOR_WINDOWS=1` opt into Windows/MSVC smoke；floor script 会调用
Windows SSH/matrix wrapper，后者再调用 Windows-native PowerShell entrypoint。机器相关的
主机名、用户目录和安装路径只放在本地环境变量或 shell history 中，不提交到仓库。

这条 track 最近的 known-good shape：

- LLVM/libc++ all-backport path：全部测试通过；
- TSAN forge/execution subset：全部测试通过；
- ASAN/UBSAN forge/execution subset：全部测试通过；
- install package consumer smoke：通过；
- Windows/MSVC smoke：通过，包含 IOCP gate checks。

不要把这些总数写成硬编码 policy。测试总数预期会增长。优先使用命名 critical tests 和
feature-gate registration checks。

## Gate 预期

Optional features 通过 registration shape 验证行为：

- `FORGE_ENABLE_FORGE_IO=AUTO` 或 `ON`
  - Linux：注册 backend-specific Linux IO tests/examples；
  - Windows：注册 backend-specific IOCP tests/examples；
  - unsupported platform 且为 `AUTO`：跳过 IO backend tests/examples；
  - `ON` 且无 backend：configure 应失败。
- `FORGE_ENABLE_FORGE_IO=OFF`
  - 注册零 IO backend tests/examples。

Sanitizer coverage 中，新 runtime tests 应使用 `forge_` test prefix，除非有明确理由不用。
Sanitizer gate filters 旨在捕捉 execution 和 `forge::` lifetime issues。

## 收敛清单

每轮 taskbook 后使用这张清单。

| 目标 | 证据 | 当前状态 |
| --- | --- | --- |
| Verification 可重复 | `scripts/verify-selfhosted-floor.sh`, `scripts/verify-native.sh`, `scripts/verify-install-package.sh`, `scripts/verify-windows-msvc.ps1`, `scripts/verify-windows-msvc-ssh.sh`, `docs/testing.md` | 已就位 |
| Feature gates 可测试 | Windows gate checks；本地 `ctest -N -R 'forge_io'` gate checks | 已就位 |
| Resource behavior 可审计 | `docs/forge-utilities.md`, `forge_resource_policy`, `example/forge_resource_policy_example.cpp`, `example/forge_bounded_pipeline_example.cpp` | Audit table 已就位；新增 primitive 时逐项 review |
| IO lifecycle 明确 | `docs/forge-io.md`, `forge_io_context`, `forge_io_iocp`, `example/forge_io_read_write_example.cpp` | Per-op cancellation 和 IOCP handle-cache pruning 已就位；IO 语义变更时重新审计 |
| Typed-error boundaries 可用 | `forge_wait_result`, `forge_erased_sender`, `example/forge_io_typed_error_example.cpp` | `wait_result` helper 已就位；新增 typed surface 时 review |
| Examples 教会组合 | `docs/forge-cookbook.md`, `example/forge_graceful_shutdown_example.cpp`, `example/forge_bounded_pipeline_example.cpp`, `^example_` smoke tests | 组合示例已就位；public helper 延后到重复 lifecycle shape 足够稳定时再冻结 |
| Deferred large backends 保持显式 | `docs/roadmap/forge-runtime-vision.md`, `docs/roadmap/forge-backend-proof-policy.md`, backend SPI sketches | 已就位 |

一轮完成的条件是每个变更过的行都有具体证据：test name、example path、doc section 或刻意限制条目。

## Wakeup 与 cancellation 审计规则

Timer cancellation hardening 暴露过一类反复出现的 bug：`atomic` state 加 unlocked
`condition_variable::notify_*` 仍可能丢 wakeup。如果 waiter 在持 mutex 时检查 predicate，
所有为了唤醒该 waiter 而修改 predicate 的路径，都必须在同一把 mutex 下发布修改，再 notify。

触碰 cancellation、shutdown、timer、queue 或 stop-callback path 后，使用
`scripts/audit-runtime-wakeups.sh` 作为 candidate-site inventory。该脚本不证明正确性；
它把 review 聚焦到需要人工 lifecycle check 的位置。

## 自循环协议

每个新的 runtime/IO taskbook 都应遵循：

1. **Inspect** 当前代码和文档。不要从过时 taskbook facts 直接实现。
2. **Implement one slice**，并形成聚焦提交。
3. **Verify** 聚焦测试，再跑 taskbook 要求的 gates。
4. **Review** UAF、races、completion-under-lock、stop-callback lifetime、gate drift 和 docs drift。
5. **Update backlog**：如果发现新的高价值 gap，编辑相关 roadmap/taskbook。
6. **Converge check**：回到上面的清单逐项确认。

如果剩余项高价值且风险低/中，创建下一小轮并重复。若剩余工作需要新 OS backend、
广泛 API commitment 或外部生态 adapter，停止并要求独立 owner decision。

## 已知刻意边界

- Windows/MSVC smoke 是 manual/self-hosted optional gate，不替代 Linux container verification。
- macOS/BSD kqueue 在出现具体 BSD/macOS 需求前不在范围内。
- Linux `io_uring` coroutine-native completion proof 已落地，由 gate
  `FORGE_ENABLE_FORGE_IO_URING` 控制（从属于 `FORGE_ENABLE_FORGE_IO`：父 gate OFF
  时随之关闭，`ON`+父 OFF 是 configure 错误）；受限沙箱 runtime 以 77 skip。超出
  proof 的 hardening（SQPOLL、registered buffers、multishot 等）与 RDMA/fabric
  backend 仍延后。
- Additional IOCP hardening 是 requirement-driven。当前 proof 覆盖 completion drain、
  per-operation cancellation 和 conservative associated handle pruning；更强 handle ownership
  或 high-churn pooling 需要新 taskbook。
- `timer_context` 对 pending timer cancellation 使用 receiver stop callbacks。
- 不应只为了支持 `forge::` extension 而改变 `std::execution` backport behavior。

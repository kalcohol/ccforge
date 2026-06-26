# Backend proof 策略

这份策略适用于 `include/forge/` 下的 optional platform/vendor proof backend。它不适用于
`backport/` 下的标准 backport。

目标是证明 Forge 的 runtime substrate 能表达真实系统，同时不把项目变成 networking、
tensor 或 driver framework。

## Gate 策略

每个 optional backend 都必须有显式 CMake feature gate：

- `AUTO`：只有 platform/backend probe 成功时启用；
- `ON`：要求 backend 可用，不可用时 configure 失败；
- `OFF`：该 backend 的 tests/examples 注册数为零。

Backend 代码不存在时，不要提前添加 SDK probe。未来 `io_uring` 或其它 platform proof
必须在同一份 taskbook 里同时添加 probe、backend 代码和测试。

Package install 不能冻结构建机器上的 probe 结果。Installed configs 应在 consumer project
中重新运行 backend probes。

## 验证策略

每个 backend proof 需要：

- 尽可能使用 `forge_` 前缀的 focused test name；
- public examples 的 smoke tests；
- gate-off registration checks；
- backend 在 sanitizer 环境可用时纳入 sanitizer coverage；
- install-package coverage，验证 header 和 CMake propagation；
- OS/toolchain 不在 Linux container matrix 内时，需要 platform smoke。

不要把某个全局 CTest count 写成 policy。测试数量会增长。应使用具体 regex 的 nonzero 或 zero
registration checks。

Windows/MSVC smoke 是 manual 或 self-hosted gate。公开文档和脚本不得包含私有主机名、
用户名或本地安装路径；这些信息留在环境变量或本地 shell history 里。

## Lifetime 策略

Backend 必须精确说明 ownership：

- OS handle 默认 borrowed，除非类型名明确表示 owning；
- span 默认 borrowed，除非 command packet 明确拥有 storage；
- context resources 是 non-owning，必须比 context 和从中分配的对象活得更久；
- receiver completion 不得在 backend internal lock 下运行；
- cancellation 不得在 in-flight callback 或 completion packet 仍可能触碰 operation storage 时释放该 storage。

未来更强的 storage category，例如 pinned host memory、native device allocation、owning
command packet 或 exported native handle，必须是显式 opt-in type。它们不应静默改变现有
borrowed-span contract。

## Typed-error 策略

默认 backend API 使用 `set_error(std::exception_ptr)`。

Typed-error variant 只暴露稳定的 portable categories：

- invalid handle / buffer / event；
- operation already in progress；
- capacity 或 size mismatch；
- sender contract 内的 cancellation / stopped；
- backend/system error，并在可能时保留原始 code。

不要推测性地把 vendor/platform status code 加进 portable enum。真实 backend 可以在单独
mapping 决策和测试之后，在 backend-specific detail 中保留 raw status。`forge::erased_sender`
可以携带目标 completion signatures 明确声明的 typed error；`forge::wait_result` 是 examples
和 tests 的同步边界 helper。

## 项目身份规则

当新的 backend proof 引入以下内容时，它是 owner decision，不是 routine maintenance：

- vendor SDK 或 driver header；
- 新 OS backend model；
- public API 中的 native handle；
- 新 error taxonomy；
- 超出已有 Forge contexts 的 long-running background runtime。

拿不准时，先写 taskbook，并保持 portable surface mock-first。

## 当前 backend 立场

当前实际 IO 组合是 Linux `epoll/eventfd` readiness 与 Windows IOCP completion。它们是两个
刻意分离的 backend model。不要把 IOCP 强塞进 Linux readiness state machine，也不要在没有
BSD/macOS owner 和验证主机时添加 kqueue。

Linux `io_uring` 延后，直到项目需要 `epoll` readiness 加 one-shot read/write 无法表达的
kernel submission/completion queue 语义。若将来需要，把它作为新的 backend proof，带独立
gate、examples 和 sanitizer story。

其它平台或外部生态 adapter 只有在现有 IO/runtime surface 证明需求后，才作为独立 proof
启动。不要为尚不存在的 backend 先冻结 public ABI。

# `forge::accel` backend SPI 草案

这是未来 accelerator backend proof 的设计草案。它不是 public plugin ABI，也不单独批准
CUDA、HIP、SYCL、FPGA 或 NPU vendor dependency。
通用 gate、lifetime、verification 和 typed-error 规则见
[Backend proof 策略](forge-backend-proof-policy.md)。

当前发布的 backend 是 `include/forge/accel/mock/` 下的 portable mock/in-memory reference
backend，以及 `include/forge/accel/cpu/` 下的 dependency-free CPU/SIMD reference backend。
Backend-neutral vocabulary 位于 `include/forge/accel/`。未来 vendor backend 仍是单独的
owner-gated proof；它们应先保持同样的 user-facing shape，再决定是否暴露 vendor-specific detail。

可执行 contract 是 `forge_accel_backend_conformance`，由仓库本地测试 harness
`test/forge/runtime/forge_accel_backend_conformance.hpp` 支撑。Harness 通过
`TYPED_TEST_SUITE` 同时适配 mock 和 CPU；portable operations 会在两个 backend 上运行，
mock-only fault-injection behavior 保持 plain tests。

## Portable concepts（可移植概念）

稳定 portable vocabulary 刻意保持小：

- owning backend context，目前是 `forge::accel::mock::context` 和 `forge::accel::cpu::context`；
- 从 context 派生的 lightweight device 和 queue handles；
- general、compute、copy、command/message lanes 的 queue kind metadata；
- command/response style device 可选的 `device_session`；
- 带 portable `memory_kind` metadata 的 owning host/device buffers；
- command/model IO proof 使用的 byte-oriented host/device buffers；
- copy commands 使用的 borrowed host spans；
- command/response runtime 使用的 owning command packets；
- cached-like memory proof 的显式 `flush` / `invalidate` coherence command boundaries；
- 最小 event / record / wait / fence completion boundary；
- 基于 byte-size IO descriptors 和 borrowed byte spans 的 model/session execute proof，不包含 tensor 或 graph semantics；
- H2D、D2H、D2D、generic `submit` 和 message submit 的 command senders。

这套 vocabulary 覆盖 GPU、NPU、FPGA 和其它 accelerator runtime 中常见的
stream/queue/event/device-memory 模式，同时不绑定 Forge 到任何具体 vendor SDK。

## Sender contract（sender 契约）

Backend command sender 必须保持现有 Forge runtime contract：

- exactly one terminal completion；
- receiver completion 不在 backend internal mutex 下运行；
- callback 或 completion-packet storage 必须活到 callback return path 之后；
- default API 使用 `set_error(std::exception_ptr)`；
- opt-in typed API 使用 `set_error(forge::accel::error)`；
- queue-capacity 或 closed-context rejection 在可能时完成为 stopped；
- request-stop 是 best-effort；除非 backend 已测试支持，否则不能声称会中断已经运行的 kernel/callable。

Typed errors 应保持为小的 portable classification。Vendor status code 只有在单独 mapping
决策后，才能作为 backend-specific detail 保留。

## Lifetime contract（生命周期契约）

当前 public contract 是 borrowed-by-default：

- device handles 暴露 portable `device_info` 和 availability；backend 必须说明 "lost" 和
  "reset" 是 simulated flag、native device loss、driver reset 还是 context rebuild；
- device-bound queues 和 sessions 运行 queued command 前必须检查 device availability，并把 lost-device rejection 映射到 portable `device_lost` classification；
- host spans 必须活到 command completion；
- `host_buffer<T>` 和 `device_buffer<T>` 必须活到 command completion；
- pending command 捕获某 buffer 时，移动该 buffer object 是 caller error；
- `memory_kind` 默认只是 portable metadata，除非 backend 明确记录更强 native allocation behavior；
- cached-like memory 在 backend 记录需要时，必须通过显式 command-boundary coherence operations；
- `event` 是 shared completion marker，不是 dependency graph node；
- `submit_packet` 拥有 request/response storage 直到 terminal completion；
- `submit_message` 是显式 borrowed response path；
- `model_bindings` 存储 borrowed byte spans；若 backend 支持更强 native tensor 或 buffer ownership，必须用显式 opt-in type 暴露；
- queued-command timeout 可以拒绝 deadline 前未开始的 work，但不能声称会打断已经运行的 command/kernel。

未来 backend 可以新增 pinned host buffers、native event handles 或更强 backend-specific packet
ownership，但必须是显式 opt-in types，不应静默改变当前 mock surface 的 borrowed contract。

## Event 与 fence 边界

除非真实 backend proof 需要更多，event 必须保持最小：

- `record_event(queue, event)` 在该 queue 上早先 accepted work 到达 command 后记录 readiness；
- `wait_event(queue, event)` 等待 marker ready 或 context stop；
- `fence(queue)` 是已 accepted work 的 no-op command boundary；
- context 可以暴露多条 queue。每条 queue 内保证 FIFO；cross-queue ordering 只通过显式 event record/wait 表达。

不要在 portable layer 把这做成通用 dependency graph。Cross-queue dependency management、
native event export、timeline semaphores 和 graph submission 都是单独的 backend-specific
proposal。

## Backend proof 清单

添加真实 backend 前，必须具备：

- 显式 gate 和 CMake detection policy；
- gate-off build 注册零 backend tests/examples；
- portable mock headers 不 include vendor headers；
- reusable `forge_accel_backend_conformance` test suite 在 backend adapter 上通过；
- backend-specific 行为有聚焦测试；
- 文档说明哪些 resource 是 owned、borrowed、pinned 或 vendor-owned；
- examples 优先使用 portable surface；native handles 只出现在清楚标记的 backend-specific example 中。

第一个真实 backend proof 应作为新的 project identity decision 审查，而不是 routine maintenance。

## Conformance 覆盖

Portable conformance suite 覆盖以下 backend obligations：

- basic queue、copy、submit 和 fence 行为；
- cross-queue event ordering 和 same-queue wait-before-record limitation；
- mock worker proof 中的 stream query、per-stream synchronize、sticky stream error 和 event elapsed time；
- capacity-full rejection through stopped completion；
- size mismatch 和 cached-memory coherence classification；
- device loss、device reset、stale sessions、drain freeze 和 worker fault；
- request timeout 和 late-response accounting；
- bypass request-pending map 的 protocol lifecycle signals；
- mock worker proof 中的 stream-ordered host callback invoke/complete 和 unregister drain；
- 不改变 command behavior 的 optional trace collection；
- typed accelerator errors 跨 `forge::erased_sender` 和 `forge::wait_result`。

Suite 刻意不证明 vendor allocation classes、native event export、graph submission、tensor
semantics、driver reset、firmware behavior 或 kernel interruption。CPU backend 覆盖 aligned
CPU storage、真实 H2D/D2H copy paths 和真实 CPU/SIMD submit work；vendor backend 若暴露
native-only behavior，仍必须把它作为 backend-specific addition 单独记录和测试。

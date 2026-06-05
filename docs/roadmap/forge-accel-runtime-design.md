# `forge::accel` runtime 设计

这份文档描述 `forge::accel` 当前完成态的 host/device runtime support design。它不是
历史 roadmap 的拼接版，也不是发布计划；它按 runtime 机制解释这层设施现在如何组织，
方便 review、PPT 和后续 backend proof 共享同一套上下文。

本文只使用通用、vendor-neutral 的 runtime 术语。它不记录任何私有项目名、芯片名、私有
API、私有路径、结构体字段、日志或源码片段。

## 定位

`forge::accel` 的目标层级是 user-space runtime substrate：

```text
DL / tensor / framework backend glue       examples and design contracts only
forge::accel runtime substrate            target layer
optional vendor runtime / driver SDK      future owner-approved backend input
kernel driver / firmware / hardware       out of scope
```

它不实现 driver，不包装 vendor SDK，不暴露 native handle，也不是 tensor framework、
model server 或 graph optimizer。它提供的是一组可以被 mock、CPU reference 和未来
backend 共同理解的 vocabulary、lifecycle contract 和 executable proof。

## Backend-neutral 分层

当前结构分三层：

- `forge::accel`：backend-neutral vocabulary 和 error/status contract；
- `forge::accel::mock`：dependency-free reference backend，也是 fault-injection 和
  runtime-substrate executable specification；
- `forge::accel::cpu`：dependency-free CPU/SIMD reference backend，用真实 CPU work 验证
  queue/copy/submit/event vocabulary 不只适用于 mock state machine。

未来真实 backend 必须先说明自身如何映射到这些 portable contracts，再决定是否暴露
backend-specific extension。真实 backend 不会自动把 vendor dependency 变成 Forge 默认依赖。

## Host request 路径

Host API 发起 work 时，runtime 需要区分两个边界：

- enqueue/admission：请求是否被 runtime 接受；
- execution completion：请求对应的 worker work 是否执行完成并返回结果。

因此 transport proof 支持两种 call mode：

- `posted`：host 只等待 admission/enqueue result，completion 后续异步到达；
- `non_posted`：host 将匹配 response 视为该 operation 的 completion boundary。

两种模式共用同一条 request correlation 机制。Request 通过 `request_id` 关联 response；
duplicate request 会被拒绝；late response 会被 discard 并计数。Lifecycle/control signal
不依赖普通 pending request map，因此 host lost、worker fault、drain 等控制事件不会被
业务请求的关联表卡住。

这一层表达的是“host 如何把 work 送入 runtime 并得到可追踪结果”，不是 fd、HANDLE、
ioctl、DMA doorbell 或 native queue handle。

## Control plane 与 lifecycle

Control plane 负责 runtime 生命期，不负责具体业务命令。当前 vocabulary 覆盖：

- context / device / session identity；
- `device_epoch` 和 `worker_generation`；
- heartbeat；
- device lost；
- host lost cleanup；
- drain freeze / complete drain；
- reset / resume；
- stale session；
- worker fault。

Heartbeat 是 worker liveness 的主路径信号。Mock proof 提供 heartbeat tick 和 timeout
检查；timeout 会将 worker fault latch 到 device/session admission path 上。

Host lost 与 device lost 分开建模。Host lost 表示 host 侧会话或连接丢失，device 侧需要先
冻结新 admission，清退旧 worker/session state，完成后推进 epoch/generation。旧 session
随后以 stale-session 失败，新 session 才重新接纳业务。

Power/resume 的 portable default 是保守的：sleep 前 quiesce / drain / fence；resume 后
重新 probe，并用 epoch、session、generation 验证旧 handle 是否仍有效。除非真实 backend
proof 证明 command/session 能跨低功耗存活，否则旧 session 应视为可能 stale。

## Worker 与 stream 模型

Mock backend 把 queue 建模成 worker stream。每条 stream 是 FIFO lane，所有 user callback
和 receiver completion 都遵守 Forge runtime 的通用规则：

- exactly-one terminal completion；
- 不在 internal lock 下调用 receiver 或 user callback；
- wakeup predicate 在 waiter 使用的同一把 mutex 下发布；
- shutdown/wait 显式 drain 已接受 work；
- 从本 worker 内部调用 wait 时避免 self-deadlock。

Stream `0` 是 default stream。没有显式 stream 参数的同步/普通 command 可以落到 stream 0；
显式 copy/compute queue 使用非零 stream id。这个模型表达“默认队列也有顺序语义”，但不复制
任何特定 vendor 的 legacy default-stream 规则。

Stream FIFO node 包括：

- command node；
- module/command dispatch node；
- event-record node；
- event-wait node；
- stream-ordered callback node；
- sync/fence node。

`query_stream(q)` 是 non-blocking snapshot，报告 stream id、queue kind、idle 状态、
pending node 数和 sticky error。`synchronize_stream(q, options)` 是 host blocking wait，
等待单条 stream idle 或 timeout。它不是 whole-context wait，也不是 tensor framework 的
全设备同步。

Sticky stream error 的 proof 语义是：记录第一条 non-success stream error，后续 node 仍可
继续执行；`query_stream` 只观察；`synchronize_stream` 返回并可清除该 sticky error。这把
错误聚合放在 sync point，而不是把 stream 变成自动 skip 后续 node 的 graph scheduler。

## Module/command dispatch（模块命令分发）

Portable `module_id + command_id` 表达“worker 如何把请求路由到某个 runtime module 或
command handler”。`command_dispatcher<Request, Response>` 是 dependency-free proof：
注册 handler 后，packet 携带 module/command key，worker 执行到 dispatch node 时查表并
运行 handler。

这个机制不是 kernel launch ABI。它不定义 grid/block/shared memory，也不定义 model 文件格式
或 vendor module loader。它只说明 runtime substrate 需要一条可测试的 command routing
路径。真正的 module load/unload lifecycle 可以在真实 backend 需求出现后作为独立 proof
补充。

## Event、fence 与 ordering

Event 是 stream 之间的 ordering marker。`record_event(q, ev)` 在某条 stream 上发布
generation；`wait_event(q, ev)` 在另一条 stream 上等待该 generation；`fence(q)` 是同一
stream 上的 no-op boundary。

Mock 与 CPU backend 都验证了常见的单次 record/wait ordering；它们不承诺在多次 record
同一个 event 的边界场景下逐位等价。Mock 在 command start 时 reserve record generation，
CPU reference backend 在 command 执行时 reserve generation，并把 wait timeout 视为“等待 node
开始执行之后”的预算。

`elapsed_time(event)` 使用 mock steady clock 提供 dependency-free profiling proof。Trace
event 同时记录 activity start/end timestamp，用于说明 enqueue、execute、complete 的 duration
如何进入诊断流。

Event 不是 native handle、timeline semaphore、dependency graph 或 cycle detector。
Same-stream wait-before-record 是一个 cycle，mock 会把它作为可停止/可超时的边界处理，而不是
尝试推断用户意图。

## Stream-ordered host callback（按 stream 排序的 host 回调）

Device-to-host callback 被建模为 stream FIFO node，而不是任意时刻从 device 旁路打断 host。

流程是：

1. host 注册 callback，获得 `callback_id`；
2. host 把 callback node 插入某条 stream；
3. worker FIFO 执行到 callback node；
4. callback dispatcher 在当前 stream queue 的 strand 上运行 callback body；
5. callback completion 记录 invoke/complete ACK；
6. stream 上后续 node 才继续推进。

`unregister_callback` 会等待 in-flight callback drain。User callback 拷贝到局部后在锁外执行；
异常会映射到 typed error 或 exception path。这个模型与 stream ordering 直接衔接，也避免把
callback 做成难以验证的随机中断通道。

## Framework glue contracts（框架接入契约）

`forge::accel` 只记录 framework-style backend glue 的通用需求，不包含 framework header：

- `current_device_guard` 和 `current_stream_guard`；
- allocator / memory-pool contract；
- tensor lifetime / record-stream expectation；
- event wrapper；
- per-stream synchronize；
- async copy and op dispatch；
- model/session binding；
- typed error mapping；
- peer access enable/query；
- graph capture state-machine boundary；
- stream priority metadata；
- user trace range marker。

这些项目不是全部已实现功能。当前实现将 device/stream guard、per-stream synchronize、event、
typed error 和 examples 作为 dependency-free proof；peer access、graph capture、stream
priority、memory pool 和 trace range marker 保持 design contract，等待真实 backend 需求。

## 已实现 proof 与刻意延后项

已完成的 portable proof：

- mock context/device/queue/session；
- CPU reference context/device buffer/copy/submit/event；
- posted / non-posted transport；
- request correlation、late response、typed error；
- heartbeat、host lost、drain freeze、worker fault、epoch/stale session；
- default stream 0、stream query、per-stream synchronize；
- module/command dispatch；
- event elapsed time、trace activity duration；
- stream-ordered host callback；
- resume revalidation example；
- framework glue example。

刻意不覆盖或延后的项目：

- driver/ioctl/native handle；
- CUDA/HIP/SYCL/NPU SDK dependency；
- tensor graph、graph optimizer、operator fusion、serving policy；
- stream-ordered async allocation/free；
- vendor graph capture execution；
- kernel launch configuration；
- IPC memory/event handle；
- pinned memory registration；
- real peer routing；
- real module load/unload lifecycle。

这些不是当前完成态缺陷；它们需要真实 backend proof 或 owner 决策。

## 阅读路径

学习或准备分享时，优先读：

1. [`forge::accel` user documentation](../forge-accel.md)；
2. 本文档；
3. [`forge::accel` backend SPI 草案](forge-accel-backend-spi.md)；
4. [`backend proof` 策略](forge-backend-proof-policy.md)。

历史实现过程可以从 Git history 追溯；仓库文档只保留当前完成态和未来 proof 需要的上下文。

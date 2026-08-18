# `forge::io` backend SPI 草案

这是未来 IO backend proof 的设计草案。它不是 public plugin ABI，也不单独批准
`io_uring` 或 networking abstraction。

当前发布的 IO surface 有两个刻意不同的 backend model：

- Linux `epoll` / `eventfd` readiness；
- Windows IOCP completion。

未来 backend 应保持这种诚实表达。如果 OS completion model 不同，不要强行把所有平台塞进
同一个伪 abstraction。

## Portable user-facing shape（用户可见形态）

稳定 vocabulary 很小：

- owning `forge::io::context`；
- borrowed OS handles；
- one-shot read/write helpers 使用 borrowed byte spans；
- Linux readiness senders：`readable(fd)` 和 `writable(fd)`；
- platform read/write helpers：`async_read_some(...)` 和 `async_write_some(...)`；
- `close()`、`request_stop()`、`shutdown()`、`wait()`，以及 per-handle `cancel(...)`；
- 默认 exception errors 和 opt-in typed-error variants。

Readiness backend 和 completion backend 不需要拥有完全相同的 API。例如，IOCP 不暴露
`readable()` / `writable()`，因为 completion packet 已经代表提交过的 operation。

## Backend contract（后端契约）

每个 backend proof 必须定义：

- `close()` / `shutdown()` 后 operation 如何被接受或拒绝；
- context stop 如何完成 pending operation；
- receiver stop token 是否取消已经 accepted 的 operation；
- cancellation 如何 drain，才能安全释放 record ownership；
- 哪个线程调用 receiver completion；
- completion 是否可能在 backend lock 下运行；
- OS handle 和 buffer 的 borrowed lifetime；
- `_typed` variants 暴露哪些稳定 typed errors。

默认规则是 exactly one terminal completion，且 receiver completion 不在 backend internal lock 下运行。

## Linux readiness 策略

现有 Linux backend 是 level-triggered readiness。它报告“fd 看起来 ready”；除非用户选择
`async_read_some` / `async_write_some`，否则 backend 不拥有后续 syscall。

后果：

- `readable(fd)` / `writable(fd)` 只用 `set_value()` 完成；
- EOF、socket errors 和 short IO 由用户 syscall 观察；
- 如果 readiness 到 syscall 之间被其它 consumer 抽干 fd，`EAGAIN` / `EWOULDBLOCK` 是正常 syscall error；
- 每个 fd 支持一个 pending read waiter 和一个 pending write waiter。

不要把 `io_uring` 当成“更好的 epoll”添加。只有项目需要 kernel submission/completion queue
语义，并能测试不同的 cancellation/drain behavior 时，才考虑它。

当前决策：延后 `io_uring`。Linux epoll/eventfd backend 已覆盖当前 readiness 和 one-shot
read/write 用例；项目还没有需要 kernel submission/completion queues 的场景。只有以下条件同时满足时才重新评估：

- epoll readiness 加 `async_read_some` / `async_write_some` 不够；
- workload 需要 SQ/CQ 语义，而不是 readiness notification；
- 所需 syscalls 和 cancellation paths 能在常规验证环境中测试；
- backend 能保持 optional AUTO/ON/OFF gates，且除非明确批准，不引入 mandatory `liburing` dependency；
- typed-error categories 仍保持小而 portable。

2026-08 重估记录：owner 确认支撑层需要服务非以太网介质的 byte-stream fabric 场景
（RoCEv2/RDMA、DMA、PCIe/UALink/UCIe 类加速器互连），该类 workload 属于
submission/completion queue 语义，重估条件成立。`io_uring` 是该模型在通用 Linux
内核上可验证的 commodity 载体，是未来 coroutine-native backend proof 的首选候选。
启动前仍需独立 taskbook，且 taskbook 必须先解决：

- native protocol 选择：sender-native + coroutine facade，或 coroutine-native +
  sender bridge（初步倾向后者，作为 per-backend native protocol 原则的首个
  completion-queue 实例）；
- `liburing` vs raw syscalls（维持默认不引入 mandatory `liburing`）；
- kernel 版本地板与 op 子集；
- 验证通道：容器运行时默认 seccomp profile 普遍禁用 `io_uring_setup` /
  `io_uring_enter`，需要自定义 seccomp profile 或 host-side lane；
- cancellation/drain 行为（`IORING_OP_ASYNC_CANCEL`、ring teardown）与
  typed-error 分类的聚焦测试。

## Windows IOCP 策略

现有 Windows backend 是 completion-based。Operation 显式提交，并通过 IOCP packet 完成。

后果：

- 不暴露 readiness senders；
- `CancelIoEx` 是异步的，completion packet 仍必须 drain；
- handle 必须支持 overlapped IO，并在 operation completion 或 context drain 前保持有效；
- 当前 proof 用 per-handle in-flight counts 记录 associated handles，并且只在 OS 报告 borrowed handle value 无效后保守 prune idle records。

Associated-handle cache 通过保守 pruning 变成有界，但它仍不是 ownership model。若 production
backend 需要拥有 handle 或支持 high-churn handle pool，应新增显式 handle-lifetime abstraction，
而不是把 cache policy 当成 portable contract。

## 未来 backend 进入清单

添加新 IO backend 前，必须具备：

- 显式 gate 和 configure probe；
- gate-off build 注册零 backend tests/examples；
- accept、cancel、request-stop、close、shutdown 和 borrowed lifetime boundaries 的聚焦测试；
- backend 可用时的 sanitizer coverage；
- install-package behavior 会在 consumer project 重新运行 probes；
- readiness vs completion semantics 文档；
- typed-error mapping 只审查稳定 portable categories。

只有出现具体平台需求且语义匹配时，新的 readiness backend 才能沿用 Linux shape。
macOS/BSD kqueue 不是当前项目目标。Completion backend 应沿用 IOCP shape，而不是伪装成
readiness。

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

## io_uring Phase 0 决策记录（2026-08）

### D1：native protocol

结论：go，选择 coroutine-native + explicit sender bridge。

- Public backend type 使用独立的 `forge::io::io_uring_context`，计划由 direct header
  `forge/io/io_uring_context.hpp` 暴露。它与现有 `forge::io::context` 的 epoll
  readiness backend 并存，不参与 `<forge/io.hpp>` 的 portable context 选择，也不替换
  `readable()` / `writable()`。
- 原生 one-shot operation 是非 coroutine 的 direct `io_awaitable` object。
  `await_suspend(handle, io_env const*)` 登记 pending record、填充 SQE 并 submit；
  poller thread drain CQE 后在 backend lock 外直接 resume handle。后续 coroutine body
  因而运行在 poller thread；需要业务 executor affinity 时显式 await
  `env.executor.schedule()`。
- `io_uring_context` 持有 SQ/CQ mappings、submission lock、pending registry 和 poller
  thread。Awaitable、fd 和 buffer 都是 borrowed operation state；context 与 awaiting
  coroutine frame 必须活到 CQE 或 cancellation CQE 被 drain。
- V1 不提供平行的 native sender implementation。Sender consumer 通过一个显式
  `io_task<io_result<...>>` coroutine await 原生 operation，再使用既有
  `as_sender(io_task, env)`。这条桥可能分配 coroutine frame，属于 interop cost，不进入
  coroutine-native hot-path 的零分配声明。
- P4123 指出的 sender bridge 成本不足以在本仓冻结第二套 backend operation state。
  只有后续 benchmark 证明该显式桥是实际瓶颈，才另立任务评估薄 sender；本 proof 不提前
  增加 native sender surface。

这个选择落实 “per-backend 单一原生 async 协议 + 显式桥”：epoll/IOCP 既有 backend
继续 sender-native，io_uring proof 则作为第一个 coroutine-native completion-queue
backend。

### D2：raw syscalls vs liburing

结论：go，V1 使用 raw Linux UAPI，不引入 liburing。

- 可重复探针入口是 `scripts/probe-io-uring.sh`，其 C++ source 直接调用
  `io_uring_setup`、mmap SQ/CQ/SQEs、提交 `IORING_OP_NOP`，再用
  `io_uring_enter` 等待并验证 CQE。探针只依赖 `<linux/io_uring.h>`、libc syscall/mmap
  surface 和 C++23 compiler。
- 2026-08-19 本机证据：8-entry setup 成功，kernel 返回 8 SQ entries、16 CQ entries、
  features `0x3ffff`，NOP CQE result 为 `0`。这证明当前 host lane 可运行 raw ring，
  不只是在 compile-time 看见 UAPI header。
- `io_uring_setup` / `io_uring_enter` 因 `ENOSYS`、`EPERM` 或 `EACCES` 不可用时，探针以
  `77` 退出，供 AUTO gate 和 CTest 表达 graceful skip；其它 setup/mmap/CQE 错误保持
  hard failure。
- 当前 proof 的 op 子集很小，raw UAPI 所需代码局限在 ring mapping、SQ publication、
  CQ drain 和四个 opcode preparation。此范围不足以证明 mandatory liburing dependency
  的维护与 packaging 成本合理。
- 若后续加入 registered buffers、provided-buffer rings、multishot 或复杂 linked SQE，
  必须重新评估 raw maintenance cost；那是新决策，不在 V1 中自动开启 optional/mandatory
  liburing 路径。

### D3：kernel floor 与 operation subset

结论：语义地板是 Linux 5.6，并以 runtime capability probe 为最终判据。

- Linux 5.6 同时提供 V1 所需的 non-vectored `IORING_OP_READ` /
  `IORING_OP_WRITE` 和 `IORING_REGISTER_PROBE`；`IORING_OP_ASYNC_CANCEL` 自 5.5
  已存在。Build-time UAPI header 必须声明 setup/enter/register syscalls、四个 opcodes
  与 probe structs。
- Configure/runtime probe 不解析 `uname` 来替代 capability detection。Ring setup 后通过
  `IORING_REGISTER_PROBE` 要求 `NOP`、`READ`、`WRITE`、`ASYNC_CANCEL` 都带
  `IO_URING_OP_SUPPORTED`；因此带 backport 的旧版本 kernel 只要实际 capability 满足也可
  通过，缺 opcode 的新版本或受限环境则不可用。
- Setup feature 要求 `IORING_FEAT_NODROP`，避免 CQ overflow 静默丢失 terminal
  completion；`IORING_FEAT_SINGLE_MMAP` 只影响 mapping 优化，不是必需条件。
  `FAST_POLL`、`SUBMIT_STABLE`、`EXT_ARG`、SQPOLL 和 registered resources 都不作为
  V1 前提。
- Data operation 仅有 `IORING_OP_READ` / `IORING_OP_WRITE`，`off` 固定为
  `-1`，表达 stream/current-position one-shot IO。Public API 不接收 file offset，
  不做 seekable-file random access。
- `IORING_OP_ASYNC_CANCEL` 只按目标 request 的 unique `user_data` 取消；
  cancel CQE 与目标 CQE 都必须 drain。`IORING_OP_NOP` 用于 wakeup/teardown
  submission，不承载用户 operation。
- 不做 `READV`/`WRITEV`、accept/connect、send/recv、timeout、linked SQE、multishot、
  provided/registered buffers、registered files 或 SQPOLL。这些能力不能在 V1 中通过
  detail API 绕过 public lifetime contract。

`scripts/probe-io-uring.sh` 已扩展为同时检查 `IORING_FEAT_NODROP` 和上述四个
opcode；当前 host 返回 33 个 probe entries，必需集合全部可用。

### D4：verification lane 与 sandbox policy

V1 分开验证 build capability、kernel capability 和 container sandbox policy：

| lane | 预期结果 | 说明 |
|---|---|---|
| Host `scripts/probe-io-uring.sh` | pass 或 77 skip | 当前 host pass；直接验证 setup、mmap、register、enter 与 CQ drain |
| 默认 rootless Podman policy | 77 skip | Fedora/Podman 默认 seccomp 对 setup 返回 `ENOSYS`；这不是 kernel 缺失 |
| 自定义 seccomp、默认 SELinux container label | 77 skip | 当前 Fedora policy 对 setup 返回 `EACCES` |
| `scripts/probe-io-uring-container.sh` | pass | 从 Podman 默认 profile 派生并只放行三个 io_uring syscalls；当前返回 58 个 probe entries |

Container lane 需要关闭 SELinux container label 才能在当前 Fedora host 上执行
`io_uring_setup`。这是 backend verification 专用的安全例外，不是生产容器建议。脚本同时
使用无网络、drop all capabilities、`no-new-privileges`、只读 source mount 和 ephemeral
container 来缩小范围；它不会使用 `--privileged` 或 seccomp unconfined。Custom profile
由 `/usr/share/containers/seccomp.json` 派生，可通过
`FORGE_IO_URING_SECCOMP_BASE` 指定其他 Podman profile。

可重复的 container lane 为：

```sh
podman build -t forge-tsan -f containers/Containerfile.tsan .
scripts/probe-io-uring-container.sh
```

未来 `FORGE_ENABLE_FORGE_IO_URING` gate 使用以下规则：

- `OFF`：不编译 backend，也不运行 capability/runtime tests；
- `AUTO`：Linux UAPI compile probe 通过即编译 backend；runtime setup/probe 不可用时，
  runtime tests 以 77 明确 skip。Configure 不用 `uname`，也不在 cross compile 时执行
  target binary；
- `ON`：UAPI compile probe 失败时 configure hard fail；该 lane 的 runtime
  setup/probe 不可用属于 test failure，不能以 77 隐藏。

Phase 1/2 可在默认 sandbox 中完成 compile-only 与 mock queue 测试。Phase 3 的真实
pipe/socketpair、stop race 和 shutdown CQ drain 测试必须运行在 host lane 或上述 custom
policy lane；TSAN/ASAN 也必须沿用同一 policy，默认容器的 77 只证明 skip path。

### D5：cancellation、drain 与 typed error

结论：go。每个 accepted data request 只有对应的 READ/WRITE target CQE 能决定用户
terminal；ASYNC_CANCEL CQE 是必须 drain 的 administrative completion，不能直接恢复
coroutine。Pending record 使用 unique `user_data`，并以 atomic terminal claim 防御重复
completion；target record 和 cancel bookkeeping 分别在对应 CQE 从 CQ 摘下后才可释放。

Direct awaitable 的 terminal 映射如下：

| 条件 | target SQE | 用户观察 |
|---|---|---|
| `await_suspend` 前 env stop 已请求 | 不提交 | `await_resume()` 抛 `sender_stopped` |
| context 已 `close()` / `request_stop()` | 不提交 | `sender_stopped` |
| target CQE `res > 0` | 已 drain | value，byte count 照实交付 |
| non-empty READ target CQE `res == 0` | 已 drain | EOF |
| empty buffer | 不提交 | value `0`；READ 不合成 EOF |
| target CQE `res < 0` 且不是 request-linked `-ECANCELED` | 已 drain | `io_result` error，code 为 `-res` |
| stop 后 target CQE 为 `-ECANCELED` | 已 drain | `sender_stopped` |
| stop 后 target CQE 先返回 value/EOF/其它 error | 已 drain | target 实际结果；late cancel 不覆盖 |
| cancel CQE 为 `0` | 继续等待 target CQE | 无用户 terminal |
| cancel CQE 为 `-ENOENT` / `-EALREADY` | 继续等待 target CQE | 无用户 terminal |

因此 stop request 是 best-effort cancellation request，不是“请求瞬间即完成”。Cancel
submission 若遇 SQ backpressure，先保留在 context-owned retry queue，由 poller/wakeup
继续提交；不能提前释放 borrowed fd/buffer 或伪造 stopped。Unexpected cancel CQE error
记录为 backend failure，但仍不能代替 target CQE retire request。正常 success/error CQE
若先于 cancellation 获胜，必须照实交付。

Lifecycle 与现有 epoll/IOCP context 使用同一词汇：

- `close()` 幂等关闭 ingress；新 operation 观察 stopped，已 accepted operation 继续等待
  自然 CQE，close 本身不取消。
- `request_stop()` 幂等关闭后续 ingress，并为所有尚未 terminal 的 accepted target 最多
  排队一个 `IORING_OP_ASYNC_CANCEL`。Pre-submit operation 不进入 ring，直接 stopped。
- `shutdown()` 等价于 `close()` + `request_stop()`。
- Poller 只有在 ingress 已关闭、所有 target CQE、cancel CQE、NOP CQE 和 userspace
  submission bookkeeping 都 drain 后才退出。`wait()` 从外部线程 join；从 poller-thread
  completion 内调用时避免 self-join，由 poller 持有 state keepalive 完成 terminal tail。
- 析构执行 `shutdown()` + `wait()`，因而可以阻塞。调用方 submission threads 必须先
  quiesce；context drain 不会 join 外部 submitter。

`io_result<std::size_t>` 继续承载 value/EOF/`std::error_code` compound result；stopped
继续通过 `sender_stopped` 跨 coroutine 边界，并由 `as_sender(io_task)` 恢复为 sender
stopped channel。Raw negative CQE 使用 `std::generic_category()` 的正 errno 值。
Typed adapter 继续调用既有 `typed_detail::classify`，closed `error_kind` 集合保持
`unknown`、`system`、`invalid_handle`、`operation_in_progress`、`would_block`，不新增
io_uring-only kind。无 stop request 的 `-ECANCELED` 是普通
`std::errc::operation_canceled` error；EOF 也不伪装成 typed error。

fd、buffer、operation awaitable 和 context 是 borrowed dependencies，必须活到 operation
terminal/`await_resume()`；context-owned cancel record 可以在用户 target terminal 后继续
由 poller drain。若 completion 内销毁 context，非拥有的 PMR resource 仍须活过 detached
poller/state 的最终释放尾部。

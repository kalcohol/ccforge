# `forge::io` 使用说明

`forge::io` 是 Forge runtime extension 的 OS IO backend。它不是标准 backport，
不向 `namespace std` 增加名字。

Linux backend 是 `epoll/eventfd` readiness backend；Windows backend 是小型 IOCP
completion proof。两者都只覆盖最窄的 sender/receiver 接入，不是完整网络库。
未来 backend entry rules 记录在
[`forge::io` backend SPI sketch](roadmap/forge-io-backend-spi.md) 和
[backend proof policy](roadmap/forge-backend-proof-policy.md) 中。

Coroutine-native byte IO vocabulary 和 stream boundary 的长期跟踪记录在
[`coroutine-native byte IO 路线图`](roadmap/coroutine-native-io.md)。该路线不会把
`forge::io::context` 扩展成完整 networking library，也不会改变 Linux readiness 与
Windows IOCP 的 backend 语义。

## 这不是什么

- 不是 `std::io`、`std::networking`、`<io>` 或 `<networking>` backport。
- 不是 TCP/DNS/UDP/TLS、socket option、endpoint/address-resolution library。
- 不是 `std::execution` 的替代模型；coroutine bridge 只在 `forge::io`
  下与现有 sender runtime 互通。
- 不是稳定 ABI 的 stream erasure layer；当前 `any_read_stream` / `any_write_stream` 是
  borrowed wrapper。
- 不是 platform semantics normalizer；Linux readiness 和 Windows IOCP completion
  继续按各自 backend 语义暴露。

## 纯 byte vocabulary

Coroutine-native byte IO track 的 backend-free 设施当前通过 direct headers 使用：

```cpp
#include <forge/io/result.hpp>
#include <forge/io/buffer.hpp>
#include <forge/io/memory_stream.hpp>
#include <forge/io/stream.hpp>
#include <forge/io/coro.hpp> // coroutine substrate proof
#include <forge/io/context_await.hpp> // coroutine facade over context backend
```

`forge::io::io_result<Ts...>` 是以 `std::error_code` 为首元素的 compound result，
支持 structured binding：

```cpp
forge::io::io_result<std::size_t> result{
    std::make_error_code(std::errc::connection_reset),
    12};

auto [ec, n] = result;
```

`ec` 永远存在；`!ec` 表示成功，后续 payload 有成功语义。`ec` 为错误时，payload
仍保留，用于表达 partial IO progress。普通 IO 失败不需要通过异常传播。

`forge::io::const_buffer` 和 `forge::io::mutable_buffer` 是 borrowed byte-region
descriptors。它们不拥有内存，只记录 pointer 和 byte count。`buffer_size`、
`buffer_empty`、`buffer_prefix` 和 `buffer_copy` 覆盖单 buffer 与 scatter/gather
buffer sequence 的最小用法。纯 vocabulary 不受 `FORGE_ENABLE_FORGE_IO` backend gate
控制；backend gate 仍只控制 OS IO context、backend examples 和 backend tests。

`forge::io::memory_read_stream`、`forge::io::memory_write_stream`、
`forge::io::memory_stream` 和 `forge::io::scripted_read_stream` 是同一条路线的第二批
backend-free 测试设施。它们提供与真实 byte stream 相同的 `read_some` /
`write_some` 结果形状，但不打开 socket、pipe、file，也不依赖 `forge::io::context`。

- `memory_read_stream` 从 borrowed input 读取，支持配置单次最大读量，用于稳定复现
  short read 和 EOF；调用方必须保证输入 memory 活过 stream 使用期。
- `memory_write_stream` 写入 owned storage，或写入 borrowed output buffer；只有容量
  限制会造成 short write。`bytes()` 返回的是 view；owned storage 后续写入可能 reallocate，
  因而会使之前取得的 span 失效。
- `memory_stream` 把一个 read side 和一个 write side 合在一起，适合 request/response
  风格的小型协议测试；`written_bytes()` 与 `memory_write_stream::bytes()` 有相同的 span
  invalidation 规则。
- `scripted_read_stream` 按脚本返回 bytes、EOF、错误，或 “bytes 后错误”。错误路径仍返回
  `0` byte progress；需要 partial progress 时使用 “bytes 后错误”，这样返回的 byte count
  一定对应已经写入 caller buffer 的 bytes。

这些类型的直接价值是让 length-prefixed、line-oriented、frame parser 等协议层可以在
没有 OS backend 的构建里测试 partial IO、EOF 和错误传播。它们不是网络库，不提供 TCP、
DNS、TLS、地址解析、listener、连接管理或 buffer policy framework。

`forge::io::stream.hpp` 在这些测试流之上定义最小同步 byte stream 边界：

- `read_stream<T>`：`T&` 支持用 prvalue `mutable_buffer` 调用 `read_some(...)`，返回
  `io_result<std::size_t>`。
- `write_stream<T>`：`T&` 支持用 prvalue `const_buffer` 调用 `write_some(...)`，返回
  `io_result<std::size_t>`。
- `read_write_stream<T>`：同时满足 read/write 两侧。
- `read_exactly(stream, mutable_buffer)` 和 `write_all(stream, const_buffer)` 是小型验证
  算法；遇到 stream error 时返回累计 byte count，遇到提前 EOF/容量耗尽导致的
  `0` byte progress 时返回 `std::errc::io_error` 和累计 byte count。
- `read_until(stream, std::string&, delimiter, max_bytes)` 是小型 line/record helper；
  它逐 byte 读取直到 delimiter，输出包含 delimiter，routine error 仍通过
  `io_result` 返回并保留已读取文本。超过 `max_bytes` 时返回
  `std::errc::message_size` 和累计 byte count。
- `any_read_stream` 和 `any_write_stream` 是 non-owning borrowed wrappers。构造时只保存
  目标 stream 的地址和一个函数指针；copy/move 只复制这条引用，调用方必须保证 concrete
  stream 比 erased wrapper 活得更久。空 wrapper 调用会返回
  `std::errc::bad_address`，不抛异常。

这层的设计目标是 separate-compilation friendly 的协议边界：协议函数可以接收
`any_read_stream&` / `any_write_stream&`，测试时传入 `memory_read_stream` 或
`scripted_read_stream`。当前实现仍是 header-only proof，没有承诺稳定 ABI、固定对象布局、
owning erased storage 或 per-operation allocation 策略。

P4124 风格的 domain-aware `when_all` 当前只保留为设计方向，没有实现公共 helper。原因是
Stage 7 还不能证明 sibling operation 的取消、partial result 保留和 exactly-once completion
可以在当前 substrate 上同时满足。

`forge::io::coro.hpp` 是 `forge::io` 下的 coroutine-native substrate
proof。它吸收的是提案路线里的 env propagation 价值，不替换现有 sender runtime，也不改变
`forge::task`：

- `executor_ref` 是对现有 Forge scheduler 的窄适配，内部使用 `forge::any_scheduler`，
  当前只承诺能产生 schedule sender。
- `io_env` 携带 `executor_ref`、`std::inplace_stop_token` 和可选
  `std::pmr::memory_resource*`。它是轻量 borrowed environment：`executor_ref`、
  stop token 和 memory resource pointer 都不拥有底层 runtime。若使用
  `std::inplace_stop_source` 提供 token，source 必须活过使用该 `io_env` 的
  `as_sender(io_task<T>, env)` operation 或父 `io_task` await 链。
- `io_awaitable<T>` 检查 awaitable 是否提供
  `await_suspend(std::coroutine_handle<>, io_env const*)` 形态。
- `io_task<T>` 是最小 coroutine proof，用于把 `io_env` 传给 env-aware awaitable；它不是
  sender，也不是 `forge::task` 的替代品。它没有 public fire-and-forget `start()`；
  支持的 ownership 形态只有两种：在父 `io_task` 内 `co_await` 子 task，或用
  `as_sender(io_task<T>, env)` 交给 sender operation-state 持有到 terminal completion。
- `this_io_env()` 是 immediate awaitable，用于在 coroutine 内读取当前 `io_env`。

当前 Stage 4 实现只证明 stop token、resource pointer 和 scheduler handle 可以沿
coroutine-native 边界传递。它没有实现 owning frame allocator、symmetric transfer、
timer awaitable、platform IO awaitable，或把 sender cancellation 规则重新包装成另一套稳定
async model。frame allocator policy 保持 deferred，直到能用测试证明 coroutine frame allocation
确实经由指定 resource。

Sender interop 分两层：

- 对现有 `forge::task`，优先使用 `<execution>` backport 已有的
  `std::execution::with_awaitable_senders` / `as_awaitable`；本路线不修改 backport 的
  coroutine awaitability 规则。
- 对 `forge::io::io_task<T>`，使用显式
  `forge::io::await_sender(sender)` 在 coroutine 内 await sender。它会把
  sender value 作为 `std::tuple<...>`（或多 value alternative 的 `std::variant`）返回，
  error 以异常重新抛出，stopped 以 `sender_stopped` 抛出，并把 `io_env` 的 stop token
  暴露给 receiver env。
- `forge::io::as_sender(io_task<T>, io_env)` 把简单 `io_task<T>` 暴露成
  sender，completion shape 是 `set_value(T)` / `set_error(std::exception_ptr)` /
  `set_stopped()`；`io_task<void>` 使用 `set_value()`。该 bridge 是 single-use：
  connect 会 move 走 task，并由 operation-state 持有 task 与 `io_env` 副本直到完成。
  当前 proof bridge 的 cancellation channel 是传入 `io_env.stop_token`；receiver 侧
  stop token 不会自动与它融合。
  和 `forge::task` 一样，receiver 不应在 final-suspend completion callback 内同步销毁
  已连接的 operation-state。
- `io_result<Ts...>` 不会被 `as_sender` 隐式拆成 sender value/error channels。若 coroutine
  返回 `io_result<std::size_t>`，它作为单个 value 传出，保留 error code 与 partial byte count。
  需要把 compound I/O result 转成 sender channels 时，应写显式 adapter，避免静默丢失
  partial progress。

`await_sender(sender)` 当前在源 sender 的 completion 线程上 resume coroutine，不会自动
hop 到 `io_env.executor`。如果源 sender inline 完成，awaiter 会在 `await_suspend`
返回后继续 coroutine，避免从 completion callback 递归 resume。经 `context_await.hpp`
await backend operation 时，后续 coroutine body 仍可能运行在 Linux poller thread 或
Windows IOCP completion thread 上。需要切回业务 executor 时，应显式
`co_await await_sender(env.executor.schedule())`。

`await_sender` 的 source sender 还必须满足常见 sender-awaitable 的 lifetime 约束：它的
operation-state 不能要求 receiver completion callback 返回后才允许销毁。`await_sender`
会在 completion 中 resume coroutine；若 coroutine 随即完成，awaiter 会随 frame 一起析构。
这与 `forge::task` 对自定义 receiver 的 final-suspend 约束同类。需要 await 不满足该约束的
sender 时，应先经由一个 owning/queued adapter 把 completion hop 到安全 owner 上。

`forge::io::context_await.hpp` 是 `forge::io` 下对现有
`forge::io::context` 的 coroutine facade。它不是新的 backend contract，也不是 socket API；
在 backend 关闭时可以直接 include，但不会暴露需要 `forge::io::context` 的函数。

- `async_read_some(context, handle, std::span<std::byte>)` 和
  `async_write_some(context, handle, std::span<const std::byte>)` 返回
  `io_task<io_result<std::size_t>>`。成功时 byte count 来自现有 backend sender。
- backend 的 `set_error(std::exception_ptr)` 会映射成 `io_result` 的 `std::error_code`；
  `std::system_error` 保留原始 code，其它异常退化为 `std::errc::io_error`。当前 backend
  error path 没有 partial byte count，因此 failure payload 是 `0`。
- backend 的 `set_stopped()` 不会被压成 error code；`as_sender(io_task<T>)` 仍把它交付为
  stopped channel。
- Linux `readable(context, fd)` / `writable(context, fd)` 返回 `io_task<io_result<>>`，
  只表示 readiness 完成，没有 byte count。真实 `read(2)` / `write(2)` 仍由用户代码执行。
- handle、span buffer 和 `context` 都是 borrowed；调用方必须保证它们活到 operation
  完成。`context` 析构仍可能因为 `shutdown()` / `wait()` 而阻塞。

## 使用选择

- 协议 parser/unit test：优先用 `memory_read_stream`、`memory_write_stream` 和
  `scripted_read_stream`，覆盖 short read、EOF、错误和 partial progress。
- separate-compilation 或插件边界：接收 `any_read_stream&` / `any_write_stream&`，
  让调用方决定 concrete stream。
- coroutine 与 sender runtime 互通：在 `io_task` 内使用 `await_sender(sender)`，
  或用 `as_sender(io_task<T>)` 把 coroutine operation 暴露给 sender consumer。
- 已有 OS backend handoff：只在需要现有 `forge::io::context` 时使用
  `<forge/io/context_await.hpp>`；fd/HANDLE、buffer 和 context lifetime 仍由调用方负责。

## 平台与 gate

当前 backend：

- Linux：`epoll` + `eventfd` readiness。
- Windows：IOCP completion proof。

- `FORGE_ENABLE_FORGE_IO=AUTO`：Linux 或 Windows backend 可用时启用；其它平台跳过。
- `FORGE_ENABLE_FORGE_IO=ON`：缺少 Linux `epoll/eventfd` 或 Windows IOCP 支持时
  configure 报错。
- `FORGE_ENABLE_FORGE_IO=OFF`：跳过 IO examples/tests。

`<forge/io.hpp>` 在没有 backend 时可以 include，但不会暴露 `forge::io::context`。
直接包含 backend 头 `<forge/io/context.hpp>` 需要 `FORGE_HAS_FORGE_IO_BACKEND`。

macOS/BSD kqueue 当前不在项目需求内，不作为本轮目标。Linux `io_uring` 只有在需要
kernel submission/completion queue 语义时才会单独立项；它不是 `epoll` readiness
backend 的替代写法。Zig 可以帮助构建或 C ABI 互操作，但不能抹平这些 backend 的语义差异。

## API 概览

```cpp
#include <forge/io.hpp>

forge::io::context io;

auto readable = io.readable(fd);
auto writable = io.writable(fd);

std::array<std::byte, 4096> input;
auto read = io.async_read_some(fd, std::span{input});
```

Linux `readable(fd)` / `writable(fd)` 返回 sender，完成形状为：

```cpp
std::execution::set_value_t()
std::execution::set_error_t(std::exception_ptr)
std::execution::set_stopped_t()
```

`set_value()` 只表示 fd 已经 ready；真正的 `read(2)` / `write(2)` 仍由用户代码执行。
这避免 readiness API 过早承诺 buffer lifetime、partial IO、EOF 和 retry 策略。

Linux `async_read_some(fd, std::span<std::byte>)` 和
`async_write_some(fd, std::span<const std::byte>)` 会先等待相应 readiness，再执行一次
`read(2)` / `write(2)`，完成形状为：

```cpp
std::execution::set_value_t(std::size_t)
std::execution::set_error_t(std::exception_ptr)
std::execution::set_stopped_t()
```

返回值是该次 syscall 的 byte count。这些 convenience sender 要求 fd 处于
nonblocking 模式；如果传入 blocking fd，poller thread 可能在后续 syscall 中被阻塞。
空 span 是特殊情况：Linux backend 不等待 readiness，直接完成 `set_value(0)`。
`0` 对 read 表示 EOF 或零长度 buffer；write 可能因非阻塞 fd 状态只完成部分 bytes。
span 是 borrowed，调用方必须保证 buffer 活到 operation 完成。`EINTR` 会重试；
其它 syscall error 通过 `std::exception_ptr` 传出。
因为 Linux backend 使用 level-triggered readiness，ready 到实际 syscall 之间如果有其它
consumer 抽干 fd，`EAGAIN` / `EWOULDBLOCK` 会作为普通 syscall error 通过
`set_error(std::exception_ptr)` 传播。

Windows `async_read_some(HANDLE, std::span<std::byte>)` 和
`async_write_some(HANDLE, std::span<const std::byte>)` 直接发起 overlapped IO，并通过
IOCP completion 完成。返回值同样是该次 operation 的 byte count。Read completion
遇到 byte-stream EOF（例如 named pipe peer close）时返回 `0`。V1 要求传入的
`HANDLE` 支持 overlapped IO，且未绑定到其它 completion port；它面向 named pipe /
socket-like byte stream，不提供 random-access file offset 参数。对文件 HANDLE 的
显式 offset IO 需要后续公共 API 扩展。

## Typed-error variants（类型化错误变体）

默认 IO API 使用 `set_error(std::exception_ptr)`，保持与其它 Forge runtime sender
一致。需要在插件边界或 erased sender 边界保留错误分类时，可以使用 opt-in `_typed`
变体：

```cpp
auto ready = io.readable_typed(fd);              // Linux readiness only
auto read = io.async_read_some_typed(fd, span);  // Linux fd 或 Windows HANDLE
```

typed 变体的 completion signatures 使用：

```cpp
std::execution::set_error_t(forge::io::error)
```

`forge::io::error` 是小型 closed-set error 值：

- `error_kind::invalid_handle`
- `error_kind::operation_in_progress`
- `error_kind::would_block`
- `error_kind::system`
- `error_kind::unknown`

`error::code` 保留底层 `std::error_code`。V1 typed API 只覆盖最稳定的分类；默认
exception_ptr API 仍是主路径。Typed wrappers currently map `std::system_error`
to `forge::io::error`; non-`system_error` exceptions become
`error_kind::unknown` with an empty `std::error_code`. Use the default
`std::exception_ptr` API when preserving arbitrary exception diagnostics matters.

Typed sender 可以直接跨 `forge::erased_sender` 边界，并用 `forge::wait_result`
同步消费，不需要为常见边界手写 receiver：

```cpp
using ready = forge::erased_sender<
    std::execution::completion_signatures<
        std::execution::set_value_t(),
        std::execution::set_error_t(forge::io::error),
        std::execution::set_stopped_t()>>;

ready op{io.readable_typed(fd)};
auto result = forge::wait_result(std::move(op));
if (auto* error = result.error_if<forge::io::error>()) {
    // inspect error->kind and error->code
}
```

## FD / HANDLE lifetime（句柄生命周期）

fd / `HANDLE` 都是 borrowed。`forge::io::context` 不拥有 OS handle。

调用方必须保证 OS handle 在 pending operation 完成、`cancel(...)` 后 drain，或
context `shutdown()` / `wait()` 之后再关闭。否则 OS 可能复用同一个 handle value，
让 pending operation 观察到错误对象。

推荐模式：

1. 创建 fd 的 RAII wrapper；
2. 启动 readiness sender；
3. operation 完成后执行实际 syscall；
4. shutdown/cancel/drain 后再销毁 fd wrapper。

## Lifecycle（生命周期）

`forge::io::context` 是 owning runtime primitive，析构会 `shutdown()` + `wait()`，
因此可能阻塞。

- `close()`：关闭 ingress，拒绝新 readiness operation；已 pending operation 仍可正常
  因 readiness 完成。若 close 后没有 pending operation，poller 会退出。
- `request_stop()`：请求取消 pending operation。若取消先赢，operation 以 `set_stopped()`
  完成；若它和 OS 成功 completion 竞态并输掉，operation 仍会交付 `set_value(...)`。
- `shutdown()`：`close()` + `request_stop()`。
- `wait()`：等待 poller thread 退出。若从 poller completion 内调用，会避免 self-join。
- `cancel(fd)` / `cancel(HANDLE)`：取消该 handle 的 pending operation。

Linux readiness operation 会观察 receiver stop token：operation `start()` 前如果 token 已请求，
会直接 `set_stopped()`；如果 operation 已经进入 pending fd table 且 token 后续请求停止，
context 会把该 pending waiter 从 fd table 移除、更新 epoll interest、唤醒 poller，并在
mutex 外完成 `set_stopped()`。不带 stoppable token 的 pending waiter 仍由 readiness、
`cancel(fd)`、context `request_stop()` 或 `shutdown()` 完成。

Windows IOCP operation 也会观察 receiver stop token：`start()` 前如果 token 已请求，
会直接 `set_stopped()`；operation 接受后若 token 请求停止，context 会调用
`CancelIoEx(handle, &overlapped)` 请求取消。IOCP 取消仍是 completion-based：最终
receiver completion 和 pending record 释放发生在 completion packet 被 poller drain 之后。
不带 stoppable token 的 pending operation 仍由 `cancel(HANDLE)`、context
`request_stop()` 或 `shutdown()` 完成。

## Readiness 规则

V1 对每个 fd 最多支持一个 pending read waiter 和一个 pending write waiter。重复提交同一
fd/readiness kind 会让新的 operation 以 `set_error(std::exception_ptr)` 完成，内部错误为
`std::errc::operation_in_progress`。

read/write readiness 使用 level-triggered epoll。`EPOLLERR` / `EPOLLHUP` 也会唤醒
readiness sender，让用户后续 syscall 读取真实错误或 EOF。

Completion 不会在 context mutex 下运行。callback 默认在 IO poller thread 上执行；重 CPU
工作应显式切到 `static_thread_pool`、`runtime_context` 或 `strand`：

```cpp
auto work = std::execution::continues_on(
    io.readable(fd),
    runtime.get_scheduler())
  | std::execution::then([fd] {
        // do read(2) here
    });
```

## Windows IOCP 规则

Windows backend 是 completion model，不提供 `readable()` / `writable()` readiness
sender。它的 V1 目标是证明 Forge 的 IO 抽象能承载 completion-based backend，而不是把
Windows 强行压成 Linux readiness。

要求：

- handle 必须以 overlapped 模式创建；
- handle 必须保持有效直到 operation completion 或 context drain；
- 一个 handle 不应同时绑定到其它 IOCP；
- `async_read_some` / `async_write_some` 是 one-shot operation。
- V1 不接收外部 `OVERLAPPED*` 包，也不允许同一 HANDLE 上混用用户自发的 overlapped
  IO；worker 会丢弃不属于本 context pending record 的 completion packet。
- V1 不支持 random-access file offset；如果需要文件 offset，请先扩展公共 API，而不是
  依赖隐式 file pointer。

V1 每次启动 operation 都会尝试把 handle 关联到 context IOCP；如果同一个 live handle
已经关联过，Windows 可能拒绝重复关联，此时 backend 使用内部 associated-handle cache
确认这是已知 handle 并继续。context 会记录每个关联 handle 的 active operation
计数，并在后续启动 operation 时清理已经 idle 且系统确认 invalid 的旧 borrowed handle
记录。这样 OS 关闭并复用相同 HANDLE 数值时，新 handle 仍会先被重新尝试关联。它仍不是
生产级 handle cache：更强的 pruning 需要显式 handle lifetime 模型，当前不把 context
变成 handle owner。

## Resource policy（资源策略）

`forge::io::context_options{.memory = resource}` 控制 context state、pending fd map、
event buffer、action batch 和 receiver record 等 context-owned allocation。resource
非拥有，必须比 context 活得更久。

这不控制用户 fd、用户 buffer，也不承诺标准库内部对象零分配。

## 示例

- `example/forge_line_protocol_example.cpp`：`read_until` + memory streams 上的
  tiny line request/response。
- `example/forge_coro_line_pipeline_example.cpp`：memory stream read -> coroutine
  parse -> strand state update -> response write 的 runtime composition smoke。
- `example/forge_io_readiness_example.cpp`：nonblocking pipe + `readable(fd)`。
- `example/forge_io_pipeline_example.cpp`：IO readiness -> strand continuation ->
  channel message。
- `example/forge_io_read_write_example.cpp`：borrowed span + async read/write convenience。
- `example/forge_context_await_example.cpp`：coroutine facade over context sender；
  Linux 下演示 await readiness 后由用户代码执行 nonblocking `read(2)`。
- `example/forge_io_iocp_example.cpp`：Windows named pipe + IOCP async read/write。

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

- `example/forge_io_readiness_example.cpp`：nonblocking pipe + `readable(fd)`。
- `example/forge_io_pipeline_example.cpp`：IO readiness -> strand continuation ->
  channel message。
- `example/forge_io_read_write_example.cpp`：borrowed span + async read/write convenience。
- `example/forge_io_accel_pipeline_example.cpp`：Linux pipe read/write handoff
  到 CPU reference accel queue。
- `example/forge_io_iocp_example.cpp`：Windows named pipe + IOCP async read/write。

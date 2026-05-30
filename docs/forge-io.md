# `forge::io`

`forge::io` 是 Forge runtime extension 的 Linux fd readiness backend。它不是
标准 backport，不向 `namespace std` 增加名字。

V1 目标很窄：把真实 OS readiness 事件接入 sender/receiver，而不是提供完整网络库或
异步 read/write buffer 抽象。

## Platform And Gates

当前 backend 只支持 Linux `epoll` + `eventfd`。

- `FORGE_ENABLE_FORGE_IO=AUTO`：Linux 支持可用时启用；其它平台跳过。
- `FORGE_ENABLE_FORGE_IO=ON`：缺少 Linux `epoll/eventfd` 时 configure 报错。
- `FORGE_ENABLE_FORGE_IO=OFF`：跳过 IO examples/tests。

`<forge/io.hpp>` 在没有 backend 时可以 include，但不会暴露 `forge::io::context`。
直接包含 backend 头 `<forge/io/context.hpp>` 需要 `FORGE_HAS_FORGE_IO_BACKEND`。

Windows IOCP、macOS/BSD kqueue 和 Linux `io_uring` 都是后续工作。Zig 可以帮助构建
或 C ABI 互操作，但不能抹平这些 backend 的语义差异。

## API

```cpp
#include <forge/io.hpp>

forge::io::context io;

auto readable = io.readable(fd);
auto writable = io.writable(fd);
```

`readable(fd)` / `writable(fd)` 返回 sender，完成形状为：

```cpp
std::execution::set_value_t()
std::execution::set_error_t(std::exception_ptr)
std::execution::set_stopped_t()
```

`set_value()` 只表示 fd 已经 ready；真正的 `read(2)` / `write(2)` 仍由用户代码执行。
这避免 V1 过早承诺 buffer lifetime、partial IO、EOF 和 retry 策略。

## FD Lifetime

fd 是 borrowed。`forge::io::context` 不拥有 fd。

调用方必须保证 fd 在 pending readiness operation 完成、`cancel(fd)` 后 drain，或
context `shutdown()` / `wait()` 之后再关闭。否则 OS 可能复用同一个 fd number，让
pending operation 观察到错误对象。

推荐模式：

1. 创建 fd 的 RAII wrapper；
2. 启动 readiness sender；
3. operation 完成后执行实际 syscall；
4. shutdown/cancel/drain 后再销毁 fd wrapper。

## Lifecycle

`forge::io::context` 是 owning runtime primitive，析构会 `shutdown()` + `wait()`，
因此可能阻塞。

- `close()`：关闭 ingress，拒绝新 readiness operation；已 pending operation 仍可正常
  因 readiness 完成。若 close 后没有 pending operation，poller 会退出。
- `request_stop()`：取消 pending operation，并以 `set_stopped()` 完成。
- `shutdown()`：`close()` + `request_stop()`。
- `wait()`：等待 poller thread 退出。若从 poller completion 内调用，会避免 self-join。
- `cancel(fd)`：取消该 fd 的 pending readable/writable waiter。

V1 只支持 context-level cancellation 和 receiver start-time stop-token 观察。operation
入队后，如果 receiver stop token 才请求停止，idle waiter 不会靠 per-op stop callback
单独醒来；需要 readiness、`cancel(fd)`、`request_stop()` 或 `shutdown()`。

## Readiness Rules

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

## Resource Policy

`forge::io::context_options{.memory = resource}` 控制 context state、pending fd map、
event buffer、action batch 和 receiver record 等 context-owned allocation。resource
非拥有，必须比 context 活得更久。

这不控制用户 fd、用户 buffer，也不承诺标准库内部对象零分配。

## Examples

- `example/forge_io_readiness_example.cpp`：nonblocking pipe + `readable(fd)`。
- `example/forge_io_pipeline_example.cpp`：IO readiness -> strand continuation ->
  channel message。

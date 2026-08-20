# `forge::io` 使用说明

`forge::io` 是 Forge runtime extension 的 byte IO vocabulary、coroutine bridge 与可选
OS IO backend。它不是标准 backport，不向 `namespace std` 增加名字。

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
- 不是稳定 ABI 的 stream erasure layer；borrowed 与 owning wrappers 都是 header-only
  Forge extension，不承诺跨版本二进制布局。
- 不是 platform semantics normalizer；Linux readiness 和 Windows IOCP completion
  继续按各自 backend 语义暴露。

## 纯 byte vocabulary

Coroutine-native byte IO track 的 backend-free 设施当前通过 direct headers 使用：

```cpp
#include <forge/io/result.hpp>
#include <forge/io/buffer.hpp>
#include <forge/io/memory_stream.hpp>
#include <forge/io/stream.hpp>
#include <forge/io/async_stream.hpp> // direct-awaitable concepts and owning erasure
#include <forge/io/coro.hpp> // coroutine substrate proof
#include <forge/io/timer_await.hpp> // backend-free timer coroutine facade
#include <forge/io/context_await.hpp> // coroutine facade over context backend
#include <forge/io/combinators.hpp> // narrow P4124-style proof helper
```

`<forge/io.hpp>` 不是“全部 IO 词汇”的 umbrella header；它是 OS backend entry
header。backend 可用时它暴露 `forge::io::context`，backend 关闭或平台不支持时它仍可
include 但不暴露 context。纯 vocabulary、memory stream、stream concepts、
coroutine substrate 和 combinator proof helper 都应直接包含对应 `<forge/io/*.hpp>`。
这让不需要 OS backend 的代码可以在 `FORGE_ENABLE_FORGE_IO=OFF` 或无 epoll/IOCP 平台上
继续使用 byte vocabulary。

`forge::io::io_result<Ts...>` 是以 `std::error_code` 为首元素的 compound result，
同时带一个轻量状态位：`value`、`eof` 或 `error`。它保留原有 structured binding
形状：

```cpp
forge::io::io_result<std::size_t> result{
    std::make_error_code(std::errc::connection_reset),
    12};

auto [ec, n] = result;
```

`ec` 永远存在；`result.has_value()` / `static_cast<bool>(result)` 表示成功，
`result.eof()` 表示 EOF，`ec` 非空表示 routine error。EOF 不使用 error channel，
但 payload 仍保留，用于表达 partial IO progress。普通 IO 失败不需要通过异常传播。
保留 `std::error_code` 作为 tuple element 0 是为了让既有 structured-binding 代码继续编译；
需要区分 EOF 的代码必须检查 `result` 或 `result.eof()`，不能只检查 `ec`。
Tuple access 通过 `forge::io` 中的 ADL `get` 提供；支持 structured binding，未承诺
`std::get<0>(result)` 这一非必要拼写。

`forge::io::const_buffer` 和 `forge::io::mutable_buffer` 是 borrowed byte-region
descriptors。它们不拥有内存，只记录 pointer 和 byte count。`buffer_size`、
`buffer_empty`、`buffer_prefix` 和 `buffer_copy` 覆盖单 buffer 与 scatter/gather
buffer sequence 的最小用法；单个 source/destination region 即使重叠也按
`memmove` 语义复制。纯 vocabulary 不受 `FORGE_ENABLE_FORGE_IO` backend gate
控制；backend gate 仍只控制 OS IO context、backend examples 和 backend tests。

`forge::io::memory_read_stream`、`forge::io::memory_write_stream`、
`forge::io::memory_stream` 和 `forge::io::scripted_read_stream` 是同一条路线的第二批
backend-free 测试设施。它们提供与真实 byte stream 相同的 `read_some` /
`write_some` 结果形状，但不打开 socket、pipe、file，也不依赖 `forge::io::context`。

- `memory_read_stream` 从 borrowed input 读取，支持配置单次最大读量，用于稳定复现
  short read 和 EOF；调用方必须保证输入 memory 活过 stream 使用期。为避免常见悬垂误用，
  直接从临时 `std::string` 构造会被拒绝。`max_read_size == 0` 是显式的 no-limit 值，
  等价于默认的 `dynamic_extent_limit`。
- `memory_write_stream` 写入 owned storage，或写入 borrowed output buffer；只有容量
  限制会造成 short write。`bytes()` 返回的是 view；owned storage 后续写入可能 reallocate，
  因而会使之前取得的 span 失效。
- `memory_stream` 把一个 read side 和一个 write side 合在一起，适合 request/response
  风格的小型协议测试；`written_bytes()` 与 `memory_write_stream::bytes()` 有相同的 span
  invalidation 规则。
- `scripted_read_stream` 按脚本返回 bytes、EOF、错误，或 “bytes 后错误”。EOF 使用
  `result.eof()` 状态，通常由 `io_result<T...>::end_of_file(...)` 构造；error code
  为空，byte count 保留。错误路径仍返回
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
  算法；遇到 stream error 时返回累计 byte count。`read_exactly` 遇到提前 EOF 时返回
  EOF 状态和累计 byte count，包括一次 read 同时报告 progress 与 EOF 的情况；
  如果同一次 read 的 progress 恰好填满目标 buffer，则完整读取优先并返回 value；
  `write_all` 遇到容量耗尽导致的 `0` byte progress 时仍返回
  `std::errc::io_error` 和累计 byte count，因为 write side 没有 EOF 语义。
- `read_until(stream, std::string&, delimiter, max_bytes)` 是小型 line/record helper；
  它逐 byte 读取直到 delimiter，输出包含 delimiter，routine error 仍通过
  `io_result` 返回并保留已读取文本。提前 EOF 返回 EOF 状态并保留 partial line；
  若同一次 read 同时读到 delimiter 和 EOF，则 delimiter 完成优先并返回成功；
  超过 `max_bytes` 时返回
  `std::errc::message_size` 和累计 byte count。注意 `max_bytes` 缺省值为
  `SIZE_MAX`（不设上限）：面对不可信 peer 时必须传入显式上限，否则一条永不出现
  delimiter 的流会让 output 无界增长直至内存耗尽。
- `any_read_stream` 和 `any_write_stream` 是 non-owning borrowed wrappers。构造时只保存
  目标 stream 的地址和一个函数指针；copy/move 只复制这条引用，调用方必须保证 concrete
  stream 比 erased wrapper 活得更久。空 wrapper 调用会返回
  `std::errc::bad_address`，不抛异常。
- `owning_any_read_stream` 和 `owning_any_write_stream` 是 move-only owning wrappers。
  构造时用可注入的 `std::pmr::memory_resource*` 为 concrete stream object 做一次分配；
  `read_some` / `write_some` 仍是直接 vtable 调用，不做 operation allocation。Resource
  是 borrowed，必须比 wrapper 活得更久；concrete stream 自己借用的 buffer 或其它资源仍由
  调用方保证生命周期。移动后的 source 与 reset 后的 wrapper 都为空，调用返回
  `std::errc::bad_address`。

这层的设计目标是 separate-compilation friendly 的协议边界：协议函数可以接收
`any_read_stream&` / `any_write_stream&`，测试时传入 `memory_read_stream` 或
`scripted_read_stream`；需要跨调用边界转移 concrete object 所有权时可改收对应
`owning_any_*` 类型。当前实现仍是 header-only proof，没有承诺跨版本稳定 ABI 或固定
wrapper 对象布局。

`<forge/io/async_stream.hpp>` 在同一 byte-stream vocabulary 上增加 direct-awaitable
protocol 和 owning async erasure：

- `async_read_stream<T>` 要求 `T&.read_some(mutable_buffer{})` 返回 prvalue object；
  该对象满足 `io_awaitable`，且 `await_resume()` 精确返回
  `io_result<std::size_t>`。`async_write_stream<T>` 对
  `write_some(const_buffer{})` 有同样规则；`async_read_write_stream<T>` 同时满足两侧。
  返回 `io_task` 的 stream 不属于这个 direct-awaitable concept。
- `immediate_async_stream<Stream>` 拥有一个同步 `read_stream` / `write_stream`，调用时立即
  执行同步 operation，再返回 ready awaitable。它是 backend-free 的测试与迁移适配器，
  不会把同步工作异步调度。
- `owning_any_async_read_stream` / `owning_any_async_write_stream` 用可注入 PMR resource
  持有 concrete stream object，并把每次 concrete awaitable 构造到 wrapper 内的
  128-byte、`alignof(std::max_align_t)` operation slot。超出 size/alignment 的 awaitable
  在 owning wrapper 构造约束处被 compile-time 拒绝，不会隐式退化成 heap allocation。
- Erasure layer 自己只在 wrapper 构造/析构时分配/释放 concrete stream object；每次
  `read_some` / `write_some` 不向该 resource 分配。这个声明不覆盖 concrete
  stream/awaitable、`io_task` coroutine frame、sender bridge 或标准库内部 allocation。
- 每个 read wrapper 或 write wrapper 只允许一个 in-flight operation。重叠调用返回 ready
  awaitable，其 result error 是 `std::errc::operation_in_progress`；空 wrapper 返回
  `std::errc::bad_address`。未开始 await 就丢弃 operation handle 会释放 slot。
- `erased_io_awaitable`、buffer、concrete stream 的 borrowed dependencies 和 PMR resource
  都必须活到 `await_resume()` 完成。Active operation 存在时 move/reset 抛
  `std::logic_error`；销毁 active wrapper 会 `std::terminate()`。通用 erasure 无法替
  concrete backend 发明 cancel/drain，因此等待 coroutine 也不得在 suspension 中途销毁。

固定 vtable 和 inline operation slot 让同一构建中的 separate-TU 协议边界不需要知道
concrete stream 类型，但这不是 plugin ABI 承诺。
`example/forge_owned_async_stream_example.cpp` 与
`forge_owned_async_stream_protocol.cc` 分属不同 translation units：main TU 构造
`immediate_async_stream<memory_read_stream>`，protocol TU 只通过
`owning_any_async_read_stream&` 执行 coroutine read loop。

P4124 风格的 domain-aware combinator 当前提供 two-child、IO-result-specific proof：
`when_all_results(first, second, env)`、`when_any_results(first, second, env)` 和基于后者的
`with_timeout(task, duration, timer_context, env)`。它们只接受
`io_task<io_result<...>>`，并返回 sender；它们不是 `std::execution::when_all` /
`when_any` 的替代品，也不组合任意 sender。

`when_all_results` 的 value payload 是
`when_all_result<First, Second>`，其中 `first` / `second` 是各自 child
`io_result` 的 `std::optional` slot。这样 routine error 或 EOF 触发 sibling stop 时，
已经完成的 child result 仍可保留。aggregate priority 是：

1. 任一 child `io_result` 为 error：aggregate `io_result` 为 error，使用 index 最小的
   child error code，并保留 child slots；
2. 否则任一 child 为 EOF：aggregate `io_result` 为 EOF，并保留 child slots；
3. 否则任一 child stopped 或被 sibling stop 后没有产出 `io_result`：aggregate sender
   交付 `set_stopped()`；
4. 否则两个 child 都是 value：aggregate `io_result` 为 value。

child error / EOF / stopped 都会请求 shared stop source，pending sibling 会通过
`as_sender` 的 fused stop-token 观察到 stop。这个 helper 的 proof surface 是
two-child、IO-result-specific；更通用的 domain-aware combinator、variadic shape、
policy-based priority 或 owning result storage 都保持 deferred。
`when_all_results` 允许 consumer receiver 在 terminal completion callback 内同步销毁
已连接的 operation-state；value 路径和 sibling-stop 重入路径都有 sanitizer
regression test。这个保证不改变 `as_sender` 的 source-sender lifetime 约束：
source operation-state 仍必须允许在其 receiver completion callback 内销毁。
`example/forge_coro_combinator_example.cpp` 展示了 value+EOF 和 value+error
组合时如何检查 aggregate status，并读取保留下来的 child partial result。

`when_any_results` 的 value payload 是
`when_any_result<First, Second>`：除两个 optional result slot 外，还包含 `winner`
index。Winner 是 shared-state mutex 下第一个被接受的 terminal callback；不是跨线程不可观测的
wall-clock 时刻。两个 callback 并发时，`0` 或 `1` 都是合法 winner。实现先启动 first child，
再启动 second child，因此 first inline-complete 时会在 second 启动前获胜。

选出 winner 后会请求 shared stop，但 aggregate 仍等两个 child 都到达 terminal callback 才
交付。Loser 若响应 stop，其 slot 为空；若忽略 stop 并经 value channel 交付 late
`io_result`，该 result（包括 EOF/error 和 partial progress）保留在 slot 中，但不会覆盖
winner 状态。Downstream stop request 本身不是 terminal；如果 child 忽略 stop 并先交付
value，它仍可成为 winner。行为表如下：

| # | 首个 terminal | loser | aggregate / payload |
| --- | --- | --- | --- |
| 1 | value | stopped | value；仅 winner slot |
| 2 | value | late value | value；两个 slots |
| 3 | error `io_result` | stopped | winner error code；仅 winner slot |
| 4 | error `io_result` | late value/error/EOF result | winner error code；两个 slots，loser 状态保留 |
| 5 | EOF `io_result` | stopped | EOF；仅 winner slot |
| 6 | EOF `io_result` | late value/error/EOF result | EOF；两个 slots，loser 状态保留 |
| 7 | stopped | stopped | `set_stopped()`；无 payload |
| 8 | stopped | late value | `set_stopped()`；late child 只用于 drain |
| 9 | downstream stop，两个 child 响应 | first observed stopped | `set_stopped()` |
| 10 | downstream stop，某 child 忽略并先 value-complete | stopped 或 late result | value；可用 slots |
| 11 | 两个 terminal 并发 | 任一完成顺序 | winner `0`/`1` 均允许；winner 状态控制 aggregate |
| 12 | winner sender `set_error` / start throw，或 aggregate move 失败 | drain | `set_error(exception_ptr)` |

Loser 的 sender `set_error` 不覆盖已选 winner；它没有 `io_result` slot。Completion 在 helper
mutex 外交付且 exactly-once。`when_any_results` 不新增 source lifetime 特权：其 child 经
`as_sender(io_task)` 连接，所以 consumer receiver 不得在 final-suspend completion callback
内同步销毁 connected operation-state；应在 terminal callback 返回后再销毁。

`with_timeout` 把用户 task 放在 index `0`，把 `async_sleep_for` 放在 index `1`。Timer
value 先完成时，aggregate 映射为 `std::errc::timed_out`，仍保留用户 task 在 drain 期间产生的
late partial slot；用户 task 先完成时，它的原 value/error/EOF 状态保持不变并请求停止 timer。
对仍 pending 的 task，零或负 duration 会走 timeout；若 task 在 first-child start 中 inline
完成，则 task 先获胜。External stop 继续交付 stopped，而不是伪装成 timeout。
`example/forge_coro_timeout_example.cpp` 用 scripted memory read 展示 task-first 与
timer-first 两条路径。

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
- `io_task` 的 `co_await` 保留 awaitable 的值类别：lvalue awaitable 是 borrowed，必须活过
  对应 suspension；rvalue awaitable 会被 coroutine frame 持有。
- `io_task<T>` 是最小 coroutine proof，用于把 `io_env` 传给 env-aware awaitable；它不是
  sender，也不是 `forge::task` 的替代品。它没有 public fire-and-forget `start()`；
  支持的 ownership 形态只有两种：在父 `io_task` 内 `co_await` 子 task，或用
  `as_sender(io_task<T>, env)` 交给 sender operation-state 持有到 terminal completion。
- `this_io_env()` 是 immediate awaitable，用于在 coroutine 内读取当前 `io_env`。

当前 coroutine-native IO track 证明 stop token、resource pointer 和 scheduler handle 可以沿
coroutine-native 边界传递，提供既有 timer/backend sender 的 coroutine facade，以及
io_uring completion backend 的原生 platform IO awaitable。父子 `io_task` await 使用
symmetric transfer；本 track 不把 sender cancellation 规则重新包装成另一套稳定 async
model。

弃置（销毁悬挂中的 `io_task` 链）按迭代方式逐帧执行：每帧在 await 子 task 时记录
向下链接，销毁时沿链循环拆帧而不是经嵌套 awaitable 析构递归，深链弃置的 native
stack 占用是常数。该保证只覆盖 task 链本身；悬挂中的叶子 awaitable/operation 的
弃置语义按 backend 分为两类：

- 可安全弃置（poller 持有，库可同步撤销）：timer facade 与 epoll backend 的
  readiness/byte operation。弃置析构先原子认领完成权（poller 此后不可能再打进
  该 operation state），再从注册表摘除记录；若记录已被 poller 提取、正在完成，
  析构会等待其收尾（stop 注册的拆除不得晚于 receiver 环境销毁）。与完成"同一
  瞬间"的弃置仍是调用方自身的竞态。
- 快速失败（kernel 持有 borrowed buffer，无法同步撤销）：io_uring 与 IOCP 的
  in-flight operation。弃置违反 borrowed 契约，析构护栏 `std::terminate()`，
  见各 backend 一节。erased async stream 的悬挂 operation 同属此类。

`io_task` 帧分配支持 P4127 的显式参数路径：coroutine 参数列表以
`(std::allocator_arg_t, std::pmr::memory_resource*)` 开头时，coroutine frame 从该
resource 分配；其余 coroutine 保持全局 operator new 路径。

```cpp
auto parse(std::allocator_arg_t, std::pmr::memory_resource*, int seed)
    -> forge::io::io_task<int> {
    co_return seed + 1;
}

std::pmr::unsynchronized_pool_resource pool;
auto task = parse(std::allocator_arg, &pool, 40);
```

规则与边界：

- 帧分配在帧尾保留一个 pointer 大小的 trailer 记录 owning resource；deallocate 与
  allocate 的 size/alignment 严格配对。全局路径同样带 trailer，即每帧一个指针的
  固定开销。
- resource 为 null 时回退全局路径。
- 帧分配失败以异常从 coroutine 调用表达式传播；刻意不提供
  `get_return_object_on_allocation_failure`，避免把 throwing resource 误判成空 task。
- 传播是显式的：父 coroutine 以 `co_await child(std::allocator_arg, env.memory, ...)`
  转传。HALO 可能合法地 elide 被内联的子帧，此时 resource 观察到的分配次数减少。
- member/lambda coroutine 同样被识别：[dcl.fct.def.coroutine]/9 允许实现把
  implicit object 参数排在声明参数之前传给 `operator new`（GCC 传，Clang 目前
  不传），promise 按 `std::generator` 的 allocator 协议提供 This-aware 重载，
  两类编译器上都命中显式 resource 而不是静默回退全局堆。
- 线程安全由调用方保证：帧的 deallocate 发生在 coroutine 最终完成或被弃置的
  线程上，不保证与 allocate 同线程。跨线程 resume 的 task（如经由
  `runtime_context` 或 `io_uring` 完成路径）必须使用线程安全 resource
  （`synchronized_pool_resource`、`new_delete_resource`）；
  `unsynchronized_pool_resource` 仅适用于单线程 event loop 或外部同步的场景。
- 对齐边界（陷阱警告）：coroutine 帧分配不参与 aligned-new 重载选择，帧对齐
  需求超过 `__STDCPP_DEFAULT_NEW_ALIGNMENT__` 的 coroutine（例如持有
  `alignas(64)` local）会静默拿到默认对齐的存储，没有任何编译期或运行期
  诊断（GCC 16 / Clang 19 实测：local 落在 mod 64 == 16 的地址上）。过对齐
  状态必须放到间接持有的 storage（堆分配或对齐 buffer）里，不要放帧内。

两条被拒绝的替代路径（除非未来 A/B 数据要求重议）：

- `io_env` 驱动的自动帧分配：P4127 的时序问题——`await_transform` 触发时子协程帧
  已经分配完成，环境无法追溯地为自己的帧选择 resource；
- ambient TLS 分配器：隐藏状态使分配来源不可审计，与本仓显式 lifetime 词汇冲突。

`io_env::memory` 维持原语义：只作为 awaitable/operation 内部分配的来源提示，不会
自动接管 coroutine frame allocation。

性能口径（2026-08 基准，见 roadmap `coroutine-native-io.md` 的记录）：小帧上全局
路径更快；pool 帧在 syscall 主导的 io_uring echo 路径上至多带来中个位数百分比改善。
显式 allocator 参数的定位是分配次数控制与确定性，不是默认性能建议。

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
  operation-state 会把传入的 `io_env.stop_token` 与连接方 receiver/env stop token
  融合；任一 stop source 请求都会让 coroutine 内的 `await_sender` 观察到 stopped。
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

`<forge/io/timer_await.hpp>` 是对既有 `forge::timer_context` sender 的 backend-free
coroutine facade：

- `async_sleep_for(context, duration)` 和 `async_sleep_until(context, deadline)` 返回
  `io_task<io_result<>>`。正常到期返回 value 状态。
- Timer sender 的 stopped completion 不会被压成 `io_result` error；经
  `as_sender(io_task<T>)` 消费时仍交付 stopped。Timer sender 只有 value/stopped channel；
  connect-time exception 中的 `std::system_error` 保留 error code，其它异常映射为
  `std::errc::io_error`。
- 正常到期在 timer worker thread 上恢复 coroutine；pre-stopped、shutdown 后启动或
  start-time allocation failure 可以在启动线程同步恢复。两条路径都不会自动 hop 到
  `io_env.executor`，需要时应显式 await `env.executor.schedule()`。
- `timer_context&` 只在 facade 调用期间 borrowed；返回 task 持有 timer sender 的共享
  state。销毁 context 会 shutdown 该 state，使尚未完成的 sleep 观察 stopped。
- 使用自定义 receiver 连接 `as_sender(async_sleep_...)` 时，仍必须遵守
  `as_sender(io_task<T>)` 的 operation-state lifetime 约束。观察 terminal signal 后若要立即
  销毁 operation-state，应先用 `timer_context::wait()` drain source callback。

`<forge/io/context_await.hpp>` 是 `forge::io` 下对现有
`forge::io::context` 的 coroutine facade。它不是新的 backend contract，也不是 socket API；
在 backend 关闭时可以直接 include，但不会暴露需要 `forge::io::context` 的函数。

- `async_read_some(context, handle, std::span<std::byte>)` 和
  `async_write_some(context, handle, std::span<const std::byte>)` 返回
  `io_task<io_result<std::size_t>>`。成功时 byte count 来自现有 backend sender。
- backend 的 `set_error(std::exception_ptr)` 会映射成 `io_result` 的 `std::error_code`；
  `std::system_error` 保留原始 code，其它异常退化为 `std::errc::io_error`。当前 backend
  error path 没有 partial byte count，因此 failure payload 是 `0`。
- `async_read_some(context, handle, non_empty_buffer)` 看到 byte count `0` 时会映射成
  EOF 状态；零长度 read 仍是成功 `0`。`async_write_some` 不合成 EOF。
- backend 的 `set_stopped()` 不会被压成 error code；`as_sender(io_task<T>)` 仍把它交付为
  stopped channel。
- Linux `readable(context, fd)` / `writable(context, fd)` 返回 `io_task<io_result<>>`，
  只表示 readiness 完成，没有 byte count。真实 `read(2)` / `write(2)` 仍由用户代码执行。
- handle、span buffer 和 `context` 都是 borrowed；调用方必须保证它们活到 operation
  完成。`context` 析构仍可能因为 `shutdown()` / `wait()` 而阻塞。

## 使用选择

- 协议 parser/unit test：优先用 `memory_read_stream`、`memory_write_stream` 和
  `scripted_read_stream`，覆盖 short read、EOF、错误和 partial progress。
- non-owning separate-compilation 边界：接收 `any_read_stream&` /
  `any_write_stream&`，让调用方保持 concrete stream 存活。
- 需要转移 concrete stream ownership 的边界：同步使用 `owning_any_read_stream` /
  `owning_any_write_stream`；direct-awaitable coroutine protocol 使用
  `owning_any_async_read_stream` / `owning_any_async_write_stream`，并遵守 single-flight
  与 active-operation lifetime 规则。
- coroutine 与 sender runtime 互通：在 `io_task` 内使用 `await_sender(sender)`，
  或用 `as_sender(io_task<T>)` 把 coroutine operation 暴露给 sender consumer。
- Coroutine sleep：使用 backend-free `<forge/io/timer_await.hpp>`；需要回到业务 executor
  时在 sleep 后显式 await `env.executor.schedule()`。
- Two-child race/deadline：使用 `when_any_results` 或 `with_timeout`，并保留 connected
  operation-state 直到两个 child 的 terminal callbacks 均已返回。
- 已有 OS backend handoff：只在需要现有 `forge::io::context` 时使用
  `<forge/io/context_await.hpp>`；fd/HANDLE、buffer 和 context lifetime 仍由调用方负责。
- Linux completion-queue 语义（coroutine-native proof）：使用
  `<forge/io/io_uring_context.hpp>` 的 `async_read_some` / `async_write_some`；
  awaitable/fd/buffer 是 borrowed，coroutine 恢复发生在 poller thread 上。

## 平台与 gate

当前 backend：

- Linux：`epoll` + `eventfd` readiness。
- Windows：IOCP completion proof。
- Linux：io_uring completion proof（独立 gate，不参与 portable context 选择）。

- `FORGE_ENABLE_FORGE_IO=AUTO`：Linux 或 Windows backend 可用时启用 OS
  backend；其它平台跳过 backend context、backend examples 和 backend tests。
- `FORGE_ENABLE_FORGE_IO=ON`：缺少 Linux `epoll/eventfd` 或 Windows IOCP 支持时
  configure 报错。
- `FORGE_ENABLE_FORGE_IO=OFF`：禁用 OS backend、`forge::io::context` 以及
  backend-specific examples/tests。`error.hpp`、`result.hpp`、`buffer.hpp`、
  `memory_stream.hpp`、`stream.hpp`、`async_stream.hpp`、`coro.hpp`、
  `timer_await.hpp`、`combinators.hpp` 和 `context_await.hpp` 等 direct headers 仍可使用；对应的
  backend-free tests/examples 仍由 `FORGE_TEST_ENABLE_FORGE_IO` 和 example build 开关控制。

`<forge/io.hpp>` 在没有 backend 时可以 include，但不会暴露 `forge::io::context`。
链接 `forge::forge` 时，CMake target 会提供一组一致的 backend feature macros；consumer
不应手工定义它们：

- `FORGE_HAS_FORGE_IO_BACKEND`：至少选中了一个 OS backend，因而可以使用
  `<forge/io/context.hpp>`；
- `FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND`：选中 Linux epoll backend，并提供
  `readable` / `writable` / `*_typed` readiness surface；
- `FORGE_HAS_FORGE_IO_WINDOWS_IOCP_BACKEND`：选中 Windows IOCP backend，提供
  completion-based byte IO surface，不提供 Linux readiness surface。
- `FORGE_HAS_FORGE_IO_URING_BACKEND`：由 gate
  `FORGE_ENABLE_FORGE_IO_URING=AUTO/ON/OFF` 控制的 Linux io_uring
  completion backend proof，暴露 `<forge/io/io_uring_context.hpp>`。它不参与
  `<forge/io.hpp>` 的 portable context 选择，与 epoll readiness backend 并存。
  该 gate 从属于父 gate：`FORGE_ENABLE_FORGE_IO=OFF` 时 io_uring backend 与其
  测试/示例一并关闭（`FORGE_ENABLE_FORGE_IO_URING=ON` 与父 gate OFF 的组合是
  configure 错误），保证 `forge_io|example_forge_io` 审计不变量成立。

Portable source 应使用 per-backend macro 守卫 backend-specific API；参考
`example/forge_context_await_example.cpp`。

macOS/BSD kqueue 当前不在项目需求内，不作为本轮目标。Linux `io_uring` 的重估条件已于
2026-08 触发（byte-stream fabric 方向确认），并已作为独立的 coroutine-native
completion backend proof 落地（见下文"io_uring completion backend"）；它仍不是
`epoll` readiness backend 的替代写法。Zig 可以帮助构建或 C ABI 互操作，但不能抹平
这些 backend 的语义差异。

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
V1 设计边界：epoll backend 的实际 `read(2)` / `write(2)` syscall 在单个 poller
线程上执行，所有 fd 的数据拷贝互相串行化；高吞吐或大 buffer 场景应换用
io_uring backend 或自行分片。
epoll backend 的 operation state 支持安全弃置：销毁已 start 未完成的 operation
会认领完成权并从注册表摘除记录，poller 不会再打进已销毁的 state 或向借用
buffer 写入（见上文弃置语义两分类）。
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
显式 offset IO 需要后续公共 API 扩展。V1 设计边界：`start()` 在 context 全局锁下
发起 `ReadFile` / `WriteFile`，提交与 completion 处理互相串行化；单次传输长度按
`DWORD` 上限（4GB-1）钳制为 short IO，`*_some` 语义由调用方循环补齐。

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
`error_kind::unknown` with a generic `std::errc::io_error` code (an empty code
means no exception at all), keeping the failure diagnosable while the original
exception detail is dropped. Use the default `std::exception_ptr` API when
preserving arbitrary exception diagnostics matters.

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

Linux 和 Windows context 都允许 receiver completion 在 poller/worker thread 上销毁该
context：`wait()` 会避免 self-join，worker 持有的 state keepalive 会继续完成 terminal
release。该例外只覆盖 context 自己；若 `context_options::memory` 指向非 process-lifetime
resource（包括调用方替换过的 default resource），resource 必须在 detached worker、
pending record 和 completion 尾部全部释放后才能销毁。最简单的安全模式仍是由外层 owner
先 `shutdown()` / `wait()`，再按逆序销毁 context 与 resource。

Context drain 只覆盖 context 已接受的 operation 和 poller worker，不会 join 调用方的
submission thread。开始 teardown 前必须先停止并 join 仍可能并发调用 readiness /
read-write sender `start()` 的线程；shutdown 后被拒绝的 operation 可以在该 submitter
线程内同步完成为 stopped。若 external submission 尚未 quiescent，仅调用
`shutdown()` / `wait()` 不能保护 completion 所访问的 user state。

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

当前 Windows parity 覆盖 byte-stream one-shot read/write completion、message-mode read
的 partial-progress completion、stop/cancel finalization、EOF 映射和 package/gate smoke。
message-mode read 遇到 `ERROR_MORE_DATA` 时会以 value completion 返回本次已传输的 byte
count；消息边界与后续分片仍由调用者管理。以下格子故意 scoped out：

- random-access file offset：公共 API 不接收 offset，seekable file IO 需要后续 API 扩展；
- handle ownership / association lifecycle：context 只维护 borrowed-handle cache，不成为
  handle owner；
- Windows-only completion semantics：IOCP completion packet 是已提交 operation 的完成，
  不会被包装成 Linux readiness sender。

要求：

- handle 必须以 overlapped 模式创建；
- handle 必须保持有效直到 operation completion 或 context drain；
- 一个 handle 不应同时绑定到其它 IOCP；
- `async_read_some` / `async_write_some` 是 one-shot operation。
- span 大于 Windows 单次 `DWORD` byte-count 上限时，one-shot operation 只提交
  `DWORD_MAX` 字节；返回的 byte count 仍表示本次实际进度，剩余部分由调用方继续提交。
- context 首次关联 handle 时会请求 `FILE_SKIP_COMPLETION_PORT_ON_SUCCESS`。请求成功时，
  overlapped 调用同步成功后会立即交付 byte count，且不会再产生 completion packet；
  请求失败时，context 会保留 operation，直到普通 completion packet 被 worker drain。
  两条路径都保证 pending record 在任何可能到达的 packet 处理完之前不会被回收或复用。
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

## io_uring completion backend（coroutine-native proof）

`<forge/io/io_uring_context.hpp>` 提供本仓第一个 coroutine-native completion backend
proof：`forge::io::io_uring_context` 持有 SQ/CQ mappings 与专职 poller thread，原生
operation 是直接实现 `io_awaitable` 协议的非协程 operation-state 对象，不是 sender。
Sender consumer 通过显式 `io_task` + `as_sender(io_task, env)` 桥接；该桥会分配
coroutine frame，属于 interop cost。设计决策与冻结语义见
`docs/roadmap/forge-io-backend-spi.md` 的 Phase 0 决策记录。

```cpp
#include <forge/io/io_uring_context.hpp>

forge::io::io_uring_context ring;

auto echo = [](forge::io::io_uring_context& ring, int write_fd, int read_fd,
               std::span<const std::byte> out, std::span<std::byte> in)
    -> forge::io::io_task<forge::io::io_result<std::size_t>> {
    auto wrote = co_await forge::io::async_write_some(ring, write_fd, out);
    if (!wrote.has_value()) {
        co_return wrote;
    }
    co_return co_await forge::io::async_read_some(ring, read_fd, in);
};
```

语义要点：

- `async_read_some(context, fd, span)` / `async_write_some(context, fd, span)`
  是 one-shot stream IO（SQE offset 固定 `-1`），结果为 `io_result<std::size_t>`。
  short IO 照实交付；CQE 负值以 `std::generic_category()` 的正 errno 映射为
  error（含 `-EINTR`，不模拟 readiness backend 的重试）。
- 对端已关闭的 pipe 写以 `EPIPE` error 交付：每次 `io_uring_enter` 都包在与 epoll
  backend 相同的 SIGPIPE guard 内（enter 线程可能内联执行 pipe write 并收到该
  信号），进程的 SIGPIPE 处置不被改动。
- CQE 由 poller thread drain，并在 backend lock 外直接 resume 等待的 coroutine；
  后续 coroutine body 运行在 poller thread 上，需要业务 executor affinity 时显式
  await `env.executor.schedule()`。
- env stop 预检失败、context 已 `close()`/`request_stop()` 时 operation 不进入
  ring，`await_resume()` 抛 `sender_stopped`；空 buffer 在 stop 预检之后、任何
  context 咨询之前内联完成 value `0`（对齐 epoll 先例）。
- 非空 READ 的 CQE `res == 0` 映射 EOF；stop 请求触发的 `-ECANCELED` 映射
  stopped，无 stop 请求的 `-ECANCELED` 是普通 `operation_canceled` error；正常
  value/error 先于 cancellation 完成时照实交付，late cancel 不覆盖。
- receiver/env stop token 对已 accepted operation 触发 `IORING_OP_ASYNC_CANCEL`
  best-effort 取消；cancel CQE 是 administrative drain，用户终态只由 target CQE
  决定。一旦为某 operation 发布了 cancel SQE，该 operation 等 target 与 cancel
  两个 CQE 都 drain 后才 resume，防止 user_data 地址被后续 operation 复用。
- `close()` 幂等拒新且不取消；`request_stop()` 拒新并为所有在飞 operation 排队
  cancel；`shutdown()` 等价于两者之和；析构执行 `shutdown()` + `wait()`，可阻塞。
  从 poller thread 上的 completion 内析构 context 走 detach 路径，非拥有的 memory
  resource 必须活过 detached poller 的最终释放尾部。
- awaitable、fd、buffer、context 都是 borrowed：必须活到 `await_resume()`；悬挂中
  的 operation 不得被弃置（例如销毁尚未完成的 `io_task`）。该契约由析构护栏强制：
  已提交、尚未 resume 的 awaitable 在析构时 `std::terminate()`，避免 kernel 向已
  释放的 frame/buffer 写入这类延迟内存破坏。护栏有意不区分 CQE 是否已 drain：
  未 resume 的 operation 可能还挂在 poller 的侵入式 ready 链上（链节点与
  continuation 就在协程帧里），任何"已完成未恢复"窗口的销毁同样不安全。
  `erased_io_awaitable` 有同型护栏：已启动未 resume 的 erased operation 在析构
  时 `std::terminate()`（否则 slot 永久 active、后端完成会 resume 已释放的帧）。
- V1 边界：SQ 满且 flush 后仍无法接纳时，operation 以
  `std::errc::no_buffer_space` error 完成；poller 遇到非 EINTR/EAGAIN/EBUSY 的
  `io_uring_enter` 硬错误会停机，届时仍悬挂的 awaiter 不会被恢复（proof 阶段
  边界，构造期的同步 NOP round-trip 已把"环从未可用"的沙箱排除在外）。停机后
  新提交一律以 stopped 完成；`context.last_error()` 只返回这类让 poller 停机的
  硬错误（默认零值），用于把后端死亡与 `request_stop()`/`close()` 的优雅排空
  区分开。可恢复的观测（flush 重试错误、瞬态唤醒饱和、异常 administrative CQE
  结果）单独记录，经 `context.last_flush_diagnostic()` 读取，非零只说明环曾经
  繁忙或受压，不代表后端死亡。硬错后仍悬挂的 operation 的 buffer 保持
  borrowed（其 CQE 不再被 drain），强行销毁这些 frame 会触发上述 terminate
  护栏而不是静默 UAF。
- 生命周期唤醒对 flush 硬失败有自愈路径：`close()`/`request_stop()`/`shutdown()`
  发布的唤醒 NOP 若 flush 失败（如瞬态 ENOMEM）会留驻 SQ，`wait()`（含析构）
  在 join 前循环重驱动完整唤醒链（必要时重试发布，已发布则重试 flush）直至内核
  接纳，poller 不会因单次 flush 失败而在无超时的 GETEVENTS 里永久沉睡。数据
  SQE 的 flush 硬失败则在提交时回退发布并以 `no_buffer_space` error 完成，
  不会留下无唤醒保证的悬挂操作。
- io_uring awaitable 的 operation state 大于 03 冻结的 128-byte erasure slot，
  `owning_any_async_*` 按设计在编译期拒绝它；direct async stream concept
  （`async_read_stream` / `async_write_stream`）适配不受影响。

构造在 ring setup、`IORING_REGISTER_PROBE` 必需 opcode 集合或同步 NOP round-trip
失败时抛 `std::system_error`。受限沙箱（容器默认 seccomp 等）通常在构造期以
`ENOSYS`/`EPERM`/`EACCES` 表现；runtime tests 在 AUTO gate 下以 77 skip，显式 `ON`
时视为失败。自定义 seccomp 验证通道见 `scripts/probe-io-uring-container.sh`。

不做（另立任务书前不变）：socket 建连/accept/DNS/TLS、SQPOLL、registered buffers、
multishot、链式 SQE、显式 file offset API、mandatory liburing 依赖，以及对 epoll
readiness backend 的任何替代或改动。

## Resource policy（资源策略）

`forge::io::context_options{.memory = resource}` 控制 context state、pending fd map、
event buffer 和 receiver record 等 context-owned allocation。Linux backend 从 pending
map 摘下 record 后，通过 record 内嵌的 intrusive action chain 在锁外完成 receiver；
readiness、cancel、shutdown 和 error completion 的 deferred batch 本身不再分配。
resource 非拥有，必须比 context 活得更久。
若 context 在自己的 completion 中析构并 detach worker，resource 还必须活过随后发生的
state/record terminal release tail；仅活到 context destructor 返回并不够。

这不控制用户 fd、用户 buffer，也不承诺标准库内部对象零分配。

## 示例

- `example/forge_line_protocol_example.cpp`：`read_until` + memory streams 上的
  tiny line request/response。
- `example/forge_coro_line_pipeline_example.cpp`：memory stream read -> coroutine
  parse -> strand state update -> response write 的 runtime composition smoke。
- `example/forge_coro_timeout_example.cpp`：scripted memory read 的 task-first 与
  timer-first timeout 路径。
- `example/forge_owned_async_stream_example.cpp`：main/protocol 两个 translation units
  之间的 owning direct-awaitable stream boundary。
- `example/forge_timer_await_example.cpp`：timer worker 上恢复 coroutine，再显式 hop 到
  `io_env.executor`。
- `example/forge_io_readiness_example.cpp`：nonblocking pipe + `readable(fd)`。
- `example/forge_io_pipeline_example.cpp`：IO readiness -> strand continuation ->
  channel message。
- `example/forge_io_read_write_example.cpp`：borrowed span + async read/write convenience。
- `example/forge_context_await_example.cpp`：coroutine facade over context sender；
  Linux 下演示 await readiness 后由用户代码执行 nonblocking `read(2)`。
- `example/forge_io_iocp_example.cpp`：Windows named pipe + IOCP async read/write。
- `example/forge_io_uring_read_write_example.cpp`：io_uring completion backend 上的
  coroutine-native write -> read round trip，echo coroutine 经显式 allocator 参数把
  frame 放进 pool resource；runtime 受限沙箱下打印 skip 信息退出。

# `inplace_stop_callback` 无分配设计记录

这份记录说明 Forge execution backport 的 `std::inplace_stop_callback` 为什么暂时保留
per-registration control-block allocation，以及未来移除该分配必须同时证明哪些不变量。
它记录的是一个明确的 conformance residual，不把当前实现描述成 current-WD complete。

## 当前偏差

Current WD `[stopcallback.inplace.cons]` 要求 callback constructor 的异常规格等于
`is_nothrow_constructible_v<CallbackFn, Initializer>`，并且 callback registration 只能抛出
callback 自身构造产生的异常。当前 Forge implementation 为每个 registration 调用
`make_shared<callback_control>()`，因此：

- 即使 callback 是 nothrow constructible，constructor 仍可能抛 `bad_alloc`；
- 每次 registration 都产生一次 allocator-neutral heap allocation；
- constructor 不能声明 WD 要求的条件 `noexcept`。

这是真实偏差，不应与 `forge::any_stop_token` 类型擦除所接受的 allocator-neutral allocation
混为一谈。

## 为什么当前 control block 存在

`request_stop()` 会先在 source lock 下 detach 全部 callback，再在锁外调用用户代码。
独立 control block 在 callback object 和 source object 之外保存：

- callback 是否已经 detached、cancelled 或正在执行；
- 发起 stop 的 thread id；
- concurrent deregistration 等待当前 callback 返回所需的 mutex / condition variable；
- detached callback chain 的所有权。

这使当前 backport 还能承受一个超出标准 lifetime precondition 的 Forge 路径：

```text
request_stop()
  -> callback completes an operation
     -> receiver synchronously destroys that operation
        -> callback object, or a nested stop source, is destroyed
```

`docs/roadmap/unified-cancellation-design.md` 和对应 ASan tests 已把这条路径纳入 Forge
backport 的 proof surface。共享 control block 让 `request_stop()` 在用户 callback 返回后
不必再次访问已经销毁的 callback object，也不依赖 owning operation 仍然存在。

## 参考实现对照

截至 2026-07-30，检查的 LLVM libc++ main
(`8a1ff7feefc5825cc10349b8946fe555f435d53b`) 和 Microsoft STL main
(`adf25f70db25f0db934d13811fcc1e4d40c44fd8`) 尚未提供可用于对照的
`inplace_stop_source` implementation。以下两个成熟 sender library implementation
采用相同的 allocation-free 方案：

- [NVIDIA stdexec `stop_token.hpp`](https://github.com/NVIDIA/stdexec/blob/f0e8ae6fdc6c188389b146bb854b80d399724b04/include/stdexec/stop_token.hpp)
- [libunifex `inplace_stop_token.hpp`](https://github.com/facebookexperimental/libunifex/blob/effb7527401b32b5a2d82fdf6d1a8e8810cbdb07/include/unifex/inplace_stop_token.hpp)
  与
  [out-of-line state machine](https://github.com/facebookexperimental/libunifex/blob/effb7527401b32b5a2d82fdf6d1a8e8810cbdb07/source/inplace_stop_token.cpp)

两者都把 callback node 内联在 `inplace_stop_callback`，通过 source-owned spin lock、
`removedDuringCallback` stack flag 和 per-callback completion atomic 协调注销。它们满足
标准要求的无分配和条件 `noexcept`，但共同依赖标准 lifetime rule：

- source 必须活过所有 associated token 和 callback；
- `request_stop()` 在 callback 返回后会重新访问 source；
- source destructor 会断言没有 callback 且 source lock 不在使用中。

因此，直接复制这套实现会删除 Forge 当前已测试的重入销毁能力。

Intel bare-metal sender library 也使用 intrusive allocation-free callback list，但它没有实现
标准要求的 concurrent callback deregistration wait，不能作为本仓库线程安全实现的模板。

## 候选方向

### 方向 A：采用标准 lifetime rule

采用 stdexec / libunifex 的 proven intrusive state machine，并把 constructor 恢复为条件
`noexcept`。

优点：

- 直接满足 current WD；
- 无 per-registration allocation；
- 状态机已有成熟实现经验。

前置工作：

- 找出所有依赖 callback/source 在 `request_stop()` 栈上被销毁的 Forge sender；
- 给每个具体 sender 增加 completion keepalive，或收紧其 self-destroy receiver contract；
- 在 native stand-aside lane 证明相同的 lifetime contract。

在这些前置工作完成前直接采用方向 A 会重新引入 UAF。

### 方向 B：stack-owned detached request context

理论上可以让 `request_stop()` 在栈上建立 detached-list context；callback node 原子发布该
context，pending callback destructor 在 context lock 下摘链，running callback destructor
按 thread id 决定等待或标记 self-removal。source 被销毁后，request loop 只访问栈上
context。

这条路线可能同时保留无分配与 Forge 重入扩展，但目前没有成熟参考实现，且必须证明：

- registration / detach / destructor 三方 publication 没有漏唤醒或 ABA；
- pending callback 可被任意 callback 同步销毁而不留下 raw-pointer UAF；
- concurrent destructor 返回前，request context 不会离开栈；
- self-destroying running callback 返回后，request loop 不再触碰 callback storage；
- source destruction 与尚未观察到 detached context 的 destructor 不竞争。

在有完整状态机证明和 deterministic interleaving tests 之前，不实现这条路线。

### 方向 C：暂时保留独立 control block

保持当前 correctness behavior，接受 allocation 和 constructor exception-spec deviation。
这是当前选择。它不是永久标准化方案，也不是性能最优方案。

## 后续实现门槛

未来重开此项时，必须一次满足：

1. `is_nothrow_constructible_v<inplace_stop_callback<F>, inplace_stop_token, F>` 对
   nothrow `F` 为 true；
2. callback registration 本身零 allocation；
3. callback destructor 在另一线程执行 callback 时等待，在同一 callback thread 自毁时不等待；
4. callback 可销毁任意尚未执行的 sibling callback；
5. concurrent register / request / destroy stress 在 TSan 下通过；
6. 所有现有 operation self-destroy ASan tests 保持通过；
7. 若选择标准 lifetime rule，先修改具体 Forge sender，不能靠删除既有 regression test
   掩盖 UAF。

在这些条件具备前，不把一个局部 `source` condition variable patch 当作修复。

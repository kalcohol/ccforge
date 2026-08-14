# Erased stop-token allocator 设计记录

这份记录说明 Forge extension 的 erased stop-token callback allocator-awareness 取舍。
它是设计结论，不是待实现任务。

## 边界

当前标准 execution vocabulary 定义 `inplace_stop_source/token/callback`、
`never_stop_token`、`stop_callback_for_t` 和 stoppable-token concepts，但不定义
`std::any_stop_token`。因此 Forge 不应把自己的 type erasure 注入 `namespace std`，
也不应让 native execution COMPLETE 探针依赖这个非标准名字。

公共 Forge 类型是 `<forge/any_stop_token.hpp>` 中的 `forge::any_stop_token`：

- token implementation 放在 `shared_ptr` 中；
- callback state 放在另一个 `shared_ptr` 中；
- concrete callback registration 由 `unique_ptr` 持有；
- 这些 allocation 当前都是 allocator-neutral。

`bounded_channel`、`timer_context`、`forge::task`、Forge erasure 和两个 IO backend
使用该类型。标准 backport 内的 `spawn_future` 不经过 type erasure，而是直接按 receiver
env 返回的具体 stop-token 类型实例化 callback registration。

其它路径在 API 有明确 allocator channel 时已经实现 allocator-aware：

- `spawn` 和 `spawn_future` 会从 `env` 或 source sender attributes 选择 allocator；
- `spawn_future` 使用该 allocator 分配 shared state 和 consumer record；
- Forge runtime primitives 使用 `std::pmr::memory_resource*` 控制 queue、record、
  timer item 和 callable record。

剩余取舍仅限 `forge::any_stop_token::callback_type<Callback>` 的内部 allocation；这个
public callback shape 没有 allocator 参数。

## 备选方案

### 保持 allocator-neutral Forge erasure

- API compatibility：稳定，Forge extension 名字和标准 surface 明确分层。
- Template/code-size：最低，runtime wrappers 共享一条 erased-token path。
- Runtime overhead：每个 erased token/callback 可能使用默认 heap。
- Native handoff：标准 backport 不依赖 Forge vocabulary；完整 native execution 可以直接让位。

### 增加内部 allocator-aware callback wrapper

拥有 resource 或 allocator 的 runtime primitive 可以直接按 concrete downstream token
注册 callback，并用本地 allocator 管理 wrapper。该方案不需要新增 public API，但会让
channel、timer、IO 和 erasure 各自多一套 callback lifetime 实现。

只有在 allocation control 成为可测的硬需求时才采用；当前增加的生命周期证明面大于收益。

### 增加 allocator-parameterized public token

例如新增 `forge::pmr_any_stop_token`。这会要求 env propagation 和 erased receiver
理解多种 erased-token 形态，并增加用户可见 vocabulary。当前拒绝该方案。

## 决策

保留 `forge::any_stop_token` 的 allocator-neutral internals；标准 execution backport
只实现标准 stop-token surface，`spawn_future` 直接注册 concrete token callback。

不要恢复 `std::any_stop_token`，不要把 Forge-only constructor 或 allocator behavior
注入 `namespace std`。若未来需要 resource-controlled stop-callback allocation，先做
窄的 Forge-internal wrapper，并为其 callback 销毁、并发 request-stop 和 PMR teardown
补强 oracle。

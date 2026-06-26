# Execution stop-token allocator 设计记录

这份记录说明 erased stop-token callback 的 allocator-awareness 取舍。它是设计结论，
不是待实现任务。

## 问题

`std::any_stop_token` 是 execution backport 和 Forge runtime wrappers 共同使用的
standard-shaped erased stop-token vocabulary。当前实现会把 erased token 放在
`shared_ptr` 中，把 callback state 放在另一个 `shared_ptr` 中，并用 `unique_ptr`
保存具体 callback registration。这些 allocation 目前都是 allocator-neutral。

其它 execution 路径在 API 有明确 allocator channel 时已经实现 allocator-aware：

- `spawn` 和 `spawn_future` 会从 `env` 或 source sender attributes 选择 allocator；
- `spawn_future` 使用该 allocator 分配 shared state 和 consumer record；
- Forge runtime primitives 使用 `std::pmr::memory_resource*` 控制 queue、record、
  timer item 和 callable record。

剩余缺口更窄：erased stop-token callback internals 在标准形状
`any_stop_token::callback_type<TokenCallback>` API 中没有自然 allocator 参数。

## 方案 1：保持 `std::any_stop_token` 标准形状

保持当前 public shape，并接受 internals 的 allocator-neutral。

- API compatibility：最好。类型继续贴近预期标准 vocabulary，native handoff 简单。
- Template/code-size 影响：最低。仍只有一条 erased token 和 callback path。
- Runtime allocation overhead：不变；每个 erased token/callback 可能使用默认 heap。
- Native-handoff 影响：最好。不会给 `std::any_stop_token` 增加 Forge-only constructor 或 allocator 参数。
- 使用路径：当前所有 erased-stop-token 用户，包括 `spawn_future`、`bounded_channel`、
  `timer_context`、`forge::io` 和 `forge::erased_sender`。

这是当前 backport 接受的决策。它保留 standard-shaped stop-token vocabulary，避免把
Forge-only allocator 参数塞进一个未来应当无感让位的类型。

## 方案 2：内部 allocator-aware callback wrapper

保持 `std::any_stop_token` 不变，但给拥有 resource 或 allocator 的 Forge runtime 路径增加
内部 helper。该 helper 用指定 resource 分配 erased callback state/control block，并在转成
`std::any_stop_token` 之前或之外直接注册具体 downstream token。

- API compatibility：好，只要放在 `forge::detail` 或本地 operation-state 实现里即可。
- Template/code-size 影响：中等。runtime 路径会多实例化一个小 wrapper，但 public sender signatures 不变。
- Runtime allocation overhead：对 Forge runtime 路径最好。Channel、timer、IO 以及未来
  `spawn_future` consumer path 都可走已有 resource/allocator。
- Native-handoff 影响：好。标准形状代码仍看见 `std::any_stop_token`；Forge-only resource control 是实现细节。
- 使用路径：先用于 Forge runtime primitives，再在 allocator channel 可用时考虑
  `spawn_future` consumer stop-callback registration。

如果未来某条 Forge runtime 路径对 resource-controlled stop-callback allocation 有硬需求，
这仍是可能的 internal-only experiment。当前不启动，因为它会增加第二套 stop-callback 实现和更多 lifetime surface，收益只是较小的 allocation-control 改进。

## 方案 3：allocator-parameterized stop-token variant

引入新的 public erased token，例如 PMR-backed 或 allocator-typed 的
`forge::pmr_any_stop_token`。

- API compatibility：混合。它不修改 `std::any_stop_token`，但新增一个需要 env query 传播的 public vocabulary type。
- Template/code-size 影响：最高。Env propagation、receiver wrappers 和 erased-sender 都要理解多个 erased stop-token 形态或在它们之间转换。
- Runtime allocation overhead：潜在较好，但前提是每个 wrapper/query 都保持 PMR token，而不是退化为 `std::any_stop_token`。
- Native-handoff 影响：较弱。用户代码可能开始依赖 Forge-only token type，native execution handoff 需要额外 adaptation story。
- 使用路径：只应限于 Forge extension code，标准 backport path 不应使用。

当前项目形态下拒绝此方案。它会增加 Forge-only public vocabulary，并削弱 native handoff。

## 决策

接受方案 1：保持 `std::any_stop_token` 标准形状，并文档化其 allocator-neutral internals。

Allocator-neutral callback/type-erasure control blocks 是已接受的 tradeoff，不是开放的
convergence defect。试图通过修改 public `std::any_stop_token` 把这条路径变成
allocator-aware，会制造比它消除的 native-handoff 风险更多的问题。若未来 resource-controlled
stop-callback allocation 成为硬需求，先回到方案 2 做窄的 Forge-internal helper；不要先增加
public PMR stop-token vocabulary。

不要给 `std::any_stop_token` 增加 allocator-taking constructor 或 Forge-only behavior。

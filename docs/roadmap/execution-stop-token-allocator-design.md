# execution stop-token allocator design

This note records the accepted allocator-awareness decision for erased
stop-token callbacks. It is intentionally a design record, not an implementation
task.

## problem

`std::any_stop_token` is the standard-shaped erased stop-token vocabulary used by
the execution backport and by Forge runtime wrappers. Its current implementation
stores the erased token in a `shared_ptr`, stores callback state in another
`shared_ptr`, and stores the concrete callback registration behind a
`unique_ptr`. Those allocations are allocator-neutral today.

Other execution paths have already been made allocator-aware where the API has a
clear allocator channel:

- `spawn` and `spawn_future` select an allocator from `env` or source sender
  attributes when available;
- `spawn_future` uses that allocator for shared state and consumer records;
- Forge runtime primitives use `std::pmr::memory_resource*` for their queues,
  records, timer items, and callable records.

The remaining gap is narrower: erased stop-token callback internals do not have
a natural allocator parameter in the standard-shaped
`any_stop_token::callback_type<TokenCallback>` API.

## option 1: keep `std::any_stop_token` standard-shaped

Keep the current public shape and accept allocator-neutral internals.

- API compatibility: best. The type stays close to the expected standard
  vocabulary and native handoff remains straightforward.
- Template/code-size impact: lowest. One erased token and callback path remains.
- Runtime allocation overhead: unchanged; each erased token/callback may use
  default heap allocations.
- Native-handoff impact: best. No Forge-only constructor or allocator parameter
  is added to `std::any_stop_token`.
- Paths using it: all current erased-stop-token users, including `spawn_future`,
  `bounded_channel`, `timer_context`, `forge::io`, and `forge::erased_sender`.

This is the accepted decision for the current backport. It preserves the
standard-shaped stop-token vocabulary and avoids adding Forge-only allocator
parameters to a type that should hand off cleanly when native execution becomes
available.

## option 2: internal allocator-aware callback wrapper

Keep `std::any_stop_token` unchanged, but add a Forge-internal helper for
runtime paths that have a resource or allocator in hand. The helper would own an
erased callback state/control block allocated from that resource and would
register against the concrete downstream token before or instead of converting
through `std::any_stop_token`.

- API compatibility: good if kept internal under `forge::detail` or the local
  operation-state implementation. No public `std::any_stop_token` shape changes.
- Template/code-size impact: moderate. Each runtime path may instantiate a small
  wrapper, but public sender signatures stay unchanged.
- Runtime allocation overhead: best for Forge runtime paths. Channel, timer,
  IO, accel, and future `spawn_future` consumer paths could route callback
  records through their existing resource/allocator.
- Native-handoff impact: good. Standard-shaped code still sees
  `std::any_stop_token`; Forge-only resource control remains an implementation
  detail.
- Paths using it: Forge runtime primitives first (`bounded_channel`,
  `timer_context`, `forge::io`, `forge::accel` support code), then
  `spawn_future` consumer stop-callback registration if the allocator channel is
  available at that point.

This remains a possible internal-only experiment if a future Forge runtime path
has a hard resource-control requirement. It is not active work today because it
would add a second stop-callback implementation and more lifetime surface for a
minor allocation-control gain.

## option 3: allocator-parameterized stop-token variant

Introduce a new public erased token, such as a PMR-backed or allocator-typed
`forge::pmr_any_stop_token`.

- API compatibility: mixed. It avoids mutating `std::any_stop_token`, but adds a
  second public vocabulary type that must be carried through env queries.
- Template/code-size impact: highest. Env propagation, receiver wrappers, and
  erased-sender code must understand multiple erased stop-token forms or convert
  between them.
- Runtime allocation overhead: potentially good, but only when every wrapper and
  query preserves the PMR token rather than degrading to `std::any_stop_token`.
- Native-handoff impact: weaker. User code may start depending on a Forge-only
  token type, so native execution handoff needs an explicit adaptation story.
- Paths using it: only Forge extension code should use it; standard backport
  paths should not.

This option remains rejected for the current project shape. It would add a
Forge-only public vocabulary and weaken native handoff.

## decision

Option 1 is accepted: keep `std::any_stop_token` standard-shaped and document
its allocator-neutral internals.

The allocator-neutral callback/type-erasure control blocks are an accepted
tradeoff, not an open convergence defect. Trying to make this path
allocator-aware through public `std::any_stop_token` changes would create more
native-handoff risk than it removes. If resource-controlled stop-callback
allocation ever becomes a hard runtime requirement, revisit option 2 as a narrow
Forge-internal helper; do not add a public PMR stop-token vocabulary first.

Do not add allocator-taking constructors or Forge-only behavior to
`std::any_stop_token`.

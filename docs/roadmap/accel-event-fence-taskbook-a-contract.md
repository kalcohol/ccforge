# Taskbook A: Accel Event/Fence Contract

## Objective

Define the minimal public event/fence contract before changing code.

## Current Facts

- `forge::accel` currently exposes `context`, `queue`, `device_buffer`,
  `copy_to_device`, `copy_to_host`, `copy_device_to_device`, and `submit`.
- All commands are accepted on `start()`.
- Commands reuse `resource_context + strand`, so queue order is serialized.
- Existing command senders complete with `set_value_t()`,
  `set_error_t(std::exception_ptr)`, and `set_stopped_t()`.
- `docs/forge-accel.md` explicitly says standalone event/fence was deferred.

## Contract

Add:

```cpp
namespace forge::accel {

class event {
public:
    event();
    [[nodiscard]] bool ready() const noexcept;
};

auto record_event(queue& q, event ev);
auto wait_event(queue& q, event ev);
auto fence(queue& q);

} // namespace forge::accel
```

`event` is a copyable handle. Copies observe the same ready state. V1 event state
is one-shot: it starts unready and becomes ready once a `record_event` command
executes. V1 does not expose reset.

`record_event(q, ev)` returns a void command sender. When accepted and executed,
it marks `ev` ready and then completes `set_value()`.

`wait_event(q, ev)` returns a void command sender. When accepted and executed, it
waits until `ev` is ready. If the owning context requests stop before `ev`
becomes ready, it completes stopped.

`fence(q)` returns a void command sender that performs no user work. Because the
queue is serialized, it completes after earlier accepted commands in the same
queue ordering.

## Explicit Limitations

- Dependency cycles are not detected. `wait_event(q, ev)` before the only
  `record_event(q, ev)` on the same serialized queue can stall until context stop.
- There is no multi-stream, cross-context, cross-process, or native event handle.
- There is no event reset/re-record.
- Event allocation is not part of this round's resource-policy work.

## Tests To Require

- default event starts unready;
- copied handle observes readiness after `record_event`;
- `wait_event` completes for a ready event;
- `wait_event` on an unready event completes stopped after context stop;
- `fence` does not complete until already accepted work in front of it completes.

## Verification

Focused compile/test of the accel group is enough for this taskbook. Sanitizers
belong to Taskbook C after implementation and tests are complete.

# Forge channel post-enqueue stop callbacks

## Goal

Upgrade `forge::bounded_channel` from pre-start stop-token observation to
per-operation post-enqueue cancellation for pending send/recv operations.

This closes the most visible V1 channel lifecycle caveat: a receiver stop token
requested after `start()` no longer needs `close()` / `request_stop()` / another
channel state change to wake an idle pending operation.

## Scope

- Add stop-callback registration only for operations that actually become
  pending.
- Complete canceled pending operations with `set_stopped()`.
- Keep all user completions outside the channel mutex.
- Preserve `close()` drain semantics and `request_stop()` cancel-now semantics.
- Keep send/recv completion signatures unchanged.

## Non-Goals

- No IO backend stop-callback changes in this slice.
- No timer-context stop-callback rewrite.
- No typed-error changes.
- No public cancellation token abstraction beyond normal receiver env stop token.

## Acceptance

- Pending recv stop-token request completes stopped immediately.
- Pending send stop-token request completes stopped immediately.
- The canceled operation is removed from pending queues and does not consume or
  deliver later channel values.
- Existing close/request_stop/reentrancy/move-only tests remain green.
- ASan/TSan stay clean.

## Taskbooks

1. `docs/roadmap/channel-stop-callback-taskbook-a-contract.md`
2. `docs/roadmap/channel-stop-callback-taskbook-b-implementation.md`
3. `docs/roadmap/channel-stop-callback-taskbook-c-docs-verification.md`

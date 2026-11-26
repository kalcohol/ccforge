# Parallel scheduler proxy stop adapter

This model covers the translated-token path in the [adapter header](../../backport/cpp26/execution/detail/proxy_stop_adapter.hpp). It gives batch C-05 and the shared first-query case G2-06 bounded protocol evidence. It does not claim that the parallel scheduler frontend or any provider exists.

## Protocol and assumptions

- SC compare-exchange claims first use. The winner constructs the in-place source and registers only if the upstream token can stop. A competitor can see ready immediately, notice it before sleeping, or park and be notified. Ready publishes the source and completed callback registration. Each answer records those facts and delivery of a stop pending at registration as they stood at return, before any later interleaving can repair them. Identity is represented by the only source cell or by the disengaged value.
- An upstream request or callback registration can linearize first. The stop contract is the same as in the [stop-token model](../stop_token/README.md): a selected callback runs once, registration against an already requested source invokes on the registering thread, and removing an executing callback waits on a foreign invocation but not on its own thread. The model uses these abstract transitions and does not re-model the upstream source lock.
- Queries and item execution join before terminal dispatch. The terminal may race upstream stop. Outward completion may delete the proxy, so a forwarding invocation pins the source until its own call returns, independently of the proxy and registered callback. The terminal step models entry and possible immediate destruction together. The runtime trace ends at terminal cleanup and checks outward entry separately.
- Bounds are one operation, zero to two queries, up to two upstream requests, and one outward terminal. The suite includes a request before registration (including during source setup), one racing after registration, cross-thread removal, same-thread removal from a downstream callback, a nonrequestable token and the unused path. No C++ lowering, arbitrary custom token implementation, platform memory semantics beyond the used SC contract, backend work queues or allocator failures are proved here. Exact token and unsupported-query dispatch are covered by the C++ fixtures.

## Invariants and sensitivity

| Check | Obligation | Failing sensitivity case |
|---|---|---|
| PA1 | A committed stop must reach the translated source | `lose_request.already_requested.2` |
| PA2 | Single source initialization, callback registration and forwarding | `duplicate_registration.request_race.2` |
| PA3 | No registered callback storage at outward completion | `terminal_before_cleanup.request_race.2` |
| PA4 | A forwarding invocation may resume after destruction only with its source pin | `no_source_pin.same_thread_cleanup.2` |
| PA5 | A translated answer returns only after ready publication, one callback registration and delivery of any stop already pending at registration. An impossible token gives the stable disengaged answer without registering | `publish_before_registration.request_race.2`, `publish_before_immediate_forward.already_requested.2` |
| PA6 | All queries, the invocation and the terminal retire | — |

The two mutants and four negative controls run in both tiers. The lose-request mutant leaves synchronous invocation to the upstream library and skips `request_stop()` in the adapter forwarder. The PA5 controls publish too early, first before registration and then before a committed synchronous invocation can deliver the pending stop. Each tier run validates the exact named PA category for each negative case against the [sensitivity manifest](sensitivity.txt) before the shared runner can regenerate [expected records](expected/results.tsv) or refresh the lock. The passing configurations also require the scenario-specific transition coverage in [scenarios.py](scenarios.py).

## Checking and maintenance

```bash
python3 -B formal/proxy_stop_adapter/check.py --check-source
python3 -B formal/proxy_stop_adapter/check.py --tier quick --jobs 2
python3 -B formal/proxy_stop_adapter/check.py --tier full --jobs 2
python3 -B formal/proxy_stop_adapter/check.py --steps
```

Quick selects ten configurations and full selects nineteen. Each configuration is capped at 4,000 states. The tool saves results, summaries and shortest counterexamples under ignored `build/formal/proxy_stop_adapter/`. Review protocol fidelity against the C++ before refreshing [source.lock](source.lock). For changes to model or data, qualify a full run, inspect the named failures, update the expected records with `--tier full --update-expected`, then rerun separately with `--tier full --refresh-lock --assessment '<review>'`. The lock detects edits and checks function-local source anchors. It is not a proof of source/model equivalence.

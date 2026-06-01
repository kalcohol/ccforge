#!/usr/bin/env bash
#
# Manual audit helper for runtime wait/notify and cancellation paths.
#
# This script is intentionally conservative: it lists sites that deserve human
# review instead of trying to prove correctness. For condition_variable users,
# the review rule is that changes to the wait predicate must be published under
# the same mutex used by the waiter before notify_one/notify_all.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

patterns='condition_variable|wait_for|wait_until|[^[:alnum:]_]wait\(|notify_one|notify_all|stop_callback|request_stop'
paths=(
    include/forge
    backport/cpp26/execution
)

printf '[runtime-wakeup-audit] review rule: publish condition_variable predicates under the waiter mutex before notify\n'
printf '[runtime-wakeup-audit] paths: %s\n' "${paths[*]}"
printf '[runtime-wakeup-audit] candidate sites follow\n'

rg --sort path -n --glob '*.hpp' --glob '*.cpp' "${patterns}" "${paths[@]}"

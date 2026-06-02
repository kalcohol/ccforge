#!/usr/bin/env bash
#
# verify-selfhosted-floor.sh — CI-portable local verification floor.
#
# This script deliberately does not know about GitHub Actions or any hosted CI
# provider. It runs the existing verification entrypoints sequentially so the
# same command can be used on a local workstation, a Jenkins agent, a self-hosted
# runner, or a future hosted runner.
#
# Usage:
#   scripts/verify-selfhosted-floor.sh [native-lane ...]
#
# Defaults:
#   native lanes: llvm gcc-exec tsan asan
#   install package check: enabled
#   Windows/MSVC smoke: disabled unless FORGE_VERIFY_FLOOR_WINDOWS=1
#
# Environment:
#   FORGE_VERIFY_FLOOR_NATIVE_LANES
#       Space-separated native lanes used when no positional lanes are passed.
#   FORGE_VERIFY_FLOOR_SKIP_INSTALL=1
#       Skip scripts/verify-install-package.sh.
#   FORGE_VERIFY_FLOOR_WINDOWS=1
#       Also run the Windows/MSVC smoke through scripts/verify-windows-msvc-matrix.sh.
#       The Windows wrapper consumes FORGE_WINDOWS_* environment variables.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

log() {
    printf '[verify-floor] %s\n' "$*"
}

if [[ $# -gt 0 ]]; then
    native_lanes=("$@")
else
    # shellcheck disable=SC2206
    native_lanes=(${FORGE_VERIFY_FLOOR_NATIVE_LANES-llvm gcc-exec tsan asan})
fi

if [[ ${#native_lanes[@]} -eq 0 ]]; then
    log "no native lanes requested"
else
    log "native lanes: ${native_lanes[*]}"
fi

for lane in "${native_lanes[@]}"; do
    log "start native lane: ${lane}"
    scripts/verify-native.sh "${lane}"
    log "ok native lane: ${lane}"
done

if [[ "${FORGE_VERIFY_FLOOR_SKIP_INSTALL:-0}" != "1" ]]; then
    log "start install package smoke"
    scripts/verify-install-package.sh
    log "ok install package smoke"
else
    log "skip install package smoke"
fi

if [[ "${FORGE_VERIFY_FLOOR_WINDOWS:-0}" == "1" ]]; then
    log "start Windows/MSVC smoke"
    scripts/verify-windows-msvc-matrix.sh
    log "ok Windows/MSVC smoke"
else
    log "skip Windows/MSVC smoke (set FORGE_VERIFY_FLOOR_WINDOWS=1 to enable)"
fi

log "verification floor passed"

#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 4 ]]; then
    printf '%s\n' \
        'usage: run-io-uring-runtime-gate.sh <cxx> <timeout-seconds> <require-runtime:0|1> <probe> [build-dir ...]' \
        >&2
    exit 2
fi

compiler="$1"
timeout_seconds="$2"
require_runtime="$3"
probe="$4"
shift 4

timeout_command="${FORGE_IO_URING_TIMEOUT_COMMAND:-}"
if [[ -z "${timeout_command}" ]]; then
    if command -v timeout >/dev/null 2>&1; then
        timeout_command=timeout
    elif command -v gtimeout >/dev/null 2>&1; then
        timeout_command=gtimeout
    else
        printf '%s\n' \
            'io_uring runtime gate requires GNU timeout or gtimeout' >&2
        exit 2
    fi
elif ! command -v "${timeout_command}" >/dev/null 2>&1; then
    printf 'io_uring timeout command is unavailable: %s\n' \
        "${timeout_command}" >&2
    exit 2
fi

run_bounded() {
    "${timeout_command}" --signal=TERM --kill-after=5s \
        "${timeout_seconds}s" "$@"
}

case "${require_runtime}" in
    0|1) ;;
    *)
        printf 'invalid require-runtime value: %s\n' "${require_runtime}" >&2
        exit 2
        ;;
esac

set +e
run_bounded \
    env CXX="${compiler}" \
    FORGE_IO_URING_PROBE_BUILD_DIR=/tmp/ccforge-io-uring-probe \
    "${probe}"
probe_status=$?
set -e

if [[ ${probe_status} -eq 77 ]]; then
    if [[ "${require_runtime}" == "1" ]]; then
        printf '%s\n' \
            '[io-uring-container] runtime required but unavailable' >&2
        exit 1
    fi
    printf '%s\n' \
        '[io-uring-container] runtime unavailable; bounded skip'
    exit 0
fi
if [[ ${probe_status} -ne 0 ]]; then
    exit "${probe_status}"
fi

for dir in "$@"; do
    for name in test_forge_io_uring_context test_forge_io_uring_rw; do
        binary="${dir}/test/forge/${name}"
        if [[ ! -x "${binary}" ]]; then
            printf 'missing io_uring test binary: %s\n' "${binary}" >&2
            exit 3
        fi
        printf '[io-uring-container] running %s\n' "${binary}"
        run_bounded "${binary}"
    done

    # The failure-injection binary only exists when the linker supports
    # --wrap=syscall, so absence is a toolchain capability gap.
    fault_binary="${dir}/test/forge/test_forge_io_uring_fault"
    if [[ -x "${fault_binary}" ]]; then
        printf '[io-uring-container] running %s\n' "${fault_binary}"
        run_bounded "${fault_binary}"
    else
        printf '[io-uring-container] %s not built (linker lacks --wrap); skipping\n' \
            "${fault_binary}"
    fi
done

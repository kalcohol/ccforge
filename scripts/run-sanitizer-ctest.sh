#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 4 ]]; then
    echo "usage: $0 <asan|tsan> <build-dir> <test-regex> <first-run-log>" >&2
    exit 2
fi

kind="$1"
build_dir="$2"
test_regex="$3"
first_log="$4"

case "${kind}" in
    asan|tsan) ;;
    *)
        echo "unsupported sanitizer kind: ${kind}" >&2
        exit 2
        ;;
esac

mkdir -p "$(dirname "${first_log}")"

ctest_command="${CTEST:-ctest}"
setarch_command="${SETARCH:-setarch}"

set +e
"${ctest_command}" \
    --test-dir "${build_dir}" \
    -R "${test_regex}" \
    --output-on-failure 2>&1 | tee "${first_log}"
first_status=${PIPESTATUS[0]}
set -e

if [[ ${first_status} -eq 0 ]]; then
    exit 0
fi

startup_mapping_failure=false
if [[ "${kind}" == tsan ]]; then
    if grep -Fq "FATAL: ThreadSanitizer: unexpected memory mapping" "${first_log}"; then
        startup_mapping_failure=true
    fi
elif grep -Fq \
        "Shadow memory range interleaves with an existing memory mapping" \
        "${first_log}"; then
    startup_mapping_failure=true
fi

if [[ "${startup_mapping_failure}" != true ]]; then
    echo "[${kind}] first run failed without a recognized startup mapping failure; not retrying" >&2
    exit "${first_status}"
fi

echo "[${kind}] retrying under setarch -R after recognized startup mapping failure"
"${setarch_command}" "$(uname -m)" -R \
    "${ctest_command}" \
    --test-dir "${build_dir}" \
    -R "${test_regex}" \
    --output-on-failure

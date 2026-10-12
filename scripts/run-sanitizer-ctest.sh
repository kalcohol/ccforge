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
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
report="${first_log}.xml"
rm -f -- "${report}"
report_args=()
# CTest 3.21+ and Python 3 enable retries; older prerequisites still run tests.
if command -v python3 >/dev/null &&
        "${ctest_command}" --help 2>/dev/null | grep -F -- '--output-junit' >/dev/null; then
    report_args=(--output-junit "${report}")
fi

set +e
"${ctest_command}" \
    --test-dir "${build_dir}" \
    -R "${test_regex}" \
    "${report_args[@]}" \
    --output-on-failure 2>&1 | tee "${first_log}"
first_status=${PIPESTATUS[0]}
set -e

if [[ ${first_status} -eq 0 ]]; then
    exit 0
fi

if [[ ${first_status} -ne 8 || ! -s "${report}" ]] ||
        ! python3 "${script_dir}/check-sanitizer-startup.py" "${kind}" "${report}"; then
    echo "[${kind}] first run is not exclusively verified startup mapping failures; not retrying" >&2
    exit "${first_status}"
fi

echo "[${kind}] retrying under setarch -R after recognized startup mapping failure"
"${setarch_command}" "$(uname -m)" -R \
    "${ctest_command}" \
    --test-dir "${build_dir}" \
    -R "${test_regex}" \
    --output-on-failure

#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
runner="${repo_root}/scripts/run-sanitizer-ctest.sh"
work="$(mktemp -d "${TMPDIR:-/tmp}/ccforge-sanitizer-retry.XXXXXX")"
trap 'find "${work}" -depth -delete' EXIT

cat > "${work}/ctest" <<'EOF'
#!/usr/bin/env bash
case "${FAKE_CTEST_MODE}" in
    ordinary)
        echo "a test assertion failed"
        exit 7
        ;;
    asan-error)
        echo "ERROR: AddressSanitizer: heap-use-after-free"
        exit 8
        ;;
    tsan-mapping)
        if [[ "${FAKE_RETRIED:-0}" == 1 ]]; then
            exit 0
        fi
        echo "FATAL: ThreadSanitizer: unexpected memory mapping 0x1-0x2"
        exit 9
        ;;
    asan-mapping)
        if [[ "${FAKE_RETRIED:-0}" == 1 ]]; then
            exit 0
        fi
        echo "Shadow memory range interleaves with an existing memory mapping"
        exit 10
        ;;
esac
exit 99
EOF

cat > "${work}/setarch" <<'EOF'
#!/usr/bin/env bash
touch "${FAKE_RETRY_MARKER}"
export FAKE_RETRIED=1
shift 2
exec "$@"
EOF

chmod +x "${work}/ctest" "${work}/setarch"

run_rejected_case() {
    local kind="$1"
    local mode="$2"
    local expected_status="$3"
    local marker="${work}/${mode}.retried"
    local status=0

    FAKE_CTEST_MODE="${mode}" \
    FAKE_RETRY_MARKER="${marker}" \
    CTEST="${work}/ctest" \
    SETARCH="${work}/setarch" \
        "${runner}" "${kind}" unused '.*' "${work}/${mode}.log" \
        >/dev/null 2>&1 || status=$?

    [[ ${status} -eq ${expected_status} ]]
    [[ ! -e "${marker}" ]]
}

run_mapping_case() {
    local kind="$1"
    local mode="$2"
    local marker="${work}/${mode}.retried"

    FAKE_CTEST_MODE="${mode}" \
    FAKE_RETRY_MARKER="${marker}" \
    CTEST="${work}/ctest" \
    SETARCH="${work}/setarch" \
        "${runner}" "${kind}" unused '.*' "${work}/${mode}.log" \
        >/dev/null

    [[ -e "${marker}" ]]
}

run_rejected_case tsan ordinary 7
run_rejected_case asan asan-error 8
run_mapping_case tsan tsan-mapping
run_mapping_case asan asan-mapping

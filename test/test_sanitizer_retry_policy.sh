#!/usr/bin/env bash

set -euo pipefail

if ! command -v python3 >/dev/null ||
        ! ctest --help 2>/dev/null | grep -F -- '--output-junit' >/dev/null; then
    echo 'Sanitizer retry policy tests require Python 3 and CTest 3.21+'
    exit 77
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
runner="${repo_root}/scripts/run-sanitizer-ctest.sh"
work="$(mktemp -d "${TMPDIR:-/tmp}/ccforge-sanitizer-retry.XXXXXX")"
trap 'find "${work}" -depth -delete' EXIT

cat > "${work}/ctest" <<'EOF'
#!/usr/bin/env bash
if [[ "${1:-}" == --help ]]; then
    [[ "${FAKE_CTEST_MODE}" == old-ctest ]] || echo '--output-junit'
    exit 0
fi
report=
while [[ $# -gt 0 ]]; do
    if [[ "$1" == --output-junit ]]; then report="$2"; shift; fi
    shift
done
if [[ "${FAKE_RETRIED:-0}" == 1 ]]; then
    [[ "${FAKE_CTEST_MODE}" != retry-failure ]] || exit 11
    exit 0
fi
status=8
second=
case "${FAKE_CTEST_MODE}" in
    ordinary)
        output="a test assertion failed"
        status=7
        ;;
    asan-error)
        output="ERROR: AddressSanitizer: heap-use-after-free"
        ;;
    tsan-mapping|retry-failure|missing-report|malformed|old-ctest)
        output="FATAL: ThreadSanitizer: unexpected memory mapping 0x1-0x2"
        ;;
    asan-mapping)
        output="Shadow memory range interleaves with an existing memory mapping"
        ;;
    tsan-mixed)
        output=$'FATAL: ThreadSanitizer: unexpected memory mapping 0x1-0x2\nordinary assertion failed'
        ;;
    tsan-unknown-mixed)
        output=$'FATAL: ThreadSanitizer: unexpected memory mapping 0x1-0x2\narbitrary unrelated failure'
        ;;
    tsan-two-tests)
        output="FATAL: ThreadSanitizer: unexpected memory mapping 0x1-0x2"
        second="arbitrary unrelated failure"
        ;;
    asan-mixed)
        output=$'Shadow memory range interleaves with an existing memory mapping\narbitrary unrelated failure'
        ;;
    success)
        exit 0
        ;;
    *) exit 99
        ;;
esac
printf '%s\n' "$output" "$second"
if [[ -n "$report" && "${FAKE_CTEST_MODE}" != missing-report ]]; then
    if [[ "${FAKE_CTEST_MODE}" == malformed ]]; then
        echo '<bad' > "$report"
    else
        count=1
        [[ -z "$second" ]] || count=2
        printf '<testsuite tests="%s" failures="%s">\n' "$count" "$count" > "$report"
        printf '<testcase name="mapping"><failure/><system-out>%s</system-out></testcase>\n' "$output" >> "$report"
        if [[ -n "$second" ]]; then
            printf '<testcase name="other"><failure/><system-out>%s</system-out></testcase>\n' "$second" >> "$report"
        fi
        echo '</testsuite>' >> "$report"
    fi
fi
exit "$status"
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
run_rejected_case tsan tsan-mixed 8
run_rejected_case tsan tsan-unknown-mixed 8
run_rejected_case tsan tsan-two-tests 8
run_rejected_case asan asan-mixed 8
run_rejected_case tsan missing-report 8
run_rejected_case tsan malformed 8
run_rejected_case tsan old-ctest 8
run_rejected_case tsan success 0
run_mapping_case tsan tsan-mapping
run_mapping_case asan asan-mapping

status=0
FAKE_CTEST_MODE=retry-failure FAKE_RETRY_MARKER="${work}/retry-failure.retried" \
    CTEST="${work}/ctest" SETARCH="${work}/setarch" \
    "${runner}" tsan unused '.*' "${work}/retry-failure.log" \
    >/dev/null 2>&1 || status=$?
[[ "$status" == 11 && -e "${work}/retry-failure.retried" ]]

# Exercise actual CTest report structure, including unrelated failed tests.
cat > "${work}/mapping-test" <<'EOF'
#!/usr/bin/env bash
[[ "${FAKE_RETRIED:-0}" != 1 ]] || exit 0
echo 'FATAL: ThreadSanitizer: unexpected memory mapping 0x1-0x2'
exit 1
EOF
chmod +x "${work}/mapping-test"
cat > "${work}/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.20)
project(SanitizerRetryReplay LANGUAGES NONE)
enable_testing()
add_test(NAME mapping COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/mapping-test")
if(REPLAY_MIXED)
    add_test(NAME unknown COMMAND bash -c "echo unrelated; exit 1")
endif()
EOF
for mixed in OFF ON; do
    cmake -S "${work}" -B "${work}/real-${mixed}" -DREPLAY_MIXED="${mixed}" >/dev/null
    marker="${work}/real-${mixed}.retried"
    status=0
    FAKE_RETRY_MARKER="${marker}" CTEST="$(command -v ctest)" SETARCH="${work}/setarch" \
        "${runner}" tsan "${work}/real-${mixed}" '.*' "${work}/real-${mixed}.log" \
        >/dev/null 2>&1 || status=$?
    if [[ "$mixed" == OFF ]]; then
        [[ "$status" == 0 && -e "$marker" ]]
    else
        [[ "$status" == 8 && ! -e "$marker" ]]
    fi
done

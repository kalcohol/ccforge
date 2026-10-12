#!/usr/bin/env bash

set -euo pipefail

fake_retry_prerequisites=false
real_retry_prerequisites=false
real_ctest="${CTEST:-ctest}"
if command -v python3 >/dev/null; then
    fake_retry_prerequisites=true
    if "${real_ctest}" --help 2>/dev/null | grep -F -- '--output-junit' >/dev/null; then
        real_retry_prerequisites=true
    fi
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
    echo 'retry-output-preserved'
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
    tsan-mapping|retry-failure|missing-report|malformed|old-ctest|timeout|bad-status|duplicate-output|bad-structure|stale)
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
        reason=Failed
        test_status=fail
        [[ "${FAKE_CTEST_MODE}" != timeout ]] || reason=Timeout
        [[ "${FAKE_CTEST_MODE}" != bad-status ]] || test_status=run
        count=1
        [[ -z "$second" ]] || count=2
        printf '<testsuite tests="%s" failures="%s">\n' "$count" "$count" > "$report"
        printf '<testcase name="mapping" status="%s"><failure message="%s"/><system-out>%s</system-out>' "$test_status" "$reason" "$output" >> "$report"
        if [[ "${FAKE_CTEST_MODE}" == duplicate-output ]]; then
            echo '<system-out>unrelated failure</system-out>' >> "$report"
        elif [[ "${FAKE_CTEST_MODE}" == bad-structure ]]; then
            echo '<unknown/>' >> "$report"
        fi
        echo '</testcase>' >> "$report"
        if [[ -n "$second" ]]; then
            printf '<testcase name="other" status="fail"><failure message="Failed"/><system-out>%s</system-out></testcase>\n' "$second" >> "$report"
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
        >"${work}/${mode}.retry-output.log"

    [[ -e "${marker}" ]]
    grep -Fx 'retry-output-preserved' "${work}/${mode}.retry-output.log" >/dev/null
}

run_rejected_case tsan ordinary 7
run_rejected_case asan asan-error 8
run_rejected_case tsan tsan-mixed 8
run_rejected_case tsan tsan-unknown-mixed 8
run_rejected_case tsan tsan-two-tests 8
run_rejected_case asan asan-mixed 8
run_rejected_case tsan missing-report 8
run_rejected_case tsan malformed 8
run_rejected_case tsan timeout 8
run_rejected_case tsan bad-status 8
run_rejected_case tsan duplicate-output 8
run_rejected_case tsan bad-structure 8
run_rejected_case tsan old-ctest 8
run_rejected_case tsan success 0

# The runner must remove old evidence before invoking a command without a report.
printf '<testsuite tests="1" failures="1"><testcase status="fail"><failure message="Failed"/><system-out>FATAL: ThreadSanitizer: unexpected memory mapping 0x1-0x2</system-out></testcase></testsuite>' \
    > "${work}/missing-report.log.xml"
run_rejected_case tsan missing-report 8
if [[ "$fake_retry_prerequisites" == true ]]; then
    run_mapping_case tsan tsan-mapping
    run_mapping_case asan asan-mapping
else
    run_rejected_case tsan tsan-mapping 8
    run_rejected_case asan asan-mapping 8
    echo 'Optional positive retries unavailable; fail-closed prerequisite controls passed'
fi

status=0
FAKE_CTEST_MODE=retry-failure FAKE_RETRY_MARKER="${work}/retry-failure.retried" \
    CTEST="${work}/ctest" SETARCH="${work}/setarch" \
    "${runner}" tsan unused '.*' "${work}/retry-failure.log" \
    >/dev/null 2>&1 || status=$?
if [[ "$fake_retry_prerequisites" == true ]]; then
    [[ "$status" == 11 && -e "${work}/retry-failure.retried" ]]
else
    [[ "$status" == 8 && ! -e "${work}/retry-failure.retried" ]]
fi

# Exercise actual CTest report structure, including unrelated failed tests.
generator_args=()
if command -v ninja >/dev/null; then generator_args=(-G Ninja); fi
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
    cmake -S "${work}" -B "${work}/real-${mixed}" "${generator_args[@]}" -DREPLAY_MIXED="${mixed}" >/dev/null
    marker="${work}/real-${mixed}.retried"
    status=0
    FAKE_RETRY_MARKER="${marker}" CTEST="${real_ctest}" SETARCH="${work}/setarch" \
        "${runner}" tsan "${work}/real-${mixed}" '.*' "${work}/real-${mixed}.log" \
        >/dev/null 2>&1 || status=$?
    if [[ "$mixed" == OFF && "$real_retry_prerequisites" == true ]]; then
        [[ "$status" == 0 && -e "$marker" ]]
    else
        [[ "$status" == 8 && ! -e "$marker" ]]
    fi
done

if [[ "$fake_retry_prerequisites" == true && "${FORGE_POLICY_OLD_CTEST_CONTROL:-0}" != 1 ]]; then
    cat > "${work}/old-ctest" <<'EOF'
#!/usr/bin/env bash
if [[ "${1:-}" == --help ]]; then
    echo 'CTest without JUnit output support'
    exit 0
fi
exec "${FORGE_REAL_CTEST}" "$@"
EOF
    chmod +x "${work}/old-ctest"
    FORGE_POLICY_OLD_CTEST_CONTROL=1 FORGE_REAL_CTEST="$(command -v "$real_ctest")" \
        CTEST="${work}/old-ctest" bash "${BASH_SOURCE[0]}" \
        >"${work}/old-ctest-with-python.log" 2>&1
fi

#!/usr/bin/env bash
#
# audit-source-style.sh - lightweight whitespace checks for tracked source.
#
# This intentionally does not impose a formatter or a line-length policy. It
# catches only portable, low-noise defects that should not vary across tools.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

failures=0
checked=0

log() {
    printf '[style-audit] %s\n' "$*"
}

fail() {
    printf '[style-audit] ERROR: %s\n' "$1" >&2
    failures=$((failures + 1))
}

is_source_file() {
    case "$1" in
        *.c|*.cc|*.cpp|*.cxx|*.h|*.hpp|*.cmake|*.cmake.in|*.sh|*.bash|*.ps1|\
        CMakeLists.txt|*/CMakeLists.txt|\
        backport/execution|backport/linalg|backport/mdspan|backport/memory|\
        backport/simd|backport/utility)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

check_pattern() {
    local file="$1"
    local label="$2"
    local pattern="$3"
    local matches

    if matches="$(LC_ALL=C grep -n -- "${pattern}" "${file}")"; then
        printf '%s\n' "${matches}" >&2
        fail "${file}: ${label}"
    fi
}

log "check tracked source whitespace"
while IFS= read -r -d '' file; do
    if ! is_source_file "${file}"; then
        continue
    fi

    checked=$((checked + 1))
    check_pattern "${file}" "tab character found" $'\t'
    check_pattern "${file}" "trailing whitespace found" '[[:blank:]]$'
    check_pattern "${file}" "carriage return found; use LF line endings" $'\r'

    if [[ -s "${file}" ]]; then
        final_byte="$(tail -c 1 "${file}" | od -An -t u1 | tr -d '[:space:]')"
        if [[ "${final_byte}" != "10" ]]; then
            fail "${file}: missing final newline"
        fi
    fi
done < <(git ls-files -z)

if ((failures != 0)); then
    printf '[style-audit] failed with %d issue(s)\n' "${failures}" >&2
    exit 1
fi

log "passed (${checked} tracked source files)"

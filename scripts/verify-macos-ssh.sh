#!/usr/bin/env bash
# Upload a working tree (default) or a local Git ref and verify on macOS.
# Requires an initialized 3rdparty/googletest checkout on the sending host.
# Usage: bash scripts/verify-macos-ssh.sh [host]
# Environment: FORGE_MACOS_HOST, FORGE_MACOS_REF, FORGE_MACOS_KEEP=1,
#              FORGE_MACOS_PATH_PREFIX (default /opt/homebrew/bin:/usr/local/bin).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MACOS_SSH_OPTIONS=(-o BatchMode=yes -o ConnectTimeout=15)
MACOS_HOST=''
MACOS_REMOTE=''
MACOS_LOCAL=''
MACOS_TOKEN=''
MACOS_CLEANING=0
MACOS_STATUS=0

macos_quote() {
    local value="$1"
    printf "'%s'" "${value//\'/\'\\\'\'}"
}

macos_cleanup() {
    MACOS_STATUS=$? MACOS_CLEANING=1
    trap '' PIPE INT TERM
    trap - EXIT
    if [[ -n "${MACOS_LOCAL}" ]]; then
        rm -rf -- "${MACOS_LOCAL}" || macos_cleanup_failure "$?"
    fi
    if [[ -n "${MACOS_REMOTE}" ]]; then
        if ! macos_valid_remote "${MACOS_REMOTE}" ||
                [[ ! "${MACOS_TOKEN}" =~ ^[a-f0-9]{32}$ ]]; then
            printf '[macos] refusing cleanup of invalid remote scratch path or token\n' >&2 || :
            macos_cleanup_failure 1
        elif [[ "${FORGE_MACOS_KEEP:-0}" == 1 ]]; then
            printf '[macos] kept remote directory: %s\n' "${MACOS_REMOTE}" || :
        else
            printf '[macos] cleanup: %s\n' "${MACOS_REMOTE}" || :
            ssh "${MACOS_SSH_OPTIONS[@]}" "${MACOS_HOST}" \
                "sh -s -- $(macos_quote "${MACOS_REMOTE}") $(macos_quote "${MACOS_TOKEN}")" <<'CLEANUP' || macos_cleanup_failure "$?"
directory=$1
token=$2
if [ ! -d "$directory" ] || [ -L "$directory" ] ||
        [ ! -f "$directory/.ccforge-owner" ] || [ -L "$directory/.ccforge-owner" ]; then
    printf '[macos] refusing cleanup: ownership marker missing or unsafe\n' >&2 || :
    exit 1
fi
if ! printf '%s\n' "$token" | cmp -s - "$directory/.ccforge-owner"; then
    printf '[macos] refusing cleanup: ownership token mismatch or unreadable marker\n' >&2 || :
    exit 1
fi
rm -rf -- "$directory"
CLEANUP
        fi
    fi
    exit "${MACOS_STATUS}"
}

macos_cleanup_failure() {
    if ((MACOS_STATUS == 0)); then MACOS_STATUS=$1; fi
    return 0
}

macos_signal() {
    # A second signal must not abandon cleanup or overwrite the saved failure.
    if ((MACOS_CLEANING == 0)); then exit "$1"; fi
    return 0
}

macos_valid_remote() {
    [[ "$1" =~ ^/tmp/ccforge-macos\.[a-zA-Z0-9]{8}$ ]]
}

macos_archive() {
    local root="$1" archive="$2" list="$3" path
    [[ "$(git -C "${root}" rev-parse --show-toplevel)" == "${root}" ]] || {
        printf '[macos] expected an initialized Git checkout: %s\n' "${root}" >&2
        return 1
    }
    # Materialize the Git result first so failures cannot disappear in process substitution.
    git -C "${root}" ls-files --cached --others --exclude-standard -z >"${list}.all"
    : >"${list}"
    while IFS= read -r -d '' path; do
        case "${path}" in 3rdparty/googletest|3rdparty/googletest/*) continue ;; esac
        if [[ -e "${root}/${path}" || -L "${root}/${path}" ]]; then
            printf '%s\0' "${path}" >>"${list}"
        fi
    done <"${list}.all"
    tar -C "${root}" --null --no-recursion --exclude='.git' --exclude='*/.git' \
        -czf "${archive}" --files-from "${list}"
}

macos_main() {
    MACOS_HOST="${1:-${FORGE_MACOS_HOST:-}}"
    if [[ -z "${MACOS_HOST}" || "${MACOS_HOST}" == -* || "${MACOS_HOST}" == *[[:space:]]* ]]; then
        printf 'usage: %s [host]; set FORGE_MACOS_HOST to a valid SSH host\n' "$0" >&2
        return 2
    fi
    MACOS_LOCAL=$(mktemp -d "${TMPDIR:-/tmp}/ccforge-macos.XXXXXXXX")
    trap macos_cleanup EXIT
    trap 'macos_signal 130' INT
    trap 'macos_signal 143' TERM
    MACOS_TOKEN=$(od -An -N16 -tx1 /dev/urandom | tr -d '[:space:]')
    [[ "${MACOS_TOKEN}" =~ ^[a-f0-9]{32}$ ]] || return 1
    local gtest_root="${REPO_ROOT}/3rdparty/googletest" commit gtest_commit
    if [[ -n "${FORGE_MACOS_REF:-}" ]]; then
        commit=$(git -C "${REPO_ROOT}" rev-parse --verify --end-of-options "${FORGE_MACOS_REF}^{commit}")
        printf '[macos] archive source commit: %s\n' "${commit}"
        git -C "${REPO_ROOT}" archive --format=tar.gz -o "${MACOS_LOCAL}/src.tgz" "${commit}"
        gtest_commit=$(git -C "${REPO_ROOT}" rev-parse "${commit}:3rdparty/googletest")
        git -C "${gtest_root}" archive --format=tar.gz -o "${MACOS_LOCAL}/gtest.tgz" "${gtest_commit}"
    else
        printf '[macos] archive working tree (tracked + untracked, excluding ignored files)\n'
        [[ -f "${gtest_root}/CMakeLists.txt" ]] || {
            printf '[macos] initialize 3rdparty/googletest before uploading\n' >&2
            return 1
        }
        macos_archive "${REPO_ROOT}" "${MACOS_LOCAL}/src.tgz" "${MACOS_LOCAL}/files"
        macos_archive "${gtest_root}" "${MACOS_LOCAL}/gtest.tgz" "${MACOS_LOCAL}/gtest-files"
    fi
    printf '[macos] create remote scratch directory\n'
    local candidate creation_status
    # Capture all output without trimming newlines; failed creation never owns a target.
    if ssh "${MACOS_SSH_OPTIONS[@]}" "${MACOS_HOST}" \
        "directory=\$(mktemp -d /tmp/ccforge-macos.XXXXXXXX) && printf '%s\\n' $(macos_quote "${MACOS_TOKEN}") > \"\$directory/.ccforge-owner\" && printf '%s\\n' \"\$directory\"" \
        >"${MACOS_LOCAL}/remote-path" 2>&1; then
        :
    else
        creation_status=$?
        printf '[macos] remote scratch creation failed; no cleanup target recorded\n' >&2 || :
        cat "${MACOS_LOCAL}/remote-path" >&2 || :
        return "${creation_status}"
    fi
    if ! IFS= read -r candidate <"${MACOS_LOCAL}/remote-path" ||
            ! macos_valid_remote "${candidate}" ||
            ! printf '%s\n' "${candidate}" | cmp -s - "${MACOS_LOCAL}/remote-path"; then
        printf '[macos] invalid remote scratch path\n' >&2
        return 1
    fi
    MACOS_REMOTE=${candidate}
    local archive remote_arg
    remote_arg=$(macos_quote "${MACOS_REMOTE}")
    for archive in src gtest; do
        printf '[macos] upload %s\n' "${archive}"
        ssh "${MACOS_SSH_OPTIONS[@]}" "${MACOS_HOST}" \
            "cat > ${remote_arg}/${archive}.tgz" <"${MACOS_LOCAL}/${archive}.tgz"
    done
    local path_arg
    path_arg=$(macos_quote "${FORGE_MACOS_PATH_PREFIX-/opt/homebrew/bin:/usr/local/bin}")
    ssh "${MACOS_SSH_OPTIONS[@]}" "${MACOS_HOST}" "bash -s -- ${remote_arg} ${path_arg}" <<'REMOTE'
set -euo pipefail
root="$1"
export PATH="${2}${2:+:}${PATH}"
printf '[macos] check host tools\n'
[[ "$(uname -s)" == Darwin ]]
[[ "$(uname -m)" == arm64 ]]
for tool in xcrun cmake ctest; do
    if ! tool_path=$(command -v "${tool}"); then
        case "${tool}" in
            cmake|ctest)
                printf '[macos] missing %s: install CMake (for example: brew install cmake), or set FORGE_MACOS_PATH_PREFIX to a portable CMake bin directory\n' "${tool}" >&2 ;;
            *) printf '[macos] missing required host tool: %s\n' "${tool}" >&2 ;;
        esac
        exit 127
    fi
    printf '[macos] %s: %s\n' "${tool}" "${tool_path}"
done
xcrun --find clang++
cmake --version
ctest --version
mkdir -p "${root}/src/3rdparty/googletest"
tar -xzf "${root}/src.tgz" -C "${root}/src"
tar -xzf "${root}/gtest.tgz" -C "${root}/src/3rdparty/googletest"
generator='Unix Makefiles'
if command -v ninja >/dev/null 2>&1; then generator=Ninja; fi
if [[ "${generator}" == Ninja ]]; then
    printf '[macos] ninja: %s\n' "$(command -v ninja)"
else
    printf '[macos] make: %s\n' "$(command -v make)"
    command -v make >/dev/null
fi
status=0
for std in 23 26; do
    mode_status=0
    build="${root}/build-cxx${std}"
    printf '[macos] configure C++%s\n' "${std}"
    if cmake -S "${root}/src" -B "${build}" -G "${generator}" \
        -DCMAKE_CXX_COMPILER="$(xcrun --find clang++)" \
        -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD="${std}" \
        -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON; then
        printf '[macos] build C++%s\n' "${std}"
        if cmake --build "${build}"; then
            printf '[macos] test C++%s\n' "${std}"
            ctest --test-dir "${build}" --no-tests=error --output-on-failure || mode_status=$?
        else
            mode_status=$?
        fi
    else
        mode_status=$?
    fi
    printf '[macos] C++%s summary: exit %s\n' "${std}" "${mode_status}"
    if ((status == 0)); then status=${mode_status}; fi
done
if ((status != 0)); then exit "${status}"; fi
printf '[macos] verification passed\n'
REMOTE
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    macos_main "$@"
fi

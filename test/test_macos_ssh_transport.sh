#!/usr/bin/env bash
# Exercise transport orchestration without contacting a host or running CMake.
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "${REPO_ROOT}/scripts/verify-macos-ssh.sh"
scratch=$(mktemp -d "${TMPDIR:-/tmp}/ccforge-macos-transport.XXXXXXXX")
trap 'rm -rf -- "${scratch}"' EXIT
export scratch

[[ "$(macos_quote "a'b c")" == "'a'\''b c'" ]]
for host in '' '-oProxyCommand=unexpected' $'host\nnext'; do
    actual=0
    FORGE_MACOS_HOST='' bash -euo pipefail -c 'source "$1"; macos_main "$2"' \
        bash "${REPO_ROOT}/scripts/verify-macos-ssh.sh" "${host}" \
        >"${scratch}/output" 2>&1 || actual=$?
    [[ "${actual}" == 2 ]]
done

# Source mocks after production definitions in an independent errexit-enabled Bash.
cat >"${scratch}/mocks.sh" <<'MOCKS'
ssh() {
    local command="${!#}"
    printf '%s\n' "${command}" >>"${scratch}/calls"
    case "${command}" in
        'directory=$(mktemp '*)
            printf '%s\n' "${command}" >"${scratch}/creation-command"
            printf '%s' "${mock_remote}"
            if [[ "${mock_path_newline}" == 1 ]]; then printf '\n'; fi
            if [[ "${mock_extra_output}" == NUL ]]; then printf '\0';
            else printf '%s' "${mock_extra_output}"; fi
            return "${mock_scratch_status}" ;;
        'cat > '*) cat >/dev/null; return "${mock_upload_status}" ;;
        'bash -s '*)
            cat >"${scratch}/remote.sh"
            if [[ -n "${mock_signal}" ]]; then kill -s "${mock_signal}" "$$"; fi
            if [[ "${mock_output_failure}" == 1 ]]; then
                # Open the writer, then wait until the only reader has exited.
                # Leave SIGPIPE at its default until production cleanup ignores it.
                trap - PIPE
                mkfifo "${scratch}/closed-pipe"
                ( : <"${scratch}/closed-pipe" ) &
                local reader=$!
                exec >"${scratch}/closed-pipe" 2>&1
                wait "${reader}"
                unlink "${scratch}/closed-pipe"
            fi
            return "${mock_build_status}" ;;
        'sh -s -- '*)
            cat >"${scratch}/cleanup.sh"
            if [[ "${mock_cleanup_signal}" == 1 ]]; then
                kill -s INT "$$"
                kill -s TERM "$$"
            elif [[ "${mock_cleanup_signal}" == group ]]; then
                # An external process must inherit the ignored dispositions.
                bash -c '
                    kill -s INT -- "-$1"
                    kill -s TERM -- "-$1"
                    printf "subprocess-completed\n" >>"$scratch/cleanup-events"
                ' bash "$$"
            fi
            printf 'cleanup-completed\n' >>"${scratch}/cleanup-events"
            if [[ "${mock_real_cleanup}" == 1 ]]; then
                bash -c "${command}" <"${scratch}/cleanup.sh"
            else
                return "${mock_cleanup_status}"
            fi ;;
        *) return 99 ;;
    esac
}
macos_archive() { : >"$2"; return "${mock_archive_status}"; }
git() {
    printf '%s\n' "$*" >>"${scratch}/git-calls"
    if [[ "$3" == archive ]]; then
        local previous='' argument
        for argument in "$@"; do
            if [[ "${previous}" == -o ]]; then : >"${argument}"; fi
            previous="${argument}"
        done
        return "${mock_archive_status}"
    else
        printf '0123456789012345678901234567890123456789\n'
    fi
}
MOCKS

run_case() {
    local expected="$1" cleanup="$2" actual=0
    : >"${scratch}/calls"
    : >"${scratch}/cleanup-events"
    if [[ "${mock_cleanup_signal}" == group ]]; then
        # Job control gives this Bash and its subprocesses a private process group.
        set -m
        bash -euo pipefail -c 'source "$1"; source "$2"; macos_main test-host' \
            bash "${REPO_ROOT}/scripts/verify-macos-ssh.sh" "${scratch}/mocks.sh" \
            >"${scratch}/output" 2>&1 &
        local child=$!
        wait "${child}" || actual=$?
        set +m
    else
        bash -euo pipefail -c 'source "$1"; source "$2"; macos_main test-host' \
        bash "${REPO_ROOT}/scripts/verify-macos-ssh.sh" "${scratch}/mocks.sh" \
        >"${scratch}/output" 2>&1 || actual=$?
    fi
    [[ "${actual}" == "${expected}" ]] || {
        printf 'expected status %s, got %s\n' "${expected}" "${actual}" >&2
        cat "${scratch}/output" >&2
        exit 1
    }
    if [[ "${cleanup}" == yes ]]; then
        grep -q '^sh -s -- ' "${scratch}/calls"
        grep -qx 'cleanup-completed' "${scratch}/cleanup-events"
    else
        ! grep -q '^sh -s -- ' "${scratch}/calls"
    fi
}
export mock_build_status=0 mock_cleanup_status=0 mock_archive_status=0
export mock_scratch_status=0 mock_upload_status=0 mock_signal=''
export mock_cleanup_signal=0 mock_output_failure=0 mock_real_cleanup=0 mock_extra_output=''
export mock_path_newline=1
export mock_remote=/tmp/ccforge-macos.ABC12345
export FORGE_MACOS_REF='' FORGE_MACOS_KEEP=0
unset FORGE_MACOS_PATH_PREFIX
run_case 0 yes
grep -Fq 'for std in 23 26' "${scratch}/remote.sh"
grep -Fq -- '-DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON' "${scratch}/remote.sh"
grep -Fq 'ctest --test-dir' "${scratch}/remote.sh"
grep -Fq "'/opt/homebrew/bin:/usr/local/bin'" "${scratch}/calls"
bash -n "${scratch}/remote.sh"
export FORGE_MACOS_PATH_PREFIX="/custom tools/a'b:/another/bin"
run_case 0 yes
grep -Fq "$(macos_quote "${FORGE_MACOS_PATH_PREFIX}")" "${scratch}/calls"
mock_build_status=42
run_case 42 yes
mock_cleanup_status=1
run_case 42 yes
mock_build_status=0
run_case 1 yes
mock_cleanup_status=35
run_case 35 yes
mock_cleanup_status=1
FORGE_MACOS_KEEP=1
run_case 0 no
grep -q '\[macos\] kept remote directory:' "${scratch}/output"
FORGE_MACOS_KEEP=0 mock_cleanup_status=0
FORGE_MACOS_REF='topic with spaces'
run_case 0 yes
grep -Fq -- '--verify --end-of-options topic with spaces^{commit}' "${scratch}/git-calls"
grep -Fq ':3rdparty/googletest' "${scratch}/git-calls"
mock_archive_status=31
run_case 31 no
[[ ! -s "${scratch}/calls" ]]
FORGE_MACOS_REF=''
run_case 31 no
[[ ! -s "${scratch}/calls" ]]
mock_archive_status=0 mock_scratch_status=32
run_case 32 no
! grep -q '^cat > ' "${scratch}/calls"
mock_remote='/tmp/unrelated'
run_case 32 no
! grep -q '^cat > ' "${scratch}/calls"
mock_scratch_status=0
run_case 1 no
mock_remote=$'/tmp/ccforge-macos.ABC12345\n'
run_case 1 no
mock_remote=$'/tmp/ccforge-macos.ABC12345\nextra'
run_case 1 no
mock_remote=/tmp/ccforge-macos.ABC12345 mock_extra_output='warning'
run_case 1 no
mock_extra_output=NUL
run_case 1 no
mock_extra_output=''
mock_path_newline=0
run_case 1 no
mock_path_newline=1
mock_remote=/tmp/ccforge-macos.ABC12345 mock_upload_status=33
run_case 33 yes
! grep -q '^bash -s ' "${scratch}/calls"
[[ "$(grep -c '^cat > ' "${scratch}/calls")" == 1 ]]
mock_upload_status=0 mock_signal=INT
run_case 130 yes
mock_signal=TERM mock_cleanup_status=1
run_case 143 yes
mock_signal='' mock_cleanup_status=0

# Signals arriving inside cleanup cannot replace the first failure or stop cleanup.
mock_cleanup_signal=1 mock_build_status=42
run_case 42 yes
grep -q '^cleanup-completed$' "${scratch}/cleanup-events"
mock_signal=INT mock_build_status=0
run_case 130 yes
mock_signal=TERM
run_case 143 yes
mock_signal='' mock_cleanup_signal=0

# Deliver both signals to the whole process group during an external cleanup step.
mock_cleanup_signal=group mock_build_status=42
run_case 42 yes
grep -qx 'subprocess-completed' "${scratch}/cleanup-events"
mock_signal=INT mock_build_status=0
run_case 130 yes
grep -qx 'subprocess-completed' "${scratch}/cleanup-events"
mock_signal=TERM
run_case 143 yes
grep -qx 'subprocess-completed' "${scratch}/cleanup-events"
mock_signal='' mock_cleanup_signal=0

# Diagnostic writes failing under errexit must not prevent cleanup or change status.
mock_output_failure=1 mock_build_status=42
run_case 42 yes
mock_build_status=0
run_case 0 yes
mock_output_failure=0

# Test the cleanup guard directly on success, failure and signal exit paths.
for target in '' / /tmp /tmp/ccforge-macos. /tmp/ccforge-macos.a/../b \
        relative/ccforge-macos.abc $'/tmp/ccforge-macos.abc\nextra'; do
    for outcome in 0 42 INT TERM; do
        : >"${scratch}/calls"
        actual=0
        bash -euo pipefail -c '
            source "$1"; source "$2"
            MACOS_REMOTE="$3"
            MACOS_TOKEN=0123456789abcdef0123456789abcdef
            trap macos_cleanup EXIT
            trap "macos_signal 130" INT
            trap "macos_signal 143" TERM
            case "$4" in INT|TERM) kill -s "$4" "$$" ;; *) exit "$4" ;; esac
        ' bash "${REPO_ROOT}/scripts/verify-macos-ssh.sh" "${scratch}/mocks.sh" \
            "${target}" "${outcome}" >"${scratch}/output" 2>&1 || actual=$?
        expected="${outcome}"
        case "${outcome}" in INT) expected=130 ;; TERM) expected=143 ;;
            0) if [[ -n "${target}" ]]; then expected=1; fi ;; esac
        [[ "${actual}" == "${expected}" ]]
        [[ ! -s "${scratch}/calls" ]]
    done
done

# Execute the actual deletion program against real local marker fixtures, never SSH.
# Retain fixtures under scratch on refusal so the EXIT trap can remove them later.
for marker in missing different matching; do
    fixture=$(mktemp -d /tmp/ccforge-macos.XXXXXXXX)
    if [[ "${marker}" != missing ]]; then
        printf '%s\n' different-token >"${fixture}/.ccforge-owner"
    fi
    actual=0
    mock_real_cleanup=1 bash -euo pipefail -c '
        source "$1"; source "$2"
        MACOS_REMOTE="$3"
        MACOS_TOKEN=0123456789abcdef0123456789abcdef
        if [[ "$4" == matching ]]; then
            printf "%s\n" "$MACOS_TOKEN" >"$MACOS_REMOTE/.ccforge-owner"
        fi
        trap macos_cleanup EXIT
        trap "macos_signal 130" INT
        trap "macos_signal 143" TERM
        exit 0
    ' bash "${REPO_ROOT}/scripts/verify-macos-ssh.sh" "${scratch}/mocks.sh" \
        "${fixture}" "${marker}" >"${scratch}/output" 2>&1 || actual=$?
    if [[ "${marker}" == matching ]]; then
        [[ "${actual}" == 0 && ! -e "${fixture}" ]]
    else
        [[ "${actual}" == 1 && -d "${fixture}" ]]
        grep -q 'refusing cleanup: ownership' "${scratch}/output"
        mv "${fixture}" "${scratch}/${marker}"
    fi
done

# The creation command itself must write the same token used by the deletion guard.
bash -c "$(cat "${scratch}/creation-command")" >"${scratch}/created-path"
IFS= read -r fixture <"${scratch}/created-path"
macos_valid_remote "${fixture}"
owner=$(cat "${fixture}/.ccforge-owner")
[[ "${owner}" =~ ^[a-f0-9]{32}$ ]]
sh "${scratch}/cleanup.sh" "${fixture}" "${owner}"
[[ ! -e "${fixture}" ]]

# Execute the captured remote program with tool stubs and a minimal SSH-like PATH.
# No compiler, build system or SSH program is invoked.
mkdir -p "${scratch}/tools"
cat >"${scratch}/tools/tool" <<'TOOLS'
#!/bin/bash
stage=''
case "${0##*/}:$*" in
    cmake:-S*) stage=configure ;;
    cmake:--build*) stage=build ;;
    ctest:--test-dir*) stage=test ;;
esac
if [[ -n "${stage}" && "${stage}" == "${mock_fail_stage:-}" ]]; then
    case "$*" in
        *build-cxx23*) exit "${mock_fail23:-0}" ;;
        *build-cxx26*) exit "${mock_fail26:-0}" ;;
    esac
fi
case "${0##*/}" in
    uname) case "$1" in -s) echo Darwin ;; -m) echo arm64 ;; esac ;;
    xcrun) echo /mock/clang++ ;;
    cmake|ctest|ninja|tar) exit 0 ;;
    *) exit 99 ;;
esac
TOOLS
chmod +x "${scratch}/tools/tool"
for tool in uname xcrun cmake ctest ninja tar; do
    ln -s tool "${scratch}/tools/${tool}"
done
PATH=/usr/bin:/bin /bin/bash "${scratch}/remote.sh" "${scratch}/remote-root" \
    "${scratch}/tools" >"${scratch}/remote-output" 2>&1
for tool in xcrun cmake ctest ninja; do
    grep -qxF "[macos] ${tool}: ${scratch}/tools/${tool}" "${scratch}/remote-output"
done
grep -q '\[macos\] verification passed' "${scratch}/remote-output"

# Each mode reports its own result; failures never suppress the other mode.
for mock_fail_stage in configure build test; do
    export mock_fail_stage
    for failed_mode in 23 26 both; do
        export mock_fail23=0 mock_fail26=0
        case "${failed_mode}" in
            23) mock_fail23=41 ;;
            26) mock_fail26=43 ;;
            both) mock_fail23=41 mock_fail26=43 ;;
        esac
        expected=${mock_fail23}
        if [[ "${expected}" == 0 ]]; then expected=${mock_fail26}; fi
        actual=0
        PATH=/usr/bin:/bin /bin/bash "${scratch}/remote.sh" "${scratch}/remote-root" \
            "${scratch}/tools" >"${scratch}/remote-output" 2>&1 || actual=$?
        [[ "${actual}" == "${expected}" ]]
        grep -qxF "[macos] C++23 summary: exit ${mock_fail23}" "${scratch}/remote-output"
        grep -qxF "[macos] C++26 summary: exit ${mock_fail26}" "${scratch}/remote-output"
        ! grep -q 'verification passed' "${scratch}/remote-output"
        for std in 23 26; do
            if [[ "${failed_mode}" == "${std}" || "${failed_mode}" == both ]]; then
                if [[ "${mock_fail_stage}" == configure ]]; then
                    ! grep -q "\\[macos\\] build C++${std}" "${scratch}/remote-output"
                fi
                if [[ "${mock_fail_stage}" != test ]]; then
                    ! grep -q "\\[macos\\] test C++${std}" "${scratch}/remote-output"
                fi
            fi
        done
    done
done
unset mock_fail_stage mock_fail23 mock_fail26

# Missing CMake components report installation or portable-bin guidance, before build.
for missing in cmake ctest; do
    mkdir -p "${scratch}/without-${missing}"
    for tool in uname xcrun cmake ctest; do
        if [[ "${tool}" != "${missing}" ]]; then
            ln -s "${scratch}/tools/tool" "${scratch}/without-${missing}/${tool}"
        fi
    done
    actual=0
    PATH='' /bin/bash "${scratch}/remote.sh" "${scratch}/remote-root" \
        "${scratch}/without-${missing}" >"${scratch}/remote-output" 2>&1 || actual=$?
    [[ "${actual}" == 127 ]]
    grep -Fq "missing ${missing}: install CMake" "${scratch}/remote-output"
    grep -Fq 'brew install cmake' "${scratch}/remote-output"
    grep -Fq 'FORGE_MACOS_PATH_PREFIX' "${scratch}/remote-output"
done

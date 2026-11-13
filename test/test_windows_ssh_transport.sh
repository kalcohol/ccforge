#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "${REPO_ROOT}/scripts/verify-windows-msvc-ssh.sh"

if [[ "$(forge_ps_literal "a'b")" != "'a''b'" ]]; then
    printf 'PowerShell single-quoted literal escaping failed\n' >&2
    exit 1
fi

expect_ps_literal() {
    if [[ "$(forge_ps_literal "$1")" != "$2" ]]; then
        printf 'PowerShell literal changed a boundary payload\n' >&2
        exit 1
    fi
}
expect_ps_literal '' "''"
expect_ps_literal "'" "''''"
expect_ps_literal "a''b" "'a''''b'"
expect_ps_literal $'first\nsecond\n\n' $'\'first\nsecond\n\n\''
expect_ps_literal "C:\\path\\" "'C:\\path\\'"

forge_validate_ssh_host 'user@example-host'
for invalid_host in '-oProxyCommand=unexpected' $'example-host\nnext'; do
    if forge_validate_ssh_host "${invalid_host}" >/dev/null 2>&1; then
        printf 'unsafe SSH host was accepted: %q\n' "${invalid_host}" >&2
        exit 1
    fi
done

if (
    FORGE_WINDOWS_CLEANUP_HOST='test-host'
    FORGE_WINDOWS_CLEANUP_KEEP=0
    FORGE_WINDOWS_CLEANUP_LOCAL_TEMP=''
    FORGE_WINDOWS_CLEANUP_REMOTE_SOURCE='C:/cleanup-probe'
    FORGE_WINDOWS_CLEANUP_REMOTE_UPLOAD=''
    forge_remote_ps() { return 1; }
    trap forge_windows_cleanup EXIT
) >/dev/null 2>&1; then
    printf 'successful verification ignored a cleanup failure\n' >&2
    exit 1
fi

payload=$'space %PATH% ^ ` $() " single\' trailing\\\nsecond line'
encoded="$(forge_encode_powershell "${payload}")"
if base64 --decode </dev/null >/dev/null 2>&1; then
    decoded="$(printf '%s' "${encoded}" | base64 --decode |
        iconv -f UTF-16LE -t UTF-8)"
else
    decoded="$(printf '%s' "${encoded}" | base64 -D |
        iconv -f UTF-16LE -t UTF-8)"
fi
if [[ "${decoded}" != "${payload}" ]]; then
    printf 'PowerShell encoded-command transport changed its payload\n' >&2
    exit 1
fi

params="$(
    FORGE_WINDOWS_VC_VARS=$'C:\\Program Files\\VC %PATH% \'quoted\'' \
    FORGE_WINDOWS_CTEST_REGEX=$'forge value|stopped $() ` ^' \
        forge_common_parameter_block 18
)"
for expected in \
        "\$params.Vcvars = 'C:\\Program Files\\VC %PATH% ''quoted'''" \
        "\$params.CTestRegex = 'forge value|stopped \$() \` ^'"; do
    if ! grep -Fqx "${expected}" <<<"${params}"; then
        printf 'PowerShell parameter block lost an exact value: %s\n' \
            "${expected}" >&2
        exit 1
    fi
done

scratch="$(mktemp -d "${TMPDIR:-/tmp}/ccforge-windows-archive-test.XXXXXXXX")"
trap 'rm -rf -- "${scratch}"' EXIT
# Exercise archive filtering even when the lane has no Git executable.
(
    REPO_ROOT="${scratch}/fixture"
    mkdir -p "${REPO_ROOT}/3rdparty/googletest"
    : >"${REPO_ROOT}/CMakeLists.txt"
    : >"${REPO_ROOT}/3rdparty/googletest/CMakeLists.txt"
    git() {
        printf '%s\0' CMakeLists.txt 3rdparty/googletest \
            3rdparty/googletest/CMakeLists.txt missing-file
    }
    forge_create_local_source_archive "${scratch}/mock.tgz" "${scratch}/mock-files"
    tar -tzf "${scratch}/mock.tgz" >"${scratch}/mock-contents"
    [[ "$(cat "${scratch}/mock-contents")" == CMakeLists.txt ]]
)

# Only the Git integration checks below require a real source checkout.
if ! command -v git >/dev/null 2>&1; then
    printf 'git unavailable; real Git integration checks skipped\n'
    if [[ -e "${REPO_ROOT}/.git" || -L "${REPO_ROOT}/.git" ]]; then exit 0; fi
    exit 77
fi
git_probe=$(LC_ALL=C git -C "${REPO_ROOT}" rev-parse --is-inside-work-tree 2>&1) || {
    # Missing metadata is expected in source archives; other Git errors are real.
    if [[ ! -e "${REPO_ROOT}/.git" && ! -L "${REPO_ROOT}/.git" &&
            "${git_probe}" == 'fatal: not a git repository ('* ]]; then
        printf 'no git work tree; windows_ssh_transport skipped\n'
        exit 77
    fi
    printf '%s\n' "${git_probe}" >&2
    exit 1
}
if [[ "${git_probe}" != true ]]; then
    printf 'no git work tree; windows_ssh_transport skipped\n'
    exit 77
fi
git_root=$(git -C "${REPO_ROOT}" rev-parse --show-toplevel)
if [[ "${git_root}" != "${REPO_ROOT}" ]]; then
    printf 'source directory is not a git checkout root; windows_ssh_transport skipped\n'
    exit 77
fi
gtest_commit="$(git -C "${REPO_ROOT}" ls-files --stage -- \
    3rdparty/googletest | awk '{print $2}')"
if [[ ! "${gtest_commit}" =~ ^[0-9a-f]{40}$ ]] ||
        ! grep -Fq "\$GoogletestCommit = \"${gtest_commit}\"" \
            "${REPO_ROOT}/scripts/verify-windows-msvc.ps1"; then
    printf 'Windows GoogleTest fallback drifted from the gitlink commit\n' >&2
    exit 1
fi
forge_create_local_source_archive "${scratch}/source.tgz" "${scratch}/files"
tar -tzf "${scratch}/source.tgz" >"${scratch}/contents"

grep -qxF 'CMakeLists.txt' "${scratch}/contents"
if grep -Eq '^3rdparty/googletest(/|$)' "${scratch}/contents"; then
    printf 'local-source archive recursively included the submodule worktree\n' >&2
    exit 1
fi
if grep -Eq '(^|/)(\.git|build|\.cache|\.codex|\.qoder)(/|$)' \
        "${scratch}/contents"; then
    printf 'local-source archive included ignored or repository metadata\n' >&2
    exit 1
fi

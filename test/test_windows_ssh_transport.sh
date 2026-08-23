#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "${REPO_ROOT}/scripts/verify-windows-msvc-ssh.sh"

if [[ "$(forge_ps_literal "a'b")" != "'a''b'" ]]; then
    printf 'PowerShell single-quoted literal escaping failed\n' >&2
    exit 1
fi

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
if ! command -v git >/dev/null 2>&1; then
    printf 'git unavailable; archive-index check skipped\n'
    exit 0
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

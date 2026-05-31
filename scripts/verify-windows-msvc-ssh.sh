#!/usr/bin/env bash
#
# Thin SSH wrapper for scripts/verify-windows-msvc.ps1.
#
# Usage:
#   scripts/verify-windows-msvc-ssh.sh <host> [vs-version]
#
# Environment overrides:
#   FORGE_WINDOWS_HOST        default host when no first arg is given
#   FORGE_WINDOWS_VS_VERSION  default VS layout/version (18)
#   FORGE_WINDOWS_VS_ROOT     Visual Studio root on the remote host, if non-standard
#   FORGE_WINDOWS_VC_VARS     full remote path to vcvars64.bat, if non-standard
#   FORGE_WINDOWS_REPO        repo cloned on the Windows host
#   FORGE_WINDOWS_REF         ref checked out before verification (master)
#   FORGE_WINDOWS_USE_LOCAL_SOURCE
#                             set to 1 to archive this worktree and verify it
#                             from a remote temp directory instead of cloning
#   FORGE_WINDOWS_CTEST_REGEX override the PowerShell smoke CTest regex
#   FORGE_WINDOWS_KEEP        set to 1 to keep the remote temp clone on success
#   FORGE_WINDOWS_SKIP_GATE_CHECKS
#                             set to 1 to skip configure-only gate checks
#   FORGE_WINDOWS_SKIP_INSTALL_PACKAGE_CHECK
#                             set to 1 to skip install package consumer check

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

host="${1:-${FORGE_WINDOWS_HOST:-}}"
if [[ -z "${host}" ]]; then
    printf 'usage: %s <windows-ssh-host> [vs-version]\n' "$0" >&2
    printf 'or set FORGE_WINDOWS_HOST in the environment\n' >&2
    exit 2
fi

vs_version="${2:-${FORGE_WINDOWS_VS_VERSION:-18}}"
repo="${FORGE_WINDOWS_REPO:-https://github.com/kalcohol/ccforge.git}"
ref="${FORGE_WINDOWS_REF:-master}"
use_local_source="${FORGE_WINDOWS_USE_LOCAL_SOURCE:-0}"

remote_temp="$(
    ssh "${host}" powershell -NoProfile -NonInteractive -Command '[IO.Path]::GetTempPath()' \
        | tr -d '\r'
)"
remote_temp="${remote_temp%$'\n'}"
remote_temp="${remote_temp//\\//}"
remote_temp="${remote_temp%/}"
remote_script="${remote_temp}/ccforge-verify-windows-msvc-${RANDOM}.ps1"

win_quote() {
    local value="$1"
    value="${value//\"/\\\"}"
    printf '"%s"' "${value}"
}

remote_source=""
local_archive=""
if [[ "${use_local_source}" == "1" ]]; then
    remote_source="${remote_temp}/ccforge-local-source-${RANDOM}"
    local_archive="$(mktemp -t ccforge-windows-source.XXXXXX.tgz)"
    tar \
        --exclude='./.git' \
        --exclude='./build' \
        --exclude='./.cache' \
        --exclude='./.codex' \
        -czf "${local_archive}" .
    ssh "${host}" \
        "powershell -NoProfile -NonInteractive -Command \"New-Item -ItemType Directory -Force -Path $(win_quote "${remote_source}") | Out-Null\""
    scp "${local_archive}" "${host}:${remote_source}/src.tgz" >/dev/null
    ssh "${host}" \
        "powershell -NoProfile -NonInteractive -Command \"tar -xzf $(win_quote "${remote_source}/src.tgz") -C $(win_quote "${remote_source}")\""
    remote_script="${remote_source}/scripts/verify-windows-msvc.ps1"
else
    scp "${REPO_ROOT}/scripts/verify-windows-msvc.ps1" "${host}:${remote_script}" >/dev/null
fi

remote_command="powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $(win_quote "${remote_script}")"
remote_command+=" -VsVersion $(win_quote "${vs_version}")"
if [[ "${use_local_source}" == "1" ]]; then
    remote_command+=" -SourcePath $(win_quote "${remote_source}")"
else
    remote_command+=" -Repo $(win_quote "${repo}")"
    remote_command+=" -Ref $(win_quote "${ref}")"
fi

if [[ -n "${FORGE_WINDOWS_VS_ROOT:-}" ]]; then
    remote_command+=" -VisualStudioRoot $(win_quote "${FORGE_WINDOWS_VS_ROOT}")"
fi
if [[ -n "${FORGE_WINDOWS_VC_VARS:-}" ]]; then
    remote_command+=" -Vcvars $(win_quote "${FORGE_WINDOWS_VC_VARS}")"
fi
if [[ -n "${FORGE_WINDOWS_CTEST_REGEX:-}" ]]; then
    remote_command+=" -CTestRegex $(win_quote "${FORGE_WINDOWS_CTEST_REGEX}")"
fi

if [[ "${FORGE_WINDOWS_KEEP:-0}" == "1" ]]; then
    remote_command+=" -Keep"
fi
if [[ "${FORGE_WINDOWS_SKIP_GATE_CHECKS:-0}" == "1" ]]; then
    remote_command+=" -SkipGateChecks"
fi
if [[ "${FORGE_WINDOWS_SKIP_INSTALL_PACKAGE_CHECK:-0}" == "1" ]]; then
    remote_command+=" -SkipInstallPackageCheck"
fi

cleanup() {
    if [[ -n "${local_archive}" ]]; then
        rm -f "${local_archive}"
    fi
    if [[ "${use_local_source}" == "1" ]]; then
        if [[ "${FORGE_WINDOWS_KEEP:-0}" != "1" && -n "${remote_source}" ]]; then
            ssh "${host}" \
                "powershell -NoProfile -NonInteractive -Command \"Remove-Item -Recurse -Force $(win_quote "${remote_source}")\"" \
                >/dev/null || true
        fi
    else
        ssh "${host}" \
            "powershell -NoProfile -NonInteractive -Command \"Remove-Item -Force $(win_quote "${remote_script}")\"" \
            >/dev/null || true
    fi
}
trap cleanup EXIT

ssh "${host}" "${remote_command}"

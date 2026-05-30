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
#   FORGE_WINDOWS_KEEP        set to 1 to keep the remote temp clone on success
#   FORGE_WINDOWS_SKIP_GATE_CHECKS
#                             set to 1 to skip configure-only gate checks

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

remote_temp="$(
    ssh "${host}" powershell -NoProfile -NonInteractive -Command '[IO.Path]::GetTempPath()' \
        | tr -d '\r'
)"
remote_temp="${remote_temp%$'\n'}"
remote_temp="${remote_temp//\\//}"
remote_temp="${remote_temp%/}"
remote_script="${remote_temp}/ccforge-verify-windows-msvc-${RANDOM}.ps1"

scp "${REPO_ROOT}/scripts/verify-windows-msvc.ps1" "${host}:${remote_script}" >/dev/null

win_quote() {
    local value="$1"
    value="${value//\"/\\\"}"
    printf '"%s"' "${value}"
}

remote_command="powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $(win_quote "${remote_script}")"
remote_command+=" -VsVersion $(win_quote "${vs_version}")"
remote_command+=" -Repo $(win_quote "${repo}")"
remote_command+=" -Ref $(win_quote "${ref}")"

if [[ -n "${FORGE_WINDOWS_VS_ROOT:-}" ]]; then
    remote_command+=" -VisualStudioRoot $(win_quote "${FORGE_WINDOWS_VS_ROOT}")"
fi
if [[ -n "${FORGE_WINDOWS_VC_VARS:-}" ]]; then
    remote_command+=" -Vcvars $(win_quote "${FORGE_WINDOWS_VC_VARS}")"
fi

if [[ "${FORGE_WINDOWS_KEEP:-0}" == "1" ]]; then
    remote_command+=" -Keep"
fi
if [[ "${FORGE_WINDOWS_SKIP_GATE_CHECKS:-0}" == "1" ]]; then
    remote_command+=" -SkipGateChecks"
fi

ssh "${host}" "${remote_command}"
ssh "${host}" "powershell -NoProfile -NonInteractive -Command \"Remove-Item -Force $(win_quote "${remote_script}")\"" >/dev/null || true

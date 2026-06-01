#!/usr/bin/env bash
#
# Run one or more Windows/MSVC smoke lanes through the SSH wrapper.
#
# This wrapper intentionally contains no hostnames or local installation paths.
# Set FORGE_WINDOWS_HOST and, when needed, FORGE_WINDOWS_VC_VARS or
# FORGE_WINDOWS_VS_ROOT in the caller's environment.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

host="${1:-${FORGE_WINDOWS_HOST:-}}"
if [[ -z "${host}" ]]; then
    printf 'usage: %s <windows-ssh-host>\n' "$0" >&2
    printf 'or set FORGE_WINDOWS_HOST in the environment\n' >&2
    exit 2
fi

versions="${FORGE_WINDOWS_VS_VERSIONS:-${FORGE_WINDOWS_VS_VERSION:-18}}"

printf '[msvc-matrix] versions=%s\n' "${versions}"
for version in ${versions}; do
    printf '[msvc-matrix] start VS version %s\n' "${version}"
    FORGE_WINDOWS_USE_LOCAL_SOURCE="${FORGE_WINDOWS_USE_LOCAL_SOURCE:-1}" \
        scripts/verify-windows-msvc-ssh.sh "${host}" "${version}"
    printf '[msvc-matrix] ok VS version %s\n' "${version}"
done

printf '[msvc-matrix] all requested Windows/MSVC lanes passed\n'

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
windows_wrapper="${FORGE_WINDOWS_WRAPPER:-${REPO_ROOT}/scripts/verify-windows-msvc-ssh.sh}"
read -r -a version_list <<<"${versions}"

if [[ "${#version_list[@]}" -eq 0 ]]; then
    printf '[msvc-matrix] ERROR: no Visual Studio versions requested\n' >&2
    exit 2
fi

printf '[msvc-matrix] versions=%s\n' "${versions}"
for version in "${version_list[@]}"; do
    if [[ ! "${version}" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
        printf '[msvc-matrix] ERROR: invalid Visual Studio version: %s\n' \
            "${version}" >&2
        exit 2
    fi
done

for version in "${version_list[@]}"; do
    printf '[msvc-matrix] start VS version %s\n' "${version}"
    FORGE_WINDOWS_USE_LOCAL_SOURCE="${FORGE_WINDOWS_USE_LOCAL_SOURCE:-1}" \
        "${windows_wrapper}" "${host}" "${version}"
    printf '[msvc-matrix] ok VS version %s\n' "${version}"
done

printf '[msvc-matrix] all requested Windows/MSVC lanes passed\n'

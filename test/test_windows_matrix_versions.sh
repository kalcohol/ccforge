#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
scratch="$(mktemp -d -t ccforge-windows-matrix-test.XXXXXX)"
trap 'rm -rf -- "${scratch}"' EXIT

fake_wrapper="${scratch}/wrapper"
calls="${scratch}/calls"
cat >"${fake_wrapper}" <<'SH'
#!/usr/bin/env bash
printf '%s\n' "$2" >>"${FORGE_WINDOWS_MATRIX_CALLS}"
SH
chmod +x "${fake_wrapper}"

FORGE_WINDOWS_WRAPPER="${fake_wrapper}" \
FORGE_WINDOWS_MATRIX_CALLS="${calls}" \
FORGE_WINDOWS_VS_VERSIONS='18 17.9' \
    bash "${REPO_ROOT}/scripts/verify-windows-msvc-matrix.sh" test-host >/dev/null

printf '18\n17.9\n' >"${scratch}/expected"
cmp "${scratch}/expected" "${calls}"

if FORGE_WINDOWS_WRAPPER="${fake_wrapper}" \
        FORGE_WINDOWS_MATRIX_CALLS="${calls}" \
        FORGE_WINDOWS_VS_VERSIONS='18 *' \
        bash "${REPO_ROOT}/scripts/verify-windows-msvc-matrix.sh" test-host \
        >/dev/null 2>&1; then
    printf 'invalid Visual Studio version token was accepted\n' >&2
    exit 1
fi

cmp "${scratch}/expected" "${calls}"

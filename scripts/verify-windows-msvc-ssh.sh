#!/usr/bin/env bash
#
# SSH transport for scripts/verify-windows-msvc.ps1.
#
# Usage:
#   scripts/verify-windows-msvc-ssh.sh <host> [vs-version]
#
# Environment overrides:
#   FORGE_WINDOWS_HOST, FORGE_WINDOWS_VS_VERSION, FORGE_WINDOWS_VS_ROOT,
#   FORGE_WINDOWS_VC_VARS, FORGE_WINDOWS_REPO, FORGE_WINDOWS_REF,
#   FORGE_WINDOWS_BUILD_NAME, FORGE_WINDOWS_GENERATOR,
#   FORGE_WINDOWS_CONFIGURATION, FORGE_WINDOWS_CTEST_REGEX,
#   FORGE_WINDOWS_USE_LOCAL_SOURCE, FORGE_WINDOWS_KEEP,
#   FORGE_WINDOWS_SKIP_GATE_CHECKS, FORGE_WINDOWS_SKIP_INSTALL_PACKAGE_CHECK.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FORGE_WINDOWS_CLEANUP_HOST=""
FORGE_WINDOWS_CLEANUP_KEEP="0"
FORGE_WINDOWS_CLEANUP_LOCAL_TEMP=""
FORGE_WINDOWS_CLEANUP_REMOTE_SOURCE=""
FORGE_WINDOWS_CLEANUP_REMOTE_UPLOAD=""
FORGE_WINDOWS_SSH_OPTIONS=(-o BatchMode=yes -o ConnectTimeout=15)

forge_ps_literal() {
    local value="$1"
    value="${value//\'/\'\'}"
    printf "'%s'" "${value}"
}

forge_encode_powershell() {
    printf '%s' "$1" |
        iconv -f UTF-8 -t UTF-16LE |
        base64 |
        tr -d '\r\n'
}

forge_sha256_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        printf 'no SHA-256 tool found (need sha256sum or shasum)\n' >&2
        return 1
    fi
}

forge_random_hex() {
    LC_ALL=C od -An -N16 -tx1 /dev/urandom | tr -d '[:space:]'
}

forge_validate_ssh_host() {
    local host="$1"
    if [[ -z "${host}" || "${host}" == -* ||
            "${host}" == *[[:space:]]* ]]; then
        printf 'invalid Windows SSH host: %s\n' "${host}" >&2
        return 2
    fi
}

forge_remote_ps() {
    local script='$ProgressPreference = "SilentlyContinue"'
    local encoded
    script+=$'\n& {\n'
    script+="$1"
    script+=$'\n} 6>&1'
    encoded="$(forge_encode_powershell "${script}")"
    ssh "${FORGE_WINDOWS_SSH_OPTIONS[@]}" "${FORGE_WINDOWS_CLEANUP_HOST}" \
        powershell.exe -NoProfile -NonInteractive \
        -ExecutionPolicy Bypass -OutputFormat Text -EncodedCommand "${encoded}"
}

forge_windows_cleanup() {
    local original_status=$?
    local cleanup_failed=0
    trap - EXIT
    if [[ -n "${FORGE_WINDOWS_CLEANUP_LOCAL_TEMP}" ]]; then
        if ! rm -rf -- "${FORGE_WINDOWS_CLEANUP_LOCAL_TEMP}"; then
            printf 'warning: could not remove local Windows verification scratch: %s\n' \
                "${FORGE_WINDOWS_CLEANUP_LOCAL_TEMP}" >&2
            cleanup_failed=1
        fi
    fi
    if [[ "${FORGE_WINDOWS_CLEANUP_KEEP}" != "1" &&
            ( -n "${FORGE_WINDOWS_CLEANUP_REMOTE_SOURCE}" ||
              -n "${FORGE_WINDOWS_CLEANUP_REMOTE_UPLOAD}" ) ]]; then
        local script='$ErrorActionPreference = "Stop"'
        if [[ -n "${FORGE_WINDOWS_CLEANUP_REMOTE_SOURCE}" ]]; then
            script+=$'\n'
            script+="if (Test-Path -LiteralPath $(forge_ps_literal "${FORGE_WINDOWS_CLEANUP_REMOTE_SOURCE}")) { Remove-Item -LiteralPath $(forge_ps_literal "${FORGE_WINDOWS_CLEANUP_REMOTE_SOURCE}") -Recurse -Force }"
        fi
        if [[ -n "${FORGE_WINDOWS_CLEANUP_REMOTE_UPLOAD}" ]]; then
            script+=$'\n'
            script+="\$upload = Join-Path \$HOME $(forge_ps_literal "${FORGE_WINDOWS_CLEANUP_REMOTE_UPLOAD}"); if (Test-Path -LiteralPath \$upload) { Remove-Item -LiteralPath \$upload -Force }"
        fi
        if ! forge_remote_ps "${script}" >/dev/null 2>&1; then
            printf 'warning: could not remove remote Windows verification scratch on %s\n' \
                "${FORGE_WINDOWS_CLEANUP_HOST}" >&2
            cleanup_failed=1
        fi
    fi
    if ((original_status == 0 && cleanup_failed != 0)); then
        exit 1
    fi
    exit "${original_status}"
}

forge_create_local_source_archive() {
    local archive="$1"
    local file_list="$2"

    : >"${file_list}"
    while IFS= read -r -d '' path; do
        case "${path}" in
            3rdparty/googletest|3rdparty/googletest/*)
                continue
                ;;
        esac
        if [[ -e "${REPO_ROOT}/${path}" || -L "${REPO_ROOT}/${path}" ]]; then
            printf '%s\0' "${path}" >>"${file_list}"
        fi
    done < <(git -C "${REPO_ROOT}" ls-files \
        --cached --others --exclude-standard -z)

    tar -C "${REPO_ROOT}" \
        --null \
        --exclude='.git' --exclude='*/.git' \
        -czf "${archive}" --files-from "${file_list}"
}

forge_common_parameter_block() {
    local vs_version="$1"
    local block
    block="\$params = @{ VsVersion = $(forge_ps_literal "${vs_version}")"
    block+="; Generator = $(forge_ps_literal "${FORGE_WINDOWS_GENERATOR:-Ninja}")"
    block+="; Configuration = $(forge_ps_literal "${FORGE_WINDOWS_CONFIGURATION:-Debug}")"
    block+=" }"

    if [[ -n "${FORGE_WINDOWS_VS_ROOT:-}" ]]; then
        block+=$'\n'
        block+="\$params.VisualStudioRoot = $(forge_ps_literal "${FORGE_WINDOWS_VS_ROOT}")"
    fi
    if [[ -n "${FORGE_WINDOWS_VC_VARS:-}" ]]; then
        block+=$'\n'
        block+="\$params.Vcvars = $(forge_ps_literal "${FORGE_WINDOWS_VC_VARS}")"
    fi
    if [[ -n "${FORGE_WINDOWS_BUILD_NAME:-}" ]]; then
        block+=$'\n'
        block+="\$params.BuildName = $(forge_ps_literal "${FORGE_WINDOWS_BUILD_NAME}")"
    fi
    if [[ -n "${FORGE_WINDOWS_CTEST_REGEX:-}" ]]; then
        block+=$'\n'
        block+="\$params.CTestRegex = $(forge_ps_literal "${FORGE_WINDOWS_CTEST_REGEX}")"
    fi
    if [[ "${FORGE_WINDOWS_SKIP_GATE_CHECKS:-0}" == "1" ]]; then
        block+=$'\n$params.SkipGateChecks = $true'
    fi
    if [[ "${FORGE_WINDOWS_SKIP_INSTALL_PACKAGE_CHECK:-0}" == "1" ]]; then
        block+=$'\n$params.SkipInstallPackageCheck = $true'
    fi
    printf '%s\n' "${block}"
}

forge_windows_ssh_main() {
    cd "${REPO_ROOT}"

    local host="${1:-${FORGE_WINDOWS_HOST:-}}"
    if [[ -z "${host}" ]]; then
        printf 'usage: %s <windows-ssh-host> [vs-version]\n' "$0" >&2
        printf 'or set FORGE_WINDOWS_HOST in the environment\n' >&2
        return 2
    fi
    forge_validate_ssh_host "${host}"

    local vs_version="${2:-${FORGE_WINDOWS_VS_VERSION:-18}}"
    local repo="${FORGE_WINDOWS_REPO:-https://github.com/kalcohol/ccforge.git}"
    local ref="${FORGE_WINDOWS_REF:-$(git rev-parse HEAD)}"
    local use_local_source="${FORGE_WINDOWS_USE_LOCAL_SOURCE:-0}"
    local keep="${FORGE_WINDOWS_KEEP:-0}"
    local -a scp_options=(-q -o BatchMode=yes -o ConnectTimeout=15)
    FORGE_WINDOWS_CLEANUP_HOST="${host}"
    FORGE_WINDOWS_CLEANUP_KEEP="${keep}"
    trap forge_windows_cleanup EXIT

    local params
    params="$(forge_common_parameter_block "${vs_version}")"

    if [[ "${use_local_source}" == "1" ]]; then
        FORGE_WINDOWS_CLEANUP_LOCAL_TEMP="$(mktemp -d "${TMPDIR:-/tmp}/ccforge-windows-source.XXXXXXXX")"
        local archive="${FORGE_WINDOWS_CLEANUP_LOCAL_TEMP}/src.tgz"
        local file_list="${FORGE_WINDOWS_CLEANUP_LOCAL_TEMP}/files"
        forge_create_local_source_archive "${archive}" "${file_list}"
        local archive_hash
        archive_hash="$(forge_sha256_file "${archive}")"
        printf '[msvc-ssh] local-source-sha256=%s\n' "${archive_hash}"

        FORGE_WINDOWS_CLEANUP_REMOTE_SOURCE="$(forge_remote_ps '
$ErrorActionPreference = "Stop"
$path = Join-Path ([IO.Path]::GetTempPath()) ("ccforge-local-source-" + [Guid]::NewGuid().ToString("N"))
$null = New-Item -ItemType Directory -Path $path
[Console]::Out.Write($path)
' | tr -d '\r\n')"
        FORGE_WINDOWS_CLEANUP_REMOTE_SOURCE="${FORGE_WINDOWS_CLEANUP_REMOTE_SOURCE//\\//}"
        FORGE_WINDOWS_CLEANUP_REMOTE_UPLOAD="ccforge-source-$(forge_random_hex).tgz"
        scp "${scp_options[@]}" "${archive}" \
            "${host}:${FORGE_WINDOWS_CLEANUP_REMOTE_UPLOAD}"

        local extract_script='$ErrorActionPreference = "Stop"'
        extract_script+=$'\n'
        extract_script+="\$archive = Join-Path \$HOME $(forge_ps_literal "${FORGE_WINDOWS_CLEANUP_REMOTE_UPLOAD}")"
        extract_script+=$'\n'
        extract_script+="\$expected = $(forge_ps_literal "${archive_hash}")"
        extract_script+=$'\n'
        extract_script+='$actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash.ToLowerInvariant()'
        extract_script+=$'\nif ($actual -ne $expected) { throw "local-source archive hash mismatch" }\n'
        extract_script+="& tar.exe -xzf \$archive -C $(forge_ps_literal "${FORGE_WINDOWS_CLEANUP_REMOTE_SOURCE}")"
        extract_script+=$'\nif ($LASTEXITCODE -ne 0) { throw "local-source extraction failed" }\n'
        extract_script+='Remove-Item -LiteralPath $archive -Force'
        forge_remote_ps "${extract_script}"
        FORGE_WINDOWS_CLEANUP_REMOTE_UPLOAD=""

        local invoke_script='$ErrorActionPreference = "Stop"'
        invoke_script+=$'\n'
        invoke_script+="\$script = Join-Path $(forge_ps_literal "${FORGE_WINDOWS_CLEANUP_REMOTE_SOURCE}") 'scripts\\verify-windows-msvc.ps1'"
        invoke_script+=$'\n'
        invoke_script+="${params}"
        invoke_script+=$'\n'
        invoke_script+="\$params.SourcePath = $(forge_ps_literal "${FORGE_WINDOWS_CLEANUP_REMOTE_SOURCE}")"
        invoke_script+=$'\n& $script @params'
        forge_remote_ps "${invoke_script}"
        if [[ "${keep}" == "1" ]]; then
            printf '[msvc-ssh] kept remote source: %s\n' \
                "${FORGE_WINDOWS_CLEANUP_REMOTE_SOURCE}"
        fi
    else
        local clone_script='$ErrorActionPreference = "Stop"'
        clone_script+=$'\n'
        clone_script+="\$repo = $(forge_ps_literal "${repo}")"
        clone_script+=$'\n'
        clone_script+="\$ref = $(forge_ps_literal "${ref}")"
        clone_script+=$'\n'
        if [[ "${keep}" == "1" ]]; then
            clone_script+='$keep = $true'
        else
            clone_script+='$keep = $false'
        fi
        clone_script+=$'\n'
        clone_script+='$root = Join-Path ([IO.Path]::GetTempPath()) ("ccforge-win-ssh-" + [Guid]::NewGuid().ToString("N"))
$null = New-Item -ItemType Directory -Path $root
$source = Join-Path $root "src"
$verificationSucceeded = $false
try {
    & git.exe clone --no-checkout -- $repo $source
    if ($LASTEXITCODE -ne 0) { throw "clone failed" }
    $resolved = ((& git.exe -C $source rev-parse --verify --end-of-options ($ref + "^{commit}")) -join "").Trim()
    if ($LASTEXITCODE -ne 0 -or $resolved -notmatch "^[0-9a-fA-F]{40}$") {
        throw "could not resolve immutable source commit"
    }
    & git.exe -C $source checkout --detach $resolved
    if ($LASTEXITCODE -ne 0) { throw "detached checkout failed" }
    Write-Host "[msvc-ssh] source-commit=$resolved"
'
        clone_script+="${params}"
        clone_script+=$'\n    $params.SourcePath = $source\n'
        clone_script+='$verifier = Join-Path $source "scripts\verify-windows-msvc.ps1"
    & $verifier @params
    $verificationSucceeded = $true
} finally {
    if (-not $keep) {
        try {
            Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction Stop
        } catch {
            if ($verificationSucceeded) { throw }
            Write-Warning "Could not remove failed Windows verification root: $root"
        }
    } else {
        Write-Host "[msvc-ssh] kept remote source: $source"
    }
}'
        forge_remote_ps "${clone_script}"
    fi
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    forge_windows_ssh_main "$@"
fi

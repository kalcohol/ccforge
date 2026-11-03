#!/usr/bin/env bash

# GNU realpath -m is preferred; Python 3.6+ supplies the missing-path resolver
# on hosts without that option. Neither backend changes scratch ownership.
forge_verification_scratch_realpath() {
    local resolved

    if realpath -m -- / >/dev/null 2>&1; then
        resolved="$(realpath -m -- "$1")" || return 2
    elif command -v python3 >/dev/null 2>&1; then
        resolved="$(python3 - "$1" <<'PY'
from pathlib import Path
import sys

try:
    print(Path(sys.argv[1]).resolve(strict=False))
except (OSError, RuntimeError, TypeError, ValueError):
    sys.exit(2)
PY
)" || return 2
    else
        printf '[verification-scratch] ERROR: GNU realpath -m or Python 3.6+ is required\n' >&2
        return 2
    fi

    if [[ "${resolved}" != /* ]]; then
        printf '[verification-scratch] ERROR: path resolution did not return an absolute path\n' >&2
        return 2
    fi
    printf '%s\n' "${resolved}"
}

# Prepare a verification scratch directory without allowing a caller-provided
# path to turn a routine cleanup into an arbitrary recursive deletion.
forge_prepare_verification_scratch() {
    local requested_root="$1"
    local repo_root="$2"
    local owner="$3"
    local root repo repo_build marker expected

    if [[ -z "${requested_root}" || -z "${repo_root}" || -z "${owner}" ]]; then
        printf '[verification-scratch] ERROR: root, repo, and owner are required\n' >&2
        return 2
    fi

    root="$(forge_verification_scratch_realpath "${requested_root}")" || return 2
    repo="$(forge_verification_scratch_realpath "${repo_root}")" || return 2
    repo_build="$(forge_verification_scratch_realpath "${repo_root}/build")" || return 2
    marker="${root}/.ccforge-verification-scratch"
    expected="ccforge-verification-scratch-v1:${owner}"

    if [[ "${root}" == "/" || "${root}" == "${repo}" ||
            "${root}" == "${repo_build}" ]]; then
        printf '[verification-scratch] ERROR: refusing broad scratch root: %s\n' \
            "${root}" >&2
        return 2
    fi

    case "${root}" in
        "${repo_build}"/*)
            rm -rf -- "${root}"
            mkdir -p -- "${root}"
            ;;
        *)
            if [[ -e "${root}" && ! -d "${root}" ]]; then
                printf '[verification-scratch] ERROR: scratch root is not a directory: %s\n' \
                    "${root}" >&2
                return 2
            fi
            if [[ -d "${root}" ]]; then
                if [[ ! -f "${marker}" || "$(cat -- "${marker}")" != "${expected}" ]]; then
                    printf '[verification-scratch] ERROR: external scratch root is not owned by %s: %s\n' \
                        "${owner}" "${root}" >&2
                    return 2
                fi
                find "${root}" -mindepth 1 -maxdepth 1 \
                    ! -name '.ccforge-verification-scratch' \
                    -exec rm -rf -- {} +
            else
                mkdir -p -- "${root}"
            fi
            ;;
    esac

    printf '%s\n' "${expected}" >"${marker}"
    printf '%s\n' "${root}"
}

#!/usr/bin/env bash

# Prepare a verification scratch directory without allowing a caller-provided
# path to turn a routine cleanup into an arbitrary recursive deletion.
forge_prepare_verification_scratch() {
    local requested_root="$1"
    local repo_root="$2"
    local owner="$3"
    local root repo_build marker expected

    if [[ -z "${requested_root}" || -z "${repo_root}" || -z "${owner}" ]]; then
        printf '[verification-scratch] ERROR: root, repo, and owner are required\n' >&2
        return 2
    fi

    root="$(realpath -m -- "${requested_root}")"
    repo_build="$(realpath -m -- "${repo_root}/build")"
    marker="${root}/.ccforge-verification-scratch"
    expected="ccforge-verification-scratch-v1:${owner}"

    if [[ "${root}" == "/" || "${root}" == "$(realpath -m -- "${repo_root}")" ||
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

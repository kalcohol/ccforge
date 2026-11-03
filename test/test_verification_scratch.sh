#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "${REPO_ROOT}/scripts/lib/verification-scratch.sh"

if [[ "${1:-}" == --python-fallback ]]; then
    realpath() { return 1; }
fi

scratch="$(mktemp -d -t ccforge-verification-scratch-test.XXXXXX)"
trap 'rm -rf -- "${scratch}"' EXIT
scratch="$(cd "${scratch}" && pwd -P)"

fake_repo="${scratch}/repo"
mkdir -p "${fake_repo}/build"

repo_scratch="${fake_repo}/build/owned"
prepared="$(forge_prepare_verification_scratch \
    "${repo_scratch}" "${fake_repo}" install-package)"
[[ "${prepared}" == "${repo_scratch}" ]]
touch "${repo_scratch}/stale"
forge_prepare_verification_scratch \
    "${repo_scratch}" "${fake_repo}" install-package >/dev/null
[[ ! -e "${repo_scratch}/stale" ]]
[[ "$(cat "${repo_scratch}/.ccforge-verification-scratch")" == \
    ccforge-verification-scratch-v1:install-package ]]

prepared="$(cd "${fake_repo}" && forge_prepare_verification_scratch \
    'build/missing/../nested/leaf' . install-package)"
[[ "${prepared}" == "${fake_repo}/build/nested/leaf" ]]
[[ -d "${prepared}" && ! -e "${fake_repo}/build/missing" ]]

ln -s "${fake_repo}/build" "${scratch}/build-link"
prepared="$(forge_prepare_verification_scratch \
    "${scratch}/build-link/linked child" "${fake_repo}" install-package)"
[[ "${prepared}" == "${fake_repo}/build/linked child" ]]
[[ -d "${prepared}" ]]

external_scratch="${scratch}/external scratch"
forge_prepare_verification_scratch \
    "${external_scratch}" "${fake_repo}" stdexec-feasibility >/dev/null
touch "${external_scratch}/stale"
forge_prepare_verification_scratch \
    "${external_scratch}" "${fake_repo}" stdexec-feasibility >/dev/null
[[ ! -e "${external_scratch}/stale" ]]
[[ "$(cat "${external_scratch}/.ccforge-verification-scratch")" == \
    ccforge-verification-scratch-v1:stdexec-feasibility ]]

unowned="${scratch}/unowned"
mkdir -p "${unowned}"
touch "${unowned}/sentinel"
if forge_prepare_verification_scratch \
        "${unowned}" "${fake_repo}" install-package >/dev/null 2>&1; then
    printf 'unowned external scratch root was accepted\n' >&2
    exit 1
fi
[[ -e "${unowned}/sentinel" ]]

touch "${external_scratch}/sentinel"
if forge_prepare_verification_scratch \
        "${external_scratch}" "${fake_repo}" other-owner >/dev/null 2>&1; then
    printf 'scratch root owned by another verification script was accepted\n' >&2
    exit 1
fi
[[ -e "${external_scratch}/sentinel" ]]
[[ "$(cat "${external_scratch}/.ccforge-verification-scratch")" == \
    ccforge-verification-scratch-v1:stdexec-feasibility ]]

ln -s "${fake_repo}" "${scratch}/repo-link"
for broad_root in "${fake_repo}" "${fake_repo}/build" / \
        "${scratch}/repo-link" "${scratch}/repo-link/build" \
        "${scratch}/build-link/.."; do
    if forge_prepare_verification_scratch \
            "${broad_root}" "${fake_repo}" install-package >/dev/null 2>&1; then
        printf 'broad scratch root was accepted: %s\n' "${broad_root}" >&2
        exit 1
    fi
done

ln -s "${unowned}" "${fake_repo}/build/external-link"
if forge_prepare_verification_scratch \
        "${fake_repo}/build/external-link" "${fake_repo}" install-package \
        >/dev/null 2>&1; then
    printf 'unowned external target through a build symlink was accepted\n' >&2
    exit 1
fi
[[ -L "${fake_repo}/build/external-link" && -e "${unowned}/sentinel" ]]
[[ ! -e "${unowned}/.ccforge-verification-scratch" ]]

mkdir -p "${fake_repo}/build-sibling"
touch "${fake_repo}/build-sibling/sentinel" "${scratch}/plain-file"
for refused_root in "${fake_repo}/build-sibling" "${scratch}/plain-file"; do
    if forge_prepare_verification_scratch \
            "${refused_root}" "${fake_repo}" install-package >/dev/null 2>&1; then
        printf 'unowned directory or non-directory was accepted\n' >&2
        exit 1
    fi
done
[[ -e "${fake_repo}/build-sibling/sentinel" && -f "${scratch}/plain-file" ]]

# Resolver failures must be checked even when the caller disables errexit.
touch "${repo_scratch}/sentinel"
for failed_path in "${repo_scratch}" "${fake_repo}" "${fake_repo}/build"; do
    for result in failed empty relative; do
        (
            realpath() {
                if [[ "$3" == / ]]; then
                    printf '/\n'
                elif [[ "$3" == "${failed_path}" ]]; then
                    case "${result}" in
                        failed) printf '%s\n' "$3"; return 1 ;;
                        empty) return 0 ;;
                        relative) printf 'relative\n' ;;
                    esac
                else
                    printf '%s\n' "$3"
                fi
            }
            status=0
            output="$(forge_prepare_verification_scratch \
                "${repo_scratch}" "${fake_repo}" install-package 2>/dev/null)" || status=$?
            [[ ${status} -eq 2 && -z "${output}" && -e "${repo_scratch}/sentinel" ]]
        )
    done
done

(
    realpath() { return 1; }
    python3() { return 1; }
    status=0
    output="$(forge_prepare_verification_scratch \
        "${repo_scratch}" "${fake_repo}" install-package 2>/dev/null)" || status=$?
    [[ ${status} -eq 2 && -z "${output}" && -e "${repo_scratch}/sentinel" ]]
)

mkdir -p "${scratch}/no-tools"
status=0
output="$(PATH="${scratch}/no-tools" forge_prepare_verification_scratch \
    "${repo_scratch}" "${fake_repo}" install-package 2>/dev/null)" || status=$?
[[ ${status} -eq 2 && -z "${output}" && -e "${repo_scratch}/sentinel" ]]

if [[ "${1:-}" != --python-fallback ]] && command -v python3 >/dev/null 2>&1; then
    "${BASH}" "${BASH_SOURCE[0]}" --python-fallback
fi

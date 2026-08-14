#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "${REPO_ROOT}/scripts/lib/verification-scratch.sh"

scratch="$(mktemp -d -t ccforge-verification-scratch-test.XXXXXX)"
trap 'rm -rf -- "${scratch}"' EXIT

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

external_scratch="${scratch}/external scratch"
forge_prepare_verification_scratch \
    "${external_scratch}" "${fake_repo}" stdexec-feasibility >/dev/null
touch "${external_scratch}/stale"
forge_prepare_verification_scratch \
    "${external_scratch}" "${fake_repo}" stdexec-feasibility >/dev/null
[[ ! -e "${external_scratch}/stale" ]]

unowned="${scratch}/unowned"
mkdir -p "${unowned}"
touch "${unowned}/sentinel"
if forge_prepare_verification_scratch \
        "${unowned}" "${fake_repo}" install-package >/dev/null 2>&1; then
    printf 'unowned external scratch root was accepted\n' >&2
    exit 1
fi
[[ -e "${unowned}/sentinel" ]]

if forge_prepare_verification_scratch \
        "${external_scratch}" "${fake_repo}" other-owner >/dev/null 2>&1; then
    printf 'scratch root owned by another verification script was accepted\n' >&2
    exit 1
fi

for broad_root in "${fake_repo}" "${fake_repo}/build" /; do
    if forge_prepare_verification_scratch \
            "${broad_root}" "${fake_repo}" install-package >/dev/null 2>&1; then
        printf 'broad scratch root was accepted: %s\n' "${broad_root}" >&2
        exit 1
    fi
done

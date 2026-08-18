#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${FORGE_IO_URING_PROBE_BUILD_DIR:-${repo_root}/build/probes}"
compiler="${CXX:-c++}"
probe="${build_dir}/io_uring_raw_probe"

cmake -E make_directory "${build_dir}"
"${compiler}" \
    -std=c++23 \
    -Wall \
    -Wextra \
    -Werror \
    "${repo_root}/scripts/io_uring_raw_probe.cpp" \
    -o "${probe}"

set +e
"${probe}"
status=$?
set -e

if [[ ${status} -eq 77 ]]; then
    printf '%s\n' \
        "io_uring runtime unavailable (seccomp, policy, or kernel); probe skipped"
    exit 77
fi
exit "${status}"

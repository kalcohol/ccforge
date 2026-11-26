#!/usr/bin/env bash
# Qualify the standard target in a working-tree copy with no extension headers.
# Run one lane at a time, separately from the regular verification matrix.

set -euo pipefail

if [[ $# != 1 ]]; then
    printf 'usage: %s <llvm|zig>\n' "$0" >&2
    exit 2
fi
lane="$1"
case "${lane}" in
    llvm) standard=26 ;;
    zig) standard=23 ;;
    *) printf 'unknown standard-only lane: %s\n' "${lane}" >&2; exit 2 ;;
esac

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"
if [[ ! -f 3rdparty/googletest/CMakeLists.txt ]]; then
    printf 'Initialize GoogleTest with git submodule update --init --depth 1 -- 3rdparty/googletest\n' >&2
    exit 1
fi

PODMAN="${PODMAN:-podman}"
mkdir -p build/review
run_dir="$(mktemp -d "${REPO_ROOT}/build/review/standard-only-${lane}.XXXXXXXX")"
stage="${run_dir}/source"
trap 'rm -rf -- "${stage}"' EXIT
mkdir -p "${stage}"

# Use worktree bytes, not git archive, so the owner can qualify an uncommitted
# review. The gitlink contributes the checked-out GoogleTest directory to tar.
git ls-files -z --cached --others --exclude-standard --deduplicate -- ':!include/forge' |
    while IFS= read -r -d '' file; do
        if [[ -e "${file}" || -L "${file}" ]]; then
            printf '%s\0' "${file}"
        fi
    done |
    tar --exclude=.git --exclude=include/forge -cf - --null -T - |
    tar -xf - -C "${stage}"
if [[ -e "${stage}/include/forge" || -L "${stage}/include/forge" ]]; then
    printf 'Standard-only stage unexpectedly contains include/forge\n' >&2
    exit 1
fi
if [[ ! -f "${stage}/3rdparty/googletest/CMakeLists.txt" ]]; then
    printf 'Standard-only stage is missing the GoogleTest checkout\n' >&2
    exit 1
fi

printf '[standard-only] %s qualification log: %s\n' "${lane}" "${run_dir}/run.log"
if (
    cd "${stage}" &&
    "${PODMAN}" build -t "forge-${lane}" -f "containers/Containerfile.${lane}" . &&
    "${PODMAN}" run --rm --userns=keep-id -v "$PWD:/src:Z" -w /src "forge-${lane}" \
        bash -lc '
            set -euo pipefail
            test ! -e include/forge && test ! -L include/forge || exit 1
            cmake -S . -B build/standard-only -G Ninja -DCMAKE_BUILD_TYPE=Debug \
                -DCMAKE_CXX_STANDARD="$1" -DFORGE_ENABLE_EXTENSIONS=OFF \
                -DFORGE_BUILD_TESTS=ON -DFORGE_BUILD_EXAMPLES=ON
            cmake --build build/standard-only
            ctest --test-dir build/standard-only --no-tests=error --output-on-failure
        ' bash "${standard}"
) >"${run_dir}/run.log" 2>&1; then
    printf '[standard-only] %s passed\n' "${lane}"
else
    status=$?
    printf '[standard-only] %s failed (exit %d). See %s/run.log\n' "${lane}" "${status}" "${run_dir}" >&2
    exit "${status}"
fi

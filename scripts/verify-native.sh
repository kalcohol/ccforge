#!/usr/bin/env bash
#
# verify-native.sh — exercise Forge's "seamless injection / stand-aside" handshake
# across toolchains, using rootless podman (--userns=keep-id so the bind-mounted
# repo stays writable as the host user).
#
# Usage:
#   scripts/verify-native.sh [target ...]
#
# Targets:
#   gcc16   GCC 16 container, -std=c++26. Asserts std::simd & std::submdspan
#           STAND ASIDE (native present) and that no ODR/redefinition occurs.
#   llvm    LLVM/libc++ container, -std=c++26. All four backports inject; full
#           suite must pass (regression on the inject path).
#   zig     Zig container. Backport inject path (native x86_64).
#   local   Host toolchain (no container), -std=c++23 baseline regression.
#   all     gcc16 + llvm + zig + local (default).
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

PODMAN="${PODMAN:-podman}"
LOG_DIR="${REPO_ROOT}/build/verify-logs"
mkdir -p "${LOG_DIR}"

log()  { printf '\033[1;34m[verify]\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31m[FAIL]\033[0m %s\n' "$*" >&2; exit 1; }
ok()   { printf '\033[1;32m[ok]\033[0m %s\n' "$*"; }

# run_in_container <image-tag> <containerfile> <std> <extra-cmake-args...>
build_image() {
    local tag="$1" containerfile="$2"
    log "building image ${tag} from ${containerfile}"
    "${PODMAN}" build -t "${tag}" -f "${containerfile}" "${REPO_ROOT}"
}

# Configure + build + test inside a container, capturing the configure log.
container_run() {
    local tag="$1" builddir="$2" std="$3"
    "${PODMAN}" run --rm \
        --userns=keep-id \
        -v "${REPO_ROOT}:/src:Z" \
        -w /src \
        "${tag}" \
        bash -lc "
            set -euo pipefail
            rm -rf '${builddir}'
            cmake -S . -B '${builddir}' -G Ninja \
                  -DCMAKE_BUILD_TYPE=Debug \
                  -DCMAKE_CXX_STANDARD=${std} \
                  -DFORGE_BUILD_TESTS=ON
            cmake --build '${builddir}'
            ctest --test-dir '${builddir}' --output-on-failure
        "
}

# Assert the cmake configure log shows simd/submdspan standing aside (NOT injected).
assert_stands_aside() {
    local logfile="$1" feature="$2"
    if grep -q "CC Forge: ${feature} backport enabled" "${logfile}"; then
        fail "${feature}: backport was INJECTED — expected stand-aside on this toolchain (ODR risk). See ${logfile}"
    fi
    if grep -q "CC Forge: ${feature} native support" "${logfile}"; then
        ok "${feature}: Forge stood aside for native support"
    else
        fail "${feature}: no stand-aside message found in configure log ${logfile}"
    fi
}

target_gcc16() {
    build_image forge-gcc16 containers/Containerfile.gcc16
    local logfile="${LOG_DIR}/gcc16-configure.log"
    log "gcc16: configuring -std=c++26 (capturing configure log)"
    # Configure-only first so we can assert on the stand-aside messages.
    "${PODMAN}" run --rm --userns=keep-id -v "${REPO_ROOT}:/src:Z" -w /src forge-gcc16 \
        bash -lc "rm -rf build/gcc16 && cmake -S . -B build/gcc16 -G Ninja -DCMAKE_CXX_STANDARD=26 -DFORGE_BUILD_TESTS=ON" \
        2>&1 | tee "${logfile}"
    assert_stands_aside "${logfile}" "std::simd"
    assert_stands_aside "${logfile}" "std::submdspan"
    log "gcc16: building + testing (native coexistence must compile cleanly)"
    container_run forge-gcc16 build/gcc16 26
    ok "gcc16 verified"
}

target_llvm() {
    build_image forge-llvm containers/Containerfile.llvm
    log "llvm/libc++: configuring + testing -std=c++26 (all backports inject)"
    container_run forge-llvm build/llvm 26
    ok "llvm verified"
}

target_zig() {
    build_image forge-zig containers/Containerfile.zig
    log "zig: configuring + testing (backport inject path)"
    container_run forge-zig build/zig 23
    ok "zig verified"
}

target_local() {
    command -v cmake >/dev/null || fail "local: cmake not found on host"
    log "local host toolchain: -std=c++23 baseline regression"
    rm -rf build/local
    cmake -S . -B build/local -DCMAKE_CXX_STANDARD=23 -DFORGE_BUILD_TESTS=ON
    cmake --build build/local
    ctest --test-dir build/local --output-on-failure
    ok "local verified"
}

targets=("$@")
if [[ ${#targets[@]} -eq 0 ]]; then
    targets=(gcc16 llvm zig local)
fi

for t in "${targets[@]}"; do
    case "${t}" in
        gcc16) target_gcc16 ;;
        llvm)  target_llvm ;;
        zig)   target_zig ;;
        local) target_local ;;
        all)   target_gcc16; target_llvm; target_zig; target_local ;;
        *)     fail "unknown target: ${t}" ;;
    esac
done

ok "all requested targets verified"

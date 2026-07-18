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
#   gcc16   GCC 16 container, -std=c++26. Asserts std::simd, padded mdspan
#           layouts, and std::submdspan stand aside as complete native
#           surfaces; its pre-P4206 std::constant_wrapper must stand aside as
#           partial native. Also verifies no ODR/redefinition occurs.
#   llvm    LLVM/libc++ container, -std=c++26. Backports inject where libc++ lacks
#           native support; full suite must pass (regression on the inject path).
#   zig     Zig container. Backport inject path (native x86_64).
#   local   Host toolchain (no container), -std=c++23 baseline regression.
#   gcc-exec
#           GCC 16 container, -std=c++26. Builds only execution tests to cover
#           the std::execution backport on libstdc++ without SIMD test probes.
#   tsan    LLVM/libc++ container, -std=c++26 + -fsanitize=thread. Runs the
#           execution + forge utility subsets under ThreadSanitizer
#           (data-race / deadlock check).
#   asan    LLVM/libc++ container, -std=c++26 + -fsanitize=address,undefined.
#           Runs the execution + forge utility subsets under ASan+UBSan
#           (UAF / leak / UB check).
#   all     gcc16 + llvm + zig + local + gcc-exec + tsan + asan (default).
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

build_image() {
    local tag="$1" containerfile="$2"
    log "building image ${tag} from ${containerfile}"
    "${PODMAN}" build -t "${tag}" -f "${containerfile}" "${REPO_ROOT}"
}

# Configure + build + test inside a container.
container_run() {
    local tag="$1" builddir="$2" std="$3"
    shift 3
    "${PODMAN}" run --rm \
        --userns=keep-id \
        -v "${REPO_ROOT}:/src:Z" \
        -w /src \
        "${tag}" \
        bash -lc '
            set -euo pipefail
            builddir="$1"
            std="$2"
            shift 2
            rm -rf "${builddir}"
            cmake -S . -B "${builddir}" -G Ninja \
                  -DCMAKE_BUILD_TYPE=Debug \
                  -DCMAKE_CXX_STANDARD="${std}" \
                  -DFORGE_BUILD_TESTS=ON \
                  "$@"
            cmake --build "${builddir}"
            ctest --test-dir "${builddir}" --output-on-failure
        ' bash "${builddir}" "${std}" "$@"
}

FORGE_EXECUTION_ONLY_TEST_ARGS=(
    -DFORGE_BUILD_EXAMPLES=OFF
    -DFORGE_TEST_ENABLE_EXECUTION=ON
    -DFORGE_TEST_ENABLE_SIMD=OFF
    -DFORGE_TEST_ENABLE_CONSTANT_WRAPPER=OFF
    -DFORGE_TEST_ENABLE_UNIQUE_RESOURCE=OFF
    -DFORGE_TEST_ENABLE_SUBMDSPAN=OFF
    -DFORGE_TEST_ENABLE_LINALG=OFF
    -DFORGE_TEST_ENABLE_FORGE=OFF
    -DFORGE_TEST_ENABLE_NATIVE_HANDOFF=OFF
)

FORGE_EXECUTION_AND_FORGE_TEST_ARGS=(
    -DFORGE_BUILD_EXAMPLES=OFF
    -DFORGE_TEST_ENABLE_EXECUTION=ON
    -DFORGE_TEST_ENABLE_SIMD=OFF
    -DFORGE_TEST_ENABLE_CONSTANT_WRAPPER=OFF
    -DFORGE_TEST_ENABLE_UNIQUE_RESOURCE=OFF
    -DFORGE_TEST_ENABLE_SUBMDSPAN=OFF
    -DFORGE_TEST_ENABLE_LINALG=OFF
    -DFORGE_TEST_ENABLE_FORGE=ON
    -DFORGE_TEST_ENABLE_NATIVE_HANDOFF=OFF
)

FORGE_NATIVE_HANDOFF_ONLY_TEST_ARGS=(
    -DFORGE_BUILD_EXAMPLES=OFF
    -DFORGE_TEST_ENABLE_EXECUTION=OFF
    -DFORGE_TEST_ENABLE_SIMD=OFF
    -DFORGE_TEST_ENABLE_CONSTANT_WRAPPER=ON
    -DFORGE_TEST_ENABLE_UNIQUE_RESOURCE=OFF
    -DFORGE_TEST_ENABLE_SUBMDSPAN=OFF
    -DFORGE_TEST_ENABLE_LINALG=OFF
    -DFORGE_TEST_ENABLE_FORGE=OFF
    -DFORGE_TEST_ENABLE_NATIVE_HANDOFF=ON
)

# Assert the cmake configure log shows a feature standing aside (NOT injected).
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

assert_partial_stands_aside() {
    local logfile="$1" feature="$2"
    if grep -q "CC Forge: ${feature} backport enabled" "${logfile}"; then
        fail "${feature}: backport was INJECTED — expected partial-native stand-aside on this toolchain (ODR risk). See ${logfile}"
    fi
    if grep -q "CC Forge: ${feature} native support is present but INCOMPLETE" "${logfile}"; then
        ok "${feature}: Forge stood aside for partial native support"
    else
        fail "${feature}: no partial-native stand-aside message found in configure log ${logfile}"
    fi
}

target_gcc16() {
    build_image forge-gcc16 containers/Containerfile.gcc16
    local logfile="${LOG_DIR}/gcc16-configure.log"
    log "gcc16: configuring -std=c++26 (capturing configure log)"
    # Configure-only first so we can assert on the stand-aside messages.
    "${PODMAN}" run --rm --userns=keep-id -v "${REPO_ROOT}:/src:Z" -w /src forge-gcc16 \
        bash -lc '
            set -euo pipefail
            rm -rf build/gcc16
            cmake -S . -B build/gcc16 -G Ninja \
                  -DCMAKE_BUILD_TYPE=Debug \
                  -DCMAKE_CXX_STANDARD=26 \
                  -DFORGE_BUILD_TESTS=ON \
                  "$@"
        ' bash "${FORGE_NATIVE_HANDOFF_ONLY_TEST_ARGS[@]}" \
        2>&1 | tee "${logfile}"
    assert_stands_aside "${logfile}" "std::simd"
    assert_partial_stands_aside "${logfile}" "std::constant_wrapper"
    assert_stands_aside "${logfile}" "std::mdspan padded layouts"
    assert_stands_aside "${logfile}" "std::submdspan"
    log "gcc16: building + testing (native handoff must compile cleanly)"
    container_run forge-gcc16 build/gcc16 26 "${FORGE_NATIVE_HANDOFF_ONLY_TEST_ARGS[@]}"
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

target_gcc_exec() {
    build_image forge-gcc16 containers/Containerfile.gcc16
    log "gcc-exec: configuring + testing execution subset on GCC/libstdc++"
    container_run forge-gcc16 build/gcc-exec 26 "${FORGE_EXECUTION_ONLY_TEST_ARGS[@]}"
    ok "gcc-exec verified (execution subset on libstdc++)"
}

target_tsan() {
    build_image forge-tsan containers/Containerfile.tsan
    log "tsan: building execution + forge utility tests with -fsanitize=thread (libc++)"
    "${PODMAN}" run --rm --userns=keep-id --cap-add=SYS_PTRACE \
        -v "${REPO_ROOT}:/src:Z" -w /src forge-tsan bash -lc '
            set -e
            rm -rf build/tsan
            cmake -S . -B build/tsan -G Ninja \
                  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=26 \
                  -DFORGE_BUILD_TESTS=ON \
                  -DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1" \
                  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" \
                  "$@"
            cmake --build build/tsan
            export TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1"
            selected_tests=$(ctest --test-dir build/tsan -N -R "execution|forge" | sed -n "s/^Total Tests: //p")
            if [ -z "${selected_tests}" ] || [ "${selected_tests}" -le 0 ]; then
                echo "[tsan] selected zero tests for regex: execution|forge" >&2
                exit 1
            fi
            echo "[tsan] ctest-count=${selected_tests}"
            # Try plain ctest first; if the instrumented binaries hit the
            # mmap_rnd_bits TSAN startup crash on high-ASLR-entropy hosts, retry
            # with ASLR disabled via setarch -R.
            if ctest --test-dir build/tsan -R "execution|forge" --output-on-failure; then
                exit 0
            fi
            echo "[tsan] retrying under setarch -R (ASLR off; mmap_rnd_bits workaround)"
            setarch "$(uname -m)" -R ctest --test-dir build/tsan -R "execution|forge" --output-on-failure
        ' bash "${FORGE_EXECUTION_AND_FORGE_TEST_ARGS[@]}"
    ok "tsan verified (execution + forge utility subsets, no data races)"
}

target_asan() {
    build_image forge-asan containers/Containerfile.asan
    log "asan: building execution + forge utility tests with -fsanitize=address,undefined (libc++)"
    "${PODMAN}" run --rm --userns=keep-id --cap-add=SYS_PTRACE \
        -v "${REPO_ROOT}:/src:Z" -w /src forge-asan bash -lc '
            set -e
            rm -rf build/asan
            cmake -S . -B build/asan -G Ninja \
                  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=26 \
                  -DFORGE_BUILD_TESTS=ON \
                  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1" \
                  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
                  "$@"
            cmake --build build/asan
            # detect_container_overflow=0 avoids false positives against a
            # non-ASan-instrumented system libc++; UBSan halts on first error.
            export ASAN_OPTIONS="detect_container_overflow=0:abort_on_error=1"
            export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"
            selected_tests=$(ctest --test-dir build/asan -N -R "execution|forge" | sed -n "s/^Total Tests: //p")
            if [ -z "${selected_tests}" ] || [ "${selected_tests}" -le 0 ]; then
                echo "[asan] selected zero tests for regex: execution|forge" >&2
                exit 1
            fi
            echo "[asan] ctest-count=${selected_tests}"
            if ctest --test-dir build/asan -R "execution|forge" --output-on-failure; then
                exit 0
            fi
            echo "[asan] retrying under setarch -R (ASLR off; shadow-mapping workaround)"
            setarch "$(uname -m)" -R ctest --test-dir build/asan -R "execution|forge" --output-on-failure
        ' bash "${FORGE_EXECUTION_AND_FORGE_TEST_ARGS[@]}"
    log "asan: building focused SIMD memory tests with -fsanitize=address,undefined"
    "${PODMAN}" run --rm --userns=keep-id --cap-add=SYS_PTRACE \
        -v "${REPO_ROOT}:/src:Z" -w /src forge-asan bash -lc '
            set -e
            rm -rf build/asan-simd-memory
            cmake -S . -B build/asan-simd-memory -G Ninja \
                  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=26 \
                  -DFORGE_BUILD_TESTS=ON \
                  -DFORGE_BUILD_EXAMPLES=OFF \
                  -DFORGE_TEST_ENABLE_EXECUTION=OFF \
                  -DFORGE_TEST_ENABLE_SIMD=ON \
                  -DFORGE_TEST_ENABLE_CONSTANT_WRAPPER=OFF \
                  -DFORGE_TEST_ENABLE_UNIQUE_RESOURCE=OFF \
                  -DFORGE_TEST_ENABLE_SUBMDSPAN=OFF \
                  -DFORGE_TEST_ENABLE_LINALG=OFF \
                  -DFORGE_TEST_ENABLE_FORGE=OFF \
                  -DFORGE_TEST_ENABLE_NATIVE_HANDOFF=OFF \
                  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1" \
                  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
            cmake --build build/asan-simd-memory --target \
                  test_simd_memory_load_store \
                  test_simd_memory_gather_scatter \
                  test_simd_memory_supported_types
            export ASAN_OPTIONS="detect_container_overflow=0:abort_on_error=1"
            export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"
            if ctest --test-dir build/asan-simd-memory -R "simd_memory" --output-on-failure; then
                exit 0
            fi
            echo "[asan-simd] retrying under setarch -R (ASLR off; shadow-mapping workaround)"
            setarch "$(uname -m)" -R ctest --test-dir build/asan-simd-memory -R "simd_memory" --output-on-failure
        '
    ok "asan verified (execution + forge utility subsets plus SIMD memory tests, no UAF/leak/UB)"
}

targets=("$@")
if [[ ${#targets[@]} -eq 0 ]]; then
    targets=(gcc16 llvm zig local gcc-exec tsan asan)
fi

for t in "${targets[@]}"; do
    case "${t}" in
        gcc16) target_gcc16 ;;
        llvm)  target_llvm ;;
        zig)   target_zig ;;
        local) target_local ;;
        gcc-exec) target_gcc_exec ;;
        tsan)  target_tsan ;;
        asan)  target_asan ;;
        all)   target_gcc16; target_llvm; target_zig; target_local; target_gcc_exec; target_tsan; target_asan ;;
        *)     fail "unknown target: ${t}" ;;
    esac
done

ok "all requested targets verified"

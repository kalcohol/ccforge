#!/usr/bin/env bash
#
# Verify source-tree and installed-package consumption without exposing CC
# Forge's test-only GoogleTest dependency. This is intentionally separate from
# default CTest because it performs multiple configure/build cycles.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STANDARD_ONLY=0
DEFAULT_BUILD_ROOT="${REPO_ROOT}/build/install-package"
if [[ "${1:-}" == "--standard-only" ]]; then
    STANDARD_ONLY=1
    DEFAULT_BUILD_ROOT="${REPO_ROOT}/build/install-package-standard"
    shift
fi
BUILD_ROOT="${1:-${DEFAULT_BUILD_ROOT}}"
STD="${FORGE_INSTALL_CXX_STANDARD:-23}"
GENERATOR="${FORGE_INSTALL_CMAKE_GENERATOR:-Ninja}"
CONFIG="${FORGE_INSTALL_CONFIG:-Debug}"
source "${REPO_ROOT}/scripts/lib/verification-scratch.sh"

case "${BUILD_ROOT}" in
    /*) ;;
    *) BUILD_ROOT="${REPO_ROOT}/${BUILD_ROOT}" ;;
esac

BUILD_ROOT="$(forge_prepare_verification_scratch \
    "${BUILD_ROOT}" "${REPO_ROOT}" install-package)"

FORGE_BUILD="${BUILD_ROOT}/forge-build"
PREFIX="${BUILD_ROOT}/prefix"
CONSUMER_BUILD="${BUILD_ROOT}/consumer-build"
CONSUMER_PREFIX="${BUILD_ROOT}/consumer-prefix"
OPTIONAL_CONSUMER_BUILD="${BUILD_ROOT}/optional-consumer-build"
SOURCE_INCLUDE_BUILD="${BUILD_ROOT}/source-include-build"
SOURCE_SUBDIR_BUILD="${BUILD_ROOT}/source-subdir-build"

MULTI_CONFIG=0
case "${GENERATOR}" in
    *'Multi-Config'*|*'Visual Studio'*|Xcode)
        MULTI_CONFIG=1
        ;;
esac

log() {
    printf '[install-package] %s\n' "$*"
}

configure_project() {
    local source_dir="$1"
    local build_dir="$2"
    shift 2

    local args=(-S "${source_dir}" -B "${build_dir}")
    if [[ -n "${GENERATOR}" ]]; then
        args+=(-G "${GENERATOR}")
    fi
    if [[ "${MULTI_CONFIG}" == "0" ]]; then
        args+=(-DCMAKE_BUILD_TYPE="${CONFIG}")
    fi
    cmake "${args[@]}" "$@"
}

build_project() {
    cmake --build "$1" --config "${CONFIG}" "${@:2}"
}

install_project() {
    cmake --install "$1" --config "${CONFIG}" "${@:2}"
}

run_built_executable() {
    local build_dir="$1"
    local executable="$2"
    local path="${build_dir}/${executable}"
    if [[ "${MULTI_CONFIG}" == "1" ]]; then
        path="${build_dir}/${CONFIG}/${executable}"
    fi
    "${path}"
}

run_hive_consumers() {
    local build_dir="$1"
    local prefix="$2"
    local surface
    shift 2
    local decision
    decision=$(<"${build_dir}/hive-consumers.txt")
    case "${decision}" in
        ENABLED)
            for surface in "$@"; do
                run_built_executable "${build_dir}" "${prefix}_hive_${surface}"
            done
            ;;
        PARTIAL) log "Hive consumers skipped for partial native support" ;;
        *) log "ERROR: missing or invalid Hive consumer decision"; exit 1 ;;
    esac
}

assert_no_os_probes() {
    local build_dir="$1"
    if grep -Eq '^FORGE_(RUNTIME_PROBE_FINGERPRINT|PROBE_LINUX_EPOLL_EVENTFD|PROBE_LINUX_IO_URING|PROBE_WINDOWS_IOCP):' \
            "${build_dir}/CMakeCache.txt"; then
        log "ERROR: OS IO probe cache entries found in ${build_dir}"
        exit 1
    fi
}

verify_standard_consumer() {
    local prefix="$1"
    local build_dir="$2"
    shift 2
    local export_prefix="${build_dir}-prefix"
    local targets_file="${export_prefix}/lib/cmake/CCForgeInstallConsumer/CCForgeInstallConsumerTargets.cmake"
    local guard_log="${build_dir}/language-guard-negative.log"

    configure_project "${REPO_ROOT}/test/install_consumer" "${build_dir}" \
        -DCMAKE_CXX_STANDARD="${STD}" \
        -DCMAKE_PREFIX_PATH="${prefix}" \
        -DCCFORGE_EXPECT_STANDARD_ONLY=ON "$@"
    if ! grep -Fq "CCForge_DIR:PATH=${prefix}/lib/cmake/CCForge" "${build_dir}/CMakeCache.txt"; then
        log "ERROR: standard-only consumer did not discover the requested prefix"
        exit 1
    fi
    assert_no_os_probes "${build_dir}"
    build_project "${build_dir}"
    run_built_executable "${build_dir}" ccforge_install_std_consumer
    run_hive_consumers "${build_dir}" ccforge_install std
    install_project "${build_dir}" --prefix "${export_prefix}"
    if [[ "$(grep -Fc 'INTERFACE_LINK_LIBRARIES "forge::std"' "${targets_file}")" -ne 1 ]] ||
            grep -Eq 'forge::forge|INTERFACE_LINK_LIBRARIES "(forge|forge_std)"' "${targets_file}" ||
            grep -Fq "${REPO_ROOT}" "${targets_file}"; then
        log "ERROR: standard-only consumer export did not preserve only forge::std"
        exit 1
    fi
    if [[ -f "${build_dir}/language-guard-negative.txt" ]]; then
        if build_project "${build_dir}" --target ccforge_install_guard_mismatch >"${guard_log}" 2>&1 ||
                ! grep -Fq "CC Forge language standard mismatch" "${guard_log}"; then
            log "ERROR: expected a language guard rejection; see ${guard_log}"
            exit 1
        fi
    else
        log "verified language guard usage requirements (C++23 to C++26 mismatch control unavailable)"
    fi
}

verify_standard_only_package() {
    local source_dir="${BUILD_ROOT}/standard-source"
    local build_dir="${BUILD_ROOT}/standard-build"
    local prefix="${BUILD_ROOT}/standard-prefix"
    local relocated_prefix="${BUILD_ROOT}/relocated-standard-prefix"
    local rejection_log="${BUILD_ROOT}/standard-reject-extensions.log"

    log "staging the package source without include/forge"
    mkdir -p "${source_dir}"
    cp -R "${REPO_ROOT}/CMakeLists.txt" "${REPO_ROOT}/forge.cmake" \
        "${REPO_ROOT}/cmake" "${REPO_ROOT}/backport" "${source_dir}/"
    configure_project "${source_dir}" "${build_dir}" \
        -DCMAKE_CXX_STANDARD="${STD}" \
        -DFORGE_ENABLE_EXTENSIONS=OFF -DFORGE_ENABLE_INSTALL=ON \
        -DFORGE_BUILD_EXAMPLES=OFF -DFORGE_BUILD_TESTS=OFF \
        -DCMAKE_INSTALL_PREFIX="${prefix}" -DCMAKE_INSTALL_LIBDIR=lib
    assert_no_os_probes "${build_dir}"
    build_project "${build_dir}"
    install_project "${build_dir}"
    if [[ -e "${prefix}/include" ]] ||
            [[ ! -f "${prefix}/lib/cmake/CCForge/ForgeExtensions.cmake" ]]; then
        log "ERROR: standard-only install must omit extension headers and retain CMake helpers"
        exit 1
    fi
    mv "${prefix}" "${relocated_prefix}"
    log "consuming the relocated standard-only prefix with the default mode"
    verify_standard_consumer "${relocated_prefix}" "${BUILD_ROOT}/standard-consumer-build"

    log "checking that a standard-only prefix rejects an explicit extension request"
    if configure_project "${REPO_ROOT}/test/install_consumer" "${BUILD_ROOT}/standard-reject-build" \
            -DCMAKE_CXX_STANDARD="${STD}" -DCMAKE_PREFIX_PATH="${relocated_prefix}" \
            -DFORGE_ENABLE_EXTENSIONS=ON >"${rejection_log}" 2>&1 ||
            ! grep -Fq 'FORGE_ENABLE_EXTENSIONS=ON requires the CC Forge extension headers' "${rejection_log}"; then
        log "ERROR: expected a missing extension headers diagnostic; see ${rejection_log}"
        exit 1
    fi
}

if [[ "${STANDARD_ONLY}" == "1" ]]; then
    verify_standard_only_package
    log "ok"
    exit 0
fi

verify_source_consumer() {
    local mode="$1"
    local build_dir="$2"
    local install_dir="${build_dir}-prefix"

    log "configuring source consumer in ${mode} mode"
    configure_project "${REPO_ROOT}/test/source_consumer" "${build_dir}" \
        -DCMAKE_CXX_STANDARD="${STD}" \
        -DCCFORGE_SOURCE_DIR="${REPO_ROOT}" \
        -DCCFORGE_CONSUMER_MODE="${mode}"

    log "building source consumer in ${mode} mode"
    build_project "${build_dir}"

    log "running source consumer in ${mode} mode"
    run_built_executable "${build_dir}" ccforge_source_consumer
    run_hive_consumers "${build_dir}" ccforge_source std forge

    log "installing source consumer export set in ${mode} mode"
    install_project "${build_dir}" --prefix "${install_dir}"

    local targets_file="${install_dir}/lib/cmake/CCForgeSourceConsumer/CCForgeSourceConsumerTargets.cmake"
    for public_target in forge::forge forge::std; do
        if [[ "$(grep -Fc "INTERFACE_LINK_LIBRARIES \"${public_target}\"" "${targets_file}")" -ne 1 ]]; then
            log "ERROR: source export in ${mode} mode did not preserve ${public_target}"
            exit 1
        fi
    done
    if grep -Eq 'INTERFACE_LINK_LIBRARIES "(forge|forge_std)"' "${targets_file}"; then
        log "ERROR: source export in ${mode} mode exposed an internal CC Forge target"
        exit 1
    fi
    if grep -Fq "${REPO_ROOT}" "${targets_file}"; then
        log "ERROR: source export in ${mode} mode embedded the CC Forge source path"
        exit 1
    fi
}

verify_source_consumer INCLUDE "${SOURCE_INCLUDE_BUILD}"
verify_source_consumer ADD_SUBDIRECTORY "${SOURCE_SUBDIR_BUILD}"

for foreign_target in std forge; do
    collision_build="${BUILD_ROOT}/source-collision-${foreign_target}-build"
    collision_log="${BUILD_ROOT}/source-collision-${foreign_target}.log"
    log "checking source target collision for forge::${foreign_target}"
    if configure_project "${REPO_ROOT}/test/source_consumer" "${collision_build}" \
            -DCMAKE_CXX_STANDARD="${STD}" \
            -DCCFORGE_SOURCE_DIR="${REPO_ROOT}" \
            -DCCFORGE_CONSUMER_MODE=INCLUDE \
            -DCCFORGE_FOREIGN_TARGET="${foreign_target}" \
            >"${collision_log}" 2>&1; then
        log "ERROR: foreign forge::${foreign_target} target was silently reused"
        exit 1
    fi
    if ! grep -Fq \
            "CC Forge cannot create forge::${foreign_target} because that target already exists" \
            "${collision_log}"; then
        log "ERROR: forge::${foreign_target} collision failed for an unexpected reason"
        exit 1
    fi
done

log "configuring Forge package build"
configure_project "${REPO_ROOT}" "${FORGE_BUILD}" \
    -DCMAKE_CXX_STANDARD="${STD}" \
    -DFORGE_BUILD_EXAMPLES=OFF \
    -DFORGE_BUILD_TESTS=OFF \
    -DFORGE_ENABLE_INSTALL=ON \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}" -DCMAKE_INSTALL_LIBDIR=lib

log "building Forge package build"
build_project "${FORGE_BUILD}"

log "installing to ${PREFIX}"
install_project "${FORGE_BUILD}"

log "checking installed header-only SameMinorVersion compatibility"
cmake -DFORGE_INSTALL_PREFIX="${PREFIX}" \
    -P "${REPO_ROOT}/test/check_installed_package_version.cmake"

if find "${PREFIX}" \( -iname '*gtest*' -o -iname '*googletest*' \) \
        -print -quit | grep -q .; then
    log "ERROR: GoogleTest artifacts leaked into the CC Forge install prefix"
    exit 1
fi

log "configuring optional consumer without Threads"
configure_project "${REPO_ROOT}/test/install_optional_consumer" \
    "${OPTIONAL_CONSUMER_BUILD}" \
    -DCMAKE_PREFIX_PATH="${PREFIX}"

log "configuring external consumer with find_package(CCForge CONFIG)"
configure_project "${REPO_ROOT}/test/install_consumer" "${CONSUMER_BUILD}" \
    -DCMAKE_CXX_STANDARD="${STD}" \
    -DCMAKE_PREFIX_PATH="${PREFIX}"

log "building external consumer"
build_project "${CONSUMER_BUILD}"

log "installing external consumer export set"
install_project "${CONSUMER_BUILD}" --prefix "${CONSUMER_PREFIX}"

log "running external consumer"
run_built_executable "${CONSUMER_BUILD}" ccforge_install_consumer
run_built_executable "${CONSUMER_BUILD}" ccforge_install_std_consumer
run_hive_consumers "${CONSUMER_BUILD}" ccforge_install std forge

log "consuming the full prefix with extensions disabled"
verify_standard_consumer "${PREFIX}" "${BUILD_ROOT}/full-prefix-standard-consumer-build" \
    -DFORGE_ENABLE_EXTENSIONS=OFF
verify_standard_only_package

log "ok"

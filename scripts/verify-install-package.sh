#!/usr/bin/env bash
#
# Verify source-tree and installed-package consumption without exposing CC
# Forge's test-only GoogleTest dependency. This is intentionally separate from
# default CTest because it performs multiple configure/build cycles.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="${1:-${REPO_ROOT}/build/install-package}"
STD="${FORGE_INSTALL_CXX_STANDARD:-23}"
GENERATOR="${FORGE_INSTALL_CMAKE_GENERATOR:-Ninja}"

case "${BUILD_ROOT}" in
    /*) ;;
    *) BUILD_ROOT="${REPO_ROOT}/${BUILD_ROOT}" ;;
esac

FORGE_BUILD="${BUILD_ROOT}/forge-build"
PREFIX="${BUILD_ROOT}/prefix"
CONSUMER_BUILD="${BUILD_ROOT}/consumer-build"
CONSUMER_PREFIX="${BUILD_ROOT}/consumer-prefix"
OPTIONAL_CONSUMER_BUILD="${BUILD_ROOT}/optional-consumer-build"
SOURCE_INCLUDE_BUILD="${BUILD_ROOT}/source-include-build"
SOURCE_SUBDIR_BUILD="${BUILD_ROOT}/source-subdir-build"

GENERATOR_ARGS=()
if [[ -n "${GENERATOR}" ]]; then
    GENERATOR_ARGS=(-G "${GENERATOR}")
fi

log() {
    printf '[install-package] %s\n' "$*"
}

rm -rf "${BUILD_ROOT}"
mkdir -p "${BUILD_ROOT}"

verify_source_consumer() {
    local mode="$1"
    local build_dir="$2"

    log "configuring source consumer in ${mode} mode"
    cmake -S "${REPO_ROOT}/test/source_consumer" -B "${build_dir}" \
        "${GENERATOR_ARGS[@]}" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_CXX_STANDARD="${STD}" \
        -DCCFORGE_SOURCE_DIR="${REPO_ROOT}" \
        -DCCFORGE_CONSUMER_MODE="${mode}"

    log "building source consumer in ${mode} mode"
    cmake --build "${build_dir}"

    log "running source consumer in ${mode} mode"
    "${build_dir}/ccforge_source_consumer"
}

verify_source_consumer INCLUDE "${SOURCE_INCLUDE_BUILD}"
verify_source_consumer ADD_SUBDIRECTORY "${SOURCE_SUBDIR_BUILD}"

log "configuring Forge package build"
cmake -S "${REPO_ROOT}" -B "${FORGE_BUILD}" "${GENERATOR_ARGS[@]}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_STANDARD="${STD}" \
    -DFORGE_BUILD_EXAMPLES=OFF \
    -DFORGE_BUILD_TESTS=OFF \
    -DFORGE_ENABLE_INSTALL=ON \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}"

log "building Forge package build"
cmake --build "${FORGE_BUILD}"

log "installing to ${PREFIX}"
cmake --install "${FORGE_BUILD}"

if find "${PREFIX}" \( -iname '*gtest*' -o -iname '*googletest*' \) \
        -print -quit | grep -q .; then
    log "ERROR: GoogleTest artifacts leaked into the CC Forge install prefix"
    exit 1
fi

log "configuring optional consumer without Threads"
cmake -S "${REPO_ROOT}/test/install_optional_consumer" \
    -B "${OPTIONAL_CONSUMER_BUILD}" \
    "${GENERATOR_ARGS[@]}" \
    -DCMAKE_PREFIX_PATH="${PREFIX}"

log "configuring external consumer with find_package(CCForge CONFIG)"
cmake -S "${REPO_ROOT}/test/install_consumer" -B "${CONSUMER_BUILD}" \
    "${GENERATOR_ARGS[@]}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_STANDARD="${STD}" \
    -DCMAKE_PREFIX_PATH="${PREFIX}"

log "building external consumer"
cmake --build "${CONSUMER_BUILD}"

log "installing external consumer export set"
cmake --install "${CONSUMER_BUILD}" --prefix "${CONSUMER_PREFIX}"

log "running external consumer"
"${CONSUMER_BUILD}/ccforge_install_consumer"
"${CONSUMER_BUILD}/ccforge_install_std_consumer"

log "ok"

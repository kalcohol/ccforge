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
    local install_dir="${build_dir}-prefix"

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

    log "installing source consumer export set in ${mode} mode"
    cmake --install "${build_dir}" --prefix "${install_dir}"

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
    if cmake -S "${REPO_ROOT}/test/source_consumer" -B "${collision_build}" \
            "${GENERATOR_ARGS[@]}" \
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

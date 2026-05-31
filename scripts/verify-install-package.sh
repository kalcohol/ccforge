#!/usr/bin/env bash
#
# Verify that CC Forge can be installed and consumed from an install prefix via
# find_package(CCForge CONFIG). This is intentionally separate from default
# CTest because it configures, installs, and builds a second external project.

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

GENERATOR_ARGS=()
if [[ -n "${GENERATOR}" ]]; then
    GENERATOR_ARGS=(-G "${GENERATOR}")
fi

log() {
    printf '[install-package] %s\n' "$*"
}

rm -rf "${BUILD_ROOT}"
mkdir -p "${BUILD_ROOT}"

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

log "configuring external consumer with find_package(CCForge CONFIG)"
cmake -S "${REPO_ROOT}/test/install_consumer" -B "${CONSUMER_BUILD}" \
    "${GENERATOR_ARGS[@]}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_STANDARD="${STD}" \
    -DCMAKE_PREFIX_PATH="${PREFIX}"

log "building external consumer"
cmake --build "${CONSUMER_BUILD}"

log "running external consumer"
"${CONSUMER_BUILD}/ccforge_install_consumer"

log "ok"

#!/usr/bin/env bash
#
# Optional local spike for comparing Forge's execution backport with a locally
# provided NVIDIA stdexec checkout. This script deliberately does not fetch or
# vendor stdexec, and it is not a default verification gate.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="${1:-${REPO_ROOT}/build/stdexec-feasibility}"
CXX="${CXX:-c++}"
STD="${FORGE_STDEXEC_CXX_STANDARD:-23}"

case "${BUILD_ROOT}" in
    /*) ;;
    *) BUILD_ROOT="${REPO_ROOT}/${BUILD_ROOT}" ;;
esac

log() {
    printf '[stdexec-feasibility] %s\n' "$*"
}

if [[ -z "${STDEXEC_ROOT:-}" ]]; then
    log "skipped: set STDEXEC_ROOT=/path/to/stdexec to run this optional probe"
    exit 77
fi

STDEXEC_INCLUDE="${STDEXEC_ROOT}/include"
if [[ ! -f "${STDEXEC_INCLUDE}/stdexec/execution.hpp" ]]; then
    log "error: ${STDEXEC_INCLUDE}/stdexec/execution.hpp not found"
    exit 2
fi

rm -rf "${BUILD_ROOT}"
mkdir -p "${BUILD_ROOT}"

cat > "${BUILD_ROOT}/stdexec_smoke.cpp" <<'CPP'
#include <stdexec/execution.hpp>

int main() {
    auto s = stdexec::just();
    (void)s;
}
CPP

cat > "${BUILD_ROOT}/forge_execution_smoke.cpp" <<'CPP'
#include <execution>
#include <tuple>

int main() {
    auto result = std::execution::sync_wait(std::execution::just(42));
    return result && std::get<0>(*result) == 42 ? 0 : 1;
}
CPP

log "compiling stdexec smoke"
"${CXX}" -std=c++"${STD}" -I"${STDEXEC_INCLUDE}" \
    "${BUILD_ROOT}/stdexec_smoke.cpp" -pthread -o "${BUILD_ROOT}/stdexec_smoke"

log "compiling Forge execution smoke"
"${CXX}" -std=c++"${STD}" -I"${REPO_ROOT}/backport/cpp26" -I"${REPO_ROOT}/include" \
    "${BUILD_ROOT}/forge_execution_smoke.cpp" -pthread -o "${BUILD_ROOT}/forge_execution_smoke"

log "running Forge execution smoke"
"${BUILD_ROOT}/forge_execution_smoke"

log "ok: both tiny surfaces compile; deeper stdexec compatibility requires a separate adapter taskbook"


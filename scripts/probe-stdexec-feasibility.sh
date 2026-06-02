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
    log "result=skipped reason=missing-STDEXEC_ROOT"
    log "set STDEXEC_ROOT=/path/to/stdexec to run this optional probe"
    exit 77
fi

STDEXEC_INCLUDE="${STDEXEC_ROOT}/include"
if [[ ! -f "${STDEXEC_INCLUDE}/stdexec/execution.hpp" ]]; then
    log "result=failed reason=missing-stdexec-header"
    exit 2
fi

rm -rf "${BUILD_ROOT}"
mkdir -p "${BUILD_ROOT}"

COMMON_FLAGS=(-std=c++"${STD}" -pthread)
FORGE_INCLUDES=(-I"${REPO_ROOT}/backport" -I"${REPO_ROOT}/include")
STDEXEC_INCLUDES=(-I"${STDEXEC_INCLUDE}")

compile_only() {
    local name="$1"
    local source="$2"
    shift 2

    log "check=${name} phase=compile"
    "${CXX}" "${COMMON_FLAGS[@]}" "$@" "${source}" -o "${BUILD_ROOT}/${name}"
    log "check=${name} result=compiled"
}

compile_and_run() {
    local name="$1"
    local source="$2"
    shift 2

    compile_only "${name}" "${source}" "$@"
    log "check=${name} phase=run"
    "${BUILD_ROOT}/${name}"
    log "check=${name} result=passed"
}

cat > "${BUILD_ROOT}/stdexec_just_smoke.cpp" <<'CPP'
#include <stdexec/execution.hpp>

int main() {
    auto s = stdexec::just();
    (void)s;
}
CPP

cat > "${BUILD_ROOT}/forge_execution_sync_wait.cpp" <<'CPP'
#include <execution>
#include <tuple>

int main() {
    auto result = std::execution::sync_wait(std::execution::just(42));
    return result && std::get<0>(*result) == 42 ? 0 : 1;
}
CPP

cat > "${BUILD_ROOT}/forge_wait_result_typed_error.cpp" <<'CPP'
#include <forge/wait_result.hpp>

#include <execution>

struct typed_error {
    int code{};
};

struct typed_error_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        return std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_error_t(typed_error),
            std::execution::set_stopped_t()>{};
    }

    auto get_env() const noexcept {
        return std::execution::empty_env{};
    }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;

        void start() & noexcept {
            std::execution::set_error(static_cast<R&&>(rcvr), typed_error{7});
        }
    };

    template<class R>
    auto connect(R rcvr) noexcept -> op<R> {
        return op<R>{static_cast<R&&>(rcvr)};
    }
};

int main() {
    auto result = forge::wait_result(typed_error_sender{});
    auto* error = result.template error_if<typed_error>();
    return result.has_error() && error != nullptr && error->code == 7 ? 0 : 1;
}
CPP

cat > "${BUILD_ROOT}/forge_erased_sender_typed_error.cpp" <<'CPP'
#include <forge/erased_sender.hpp>
#include <forge/wait_result.hpp>

#include <execution>

struct typed_error {
    int code{};
};

struct typed_error_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        return std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_error_t(typed_error),
            std::execution::set_stopped_t()>{};
    }

    auto get_env() const noexcept {
        return std::execution::empty_env{};
    }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;

        void start() & noexcept {
            std::execution::set_error(static_cast<R&&>(rcvr), typed_error{9});
        }
    };

    template<class R>
    auto connect(R rcvr) noexcept -> op<R> {
        return op<R>{static_cast<R&&>(rcvr)};
    }
};

int main() {
    using cs = std::execution::completion_signatures<
        std::execution::set_value_t(int),
        std::execution::set_error_t(typed_error),
        std::execution::set_stopped_t()>;
    forge::erased_sender<cs> sender{typed_error_sender{}};
    auto result = forge::wait_result(std::move(sender));
    auto* error = result.template error_if<typed_error>();
    return result.has_error() && error != nullptr && error->code == 9 ? 0 : 1;
}
CPP

cat > "${BUILD_ROOT}/forge_any_scheduler.cpp" <<'CPP'
#include <forge/any_scheduler.hpp>
#include <forge/static_thread_pool.hpp>

#include <execution>
#include <tuple>

int main() {
    static_assert(std::execution::scheduler<forge::any_scheduler>);

    forge::static_thread_pool pool{1};
    forge::any_scheduler scheduler{pool.get_scheduler()};
    auto result = std::execution::sync_wait(std::execution::schedule(scheduler));
    pool.shutdown();
    pool.wait();
    return result ? 0 : 1;
}
CPP

cat > "${BUILD_ROOT}/forge_receiver_stop_env.cpp" <<'CPP'
#include <forge/wait_result.hpp>

#include <execution>
#include <tuple>

struct stop_env_probe_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        return std::execution::completion_signatures<
            std::execution::set_value_t(bool)>{};
    }

    auto get_env() const noexcept {
        return std::execution::empty_env{};
    }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;

        void start() & noexcept {
            auto env = std::execution::get_env(rcvr);
            auto token = std::execution::get_stop_token(env);
            std::execution::set_value(
                static_cast<R&&>(rcvr),
                token.stop_possible());
        }
    };

    template<class R>
    auto connect(R rcvr) noexcept -> op<R> {
        return op<R>{static_cast<R&&>(rcvr)};
    }
};

int main() {
    auto result = forge::wait_result(stop_env_probe_sender{});
    return result.has_value() && std::get<0>(result.value()) ? 0 : 1;
}
CPP

compile_only \
    "stdexec_just_smoke" \
    "${BUILD_ROOT}/stdexec_just_smoke.cpp" \
    "${STDEXEC_INCLUDES[@]}"

compile_and_run \
    "forge_execution_sync_wait" \
    "${BUILD_ROOT}/forge_execution_sync_wait.cpp" \
    "${FORGE_INCLUDES[@]}"

compile_and_run \
    "forge_wait_result_typed_error" \
    "${BUILD_ROOT}/forge_wait_result_typed_error.cpp" \
    "${FORGE_INCLUDES[@]}"

compile_and_run \
    "forge_erased_sender_typed_error" \
    "${BUILD_ROOT}/forge_erased_sender_typed_error.cpp" \
    "${FORGE_INCLUDES[@]}"

compile_and_run \
    "forge_any_scheduler" \
    "${BUILD_ROOT}/forge_any_scheduler.cpp" \
    "${FORGE_INCLUDES[@]}"

compile_and_run \
    "forge_receiver_stop_env" \
    "${BUILD_ROOT}/forge_receiver_stop_env.cpp" \
    "${FORGE_INCLUDES[@]}"

log "result=passed"
log "ok: named checks passed; stdexec remains an optional reference probe, not a native handoff lane"

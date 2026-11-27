#ifdef FORGE_TEST_FORGE_BULK_FALLBACK_ADVERTISED
#include <execution>
#if defined(FORGE_NATIVE_EXECUTION_POLICY_OBJECTS) || \
    !defined(__cpp_lib_execution) || __cpp_lib_execution < 201603L
#error The all-native library must advertise execution policies for the Forge fallback
#endif
int main() {}
#else
#include <forge/bulk.hpp>
#include "native_policy_config.hpp"

#if !defined(FORGE_TEST_EXPECT_FORGE_BULK)
#error The Forge bulk probe requires an availability expectation
#endif

#ifdef FORGE_TEST_FORGE_BULK_ABSENT
using forge::FORGE_TEST_FORGE_BULK_ABSENT;
#endif

#if FORGE_TEST_EXPECT_FORGE_BULK
namespace ex = std::execution;

namespace {

using source = decltype(ex::just(42));

struct each_index {
    void operator()(int, int&) const noexcept {}
};

struct each_chunk {
    void operator()(int, int, int&) const noexcept {}
};

template<class Cpo, class Function>
concept accepts_explicit_const_shape = requires(const Cpo& cpo) {
    cpo.template operator()<source, const int, Function>(ex::just(42), 2, Function{});
    ex::just(42) | cpo.template operator()<const int, Function>(2, Function{});
};

static_assert(accepts_explicit_const_shape<decltype(forge::bulk), each_index>);
static_assert(accepts_explicit_const_shape<decltype(forge::bulk_chunked), each_chunk>);
static_assert(accepts_explicit_const_shape<decltype(forge::bulk_unchunked), each_index>);

template<class Cpo, class Function>
bool calls_link(const Cpo& cpo, Function) {
    auto direct = cpo.template operator()<source, const int, Function>(ex::just(42), 2, Function{});
    auto piped = ex::just(42) | cpo.template operator()<const int, Function>(2, Function{});
    return std::this_thread::sync_wait(std::move(direct)).has_value() &&
           std::this_thread::sync_wait(std::move(piped)).has_value();
}

} // namespace
#endif

int main() {
#if FORGE_TEST_EXPECT_FORGE_BULK
    return !(calls_link(forge::bulk, each_index{}) &&
             calls_link(forge::bulk_chunked, each_chunk{}) &&
             calls_link(forge::bulk_unchunked, each_index{}));
#endif
}
#endif

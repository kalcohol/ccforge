#include <forge/bulk.hpp>
#include <forge/execution_extensions.hpp>

#include <concepts>
#include <execution>

namespace ex = std::execution;

static_assert(&forge::sync_wait == &std::this_thread::sync_wait);
static_assert(std::same_as<forge::empty_env, ex::env<>>);
static_assert(ex::sender<decltype(forge::split(ex::just(42)))>);

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
    cpo.template operator()<const int, Function>(2, Function{});
};

static_assert(accepts_explicit_const_shape<decltype(forge::bulk), each_index>);
static_assert(accepts_explicit_const_shape<decltype(forge::bulk_chunked), each_chunk>);
static_assert(accepts_explicit_const_shape<decltype(forge::bulk_unchunked), each_index>);

} // namespace

int main() {}

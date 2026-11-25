#include <execution>
#include <forge/static_thread_pool.hpp>
#include <forge/strand.hpp>
#include <utility>

namespace ex = std::execution;
#if FORGE_AFFINE_FORGE_SCHEDULER == 0
using scheduler = forge::static_thread_pool::scheduler;
#else
using scheduler = forge::strand::scheduler;
#endif
struct receiver {
    using receiver_concept = ex::receiver_tag;
    void set_value() && noexcept {}
    template<class E> void set_error(E&&) && noexcept {}
    void set_stopped() && noexcept {}
    auto get_env() const noexcept {
        auto start = ex::prop{ex::get_start_scheduler, scheduler{}};
#ifdef FORGE_ADAPTOR_POSITIVE
        return ex::env{start, ex::prop{std::get_stop_token, std::inplace_stop_token{}}};
#elif FORGE_AFFINE_FORGE_TOKEN == 1
        return ex::env{start, ex::prop{std::get_stop_token, std::never_stop_token{}}};
#else
        return start;
#endif
    }
};
static_assert(!ex::sender_in<decltype(ex::affine(ex::just())), ex::env<>>);
int main() {
    auto op = ex::connect(ex::affine(ex::just()), receiver{});
    (void)op;
}

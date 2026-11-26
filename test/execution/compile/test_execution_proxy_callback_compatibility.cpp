#include <concepts>
#include <exception>
#include <execution>

#include "parallel_scheduler_replacement_fixture.hpp"

struct probe_token {
    // False at run time, not a compile-time unstoppable token.
    bool stop_possible() const noexcept { return false; }
    bool stop_requested() const noexcept { return false; }
    bool operator==(const probe_token&) const noexcept = default;

    template<class Fn>
    struct callback_type {
#if defined(FORGE_PROXY_CALLBACK_CONST_LVALUE)
        callback_type(probe_token&&, Fn) noexcept {}
        callback_type(probe_token&, Fn) noexcept {}
        callback_type(const probe_token&&, Fn) noexcept {}
#endif
#if defined(FORGE_PROXY_QUERY_VALID)
        callback_type(const probe_token&, Fn) noexcept {}
#else
        callback_type(const probe_token&, Fn) = delete;
#endif
    };
};

// Syntactic satisfaction alone does not check the callback constructor.
static_assert(std::stoppable_token<probe_token> && !std::unstoppable_token<probe_token>);

#if defined(FORGE_PROXY_CALLBACK_CONST_LVALUE)
using callback_fn = void (*)() noexcept;
using callback = probe_token::callback_type<callback_fn>;
static_assert(std::constructible_from<callback, probe_token, callback_fn>);
static_assert(std::constructible_from<callback, probe_token&, callback_fn>);
static_assert(std::constructible_from<callback, const probe_token, callback_fn>);
#if defined(FORGE_PROXY_QUERY_VALID)
static_assert(std::constructible_from<callback, const probe_token&, callback_fn>);
#else
static_assert(!std::constructible_from<callback, const probe_token&, callback_fn>);
#endif
#endif

struct receiver {
    auto get_env() const noexcept {
        return std::execution::prop{std::get_stop_token, probe_token{}};
    }

    void set_value() && noexcept {}
    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}
};

int main() {
    test_execution::replacement_fixture::ordinary_proxy<receiver> proxy{receiver{}};
    const std::execution::parallel_scheduler_replacement::receiver_proxy& base = proxy;
    auto token = base.try_query<std::inplace_stop_token>(std::get_stop_token);
    return token && !token->stop_possible() ? 0 : 1;
}

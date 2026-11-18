#include "awaitable_conformance_types.hpp"
#include <forge/execution_extensions.hpp>
namespace ex = std::execution;
namespace a = awaitable_tests;
#ifndef FORGE_AWAITABLE_CASE
#define FORGE_AWAITABLE_CASE 1
#endif
struct bad_member {
    template<class P>
    auto as_awaitable(P&) const noexcept
#ifdef FORGE_AWAITABLE_POSITIVE
        -> a::int_awaiter { return {}; }
#else
        -> int { return 1; }
#endif
};
struct other_promise {
#ifdef FORGE_AWAITABLE_POSITIVE
    auto unhandled_stopped() noexcept -> std::coroutine_handle<> { return std::noop_coroutine(); }
#else
    int unhandled_stopped() noexcept { return 0; }
#endif
};
struct limited_receiver {
    using receiver_concept = ex::receiver_tag;
    void set_value(int) && noexcept {}
#if FORGE_AWAITABLE_CASE != 6 || defined(FORGE_AWAITABLE_POSITIVE)
    void set_error(std::exception_ptr) && noexcept {}
#endif
#if FORGE_AWAITABLE_CASE != 7 || defined(FORGE_AWAITABLE_POSITIVE)
    void set_stopped() && noexcept {}
#endif
};
int main() {
#if FORGE_AWAITABLE_CASE == 1
    a::promise promise;
    auto result = ex::as_awaitable(bad_member{}, promise);
#elif FORGE_AWAITABLE_CASE == 2
    a::promise promise; // No unhandled_stopped, no hidden exception fallback.
#ifdef FORGE_AWAITABLE_POSITIVE
    auto result = ex::as_awaitable(ex::just(1), promise);
#else
    auto result = ex::as_awaitable(ex::just_stopped(), promise);
#endif
#elif FORGE_AWAITABLE_CASE == 3
#ifdef FORGE_AWAITABLE_POSITIVE
    a::mixin_promise result; // CRTP argument was incomplete at the base specifier.
#else
    ex::with_awaitable_senders<int> result;
#endif
#elif FORGE_AWAITABLE_CASE == 4
    a::mixin_promise result;
    result.set_continuation(std::coroutine_handle<other_promise>{});
#elif FORGE_AWAITABLE_CASE == 5
    ex::counting_scope scope;
#ifdef FORGE_AWAITABLE_POSITIVE
    ex::spawn(ex::just(), scope.get_token());
#else
    ex::spawn(a::void_awaiter{}, scope.get_token());
#endif
    forge::sync_wait(scope.join());
    int result = 0;
#elif FORGE_AWAITABLE_CASE == 6 || FORGE_AWAITABLE_CASE == 7
    auto result = ex::connect(a::int_awaiter{}, limited_receiver{});
#elif FORGE_AWAITABLE_CASE == 8
#ifdef FORGE_AWAITABLE_POSITIVE
    ex::with_awaitable_senders<a::promise> result;
#else
    ex::with_awaitable_senders<const a::promise> result;
#endif
#elif FORGE_AWAITABLE_CASE == 9
#ifdef FORGE_AWAITABLE_POSITIVE
    ex::with_awaitable_senders<a::promise> result;
#else
    ex::with_awaitable_senders<a::promise&> result;
#endif
#endif
    (void)result;
}

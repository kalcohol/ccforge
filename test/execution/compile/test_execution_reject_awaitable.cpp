#include "awaitable_conformance_types.hpp"
#include <forge/execution_extensions.hpp>
#include <memory>
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
#if FORGE_AWAITABLE_CASE == 11
using prepared_sender = decltype(ex::just(std::unique_ptr<int>{}));

struct bridge_adaptor_source {
    using sender_concept = ex::sender_tag;
    prepared_sender* prepared;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(std::unique_ptr<int>)> { return {}; }

    auto get_env() const noexcept {
        auto adaptor = [prepared = prepared](auto&&) noexcept -> decltype(auto) {
#ifdef FORGE_AWAITABLE_POSITIVE
            return prepared_sender(std::move(*prepared));
#else
            return *prepared;
#endif
        };
        return ex::prop(ex::get_await_completion_adaptor, adaptor);
    }
};
#endif
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
#elif FORGE_AWAITABLE_CASE == 10
    a::promise promise;
    auto source = ex::just(1);
#ifdef FORGE_AWAITABLE_POSITIVE
    auto result = ex::as_awaitable(std::move(source), promise);
    static_assert(std::same_as<decltype(result.await_resume()), int>);
#else
    auto result = ex::as_awaitable(source, promise);
#endif
#elif FORGE_AWAITABLE_CASE == 11
    a::promise promise;
    auto prepared = ex::just(std::make_unique<int>(1));
    auto result = ex::as_awaitable(bridge_adaptor_source{&prepared}, promise);
#ifdef FORGE_AWAITABLE_POSITIVE
    static_assert(std::same_as<decltype(result.await_resume()), std::unique_ptr<int>>);
#endif
#endif
    (void)result;
}

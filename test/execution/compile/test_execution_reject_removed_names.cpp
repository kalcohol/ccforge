#include <execution>

#if FORGE_REMOVED_NAME == 34 || FORGE_REMOVED_NAME == 35
#include <forge/execution_extensions.hpp>
#endif

#include <concepts>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace ex = std::execution;

struct receiver {
    using receiver_concept = ex::receiver_tag;
    void set_value(int) && noexcept {}
};

int main() {
#if FORGE_REMOVED_NAME == 1
#if FORGE_REMOVAL_CONTROL
    static_assert(std::same_as<decltype(ex::get_env(receiver{})), ex::env<>>);
#else
    using removed [[maybe_unused]] = ex::empty_env;
#endif
#elif FORGE_REMOVED_NAME == 2
#if FORGE_REMOVAL_CONTROL
    using property = decltype(ex::prop{ex::get_scheduler, ex::inline_scheduler{}});
    static_assert(std::same_as<decltype(ex::get_scheduler(std::declval<const property&>())),
        ex::inline_scheduler>);
#else
    using removed [[maybe_unused]] = decltype(ex::make_prop(ex::get_scheduler, ex::inline_scheduler{}));
#endif
#elif FORGE_REMOVED_NAME == 3
#if FORGE_REMOVAL_CONTROL
    using source = decltype(ex::continues_on(ex::just(42), ex::inline_scheduler{}));
    static_assert(ex::sender<source>);
#else
    using removed [[maybe_unused]] = decltype(ex::transfer_just(ex::inline_scheduler{}, 42));
#endif
#elif FORGE_REMOVED_NAME == 4
#if FORGE_REMOVAL_CONTROL
    using domain = decltype(ex::get_domain(ex::env<>{}));
    static_assert(std::same_as<domain, ex::default_domain>);
#else
    using removed [[maybe_unused]] = ex::env_domain_t<ex::env<>>;
#endif
#elif FORGE_REMOVED_NAME == 5
#if FORGE_REMOVAL_CONTROL
    using domain = decltype(ex::get_domain(ex::get_env(std::declval<const receiver&>())));
    static_assert(std::same_as<domain, ex::default_domain>);
#else
    using removed [[maybe_unused]] = ex::receiver_domain_t<receiver>;
#endif
#elif FORGE_REMOVED_NAME == 6
    using sender = decltype(ex::just(42));
#if FORGE_REMOVAL_CONTROL
    using domain = decltype(ex::get_domain(ex::get_env(std::declval<sender>())));
    static_assert(std::same_as<domain, ex::default_domain>);
#else
    using removed [[maybe_unused]] = ex::sender_domain_t<sender>;
#endif
#elif FORGE_REMOVED_NAME == 7
#if FORGE_REMOVAL_CONTROL
    using source = decltype(ex::read_env(std::get_stop_token));
    static_assert(std::same_as<ex::completion_signatures_of_t<source, ex::env<>>,
        ex::completion_signatures<ex::set_value_t(std::never_stop_token)>>);
#else
    using removed [[maybe_unused]] = ex::read_env_t;
#endif
#elif FORGE_REMOVED_NAME == 8
    using sender = decltype(ex::just(42));
#if FORGE_REMOVAL_CONTROL
    using values = ex::value_types_of_t<sender, ex::env<>>;
    static_assert(std::same_as<values, std::variant<std::tuple<int>>>);
#else
    using removed [[maybe_unused]] = ex::value_types_of<sender, ex::env<>>;
#endif
#elif FORGE_REMOVED_NAME == 9
    using sender = decltype(ex::just_error(42));
#if FORGE_REMOVAL_CONTROL
    using errors = ex::error_types_of_t<sender, ex::env<>>;
    static_assert(std::same_as<errors, std::variant<int>>);
#else
    using removed [[maybe_unused]] = ex::error_types_of<sender, ex::env<>>;
#endif
#elif FORGE_REMOVED_NAME == 10
    using sender = decltype(ex::just_stopped());
#if FORGE_REMOVAL_CONTROL
    static_assert(ex::sends_stopped<sender, ex::env<>>);
#else
    using removed [[maybe_unused]] = decltype(ex::sends_stopped_v<sender, ex::env<>>);
#endif
#elif FORGE_REMOVED_NAME == 11
#if FORGE_REMOVAL_CONTROL
    static_assert(ex::receiver<receiver> && requires { ex::set_value(receiver{}, 42); });
#else
    using removed [[maybe_unused]] = decltype(ex::receiver_of<receiver, ex::completion_signatures<ex::set_value_t(int)>>);
#endif
#elif FORGE_REMOVED_NAME == 12
#if FORGE_REMOVAL_CONTROL
    using result = decltype(std::this_thread::sync_wait_with_variant(ex::just(42)));
    using value = std::variant<std::tuple<int>>;
    static_assert(std::same_as<result, std::optional<value>>);
#else
    using removed [[maybe_unused]] = decltype(ex::sync_wait_with_variant(ex::just(42)));
#endif
#elif FORGE_REMOVED_NAME == 13
#if FORGE_REMOVAL_CONTROL
    static_assert(std::forwarding_query(std::get_stop_token));
#else
    using removed [[maybe_unused]] = decltype(ex::forwarding_query);
#endif
#elif FORGE_REMOVED_NAME == 14
#if FORGE_REMOVAL_CONTROL
    static_assert(std::same_as<std::forwarding_query_t, std::remove_cv_t<decltype(std::forwarding_query)>>);
#else
    using removed [[maybe_unused]] = ex::forwarding_query_t;
#endif
#elif FORGE_REMOVED_NAME == 15
#if FORGE_REMOVAL_CONTROL
    static_assert(std::same_as<decltype(std::get_stop_token(ex::env<>{})), std::never_stop_token>);
#else
    using removed [[maybe_unused]] = decltype(ex::get_stop_token);
#endif
#elif FORGE_REMOVED_NAME == 16
#if FORGE_REMOVAL_CONTROL
    static_assert(std::same_as<std::get_stop_token_t, std::remove_cv_t<decltype(std::get_stop_token)>>);
#else
    using removed [[maybe_unused]] = ex::get_stop_token_t;
#endif
#elif FORGE_REMOVED_NAME == 17
#if FORGE_REMOVAL_CONTROL
    using allocator = std::allocator<int>;
    using environment = decltype(ex::prop{std::get_allocator, allocator{}});
    static_assert(std::same_as<std::remove_cvref_t<decltype(std::get_allocator(
        std::declval<const environment&>()))>, allocator>);
#else
    using removed [[maybe_unused]] = decltype(ex::get_allocator);
#endif
#elif FORGE_REMOVED_NAME == 18
#if FORGE_REMOVAL_CONTROL
    static_assert(std::same_as<std::get_allocator_t, std::remove_cv_t<decltype(std::get_allocator)>>);
#else
    using removed [[maybe_unused]] = ex::get_allocator_t;
#endif
#elif FORGE_REMOVED_NAME == 19
#if FORGE_REMOVAL_CONTROL
    static_assert(std::same_as<std::stop_token_of_t<ex::env<>>, std::never_stop_token>);
#else
    using removed [[maybe_unused]] = ex::stop_token_of_t<ex::env<>>;
#endif
#elif FORGE_REMOVED_NAME == 20
#if FORGE_REMOVAL_CONTROL
    static_assert(std::same_as<ex::receiver_tag, receiver::receiver_concept>);
#else
    using removed [[maybe_unused]] = ex::receiver_t;
#endif
#elif FORGE_REMOVED_NAME == 21
#if FORGE_REMOVAL_CONTROL
    using source = decltype(ex::just(42));
    static_assert(std::same_as<ex::sender_tag, source::sender_concept>);
#else
    using removed [[maybe_unused]] = ex::sender_t;
#endif
#elif FORGE_REMOVED_NAME == 22
#if FORGE_REMOVAL_CONTROL
    using operation = ex::connect_result_t<decltype(ex::just(42)), receiver>;
    static_assert(std::same_as<ex::operation_state_tag, operation::operation_state_concept>);
#else
    using removed [[maybe_unused]] = ex::operation_state_t;
#endif
#elif FORGE_REMOVED_NAME == 23
#if FORGE_REMOVAL_CONTROL
    static_assert(std::same_as<ex::scheduler_tag, ex::inline_scheduler::scheduler_concept>);
#else
    using removed [[maybe_unused]] = ex::scheduler_t;
#endif
#elif FORGE_REMOVED_NAME == 24
    using property = decltype(ex::prop{ex::get_scheduler, ex::inline_scheduler{}});
#if FORGE_REMOVAL_CONTROL
    static_assert(std::same_as<decltype(ex::env{std::declval<property>()}), ex::env<property>>);
#else
    using removed [[maybe_unused]] = decltype(ex::make_env(std::declval<property>()));
#endif
#elif FORGE_REMOVED_NAME == 25
#if FORGE_REMOVAL_CONTROL
    static_assert(std::destructible<ex::env<>>);
#else
    [[maybe_unused]] constexpr bool forms = ex::queryable<ex::env<>>;
#endif
#elif FORGE_REMOVED_NAME == 26
    using source = decltype(ex::just(42));
#if FORGE_REMOVAL_CONTROL
    static_assert(ex::receiver<receiver> && ex::sender_in<source, ex::env_of_t<receiver>>);
    static_assert(ex::operation_state<ex::connect_result_t<source, receiver>>);
#else
    [[maybe_unused]] constexpr bool forms = ex::sender_to<source, receiver>;
#endif
#elif FORGE_REMOVED_NAME == 27
    using token = std::inplace_stop_token;
    using callback = void(*)();
#if FORGE_REMOVAL_CONTROL
    static_assert(std::stoppable_token<token> && std::invocable<callback>);
    static_assert(std::constructible_from<std::stop_callback_for_t<token, callback>, token, callback>);
    static_assert(std::constructible_from<std::stop_callback_for_t<token, callback>, token&, callback>);
#else
    [[maybe_unused]] constexpr bool forms = std::stoppable_token_for<token, callback>;
#endif
#elif FORGE_REMOVED_NAME == 28
#if FORGE_REMOVAL_CONTROL
    static_assert(ex::sender<decltype(ex::just(42))>);
#else
    using removed [[maybe_unused]] = decltype(ex::split(ex::just(42)));
#endif
#elif FORGE_REMOVED_NAME == 29
#if FORGE_REMOVAL_CONTROL
    using result = decltype(std::this_thread::sync_wait(ex::just(42)));
    static_assert(std::same_as<result, std::optional<std::tuple<int>>>);
#else
    using removed [[maybe_unused]] = decltype(ex::sync_wait(ex::just(42)));
#endif
#elif FORGE_REMOVED_NAME >= 30 && FORGE_REMOVED_NAME <= 33
#if FORGE_REMOVED_NAME <= 31
    using scope = ex::simple_counting_scope;
#else
    using scope = ex::counting_scope;
#endif
#if FORGE_REMOVAL_CONTROL
    static_assert(requires(scope& s) {
        { s.close() } noexcept -> std::same_as<void>;
        { s.join() } -> ex::sender;
    });
#elif FORGE_REMOVED_NAME == 30 || FORGE_REMOVED_NAME == 32
    using removed [[maybe_unused]] = decltype(std::declval<const scope&>().count());
#else
    using removed [[maybe_unused]] = decltype(std::declval<const scope&>().is_closed());
#endif
#elif FORGE_REMOVED_NAME == 34 || FORGE_REMOVED_NAME == 35
#if FORGE_REMOVAL_CONTROL
    static_assert(ex::sender<decltype(forge::transfer_just(ex::inline_scheduler{}, 42))>);
#elif FORGE_REMOVED_NAME == 34
    using forge::scope_count;
#else
    using forge::scope_is_closed;
#endif
#else
#error "Select a removed name"
#endif
}

#include <forge/execution_extensions.hpp>

#include <concepts>
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
    static_assert(std::same_as<ex::env<>, forge::empty_env>);
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
    static_assert(std::same_as<result::value_type, std::variant<std::tuple<int>>>);
#else
    using removed [[maybe_unused]] = decltype(ex::sync_wait_with_variant(ex::just(42)));
#endif
#else
#error "Select a removed name"
#endif
}

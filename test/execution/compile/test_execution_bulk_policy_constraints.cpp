#include "../bulk_policy_probe.hpp"

#include <concepts>
#include <type_traits>
#include <utility>

using namespace bulk_policy_probe;

#if defined(FORGE_TEST_BULK_REQUIRE_NATIVE_POLICIES)
static_assert(FORGE_TEST_BULK_HAS_NATIVE_POLICIES,
    "This verification lane requires real standard execution policies");
static_assert(FORGE_TEST_BULK_HAS_UNSEQ,
    "This verification lane requires the C++20 unsequenced policy");
#endif

template<class Bound>
concept applicable_both = std::is_invocable_v<Bound, source_t> &&
    requires(Bound&& closure) {
        static_cast<Bound&&>(closure)(ex::just(10));
        ex::just(10) | static_cast<Bound&&>(closure);
    };

template<bool Chunked, class Policy, class Expected>
void check_storage() {
    using bound = bound_t<Chunked, Policy>;
    static_assert(std::same_as<decltype(std::declval<bound>().__policy_), Policy>);
    static_assert(applicable_both<bound&>);
    static_assert(applicable_both<const bound&>);
    static_assert(applicable_both<bound>);
    static_assert(applicable_both<const bound>);
    static_assert(std::same_as<stored_policy_t<std::invoke_result_t<bound&, source_t>>, Expected>);
    static_assert(std::same_as<stored_policy_t<std::invoke_result_t<const bound&, source_t>>, Expected>);
    static_assert(std::same_as<stored_policy_t<std::invoke_result_t<bound, source_t>>, Expected>);
    static_assert(std::same_as<stored_policy_t<std::invoke_result_t<const bound, source_t>>, Expected>);
    static_assert(ex::sender<std::invoke_result_t<bound&, source_t>>);
    static_assert(std::copy_constructible<std::invoke_result_t<bound&, source_t>>);
}

static_assert(std::copy_constructible<copyable_policy>);
static_assert(!std::copy_constructible<move_only_policy>);
static_assert(!std::copy_constructible<immovable_policy>);
static_assert(std::is_copy_constructible_v<explicit_copy_policy>);
static_assert(!std::copy_constructible<explicit_copy_policy>);
static_assert(std::copy_constructible<throwing_copy_policy>);
static_assert(!std::is_nothrow_copy_constructible_v<throwing_copy_policy>);

#if FORGE_TEST_BULK_HAS_NATIVE_POLICIES
template<class Algo, class Policy, class Fn>
concept full_callable = requires(Algo algo, Policy&& policy, Fn fn) {
    algo(ex::just(10), static_cast<Policy&&>(policy), 3, fn);
};

template<class Algo, class Policy, class Fn>
concept bindable = requires(Algo algo, Policy&& policy, Fn fn) {
    algo(static_cast<Policy&&>(policy), 3, fn);
};

template<class Algo, class Policy, class Fn>
void check_native() {
    using expected = std::conditional_t<std::copy_constructible<Policy>, Policy, const Policy&>;
    static_assert(full_callable<Algo, Policy&, Fn>);
    static_assert(full_callable<Algo, const Policy&, Fn>);
    static_assert(full_callable<Algo, Policy, Fn>);
    static_assert(full_callable<Algo, const Policy, Fn>);
    static_assert(full_callable<Algo, volatile Policy&, Fn> ==
        std::constructible_from<expected, volatile Policy&>);
    static_assert(!full_callable<Algo, int, Fn>);
    static_assert(bindable<Algo, const Policy&, Fn> ==
        std::constructible_from<Policy, const Policy&>);

    using sender = decltype(std::declval<Algo>()(ex::just(10),
        std::declval<const Policy&>(), 3, Fn{}));
    static_assert(std::same_as<stored_policy_t<sender>, expected>);
    static_assert(ex::sender<sender>);
    if constexpr (std::constructible_from<Policy, const Policy&>) {
        using bound = decltype(std::declval<Algo>()(std::declval<const Policy&>(), 3, Fn{}));
        static_assert(std::same_as<decltype(std::declval<bound>().__policy_), Policy>);
        static_assert(applicable_both<bound&>);
    }
}

template<class Policy>
void check_native_policy() {
    check_native<std::remove_cvref_t<decltype(ex::bulk)>, Policy, callback<false>>();
    check_native<std::remove_cvref_t<decltype(ex::bulk_unchunked)>, Policy, callback<false>>();
    check_native<std::remove_cvref_t<decltype(ex::bulk_chunked)>, Policy, callback<true>>();
}
#endif

template<bool Chunked>
void check_implementation() {
    check_storage<Chunked, copyable_policy, copyable_policy>();
    check_storage<Chunked, move_only_policy, const move_only_policy&>();
    check_storage<Chunked, immovable_policy, const immovable_policy&>();
    check_storage<Chunked, explicit_copy_policy, const explicit_copy_policy&>();
    check_storage<Chunked, throwing_copy_policy, throwing_copy_policy>();
}

int main() {
    check_implementation<false>();
    check_implementation<true>();
#if FORGE_TEST_BULK_HAS_NATIVE_POLICIES
    check_native_policy<std::remove_cvref_t<decltype(ex::seq)>>();
    check_native_policy<std::remove_cvref_t<decltype(ex::par)>>();
    check_native_policy<std::remove_cvref_t<decltype(ex::par_unseq)>>();
#if FORGE_TEST_BULK_HAS_UNSEQ
    check_native_policy<std::remove_cvref_t<decltype(ex::unseq)>>();
#endif
#endif
}

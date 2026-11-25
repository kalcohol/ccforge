#include <execution>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#ifndef FORGE_SYNC_WAIT_CASE
#define FORGE_SYNC_WAIT_CASE 0
#endif

namespace ex = std::execution;

template<class... Sigs>
struct value_sender {
    using sender_concept = ex::sender_tag;

    struct attributes {
        auto query(ex::get_completion_domain_t<ex::set_value_t>) const noexcept
            -> ex::default_domain { return {}; }
    };

    auto get_env() const noexcept -> attributes { return {}; }

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> ex::completion_signatures<Sigs...> { return {}; }

    template<class R>
    struct operation {
        using operation_state_concept = ex::operation_state_tag;
        R receiver;

        operation(R r) : receiver(std::move(r)) {}
        operation(const operation&) = delete;

        void start() & noexcept { ex::set_value(std::move(receiver), 42); }
    };

    template<ex::receiver R>
    auto connect(R r) const -> operation<R> { return operation<R>(std::move(r)); }
};

using wait_result = std::optional<std::tuple<int>>;
using variant_value = std::variant<std::tuple<int>>;
using variant_wait_result = std::optional<variant_value>;

template<class V>
using nested_wait_result = std::optional<std::variant<std::tuple<V>>>;

enum class result_kind { flat, wrong, nested };

template<result_kind Kind>
struct custom_wait_sender;

template<result_kind Kind>
struct wait_domain {
    template<class S>
    auto apply_sender(std::this_thread::sync_wait_t, S&&) const {
        if constexpr (Kind == result_kind::flat) {
            return wait_result{std::in_place, 73};
        } else {
            return 73;
        }
    }

    auto apply_sender(std::this_thread::sync_wait_with_variant_t, custom_wait_sender<Kind>&&) const {
        if constexpr (Kind == result_kind::flat) {
            return variant_wait_result{std::in_place, std::tuple{81}};
        } else if constexpr (Kind == result_kind::nested) {
            return nested_wait_result<variant_value>{
                std::in_place, std::in_place_index<0>, variant_value{std::tuple{81}}};
        } else {
            return std::optional<std::variant<std::tuple<long>>>{std::in_place, std::tuple{81L}};
        }
    }
};

template<result_kind Kind>
struct custom_wait_sender : value_sender<ex::set_value_t(int)> {
    struct attributes {
        auto query(ex::get_completion_domain_t<ex::set_value_t>) const noexcept
            -> wait_domain<Kind> { return {}; }
    };

    auto get_env() const noexcept -> attributes { return {}; }
};

int main() {
#if FORGE_SYNC_WAIT_CASE == 1
    std::this_thread::sync_wait(ex::just_stopped());
#elif FORGE_SYNC_WAIT_CASE == 2
    std::this_thread::sync_wait(value_sender<ex::set_value_t(int), ex::set_value_t(const int&)>{});
#elif FORGE_SYNC_WAIT_CASE == 3
    std::this_thread::sync_wait(42);
#elif FORGE_SYNC_WAIT_CASE == 4
    std::this_thread::sync_wait(custom_wait_sender<result_kind::wrong>{});
#elif FORGE_SYNC_WAIT_CASE == 5
    std::this_thread::sync_wait_with_variant(ex::just_stopped());
#elif FORGE_SYNC_WAIT_CASE == 6
    std::this_thread::sync_wait_with_variant(ex::just_error(42));
#elif FORGE_SYNC_WAIT_CASE == 7
    std::this_thread::sync_wait_with_variant(custom_wait_sender<result_kind::wrong>{});
#elif FORGE_SYNC_WAIT_CASE == 8
    std::this_thread::sync_wait_with_variant(custom_wait_sender<result_kind::nested>{});
#else
    auto result = std::this_thread::sync_wait(value_sender<ex::set_value_t(int)>{});
    auto customized = std::this_thread::sync_wait(custom_wait_sender<result_kind::flat>{});
    auto variant = std::this_thread::sync_wait_with_variant(value_sender<ex::set_value_t(int)>{});
    auto custom_variant = std::this_thread::sync_wait_with_variant(custom_wait_sender<result_kind::flat>{});
    using multi_sender = value_sender<ex::set_value_t(int), ex::set_value_t(double)>;
    using multi_value = std::variant<std::tuple<int>, std::tuple<double>>;
    using multi_result = decltype(std::this_thread::sync_wait_with_variant(multi_sender{}));
    static_assert(std::same_as<decltype(result), wait_result>);
    static_assert(std::same_as<decltype(customized), wait_result>);
    static_assert(std::same_as<decltype(variant), variant_wait_result>);
    static_assert(!std::same_as<decltype(variant), nested_wait_result<variant_value>>);
    static_assert(std::same_as<decltype(custom_variant), variant_wait_result>);
    static_assert(std::same_as<multi_result, std::optional<multi_value>>);
    static_assert(!std::same_as<multi_result, nested_wait_result<multi_value>>);
    return result && std::get<0>(*result) == 42 &&
        customized && std::get<0>(*customized) == 73 &&
        variant && std::get<0>(std::get<0>(*variant)) == 42 &&
        custom_variant && std::get<0>(std::get<0>(*custom_variant)) == 81 ? 0 : 1;
#endif
}

#include <forge/execution_extensions.hpp>
#include <forge/detail/execution_concepts.hpp>
#include <tuple>
#include <type_traits>
#include <variant>

namespace ex = std::execution;

struct multi_sender {
    using sender_concept = ex::sender_tag;
    using completion_signatures = ex::completion_signatures<
        ex::set_value_t(int), ex::set_value_t(double)>;
    auto get_completion_signatures(auto&&) const noexcept -> completion_signatures { return {}; }
    template<class R> struct operation {
        using operation_state_concept = ex::operation_state_tag;
        R receiver;
        explicit operation(R r) : receiver(std::move(r)) {}
        operation(const operation&) = delete;
        operation(operation&&) = delete;
        void start() & noexcept { ex::set_value(std::move(receiver), 42); }
    };
    template<class R> auto connect(R receiver) const {
        return operation<R>{std::move(receiver)};
    }
};

// A user-defined environment that answers through a member query, and a
// receiver that returns it by value.
struct tagged_domain {};

template<class Domain>
struct domain_env {
    auto query(ex::get_domain_t) const noexcept -> Domain { return {}; }
};

struct domain_receiver {
    using receiver_concept = ex::receiver_tag;
    void set_value(int) && noexcept {}
    auto get_env() const noexcept -> domain_env<tagged_domain> { return {}; }
};

// This is the execution compatibility probe for deprecated public spellings.
// Suppression starts after includes so header-internal warnings remain visible.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
int main() {
    using namespace std::execution;
    using namespace std::this_thread;
    static_assert(&std::execution::sync_wait == &std::this_thread::sync_wait);
    if (sync_wait(ex::just(42)) != std::this_thread::sync_wait(ex::just(42))) return 10;
    if (sync_wait_with_variant(ex::just(42)) !=
        std::this_thread::sync_wait_with_variant(ex::just(42))) return 11;
    // These spellings remain until the locked-header migration.
    static_assert(std::same_as<ex::receiver_t, ex::receiver_tag>);
    static_assert(std::same_as<ex::sender_t, ex::sender_tag>);
    static_assert(std::same_as<ex::scheduler_t, ex::scheduler_tag>);
    static_assert(std::same_as<ex::operation_state_t, ex::operation_state_tag>);
    static_assert(ex::receiver<domain_receiver>);
    static_assert(std::same_as<decltype(ex::make_env(std::declval<domain_env<tagged_domain>>())),
                               decltype(ex::env{std::declval<domain_env<tagged_domain>>()})>);
    static_assert(ex::queryable<domain_env<tagged_domain>> ==
                  std::destructible<domain_env<tagged_domain>>);
    using sender = decltype(ex::just(42));
    static_assert(ex::sender_to<sender, domain_receiver>);
    static_assert(forge::__detail::__sender_to<sender, domain_receiver>);
    auto old_split = ex::split(ex::just(42));
    auto new_split = forge::split(ex::just(42));
    auto a = ex::sync_wait(old_split);
    auto b = forge::sync_wait(new_split);
    if (a != b || a != forge::sync_wait(old_split)) return 1;
    auto old_multi = std::this_thread::sync_wait(multi_sender{});
    auto new_multi = forge::sync_wait(multi_sender{});
    if (old_multi != new_multi || std::get<0>(std::get<0>(*new_multi)) != 42) return 3;
    auto value = std::this_thread::sync_wait_with_variant(ex::just(1));
    if (!value || std::get<0>(std::get<0>(*value)) != 1) return 4;
    return 0;
}
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

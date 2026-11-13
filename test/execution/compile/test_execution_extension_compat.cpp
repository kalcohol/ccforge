#include <forge/execution_extensions.hpp>
#include <tuple>
#include <type_traits>
#include <variant>

namespace ex = std::execution;

struct multi_sender {
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<
        ex::set_value_t(int), ex::set_value_t(double)>;
    auto get_completion_signatures(auto&&) const noexcept -> completion_signatures { return {}; }
    template<class R> struct operation {
        using operation_state_concept = ex::operation_state_t;
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
    static_assert(&std::execution::sync_wait_with_variant ==
                  &std::this_thread::sync_wait_with_variant);
    if (sync_wait(ex::just(42)) != std::this_thread::sync_wait(ex::just(42))) return 10;
    if (sync_wait_with_variant(ex::just(42)) !=
        std::this_thread::sync_wait_with_variant(ex::just(42))) return 11;
    static_assert(std::same_as<ex::empty_env, forge::empty_env>);
    auto property = ex::make_prop(ex::get_scheduler, ex::inline_scheduler{});
    (void)ex::get_scheduler(property);
    auto old_split = ex::split(ex::just(42));
    auto new_split = forge::split(ex::just(42));
    auto a = ex::sync_wait(old_split);
    auto b = forge::sync_wait(new_split);
    if (a != b || a != forge::sync_wait(old_split)) return 1;
    auto old_transfer = ex::sync_wait(ex::transfer_just(ex::inline_scheduler{}, 7));
    auto new_transfer = forge::sync_wait(forge::transfer_just(ex::inline_scheduler{}, 7));
    if (old_transfer != new_transfer) return 2;
    auto old_multi = std::this_thread::sync_wait(multi_sender{});
    auto new_multi = forge::sync_wait(multi_sender{});
    if (old_multi != new_multi || std::get<0>(std::get<0>(*new_multi)) != 42) return 3;
    if (ex::sync_wait_with_variant(ex::just(1)) !=
        std::this_thread::sync_wait_with_variant(ex::just(1))) return 4;
    return 0;
}
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

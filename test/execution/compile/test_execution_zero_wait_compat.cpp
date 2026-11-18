#include <forge/execution_extensions.hpp>

#include <tuple>
#include <utility>
#include <variant>

int check_forge() {
    if (forge::sync_wait(std::execution::just_stopped())) return 1;
    try { (void)forge::sync_wait(std::execution::just_error(7)); }
    catch (int n) { return n == 7 ? 0 : 2; }
    return 3;
}
#if !defined(FORGE_DEPRECATION_CASE)
#define FORGE_DEPRECATION_CASE 0
#endif
#if FORGE_DEPRECATION_CASE == 0
namespace ex = std::execution;

struct multi_sender {
    using sender_concept = ex::sender_tag;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(int), ex::set_value_t(double)> {
        return {};
    }

    template<class R>
    struct operation {
        using operation_state_concept = ex::operation_state_tag;
        R receiver;

        explicit operation(R r) : receiver(std::move(r)) {}
        operation(const operation&) = delete;
        operation(operation&&) = delete;

        void start() & noexcept { ex::set_value(std::move(receiver), 42); }
    };

    template<class R>
    auto connect(R receiver) const {
        return operation<R>{std::move(receiver)};
    }
};

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#endif
int main() {

#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 1
    if (std::this_thread::sync_wait(std::execution::just_stopped())) return 4;
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 2
    try { (void)std::this_thread::sync_wait(std::execution::just_error(7)); return 5; } catch (int n) { if (n != 7) return 6; }
#endif
#if FORGE_DEPRECATION_CASE == 0
    const auto legacy_result = std::this_thread::sync_wait(multi_sender{});
    const auto forge_result = forge::sync_wait(multi_sender{});
    const auto* value = forge_result ? std::get_if<std::tuple<int>>(&*forge_result) : nullptr;
    if (legacy_result != forge_result || !value || *value != std::tuple{42}) {
        return 7;
    }
#endif
    return check_forge();
}
#if FORGE_DEPRECATION_CASE == 0
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif

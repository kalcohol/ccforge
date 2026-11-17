#include <execution>
#include <memory>
#include <utility>
namespace ex = std::execution;
#ifndef FORGE_ADAPTOR_CASE
#define FORGE_ADAPTOR_CASE 1
#endif
struct immovable_callable {
    immovable_callable() = default;
    immovable_callable(const immovable_callable&) = default;
    immovable_callable(immovable_callable&&) = delete;
    void operator()(int) const noexcept {}
};
struct schedule_receiver {
    using receiver_concept = ex::receiver_tag;
    void set_value() && noexcept {}
    void set_error(int) && noexcept {}
    struct scheduler {
        using scheduler_concept = ex::scheduler_tag;
        struct schedule_sender {
            using sender_concept = ex::sender_tag;
            template<class Self, class Env>
            static auto get_completion_signatures() noexcept
                -> ex::completion_signatures<ex::set_value_t(), ex::set_error_t(int)> { return {}; }
            template<class R> struct op : ex::__forge_detail::__immovable {
                using operation_state_concept = ex::operation_state_tag;
                R rcvr;
                explicit op(R r) : rcvr(std::move(r)) {}
                void start() & noexcept { ex::set_value(std::move(rcvr)); }
            };
            template<class R> auto connect(R r) const -> op<R> { return op<R>{std::move(r)}; }
        };
        auto schedule() const noexcept {
#ifdef FORGE_ADAPTOR_POSITIVE
            return ex::just();
#else
            return schedule_sender{};
#endif
        }
        auto query(ex::get_forward_progress_guarantee_t) const noexcept {
            return ex::forward_progress_guarantee::weakly_parallel;
        }
        bool operator==(const scheduler&) const noexcept = default;
    };
    auto get_env() const noexcept { return ex::prop{ex::get_start_scheduler, scheduler{}}; }
};
int main() {
#if FORGE_ADAPTOR_CASE == 1
#ifdef FORGE_ADAPTOR_POSITIVE
    auto f = [](int) noexcept {};
#else
    immovable_callable f;
#endif
    auto s = ex::then(ex::just(1), f);
#elif FORGE_ADAPTOR_CASE == 2
#ifdef FORGE_ADAPTOR_POSITIVE
    auto f = []() noexcept { return ex::just(); };
#else
    auto f = [](int) noexcept { return ex::just(); };
#endif
    auto s = ex::let_stopped(ex::just_stopped(), f);
#elif FORGE_ADAPTOR_CASE == 3
#ifdef FORGE_ADAPTOR_POSITIVE
    const char* error = "error";
#else
    const char error[] = "error";
#endif
    auto s = ex::stopped_as_error(ex::just_stopped(), error);
#elif FORGE_ADAPTOR_CASE == 4
    const auto error = std::make_unique<int>(1);
#ifdef FORGE_ADAPTOR_POSITIVE
    auto s = ex::stopped_as_error(std::make_unique<int>(1));
#else
    auto s = ex::stopped_as_error(error);
#endif
#elif FORGE_ADAPTOR_CASE == 5
#ifdef FORGE_ADAPTOR_POSITIVE
    auto f = [](int) noexcept {};
#else
    immovable_callable f;
#endif
    auto s = ex::upon_error(ex::just_error(1), f);
#elif FORGE_ADAPTOR_CASE == 6
#ifdef FORGE_ADAPTOR_POSITIVE
    auto f = [](int&) noexcept { return ex::just(); };
#else
    immovable_callable f;
#endif
    auto s = ex::let_value(ex::just(1), f);
#elif FORGE_ADAPTOR_CASE == 7
    auto s = ex::connect(ex::affine(ex::just()), schedule_receiver{});
#elif FORGE_ADAPTOR_CASE == 8
#ifdef FORGE_ADAPTOR_POSITIVE
    auto f = []() noexcept {};
#else
    immovable_callable f;
#endif
    auto s = ex::upon_stopped(ex::just_stopped(), f);
#elif FORGE_ADAPTOR_CASE == 9
#ifdef FORGE_ADAPTOR_POSITIVE
    auto f = [](int&) noexcept { return ex::just(); };
#else
    immovable_callable f;
#endif
    auto s = ex::let_error(ex::just_error(1), f);
#endif
    (void)s;
}

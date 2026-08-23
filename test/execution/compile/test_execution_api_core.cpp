#include <execution>

static_assert(std::execution::sender<decltype(std::execution::just(42))>);
static_assert(std::execution::enable_sender<decltype(std::execution::just(42))>);
static_assert(std::execution::scheduler<std::execution::run_loop::scheduler>);
static_assert(std::execution::scheduler<std::execution::inline_scheduler>);
static_assert(std::is_empty_v<std::execution::get_start_scheduler_t>);
static_assert(std::is_empty_v<std::execution::get_delegation_scheduler_t>);
static_assert(std::is_empty_v<std::execution::get_forward_progress_guarantee_t>);
static_assert(std::is_empty_v<std::execution::get_completion_scheduler_t<std::execution::set_value_t>>);
static_assert(std::is_empty_v<std::execution::as_awaitable_t>);
static_assert(std::is_empty_v<std::execution::schedule_from_t>);
static_assert(std::is_empty_v<std::execution::transform_sender_t>);
static_assert(std::is_empty_v<std::execution::apply_sender_t>);
static_assert(std::same_as<decltype(std::execution::forward_progress_guarantee::concurrent),
                           std::execution::forward_progress_guarantee>);

template<class S, class CPO>
concept has_completion_scheduler = requires(const S& sndr) {
    std::execution::get_completion_scheduler<CPO>(std::execution::get_env(sndr));
};

static_assert(!has_completion_scheduler<decltype(std::execution::just(42)),
                                        std::execution::set_value_t>);
static_assert(!has_completion_scheduler<decltype(std::execution::just_error(42)),
                                        std::execution::set_error_t>);
static_assert(!has_completion_scheduler<decltype(std::execution::just_stopped()),
                                        std::execution::set_stopped_t>);

static_assert(std::same_as<
    decltype(std::execution::get_completion_scheduler<std::execution::set_value_t>(
        std::execution::get_env(std::execution::schedule(std::execution::inline_scheduler{})))),
    std::execution::inline_scheduler>);

static_assert(std::same_as<
    decltype(std::execution::get_forward_progress_guarantee(std::execution::inline_scheduler{})),
    std::execution::forward_progress_guarantee>);
static_assert(std::execution::get_forward_progress_guarantee(std::execution::inline_scheduler{}) ==
              std::execution::forward_progress_guarantee::weakly_parallel);

struct default_progress_scheduler {
    using scheduler_concept = std::execution::scheduler_t;

    bool operator==(const default_progress_scheduler&) const noexcept = default;

    auto schedule() const noexcept {
        return std::execution::just();
    }
};

static_assert(std::execution::scheduler<default_progress_scheduler>);
static_assert(std::execution::get_forward_progress_guarantee(default_progress_scheduler{}) ==
              std::execution::forward_progress_guarantee::weakly_parallel);

struct value_only_receiver {
    using receiver_concept = std::execution::receiver_t;

    void set_value(int) && noexcept {}
    void set_error(std::exception_ptr) && noexcept {}

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

static_assert(requires {
    std::execution::connect(
        std::execution::just(42),
        value_only_receiver{});
});

int main() {
    auto sndr = std::execution::just(42);
    auto op = std::execution::connect(std::move(sndr), value_only_receiver{});
    std::execution::start(op);
    return 0;
}

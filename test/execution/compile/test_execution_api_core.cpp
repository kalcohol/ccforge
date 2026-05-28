#include <execution>

static_assert(std::execution::sender<decltype(std::execution::just(42))>);
static_assert(std::execution::enable_sender<decltype(std::execution::just(42))>);
static_assert(std::execution::scheduler<std::execution::run_loop::scheduler>);
static_assert(std::execution::scheduler<std::execution::inline_scheduler>);
static_assert(std::is_empty_v<std::execution::get_completion_scheduler_t<std::execution::set_value_t>>);

static_assert(std::same_as<
    decltype(std::execution::get_completion_scheduler<std::execution::set_value_t>(
        std::execution::get_env(std::execution::schedule(std::execution::inline_scheduler{})))),
    std::execution::inline_scheduler>);

int main() { return 0; }

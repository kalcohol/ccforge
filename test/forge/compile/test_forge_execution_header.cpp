#include <forge/execution.hpp>
#include <chrono>
#include <tuple>

int main() {
    forge::static_thread_pool pool{1};
    auto scheduled = std::execution::sync_wait(
        std::execution::schedule(pool.get_scheduler()));
    if (!scheduled) {
        return 1;
    }

    forge::single_thread_context single;
    auto single_result = std::execution::sync_wait(
        std::execution::schedule(single.get_scheduler()));
    if (!single_result) {
        return 2;
    }

    forge::timer_context timers;
    auto timer_result = std::execution::sync_wait(
        timers.schedule_after(std::chrono::milliseconds{0}));
    if (!timer_result) {
        return 3;
    }

    forge::runtime_context runtime{1};
    auto runtime_result = std::execution::sync_wait(
        std::execution::schedule(runtime.get_scheduler()));
    if (!runtime_result) {
        return 4;
    }

    forge::any_scheduler erased_scheduler{runtime.get_scheduler()};
    static_assert(std::execution::scheduler<forge::any_scheduler>);
    auto erased_scheduler_result = std::execution::sync_wait(
        std::execution::schedule(erased_scheduler));
    if (!erased_scheduler_result) {
        return 5;
    }

    using cs_int = std::execution::completion_signatures<
        std::execution::set_value_t(int),
        std::execution::set_error_t(std::exception_ptr),
        std::execution::set_stopped_t()>;

    forge::any_sender_of<cs_int> erased = std::execution::just(7);
    auto value = erased.sync_wait();
    if (!value || std::get<0>(*value) != 7) {
        return 6;
    }

    return 0;
}

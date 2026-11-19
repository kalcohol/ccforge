#include <forge/execution_extensions.hpp>
#include <forge/channel.hpp>
#include <forge/erased_sender.hpp>
#include <forge/start_detached.hpp>
#include <forge/static_thread_pool.hpp>
#include <forge/strand.hpp>
#include <forge/task.hpp>
#include <forge/timer_context.hpp>
#include <gtest/gtest.h>
#include <execution>

#include <chrono>
#include <exception>
#include <future>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace {
namespace ex = std::execution;
using namespace std::chrono_literals;

// The variant wait also captures values passed through by let_stopped.
template<class Result>
const int* int_value(const Result& result) {
    if constexpr (std::is_same_v<Result, std::tuple<int>>) {
        return &std::get<0>(result);
    } else {
        const auto* alternative = std::get_if<std::tuple<int>>(&result);
        return alternative ? &std::get<0>(*alternative) : nullptr;
    }
}

// let replaces its predecessor operation inside the predecessor's completion.
// The successor also completes inline, exercising both nested delivery tails.
template<class S>
void check_value(S source, bool another_thread) {
    const auto caller = std::this_thread::get_id();
    std::thread::id agent;
    int calls = 0;
    auto sender = ex::let_value(std::move(source), [&](auto&...) noexcept {
        agent = std::this_thread::get_id();
        ++calls;
        return ex::just(42);
    });
    auto value = forge::sync_wait(std::move(sender));
    ASSERT_TRUE(value);
    const int* result = int_value(*value);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, 42);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(agent != caller, another_thread);
}

TEST(ExecutionForgePredecessorLifetime, PoolCompletesOnWorkerAndRejectsInline) {
    forge::static_thread_pool pool{1};
    check_value(ex::schedule(pool.get_scheduler()), true);
    pool.wait();
    pool.shutdown();
    const auto caller = std::this_thread::get_id();
    auto value = std::this_thread::sync_wait_with_variant(ex::let_stopped(ex::schedule(pool.get_scheduler()), [&]() noexcept {
        EXPECT_EQ(std::this_thread::get_id(), caller);
        return ex::just(42);
    }));
    ASSERT_TRUE(value);
    const int* result = int_value(*value);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, 42);
}

TEST(ExecutionForgePredecessorLifetime, StrandCompletesInlineAndOnWorker) {
    forge::strand inline_strand{ex::inline_scheduler{}};
    check_value(ex::schedule(inline_strand.get_scheduler()), false);
    inline_strand.wait();
    forge::static_thread_pool pool{1};
    forge::strand strand{pool.get_scheduler()};
    check_value(ex::schedule(strand.get_scheduler()), true);
    strand.wait();
    pool.wait();
}

TEST(ExecutionForgePredecessorLifetime, TimerCompletesOnWorkerAndStopsInline) {
    forge::timer_context timer;
    check_value(timer.schedule_after(0ms), true);
    timer.wait();
    timer.shutdown();
    const auto caller = std::this_thread::get_id();
    auto value = std::this_thread::sync_wait_with_variant(ex::let_stopped(timer.schedule_after(1h), [&]() noexcept {
        EXPECT_EQ(std::this_thread::get_id(), caller);
        return ex::just(42);
    }));
    ASSERT_TRUE(value);
    const int* result = int_value(*value);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, 42);
}

struct channel_receiver {
    using receiver_concept = ex::receiver_tag;
    std::promise<int>* completion;
    void set_value(int v) && noexcept { completion->set_value(v); }
    void set_error(std::exception_ptr) && noexcept { completion->set_value(-1); }
    void set_stopped() && noexcept { completion->set_value(-2); }
};
TEST(ExecutionForgePredecessorLifetime, ChannelReceivesBufferedAndCrossThreadValues) {
    forge::bounded_channel<int> buffered{1};
    ASSERT_TRUE(buffered.try_send(7));
    check_value(buffered.async_recv(), false);

    forge::bounded_channel<int> channel{0};
    std::promise<int> completion;
    auto future = completion.get_future();
    std::thread::id agent;
    auto op = ex::connect(ex::let_value(channel.async_recv(), [&](int& value) noexcept {
        agent = std::this_thread::get_id();
        return ex::just(value + 1);
    }), channel_receiver{&completion});
    ex::start(op); // The receive is pending before the other thread sends.
    EXPECT_EQ(future.wait_for(0ms), std::future_status::timeout);
    bool sent = false;
    std::thread producer([&] { sent = channel.try_send(7); });
    const auto producer_id = producer.get_id();
    producer.join();
    channel.close();
    ASSERT_TRUE(sent);
    EXPECT_EQ(future.get(), 8);
    EXPECT_EQ(agent, producer_id);
}

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
forge::task<int> immediate_task() { co_return 7; }
forge::task<int> worker_task(forge::static_thread_pool::scheduler scheduler) {
    co_await ex::schedule(scheduler);
    co_return 7;
}
forge::task<int> failing_task() {
    throw std::runtime_error("task predecessor");
    co_return 0;
}
forge::task<int> failing_worker_task(forge::static_thread_pool::scheduler scheduler) {
    co_await ex::schedule(scheduler);
    throw std::runtime_error("task predecessor");
    co_return 0;
}
TEST(ExecutionForgePredecessorLifetime, TaskCompletesAndErrorsInlineAndOnWorker) {
    check_value(immediate_task(), false);
    forge::static_thread_pool pool{1};
    check_value(worker_task(pool.get_scheduler()), true);
    for (bool worker : {false, true}) {
        const auto caller = std::this_thread::get_id();
        std::thread::id agent;
        auto source = worker ? failing_worker_task(pool.get_scheduler()) : failing_task();
        auto value = forge::sync_wait(ex::let_error(std::move(source), [&](std::exception_ptr& error) noexcept {
            agent = std::this_thread::get_id();
            EXPECT_THROW(std::rethrow_exception(error), std::runtime_error);
            return ex::just(42);
        }));
        ASSERT_TRUE(value);
        const int* result = int_value(*value);
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(*result, 42);
        EXPECT_EQ(agent != caller, worker);
    }
    pool.wait();
}
#endif

TEST(ExecutionForgePredecessorLifetime, ErasedSenderCompletesInlineAndOnWorker) {
    using signatures = ex::completion_signatures<ex::set_value_t(int), ex::set_stopped_t()>;
    check_value(forge::erased_sender<signatures>{ex::just(7)}, false);
    forge::static_thread_pool pool{1};
    check_value(forge::erased_sender<signatures>{ex::then(ex::schedule(pool.get_scheduler()),
        []() noexcept { return 7; })}, true);
    pool.wait();
}
} // namespace

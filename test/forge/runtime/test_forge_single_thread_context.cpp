#include <gtest/gtest.h>
#include <forge/single_thread_context.hpp>
#include <forge/start_detached.hpp>
#include <execution>
#include <atomic>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

static_assert(std::execution::scheduler<forge::single_thread_context::scheduler>);

TEST(SingleThreadContextTest, RunsOnDedicatedThread) {
    forge::single_thread_context ctx;
    auto main_id = std::this_thread::get_id();

    auto result = std::execution::sync_wait(
        std::execution::schedule(ctx.get_scheduler())
            | std::execution::then([main_id] {
                  return std::this_thread::get_id() != main_id;
              }));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result));
}

TEST(SingleThreadContextTest, ReusesSameWorkerThread) {
    forge::single_thread_context ctx;
    auto sch = ctx.get_scheduler();

    auto first = std::execution::sync_wait(
        std::execution::schedule(sch)
            | std::execution::then([] { return std::this_thread::get_id(); }));
    auto second = std::execution::sync_wait(
        std::execution::schedule(sch)
            | std::execution::then([] { return std::this_thread::get_id(); }));

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(std::get<0>(*first), std::get<0>(*second));
}

TEST(SingleThreadContextTest, PreservesFifoOrderForAcceptedTasks) {
    forge::single_thread_context ctx;
    auto sch = ctx.get_scheduler();
    std::mutex mtx;
    std::vector<int> order;

    for (int value : {1, 2, 3}) {
        forge::start_detached(
            std::execution::schedule(sch) | std::execution::then([&, value] {
                std::lock_guard lk{mtx};
                order.push_back(value);
            }));
    }

    ctx.wait();

    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST(SingleThreadContextTest, ScheduleAfterShutdownCompletesStopped) {
    forge::single_thread_context ctx;
    auto sch = ctx.get_scheduler();
    ctx.shutdown();

    auto result = std::execution::sync_wait(std::execution::schedule(sch));

    EXPECT_FALSE(result.has_value());
}

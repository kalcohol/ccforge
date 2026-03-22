#include <gtest/gtest.h>
#include <forge/static_thread_pool.hpp>
#include <forge/system_context.hpp>
#include <execution>
#include <atomic>
#include <tuple>

static_assert(std::execution::scheduler<forge::static_thread_pool::scheduler>);

TEST(StaticThreadPoolTest, BasicSchedule) {
    forge::static_thread_pool pool(2);
    auto sch = pool.get_scheduler();
    auto result = std::execution::sync_wait(
        std::execution::schedule(sch) | std::execution::then([]{ return 42; }));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(StaticThreadPoolTest, ConcurrentTasks) {
    forge::static_thread_pool pool(4);
    auto sch = pool.get_scheduler();
    std::atomic<int> counter{0};
    for (int i = 0; i < 10; ++i) {
        std::execution::start_detached(
            std::execution::schedule(sch) | std::execution::then([&counter]{
                counter.fetch_add(1, std::memory_order_relaxed);
            }));
    }
    pool.wait();
    EXPECT_EQ(counter.load(), 10);
}

TEST(StaticThreadPoolTest, ThreadCount) {
    forge::static_thread_pool pool(3);
    EXPECT_EQ(pool.thread_count(), 3u);
}

TEST(SystemContextTest, GlobalScheduler) {
    auto& ctx = forge::system_context::get();
    auto sch = ctx.get_scheduler();
    static_assert(std::execution::scheduler<decltype(sch)>);
    auto result = std::execution::sync_wait(
        std::execution::schedule(sch) | std::execution::then([]{ return 99; }));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 99);
}

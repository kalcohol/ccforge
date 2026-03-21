#include <gtest/gtest.h>

#include <execution>

#include <atomic>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

static_assert(std::execution::scheduler<std::execution::run_loop::scheduler>);

TEST(RunLoopTest, BasicScheduleThenSyncWait) {
    std::execution::run_loop loop;
    auto sch = loop.get_scheduler();

    int result = -1;
    std::thread worker([&] { loop.run(); });

    auto sndr = std::execution::schedule(sch)
              | std::execution::then([&] { result = 42; });

    std::execution::sync_wait(std::move(sndr));
    loop.finish();
    worker.join();

    EXPECT_EQ(result, 42);
}

TEST(RunLoopTest, FinishBeforeRunReturnsImmediately) {
    std::execution::run_loop loop;
    loop.finish();
    loop.run();
    SUCCEED();
}

TEST(RunLoopTest, MultipleTasksExecuteInOrder) {
    std::execution::run_loop loop;
    auto sch = loop.get_scheduler();

    std::vector<int> order;
    std::thread worker([&] { loop.run(); });

    for (int i = 0; i < 5; ++i) {
        auto sndr = std::execution::schedule(sch)
                  | std::execution::then([&order, i] { order.push_back(i); });
        std::execution::sync_wait(std::move(sndr));
    }

    loop.finish();
    worker.join();

    ASSERT_EQ(order.size(), 5u);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(order[static_cast<size_t>(i)], i);
    }
}

TEST(RunLoopTest, SchedulerEnvRoundtrip) {
    std::execution::run_loop loop;
    auto sch = loop.get_scheduler();
    auto sndr = std::execution::schedule(sch);
    auto env = std::execution::get_env(sndr);
    auto sch2 = std::execution::get_scheduler(env);
    EXPECT_EQ(sch, sch2);
}

TEST(RunLoopTest, ConcurrentPush) {
    std::execution::run_loop loop;
    auto sch = loop.get_scheduler();
    std::thread worker([&] { loop.run(); });

    constexpr int kThreads = 4;
    constexpr int kTasksPerThread = 25;
    std::atomic<int> counter{0};

    std::vector<std::thread> producers;
    producers.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        producers.emplace_back([&] {
            for (int j = 0; j < kTasksPerThread; ++j) {
                auto sndr = std::execution::schedule(sch)
                          | std::execution::then([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
                std::execution::sync_wait(std::move(sndr));
            }
        });
    }

    for (auto& p : producers) { p.join(); }
    loop.finish();
    worker.join();

    EXPECT_EQ(counter.load(), kThreads * kTasksPerThread);
}

TEST(RunLoopTest, DestructorSafeWhenEmpty) {
    {
        std::execution::run_loop loop;
        loop.finish();
        loop.run();
    }
    SUCCEED();
}

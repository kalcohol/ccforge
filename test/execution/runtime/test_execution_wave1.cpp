#include <gtest/gtest.h>
#include <execution>
#include <atomic>
#include <thread>
#include <variant>
#include <tuple>

TEST(IntoVariantTest, WrapsValue) {
    auto sndr = std::execution::into_variant(std::execution::just(42));
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    using var_t = std::variant<std::tuple<int>>;
    auto& var = std::get<0>(*result);
    EXPECT_EQ(std::get<std::tuple<int>>(var), std::make_tuple(42));
}

TEST(SyncWaitWithVariantTest, Works) {
    auto result = std::this_thread::sync_wait_with_variant(std::execution::just(42));
    ASSERT_TRUE(result.has_value());
}

TEST(BulkTest, SerialExecution) {
    int sum = 0;
    auto sndr = std::execution::just(0)
              | std::execution::bulk(5, [&sum](int idx, int& v) {
                    sum += idx; v += idx;
                });
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(sum, 0+1+2+3+4);
    EXPECT_EQ(std::get<0>(*result), 0+1+2+3+4);
}

TEST(StartDetachedTest, Executes) {
    std::atomic<int> counter{0};
    std::execution::start_detached(
        std::execution::just() | std::execution::then([&counter] { counter++; }));
    EXPECT_EQ(counter.load(), 1);
}

TEST(ContinuesOnTest, TransfersToScheduler) {
    std::execution::run_loop loop;
    auto sch = loop.get_scheduler();
    int result = -1;
    std::thread worker([&loop] { loop.run(); });

    auto sndr = std::execution::continues_on(
                  std::execution::just(42) | std::execution::then([&result](int x) { result = x; }),
                  sch);
    std::execution::sync_wait(std::move(sndr));

    loop.finish();
    worker.join();
    EXPECT_EQ(result, 42);
}

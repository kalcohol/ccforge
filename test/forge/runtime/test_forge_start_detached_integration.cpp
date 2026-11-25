#include <execution>
#include <forge/start_detached.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <utility>

TEST(StartDetachedTest, Executes) {
    std::atomic<int> counter{0};
    forge::start_detached(
        std::execution::just() | std::execution::then([&counter] { counter++; }));
    EXPECT_EQ(counter.load(), 1);
}

TEST(StartDetachedTest, HandlesSynchronousWhenAllCompletion) {
    std::atomic<int> counter{0};
    forge::start_detached(
        std::execution::when_all(std::execution::just(), std::execution::just())
        | std::execution::then([&counter] { counter.fetch_add(1, std::memory_order_relaxed); }));
    EXPECT_EQ(counter.load(), 1);
}

TEST(StartDetachedTest, HandlesSynchronousStartsOnCompletion) {
    std::atomic<int> counter{0};
    std::execution::inline_scheduler sch;
    forge::start_detached(
        std::execution::starts_on(
            sch,
            std::execution::just()
            | std::execution::then([&counter] { counter.fetch_add(1, std::memory_order_relaxed); })));
    EXPECT_EQ(counter.load(), 1);
}

TEST(StartDetachedTest, HandlesSynchronousContinuesOnCompletion) {
    std::atomic<int> counter{0};
    std::execution::inline_scheduler sch;
    forge::start_detached(
        std::execution::continues_on(
            std::execution::just()
            | std::execution::then([&counter] { counter.fetch_add(1, std::memory_order_relaxed); }),
            sch));
    EXPECT_EQ(counter.load(), 1);
}

TEST(ExecutionMvpTest, NonCopyableLvaluePipelineStartDetachedRequiresMove) {
    bool ran = false;
    auto sndr = std::execution::just(std::make_unique<int>(7))
        | std::execution::then([&](std::unique_ptr<int> value) noexcept {
              ran = (*value == 7);
          });

    forge::start_detached(std::move(sndr));

    EXPECT_TRUE(ran);
}

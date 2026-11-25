#include <execution>
#include <forge/execution_extensions.hpp>
#include <gtest/gtest.h>

#include <memory>
#include <tuple>
#include <utility>

namespace {
namespace ex = std::execution;

struct deref_unique {
    int operator()(std::unique_ptr<int> p) const noexcept { return *p; }
};

TEST(ExecutionMvpTest, NonCopyableLvaluePipelineSplitRequiresMove) {
    auto source = std::execution::just(std::make_unique<int>(13))
        | std::execution::then(deref_unique{});
    auto sndr = forge::split(std::move(source));

    auto result = std::this_thread::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 13);
}

TEST(ExecutionMvpTest, TransferJustCompletesOnScheduler) {
    std::execution::inline_scheduler scheduler;

    auto result = std::this_thread::sync_wait(
        forge::transfer_just(scheduler, 42));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(ExecutionAdaptorConformance, LockedPredecessorsCanBeReplacedDuringCompletion) {
    const auto check = [](auto source) {
        auto sender = ex::let_value(std::move(source), [](auto&...) noexcept { return ex::just(42); });
        auto value = std::this_thread::sync_wait(std::move(sender));
        ASSERT_TRUE(value);
        EXPECT_EQ(std::get<0>(*value), 42);
    };
    check(forge::split(ex::just(1)));
}
} // namespace

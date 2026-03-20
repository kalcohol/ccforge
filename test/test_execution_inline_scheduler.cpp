#include <gtest/gtest.h>

#include <execution>

#include <tuple>
#include <type_traits>
#include <utility>

TEST(ExecutionInlineSchedulerTest, ScheduleRunsInline) {
    std::execution::inline_scheduler sch{};

    auto result = std::execution::sync_wait(std::execution::schedule(sch));

    ASSERT_TRUE(static_cast<bool>(result));
    using tuple_t = std::remove_cvref_t<decltype(*result)>;
    static_assert(std::tuple_size_v<tuple_t> == 0);
}

TEST(ExecutionInlineSchedulerTest, ScheduleThenChain) {
    std::execution::inline_scheduler sch{};

    auto sender =
        std::execution::schedule(sch) |
        std::execution::then([] { return 123; });

    auto result = std::execution::sync_wait(std::move(sender));

    ASSERT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(std::get<0>(*result), 123);
}


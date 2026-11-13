#include <gtest/gtest.h>
#include <forge/task.hpp>
#include <execution>
#include <tuple>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
namespace {
auto value_task() -> forge::task<int> {
    auto value = co_await std::execution::just(42);
    co_return value;
}
auto stopped_task() -> forge::task<int> {
    co_await std::execution::just_stopped();
    co_return 42;
}
}
TEST(TaskMemberQueriesTest, ValueAndStoppedSurviveStrictDispatch) {
    auto value = std::execution::sync_wait(value_task());
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(std::get<0>(*value), 42);
    EXPECT_FALSE(std::execution::sync_wait(stopped_task()).has_value());
}
#endif

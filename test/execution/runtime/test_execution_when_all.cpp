#include <gtest/gtest.h>
#include <execution>
#include <tuple>
#include <string>

TEST(WhenAllTest, AggregatesMultipleValues) {
    auto sndr = std::execution::when_all(
        std::execution::just(1),
        std::execution::just(2.0));
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 1);
    EXPECT_DOUBLE_EQ(std::get<1>(*result), 2.0);
}

TEST(WhenAllTest, SingleSender) {
    auto sndr = std::execution::when_all(std::execution::just(42));
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(WhenAllTest, ErrorPropagates) {
    auto sndr = std::execution::when_all(
        std::execution::just(1),
        std::execution::just_error(42));
    EXPECT_THROW(std::execution::sync_wait(std::move(sndr)), int);
}

TEST(WhenAllTest, StoppedPropagates) {
    auto sndr = std::execution::when_all(
        std::execution::just(1),
        std::execution::just_stopped());
    auto result = std::execution::sync_wait(std::move(sndr));
    EXPECT_FALSE(result.has_value());
}

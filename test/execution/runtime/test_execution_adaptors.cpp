#include <gtest/gtest.h>
#include <execution>
#include <optional>
#include <atomic>
#include <thread>

TEST(ReadEnvTest, SenderExists) {
    auto sndr = std::execution::read_env(std::execution::get_stop_token);
    static_assert(std::execution::sender<decltype(sndr)>);
    SUCCEED();
}

TEST(UponErrorTest, ValuePassThrough) {
    bool fn_called = false;
    auto sndr = std::execution::just(10)
              | std::execution::upon_error([&](auto) { fn_called = true; });
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(fn_called);
    EXPECT_EQ(std::get<0>(*result), 10);
}

TEST(UponErrorTest, ErrorHandledWithoutThrow) {
    bool fn_called = false;
    auto sndr = std::execution::just_error(42)
              | std::execution::upon_error([&](int) { fn_called = true; });
    EXPECT_NO_THROW(std::execution::sync_wait(std::move(sndr)));
    EXPECT_TRUE(fn_called);
}

TEST(UponStoppedTest, ValuePassThrough) {
    bool fn_called = false;
    auto sndr = std::execution::just(10)
              | std::execution::upon_stopped([&] { fn_called = true; });
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(fn_called);
    EXPECT_EQ(std::get<0>(*result), 10);
}

TEST(UponStoppedTest, StoppedHandledWithoutThrow) {
    bool fn_called = false;
    auto sndr = std::execution::just_stopped()
              | std::execution::upon_stopped([&] { fn_called = true; });
    EXPECT_NO_THROW(std::execution::sync_wait(std::move(sndr)));
    EXPECT_TRUE(fn_called);
}

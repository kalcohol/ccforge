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

TEST(LetValueTest, ChainNewSender) {
    auto sndr = std::execution::just(42)
              | std::execution::let_value([](int x) {
                    return std::execution::just(x + 1);
                });
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 43);
}

TEST(LetValueTest, ErrorPassThrough) {
    bool fn_called = false;
    auto sndr = std::execution::just_error(42)
              | std::execution::let_value([&](auto) {
                    fn_called = true;
                    return std::execution::just(0);
                });
    EXPECT_THROW(std::execution::sync_wait(std::move(sndr)), int);
    EXPECT_FALSE(fn_called);
}

TEST(LetErrorTest, HandleError) {
    bool fn_called = false;
    auto sndr = std::execution::just_error(42)
              | std::execution::let_error([&](int) {
                    fn_called = true;
                    return std::execution::just_stopped();
                });
    EXPECT_NO_THROW(std::execution::sync_wait(std::move(sndr)));
    EXPECT_TRUE(fn_called);
}

TEST(LetErrorTest, ValuePassThrough) {
    bool fn_called = false;
    auto sndr = std::execution::just(10)
              | std::execution::let_error([&](auto) {
                    fn_called = true;
                    return std::execution::just(0);
                });
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(fn_called);
    EXPECT_EQ(std::get<0>(*result), 10);
}

TEST(LetStoppedTest, HandleStopped) {
    bool fn_called = false;
    auto sndr = std::execution::just_stopped()
              | std::execution::let_stopped([&] {
                    fn_called = true;
                    return std::execution::just_stopped();
                });
    EXPECT_NO_THROW(std::execution::sync_wait(std::move(sndr)));
    EXPECT_TRUE(fn_called);
}

TEST(StartsOnTest, RunsOnScheduler) {
    std::execution::run_loop loop;
    auto sch = loop.get_scheduler();

    int result = -1;
    std::thread worker([&] { loop.run(); });

    auto sndr = std::execution::starts_on(sch,
        std::execution::just(42) | std::execution::then([&](int x) {
            result = x;
        }));

    std::execution::sync_wait(std::move(sndr));
    loop.finish();
    worker.join();

    EXPECT_EQ(result, 42);
}

TEST(StoppedAsOptionalTest, SenderExists) {
    auto sndr1 = std::execution::stopped_as_optional(std::execution::just_stopped());
    static_assert(std::execution::sender<decltype(sndr1)>);
    auto sndr2 = std::execution::stopped_as_error(std::execution::just_stopped(), 42);
    static_assert(std::execution::sender<decltype(sndr2)>);
    SUCCEED();
}

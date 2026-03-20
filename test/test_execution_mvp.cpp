#include <gtest/gtest.h>

#include <execution>

#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

template<class T>
concept optional_like = requires(T t) {
    static_cast<bool>(t);
    *t;
};

} // namespace

TEST(ExecutionMvpTest, JustSyncWaitSingleValue) {
    auto result = std::execution::sync_wait(std::execution::just(42));

    static_assert(optional_like<decltype(result)>);
    ASSERT_TRUE(static_cast<bool>(result));

    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(ExecutionMvpTest, JustSyncWaitMultipleValues) {
    auto result = std::execution::sync_wait(std::execution::just(1, std::string("ok")));

    ASSERT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(std::get<0>(*result), 1);
    EXPECT_EQ(std::get<1>(*result), "ok");
}

TEST(ExecutionMvpTest, ThenTransformsValue) {
    auto sender = std::execution::then(std::execution::just(10), [](int v) { return v + 5; });
    auto result = std::execution::sync_wait(std::move(sender));

    ASSERT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(std::get<0>(*result), 15);
}

TEST(ExecutionMvpTest, ThenWorksWithPipeOperator) {
    auto sender = std::execution::just(10) | std::execution::then([](int v) { return v + 7; });
    auto result = std::execution::sync_wait(std::move(sender));

    ASSERT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(std::get<0>(*result), 17);
}

TEST(ExecutionMvpTest, ThenVoidReturnBecomesEmptyTuple) {
    int observed = 0;
    auto sender = std::execution::then(std::execution::just(1), [&](int v) { observed = v; });
    auto result = std::execution::sync_wait(std::move(sender));

    ASSERT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(observed, 1);

    using tuple_t = std::remove_cvref_t<decltype(*result)>;
    static_assert(std::tuple_size_v<tuple_t> == 0);
}

TEST(ExecutionMvpTest, ThenExceptionPropagatesViaSyncWait) {
    auto sender = std::execution::then(std::execution::just(), []() -> int {
        throw std::runtime_error("boom");
    });

    EXPECT_THROW((void)std::execution::sync_wait(std::move(sender)), std::runtime_error);
}

TEST(ExecutionMvpTest, ThenForwardsErrorAndDoesNotCallFn) {
    bool called = false;
    auto sender =
        std::execution::just_error(std::runtime_error("err")) |
        std::execution::then([&](int) {
            called = true;
            return 0;
        });

    EXPECT_THROW((void)std::execution::sync_wait(std::move(sender)), std::runtime_error);
    EXPECT_FALSE(called);
}

TEST(ExecutionMvpTest, ThenForwardsStoppedAndDoesNotCallFn) {
    bool called = false;
    auto sender =
        std::execution::just_stopped() |
        std::execution::then([&] {
            called = true;
            return 0;
        });

    auto result = std::execution::sync_wait(std::move(sender));
    EXPECT_FALSE(static_cast<bool>(result));
    EXPECT_FALSE(called);
}

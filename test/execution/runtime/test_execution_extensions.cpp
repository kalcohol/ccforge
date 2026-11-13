#include <forge/execution_extensions.hpp>
#include <gtest/gtest.h>
#include <tuple>
#include <concepts>
#include "../../forge/runtime/forge_operation_destroy.hpp"

// These uses must stay warning-free with -Werror=deprecated-declarations.
// The fallback environment and senders share the non-deprecated Forge type.
static_assert(std::same_as<decltype(std::execution::get_env(0)), forge::empty_env>);
static_assert(std::same_as<decltype(std::execution::get_env(std::execution::just())),
                           forge::empty_env>);
static_assert(std::same_as<decltype(std::execution::get_completion_signatures(
                               std::execution::just(42))),
                           std::execution::completion_signatures<
                               std::execution::set_value_t(int)>>);

TEST(ExecutionExtensions, SplitCachesAndReplays) {
    int invocations = 0;
    auto shared = forge::split(std::execution::just(21) |
        std::execution::then([&](int x) { ++invocations; return x * 2; }));
    auto first = forge::sync_wait(shared);
    auto second = forge::sync_wait(shared);
    ASSERT_TRUE(first);
    EXPECT_EQ(std::get<0>(*first), 42);
    EXPECT_EQ(first, second);
    EXPECT_EQ(invocations, 1);
}

TEST(ExecutionExtensions, TransferAndStandardWaitAgree) {
    auto result = forge::sync_wait(forge::transfer_just(
        std::execution::inline_scheduler{}, 42));
    auto expected = std::this_thread::sync_wait(std::execution::continues_on(
        std::execution::just(42), std::execution::inline_scheduler{}));
    EXPECT_EQ(result, expected);
}

TEST(ExecutionExtensions, StandardEnvironmentSpellings) {
    std::execution::env<> empty;
    EXPECT_EQ(std::execution::get_stop_token(empty).stop_possible(), false);
    auto property = std::execution::prop(
        std::execution::get_scheduler, std::execution::inline_scheduler{});
    EXPECT_EQ(std::execution::get_scheduler(property), std::execution::inline_scheduler{});
}

namespace {
struct destroying_split_receiver {
    using receiver_concept = std::execution::receiver_t;
    forge_test::destroy_context_base* context;
    int* result;
    void set_value(const int& value) && noexcept {
        *result = value;
        context->destroy();
    }
    void set_error(std::exception_ptr) && noexcept { context->destroy(); }
};
} // namespace

TEST(ExecutionExtensions, SplitCompletionCanDestroySubscriberOperation) {
    auto shared = forge::split(std::execution::just(42));
    int result = 0;
    bool destroyed = false;
    using op_t = decltype(std::execution::connect(shared, destroying_split_receiver{}));
    forge_test::operation_destroy_context<op_t> context{&destroyed};
    auto& op = context.emplace_from([&] {
        return std::execution::connect(shared, destroying_split_receiver{&context, &result});
    });
    std::execution::start(op);
    EXPECT_TRUE(destroyed);
    EXPECT_FALSE(context.has_value);
    EXPECT_EQ(result, 42);
    auto replay = forge::sync_wait(shared);
    ASSERT_TRUE(replay.has_value());
    EXPECT_EQ(std::get<0>(*replay), 42);
}

namespace {
struct split_channel_receiver {
    using receiver_concept = std::execution::receiver_t;
    int* values;
    int* error;
    int* stopped;
    void set_value() && noexcept { ++*values; }
    void set_error(std::exception_ptr e) && noexcept {
        try { std::rethrow_exception(e); }
        catch (int value) { *error = value; }
        catch (...) { *error = -1; }
    }
    void set_stopped() && noexcept { ++*stopped; }
};
} // namespace

TEST(ExecutionExtensions, SplitReplaysVoidErrorAndStoppedChannels) {
    auto check = [](auto source, int expected_values, int expected_error, int expected_stopped) {
        auto shared = forge::split(std::move(source));
        int values = 0;
        int error = 0;
        int stopped = 0;
        auto first = std::execution::connect(shared, split_channel_receiver{&values, &error, &stopped});
        std::execution::start(first);
        auto second = std::execution::connect(shared, split_channel_receiver{&values, &error, &stopped});
        std::execution::start(second);
        EXPECT_EQ(values, expected_values);
        EXPECT_EQ(error, expected_error);
        EXPECT_EQ(stopped, expected_stopped);
    };
    check(std::execution::just(), 2, 0, 0);
    check(std::execution::just_error(7), 0, 7, 0);
    check(std::execution::just_stopped(), 0, 0, 2);
}

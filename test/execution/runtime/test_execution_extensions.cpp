#include <forge/execution_extensions.hpp>
#include <gtest/gtest.h>
#include <tuple>
#include <concepts>

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

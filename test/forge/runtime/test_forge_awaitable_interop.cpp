#include "owned_awaiters.hpp"
#include <forge/any_sender.hpp>
#include <forge/async_scope.hpp>
#include <forge/erased_sender.hpp>
#include <forge/io/coro.hpp>
#include <forge/resource_context.hpp>
#include <forge/split.hpp>
#include <forge/start_detached.hpp>
#include <forge/task.hpp>
#include <forge/wait_result.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <execution>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace {
namespace ex = std::execution;
namespace a = awaitable_tests;
using a::owned_error;
using a::owned_suspend_error;
using a::owned_stopped;

template<class S>
forge::task<void> await_without_fallthrough(S source, bool* after_await) {
    co_await std::move(source);
    *after_await = true;
}

TEST(AwaitableSender, ErrorAndStoppedNeverReachCodeAfterAwait) {
    bool destroyed = false;
    bool after_await = false;
    EXPECT_THROW(std::this_thread::sync_wait(await_without_fallthrough(
        owned_error{&destroyed}, &after_await)), std::runtime_error);
    EXPECT_FALSE(after_await);
    EXPECT_TRUE(destroyed);
    destroyed = false;
    EXPECT_THROW(std::this_thread::sync_wait(await_without_fallthrough(
        owned_suspend_error{&destroyed}, &after_await)), std::runtime_error);
    EXPECT_FALSE(after_await);
    EXPECT_TRUE(destroyed);
    destroyed = false;
    EXPECT_FALSE(std::this_thread::sync_wait(await_without_fallthrough(
        owned_stopped{&destroyed}, &after_await)));
    EXPECT_FALSE(after_await);
    EXPECT_TRUE(destroyed);
    EXPECT_THROW(std::this_thread::sync_wait(await_without_fallthrough(
        a::adl_value_awaiter{true}, &after_await)), std::runtime_error);
    EXPECT_FALSE(after_await);
}

TEST(AwaitableSender, LockedAlgorithmsComposeThroughUnchangedCpos) {
    auto cached = forge::split(a::int_awaiter{});
    EXPECT_EQ(std::get<0>(*std::this_thread::sync_wait(cached)), 17);
    auto rvalue_only = forge::split(a::member_awaitable{});
    EXPECT_EQ(std::get<0>(*std::this_thread::sync_wait(rvalue_only)), 17);
    EXPECT_EQ(std::get<0>(*std::this_thread::sync_wait(cached)), 17);
}

struct recording_awaiter : a::void_awaiter {
    std::atomic<int>* count;
    explicit recording_awaiter(std::atomic<int>* value) : count(value) {}
    void await_resume() const noexcept { ++*count; }
};
forge::task<int> task_awaits_ordinary() { co_return co_await a::int_awaiter{}; }
forge::io::io_task<int> io_awaits_sender() {
    auto [value] = co_await forge::io::await_sender(a::int_awaiter{});
    co_return value;
}
TEST(AwaitableSender, ForgeEntryPointsAcceptCompatibleAwaitables) {
    std::atomic<int> count{0};
    forge::start_detached(recording_awaiter{&count});
    forge::async_scope scope;
    EXPECT_TRUE(scope.spawn(recording_awaiter{&count}));
    scope.wait();
    forge::resource_context context{1};
    EXPECT_TRUE(context.spawn(recording_awaiter{&count}));
    context.wait();
    EXPECT_EQ(count.load(), 3);
    forge::any_sender_of<ex::completion_signatures<ex::set_value_t(int)>> erased{a::int_awaiter{}};
    auto result = erased.sync_wait();
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 17);
    forge::any_sender_of<ex::completion_signatures<ex::set_value_t(int)>>
        lvalue_only{a::lvalue_sender{}};
    auto lvalue_result = lvalue_only.sync_wait();
    ASSERT_TRUE(lvalue_result);
    EXPECT_EQ(std::get<0>(*lvalue_result), 29);
    // Borrowing is repeatable, rather than consuming the stored source.
    EXPECT_EQ(std::get<0>(*lvalue_only.sync_wait()), 29);
    auto outcome = forge::wait_result(a::int_awaiter{});
    ASSERT_TRUE(outcome.has_value());
    EXPECT_EQ(std::get<0>(outcome.value()), 17);
    using erased_sigs = ex::completion_signatures<ex::set_value_t(int),
        ex::set_error_t(std::exception_ptr), ex::set_stopped_t()>;
    forge::erased_sender<erased_sigs> erased_source{a::int_awaiter{}};
    auto erased_result = std::this_thread::sync_wait(std::move(erased_source));
    ASSERT_TRUE(erased_result);
    EXPECT_EQ(std::get<0>(*erased_result), 17);
    auto task_result = std::this_thread::sync_wait(task_awaits_ordinary());
    ASSERT_TRUE(task_result);
    EXPECT_EQ(std::get<0>(*task_result), 17);
    auto io_result = std::this_thread::sync_wait(forge::io::as_sender(io_awaits_sender()));
    ASSERT_TRUE(io_result);
    EXPECT_EQ(std::get<0>(*io_result), 17);
}
} // namespace

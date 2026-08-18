#include <gtest/gtest.h>

#include <forge/io/timer_await.hpp>

#include "forge_counting_resource.hpp"

#include <chrono>
#include <condition_variable>
#include <exception>
#include <execution>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <system_error>
#include <thread>
#include <utility>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace {

namespace cio = forge::io;
using namespace std::chrono_literals;

struct completion_state {
    std::mutex mutex;
    std::condition_variable cv;
    std::optional<cio::io_result<>> result;
    std::exception_ptr error;
    bool stopped = false;

    [[nodiscard]] auto done() const noexcept -> bool {
        return result.has_value() || error || stopped;
    }
};

struct completion_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<completion_state> state;

    auto set_value(cio::io_result<> result) && noexcept -> void {
        {
            std::lock_guard lock{state->mutex};
            state->result.emplace(std::move(result));
        }
        state->cv.notify_all();
    }

    auto set_error(std::exception_ptr error) && noexcept -> void {
        {
            std::lock_guard lock{state->mutex};
            state->error = std::move(error);
        }
        state->cv.notify_all();
    }

    auto set_stopped() && noexcept -> void {
        {
            std::lock_guard lock{state->mutex};
            state->stopped = true;
        }
        state->cv.notify_all();
    }

    [[nodiscard]] auto get_env() const noexcept
        -> std::execution::empty_env {
        return {};
    }
};

[[nodiscard]] auto wait_done(
    const std::shared_ptr<completion_state>& state) -> bool {
    std::unique_lock lock{state->mutex};
    return state->cv.wait_for(lock, 2s, [&] { return state->done(); });
}

auto observe_resume_thread(
    forge::timer_context& context,
    std::thread::id* before,
    std::thread::id* after) -> cio::io_task<cio::io_result<>> {
    *before = std::this_thread::get_id();
    auto result = co_await cio::async_sleep_for(context, 1ms);
    *after = std::this_thread::get_id();
    co_return result;
}

} // namespace

TEST(ForgeTimerAwaitTest, SleepForZeroCompletesWithValue) {
    forge::timer_context context;

    auto result = std::execution::sync_wait(
        cio::as_sender(cio::async_sleep_for(context, 0ms)));

    ASSERT_TRUE(result.has_value());
    auto [io] = std::move(*result);
    EXPECT_TRUE(io.has_value());
    EXPECT_FALSE(io.error());
}

TEST(ForgeTimerAwaitTest, SleepUntilPastDeadlineCompletesWithValue) {
    forge::timer_context context;

    auto result = std::execution::sync_wait(
        cio::as_sender(cio::async_sleep_until(
            context,
            std::chrono::steady_clock::now() - 1ms)));

    ASSERT_TRUE(result.has_value());
    auto [io] = std::move(*result);
    EXPECT_TRUE(io.has_value());
}

TEST(ForgeTimerAwaitTest, DeadlineCompletionResumesOnTimerWorker) {
    forge::timer_context context;
    std::thread::id before;
    std::thread::id after;
    const auto caller = std::this_thread::get_id();

    auto result = std::execution::sync_wait(
        cio::as_sender(observe_resume_thread(context, &before, &after)));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(before, caller);
    EXPECT_NE(after, caller);
    EXPECT_NE(after, before);
}

TEST(ForgeTimerAwaitTest, IoEnvironmentStopCompletesStopped) {
    forge::timer_context context;
    std::inplace_stop_source stop;
    cio::io_env env;
    env.stop_token = stop.get_token();
    auto state = std::make_shared<completion_state>();
    auto sender = cio::as_sender(
        cio::async_sleep_for(context, 1h),
        env);
    auto operation = std::execution::connect(
        std::move(sender),
        completion_receiver{state});

    std::execution::start(operation);
    stop.request_stop();

    ASSERT_TRUE(wait_done(state));
    context.wait();
    std::lock_guard lock{state->mutex};
    EXPECT_TRUE(state->stopped);
    EXPECT_FALSE(state->result.has_value());
    EXPECT_FALSE(state->error);
}

TEST(ForgeTimerAwaitTest, ShutdownCompletesPendingSleepStopped) {
    forge::timer_context context;
    auto state = std::make_shared<completion_state>();
    auto sender = cio::as_sender(cio::async_sleep_for(context, 1h));
    auto operation = std::execution::connect(
        std::move(sender),
        completion_receiver{state});

    std::execution::start(operation);
    context.shutdown();

    ASSERT_TRUE(wait_done(state));
    context.wait();
    std::lock_guard lock{state->mutex};
    EXPECT_TRUE(state->stopped);
    EXPECT_FALSE(state->result.has_value());
    EXPECT_FALSE(state->error);
}

TEST(ForgeTimerAwaitTest, ShutdownRejectsNewSleepWithStopped) {
    forge::timer_context context;
    context.shutdown();

    auto result = std::execution::sync_wait(
        cio::as_sender(cio::async_sleep_for(context, 0ms)));

    EXPECT_FALSE(result.has_value());
}

TEST(ForgeTimerAwaitTest, ConnectAllocationFailureMapsToIoResultError) {
    forge_test::fail_next_resource resource;
    forge::timer_context context{
        forge::timer_context_options{.memory = &resource}};
    auto task = cio::async_sleep_for(context, 0ms);
    resource.fail_next_allocation();

    auto result = std::execution::sync_wait(
        cio::as_sender(std::move(task)));

    ASSERT_TRUE(result.has_value());
    auto [io] = std::move(*result);
    EXPECT_FALSE(io.has_value());
    EXPECT_EQ(io.error(), std::make_error_code(std::errc::io_error));
}

#else

TEST(ForgeTimerAwaitTest, CoroutinesNotAvailable) {
    GTEST_SKIP() << "C++20 coroutines not available on this toolchain";
}

#endif


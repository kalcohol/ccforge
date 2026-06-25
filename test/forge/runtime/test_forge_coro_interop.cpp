#include <gtest/gtest.h>

#include <forge/erased_sender.hpp>
#include <forge/io/coro.hpp>
#include <forge/io/result.hpp>
#include <forge/static_thread_pool.hpp>

#include <exception>
#include <execution>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <system_error>
#include <tuple>
#include <utility>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace {

namespace cio = forge::io::experimental;

struct interop_marker_error {};

template<class R>
struct stop_probe_op {
    using operation_state_concept = std::execution::operation_state_t;

    R receiver;

    auto start() & noexcept -> void {
        auto token = std::execution::get_stop_token(
            std::execution::get_env(receiver));
        if (token.stop_requested()) {
            std::execution::set_stopped(std::move(receiver));
        } else {
            std::execution::set_value(std::move(receiver), 9);
        }
    }
};

struct stop_probe_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_stopped_t()> {
        return {};
    }

    [[nodiscard]] auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R receiver) && -> stop_probe_op<R> {
        return stop_probe_op<R>{std::move(receiver)};
    }
};

auto await_just_task() -> cio::io_task<int> {
    auto [value] = co_await cio::await_sender(std::execution::just(41));
    co_return value + 1;
}

auto await_schedule_task() -> cio::io_task<bool> {
    const auto& env = co_await cio::this_io_env();
    co_await cio::await_sender(env.executor.schedule());
    co_return true;
}

auto await_error_task() -> cio::io_task<int> {
    co_await cio::await_sender(
        std::execution::just_error(interop_marker_error{}));
    co_return 0;
}

auto await_stopped_task() -> cio::io_task<int> {
    co_await cio::await_sender(std::execution::just_stopped());
    co_return 0;
}

auto await_move_only_task() -> cio::io_task<int> {
    auto [value] = co_await cio::await_sender(
        std::execution::just(std::make_unique<int>(17)));
    co_return *value;
}

auto await_stop_probe_task() -> cio::io_task<int> {
    auto [value] = co_await cio::await_sender(stop_probe_sender{});
    co_return value;
}

auto await_void_task() -> cio::io_task<void> {
    co_await cio::await_sender(std::execution::just());
}

auto await_io_result_task() -> cio::io_task<forge::io::io_result<std::size_t>> {
    co_return forge::io::io_result<std::size_t>::failure(
        std::make_error_code(std::errc::broken_pipe),
        3);
}

} // namespace

TEST(ForgeCoroInteropTest, AwaitSenderConsumesJust) {
    cio::io_env env;
    auto task = await_just_task();

    task.start(env);

    ASSERT_TRUE(task.done());
    EXPECT_EQ(std::move(task).result(), 42);
}

TEST(ForgeCoroInteropTest, AwaitSenderSchedulesOnExecutorRef) {
    forge::static_thread_pool pool{1};
    cio::io_env env;
    env.executor = cio::executor_ref{pool.get_scheduler()};
    auto task = await_schedule_task();

    task.start(env);
    pool.wait();

    ASSERT_TRUE(task.done());
    EXPECT_TRUE(std::move(task).result());
}

TEST(ForgeCoroInteropTest, AwaitSenderPropagatesError) {
    cio::io_env env;
    auto task = await_error_task();

    task.start(env);

    ASSERT_TRUE(task.done());
    EXPECT_THROW((void)std::move(task).result(), interop_marker_error);
}

TEST(ForgeCoroInteropTest, AwaitSenderPropagatesStopped) {
    cio::io_env env;
    auto task = await_stopped_task();

    task.start(env);

    ASSERT_TRUE(task.done());
    EXPECT_THROW((void)std::move(task).result(), cio::sender_stopped);
}

TEST(ForgeCoroInteropTest, AwaitSenderPreservesMoveOnlyValue) {
    cio::io_env env;
    auto task = await_move_only_task();

    task.start(env);

    ASSERT_TRUE(task.done());
    EXPECT_EQ(std::move(task).result(), 17);
}

TEST(ForgeCoroInteropTest, AwaitSenderExposesIoEnvStopToken) {
    std::stop_source stop;
    stop.request_stop();
    cio::io_env env;
    env.stop_token = stop.get_token();
    auto task = await_stop_probe_task();

    task.start(env);

    ASSERT_TRUE(task.done());
    EXPECT_THROW((void)std::move(task).result(), cio::sender_stopped);
}

TEST(ForgeCoroInteropTest, AsSenderExposesValueTask) {
    auto result = std::execution::sync_wait(
        cio::as_sender(await_just_task()));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(ForgeCoroInteropTest, AsSenderExposesVoidTask) {
    auto result = std::execution::sync_wait(
        cio::as_sender(await_void_task()));

    EXPECT_TRUE(result.has_value());
}

TEST(ForgeCoroInteropTest, AsSenderMapsStoppedTaskToStoppedCompletion) {
    auto result = std::execution::sync_wait(
        cio::as_sender(await_stopped_task()));

    EXPECT_FALSE(result.has_value());
}

TEST(ForgeCoroInteropTest, AsSenderKeepsIoResultAsValue) {
    auto result = std::execution::sync_wait(
        cio::as_sender(await_io_result_task()));

    ASSERT_TRUE(result.has_value());
    auto [io_result] = std::move(*result);
    auto [error, count] = io_result;
    EXPECT_EQ(error, std::make_error_code(std::errc::broken_pipe));
    EXPECT_EQ(count, 3u);
}

TEST(ForgeCoroInteropTest, AsSenderWorksAcrossErasedSenderBoundary) {
    using completions = std::execution::completion_signatures<
        std::execution::set_value_t(int),
        std::execution::set_error_t(std::exception_ptr),
        std::execution::set_stopped_t()>;

    forge::erased_sender<completions> erased{
        cio::as_sender(await_just_task())};
    auto result = std::execution::sync_wait(std::move(erased));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

#else

TEST(ForgeCoroInteropTest, CoroutinesNotAvailable) {
    GTEST_SKIP() << "C++20 coroutines not available on this toolchain";
}

#endif

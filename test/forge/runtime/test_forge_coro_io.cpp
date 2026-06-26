#include <gtest/gtest.h>

#include <forge/io/coro.hpp>
#include <forge/static_thread_pool.hpp>

#include <execution>
#include <memory_resource>
#include <stdexcept>
#include <stop_token>
#include <type_traits>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
#include <coroutine>

namespace {

namespace cio = forge::io;

struct env_shape_awaitable {
    bool await_ready() noexcept { return false; }
    bool await_suspend(std::coroutine_handle<>, const cio::io_env*) noexcept {
        return false;
    }
    int await_resume() noexcept { return 1; }
};

struct regular_awaitable {
    bool await_ready() noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) noexcept {}
    int await_resume() noexcept { return 7; }
};

static_assert(cio::io_awaitable<env_shape_awaitable>);
static_assert(!cio::io_awaitable<regular_awaitable>);
static_assert(!std::execution::scheduler<cio::executor_ref>);
static_assert(std::copy_constructible<cio::io_env>);
static_assert(std::move_constructible<cio::io_env>);
static_assert(!std::constructible_from<forge::any_scheduler, cio::executor_ref>);

auto observes_stop_token() -> cio::io_task<bool> {
    const auto& env = co_await cio::this_io_env();
    co_return env.stop_token.stop_requested();
}

auto observes_memory(std::pmr::memory_resource* expected) -> cio::io_task<bool> {
    const auto& env = co_await cio::this_io_env();
    co_return env.memory == expected;
}

auto observes_executor() -> cio::io_task<bool> {
    const auto& env = co_await cio::this_io_env();
    co_return static_cast<bool>(env.executor);
}

auto awaits_regular_awaitable() -> cio::io_task<int> {
    auto value = co_await regular_awaitable{};
    co_return value;
}

auto throws_from_task() -> cio::io_task<int> {
    throw std::runtime_error{"coro io failure"};
    co_return 0;
}

auto suspends_forever() -> cio::io_task<int> {
    co_await std::suspend_always{};
    co_return 1;
}

auto completes_void(bool* observed) -> cio::io_task<void> {
    const auto& env = co_await cio::this_io_env();
    *observed = env.memory != nullptr;
    co_return;
}

auto awaits_child_task() -> cio::io_task<bool> {
    co_return co_await observes_stop_token();
}

auto awaits_void_child_task(bool* observed) -> cio::io_task<bool> {
    co_await completes_void(observed);
    co_return *observed;
}

} // namespace

TEST(ForgeCoroIoTest, IoTaskPropagatesStopTokenAndMemory) {
    std::stop_source source;
    source.request_stop();
    std::pmr::monotonic_buffer_resource memory;

    cio::io_env env;
    env.stop_token = source.get_token();
    env.memory = &memory;

    auto stop_task = observes_stop_token();
    stop_task.start(env);
    ASSERT_TRUE(stop_task.done());
    EXPECT_TRUE(std::move(stop_task).result());

    auto memory_task = observes_memory(&memory);
    memory_task.start(env);
    ASSERT_TRUE(memory_task.done());
    EXPECT_TRUE(std::move(memory_task).result());
}

TEST(ForgeCoroIoTest, IoTaskCanAwaitChildTaskWithSameEnv) {
    std::stop_source source;
    source.request_stop();

    cio::io_env env;
    env.stop_token = source.get_token();

    auto task = awaits_child_task();
    task.start(env);

    ASSERT_TRUE(task.done());
    EXPECT_TRUE(std::move(task).result());
}

TEST(ForgeCoroIoTest, IoTaskCanAwaitVoidChildTask) {
    cio::io_env env;
    bool observed = false;

    auto task = awaits_void_child_task(&observed);
    task.start(env);

    ASSERT_TRUE(task.done());
    EXPECT_TRUE(std::move(task).result());
    EXPECT_TRUE(observed);
}

TEST(ForgeCoroIoTest, ExecutorRefAdaptsForgeScheduler) {
    forge::static_thread_pool pool{1};
    cio::io_env env;
    env.executor = cio::executor_ref{pool.get_scheduler()};

    EXPECT_TRUE(env.executor);

    auto scheduled = std::execution::sync_wait(env.executor.schedule());
    pool.wait();
    ASSERT_TRUE(scheduled.has_value());

    auto task = observes_executor();
    task.start(env);
    ASSERT_TRUE(task.done());
    EXPECT_TRUE(std::move(task).result());
}

TEST(ForgeCoroIoTest, IoTaskPassesThroughRegularAwaitables) {
    cio::io_env env;
    auto task = awaits_regular_awaitable();

    task.start(env);

    ASSERT_TRUE(task.done());
    EXPECT_EQ(std::move(task).result(), 7);
}

TEST(ForgeCoroIoTest, IoTaskRethrowsStoredException) {
    cio::io_env env;
    auto task = throws_from_task();

    task.start(env);

    ASSERT_TRUE(task.done());
    EXPECT_THROW((void)std::move(task).result(), std::runtime_error);
}

TEST(ForgeCoroIoTest, IoTaskReportsSuspendedState) {
    cio::io_env env;
    auto task = suspends_forever();

    task.start(env);

    EXPECT_FALSE(task.done());
    EXPECT_THROW((void)std::move(task).result(), std::logic_error);
}

TEST(ForgeCoroIoTest, VoidIoTaskUsesEnv) {
    cio::io_env env;
    bool observed = false;
    auto task = completes_void(&observed);

    task.start(env);

    ASSERT_TRUE(task.done());
    std::move(task).result();
    EXPECT_TRUE(observed);
}

#else

TEST(ForgeCoroIoTest, CoroutinesNotAvailable) {
    GTEST_SKIP() << "C++20 coroutines not available on this toolchain";
}

#endif

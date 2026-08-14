#include <gtest/gtest.h>

#include <forge/io/coro.hpp>
#include <forge/static_thread_pool.hpp>

#include <execution>
#include <memory_resource>
#include <stdexcept>
#include <type_traits>
#include <tuple>

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

struct noncopyable_env_awaitable {
    explicit noncopyable_env_awaitable(
        std::pmr::memory_resource** observed) noexcept
        : observed_(observed) {}

    noncopyable_env_awaitable(const noncopyable_env_awaitable&) = delete;
    auto operator=(const noncopyable_env_awaitable&)
        -> noncopyable_env_awaitable& = delete;
    noncopyable_env_awaitable(noncopyable_env_awaitable&&) = delete;
    auto operator=(noncopyable_env_awaitable&&)
        -> noncopyable_env_awaitable& = delete;

    bool await_ready() noexcept { return false; }
    bool await_suspend(
        std::coroutine_handle<>,
        const cio::io_env* env) noexcept {
        *observed_ = env == nullptr ? nullptr : env->memory;
        return false;
    }
    int await_resume() noexcept { return 11; }

private:
    std::pmr::memory_resource** observed_;
};

struct noncopyable_regular_awaitable {
    noncopyable_regular_awaitable() = default;
    noncopyable_regular_awaitable(const noncopyable_regular_awaitable&) = delete;
    auto operator=(const noncopyable_regular_awaitable&)
        -> noncopyable_regular_awaitable& = delete;
    noncopyable_regular_awaitable(noncopyable_regular_awaitable&&) = delete;
    auto operator=(noncopyable_regular_awaitable&&)
        -> noncopyable_regular_awaitable& = delete;

    bool await_ready() noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) noexcept {}
    int await_resume() noexcept { return 13; }
};

static_assert(cio::io_awaitable<env_shape_awaitable>);
static_assert(cio::io_awaitable<noncopyable_env_awaitable&>);
static_assert(!cio::io_awaitable<regular_awaitable>);
static_assert(!std::execution::scheduler<cio::executor_ref>);
static_assert(std::copy_constructible<cio::io_env>);
static_assert(std::move_constructible<cio::io_env>);
static_assert(!std::constructible_from<forge::any_scheduler, cio::executor_ref>);

template<class Task>
concept publicly_startable_io_task =
    requires(Task task, const cio::io_env& env) {
        task.start(env);
    };

static_assert(!publicly_startable_io_task<cio::io_task<int>>);
static_assert(!publicly_startable_io_task<cio::io_task<void>>);

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

auto awaits_noncopyable_lvalue(noncopyable_env_awaitable& awaitable)
    -> cio::io_task<int> {
    co_return co_await awaitable;
}

auto awaits_noncopyable_regular_lvalue(
    noncopyable_regular_awaitable& awaitable) -> cio::io_task<int> {
    co_return co_await awaitable;
}

auto throws_from_task() -> cio::io_task<int> {
    throw std::runtime_error{"coro io failure"};
    co_return 0;
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
    std::inplace_stop_source source;
    source.request_stop();
    std::pmr::monotonic_buffer_resource memory;

    cio::io_env env;
    env.stop_token = source.get_token();
    env.memory = &memory;

    auto stop_result = std::execution::sync_wait(
        cio::as_sender(observes_stop_token(), env));
    ASSERT_TRUE(stop_result.has_value());
    EXPECT_TRUE(std::get<0>(*stop_result));

    auto memory_result = std::execution::sync_wait(
        cio::as_sender(observes_memory(&memory), env));
    ASSERT_TRUE(memory_result.has_value());
    EXPECT_TRUE(std::get<0>(*memory_result));
}

TEST(ForgeCoroIoTest, IoTaskCanAwaitChildTaskWithSameEnv) {
    std::inplace_stop_source source;
    source.request_stop();

    cio::io_env env;
    env.stop_token = source.get_token();

    auto result = std::execution::sync_wait(
        cio::as_sender(awaits_child_task(), env));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result));
}

TEST(ForgeCoroIoTest, IoTaskCanAwaitVoidChildTask) {
    cio::io_env env;
    bool observed = false;

    auto result = std::execution::sync_wait(
        cio::as_sender(awaits_void_child_task(&observed), env));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result));
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

    auto result = std::execution::sync_wait(
        cio::as_sender(observes_executor(), env));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result));
}

TEST(ForgeCoroIoTest, IoTaskPassesThroughRegularAwaitables) {
    auto result = std::execution::sync_wait(
        cio::as_sender(awaits_regular_awaitable()));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 7);
}

TEST(ForgeCoroIoTest, IoTaskBorrowsNoncopyableLvalueAwaitables) {
    std::pmr::memory_resource* observed = nullptr;
    noncopyable_env_awaitable awaitable{&observed};
    std::pmr::monotonic_buffer_resource memory;
    cio::io_env env;
    env.memory = &memory;

    auto result = std::execution::sync_wait(
        cio::as_sender(awaits_noncopyable_lvalue(awaitable), env));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 11);
    EXPECT_EQ(observed, &memory);
}

TEST(ForgeCoroIoTest, IoTaskBorrowsNoncopyableRegularLvalueAwaitables) {
    noncopyable_regular_awaitable awaitable;

    auto result = std::execution::sync_wait(
        cio::as_sender(awaits_noncopyable_regular_lvalue(awaitable)));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 13);
}

TEST(ForgeCoroIoTest, IoTaskRethrowsStoredException) {
    EXPECT_THROW((void)std::execution::sync_wait(
                     cio::as_sender(throws_from_task())),
                 std::runtime_error);
}

TEST(ForgeCoroIoTest, VoidIoTaskUsesEnv) {
    cio::io_env env;
    bool observed = false;

    auto result = std::execution::sync_wait(
        cio::as_sender(completes_void(&observed), env));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(observed);
}

#else

TEST(ForgeCoroIoTest, CoroutinesNotAvailable) {
    GTEST_SKIP() << "C++20 coroutines not available on this toolchain";
}

#endif

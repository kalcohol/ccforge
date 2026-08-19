#include <gtest/gtest.h>

#include <forge/erased_sender.hpp>
#include <forge/io/coro.hpp>
#include <forge/io/result.hpp>
#include <forge/static_thread_pool.hpp>

#include "forge_operation_destroy.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <condition_variable>
#include <execution>
#include <chrono>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace {

namespace cio = forge::io;
using namespace std::chrono_literals;

using sender_awaitable_probe =
    decltype(cio::await_sender(std::execution::just()));
static_assert(std::move_constructible<sender_awaitable_probe>);

struct interop_marker_error {};

enum class gated_completion {
    value,
    error,
    stopped
};

struct gated_async_state {
    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool release = false;
    bool returned = false;
};

auto wait_until_started(const std::shared_ptr<gated_async_state>& state)
    -> void {
    std::unique_lock lock{state->mtx};
    state->cv.wait(lock, [&] { return state->started; });
}

auto release_async_completion(const std::shared_ptr<gated_async_state>& state)
    -> void {
    {
        std::lock_guard lock{state->mtx};
        state->release = true;
    }
    state->cv.notify_all();
}

auto wait_until_returned(const std::shared_ptr<gated_async_state>& state)
    -> void {
    std::unique_lock lock{state->mtx};
    state->cv.wait(lock, [&] { return state->returned; });
}

template<class T>
struct task_result_state {
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    std::optional<T> value;
    std::exception_ptr error;
    bool stopped = false;
};

template<class T>
struct task_result_receiver {
    using receiver_concept = std::execution::receiver_t;

    struct env {
        std::inplace_stop_source* source = nullptr;

        friend auto tag_invoke(
            std::execution::get_stop_token_t,
            const env& self) noexcept -> std::inplace_stop_token {
            return self.source ? self.source->get_token() : std::inplace_stop_token{};
        }
    };

    std::shared_ptr<task_result_state<T>> state;
    std::inplace_stop_source* stop_source = nullptr;

    auto set_value(T value) && noexcept -> void {
        {
            std::lock_guard lock{state->mtx};
            state->value.emplace(std::move(value));
            state->done = true;
        }
        state->cv.notify_all();
    }

    template<class Error>
    auto set_error(Error&& error) && noexcept -> void {
        {
            std::lock_guard lock{state->mtx};
            if constexpr (std::is_same_v<std::decay_t<Error>, std::exception_ptr>) {
                state->error = static_cast<Error&&>(error);
            } else {
                state->error = std::make_exception_ptr(static_cast<Error&&>(error));
            }
            state->done = true;
        }
        state->cv.notify_all();
    }

    auto set_stopped() && noexcept -> void {
        {
            std::lock_guard lock{state->mtx};
            state->stopped = true;
            state->done = true;
        }
        state->cv.notify_all();
    }

    [[nodiscard]] auto get_env() const noexcept -> env {
        return env{stop_source};
    }
};

template<class T>
auto wait_task_done(const std::shared_ptr<task_result_state<T>>& state)
    -> void {
    std::unique_lock lock{state->mtx};
    state->cv.wait(lock, [&] { return state->done; });
}

template<class T>
auto wait_task_done_for(const std::shared_ptr<task_result_state<T>>& state,
                        std::chrono::milliseconds timeout) -> bool {
    std::unique_lock lock{state->mtx};
    return state->cv.wait_for(lock, timeout, [&] { return state->done; });
}

struct stop_wait_state {
    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool completed = false;
    int stop_attempts = 0;
};

auto wait_stop_wait_started(const std::shared_ptr<stop_wait_state>& state)
    -> void {
    std::unique_lock lock{state->mtx};
    state->cv.wait(lock, [&] { return state->started; });
}

template<class R>
struct stop_wait_op {
    using operation_state_concept = std::execution::operation_state_t;

    struct stop_callback {
        stop_wait_op* self = nullptr;

        void operator()() noexcept {
            self->complete_stopped();
        }
    };

    using env_t = std::execution::env_of_t<R>;
    using token_t = decltype(std::execution::get_stop_token(std::declval<env_t>()));
    using callback_t = std::stop_callback_for_t<token_t, stop_callback>;

    R receiver;
    std::shared_ptr<stop_wait_state> state;
    std::optional<callback_t> callback;
    std::atomic<bool> done{false};

    stop_wait_op(R rcvr, std::shared_ptr<stop_wait_state> st)
        : receiver(std::move(rcvr))
        , state(std::move(st))
    {}

    stop_wait_op(stop_wait_op&&) = delete;
    auto operator=(stop_wait_op&&) -> stop_wait_op& = delete;
    stop_wait_op(const stop_wait_op&) = delete;
    auto operator=(const stop_wait_op&) -> stop_wait_op& = delete;

    auto start() & noexcept -> void {
        auto token = std::execution::get_stop_token(
            std::execution::get_env(receiver));
        {
            std::lock_guard lock{state->mtx};
            state->started = true;
        }
        state->cv.notify_all();

        if (token.stop_requested()) {
            complete_stopped();
            return;
        }
        if (token.stop_possible()) {
            callback.emplace(token, stop_callback{this});
        }
    }

    auto complete_stopped() noexcept -> void {
        {
            std::lock_guard lock{state->mtx};
            ++state->stop_attempts;
        }
        if (done.exchange(true, std::memory_order_acq_rel)) {
            state->cv.notify_all();
            return;
        }
        {
            std::lock_guard lock{state->mtx};
            state->completed = true;
        }
        state->cv.notify_all();
        callback.reset();
        std::execution::set_stopped(std::move(receiver));
    }
};

struct stop_wait_sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<stop_wait_state> state;

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
    auto connect(R receiver) && -> stop_wait_op<R> {
        return stop_wait_op<R>{std::move(receiver), std::move(state)};
    }
};

template<class R>
struct gated_async_delivery {
    R receiver;
    std::shared_ptr<gated_async_state> state;
    gated_completion completion = gated_completion::value;

    gated_async_delivery(R rcvr,
                         std::shared_ptr<gated_async_state> st,
                         gated_completion c)
        : receiver(std::move(rcvr))
        , state(std::move(st))
        , completion(c)
    {}
};

template<class R>
struct gated_async_op {
    using operation_state_concept = std::execution::operation_state_t;

    std::shared_ptr<gated_async_delivery<R>> delivery;

    gated_async_op(R rcvr,
                   std::shared_ptr<gated_async_state> st,
                   gated_completion c)
        : delivery(std::make_shared<gated_async_delivery<R>>(
              std::move(rcvr),
              std::move(st),
              c))
    {}

    gated_async_op(gated_async_op&&) = delete;
    auto operator=(gated_async_op&&) -> gated_async_op& = delete;
    gated_async_op(const gated_async_op&) = delete;
    auto operator=(const gated_async_op&) -> gated_async_op& = delete;

    ~gated_async_op() {
        if (delivery) {
            release_async_completion(delivery->state);
        }
    }

    auto start() & noexcept -> void {
        try {
            auto shared = delivery;
            std::thread([shared] {
                {
                    std::unique_lock lock{shared->state->mtx};
                    shared->state->started = true;
                    shared->state->cv.notify_all();
                    shared->state->cv.wait(
                        lock,
                        [&] { return shared->state->release; });
                }

                switch (shared->completion) {
                case gated_completion::value:
                    std::execution::set_value(std::move(shared->receiver), 21);
                    break;
                case gated_completion::error:
                    std::execution::set_error(
                        std::move(shared->receiver),
                        interop_marker_error{});
                    break;
                case gated_completion::stopped:
                    std::execution::set_stopped(std::move(shared->receiver));
                    break;
                }

                {
                    std::lock_guard lock{shared->state->mtx};
                    shared->state->returned = true;
                }
                shared->state->cv.notify_all();
            }).detach();
        } catch (...) {
            std::execution::set_error(
                std::move(delivery->receiver),
                std::current_exception());
        }
    }
};

struct gated_async_sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<gated_async_state> state;
    gated_completion completion = gated_completion::value;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_error_t(interop_marker_error),
            std::execution::set_error_t(std::exception_ptr),
            std::execution::set_stopped_t()> {
        return {};
    }

    [[nodiscard]] auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R receiver) && -> gated_async_op<R> {
        return gated_async_op<R>{
            std::move(receiver),
            std::move(state),
            completion};
    }
};

struct inline_probe_state {
    bool start_returned = false;
    bool continuation_ran_before_start_returned = false;
};

template<class R>
struct inline_probe_op {
    using operation_state_concept = std::execution::operation_state_t;

    R receiver;
    inline_probe_state* state = nullptr;

    auto start() & noexcept -> void {
        std::execution::set_value(std::move(receiver), 5);
        state->start_returned = true;
    }
};

struct inline_probe_sender {
    using sender_concept = std::execution::sender_t;

    inline_probe_state* state = nullptr;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int)> {
        return {};
    }

    [[nodiscard]] auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R receiver) && -> inline_probe_op<R> {
        return inline_probe_op<R>{std::move(receiver), state};
    }
};

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

auto await_schedule_thread_task(
    std::thread::id* before,
    std::thread::id* after) -> cio::io_task<bool> {
    *before = std::this_thread::get_id();
    const auto& env = co_await cio::this_io_env();
    co_await cio::await_sender(env.executor.schedule());
    *after = std::this_thread::get_id();
    co_return *after != *before;
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

auto await_moved_from_child_task() -> cio::io_task<int> {
    auto child = await_just_task();
    auto moved = std::move(child);
    (void)moved;
    co_await std::move(child);
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

auto await_gated_async_task(std::shared_ptr<gated_async_state> state)
    -> cio::io_task<int> {
    auto [value] = co_await cio::await_sender(gated_async_sender{
        std::move(state),
        gated_completion::value});
    co_return value + 1;
}

auto await_gated_async_error_task(std::shared_ptr<gated_async_state> state)
    -> cio::io_task<int> {
    co_await cio::await_sender(gated_async_sender{
        std::move(state),
        gated_completion::error});
    co_return 0;
}

auto await_gated_async_stopped_task(std::shared_ptr<gated_async_state> state)
    -> cio::io_task<int> {
    co_await cio::await_sender(gated_async_sender{
        std::move(state),
        gated_completion::stopped});
    co_return 0;
}

auto await_stop_wait_task(std::shared_ptr<stop_wait_state> state)
    -> cio::io_task<int> {
    auto [value] = co_await cio::await_sender(stop_wait_sender{std::move(state)});
    co_return value;
}

auto await_inline_probe_task(inline_probe_state* state) -> cio::io_task<bool> {
    auto [value] = co_await cio::await_sender(inline_probe_sender{state});
    state->continuation_ran_before_start_returned = !state->start_returned;
    co_return value == 5;
}

#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
auto current_stack_position() noexcept -> std::uintptr_t {
#if defined(_MSC_VER)
    return reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
#else
    return reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
#endif
}

struct inline_child_chain_result {
    int value = 0;
    std::uintptr_t stack_span = 0;
};

auto inline_child_task() -> cio::io_task<int> {
    co_return 1;
}

auto await_inline_child_chain(int count)
    -> cio::io_task<inline_child_chain_result> {
    int value = 0;
    auto low = std::numeric_limits<std::uintptr_t>::max();
    std::uintptr_t high = 0;

    for (int i = 0; i < count; ++i) {
        value += co_await inline_child_task();
        const auto position = current_stack_position();
        low = std::min(low, position);
        high = std::max(high, position);
    }

    co_return inline_child_chain_result{
        .value = value,
        .stack_span = high - low,
    };
}

// Never completes: the awaiting chain stays suspended until abandoned.
struct hang_forever_awaitable {
    [[nodiscard]] auto await_ready() const noexcept -> bool {
        return false;
    }

    auto await_suspend(std::coroutine_handle<>, const cio::io_env*) noexcept
        -> void {}

    auto await_resume() noexcept -> void {}
};

auto hang_leaf_task() -> cio::io_task<int> {
    co_await hang_forever_awaitable{};
    co_return 0;
}

auto deep_suspended_chain(int depth) -> cio::io_task<int> {
    if (depth <= 0) {
        co_return co_await hang_leaf_task();
    }
    co_return co_await deep_suspended_chain(depth - 1);
}

} // namespace

TEST(ForgeCoroInteropTest, AwaitSenderConsumesJust) {
    auto result = std::execution::sync_wait(
        cio::as_sender(await_just_task()));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(ForgeCoroInteropTest, AwaitSenderSchedulesOnExecutorRef) {
    forge::static_thread_pool pool{1};
    cio::io_env env;
    env.executor = cio::executor_ref{pool.get_scheduler()};
    std::thread::id before;
    std::thread::id after;
    const auto caller = std::this_thread::get_id();
    auto result = std::make_shared<task_result_state<bool>>();
    auto sender = cio::as_sender(
        await_schedule_thread_task(&before, &after),
        env);
    auto op = std::execution::connect(
        std::move(sender),
        task_result_receiver<bool>{result});

    std::execution::start(op);
    wait_task_done(result);
    pool.wait();

    std::lock_guard lock{result->mtx};
    ASSERT_TRUE(result->value.has_value());
    EXPECT_TRUE(*result->value);
    EXPECT_EQ(before, caller);
    EXPECT_NE(after, caller);
    EXPECT_NE(after, before);
    EXPECT_FALSE(result->error);
    EXPECT_FALSE(result->stopped);
}

TEST(ForgeCoroInteropTest, AwaitSenderPropagatesError) {
    EXPECT_THROW((void)std::execution::sync_wait(
                     cio::as_sender(await_error_task())),
                 interop_marker_error);
}

TEST(ForgeCoroInteropTest, AwaitSenderPropagatesStopped) {
    auto result = std::execution::sync_wait(
        cio::as_sender(await_stopped_task()));

    EXPECT_FALSE(result.has_value());
}

TEST(ForgeCoroInteropTest, AwaitingMovedFromChildReportsError) {
    EXPECT_THROW((void)std::execution::sync_wait(
                     cio::as_sender(await_moved_from_child_task())),
                 std::logic_error);
}

TEST(ForgeCoroInteropTest, MovedFromTaskSenderReportsError) {
    auto task = await_just_task();
    auto moved = std::move(task);
    (void)moved;

    EXPECT_THROW((void)std::execution::sync_wait(
                     cio::as_sender(std::move(task))),
                 std::logic_error);
}

TEST(ForgeCoroInteropTest, AwaitSenderPreservesMoveOnlyValue) {
    auto result = std::execution::sync_wait(
        cio::as_sender(await_move_only_task()));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 17);
}

TEST(ForgeCoroInteropTest, AwaitSenderExposesIoEnvStopToken) {
    std::inplace_stop_source stop;
    stop.request_stop();
    cio::io_env env;
    env.stop_token = stop.get_token();
    auto result = std::execution::sync_wait(
        cio::as_sender(await_stop_probe_task(), env));

    EXPECT_FALSE(result.has_value());
}

TEST(ForgeCoroInteropTest, AsSenderPropagatesReceiverStopToken) {
    auto wait_state = std::make_shared<stop_wait_state>();
    auto result = std::make_shared<task_result_state<int>>();
    std::inplace_stop_source receiver_stop;
    auto sender = cio::as_sender(await_stop_wait_task(wait_state));
    auto op = std::execution::connect(
        std::move(sender),
        task_result_receiver<int>{result, &receiver_stop});

    std::execution::start(op);
    wait_stop_wait_started(wait_state);
    {
        std::lock_guard lock{result->mtx};
        EXPECT_FALSE(result->done);
    }

    receiver_stop.request_stop();

    ASSERT_TRUE(wait_task_done_for(result, 500ms));
    {
        std::lock_guard lock{result->mtx};
        EXPECT_TRUE(result->stopped);
        EXPECT_FALSE(result->value.has_value());
        EXPECT_FALSE(result->error);
    }
    {
        std::lock_guard lock{wait_state->mtx};
        EXPECT_TRUE(wait_state->completed);
        EXPECT_EQ(wait_state->stop_attempts, 1);
    }
}

TEST(ForgeCoroInteropTest, AsSenderStillPropagatesIoEnvStopTokenAfterStart) {
    auto wait_state = std::make_shared<stop_wait_state>();
    auto result = std::make_shared<task_result_state<int>>();
    std::inplace_stop_source env_stop;
    cio::io_env env;
    env.stop_token = env_stop.get_token();
    auto sender = cio::as_sender(await_stop_wait_task(wait_state), env);
    auto op = std::execution::connect(
        std::move(sender),
        task_result_receiver<int>{result});

    std::execution::start(op);
    wait_stop_wait_started(wait_state);
    {
        std::lock_guard lock{result->mtx};
        EXPECT_FALSE(result->done);
    }

    env_stop.request_stop();

    ASSERT_TRUE(wait_task_done_for(result, 500ms));
    {
        std::lock_guard lock{result->mtx};
        EXPECT_TRUE(result->stopped);
        EXPECT_FALSE(result->value.has_value());
        EXPECT_FALSE(result->error);
    }
    {
        std::lock_guard lock{wait_state->mtx};
        EXPECT_TRUE(wait_state->completed);
        EXPECT_EQ(wait_state->stop_attempts, 1);
    }
}

TEST(ForgeCoroInteropTest, AsSenderCompletesOnceWhenIoEnvAndReceiverStopRace) {
    constexpr int iterations = 64;
    for (int i = 0; i < iterations; ++i) {
        auto wait_state = std::make_shared<stop_wait_state>();
        auto result = std::make_shared<task_result_state<int>>();
        std::inplace_stop_source env_stop;
        std::inplace_stop_source receiver_stop;
        cio::io_env env;
        env.stop_token = env_stop.get_token();
        auto sender = cio::as_sender(await_stop_wait_task(wait_state), env);
        auto op = std::execution::connect(
            std::move(sender),
            task_result_receiver<int>{result, &receiver_stop});
        std::atomic<bool> go{false};

        std::execution::start(op);
        wait_stop_wait_started(wait_state);

        std::thread env_thread([&] {
            while (!go.load(std::memory_order_acquire)) {}
            env_stop.request_stop();
        });
        std::thread receiver_thread([&] {
            while (!go.load(std::memory_order_acquire)) {}
            receiver_stop.request_stop();
        });

        go.store(true, std::memory_order_release);
        env_thread.join();
        receiver_thread.join();

        ASSERT_TRUE(wait_task_done_for(result, 500ms));
        {
            std::lock_guard lock{result->mtx};
            EXPECT_TRUE(result->stopped);
            EXPECT_FALSE(result->value.has_value());
            EXPECT_FALSE(result->error);
        }
        {
            std::lock_guard lock{wait_state->mtx};
            EXPECT_TRUE(wait_state->completed);
            EXPECT_EQ(wait_state->stop_attempts, 1);
        }
    }
}

TEST(ForgeCoroInteropTest, AsSenderExposesVoidTask) {
    auto result = std::execution::sync_wait(
        cio::as_sender(await_void_task()));

    EXPECT_TRUE(result.has_value());
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

TEST(ForgeCoroInteropTest, AwaitSenderCompletesAfterAsyncSuspension) {
    auto state = std::make_shared<gated_async_state>();
    auto result = std::make_shared<task_result_state<int>>();
    auto sender = cio::as_sender(await_gated_async_task(state));
    auto op = std::execution::connect(
        std::move(sender),
        task_result_receiver<int>{result});

    std::execution::start(op);
    wait_until_started(state);
    {
        std::lock_guard lock{result->mtx};
        EXPECT_FALSE(result->done);
    }
    release_async_completion(state);
    wait_task_done(result);
    wait_until_returned(state);

    std::lock_guard lock{result->mtx};
    ASSERT_TRUE(result->value.has_value());
    EXPECT_EQ(*result->value, 22);
    EXPECT_FALSE(result->error);
    EXPECT_FALSE(result->stopped);
}

TEST(ForgeCoroInteropTest, InlineCompletionDoesNotRecursivelyResumeFromStart) {
    inline_probe_state probe;

    auto result = std::execution::sync_wait(
        cio::as_sender(await_inline_probe_task(&probe)));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result));
    EXPECT_TRUE(probe.start_returned);
    EXPECT_FALSE(probe.continuation_ran_before_start_returned);
}

TEST(ForgeCoroInteropTest, InlineChildTasksUseBoundedNativeStack) {
    constexpr int child_count = 2048;

    auto result = std::execution::sync_wait(
        cio::as_sender(await_inline_child_chain(child_count)));

    ASSERT_TRUE(result.has_value());
    const auto& chain = std::get<0>(*result);
    EXPECT_EQ(chain.value, child_count);
    EXPECT_LT(chain.stack_span, 16u * 1024u);
}

// Destroying a suspended chain of this depth recursed one native frame set
// per level through nested task_awaitable destructors before the iterative
// chain walk; the depth here overflows an 8 MiB stack under the recursive
// scheme.
TEST(ForgeCoroInteropTest, AbandoningDeepSuspendedChainUsesBoundedNativeStack) {
    constexpr int depth = 400'000;
    auto result = std::make_shared<task_result_state<int>>();
    auto factory = [&] {
        return std::execution::connect(
            cio::as_sender(deep_suspended_chain(depth)),
            task_result_receiver<int>{result});
    };
    using op_t = decltype(factory());

    bool destroyed = false;
    forge_test::operation_destroy_context<op_t> context{&destroyed};
    auto& op = context.emplace_from(factory);
    std::execution::start(op);

    // The chain is parked on the never-completing leaf; dropping the
    // operation abandons every suspended frame at once.
    context.reset();

    std::lock_guard lock{result->mtx};
    EXPECT_FALSE(result->done);
    EXPECT_FALSE(result->value.has_value());
    EXPECT_FALSE(result->error);
    EXPECT_FALSE(result->stopped);
}

TEST(ForgeCoroInteropTest, AwaitSenderRejectsMoveAfterStart) {
    auto awaitable = cio::await_sender(std::execution::just());
    cio::io_env env;

    (void)awaitable.await_suspend(std::noop_coroutine(), &env);

    EXPECT_THROW(
        (void)sender_awaitable_probe{std::move(awaitable)},
        std::logic_error);
}

TEST(ForgeCoroInteropTest, AwaitSenderPropagatesAsyncError) {
    auto state = std::make_shared<gated_async_state>();
    auto result = std::make_shared<task_result_state<int>>();
    auto sender = cio::as_sender(await_gated_async_error_task(state));
    auto op = std::execution::connect(
        std::move(sender),
        task_result_receiver<int>{result});

    std::execution::start(op);
    wait_until_started(state);
    {
        std::lock_guard lock{result->mtx};
        EXPECT_FALSE(result->done);
    }
    release_async_completion(state);
    wait_task_done(result);
    wait_until_returned(state);

    std::lock_guard lock{result->mtx};
    ASSERT_TRUE(result->error);
    EXPECT_THROW(std::rethrow_exception(result->error), interop_marker_error);
    EXPECT_FALSE(result->value.has_value());
    EXPECT_FALSE(result->stopped);
}

TEST(ForgeCoroInteropTest, AwaitSenderPropagatesAsyncStopped) {
    auto state = std::make_shared<gated_async_state>();
    auto result = std::make_shared<task_result_state<int>>();
    auto sender = cio::as_sender(await_gated_async_stopped_task(state));
    auto op = std::execution::connect(
        std::move(sender),
        task_result_receiver<int>{result});

    std::execution::start(op);
    wait_until_started(state);
    {
        std::lock_guard lock{result->mtx};
        EXPECT_FALSE(result->done);
    }
    release_async_completion(state);
    wait_task_done(result);
    wait_until_returned(state);

    std::lock_guard lock{result->mtx};
    EXPECT_TRUE(result->stopped);
    EXPECT_FALSE(result->value.has_value());
    EXPECT_FALSE(result->error);
}

#else

TEST(ForgeCoroInteropTest, CoroutinesNotAvailable) {
    GTEST_SKIP() << "C++20 coroutines not available on this toolchain";
}

#endif

#include <gtest/gtest.h>
#include <forge/runtime_context.hpp>
#include <execution>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <tuple>

namespace {

using namespace std::chrono_literals;

struct stopped_receiver {
    using receiver_concept = std::execution::receiver_t;

    bool* stopped;

    void set_value() && noexcept { *stopped = false; }
    void set_error(std::exception_ptr) && noexcept { *stopped = false; }
    void set_stopped() && noexcept { *stopped = true; }
    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
};

struct timer_state {
    std::mutex mtx;
    std::condition_variable cv;
    bool value = false;
    bool stopped = false;

    bool done() const noexcept {
        return value || stopped;
    }
};

struct timer_receiver {
    using receiver_concept = std::execution::receiver_t;

    timer_state* state;

    void set_value() && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->value = true;
        }
        state->cv.notify_all();
    }

    void set_error(std::exception_ptr) && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->stopped = true;
        }
        state->cv.notify_all();
    }

    void set_stopped() && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->stopped = true;
        }
        state->cv.notify_all();
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

bool wait_done(timer_state& state) {
    std::unique_lock lk{state.mtx};
    return state.cv.wait_for(lk, 2s, [&] { return state.done(); });
}

bool wait_done_for(timer_state& state, std::chrono::milliseconds duration) {
    std::unique_lock lk{state.mtx};
    return state.cv.wait_for(lk, duration, [&] { return state.done(); });
}

} // namespace

static_assert(std::execution::scheduler<forge::runtime_context::scheduler>);

TEST(RuntimeContextTest, CpuScheduleRunsOnWorkerThread) {
    forge::runtime_context ctx{1};
    auto main_thread = std::this_thread::get_id();

    auto result = std::execution::sync_wait(
        std::execution::schedule(ctx.get_scheduler())
        | std::execution::then([] { return std::this_thread::get_id(); }));

    ASSERT_TRUE(result.has_value());
    EXPECT_NE(std::get<0>(*result), main_thread);
}

TEST(RuntimeContextTest, ScheduleAfterZeroCompletes) {
    forge::runtime_context ctx{1};

    auto result = std::execution::sync_wait(ctx.schedule_after(0ms));

    EXPECT_TRUE(result.has_value());
}

TEST(RuntimeContextTest, ScheduleAfterDoesNotCompleteBeforeDeadline) {
    forge::runtime_context ctx{1};
    timer_state state;
    auto op = std::execution::connect(
        ctx.schedule_after(100ms),
        timer_receiver{&state});

    std::execution::start(op);

    EXPECT_FALSE(wait_done_for(state, 10ms));
    ASSERT_TRUE(wait_done(state));
    EXPECT_TRUE(state.value);
    EXPECT_FALSE(state.stopped);
}

TEST(RuntimeContextTest, ShutdownMakesNewCpuScheduleStopped) {
    forge::runtime_context ctx{1};
    auto sch = ctx.get_scheduler();
    ctx.shutdown();

    auto result = std::execution::sync_wait(std::execution::schedule(sch));

    EXPECT_FALSE(result.has_value());
}

TEST(RuntimeContextTest, DirectScheduleAfterShutdownCompletesStopped) {
    forge::runtime_context ctx{1};
    auto sch = ctx.get_scheduler();
    ctx.shutdown();

    bool stopped = false;
    auto op = std::execution::connect(
        std::execution::schedule(sch),
        stopped_receiver{&stopped});

    std::execution::start(op);

    EXPECT_TRUE(stopped);
}

TEST(RuntimeContextTest, ShutdownCompletesPendingTimerStopped) {
    forge::runtime_context ctx{1};
    timer_state state;
    auto op = std::execution::connect(
        ctx.schedule_after(1h),
        timer_receiver{&state});

    std::execution::start(op);
    ctx.shutdown();

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state.value);
    EXPECT_TRUE(state.stopped);
}

TEST(RuntimeContextTest, WaitDrainsAcceptedCpuWork) {
    forge::runtime_context ctx{1};
    auto sch = ctx.get_scheduler();
    std::atomic<int> completed{0};

    for (int i = 0; i < 4; ++i) {
        std::execution::start_detached(
            std::execution::schedule(sch)
            | std::execution::then([&] {
                completed.fetch_add(1, std::memory_order_relaxed);
            }));
    }

    ctx.wait();

    EXPECT_EQ(completed.load(std::memory_order_relaxed), 4);
}

TEST(RuntimeContextTest, WaitDrainsTimerWork) {
    forge::runtime_context ctx{1};
    std::atomic<bool> completed{false};

    std::execution::start_detached(
        ctx.schedule_after(20ms)
        | std::execution::then([&] {
            completed.store(true, std::memory_order_relaxed);
        }));

    ctx.wait();

    EXPECT_TRUE(completed.load(std::memory_order_relaxed));
}

TEST(RuntimeContextTest, WaitDrainsCpuToTimerHandoff) {
    forge::runtime_context ctx{1};
    auto scheduler = ctx.get_scheduler();
    std::atomic<bool> completed{false};

    std::execution::start_detached(
        std::execution::schedule(scheduler)
        | std::execution::then([&] {
            std::execution::start_detached(
                ctx.schedule_after(0ms)
                | std::execution::then([&] {
                    completed.store(true, std::memory_order_relaxed);
                }));
        }));

    ctx.wait();

    EXPECT_TRUE(completed.load(std::memory_order_relaxed));
}

TEST(RuntimeContextTest, WaitDrainsTimerToCpuHandoff) {
    forge::runtime_context ctx{1};
    auto scheduler = ctx.get_scheduler();
    std::atomic<bool> completed{false};

    std::execution::start_detached(
        ctx.schedule_after(0ms)
        | std::execution::then([&] {
            std::execution::start_detached(
                std::execution::schedule(scheduler)
                | std::execution::then([&] {
                    completed.store(true, std::memory_order_relaxed);
                }));
        }));

    ctx.wait();

    EXPECT_TRUE(completed.load(std::memory_order_relaxed));
}

TEST(RuntimeContextTest, ShutdownThenWaitIsIdempotent) {
    forge::runtime_context ctx{1};
    ctx.shutdown();
    ctx.wait();
    ctx.shutdown();
    ctx.wait();

    SUCCEED();
}

TEST(RuntimeContextTest, ZeroThreadCountNormalizesToOne) {
    forge::runtime_context ctx{0};

    EXPECT_GE(ctx.thread_count(), 1u);
}

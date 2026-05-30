#include <gtest/gtest.h>
#include <forge/timer_context.hpp>
#include "forge_counting_resource.hpp"
#include <execution>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

namespace {

using namespace std::chrono_literals;

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

struct pre_stopped_timer_receiver : timer_receiver {
    std::inplace_stop_source* source;

    auto get_env() const noexcept {
        return std::execution::make_env(
            std::execution::make_prop(
                std::execution::get_stop_token_t{}, source->get_token()));
    }
};

struct stopped_timer_receiver : timer_receiver {
    std::inplace_stop_source* source;

    auto get_env() const noexcept {
        return std::execution::make_env(
            std::execution::make_prop(
                std::execution::get_stop_token_t{}, source->get_token()));
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

TEST(TimerContextTest, ScheduleAfterZeroCompletes) {
    forge::timer_context ctx;

    auto result = std::execution::sync_wait(ctx.schedule_after(0ms));

    EXPECT_TRUE(result.has_value());
}

TEST(TimerContextTest, CustomMemoryResourceControlsTimerStorage) {
    forge_test::counting_resource resource;

    {
        forge::timer_context ctx{
            forge::timer_context_options{.memory = &resource}};

        auto result = std::execution::sync_wait(ctx.schedule_after(0ms));

        EXPECT_TRUE(result.has_value());
        EXPECT_GT(resource.allocations(), 0u);
        ctx.shutdown();
        ctx.wait();
    }

    EXPECT_EQ(resource.allocations(), resource.deallocations());
}

TEST(TimerContextTest, ScheduleAfterDoesNotCompleteBeforeDeadline) {
    forge::timer_context ctx;
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

TEST(TimerContextTest, MultipleTimersCompleteInDeadlineOrder) {
    forge::timer_context ctx;
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<int> order;

    struct order_receiver {
        using receiver_concept = std::execution::receiver_t;

        std::mutex* mtx;
        std::condition_variable* cv;
        std::vector<int>* order;
        int value;

        void set_value() && noexcept {
            {
                std::lock_guard lk{*mtx};
                order->push_back(value);
            }
            cv->notify_all();
        }

        void set_error(std::exception_ptr) && noexcept {}
        void set_stopped() && noexcept {}
        auto get_env() const noexcept -> std::execution::empty_env { return {}; }
    };

    auto op1 = std::execution::connect(
        ctx.schedule_after(80ms),
        order_receiver{&mtx, &cv, &order, 3});
    auto op2 = std::execution::connect(
        ctx.schedule_after(10ms),
        order_receiver{&mtx, &cv, &order, 1});
    auto op3 = std::execution::connect(
        ctx.schedule_after(40ms),
        order_receiver{&mtx, &cv, &order, 2});

    std::execution::start(op1);
    std::execution::start(op2);
    std::execution::start(op3);

    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return order.size() == 3; }));
    }

    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST(TimerContextTest, ShutdownRejectsNewTimersWithStopped) {
    forge::timer_context ctx;
    ctx.shutdown();

    auto result = std::execution::sync_wait(ctx.schedule_after(0ms));

    EXPECT_FALSE(result.has_value());
}

TEST(TimerContextTest, ShutdownCompletesPendingTimersStopped) {
    forge::timer_context ctx;
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

TEST(TimerContextTest, PreStoppedReceiverCompletesStopped) {
    forge::timer_context ctx;
    timer_state state;
    std::inplace_stop_source source;
    source.request_stop();
    auto op = std::execution::connect(
        ctx.schedule_after(1h),
        pre_stopped_timer_receiver{{&state}, &source});

    std::execution::start(op);

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state.value);
    EXPECT_TRUE(state.stopped);
}

TEST(TimerContextTest, StopAfterEnqueueCompletesStoppedBeforeDeadline) {
    forge::timer_context ctx;
    timer_state state;
    std::inplace_stop_source source;
    auto op = std::execution::connect(
        ctx.schedule_after(1h),
        stopped_timer_receiver{{&state}, &source});

    std::execution::start(op);
    source.request_stop();

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state.value);
    EXPECT_TRUE(state.stopped);
}

TEST(TimerContextTest, StopAndDeadlineRaceCompletesExactlyOnce) {
    forge::timer_context ctx;

    for (int i = 0; i < 40; ++i) {
        timer_state state;
        std::inplace_stop_source source;
        auto op = std::execution::connect(
            ctx.schedule_after(1ms),
            stopped_timer_receiver{{&state}, &source});

        std::execution::start(op);
        std::thread stopper([&] {
            std::this_thread::sleep_for(1ms);
            source.request_stop();
        });

        ASSERT_TRUE(wait_done(state));
        stopper.join();

        EXPECT_NE(state.value, state.stopped);
    }
}

TEST(TimerContextTest, WaitBlocksUntilDelayedTimerCompletes) {
    forge::timer_context ctx;
    timer_state state;
    auto op = std::execution::connect(
        ctx.schedule_after(20ms),
        timer_receiver{&state});

    std::execution::start(op);
    ctx.wait();

    EXPECT_TRUE(state.value);
    EXPECT_FALSE(state.stopped);
}

TEST(TimerContextTest, WaitObservesActiveCallbackCompletion) {
    forge::timer_context ctx;
    std::mutex mtx;
    std::condition_variable cv;
    bool callback_started = false;
    bool release_callback = false;
    bool callback_finished = false;
    bool wait_returned = false;

    struct blocking_receiver {
        using receiver_concept = std::execution::receiver_t;

        std::mutex* mtx;
        std::condition_variable* cv;
        bool* callback_started;
        bool* release_callback;
        bool* callback_finished;

        void set_value() && noexcept {
            {
                std::lock_guard lk{*mtx};
                *callback_started = true;
            }
            cv->notify_all();

            std::unique_lock lk{*mtx};
            cv->wait(lk, [&] { return *release_callback; });
            *callback_finished = true;
            lk.unlock();
            cv->notify_all();
        }

        void set_error(std::exception_ptr) && noexcept {}
        void set_stopped() && noexcept {}
        auto get_env() const noexcept -> std::execution::empty_env { return {}; }
    };

    auto op = std::execution::connect(
        ctx.schedule_after(0ms),
        blocking_receiver{
            &mtx,
            &cv,
            &callback_started,
            &release_callback,
            &callback_finished});

    std::execution::start(op);

    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return callback_started; }));
    }

    std::thread waiter([&] {
        ctx.wait();
        {
            std::lock_guard lk{mtx};
            wait_returned = true;
        }
        cv.notify_all();
    });

    {
        std::unique_lock lk{mtx};
        EXPECT_FALSE(cv.wait_for(lk, 20ms, [&] { return wait_returned; }));
        release_callback = true;
    }
    cv.notify_all();

    waiter.join();

    EXPECT_TRUE(callback_finished);
    EXPECT_TRUE(wait_returned);
}

TEST(TimerContextTest, DestroyingContextInsideTimerCallbackIsSafe) {
    auto ctx = std::make_unique<forge::timer_context>();
    timer_state state;

    struct deleting_receiver {
        using receiver_concept = std::execution::receiver_t;

        std::unique_ptr<forge::timer_context>* context;
        timer_state* state;

        void set_value() && noexcept {
            context->reset();
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

        auto get_env() const noexcept -> std::execution::empty_env { return {}; }
    };

    auto op = std::execution::connect(
        ctx->schedule_after(0ms),
        deleting_receiver{&ctx, &state});

    std::execution::start(op);

    ASSERT_TRUE(wait_done(state));
    EXPECT_TRUE(state.value);
    EXPECT_FALSE(state.stopped);
    EXPECT_FALSE(ctx);
}

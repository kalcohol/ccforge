#include <gtest/gtest.h>
#include <forge/timer_context.hpp>
#include "forge_counting_resource.hpp"
#include "forge_operation_destroy.hpp"
#include <array>
#include <cstddef>
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
    bool error = false;

    bool done() const noexcept {
        return value || stopped || error;
    }
};

struct timer_receiver {
    using receiver_concept = std::execution::receiver_t;

    timer_state* state;

    void set_value() && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->value = true;
            state->cv.notify_all();
        }
    }

    void set_error(std::exception_ptr) && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->error = true;
            state->cv.notify_all();
        }
    }

    void set_stopped() && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->stopped = true;
            state->cv.notify_all();
        }
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

struct self_destroying_timer_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::inplace_stop_source* source = nullptr;
    forge_test::destroy_context_base* context = nullptr;

    void set_value() && noexcept { context->destroy(); }
    void set_error(std::exception_ptr) && noexcept { context->destroy(); }
    void set_stopped() && noexcept { context->destroy(); }

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

TEST(TimerContextTest, AbandonRacesConcurrentDelivery) {
    forge::timer_context ctx;

    // An immediately-due timer makes the worker delivery race the owning
    // destructor. Every interleaving must settle before the destructor
    // returns: either the item was discarded (no completion) or the
    // in-flight delivery was waited out, so the receiver state read below
    // can never change afterwards. Sanitizer lanes assert memory safety.
    for (int round = 0; round < 512; ++round) {
        timer_state state;
        {
            auto op = std::execution::connect(
                ctx.schedule_after(0ms),
                timer_receiver{&state});
            std::execution::start(op);
            // Sweep the destruction point across the delivery window.
            for (int spin = 0; spin < round % 37; ++spin) {
                std::this_thread::yield();
            }
        }
        const bool settled = [&] {
            std::lock_guard lk{state.mtx};
            return state.value;
        }();
        std::this_thread::yield();
        std::lock_guard lk{state.mtx};
        EXPECT_EQ(state.value, settled);
        EXPECT_FALSE(state.stopped);
        EXPECT_FALSE(state.error);
    }
}

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

TEST(TimerContextTest, AllocationFailureCompletesStopped) {
    forge_test::fail_next_resource resource;
    forge::timer_context ctx{
        forge::timer_context_options{.memory = &resource}};
    timer_state state;
    auto op = std::execution::connect(
        ctx.schedule_after(1h),
        timer_receiver{&state});
    using sender_t = decltype(ctx.schedule_after(1h));
    using signatures_t = std::execution::completion_signatures_of_t<
        sender_t,
        std::execution::empty_env>;
    static_assert(std::same_as<
        signatures_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(),
            std::execution::set_stopped_t()>>);

    resource.fail_next_allocation();
    std::execution::start(op);

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state.value);
    EXPECT_TRUE(state.stopped);
    EXPECT_FALSE(state.error);
    ctx.shutdown();
    ctx.wait();
}

TEST(TimerContextTest, AllocatorBackedCallableUsesProvidedResource) {
    forge_test::counting_resource resource;
    bool called = false;

    struct large_callable {
        bool* called;
        std::array<std::byte, 256> padding{};

        void operator()() noexcept {
            *called = true;
        }
    };

    {
        auto before = resource.allocations();
        auto callable = forge::__timer_detail::__callable::make(
            &resource,
            large_callable{&called});

        EXPECT_GT(resource.allocations(), before);
        callable();
        EXPECT_TRUE(called);
    }

    EXPECT_EQ(resource.allocations(), resource.deallocations());
}

TEST(TimerContextTest, TimerCallbackStorageUsesCustomMemoryResource) {
    forge_test::counting_resource resource;

    {
        forge::timer_context ctx{
            forge::timer_context_options{.memory = &resource}};

        timer_state state;
        auto op = std::execution::connect(
            ctx.schedule_after(1h),
            timer_receiver{&state});

        auto before_start = resource.allocations();
        std::execution::start(op);
        auto after_start = resource.allocations();

        EXPECT_GE(after_start - before_start, 3u);

        ctx.shutdown();
        ASSERT_TRUE(wait_done(state));
        EXPECT_FALSE(state.value);
        EXPECT_TRUE(state.stopped);
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

TEST(TimerContextTest, ScheduleAtSteadyClockDeadlineDoesNotCompleteEarly) {
    forge::timer_context ctx;
    timer_state state;
    auto op = std::execution::connect(
        ctx.schedule_at(std::chrono::steady_clock::now() + 100ms),
        timer_receiver{&state});

    std::execution::start(op);

    EXPECT_FALSE(wait_done_for(state, 10ms));
    ASSERT_TRUE(wait_done(state));
    EXPECT_TRUE(state.value);
    EXPECT_FALSE(state.stopped);
}

TEST(TimerContextTest, ScheduleAtSystemClockDeadlineDoesNotCompleteEarly) {
    forge::timer_context ctx;
    timer_state state;
    auto op = std::execution::connect(
        ctx.schedule_at(std::chrono::system_clock::now() + 100ms),
        timer_receiver{&state});

    std::execution::start(op);

    EXPECT_FALSE(wait_done_for(state, 10ms));
    ASSERT_TRUE(wait_done(state));
    EXPECT_TRUE(state.value);
    EXPECT_FALSE(state.stopped);
}

TEST(TimerContextTest, ScheduleAtPastSystemClockDeadlineCompletesPromptly) {
    forge::timer_context ctx;

    auto result = std::execution::sync_wait(
        ctx.schedule_at(std::chrono::system_clock::now() - 1h));

    EXPECT_TRUE(result.has_value());
}

TEST(TimerContextTest, ScheduleAfterSaturatesHugeDurations) {
    forge::timer_context ctx;
    timer_state state;
    using seconds = std::chrono::duration<long long>;
    auto op = std::execution::connect(
        ctx.schedule_after(seconds::max()),
        timer_receiver{&state});

    std::execution::start(op);

    EXPECT_FALSE(wait_done_for(state, 10ms));
    ctx.shutdown();
    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state.value);
    EXPECT_TRUE(state.stopped);
}

TEST(TimerContextTest, ScheduleAtSaturatesMaximumDeadline) {
    forge::timer_context ctx;
    timer_state state;
    auto op = std::execution::connect(
        ctx.schedule_at(std::chrono::steady_clock::time_point::max()),
        timer_receiver{&state});

    std::execution::start(op);

    EXPECT_FALSE(wait_done_for(state, 10ms));
    ctx.shutdown();
    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state.value);
    EXPECT_TRUE(state.stopped);
}

// A lazily started sender must wait its full delay from start(), not from
// composition: the gap between schedule_after() and start() exceeds the
// delay here, so a composition-anchored deadline would fire immediately.
TEST(TimerContextTest, ScheduleAfterAnchorsDelayAtStart) {
    forge::timer_context ctx;
    timer_state state;
    auto sender = ctx.schedule_after(60ms);
    std::this_thread::sleep_for(90ms);

    const auto started = std::chrono::steady_clock::now();
    auto op = std::execution::connect(
        std::move(sender),
        timer_receiver{&state});
    std::execution::start(op);

    EXPECT_FALSE(wait_done_for(state, 10ms));
    ASSERT_TRUE(wait_done(state));
    EXPECT_GE(std::chrono::steady_clock::now() - started, 55ms);
    EXPECT_TRUE(state.value);
    EXPECT_FALSE(state.stopped);
}

// A coarse-duration deadline converts through common_type inside a naive
// comparison; time_point<steady_clock, seconds>::max() used to overflow the
// tick multiplication and fire immediately instead of never.
TEST(TimerContextTest, ScheduleAtSaturatesCoarseDurationMaximumDeadline) {
    forge::timer_context ctx;
    timer_state state;
    using coarse_point = std::chrono::time_point<
        std::chrono::steady_clock,
        std::chrono::duration<long long>>;
    auto op = std::execution::connect(
        ctx.schedule_at(coarse_point::max()),
        timer_receiver{&state});

    std::execution::start(op);

    EXPECT_FALSE(wait_done_for(state, 10ms));
    ctx.shutdown();
    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state.value);
    EXPECT_TRUE(state.stopped);
}

TEST(TimerContextTest, ScheduleAtCoarsePastDeadlineCompletesPromptly) {
    forge::timer_context ctx;

    const auto past = std::chrono::time_point_cast<
        std::chrono::duration<long long>>(
        std::chrono::system_clock::now() - 1h);
    auto result = std::execution::sync_wait(ctx.schedule_at(past));

    EXPECT_TRUE(result.has_value());
}

// Destroying a started-but-pending operation used to leave the queued item
// holding completion callables that would later fire into a destroyed
// receiver environment; the destructor now claims the completion and
// deregisters the item.
TEST(TimerContextTest, DestroyingPendingOperationDeregistersItem) {
    forge::timer_context ctx;
    timer_state state;
    bool destroyed = false;
    auto factory = [&] {
        return std::execution::connect(
            ctx.schedule_after(10s),
            timer_receiver{&state});
    };
    using op_t = decltype(factory());

    forge_test::operation_destroy_context<op_t> context{&destroyed};
    auto& op = context.emplace_from(factory);
    std::execution::start(op);
    context.reset();

    // The pending count is balanced by the deregistration, so wait()
    // returns instead of hanging on the abandoned 10s timer.
    ctx.wait();
    EXPECT_FALSE(state.done());

    // The context stays fully usable afterwards.
    auto result = std::execution::sync_wait(ctx.schedule_after(1ms));
    EXPECT_TRUE(result.has_value());
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

TEST(TimerContextTest, PreStoppedReceiverMayDestroyOperationInCallback) {
    forge::timer_context ctx;
    std::inplace_stop_source source;
    source.request_stop();

    using sender_t = decltype(ctx.schedule_after(1h));
    using receiver_t = self_destroying_timer_receiver;
    using op_t = decltype(std::execution::connect(
        std::declval<sender_t>(),
        std::declval<receiver_t>()));

    bool destroyed = false;
    forge_test::operation_destroy_context<op_t> context{&destroyed};

    auto& op = context.emplace_from([&] {
        return std::execution::connect(
            ctx.schedule_after(1h),
            self_destroying_timer_receiver{&source, &context});
    });
    std::execution::start(op);

    EXPECT_TRUE(destroyed);
    EXPECT_FALSE(context.has_value);
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

    ASSERT_TRUE(wait_done_for(state, 100ms));
    EXPECT_FALSE(state.value);
    EXPECT_TRUE(state.stopped);
}

TEST(TimerContextTest, RepeatedLongDeadlineStopsWakePromptly) {
    forge::timer_context ctx;

    for (int i = 0; i < 100; ++i) {
        timer_state state;
        std::inplace_stop_source source;
        auto op = std::execution::connect(
            ctx.schedule_after(1h),
            stopped_timer_receiver{{&state}, &source});

        std::execution::start(op);
        source.request_stop();

        ASSERT_TRUE(wait_done_for(state, 100ms));
        EXPECT_FALSE(state.value);
        EXPECT_TRUE(state.stopped);
    }
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
                state->cv.notify_all();
            }
        }

        void set_error(std::exception_ptr) && noexcept {
            {
                std::lock_guard lk{state->mtx};
                state->stopped = true;
                state->cv.notify_all();
            }
        }

        void set_stopped() && noexcept {
            {
                std::lock_guard lk{state->mtx};
                state->stopped = true;
                state->cv.notify_all();
            }
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

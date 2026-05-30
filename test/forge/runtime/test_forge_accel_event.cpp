#include <gtest/gtest.h>
#include <forge/accel.hpp>
#include <execution>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {

using namespace std::chrono_literals;

struct async_state {
    std::mutex mtx;
    std::condition_variable cv;
    bool value = false;
    bool stopped = false;
    std::exception_ptr error;

    [[nodiscard]] bool done() const noexcept {
        return value || stopped || error;
    }
};

struct async_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<async_state> state;

    void set_value() && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->value = true;
        }
        state->cv.notify_all();
    }

    void set_error(std::exception_ptr error) && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->error = std::move(error);
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

auto wait_done(const std::shared_ptr<async_state>& state) -> bool {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, 2s, [&] { return state->done(); });
}

auto wait_done_for(const std::shared_ptr<async_state>& state, std::chrono::milliseconds timeout)
    -> bool {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, timeout, [&] { return state->done(); });
}

} // namespace

TEST(AccelEventTest, EventStartsUnreadyAndCopiesShareState) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    forge::accel::event ev;
    auto copy = ev;

    EXPECT_FALSE(ev.ready());
    EXPECT_FALSE(copy.ready());

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::record_event(q, ev)).has_value());

    EXPECT_TRUE(ev.ready());
    EXPECT_TRUE(copy.ready());
}

TEST(AccelEventTest, WaitEventCompletesAfterRecordEvent) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    forge::accel::event ev;

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::record_event(q, ev)).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::wait_event(q, ev)).has_value());
}

TEST(AccelEventTest, WaitEventStopsWhenContextStops) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    forge::accel::event ev;
    auto state = std::make_shared<async_state>();

    auto sender = forge::accel::wait_event(q, ev);
    auto op = std::execution::connect(std::move(sender), async_receiver{state});
    std::execution::start(op);

    ctx.request_stop();

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state->value);
    EXPECT_TRUE(state->stopped);
    EXPECT_FALSE(state->error);
}

TEST(AccelEventTest, FenceCompletesAfterEarlierAcceptedCommand) {
    forge::accel::context ctx{forge::accel::context_options{
        .thread_count = 1,
        .queue_capacity = std::nullopt,
    }};
    auto q = ctx.get_queue();

    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool release = false;
    auto first_state = std::make_shared<async_state>();
    auto fence_state = std::make_shared<async_state>();

    auto first = forge::accel::submit(q, [&] {
        {
            std::lock_guard lk{mtx};
            started = true;
        }
        cv.notify_all();
        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return release; });
    });
    auto first_op = std::execution::connect(std::move(first), async_receiver{first_state});
    std::execution::start(first_op);

    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return started; }));
    }

    auto fence = forge::accel::fence(q);
    auto fence_op = std::execution::connect(std::move(fence), async_receiver{fence_state});
    std::execution::start(fence_op);

    EXPECT_FALSE(wait_done_for(fence_state, 50ms));

    {
        std::lock_guard lk{mtx};
        release = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done(first_state));
    ASSERT_TRUE(wait_done(fence_state));
    EXPECT_TRUE(first_state->value);
    EXPECT_TRUE(fence_state->value);
}

TEST(AccelEventTest, MovedFromEventRoutesError) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    forge::accel::event ev;
    auto moved = std::move(ev);

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::record_event(q, moved)).has_value());
    EXPECT_THROW(
        (void)std::execution::sync_wait(
            forge::accel::record_event(q, std::move(ev))),
        std::runtime_error);
}

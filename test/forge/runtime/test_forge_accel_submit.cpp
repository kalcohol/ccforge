#include <gtest/gtest.h>
#include <forge/accel.hpp>
#include <execution>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace {

struct stop_env {
    std::inplace_stop_source* source;

    friend auto tag_invoke(
        std::execution::get_stop_token_t,
        const stop_env& self) noexcept -> std::inplace_stop_token {
        return self.source->get_token();
    }
};

struct stopped_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::inplace_stop_source* source;
    bool* value;
    bool* stopped;

    void set_value() && noexcept { *value = true; }
    void set_error(std::exception_ptr) && noexcept { *value = true; }
    void set_stopped() && noexcept { *stopped = true; }

    auto get_env() const noexcept -> stop_env {
        return stop_env{source};
    }
};

struct async_state {
    std::mutex mtx;
    std::condition_variable cv;
    bool value = false;
    bool stopped = false;
    bool error = false;

    [[nodiscard]] bool done() const noexcept {
        return value || stopped || error;
    }
};

struct stoppable_async_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<async_state> state;
    std::inplace_stop_source* source;

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
            state->error = true;
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

    auto get_env() const noexcept -> stop_env {
        return stop_env{source};
    }
};

[[nodiscard]] bool wait_done_for(
    const std::shared_ptr<async_state>& state,
    std::chrono::milliseconds timeout) {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, timeout, [&] { return state->done(); });
}

} // namespace

TEST(AccelSubmitTest, SubmitMutatesDeviceBuffer) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    forge::accel::device_buffer<int> device{ctx, 3};
    std::vector<int> input{2, 4, 6};
    std::vector<int> output(3);

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::copy_to_device(q, device, std::span<const int>{input})).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::submit(q, [&] {
            auto values = device.span();
            values[0] += 10;
            values[1] += 20;
            values[2] += 30;
        })).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::copy_to_host(q, std::span<int>{output}, device)).has_value());

    EXPECT_EQ(output, (std::vector<int>{12, 24, 36}));
}

TEST(AccelSubmitTest, SubmitExceptionsRouteError) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();

    EXPECT_THROW(
        (void)std::execution::sync_wait(
            forge::accel::submit(q, [] {
                throw std::runtime_error{"kernel failed"};
            })),
        std::runtime_error);
}

TEST(AccelSubmitTest, PreStoppedReceiverCompletesStoppedWithoutRunningCommand) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    std::inplace_stop_source source;
    source.request_stop();

    bool ran = false;
    bool value = false;
    bool stopped = false;
    auto sender = forge::accel::submit(q, [&] {
        ran = true;
    });
    auto op = std::execution::connect(
        std::move(sender),
        stopped_receiver{&source, &value, &stopped});

    std::execution::start(op);
    ctx.wait();

    EXPECT_FALSE(ran);
    EXPECT_FALSE(value);
    EXPECT_TRUE(stopped);
}

TEST(AccelSubmitTest, PostStartReceiverStopDoesNotCancelAcceptedCommandV1) {
    using namespace std::chrono_literals;

    forge::accel::context ctx{forge::accel::context_options{
        .thread_count = 1,
        .queue_capacity = 2,
    }};
    auto q = ctx.get_queue();

    std::mutex mtx;
    std::condition_variable cv;
    bool first_started = false;
    bool release_first = false;

    auto first_state = std::make_shared<async_state>();
    std::inplace_stop_source first_source;
    auto first = forge::accel::submit(q, [&] {
        {
            std::lock_guard lk{mtx};
            first_started = true;
        }
        cv.notify_all();

        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return release_first; });
    });
    auto first_op = std::execution::connect(
        std::move(first),
        stoppable_async_receiver{first_state, &first_source});
    std::execution::start(first_op);

    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return first_started; }));
    }

    auto second_state = std::make_shared<async_state>();
    std::inplace_stop_source second_source;
    bool second_ran = false;
    auto second = forge::accel::submit(q, [&] {
        second_ran = true;
    });
    auto second_op = std::execution::connect(
        std::move(second),
        stoppable_async_receiver{second_state, &second_source});
    std::execution::start(second_op);

    second_source.request_stop();
    EXPECT_FALSE(wait_done_for(second_state, 50ms));

    {
        std::lock_guard lk{mtx};
        release_first = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done_for(first_state, 2s));
    ASSERT_TRUE(wait_done_for(second_state, 2s));
    EXPECT_TRUE(second_ran);
    EXPECT_TRUE(second_state->value);
    EXPECT_FALSE(second_state->stopped);
    EXPECT_FALSE(second_state->error);
}

TEST(AccelSubmitTest, CommandsComposeWithThen) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    int value = 0;

    auto result = std::execution::sync_wait(
        forge::accel::submit(q, [&] {
            value = 41;
        }) | std::execution::then([&] {
            return value + 1;
        }));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

#include <gtest/gtest.h>
#include <forge/accel.hpp>
#include "forge_counting_resource.hpp"
#include <execution>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
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

} // namespace

TEST(AccelContextTest, EmptyContextDestroysCleanly) {
    forge::accel::mock::context ctx;
    ctx.shutdown();
    ctx.wait();
}

TEST(AccelContextTest, OptionsConstructorUsesCustomMemoryResource) {
    forge_test::counting_resource resource;

    {
        forge::accel::mock::context ctx{forge::accel::mock::context_options{
            .thread_count = 1,
            .queue_capacity = std::nullopt,
            .memory = &resource,
        }};
        forge::accel::mock::device_buffer<int> buffer{ctx, 32};
        EXPECT_GT(resource.allocations(), 0u);
    }

    EXPECT_EQ(resource.allocations(), resource.deallocations());
}

TEST(AccelContextTest, QueueRunsCommandsInFifoOrder) {
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue();
    std::vector<int> order;

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::submit(q, [&] { order.push_back(1); })).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::submit(q, [&] { order.push_back(2); })).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::submit(q, [&] { order.push_back(3); })).has_value());

    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST(AccelContextTest, CloseRejectsNewCommands) {
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue();

    ctx.close();
    auto result = std::execution::sync_wait(forge::accel::mock::submit(q, [] {}));

    EXPECT_FALSE(result.has_value());
}

TEST(AccelContextTest, QueueAfterContextDestructionCompletesStopped) {
    forge::accel::mock::queue q;
    {
        forge::accel::mock::context ctx;
        q = ctx.get_queue();
    }

    EXPECT_TRUE(q.closed());
    auto result = std::execution::sync_wait(forge::accel::mock::submit(q, [] {}));

    EXPECT_FALSE(result.has_value());
}

TEST(AccelContextTest, RequestStopStopsQueuedCommands) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 1,
        .queue_capacity = 2,
    }};
    auto q = ctx.get_queue();

    std::mutex mtx;
    std::condition_variable cv;
    bool first_started = false;
    bool release_first = false;
    auto first_state = std::make_shared<async_state>();
    auto second_state = std::make_shared<async_state>();

    auto first = forge::accel::mock::submit(q, [&] {
        {
            std::lock_guard lk{mtx};
            first_started = true;
        }
        cv.notify_all();
        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return release_first; });
    });
    auto first_op = std::execution::connect(std::move(first), async_receiver{first_state});
    std::execution::start(first_op);

    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return first_started; }));
    }

    auto second = forge::accel::mock::submit(q, [] {});
    auto second_op = std::execution::connect(std::move(second), async_receiver{second_state});
    std::execution::start(second_op);

    ctx.request_stop();

    ASSERT_TRUE(wait_done(second_state));
    EXPECT_FALSE(second_state->value);
    EXPECT_TRUE(second_state->stopped);
    EXPECT_FALSE(second_state->error);

    {
        std::lock_guard lk{mtx};
        release_first = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done(first_state));
    EXPECT_TRUE(first_state->value);
}

TEST(AccelContextTest, QueueCapacityFullCompletesStopped) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 1,
        .queue_capacity = 1,
    }};
    auto q = ctx.get_queue();

    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool release = false;
    auto state = std::make_shared<async_state>();

    auto first = forge::accel::mock::submit(q, [&] {
        {
            std::lock_guard lk{mtx};
            started = true;
        }
        cv.notify_all();
        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return release; });
    });
    auto op = std::execution::connect(std::move(first), async_receiver{state});
    std::execution::start(op);

    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return started; }));
    }

    auto result = std::execution::sync_wait(forge::accel::mock::submit(q, [] {}));
    EXPECT_FALSE(result.has_value());

    {
        std::lock_guard lk{mtx};
        release = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done(state));
    EXPECT_TRUE(state->value);
}

TEST(AccelContextTest, RepeatedShutdownAndWaitAreHarmless) {
    forge::accel::mock::context ctx;
    ctx.shutdown();
    ctx.shutdown();
    ctx.wait();
    ctx.wait();
}

TEST(AccelContextTest, CurrentDeviceGuardRestoresAndIsThreadLocal) {
    EXPECT_FALSE(forge::accel::current_device().has_value());

    {
        forge::accel::current_device_guard guard{forge::accel::device_id{3}};
        ASSERT_TRUE(forge::accel::current_device().has_value());
        EXPECT_EQ(forge::accel::current_device()->value, 3u);

        {
            forge::accel::current_device_guard nested{forge::accel::device_id{5}};
            ASSERT_TRUE(forge::accel::current_device().has_value());
            EXPECT_EQ(forge::accel::current_device()->value, 5u);
        }

        ASSERT_TRUE(forge::accel::current_device().has_value());
        EXPECT_EQ(forge::accel::current_device()->value, 3u);

        bool thread_has_current_device = true;
        std::thread worker{[&] {
            thread_has_current_device = forge::accel::current_device().has_value();
        }};
        worker.join();
        EXPECT_FALSE(thread_has_current_device);
    }

    EXPECT_FALSE(forge::accel::current_device().has_value());
}

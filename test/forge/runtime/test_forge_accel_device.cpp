#include <gtest/gtest.h>
#include <forge/accel.hpp>
#include "forge_counting_resource.hpp"
#include <execution>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

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

[[nodiscard]] auto wait_done(const std::shared_ptr<async_state>& state) -> bool {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, 2s, [&] { return state->done(); });
}

struct request_packet {
    int value = 0;
};

struct response_packet {
    int value = 0;
};

} // namespace

TEST(AccelDeviceTest, DeviceDiscoveryReportsMockDevices) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 1,
        .queue_capacity = std::nullopt,
        .device_count = 2,
    }};

    auto devices = ctx.devices();
    auto infos = ctx.device_infos();

    ASSERT_EQ(devices.size(), 2u);
    ASSERT_EQ(infos.size(), 2u);
    EXPECT_TRUE(devices[0].available());
    EXPECT_TRUE(devices[1].available());
    EXPECT_EQ(infos[0].id.value, 0u);
    EXPECT_EQ(infos[1].id.value, 1u);
    EXPECT_EQ(ctx.get_device(forge::accel::device_id{1}).info().ordinal, 1u);
    EXPECT_FALSE(ctx.get_device(forge::accel::device_id{7}).available());
}

TEST(AccelDeviceTest, NoDeviceContextRejectsDeviceWork) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 1,
        .queue_capacity = std::nullopt,
        .device_count = 0,
    }};

    EXPECT_TRUE(ctx.devices().empty());
    auto device = ctx.get_device();
    EXPECT_FALSE(device.available());

    auto session = device.open_session();
    auto result = std::execution::sync_wait(
        forge::accel::mock::submit(session, [] {}));
    EXPECT_FALSE(result.has_value());
}

TEST(AccelDeviceTest, DeviceOpensSessionAndRunsCommand) {
    forge::accel::mock::context ctx;
    auto device = ctx.get_device();
    auto session = device.open_session();

    int observed = 0;
    auto result = std::execution::sync_wait(
        forge::accel::mock::submit(session, [&] {
            observed = 42;
        }));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(observed, 42);
    EXPECT_TRUE(device.available());
    EXPECT_FALSE(session.reset_requested());
}

TEST(AccelDeviceTest, DeviceLostWhileCommandPendingRoutesError) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 1,
        .queue_capacity = 2,
    }};
    auto device = ctx.get_device();
    auto q = device.get_queue();

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
    auto second = forge::accel::mock::submit(q, [] {});
    auto first_op = std::execution::connect(std::move(first), async_receiver{first_state});
    auto second_op = std::execution::connect(std::move(second), async_receiver{second_state});

    std::execution::start(first_op);
    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return first_started; }));
    }
    std::execution::start(second_op);
    device.mark_lost();

    {
        std::lock_guard lk{mtx};
        release_first = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done(first_state));
    ASSERT_TRUE(wait_done(second_state));
    EXPECT_TRUE(first_state->value);
    EXPECT_FALSE(second_state->value);
    EXPECT_FALSE(second_state->stopped);
    ASSERT_TRUE(second_state->error);
    try {
        std::rethrow_exception(second_state->error);
        FAIL() << "expected operation_error";
    } catch (const forge::accel::operation_error& error) {
        EXPECT_EQ(error.kind(), forge::accel::error_kind::invalid_context);
    }

    EXPECT_FALSE(device.available());
    device.reset();
    EXPECT_TRUE(device.available());
}

TEST(AccelDeviceTest, MessageCommandProducesResponse) {
    forge::accel::mock::context ctx;
    auto session = ctx.get_device().open_session();

    response_packet response{};
    auto result = std::execution::sync_wait(
        forge::accel::mock::submit_message(
            session,
            request_packet{21},
            response,
            [](request_packet& request, response_packet& out) noexcept {
                out.value = request.value * 2;
                return forge::accel::command_status::ok;
            }));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(response.value, 42);
}

TEST(AccelDeviceTest, MessageFailureRoutesCommandError) {
    forge::accel::mock::context ctx;
    auto session = ctx.get_device().open_session();

    response_packet response{};

    EXPECT_THROW(
        (void)std::execution::sync_wait(
            forge::accel::mock::submit_message(
                session,
                request_packet{1},
                response,
                [](request_packet&, response_packet&) noexcept {
                    return forge::accel::command_status::failed;
                })),
        forge::accel::command_error);
}

TEST(AccelDeviceTest, ResetStopsNewSessionCommands) {
    forge::accel::mock::context ctx;
    auto session = ctx.get_device().open_session();

    session.reset();
    auto result = std::execution::sync_wait(
        forge::accel::mock::submit(session, [] {}));

    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(session.reset_requested());
}

TEST(AccelDeviceTest, ResetStopsQueuedCommandBeforeExecution) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 1,
        .queue_capacity = 2,
    }};
    auto session = ctx.get_device().open_session();

    std::mutex mtx;
    std::condition_variable cv;
    bool first_started = false;
    bool release_first = false;
    auto first_state = std::make_shared<async_state>();
    auto second_state = std::make_shared<async_state>();

    auto first = forge::accel::mock::submit(session, [&] {
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

    auto second = forge::accel::mock::submit(session, [] {});
    auto second_op = std::execution::connect(std::move(second), async_receiver{second_state});
    std::execution::start(second_op);

    session.reset();

    {
        std::lock_guard lk{mtx};
        release_first = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done(first_state));
    ASSERT_TRUE(wait_done(second_state));
    EXPECT_TRUE(first_state->value);
    EXPECT_FALSE(second_state->value);
    EXPECT_TRUE(second_state->stopped);
    EXPECT_FALSE(second_state->error);
}

TEST(AccelDeviceTest, SessionStateUsesContextResource) {
    forge_test::counting_resource resource;

    {
        forge::accel::mock::context ctx{forge::accel::mock::context_options{
            .thread_count = 1,
            .queue_capacity = std::nullopt,
            .memory = &resource,
        }};
        auto session = ctx.get_device().open_session();
        ASSERT_TRUE(std::execution::sync_wait(
            forge::accel::mock::submit(session, [] {})).has_value());
    }

    EXPECT_GT(resource.allocations(), 0u);
    EXPECT_EQ(resource.allocations(), resource.deallocations());
}

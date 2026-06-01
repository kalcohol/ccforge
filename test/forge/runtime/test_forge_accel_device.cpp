#include <gtest/gtest.h>
#include <forge/accel.hpp>
#include "forge_counting_resource.hpp"
#include <execution>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
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

template<class Packet>
struct packet_state {
    std::mutex mtx;
    std::condition_variable cv;
    std::optional<Packet> packet;
    bool stopped = false;
    std::exception_ptr error;

    [[nodiscard]] bool done() const noexcept {
        return packet.has_value() || stopped || error;
    }
};

template<class Packet>
struct packet_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<packet_state<Packet>> state;

    void set_value(Packet packet) && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->packet = std::move(packet);
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

template<class Packet>
struct shutdown_packet_receiver {
    using receiver_concept = std::execution::receiver_t;

    forge::accel::mock::context* ctx = nullptr;
    std::shared_ptr<packet_state<Packet>> state;

    void set_value(Packet packet) && noexcept {
        ctx->shutdown();
        ctx->wait();
        {
            std::lock_guard lk{state->mtx};
            state->packet = std::move(packet);
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

template<class Packet>
[[nodiscard]] auto wait_done(const std::shared_ptr<packet_state<Packet>>& state)
    -> bool {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, 2s, [&] { return state->done(); });
}

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

TEST(AccelDeviceTest, OwningPacketProducesResponse) {
    forge::accel::mock::context ctx;
    auto session = ctx.get_device().open_session();
    using packet_t = forge::accel::mock::command_packet<
        request_packet,
        response_packet>;

    auto result = std::execution::sync_wait(
        forge::accel::mock::submit_packet(
            session,
            packet_t{
                forge::accel::command_id{7},
                request_packet{11},
                response_packet{}},
            [](request_packet& request, response_packet& out) noexcept {
                out.value = request.value * 3;
                return forge::accel::command_status::ok;
            }));

    ASSERT_TRUE(result.has_value());
    auto& packet = std::get<0>(*result);
    EXPECT_EQ(packet.id.value, 7u);
    EXPECT_EQ(packet.request.value, 11);
    EXPECT_EQ(packet.response.value, 33);
    EXPECT_EQ(packet.status, forge::accel::command_status::ok);
}

TEST(AccelDeviceTest, OwningPacketFailureRoutesCommandError) {
    forge::accel::mock::context ctx;
    auto session = ctx.get_device().open_session();
    using packet_t = forge::accel::mock::command_packet<
        request_packet,
        response_packet>;

    EXPECT_THROW(
        (void)std::execution::sync_wait(
            forge::accel::mock::submit_packet(
                session,
                packet_t{
                    forge::accel::command_id{8},
                    request_packet{1},
                    response_packet{}},
                [](request_packet&, response_packet&) noexcept {
                    return forge::accel::command_status::failed;
                })),
        forge::accel::command_error);
}

TEST(AccelDeviceTest, OwningPacketTimeoutWhilePendingReportsTimeout) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 1,
        .queue_capacity = 2,
    }};
    auto session = ctx.get_device().open_session();
    using packet_t = forge::accel::mock::command_packet<
        request_packet,
        response_packet>;

    std::mutex mtx;
    std::condition_variable cv;
    bool first_started = false;
    bool release_first = false;
    auto first_state = std::make_shared<async_state>();

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

    auto packet_result = std::make_shared<packet_state<packet_t>>();
    auto packet_sender = forge::accel::mock::submit_packet(
        session,
        packet_t{
            forge::accel::command_id{9},
            request_packet{1},
            response_packet{}},
        [](request_packet&, response_packet& out) noexcept {
            out.value = 99;
            return forge::accel::command_status::ok;
        },
        forge::accel::mock::command_options{.timeout = 10ms});
    auto packet_op = std::execution::connect(
        std::move(packet_sender),
        packet_receiver<packet_t>{packet_result});
    std::execution::start(packet_op);

    std::this_thread::sleep_for(30ms);
    {
        std::lock_guard lk{mtx};
        release_first = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done(first_state));
    ASSERT_TRUE(wait_done(packet_result));
    EXPECT_TRUE(first_state->value);
    EXPECT_FALSE(packet_result->packet.has_value());
    ASSERT_TRUE(packet_result->error);
    try {
        std::rethrow_exception(packet_result->error);
        FAIL() << "expected timeout";
    } catch (const forge::accel::operation_error& error) {
        EXPECT_EQ(error.kind(), forge::accel::error_kind::timeout);
        EXPECT_EQ(error.status(), forge::accel::command_status::timed_out);
    }
}

TEST(AccelDeviceTest, OwningPacketResetWhilePendingCompletesStopped) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 1,
        .queue_capacity = 2,
    }};
    auto session = ctx.get_device().open_session();
    using packet_t = forge::accel::mock::command_packet<
        request_packet,
        response_packet>;

    std::mutex mtx;
    std::condition_variable cv;
    bool first_started = false;
    bool release_first = false;
    auto first_state = std::make_shared<async_state>();

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

    auto packet_result = std::make_shared<packet_state<packet_t>>();
    auto packet_sender = forge::accel::mock::submit_packet(
        session,
        packet_t{
            forge::accel::command_id{10},
            request_packet{1},
            response_packet{}},
        [](request_packet&, response_packet& out) noexcept {
            out.value = 99;
            return forge::accel::command_status::ok;
        });
    auto packet_op = std::execution::connect(
        std::move(packet_sender),
        packet_receiver<packet_t>{packet_result});
    std::execution::start(packet_op);

    session.reset();
    {
        std::lock_guard lk{mtx};
        release_first = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done(first_state));
    ASSERT_TRUE(wait_done(packet_result));
    EXPECT_TRUE(first_state->value);
    EXPECT_FALSE(packet_result->packet.has_value());
    EXPECT_TRUE(packet_result->stopped);
    EXPECT_FALSE(packet_result->error);
}

TEST(AccelDeviceTest, OwningPacketOnLostDeviceRoutesInvalidContext) {
    forge::accel::mock::context ctx;
    auto device = ctx.get_device();
    device.mark_lost();
    auto session = device.open_session();
    using packet_t = forge::accel::mock::command_packet<
        request_packet,
        response_packet>;

    try {
        (void)std::execution::sync_wait(
            forge::accel::mock::submit_packet(
                session,
                packet_t{
                    forge::accel::command_id{12},
                    request_packet{1},
                    response_packet{}},
                [](request_packet&, response_packet& out) noexcept {
                    out.value = 99;
                    return forge::accel::command_status::ok;
                }));
        FAIL() << "expected invalid_context";
    } catch (const forge::accel::operation_error& error) {
        EXPECT_EQ(error.kind(), forge::accel::error_kind::invalid_context);
    }
}

TEST(AccelDeviceTest, OwningPacketCompletionCanShutdownContext) {
    forge::accel::mock::context ctx;
    auto session = ctx.get_device().open_session();
    using packet_t = forge::accel::mock::command_packet<
        request_packet,
        response_packet>;
    auto state = std::make_shared<packet_state<packet_t>>();

    auto sender = forge::accel::mock::submit_packet(
        session,
        packet_t{
            forge::accel::command_id{11},
            request_packet{2},
            response_packet{}},
        [](request_packet& request, response_packet& out) noexcept {
            out.value = request.value + 5;
            return forge::accel::command_status::ok;
        });
    auto op = std::execution::connect(
        std::move(sender),
        shutdown_packet_receiver<packet_t>{&ctx, state});
    std::execution::start(op);

    ASSERT_TRUE(wait_done(state));
    ASSERT_TRUE(state->packet.has_value());
    EXPECT_EQ(state->packet->response.value, 7);
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

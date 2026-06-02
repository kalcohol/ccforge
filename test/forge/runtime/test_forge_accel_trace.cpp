#include <gtest/gtest.h>
#include <forge/accel.hpp>
#include <execution>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
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

[[nodiscard]] auto wait_done(const std::shared_ptr<async_state>& state) -> bool {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, 2s, [&] { return state->done(); });
}

template<class Packet>
[[nodiscard]] auto wait_done(const std::shared_ptr<packet_state<Packet>>& state)
    -> bool {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, 2s, [&] { return state->done(); });
}

[[nodiscard]] auto has_kind(
    const std::vector<forge::accel::mock::trace_event>& events,
    forge::accel::mock::trace_event_kind kind) -> bool {
    for (const auto& event : events) {
        if (event.kind == kind) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] auto count_kind(
    const std::vector<forge::accel::mock::trace_event>& events,
    forge::accel::mock::trace_event_kind kind) -> std::size_t {
    std::size_t count = 0;
    for (const auto& event : events) {
        if (event.kind == kind) {
            ++count;
        }
    }
    return count;
}

struct request_packet {
    int value = 0;
};

struct response_packet {
    int value = 0;
};

} // namespace

TEST(AccelTraceTest, CommandEmitsSubmittedStartedCompletedInOrder) {
    forge::accel::mock::trace_sink trace;
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 1,
        .queue_capacity = std::nullopt,
        .device_count = 1,
        .memory = forge::default_memory_resource(),
        .trace = &trace,
    }};
    auto q = ctx.get_queue(forge::accel::queue_kind::compute);

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::submit(q, [] {})).has_value());

    auto events = trace.snapshot();
    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events[0].kind, forge::accel::mock::trace_event_kind::submitted);
    EXPECT_EQ(events[1].kind, forge::accel::mock::trace_event_kind::started);
    EXPECT_EQ(events[2].kind, forge::accel::mock::trace_event_kind::completed);
    EXPECT_LT(events[0].sequence, events[1].sequence);
    EXPECT_LT(events[1].sequence, events[2].sequence);
    EXPECT_NE(events[0].context.value, 0U);
    EXPECT_NE(events[0].stream.value, 0U);
}

TEST(AccelTraceTest, PacketTimeoutEmitsTimeoutOnce) {
    using packet_t = forge::accel::mock::command_packet<
        request_packet,
        response_packet>;

    forge::accel::mock::trace_sink trace;
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 1,
        .queue_capacity = 2,
        .device_count = 1,
        .memory = forge::default_memory_resource(),
        .trace = &trace,
    }};
    auto session = ctx.get_device().open_session();
    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool release = false;
    auto blocker_state = std::make_shared<async_state>();

    auto blocker = forge::accel::mock::submit(session, [&] {
        {
            std::lock_guard lk{mtx};
            started = true;
        }
        cv.notify_all();
        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return release; });
    });
    auto blocker_op = std::execution::connect(
        std::move(blocker),
        async_receiver{blocker_state});
    std::execution::start(blocker_op);
    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return started; }));
    }

    auto packet_result = std::make_shared<packet_state<packet_t>>();
    auto packet_sender = forge::accel::mock::submit_packet(
        session,
        packet_t{
            forge::accel::command_id{99},
            request_packet{1},
            response_packet{}},
        [](request_packet&, response_packet& out) noexcept {
            out.value = 7;
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
        release = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done(blocker_state));
    ASSERT_TRUE(wait_done(packet_result));
    ASSERT_TRUE(packet_result->error);

    auto events = trace.snapshot();
    EXPECT_EQ(count_kind(events, forge::accel::mock::trace_event_kind::timeout), 1U);
    bool saw_packet_timeout = false;
    for (const auto& event : events) {
        if (event.kind == forge::accel::mock::trace_event_kind::timeout) {
            saw_packet_timeout = event.command == forge::accel::command_id{99} &&
                event.error == forge::accel::error_kind::timeout &&
                event.status == forge::accel::command_status::timed_out;
        }
    }
    EXPECT_TRUE(saw_packet_timeout);
}

TEST(AccelTraceTest, DeviceLostAndStaleSessionAreObservable) {
    forge::accel::mock::trace_sink trace;
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 1,
        .queue_capacity = std::nullopt,
        .device_count = 1,
        .memory = forge::default_memory_resource(),
        .trace = &trace,
    }};
    auto device = ctx.get_device();
    auto session = device.open_session();

    device.mark_lost();
    EXPECT_THROW(
        (void)std::execution::sync_wait(
            forge::accel::mock::submit(session, [] {})),
        forge::accel::operation_error);

    device.reset();
    EXPECT_THROW(
        (void)std::execution::sync_wait(
            forge::accel::mock::submit(session, [] {})),
        forge::accel::operation_error);

    auto events = trace.snapshot();
    EXPECT_TRUE(has_kind(events, forge::accel::mock::trace_event_kind::device_lost));
    EXPECT_TRUE(has_kind(events, forge::accel::mock::trace_event_kind::session_stale));
}

TEST(AccelTraceTest, DisabledTracingDoesNotChangeCommandBehavior) {
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue();
    int value = 0;

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::submit(q, [&] {
            value = 42;
        })).has_value());

    EXPECT_EQ(value, 42);
}

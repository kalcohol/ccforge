#include <gtest/gtest.h>
#include <forge/accel.hpp>
#include <forge/erased_sender.hpp>
#include <forge/wait_result.hpp>

#include <chrono>
#include <condition_variable>
#include <execution>
#include <mutex>
#include <tuple>

namespace {

struct blocking_gate {
    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool release = false;

    void wait_started() {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, std::chrono::seconds{2}, [&] {
            return started;
        }));
    }

    void mark_started_and_wait() {
        {
            std::lock_guard lk{mtx};
            started = true;
        }
        cv.notify_all();

        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return release; });
    }

    void release_gate() {
        {
            std::lock_guard lk{mtx};
            release = true;
        }
        cv.notify_all();
    }
};

} // namespace

TEST(AccelRequestRuntimeTest, SubmitRequestReturnsCorrelatedResponse) {
    forge::accel::mock::context ctx;
    auto dev = ctx.get_device();
    forge::accel::mock::request_session requests{dev.open_session()};

    auto result = std::execution::sync_wait(
        requests.submit_request(
            21,
            0,
            [](int& request, int& response) noexcept {
                response = request * 2;
            }));

    ASSERT_TRUE(result.has_value());
    auto packet = std::get<0>(std::move(*result));
    EXPECT_EQ(packet.id.value, 1U);
    EXPECT_EQ(packet.request, 21);
    EXPECT_EQ(packet.response, 42);
    EXPECT_EQ(packet.status, forge::accel::command_status::ok);
    EXPECT_EQ(requests.pending_count(), 0U);
    EXPECT_EQ(requests.late_response_count(), 0U);
}

TEST(AccelRequestRuntimeTest, RequestIdsAreMonotonic) {
    forge::accel::mock::context ctx;
    auto dev = ctx.get_device();
    forge::accel::mock::request_session requests{dev.open_session()};

    auto first = std::execution::sync_wait(
        requests.submit_request(
            1,
            0,
            [](int& request, int& response) noexcept {
                response = request;
            }));
    auto second = std::execution::sync_wait(
        requests.submit_request(
            2,
            0,
            [](int& request, int& response) noexcept {
                response = request;
            }));

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(std::get<0>(*first).id.value, 1U);
    EXPECT_EQ(std::get<0>(*second).id.value, 2U);
}

TEST(AccelRequestRuntimeTest, TimeoutCompletesBeforeLateResponse) {
    using namespace std::chrono_literals;

    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 1,
    }};
    auto dev = ctx.get_device();
    auto session = dev.open_session();
    forge::accel::mock::request_session requests{session};

    blocking_gate gate;
    std::execution::start_detached(
        forge::accel::mock::submit(session, [&] {
            gate.mark_started_and_wait();
        }));
    gate.wait_started();

    auto result = forge::wait_result(
        requests.submit_request(
            7,
            0,
            [](int& request, int& response) noexcept {
                response = request + 1;
            },
            forge::accel::mock::request_options{.timeout = 25ms}));

    ASSERT_TRUE(result.has_error());
    auto* error = result.error_if<std::exception_ptr>();
    ASSERT_NE(error, nullptr);

    gate.release_gate();
    ctx.wait();

    EXPECT_EQ(requests.pending_count(), 0U);
    EXPECT_EQ(requests.late_response_count(), 1U);
}

TEST(AccelRequestRuntimeTest, TypedTimeoutCrossesErasedSenderBoundary) {
    using namespace std::chrono_literals;
    using packet_t = forge::accel::mock::request_packet<int, int>;
    using completions = std::execution::completion_signatures<
        std::execution::set_value_t(packet_t),
        std::execution::set_error_t(forge::accel::error),
        std::execution::set_stopped_t()>;

    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 1,
    }};
    auto dev = ctx.get_device();
    auto session = dev.open_session();
    forge::accel::mock::request_session requests{session};

    blocking_gate gate;
    std::execution::start_detached(
        forge::accel::mock::submit(session, [&] {
            gate.mark_started_and_wait();
        }));
    gate.wait_started();

    forge::erased_sender<completions> sender{
        requests.submit_request_typed(
            3,
            0,
            [](int& request, int& response) noexcept {
                response = request;
            },
            forge::accel::mock::request_options{.timeout = 25ms})};

    auto result = forge::wait_result(std::move(sender));
    ASSERT_TRUE(result.has_error());
    auto* error = result.error_if<forge::accel::error>();
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->kind, forge::accel::error_kind::timeout);

    gate.release_gate();
    ctx.wait();
    EXPECT_EQ(requests.late_response_count(), 1U);
}

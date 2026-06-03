#include <gtest/gtest.h>
#include <forge/accel.hpp>
#include <forge/wait_result.hpp>
#include "forge_counting_resource.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <execution>
#include <exception>
#include <memory>
#include <mutex>
#include <span>
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

template<class Op>
struct connected_operation {
    Op op;
    std::shared_ptr<async_state> state;
};

template<class Sender>
auto connect_async(Sender&& sender) {
    auto state = std::make_shared<async_state>();
    return connected_operation<
        decltype(std::execution::connect(
            std::forward<Sender>(sender),
            async_receiver{state}))>{
        std::execution::connect(
            std::forward<Sender>(sender),
            async_receiver{state}),
        std::move(state)};
}

auto wait_done(const std::shared_ptr<async_state>& state, std::chrono::milliseconds timeout = 2s) -> bool {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, timeout, [&] { return state->done(); });
}

struct blocking_gate {
    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool release = false;

    void mark_started_and_wait() {
        {
            std::lock_guard lk{mtx};
            started = true;
        }
        cv.notify_all();

        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return release; });
    }

    bool wait_started() {
        std::unique_lock lk{mtx};
        return cv.wait_for(lk, 2s, [&] { return started; });
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

TEST(AccelCpuTest, CopySubmitAndCopyBack) {
    forge::accel::cpu::context ctx;
    auto q = ctx.get_queue(forge::accel::queue_kind::compute);
    forge::accel::cpu::device_buffer<int> device{ctx, 4};
    std::vector<int> input{1, 2, 3, 4};
    std::vector<int> output(4);

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::cpu::copy_to_device(q, device, std::span<const int>{input})).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::cpu::submit(q, [&] {
            for (auto& value : device.span()) {
                value *= 3;
            }
        })).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::cpu::copy_to_host(q, std::span<int>{output}, device)).has_value());

    EXPECT_EQ(output, (std::vector<int>{3, 6, 9, 12}));
}

TEST(AccelCpuTest, DeviceBufferUsesAlignedResourceAllocation) {
    forge_test::counting_resource resource;

    {
        forge::accel::cpu::context ctx{forge::accel::cpu::context_options{
            .thread_count = 1,
            .queue_capacity = std::nullopt,
            .memory = &resource,
        }};
        forge::accel::cpu::device_buffer<int> device{ctx, 16};
        const auto address = reinterpret_cast<std::uintptr_t>(device.span().data());
        EXPECT_EQ(address % forge::accel::cpu::device_buffer<int>::alignment, 0U);
        EXPECT_GT(resource.allocations(), 0U);
    }

    EXPECT_EQ(resource.allocations(), resource.deallocations());
}

TEST(AccelCpuTest, CapacityFullCompletesStopped) {
    forge::accel::cpu::context ctx{forge::accel::cpu::context_options{
        .thread_count = 1,
        .queue_capacity = 1,
    }};
    auto q = ctx.get_queue();
    blocking_gate gate;

    auto [first_op, first_state] = connect_async(
        forge::accel::cpu::submit(q, [&] {
            gate.mark_started_and_wait();
        }));
    std::execution::start(first_op);
    ASSERT_TRUE(gate.wait_started());

    auto rejected = std::execution::sync_wait(forge::accel::cpu::submit(q, [] {}));
    EXPECT_FALSE(rejected.has_value());

    gate.release_gate();
    ASSERT_TRUE(wait_done(first_state));
    EXPECT_TRUE(first_state->value);
}

TEST(AccelCpuTest, TypedSizeMismatchReportsPortableError) {
    forge::accel::cpu::context ctx;
    auto q = ctx.get_queue();
    forge::accel::cpu::device_buffer<int> device{ctx, 2};
    std::vector<int> input{1, 2, 3};

    auto result = forge::wait_result(
        forge::accel::cpu::copy_to_device_typed(
            q,
            device,
            std::span<const int>{input}));

    ASSERT_TRUE(result.has_error());
    auto* error = result.error_if<forge::accel::error>();
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->kind, forge::accel::error_kind::size_mismatch);
}

TEST(AccelCpuTest, StopWakesBlockedEventWait) {
    forge::accel::cpu::context ctx{forge::accel::cpu::context_options{
        .thread_count = 1,
        .queue_capacity = std::nullopt,
    }};
    auto q = ctx.get_queue();
    forge::accel::cpu::event ev;

    auto [wait_op, wait_state] = connect_async(
        forge::accel::cpu::wait_event(q, ev));
    std::execution::start(wait_op);
    EXPECT_FALSE(wait_done(wait_state, 50ms));

    ctx.request_stop();

    ASSERT_TRUE(wait_done(wait_state));
    EXPECT_TRUE(wait_state->stopped);
}

TEST(AccelCpuTest, ContextWaitCanBeCalledFromBackendWork) {
    forge::accel::cpu::context ctx{forge::accel::cpu::context_options{
        .thread_count = 1,
        .queue_capacity = std::nullopt,
    }};
    auto q = ctx.get_queue();
    bool reached = false;

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::cpu::submit(q, [&] {
            ctx.wait();
            reached = true;
        })).has_value());

    EXPECT_TRUE(reached);
}

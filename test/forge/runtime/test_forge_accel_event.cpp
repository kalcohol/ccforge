#include <gtest/gtest.h>
#include <forge/accel.hpp>
#include <forge/wait_result.hpp>
#include <execution>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
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
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue();
    forge::accel::mock::event ev;
    auto copy = ev;

    EXPECT_FALSE(ev.ready());
    EXPECT_FALSE(copy.ready());
    EXPECT_EQ(ev.record_generation().value, 0U);
    EXPECT_EQ(ev.completed_generation().value, 0U);

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::record_event(q, ev)).has_value());

    EXPECT_TRUE(ev.ready());
    EXPECT_TRUE(copy.ready());
    EXPECT_EQ(ev.record_generation().value, 1U);
    EXPECT_EQ(ev.completed_generation().value, 1U);
}

TEST(AccelEventTest, RecordReservesGenerationAtStartBeforeCompletion) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 1,
        .queue_capacity = std::nullopt,
    }};
    auto q = ctx.get_queue();
    forge::accel::mock::event ev;
    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool release = false;
    auto blocker_state = std::make_shared<async_state>();
    auto record_state = std::make_shared<async_state>();

    auto blocker = forge::accel::mock::submit(q, [&] {
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

    auto record = forge::accel::mock::record_event(q, ev);
    auto record_op = std::execution::connect(
        std::move(record),
        async_receiver{record_state});
    std::execution::start(record_op);

    EXPECT_EQ(ev.record_generation().value, 1U);
    EXPECT_EQ(ev.completed_generation().value, 0U);
    EXPECT_FALSE(ev.ready());

    {
        std::lock_guard lk{mtx};
        release = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done(record_state));
    EXPECT_TRUE(record_state->value);
    EXPECT_EQ(ev.completed_generation().value, 1U);
    EXPECT_TRUE(ev.ready());
}

TEST(AccelEventTest, QueryEventReportsGenerationState) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 1,
        .queue_capacity = std::nullopt,
    }};
    auto q = ctx.get_queue();
    forge::accel::mock::event ev;
    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool release = false;
    auto blocker_state = std::make_shared<async_state>();
    auto record_state = std::make_shared<async_state>();

    auto blocker = forge::accel::mock::submit(q, [&] {
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

    auto record = forge::accel::mock::record_event(q, ev);
    auto record_op = std::execution::connect(
        std::move(record),
        async_receiver{record_state});
    std::execution::start(record_op);

    auto pending = std::execution::sync_wait(forge::accel::mock::query_event(ev));
    ASSERT_TRUE(pending.has_value());
    auto pending_snapshot = std::get<0>(*pending);
    EXPECT_EQ(pending_snapshot.record_generation.value, 1U);
    EXPECT_EQ(pending_snapshot.completed_generation.value, 0U);
    EXPECT_FALSE(pending_snapshot.ready);

    {
        std::lock_guard lk{mtx};
        release = true;
    }
    cv.notify_all();
    ASSERT_TRUE(wait_done(record_state));

    auto completed = forge::wait_result(forge::accel::mock::query_event_typed(ev));
    ASSERT_TRUE(completed.has_value());
    auto completed_snapshot = std::get<0>(completed.value());
    EXPECT_EQ(completed_snapshot.record_generation.value, 1U);
    EXPECT_EQ(completed_snapshot.completed_generation.value, 1U);
    EXPECT_TRUE(completed_snapshot.ready);
}

TEST(AccelEventTest, QueueKindsAreRecorded) {
    forge::accel::mock::context ctx;
    auto compute = ctx.get_queue(forge::accel::queue_kind::compute);
    auto copy = ctx.get_queue(forge::accel::queue_kind::copy);
    auto command = ctx.get_device().get_queue(forge::accel::queue_kind::command);

    EXPECT_EQ(compute.kind(), forge::accel::queue_kind::compute);
    EXPECT_EQ(copy.kind(), forge::accel::queue_kind::copy);
    EXPECT_EQ(command.kind(), forge::accel::queue_kind::command);
}

TEST(AccelEventTest, PerQueueFifoIsPreserved) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 2,
        .queue_capacity = std::nullopt,
    }};
    auto q = ctx.get_queue(forge::accel::queue_kind::compute);
    std::mutex mtx;
    std::vector<int> order;
    auto first_state = std::make_shared<async_state>();
    auto second_state = std::make_shared<async_state>();
    auto third_state = std::make_shared<async_state>();

    auto first = forge::accel::mock::submit(q, [&] {
        std::lock_guard lk{mtx};
        order.push_back(1);
    });
    auto second = forge::accel::mock::submit(q, [&] {
        std::lock_guard lk{mtx};
        order.push_back(2);
    });
    auto third = forge::accel::mock::submit(q, [&] {
        std::lock_guard lk{mtx};
        order.push_back(3);
    });
    auto first_op = std::execution::connect(std::move(first), async_receiver{first_state});
    auto second_op = std::execution::connect(std::move(second), async_receiver{second_state});
    auto third_op = std::execution::connect(std::move(third), async_receiver{third_state});

    std::execution::start(first_op);
    std::execution::start(second_op);
    std::execution::start(third_op);
    ctx.wait();

    ASSERT_TRUE(wait_done(first_state));
    ASSERT_TRUE(wait_done(second_state));
    ASSERT_TRUE(wait_done(third_state));
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST(AccelEventTest, DefaultQueueHandleIsStable) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 2,
        .queue_capacity = std::nullopt,
    }};
    auto first_q = ctx.get_queue();
    auto second_q = ctx.get_queue();
    std::mutex mtx;
    std::condition_variable cv;
    bool first_started = false;
    bool release_first = false;
    auto first_state = std::make_shared<async_state>();
    auto second_state = std::make_shared<async_state>();

    auto first = forge::accel::mock::submit(first_q, [&] {
        {
            std::lock_guard lk{mtx};
            first_started = true;
        }
        cv.notify_all();
        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return release_first; });
    });
    auto second = forge::accel::mock::submit(second_q, [] {});
    auto first_op = std::execution::connect(std::move(first), async_receiver{first_state});
    auto second_op = std::execution::connect(std::move(second), async_receiver{second_state});

    std::execution::start(first_op);
    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return first_started; }));
    }
    std::execution::start(second_op);

    EXPECT_FALSE(wait_done_for(second_state, 50ms));

    {
        std::lock_guard lk{mtx};
        release_first = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done(first_state));
    ASSERT_TRUE(wait_done(second_state));
    EXPECT_TRUE(first_state->value);
    EXPECT_TRUE(second_state->value);
}

TEST(AccelEventTest, DefaultQueueUsesStreamZero) {
    forge::accel::mock::context ctx;
    auto general = ctx.get_queue();
    auto compute = ctx.get_queue(forge::accel::queue_kind::compute);

    auto general_snapshot = forge::accel::mock::query_stream(general);
    auto compute_snapshot = forge::accel::mock::query_stream(compute);

    EXPECT_EQ(general_snapshot.stream.value, 0U);
    EXPECT_EQ(general_snapshot.kind, forge::accel::queue_kind::general);
    EXPECT_TRUE(general_snapshot.idle);
    EXPECT_NE(compute_snapshot.stream.value, 0U);
    EXPECT_EQ(compute_snapshot.kind, forge::accel::queue_kind::compute);
}

TEST(AccelEventTest, QueryStreamReportsPendingAndIdle) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 1,
        .queue_capacity = std::nullopt,
    }};
    auto q = ctx.get_queue(forge::accel::queue_kind::compute);
    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool release = false;
    auto state = std::make_shared<async_state>();

    auto sender = forge::accel::mock::submit(q, [&] {
        {
            std::lock_guard lk{mtx};
            started = true;
        }
        cv.notify_all();
        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return release; });
    });
    auto op = std::execution::connect(std::move(sender), async_receiver{state});
    std::execution::start(op);

    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return started; }));
    }

    auto pending = forge::accel::mock::query_stream(q);
    EXPECT_FALSE(pending.idle);
    EXPECT_EQ(pending.pending_nodes, 1U);

    {
        std::lock_guard lk{mtx};
        release = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done(state));
    auto idle = forge::accel::mock::query_stream(q);
    EXPECT_TRUE(idle.idle);
    EXPECT_EQ(idle.pending_nodes, 0U);
}

TEST(AccelEventTest, SynchronizeStreamWaitsOnlyForOneStream) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 2,
        .queue_capacity = std::nullopt,
    }};
    auto blocked_q = ctx.get_queue(forge::accel::queue_kind::compute);
    auto idle_q = ctx.get_queue(forge::accel::queue_kind::copy);
    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool release = false;
    auto state = std::make_shared<async_state>();

    auto sender = forge::accel::mock::submit(blocked_q, [&] {
        {
            std::lock_guard lk{mtx};
            started = true;
        }
        cv.notify_all();
        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return release; });
    });
    auto op = std::execution::connect(std::move(sender), async_receiver{state});
    std::execution::start(op);
    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return started; }));
    }

    auto idle_result = forge::accel::mock::synchronize_stream(idle_q);
    EXPECT_TRUE(idle_result);
    EXPECT_TRUE(idle_result.snapshot.idle);

    auto timeout_result = forge::accel::mock::synchronize_stream(
        blocked_q,
        forge::accel::mock::stream_sync_options{.timeout = 20ms});
    EXPECT_EQ(timeout_result.status, forge::accel::command_status::timed_out);
    EXPECT_FALSE(timeout_result.snapshot.idle);

    {
        std::lock_guard lk{mtx};
        release = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done(state));
    auto done = forge::accel::mock::synchronize_stream(blocked_q);
    EXPECT_TRUE(done);
    EXPECT_TRUE(done.snapshot.idle);
}

TEST(AccelEventTest, StreamSynchronizeReportsAndClearsStickyError) {
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue(forge::accel::queue_kind::copy);
    forge::accel::mock::device_buffer<int> device{ctx, 2};
    std::vector<int> too_large{1, 2, 3};

    EXPECT_THROW(
        (void)std::execution::sync_wait(
            forge::accel::mock::copy_to_device(
                q,
                device,
                std::span<const int>{too_large})),
        forge::accel::operation_error);

    auto sticky = forge::accel::mock::query_stream(q);
    ASSERT_TRUE(sticky.has_sticky_error);
    EXPECT_EQ(sticky.sticky_error.kind, forge::accel::error_kind::size_mismatch);

    auto observed = forge::accel::mock::synchronize_stream(q);
    EXPECT_FALSE(observed);
    ASSERT_TRUE(observed.has_error);
    EXPECT_EQ(observed.sticky_error.kind, forge::accel::error_kind::size_mismatch);

    auto cleared = forge::accel::mock::query_stream(q);
    EXPECT_FALSE(cleared.has_sticky_error);
}

TEST(AccelEventTest, WaitEventCompletesAfterRecordEvent) {
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue();
    forge::accel::mock::event ev;

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::record_event(q, ev)).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::wait_event(q, ev)).has_value());
}

TEST(AccelEventTest, CrossQueueWaitCompletesAfterOtherQueueRecordsEvent) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 2,
        .queue_capacity = std::nullopt,
    }};
    auto compute = ctx.get_queue(forge::accel::queue_kind::compute);
    auto copy = ctx.get_queue(forge::accel::queue_kind::copy);
    forge::accel::mock::event ev;
    auto wait_state = std::make_shared<async_state>();
    auto record_state = std::make_shared<async_state>();

    auto wait_sender = forge::accel::mock::wait_event(compute, ev);
    auto wait_op = std::execution::connect(
        std::move(wait_sender),
        async_receiver{wait_state});
    std::execution::start(wait_op);

    EXPECT_FALSE(wait_done_for(wait_state, 50ms));

    auto record_sender = forge::accel::mock::record_event(copy, ev);
    auto record_op = std::execution::connect(
        std::move(record_sender),
        async_receiver{record_state});
    std::execution::start(record_op);

    ASSERT_TRUE(wait_done(record_state));
    ASSERT_TRUE(wait_done(wait_state));
    EXPECT_TRUE(record_state->value);
    EXPECT_TRUE(wait_state->value);
    EXPECT_TRUE(ev.ready());
}

TEST(AccelEventTest, MultipleWaitersObserveReservedGeneration) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 2,
        .queue_capacity = std::nullopt,
    }};
    auto copy = ctx.get_queue(forge::accel::queue_kind::copy);
    auto compute = ctx.get_queue(forge::accel::queue_kind::compute);
    forge::accel::mock::event ev;
    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool release = false;
    auto blocker_state = std::make_shared<async_state>();
    auto record_state = std::make_shared<async_state>();
    auto first_wait_state = std::make_shared<async_state>();
    auto second_wait_state = std::make_shared<async_state>();

    auto blocker = forge::accel::mock::submit(copy, [&] {
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

    auto record = forge::accel::mock::record_event(copy, ev);
    auto record_op = std::execution::connect(
        std::move(record),
        async_receiver{record_state});
    std::execution::start(record_op);

    auto first_wait = forge::accel::mock::wait_event(compute, ev);
    auto second_wait = forge::accel::mock::wait_event(compute, ev);
    auto first_wait_op = std::execution::connect(
        std::move(first_wait),
        async_receiver{first_wait_state});
    auto second_wait_op = std::execution::connect(
        std::move(second_wait),
        async_receiver{second_wait_state});
    std::execution::start(first_wait_op);
    std::execution::start(second_wait_op);

    EXPECT_EQ(ev.record_generation().value, 1U);
    EXPECT_EQ(ev.completed_generation().value, 0U);
    EXPECT_FALSE(wait_done_for(first_wait_state, 50ms));
    EXPECT_FALSE(wait_done_for(second_wait_state, 50ms));

    {
        std::lock_guard lk{mtx};
        release = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done(record_state));
    ASSERT_TRUE(wait_done(first_wait_state));
    ASSERT_TRUE(wait_done(second_wait_state));
    EXPECT_TRUE(first_wait_state->value);
    EXPECT_TRUE(second_wait_state->value);
    EXPECT_EQ(ev.completed_generation().value, 1U);
}

TEST(AccelEventTest, SecondRecordCreatesNewWaitTargetGeneration) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 2,
        .queue_capacity = std::nullopt,
    }};
    auto copy = ctx.get_queue(forge::accel::queue_kind::copy);
    auto compute = ctx.get_queue(forge::accel::queue_kind::compute);
    forge::accel::mock::event ev;

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::record_event(copy, ev)).has_value());
    EXPECT_EQ(ev.record_generation().value, 1U);
    EXPECT_EQ(ev.completed_generation().value, 1U);

    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool release = false;
    auto blocker_state = std::make_shared<async_state>();
    auto record_state = std::make_shared<async_state>();
    auto wait_state = std::make_shared<async_state>();

    auto blocker = forge::accel::mock::submit(copy, [&] {
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

    auto second_record = forge::accel::mock::record_event(copy, ev);
    auto second_record_op = std::execution::connect(
        std::move(second_record),
        async_receiver{record_state});
    std::execution::start(second_record_op);
    EXPECT_EQ(ev.record_generation().value, 2U);
    EXPECT_EQ(ev.completed_generation().value, 1U);
    EXPECT_FALSE(ev.ready());

    auto wait = forge::accel::mock::wait_event(compute, ev);
    auto wait_op = std::execution::connect(std::move(wait), async_receiver{wait_state});
    std::execution::start(wait_op);
    EXPECT_FALSE(wait_done_for(wait_state, 50ms));

    {
        std::lock_guard lk{mtx};
        release = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done(record_state));
    ASSERT_TRUE(wait_done(wait_state));
    EXPECT_TRUE(wait_state->value);
    EXPECT_EQ(ev.completed_generation().value, 2U);
    EXPECT_TRUE(ev.ready());
}

TEST(AccelEventTest, CopyComputeCopyPipelineUsesCrossQueueEvents) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 2,
        .queue_capacity = std::nullopt,
    }};
    auto copy = ctx.get_queue(forge::accel::queue_kind::copy);
    auto compute = ctx.get_queue(forge::accel::queue_kind::compute);
    forge::accel::mock::event uploaded;
    forge::accel::mock::event computed;
    forge::accel::mock::device_buffer<int> device{ctx, 4};
    std::vector<int> input{1, 2, 3, 4};
    std::vector<int> output(4);

    auto upload_state = std::make_shared<async_state>();
    auto uploaded_state = std::make_shared<async_state>();
    auto wait_upload_state = std::make_shared<async_state>();
    auto compute_state = std::make_shared<async_state>();
    auto computed_state = std::make_shared<async_state>();
    auto wait_compute_state = std::make_shared<async_state>();
    auto download_state = std::make_shared<async_state>();

    auto upload = forge::accel::mock::copy_to_device(
        copy,
        device,
        std::span<const int>{input});
    auto record_uploaded = forge::accel::mock::record_event(copy, uploaded);
    auto wait_uploaded = forge::accel::mock::wait_event(compute, uploaded);
    auto run_compute = forge::accel::mock::submit(compute, [&] {
        for (auto& value : device.span()) {
            value *= 3;
        }
    });
    auto record_computed = forge::accel::mock::record_event(compute, computed);
    auto wait_computed = forge::accel::mock::wait_event(copy, computed);
    auto download = forge::accel::mock::copy_to_host(copy, std::span<int>{output}, device);

    auto upload_op = std::execution::connect(std::move(upload), async_receiver{upload_state});
    auto record_uploaded_op = std::execution::connect(
        std::move(record_uploaded),
        async_receiver{uploaded_state});
    auto wait_uploaded_op = std::execution::connect(
        std::move(wait_uploaded),
        async_receiver{wait_upload_state});
    auto compute_op = std::execution::connect(
        std::move(run_compute),
        async_receiver{compute_state});
    auto record_computed_op = std::execution::connect(
        std::move(record_computed),
        async_receiver{computed_state});
    auto wait_computed_op = std::execution::connect(
        std::move(wait_computed),
        async_receiver{wait_compute_state});
    auto download_op = std::execution::connect(
        std::move(download),
        async_receiver{download_state});

    std::execution::start(upload_op);
    std::execution::start(record_uploaded_op);
    std::execution::start(wait_uploaded_op);
    std::execution::start(compute_op);
    std::execution::start(record_computed_op);
    std::execution::start(wait_computed_op);
    std::execution::start(download_op);
    ctx.wait();

    ASSERT_TRUE(wait_done(upload_state));
    ASSERT_TRUE(wait_done(uploaded_state));
    ASSERT_TRUE(wait_done(wait_upload_state));
    ASSERT_TRUE(wait_done(compute_state));
    ASSERT_TRUE(wait_done(computed_state));
    ASSERT_TRUE(wait_done(wait_compute_state));
    ASSERT_TRUE(wait_done(download_state));
    EXPECT_TRUE(upload_state->value);
    EXPECT_TRUE(uploaded_state->value);
    EXPECT_TRUE(wait_upload_state->value);
    EXPECT_TRUE(compute_state->value);
    EXPECT_TRUE(computed_state->value);
    EXPECT_TRUE(wait_compute_state->value);
    EXPECT_TRUE(download_state->value);
    EXPECT_EQ(output, (std::vector<int>{3, 6, 9, 12}));
}

TEST(AccelEventTest, WaitEventStopsWhenContextStops) {
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue();
    forge::accel::mock::event ev;
    auto state = std::make_shared<async_state>();

    auto sender = forge::accel::mock::wait_event(q, ev);
    auto op = std::execution::connect(std::move(sender), async_receiver{state});
    std::execution::start(op);

    ctx.request_stop();

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state->value);
    EXPECT_TRUE(state->stopped);
    EXPECT_FALSE(state->error);
}

TEST(AccelEventTest, WaitEventTimeoutReportsError) {
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue();
    forge::accel::mock::event ev;

    EXPECT_THROW(
        (void)std::execution::sync_wait(
            forge::accel::mock::wait_event(
                q,
                ev,
                forge::accel::mock::event_wait_options{.timeout = 10ms})),
        std::runtime_error);

    auto typed = forge::wait_result(
        forge::accel::mock::wait_event_typed(
            q,
            ev,
            forge::accel::mock::event_wait_options{.timeout = 10ms}));
    ASSERT_TRUE(typed.has_error());
    auto* error = typed.error_if<forge::accel::error>();
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->kind, forge::accel::error_kind::timeout);
}

TEST(AccelEventTest, WaitEventWithLongTimeoutStopsWhenContextStops) {
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue();
    forge::accel::mock::event ev;
    auto state = std::make_shared<async_state>();

    auto sender = forge::accel::mock::wait_event(
        q,
        ev,
        forge::accel::mock::event_wait_options{.timeout = 1h});
    auto op = std::execution::connect(std::move(sender), async_receiver{state});
    std::execution::start(op);

    ctx.request_stop();

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state->value);
    EXPECT_TRUE(state->stopped);
    EXPECT_FALSE(state->error);
}

TEST(AccelEventTest, SynchronizeEventWaitsForCurrentRecordedGeneration) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 2,
        .queue_capacity = std::nullopt,
    }};
    auto copy = ctx.get_queue(forge::accel::queue_kind::copy);
    auto compute = ctx.get_queue(forge::accel::queue_kind::compute);
    forge::accel::mock::event ev;

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::synchronize_event(compute, ev)).has_value());

    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool release = false;
    auto blocker_state = std::make_shared<async_state>();
    auto record_state = std::make_shared<async_state>();
    auto sync_state = std::make_shared<async_state>();

    auto blocker = forge::accel::mock::submit(copy, [&] {
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

    auto record = forge::accel::mock::record_event(copy, ev);
    auto record_op = std::execution::connect(
        std::move(record),
        async_receiver{record_state});
    std::execution::start(record_op);

    auto sync = forge::accel::mock::synchronize_event(compute, ev);
    auto sync_op = std::execution::connect(std::move(sync), async_receiver{sync_state});
    std::execution::start(sync_op);
    EXPECT_FALSE(wait_done_for(sync_state, 50ms));

    {
        std::lock_guard lk{mtx};
        release = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done(record_state));
    ASSERT_TRUE(wait_done(sync_state));
    EXPECT_TRUE(sync_state->value);
}

TEST(AccelEventTest, SameQueueWaitBeforeRecordStopsOnContextStop) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 1,
        .queue_capacity = std::nullopt,
    }};
    auto q = ctx.get_queue();
    forge::accel::mock::event ev;
    auto wait_state = std::make_shared<async_state>();
    auto record_state = std::make_shared<async_state>();

    auto wait_sender = forge::accel::mock::wait_event(q, ev);
    auto wait_op = std::execution::connect(
        std::move(wait_sender),
        async_receiver{wait_state});
    std::execution::start(wait_op);

    EXPECT_FALSE(wait_done_for(wait_state, 50ms));
    EXPECT_FALSE(ev.ready());

    auto record_sender = forge::accel::mock::record_event(q, ev);
    auto record_op = std::execution::connect(
        std::move(record_sender),
        async_receiver{record_state});
    std::execution::start(record_op);

    EXPECT_FALSE(wait_done_for(record_state, 50ms));
    EXPECT_FALSE(ev.ready());

    ctx.request_stop();

    ASSERT_TRUE(wait_done(wait_state));
    ASSERT_TRUE(wait_done(record_state));
    EXPECT_FALSE(wait_state->value);
    EXPECT_TRUE(wait_state->stopped);
    EXPECT_FALSE(wait_state->error);
    EXPECT_FALSE(record_state->value);
    EXPECT_TRUE(record_state->stopped);
    EXPECT_FALSE(record_state->error);
    EXPECT_FALSE(ev.ready());
}

TEST(AccelEventTest, EventElapsedTimeRequiresCompletedRecord) {
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue();
    forge::accel::mock::event ev;

    EXPECT_THROW(
        (void)forge::accel::mock::elapsed_time(ev),
        forge::accel::operation_error);

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::record_event(q, ev)).has_value());

    const auto elapsed = forge::accel::mock::elapsed_time(ev);
    EXPECT_GE(elapsed, std::chrono::steady_clock::duration::zero());
}

TEST(AccelEventTest, FenceCompletesAfterEarlierAcceptedCommand) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
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

    auto first = forge::accel::mock::submit(q, [&] {
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

    auto fence = forge::accel::mock::fence(q);
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
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue();
    forge::accel::mock::event ev;
    auto moved = std::move(ev);

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::record_event(q, moved)).has_value());
    EXPECT_THROW(
        (void)std::execution::sync_wait(
            forge::accel::mock::record_event(q, std::move(ev))),
        std::runtime_error);
}

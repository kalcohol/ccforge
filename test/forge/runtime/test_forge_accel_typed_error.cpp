#include <gtest/gtest.h>
#include <forge/accel.hpp>
#include <forge/erased_sender.hpp>
#include "forge_operation_destroy.hpp"
#include <execution>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace {

using namespace std::chrono_literals;

struct typed_state {
    std::mutex mtx;
    std::condition_variable cv;
    bool value = false;
    bool stopped = false;
    bool error_seen = false;
    forge::accel::error error{};

    [[nodiscard]] bool done() const noexcept {
        return value || stopped || error_seen;
    }
};

struct typed_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<typed_state> state;

    void set_value() && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->value = true;
        }
        state->cv.notify_all();
    }

    void set_error(forge::accel::error error) && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->error_seen = true;
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

struct stop_env {
    std::inplace_stop_source* source;

    friend auto tag_invoke(
        std::execution::get_stop_token_t,
        const stop_env& self) noexcept -> std::inplace_stop_token {
        return self.source->get_token();
    }
};

struct stopped_typed_receiver : typed_receiver {
    std::inplace_stop_source* source;

    auto get_env() const noexcept -> stop_env {
        return stop_env{source};
    }
};

struct self_destroying_accel_typed_receiver {
    using receiver_concept = std::execution::receiver_t;

    forge_test::destroy_context_base* context = nullptr;
    std::inplace_stop_source* source = nullptr;

    void set_value() && noexcept { context->destroy(); }
    void set_error(forge::accel::error) && noexcept { context->destroy(); }
    void set_stopped() && noexcept { context->destroy(); }

    auto get_env() const noexcept -> stop_env {
        return stop_env{source};
    }
};

[[nodiscard]] bool wait_done(const std::shared_ptr<typed_state>& state) {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, 2s, [&] { return state->done(); });
}

void expect_error_kind(
    const std::shared_ptr<typed_state>& state,
    forge::accel::error_kind kind) {
    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state->value);
    EXPECT_FALSE(state->stopped);
    ASSERT_TRUE(state->error_seen);
    EXPECT_EQ(state->error.kind, kind);
}

using accel_error_cs = std::execution::completion_signatures<
    std::execution::set_value_t(),
    std::execution::set_error_t(forge::accel::error),
    std::execution::set_stopped_t()>;

} // namespace

TEST(AccelTypedErrorTest, CopySizeMismatchReportsTypedError) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    forge::accel::device_buffer<int> device{ctx, 2};
    std::vector<int> input{1, 2, 3};
    auto state = std::make_shared<typed_state>();

    auto sender = forge::accel::copy_to_device_typed(
        q,
        device,
        std::span<const int>{input});
    auto op = std::execution::connect(std::move(sender), typed_receiver{state});
    std::execution::start(op);

    expect_error_kind(state, forge::accel::error_kind::size_mismatch);
    EXPECT_TRUE(state->error.cause);
}

TEST(AccelTypedErrorTest, InvalidEventReportsTypedError) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    forge::accel::event ev;
    auto moved = std::move(ev);
    auto state = std::make_shared<typed_state>();

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::record_event(q, moved)).has_value());

    auto sender = forge::accel::record_event_typed(q, std::move(ev));
    auto op = std::execution::connect(std::move(sender), typed_receiver{state});
    std::execution::start(op);

    expect_error_kind(state, forge::accel::error_kind::invalid_event);
}

TEST(AccelTypedErrorTest, MessageFailureReportsCommandStatus) {
    struct request_packet { int value = 0; };
    struct response_packet { int value = 0; };

    forge::accel::context ctx;
    auto session = ctx.get_device().open_session();
    response_packet response{};
    auto state = std::make_shared<typed_state>();

    auto sender = forge::accel::submit_message_typed(
        session,
        request_packet{1},
        response,
        [](request_packet&, response_packet&) noexcept {
            return forge::accel::command_status::failed;
        });
    auto op = std::execution::connect(std::move(sender), typed_receiver{state});
    std::execution::start(op);

    expect_error_kind(state, forge::accel::error_kind::command_failed);
    EXPECT_EQ(state->error.status, forge::accel::command_status::failed);
    EXPECT_TRUE(state->error.cause);
}

TEST(AccelTypedErrorTest, SubmitUserExceptionPreservesCause) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    auto state = std::make_shared<typed_state>();

    auto sender = forge::accel::submit_typed(q, [] {
        throw std::runtime_error{"user kernel failed"};
    });
    auto op = std::execution::connect(std::move(sender), typed_receiver{state});
    std::execution::start(op);

    expect_error_kind(state, forge::accel::error_kind::user_exception);
    ASSERT_TRUE(state->error.cause);
    EXPECT_THROW(std::rethrow_exception(state->error.cause), std::runtime_error);
}

TEST(AccelTypedErrorTest, TypedSenderCrossesErasedSenderBoundary) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    forge::accel::device_buffer<int> device{ctx, 1};
    std::vector<int> input{1, 2};
    auto state = std::make_shared<typed_state>();

    forge::erased_sender<accel_error_cs> sender{
        forge::accel::copy_to_device_typed(
            q,
            device,
            std::span<const int>{input})};
    auto op = std::execution::connect(std::move(sender), typed_receiver{state});
    std::execution::start(op);

    expect_error_kind(state, forge::accel::error_kind::size_mismatch);
}

TEST(AccelTypedErrorTest, DefaultExceptionApiStillWorks) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    forge::accel::device_buffer<int> device{ctx, 2};
    std::vector<int> input{1, 2, 3};

    EXPECT_THROW(
        (void)std::execution::sync_wait(
            forge::accel::copy_to_device(q, device, std::span<const int>{input})),
        std::runtime_error);
}

TEST(AccelTypedErrorTest, PreStoppedTypedSenderStillStops) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    std::inplace_stop_source source;
    source.request_stop();
    bool ran = false;
    auto state = std::make_shared<typed_state>();

    auto sender = forge::accel::submit_typed(q, [&] {
        ran = true;
    });
    auto op = std::execution::connect(
        std::move(sender),
        stopped_typed_receiver{{state}, &source});
    std::execution::start(op);
    ctx.wait();

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(ran);
    EXPECT_FALSE(state->value);
    EXPECT_TRUE(state->stopped);
    EXPECT_FALSE(state->error_seen);
}

TEST(AccelTypedErrorTest, PreStoppedTypedSenderAllowsReceiverToDestroyOperation) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    std::inplace_stop_source source;
    source.request_stop();
    bool ran = false;
    auto sender = forge::accel::submit_typed(q, [&] {
        ran = true;
    });

    using sender_t = decltype(sender);
    using receiver_t = self_destroying_accel_typed_receiver;
    using op_t = decltype(std::execution::connect(
        std::declval<sender_t>(),
        std::declval<receiver_t>()));

    bool destroyed = false;
    forge_test::operation_destroy_context<op_t> context{&destroyed};

    auto& op = context.emplace_from([&] {
        return std::execution::connect(
            std::move(sender),
            self_destroying_accel_typed_receiver{&context, &source});
    });
    std::execution::start(op);
    ctx.wait();

    EXPECT_FALSE(ran);
    EXPECT_TRUE(destroyed);
    EXPECT_FALSE(context.has_value);
}

#include <gtest/gtest.h>
#include <forge/accel.hpp>
#include <forge/wait_result.hpp>
#include <execution>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
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

[[nodiscard]] auto wait_done(const std::shared_ptr<async_state>& state) -> bool {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, 2s, [&] { return state->done(); });
}

[[nodiscard]] auto wait_done_for(
    const std::shared_ptr<async_state>& state,
    std::chrono::milliseconds timeout) -> bool {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, timeout, [&] { return state->done(); });
}

} // namespace

TEST(AccelCallbackTest, CallbackNodeRunsInStreamOrder) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 2,
        .queue_capacity = std::nullopt,
    }};
    forge::accel::mock::host_callback_dispatcher callbacks;
    auto q = ctx.get_queue(forge::accel::queue_kind::compute);
    std::mutex mtx;
    std::vector<int> order;

    auto id = callbacks.register_callback([&](forge::accel::callback_invoke_id invoke) {
        EXPECT_NE(invoke.value, 0U);
        std::lock_guard lk{mtx};
        order.push_back(2);
    });

    auto first_state = std::make_shared<async_state>();
    auto callback_state = std::make_shared<async_state>();
    auto third_state = std::make_shared<async_state>();

    auto first = forge::accel::mock::submit(q, [&] {
        std::lock_guard lk{mtx};
        order.push_back(1);
    });
    auto callback = forge::accel::mock::enqueue_callback(q, callbacks, id);
    auto third = forge::accel::mock::submit(q, [&] {
        std::lock_guard lk{mtx};
        order.push_back(3);
    });

    auto first_op = std::execution::connect(
        std::move(first),
        async_receiver{first_state});
    auto callback_op = std::execution::connect(
        std::move(callback),
        async_receiver{callback_state});
    auto third_op = std::execution::connect(
        std::move(third),
        async_receiver{third_state});

    std::execution::start(first_op);
    std::execution::start(callback_op);
    std::execution::start(third_op);

    ASSERT_TRUE(wait_done(first_state));
    ASSERT_TRUE(wait_done(callback_state));
    ASSERT_TRUE(wait_done(third_state));
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));

    auto completions = callbacks.completions();
    ASSERT_EQ(completions.size(), 1U);
    EXPECT_TRUE(completions[0]);
    EXPECT_EQ(completions[0].callback, id);
    EXPECT_NE(completions[0].invoke.value, 0U);
}

TEST(AccelCallbackTest, CallbackNodeBlocksFollowingCommandUntilComplete) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 2,
        .queue_capacity = std::nullopt,
    }};
    forge::accel::mock::host_callback_dispatcher callbacks;
    auto q = ctx.get_queue(forge::accel::queue_kind::compute);
    std::mutex mtx;
    std::condition_variable cv;
    bool callback_started = false;
    bool release_callback = false;
    bool third_ran = false;

    auto id = callbacks.register_callback([&] {
        {
            std::lock_guard lk{mtx};
            callback_started = true;
        }
        cv.notify_all();
        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return release_callback; });
    });

    auto callback_state = std::make_shared<async_state>();
    auto third_state = std::make_shared<async_state>();
    auto callback = forge::accel::mock::enqueue_callback(q, callbacks, id);
    auto third = forge::accel::mock::submit(q, [&] {
        std::lock_guard lk{mtx};
        third_ran = true;
    });
    auto callback_op = std::execution::connect(
        std::move(callback),
        async_receiver{callback_state});
    auto third_op = std::execution::connect(
        std::move(third),
        async_receiver{third_state});

    std::execution::start(callback_op);
    std::execution::start(third_op);

    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return callback_started; }));
        EXPECT_FALSE(third_ran);
    }
    EXPECT_FALSE(wait_done_for(third_state, 50ms));

    {
        std::lock_guard lk{mtx};
        release_callback = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done(callback_state));
    ASSERT_TRUE(wait_done(third_state));
    EXPECT_TRUE(third_state->value);
}

TEST(AccelCallbackTest, UnregisterWaitsForInFlightInvoke) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 2,
        .queue_capacity = std::nullopt,
    }};
    forge::accel::mock::host_callback_dispatcher callbacks;
    auto q = ctx.get_queue(forge::accel::queue_kind::compute);
    std::mutex mtx;
    std::condition_variable cv;
    bool callback_started = false;
    bool release_callback = false;
    bool unregister_done = false;

    auto id = callbacks.register_callback([&] {
        {
            std::lock_guard lk{mtx};
            callback_started = true;
        }
        cv.notify_all();
        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return release_callback; });
    });

    auto state = std::make_shared<async_state>();
    auto sender = forge::accel::mock::enqueue_callback(q, callbacks, id);
    auto op = std::execution::connect(std::move(sender), async_receiver{state});
    std::execution::start(op);

    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return callback_started; }));
    }

    std::thread unregister_thread{[&] {
        callbacks.unregister_callback(id);
        {
            std::lock_guard lk{mtx};
            unregister_done = true;
        }
        cv.notify_all();
    }};

    {
        std::unique_lock lk{mtx};
        EXPECT_FALSE(cv.wait_for(lk, 50ms, [&] { return unregister_done; }));
        release_callback = true;
    }
    cv.notify_all();

    unregister_thread.join();
    ASSERT_TRUE(wait_done(state));
    EXPECT_TRUE(state->value);
    EXPECT_TRUE(unregister_done);
}

TEST(AccelCallbackTest, CallbackCanInvokeAnotherCallbackReentrantly) {
    forge::accel::mock::context ctx;
    forge::accel::mock::host_callback_dispatcher callbacks;
    auto q = ctx.get_queue(forge::accel::queue_kind::compute);
    std::mutex mtx;
    std::vector<int> order;

    auto inner = callbacks.register_callback([&] {
        std::lock_guard lk{mtx};
        order.push_back(2);
    });
    auto outer = callbacks.register_callback([&] {
        {
            std::lock_guard lk{mtx};
            order.push_back(1);
        }
        auto result = callbacks.invoke(inner);
        EXPECT_TRUE(result);
    });

    auto state = std::make_shared<async_state>();
    auto sender = forge::accel::mock::enqueue_callback(q, callbacks, outer);
    auto op = std::execution::connect(std::move(sender), async_receiver{state});
    std::execution::start(op);

    ASSERT_TRUE(wait_done(state));
    EXPECT_TRUE(state->value);
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
    EXPECT_EQ(callbacks.completions().size(), 2U);
}

TEST(AccelCallbackTest, CallbackCanUnregisterItself) {
    forge::accel::mock::context ctx;
    forge::accel::mock::host_callback_dispatcher callbacks;
    auto q = ctx.get_queue(forge::accel::queue_kind::compute);
    bool ran = false;
    forge::accel::callback_id id{};
    id = callbacks.register_callback([&] {
        ran = true;
        callbacks.unregister_callback(id);
    });

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::enqueue_callback(q, callbacks, id)).has_value());
    EXPECT_TRUE(ran);

    auto result = forge::wait_result(
        forge::accel::mock::enqueue_callback_typed(q, callbacks, id));
    ASSERT_TRUE(result.has_error());
    auto* err = result.error_if<forge::accel::error>();
    ASSERT_NE(err, nullptr);
    EXPECT_EQ(err->kind, forge::accel::error_kind::protocol_error);
}

TEST(AccelCallbackTest, MissingCallbackReportsTypedProtocolError) {
    forge::accel::mock::context ctx;
    forge::accel::mock::host_callback_dispatcher callbacks;
    auto q = ctx.get_queue();

    auto result = forge::wait_result(
        forge::accel::mock::enqueue_callback_typed(
            q,
            callbacks,
            forge::accel::callback_id{42}));

    ASSERT_TRUE(result.has_error());
    auto* err = result.error_if<forge::accel::error>();
    ASSERT_NE(err, nullptr);
    EXPECT_EQ(err->kind, forge::accel::error_kind::protocol_error);
}

TEST(AccelCallbackTest, ThrowingCallbackReportsTypedUserException) {
    forge::accel::mock::context ctx;
    forge::accel::mock::host_callback_dispatcher callbacks;
    auto q = ctx.get_queue();
    auto id = callbacks.register_callback([] {
        throw std::runtime_error{"callback failed"};
    });

    auto result = forge::wait_result(
        forge::accel::mock::enqueue_callback_typed(q, callbacks, id));

    ASSERT_TRUE(result.has_error());
    auto* err = result.error_if<forge::accel::error>();
    ASSERT_NE(err, nullptr);
    EXPECT_EQ(err->kind, forge::accel::error_kind::user_exception);

    auto completions = callbacks.completions();
    ASSERT_EQ(completions.size(), 1U);
    EXPECT_EQ(completions[0].status, forge::accel::callback_status::failed);
}

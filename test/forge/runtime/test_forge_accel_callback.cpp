#include <gtest/gtest.h>
#include <forge/accel.hpp>
#include <forge/wait_result.hpp>
#include <execution>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <memory_resource>
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

struct op_holder {
    virtual ~op_holder() = default;
    virtual void start() = 0;
};

template<class Sender>
struct op_holder_model : op_holder {
    using op_t = std::execution::connect_result_t<Sender, async_receiver>;

    op_holder_model(Sender sender, std::shared_ptr<async_state> state)
        : op(std::execution::connect(
              std::move(sender),
              async_receiver{std::move(state)}))
    {}

    void start() override {
        std::execution::start(op);
    }

    op_t op;
};

template<class Sender>
[[nodiscard]] auto hold_async_op(
    Sender&& sender,
    std::shared_ptr<async_state> state) -> std::unique_ptr<op_holder> {
    using sender_t = std::decay_t<Sender>;
    return std::make_unique<op_holder_model<sender_t>>(
        std::forward<Sender>(sender),
        std::move(state));
}

class checked_memory_resource : public std::pmr::memory_resource {
public:
    void disallow() noexcept {
        allow_.store(false, std::memory_order_release);
    }

    [[nodiscard]] bool unexpected_use() const noexcept {
        return unexpected_use_.load(std::memory_order_acquire);
    }

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        note_use();
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(
        void* ptr,
        std::size_t bytes,
        std::size_t alignment) override {
        note_use();
        std::pmr::new_delete_resource()->deallocate(ptr, bytes, alignment);
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    void note_use() noexcept {
        if (!allow_.load(std::memory_order_acquire)) {
            unexpected_use_.store(true, std::memory_order_release);
        }
    }

    std::atomic<bool> allow_{true};
    std::atomic<bool> unexpected_use_{false};
};

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

TEST(AccelCallbackTest, CrossDispatcherUnregisterWaitsForInFlightCallback) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 3,
        .queue_capacity = std::nullopt,
    }};
    forge::accel::mock::host_callback_dispatcher callbacks_a;
    forge::accel::mock::host_callback_dispatcher callbacks_b;
    auto q_a = ctx.get_queue(forge::accel::queue_kind::compute);
    auto q_b = ctx.get_queue(forge::accel::queue_kind::copy);
    std::mutex mtx;
    std::condition_variable cv;
    bool b_started = false;
    bool release_b = false;
    bool unregister_started = false;
    bool unregister_returned = false;

    auto id_b = callbacks_b.register_callback([&] {
        {
            std::lock_guard lk{mtx};
            b_started = true;
        }
        cv.notify_all();
        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return release_b; });
    });
    auto id_a = callbacks_a.register_callback([&] {
        {
            std::lock_guard lk{mtx};
            unregister_started = true;
        }
        cv.notify_all();
        callbacks_b.unregister_callback(id_b);
        {
            std::lock_guard lk{mtx};
            unregister_returned = true;
        }
        cv.notify_all();
    });
    ASSERT_EQ(id_a.value, id_b.value);

    auto b_state = std::make_shared<async_state>();
    auto a_state = std::make_shared<async_state>();
    auto b_sender = forge::accel::mock::enqueue_callback(q_b, callbacks_b, id_b);
    auto a_sender = forge::accel::mock::enqueue_callback(q_a, callbacks_a, id_a);
    auto b_op = std::execution::connect(
        std::move(b_sender),
        async_receiver{b_state});
    auto a_op = std::execution::connect(
        std::move(a_sender),
        async_receiver{a_state});

    std::execution::start(b_op);
    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return b_started; }));
    }
    std::execution::start(a_op);
    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return unregister_started; }));
        EXPECT_FALSE(cv.wait_for(lk, 50ms, [&] { return unregister_returned; }));
        release_b = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done(b_state));
    ASSERT_TRUE(wait_done(a_state));
    EXPECT_TRUE(b_state->value);
    EXPECT_TRUE(a_state->value);
    EXPECT_TRUE(unregister_returned);
}

TEST(AccelCallbackTest, AutoIdSkipsExplicitId) {
    forge::accel::mock::context ctx;
    forge::accel::mock::host_callback_dispatcher callbacks;
    auto q = ctx.get_queue();
    auto explicit_id = forge::accel::callback_id{1};
    int explicit_count = 0;
    int auto_count = 0;

    callbacks.register_callback(explicit_id, [&] { ++explicit_count; });
    auto auto_id = callbacks.register_callback([&] { ++auto_count; });

    ASSERT_NE(auto_id, explicit_id);
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::enqueue_callback(q, callbacks, explicit_id)).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::enqueue_callback(q, callbacks, auto_id)).has_value());
    EXPECT_EQ(explicit_count, 1);
    EXPECT_EQ(auto_count, 1);
}

TEST(AccelCallbackTest, DuplicateRegisteredIdIsRejected) {
    forge::accel::mock::host_callback_dispatcher callbacks;
    auto id = forge::accel::callback_id{17};
    callbacks.register_callback(id, [] {});

    try {
        callbacks.register_callback(id, [] {});
        FAIL() << "duplicate callback id should be rejected";
    } catch (const forge::accel::operation_error& e) {
        EXPECT_EQ(e.kind(), forge::accel::error_kind::protocol_error);
    }
}

TEST(AccelCallbackTest, OldQueuedNodeDoesNotRunReregisteredHandler) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 2,
        .queue_capacity = std::nullopt,
    }};
    forge::accel::mock::host_callback_dispatcher callbacks;
    auto q = ctx.get_queue();
    std::mutex mtx;
    std::condition_variable cv;
    bool blocker_started = false;
    bool release_blocker = false;
    int handler_a = 0;
    int handler_b = 0;
    auto id = forge::accel::callback_id{99};

    callbacks.register_callback(id, [&] { ++handler_a; });

    auto blocker_state = std::make_shared<async_state>();
    auto old_state = std::make_shared<async_state>();
    auto blocker = forge::accel::mock::submit(q, [&] {
        {
            std::lock_guard lk{mtx};
            blocker_started = true;
        }
        cv.notify_all();
        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return release_blocker; });
    });
    auto old_callback = forge::accel::mock::enqueue_callback(q, callbacks, id);
    auto blocker_op = std::execution::connect(
        std::move(blocker),
        async_receiver{blocker_state});
    auto old_op = std::execution::connect(
        std::move(old_callback),
        async_receiver{old_state});

    std::execution::start(blocker_op);
    std::execution::start(old_op);
    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return blocker_started; }));
    }

    callbacks.unregister_callback(id);
    callbacks.register_callback(id, [&] { ++handler_b; });

    {
        std::lock_guard lk{mtx};
        release_blocker = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done(blocker_state));
    ASSERT_TRUE(wait_done(old_state));
    EXPECT_TRUE(old_state->stopped);
    EXPECT_FALSE(old_state->value);
    EXPECT_FALSE(old_state->error);
    EXPECT_EQ(handler_a, 0);
    EXPECT_EQ(handler_b, 0);

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::enqueue_callback(q, callbacks, id)).has_value());
    EXPECT_EQ(handler_b, 1);
}

TEST(AccelCallbackTest, CloseStopsQueuedCallbackNode) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 2,
        .queue_capacity = std::nullopt,
    }};
    forge::accel::mock::host_callback_dispatcher callbacks;
    auto q = ctx.get_queue();
    std::mutex mtx;
    std::condition_variable cv;
    bool blocker_started = false;
    bool release_blocker = false;
    bool callback_ran = false;
    auto id = callbacks.register_callback([&] { callback_ran = true; });

    auto blocker_state = std::make_shared<async_state>();
    auto callback_state = std::make_shared<async_state>();
    auto blocker = forge::accel::mock::submit(q, [&] {
        {
            std::lock_guard lk{mtx};
            blocker_started = true;
        }
        cv.notify_all();
        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return release_blocker; });
    });
    auto callback = forge::accel::mock::enqueue_callback(q, callbacks, id);
    auto blocker_op = std::execution::connect(
        std::move(blocker),
        async_receiver{blocker_state});
    auto callback_op = std::execution::connect(
        std::move(callback),
        async_receiver{callback_state});

    std::execution::start(blocker_op);
    std::execution::start(callback_op);
    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return blocker_started; }));
    }

    callbacks.close();
    {
        std::lock_guard lk{mtx};
        release_blocker = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done(blocker_state));
    ASSERT_TRUE(wait_done(callback_state));
    EXPECT_TRUE(callback_state->stopped);
    EXPECT_FALSE(callback_state->value);
    EXPECT_FALSE(callback_state->error);
    EXPECT_FALSE(callback_ran);
}

TEST(AccelCallbackTest, DispatcherDestructionStopsPendingQueuedNode) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 2,
        .queue_capacity = std::nullopt,
    }};
    auto q = ctx.get_queue();
    std::mutex mtx;
    std::condition_variable cv;
    bool blocker_started = false;
    bool release_blocker = false;
    bool callback_ran = false;
    checked_memory_resource memory;
    std::weak_ptr<int> handler_capture;

    auto blocker_state = std::make_shared<async_state>();
    auto callback_state = std::make_shared<async_state>();
    auto blocker = forge::accel::mock::submit(q, [&] {
        {
            std::lock_guard lk{mtx};
            blocker_started = true;
        }
        cv.notify_all();
        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return release_blocker; });
    });
    auto blocker_op = std::execution::connect(
        std::move(blocker),
        async_receiver{blocker_state});

    std::execution::start(blocker_op);
    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return blocker_started; }));
    }

    std::unique_ptr<op_holder> callback_op;
    {
        forge::accel::mock::host_callback_dispatcher callbacks{
            forge::accel::mock::host_callback_dispatcher_options{
                .memory = &memory,
            }};
        auto marker = std::make_shared<int>(42);
        handler_capture = marker;
        auto id = callbacks.register_callback([&, marker] { callback_ran = true; });
        callback_op = hold_async_op(
            forge::accel::mock::enqueue_callback(q, callbacks, id),
            callback_state);
        callback_op->start();
    }
    EXPECT_TRUE(handler_capture.expired());
    memory.disallow();

    {
        std::lock_guard lk{mtx};
        release_blocker = true;
    }
    cv.notify_all();

    ASSERT_TRUE(wait_done(blocker_state));
    ASSERT_TRUE(wait_done(callback_state));
    EXPECT_TRUE(callback_state->stopped);
    EXPECT_FALSE(callback_state->value);
    EXPECT_FALSE(callback_state->error);
    EXPECT_FALSE(callback_ran);
    EXPECT_FALSE(memory.unexpected_use());
    callback_op.reset();
    EXPECT_FALSE(memory.unexpected_use());
}

TEST(AccelCallbackTest, DispatcherDestructionWaitsForInFlightStorageRelease) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 2,
        .queue_capacity = std::nullopt,
    }};
    auto q = ctx.get_queue();
    checked_memory_resource memory;
    std::optional<forge::accel::mock::host_callback_dispatcher> callbacks;
    callbacks.emplace(forge::accel::mock::host_callback_dispatcher_options{
        .memory = &memory,
    });

    std::mutex mtx;
    std::condition_variable cv;
    bool callback_started = false;
    bool release_callback = false;
    bool dispatcher_destroyed = false;
    std::weak_ptr<int> handler_capture;

    auto marker = std::make_shared<int>(7);
    handler_capture = marker;
    auto id = callbacks->register_callback([&, marker] {
        {
            std::lock_guard lk{mtx};
            callback_started = true;
        }
        cv.notify_all();
        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return release_callback; });
    });
    marker.reset();

    auto state = std::make_shared<async_state>();
    auto sender = forge::accel::mock::enqueue_callback(q, *callbacks, id);
    auto op = std::execution::connect(std::move(sender), async_receiver{state});
    std::execution::start(op);

    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return callback_started; }));
    }

    std::thread destroyer{[&] {
        callbacks.reset();
        {
            std::lock_guard lk{mtx};
            dispatcher_destroyed = true;
        }
        cv.notify_all();
    }};

    {
        std::unique_lock lk{mtx};
        EXPECT_FALSE(cv.wait_for(lk, 50ms, [&] { return dispatcher_destroyed; }));
        release_callback = true;
    }
    cv.notify_all();
    destroyer.join();

    EXPECT_TRUE(handler_capture.expired());
    memory.disallow();
    ASSERT_TRUE(wait_done(state));
    EXPECT_TRUE(state->value);
    EXPECT_FALSE(memory.unexpected_use());
}

TEST(AccelCallbackTest, ShutdownWaitIsBarrierForLaterCallbacks) {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 2,
        .queue_capacity = std::nullopt,
    }};
    forge::accel::mock::host_callback_dispatcher callbacks;
    auto q = ctx.get_queue();
    std::mutex mtx;
    std::condition_variable cv;
    bool callback_started = false;
    bool release_callback = false;
    bool wait_returned = false;
    int callback_runs = 0;

    auto id = callbacks.register_callback([&] {
        {
            std::lock_guard lk{mtx};
            callback_started = true;
            ++callback_runs;
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

    std::thread waiter{[&] {
        callbacks.shutdown();
        callbacks.wait();
        {
            std::lock_guard lk{mtx};
            wait_returned = true;
        }
        cv.notify_all();
    }};

    {
        std::unique_lock lk{mtx};
        EXPECT_FALSE(cv.wait_for(lk, 50ms, [&] { return wait_returned; }));
        release_callback = true;
    }
    cv.notify_all();
    waiter.join();

    ASSERT_TRUE(wait_done(state));
    EXPECT_TRUE(state->value);
    EXPECT_TRUE(wait_returned);

    auto later_state = std::make_shared<async_state>();
    auto later = forge::accel::mock::enqueue_callback(q, callbacks, id);
    auto later_op = std::execution::connect(
        std::move(later),
        async_receiver{later_state});
    std::execution::start(later_op);

    ASSERT_TRUE(wait_done(later_state));
    EXPECT_TRUE(later_state->stopped);
    EXPECT_EQ(callback_runs, 1);
}

TEST(AccelCallbackTest, NestedCallbackCanUnregisterAncestor) {
    forge::accel::mock::context ctx;
    forge::accel::mock::host_callback_dispatcher callbacks;
    auto q = ctx.get_queue();
    forge::accel::callback_id outer{};
    auto inner = callbacks.register_callback([&] {
        callbacks.unregister_callback(outer);
    });
    outer = callbacks.register_callback([&] {
        auto result = callbacks.invoke(inner);
        EXPECT_TRUE(result);
    });

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::enqueue_callback(q, callbacks, outer)).has_value());

    auto result = forge::wait_result(
        forge::accel::mock::enqueue_callback_typed(q, callbacks, outer));
    ASSERT_TRUE(result.has_error());
    auto* err = result.error_if<forge::accel::error>();
    ASSERT_NE(err, nullptr);
    EXPECT_EQ(err->kind, forge::accel::error_kind::protocol_error);
}

TEST(AccelCallbackTest, CompletionCapacityKeepsNewestResults) {
    forge::accel::mock::host_callback_dispatcher_options options;
    options.completion_capacity = 2;
    forge::accel::mock::host_callback_dispatcher callbacks{options};
    auto id = callbacks.register_callback([] {});

    auto first = callbacks.invoke(id);
    auto second = callbacks.invoke(id);
    auto third = callbacks.invoke(id);

    EXPECT_TRUE(first);
    EXPECT_TRUE(second);
    EXPECT_TRUE(third);
    auto completions = callbacks.completions();
    ASSERT_EQ(completions.size(), 2U);
    EXPECT_EQ(completions[0].invoke, second.invoke);
    EXPECT_EQ(completions[1].invoke, third.invoke);
}

TEST(AccelCallbackTest, CompletionCapacityZeroKeepsNoHistory) {
    forge::accel::mock::host_callback_dispatcher_options options;
    options.completion_capacity = 0;
    forge::accel::mock::host_callback_dispatcher callbacks{options};
    auto id = callbacks.register_callback([] {});

    EXPECT_TRUE(callbacks.invoke(id));
    EXPECT_TRUE(callbacks.invoke(id));
    EXPECT_TRUE(callbacks.completions().empty());
}

#ifdef FORGE_ENABLE_TEST_HOOKS
TEST(AccelCallbackTest, RecordsArePrunedAfterUnregisterAndDrain) {
    forge::accel::mock::host_callback_dispatcher callbacks;

    for (std::uint64_t i = 1; i <= 32; ++i) {
        auto id = forge::accel::callback_id{i};
        callbacks.register_callback(id, [] {});
        EXPECT_EQ(callbacks.test_record_count(), 1U);
        callbacks.unregister_callback(id);
        EXPECT_EQ(callbacks.test_record_count(), 0U);
    }
}
#endif

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

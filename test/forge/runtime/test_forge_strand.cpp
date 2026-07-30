#include <gtest/gtest.h>
#include <forge/start_detached.hpp>
#include <forge/static_thread_pool.hpp>
#include <forge/strand.hpp>
#include "forge_counting_resource.hpp"
#include <execution>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

namespace {

using namespace std::chrono_literals;

struct stopped_state {
    bool value = false;
    bool stopped = false;
};

struct stopped_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<stopped_state> state;

    void set_value() && noexcept { state->value = true; }
    void set_stopped() && noexcept { state->stopped = true; }
    template<class E>
    void set_error(E&&) && noexcept {}
    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
};

struct blocking_stopped_state {
    std::mutex mtx;
    std::condition_variable cv;
    bool active_started = false;
    bool active_running = false;
    bool release_active = false;
    bool stopped_started = false;
    bool stopped_completed = false;
    bool release_stopped = false;
    bool overlapped = false;
};

struct blocking_stopped_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<blocking_stopped_state> state;

    void set_value() && noexcept {}

    void set_stopped() && noexcept {
        std::unique_lock lk{state->mtx};
        state->overlapped = state->active_running;
        state->stopped_started = true;
        state->cv.notify_all();
        state->cv.wait(lk, [&] { return state->release_stopped; });
        state->stopped_completed = true;
    }

    template<class E>
    void set_error(E&&) && noexcept {}

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
};

struct delayed_scheduler_state {
    std::mutex mtx;
    std::condition_variable cv;
    bool release = false;
    bool completed = false;
};

struct delayed_sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<delayed_scheduler_state> state;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        std::shared_ptr<delayed_scheduler_state> state;
        R rcvr;

        void start() & noexcept {
            auto state_copy = state;
            auto rcvr_copy = std::make_shared<R>(std::move(rcvr));
            std::thread([state_copy, rcvr_copy = std::move(rcvr_copy)] mutable {
                {
                    std::unique_lock lk{state_copy->mtx};
                    state_copy->cv.wait(lk, [&] { return state_copy->release; });
                }

                std::execution::set_value(std::move(*rcvr_copy));

                {
                    std::lock_guard lk{state_copy->mtx};
                    state_copy->completed = true;
                }
                state_copy->cv.notify_all();
            }).detach();
        }
    };

    template<class R>
    auto connect(R rcvr) && -> op<R> {
        return op<R>{std::move(state), std::move(rcvr)};
    }
};

struct delayed_scheduler {
    using scheduler_concept = std::execution::scheduler_t;

    std::shared_ptr<delayed_scheduler_state> state;

    auto schedule() const noexcept -> delayed_sender {
        return delayed_sender{state};
    }

    friend bool operator==(const delayed_scheduler&, const delayed_scheduler&) noexcept = default;
};

} // namespace

static_assert(std::execution::scheduler<forge::strand::scheduler>);

TEST(StrandTest, FifoOrder) {
    forge::static_thread_pool pool{2};
    forge::strand strand{pool.get_scheduler()};
    auto scheduler = strand.get_scheduler();
    std::mutex mtx;
    std::vector<int> order;

    for (int i = 0; i < 8; ++i) {
        forge::start_detached(
            std::execution::schedule(scheduler)
            | std::execution::then([&, i] noexcept {
                std::lock_guard lk{mtx};
                order.push_back(i);
            }));
    }

    strand.wait();
    pool.wait();

    ASSERT_EQ(order.size(), 8u);
    EXPECT_TRUE(std::is_sorted(order.begin(), order.end()));
}

TEST(StrandTest, OptionsConstructorUsesCustomMemoryResourceForRecords) {
    forge_test::counting_resource resource;

    {
        forge::static_thread_pool pool{1};
        forge::strand strand{
            pool.get_scheduler(),
            forge::strand_options{.memory = &resource}};
        auto scheduler = strand.get_scheduler();

        auto result = std::execution::sync_wait(std::execution::schedule(scheduler));
        EXPECT_TRUE(result.has_value());

        strand.wait();
        pool.wait();
        EXPECT_GT(resource.allocations(), 0u);
    }

    EXPECT_EQ(resource.allocations(), resource.deallocations());
}

TEST(StrandTest, QueueAllocationFailureStopsWorkAndClosesStrand) {
    forge_test::fail_next_resource resource;
    forge::strand strand{
        std::execution::inline_scheduler{},
        forge::strand_options{.memory = &resource}};
    auto state = std::make_shared<stopped_state>();
    auto sender = std::execution::schedule(strand.get_scheduler());
    auto op = std::execution::connect(std::move(sender), stopped_receiver{state});

    resource.fail_next_allocation();
    std::execution::start(op);

    EXPECT_FALSE(state->value);
    EXPECT_TRUE(state->stopped);
    EXPECT_TRUE(strand.closed());
    strand.wait();
}

TEST(StrandTest, NoOverlapAcrossPoolThreads) {
    forge::static_thread_pool pool{4};
    forge::strand strand{pool.get_scheduler()};
    auto scheduler = strand.get_scheduler();
    std::atomic<int> active{0};
    std::atomic<int> max_active{0};
    std::atomic<int> completed{0};

    for (int i = 0; i < 16; ++i) {
        forge::start_detached(
            std::execution::schedule(scheduler)
            | std::execution::then([&] noexcept {
                int now = active.fetch_add(1, std::memory_order_acq_rel) + 1;
                int observed = max_active.load(std::memory_order_acquire);
                while (observed < now &&
                       !max_active.compare_exchange_weak(
                           observed, now, std::memory_order_acq_rel)) {}
                std::this_thread::sleep_for(1ms);
                active.fetch_sub(1, std::memory_order_acq_rel);
                completed.fetch_add(1, std::memory_order_acq_rel);
            }));
    }

    strand.wait();
    pool.wait();

    EXPECT_EQ(completed.load(std::memory_order_acquire), 16);
    EXPECT_EQ(max_active.load(std::memory_order_acquire), 1);
}

TEST(StrandTest, ReentrantSchedulingStaysSerialAndFifo) {
    forge::static_thread_pool pool{2};
    forge::strand strand{pool.get_scheduler()};
    auto scheduler = strand.get_scheduler();
    std::mutex mtx;
    std::vector<int> order;
    std::atomic<bool> second_submitted{false};

    forge::start_detached(
        std::execution::schedule(scheduler)
        | std::execution::then([&] noexcept {
            {
                std::lock_guard lk{mtx};
                order.push_back(1);
            }
            while (!second_submitted.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            forge::start_detached(
                std::execution::schedule(scheduler)
                | std::execution::then([&] noexcept {
                    std::lock_guard lk{mtx};
                    order.push_back(3);
                }));
        }));

    forge::start_detached(
        std::execution::schedule(scheduler)
        | std::execution::then([&] noexcept {
            std::lock_guard lk{mtx};
            order.push_back(2);
        }));
    second_submitted.store(true, std::memory_order_release);

    strand.wait();
    pool.wait();

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(StrandTest, WaitFromOwnCompletionDoesNotSelfDeadlock) {
    forge::static_thread_pool pool{1};
    forge::strand strand{pool.get_scheduler()};
    auto scheduler = strand.get_scheduler();
    std::promise<void> completed;

    forge::start_detached(
        std::execution::schedule(scheduler)
        | std::execution::then([&] noexcept {
            strand.wait();
            completed.set_value();
        }));

    EXPECT_EQ(completed.get_future().wait_for(2s), std::future_status::ready);

    strand.wait();
    pool.shutdown();
    pool.wait();
}

TEST(StrandTest, DestructorFromOwnCompletionDoesNotSelfDeadlock) {
    forge::static_thread_pool pool{1};
    auto strand = std::make_unique<forge::strand>(pool.get_scheduler());
    auto scheduler = strand->get_scheduler();
    std::promise<void> completed;

    forge::start_detached(
        std::execution::schedule(scheduler)
        | std::execution::then([&] noexcept {
            strand.reset();
            completed.set_value();
        }));

    EXPECT_EQ(completed.get_future().wait_for(2s), std::future_status::ready);

    pool.shutdown();
    pool.wait();
}

TEST(StrandTest, ShutdownStopsPendingAndFutureWork) {
    forge::static_thread_pool pool{1};
    forge::strand strand{pool.get_scheduler()};
    auto scheduler = strand.get_scheduler();

    std::mutex mtx;
    std::condition_variable cv;
    bool first_started = false;
    bool release_first = false;
    auto pending_state = std::make_shared<stopped_state>();

    forge::start_detached(
        std::execution::schedule(scheduler)
        | std::execution::then([&] noexcept {
            {
                std::lock_guard lk{mtx};
                first_started = true;
            }
            cv.notify_all();
            std::unique_lock lk{mtx};
            cv.wait(lk, [&] { return release_first; });
        }));

    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return first_started; }));
    }

    auto pending = std::execution::schedule(scheduler);
    auto pending_op = std::execution::connect(
        std::move(pending),
        stopped_receiver{pending_state});
    std::execution::start(pending_op);

    strand.shutdown();
    EXPECT_FALSE(pending_state->stopped);
    EXPECT_FALSE(pending_state->value);

    auto future_state = std::make_shared<stopped_state>();
    auto future = std::execution::schedule(scheduler);
    auto future_op = std::execution::connect(
        std::move(future),
        stopped_receiver{future_state});
    std::execution::start(future_op);
    EXPECT_TRUE(future_state->stopped);
    EXPECT_FALSE(future_state->value);

    {
        std::lock_guard lk{mtx};
        release_first = true;
    }
    cv.notify_all();
    strand.wait();
    pool.wait();

    EXPECT_TRUE(pending_state->stopped);
    EXPECT_FALSE(pending_state->value);
}

TEST(StrandTest, ShutdownSerializesStoppedCompletionsAndWaitsForThem) {
    forge::static_thread_pool pool{1};
    forge::strand strand{pool.get_scheduler()};
    auto scheduler = strand.get_scheduler();
    auto state = std::make_shared<blocking_stopped_state>();

    forge::start_detached(
        std::execution::schedule(scheduler)
        | std::execution::then([state] noexcept {
            std::unique_lock lk{state->mtx};
            state->active_started = true;
            state->active_running = true;
            state->cv.notify_all();
            state->cv.wait(lk, [&] { return state->release_active; });
            state->active_running = false;
        }));

    bool active_started = false;
    {
        std::unique_lock lk{state->mtx};
        active_started = state->cv.wait_for(lk, 2s, [&] {
            return state->active_started;
        });
    }
    if (!active_started) {
        {
            std::lock_guard lk{state->mtx};
            state->release_active = true;
            state->release_stopped = true;
        }
        state->cv.notify_all();
        strand.shutdown();
        strand.wait();
        pool.wait();
        FAIL() << "active strand completion did not start";
        return;
    }

    auto pending = std::execution::schedule(scheduler);
    auto pending_op = std::execution::connect(
        std::move(pending),
        blocking_stopped_receiver{state});
    std::execution::start(pending_op);

    strand.shutdown();
    {
        std::lock_guard lk{state->mtx};
        EXPECT_FALSE(state->stopped_started);
        state->release_active = true;
    }
    state->cv.notify_all();

    bool stopped_started = false;
    {
        std::unique_lock lk{state->mtx};
        stopped_started = state->cv.wait_for(lk, 2s, [&] {
            return state->stopped_started;
        });
    }
    if (!stopped_started) {
        {
            std::lock_guard lk{state->mtx};
            state->release_stopped = true;
        }
        state->cv.notify_all();
        strand.wait();
        pool.wait();
        FAIL() << "pending stopped completion did not start";
        return;
    }

    std::atomic<bool> wait_returned{false};
    std::thread waiter{[&] {
        strand.wait();
        wait_returned.store(true, std::memory_order_release);
    }};

    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(wait_returned.load(std::memory_order_acquire));

    {
        std::lock_guard lk{state->mtx};
        state->release_stopped = true;
    }
    state->cv.notify_all();
    waiter.join();
    pool.wait();

    EXPECT_TRUE(wait_returned.load(std::memory_order_acquire));
    EXPECT_FALSE(state->overlapped);
    EXPECT_TRUE(state->stopped_completed);
}

TEST(StrandTest, ShutdownBeforeLaunchedRunnerStartsDoesNotBlockWait) {
    auto delayed = std::make_shared<delayed_scheduler_state>();
    forge::strand strand{delayed_scheduler{delayed}};
    auto scheduler = strand.get_scheduler();
    auto pending_state = std::make_shared<stopped_state>();

    auto sender = std::execution::schedule(scheduler);
    auto op = std::execution::connect(
        std::move(sender),
        stopped_receiver{pending_state});
    std::execution::start(op);

    strand.shutdown();
    strand.wait();

    EXPECT_TRUE(pending_state->stopped);
    EXPECT_FALSE(pending_state->value);

    {
        std::lock_guard lk{delayed->mtx};
        delayed->release = true;
    }
    delayed->cv.notify_all();

    std::unique_lock lk{delayed->mtx};
    EXPECT_TRUE(delayed->cv.wait_for(lk, 2s, [&] {
        return delayed->completed;
    }));
}

TEST(StrandTest, CompletionSchedulerRoundtrip) {
    forge::static_thread_pool pool{1};
    forge::strand strand{pool.get_scheduler()};
    auto scheduler = strand.get_scheduler();
    auto sender = std::execution::schedule(scheduler);
    auto env = std::execution::get_env(sender);

    auto roundtrip =
        std::execution::get_completion_scheduler<std::execution::set_value_t>(env);

    EXPECT_TRUE(roundtrip == scheduler);
}

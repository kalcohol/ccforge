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

template<class Pred>
bool wait_until(Pred pred) {
    for (int i = 0; i < 200; ++i) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return false;
}

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

    forge::start_detached(
        std::execution::schedule(scheduler)
        | std::execution::then([&] noexcept {
            {
                std::lock_guard lk{mtx};
                order.push_back(1);
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
    EXPECT_TRUE(pending_state->stopped);
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

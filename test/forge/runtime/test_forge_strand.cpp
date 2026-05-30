#include <gtest/gtest.h>
#include <forge/strand.hpp>
#include <forge/static_thread_pool.hpp>
#include <execution>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
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

} // namespace

static_assert(std::execution::scheduler<forge::strand::scheduler>);

TEST(StrandTest, FifoOrder) {
    forge::static_thread_pool pool{2};
    forge::strand strand{pool.get_scheduler()};
    auto scheduler = strand.get_scheduler();
    std::mutex mtx;
    std::vector<int> order;

    for (int i = 0; i < 8; ++i) {
        std::execution::start_detached(
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

TEST(StrandTest, NoOverlapAcrossPoolThreads) {
    forge::static_thread_pool pool{4};
    forge::strand strand{pool.get_scheduler()};
    auto scheduler = strand.get_scheduler();
    std::atomic<int> active{0};
    std::atomic<int> max_active{0};
    std::atomic<int> completed{0};

    for (int i = 0; i < 16; ++i) {
        std::execution::start_detached(
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

    std::execution::start_detached(
        std::execution::schedule(scheduler)
        | std::execution::then([&] noexcept {
            {
                std::lock_guard lk{mtx};
                order.push_back(1);
            }
            std::execution::start_detached(
                std::execution::schedule(scheduler)
                | std::execution::then([&] noexcept {
                    std::lock_guard lk{mtx};
                    order.push_back(3);
                }));
        }));

    std::execution::start_detached(
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

TEST(StrandTest, ShutdownStopsPendingAndFutureWork) {
    forge::static_thread_pool pool{1};
    forge::strand strand{pool.get_scheduler()};
    auto scheduler = strand.get_scheduler();

    std::mutex mtx;
    std::condition_variable cv;
    bool first_started = false;
    bool release_first = false;
    auto pending_state = std::make_shared<stopped_state>();

    std::execution::start_detached(
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


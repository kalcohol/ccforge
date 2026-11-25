#include "test_thread_pool.hpp"

#include "../scheduler_fixture.hpp"
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <latch>
#include <memory>
#include <thread>

namespace {
namespace ex = std::execution;
using test_execution::test_thread_pool;

static_assert(ex::scheduler<test_thread_pool::scheduler>);

template<class F>
struct value_receiver {
    using receiver_concept = ex::receiver_tag;
    F callback;

    void set_value() && noexcept { callback(); }
    void set_stopped() && noexcept { ADD_FAILURE() << "unexpected stop"; }
};

struct stopped_receiver {
    using receiver_concept = ex::receiver_tag;
    std::thread::id submitter;
    std::atomic<bool>* stopped_inline;

    void set_value() && noexcept { ADD_FAILURE() << "unexpected value"; }
    void set_stopped() && noexcept {
        stopped_inline->store(std::this_thread::get_id() == submitter, std::memory_order_relaxed);
    }
};

struct destroying_receiver {
    using receiver_concept = ex::receiver_tag;
    void* owner;
    void (*destroy)(void*) noexcept;
    bool* completed;

    void set_value() && noexcept {
        *completed = true;
        destroy(owner);
        // Neither the receiver nor its operation exists after this call.
    }

    void set_stopped() && noexcept { ADD_FAILURE() << "unexpected stop"; }
};

} // namespace

TEST(ExecutionTestThreadPool, OneWorkerPreservesFifo) {
    test_thread_pool pool{1};
    const auto sender = ex::schedule(pool.get_scheduler());
    std::array<int, 3> order{};
    std::atomic<unsigned> next{0};
    const auto caller = std::this_thread::get_id();
    std::array<std::thread::id, 3> callback_threads{};
    std::latch first_running{1}, release_first{1};
    auto append = [&](int value) {
        return value_receiver{[&, value]() noexcept {
            const auto worker = std::this_thread::get_id();
            callback_threads[value - 1] = worker;
            if (value == 1) {
                first_running.count_down();
                // Avoid blocking start itself if the first callback runs inline.
                if (worker != caller) release_first.wait();
            }
            order[next.fetch_add(1, std::memory_order_relaxed)] = value;
        }};
    };
    auto first = ex::connect(sender, append(1));
    auto second = ex::connect(sender, append(2));
    auto third = ex::connect(sender, append(3));
    ex::start(first);
    first_running.wait();
    ex::start(second);
    ex::start(third);
    release_first.count_down();
    pool.join();
    EXPECT_EQ(order, (std::array{1, 2, 3}));
    EXPECT_NE(callback_threads[0], std::thread::id{});
    EXPECT_NE(callback_threads[0], caller);
    EXPECT_EQ(callback_threads[1], callback_threads[0]);
    EXPECT_EQ(callback_threads[2], callback_threads[0]);
}

TEST(ExecutionTestThreadPool, TwoWorkersOverlap) {
    test_thread_pool pool{2};
    const auto sender = ex::schedule(pool.get_scheduler());
    std::latch both_running{2};
    std::thread::id first_id, second_id;
    auto rendezvous = [&](std::thread::id& id) {
        return value_receiver{[&both_running, &id]() noexcept {
            id = std::this_thread::get_id();
            both_running.arrive_and_wait();
        }};
    };
    auto first = ex::connect(sender, rendezvous(first_id));
    auto second = ex::connect(sender, rendezvous(second_id));
    ex::start(first);
    ex::start(second);
    pool.join();
    EXPECT_NE(first_id, std::thread::id{});
    EXPECT_NE(second_id, std::thread::id{});
    EXPECT_NE(first_id, second_id);
}

TEST(ExecutionTestThreadPool, CompletesOnAdvertisedScheduler) {
    test_thread_pool pool{1};
    const auto scheduler = pool.get_scheduler();
    const auto sender = ex::schedule(scheduler);
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(ex::get_env(sender)), scheduler);
    EXPECT_TRUE(scheduler_fixture::agrees_with_schedule(scheduler));
    std::thread::id completed_on;
    auto op = ex::connect(sender, value_receiver{[&]() noexcept {
        completed_on = std::this_thread::get_id();
    }});
    ex::start(op);
    pool.join();
    EXPECT_NE(completed_on, std::thread::id{});
    EXPECT_NE(completed_on, std::this_thread::get_id());
}

TEST(ExecutionTestThreadPool, ReceiverCanDestroyOperationDuringCompletion) {
    test_thread_pool pool{1};
    auto sender = ex::schedule(pool.get_scheduler());
    using op_t = ex::connect_result_t<decltype(sender), destroying_receiver>;
    std::unique_ptr<op_t> owner;
    bool completed = false;
    owner.reset(new op_t(ex::connect(sender, destroying_receiver{
        &owner,
        [](void* ptr) noexcept { static_cast<std::unique_ptr<op_t>*>(ptr)->reset(); },
        &completed})));
    ex::start(*owner);
    pool.join();
    EXPECT_TRUE(completed);
    EXPECT_EQ(owner, nullptr);
}

TEST(ExecutionTestThreadPool, StartAfterCloseStopsInline) {
    test_thread_pool pool{1};
    const auto scheduler = pool.get_scheduler();
    const auto sender = ex::schedule(scheduler);
    const auto attrs = ex::get_env(sender);
    const auto caller_scheduler = test_thread_pool::scheduler{nullptr};
    const auto pool_env = ex::prop{ex::get_scheduler, scheduler};
    const auto stopped_scheduler = ex::get_completion_scheduler<ex::set_stopped_t>(attrs, ex::env<>{});
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_stopped_t>(attrs), caller_scheduler);
    EXPECT_EQ(stopped_scheduler, caller_scheduler);
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_stopped_t>(attrs, pool_env), caller_scheduler);
    EXPECT_TRUE(scheduler_fixture::agrees_with_schedule(scheduler, ex::env<>{}));

    const auto submitter = std::this_thread::get_id();
    std::atomic<bool> value_inline{false};
    auto inline_op = ex::connect(ex::schedule(stopped_scheduler), value_receiver{[&]() noexcept {
        value_inline.store(std::this_thread::get_id() == submitter, std::memory_order_relaxed);
    }});
    std::atomic<bool> stopped_inline{false};
    auto op = ex::connect(sender, stopped_receiver{submitter, &stopped_inline});
    pool.close();
    ex::start(inline_op);
    ex::start(op);
    EXPECT_TRUE(value_inline.load(std::memory_order_relaxed));
    EXPECT_TRUE(stopped_inline.load(std::memory_order_relaxed));
    pool.join();
}

TEST(ExecutionTestThreadPool, CloseDrainsPendingWorkBeforeJoinReturns) {
    test_thread_pool pool{1};
    const auto sender = ex::schedule(pool.get_scheduler());
    std::latch entered{1}, release{1};
    std::atomic<bool> pending_ran{false};
    auto blocker = ex::connect(sender, value_receiver{[&]() noexcept {
        entered.count_down();
        release.wait();
    }});
    auto pending = ex::connect(sender, value_receiver{[&]() noexcept {
        pending_ran.store(true, std::memory_order_relaxed);
    }});
    ex::start(blocker);
    entered.wait();
    ex::start(pending);
    pool.close();
    EXPECT_FALSE(pending_ran.load(std::memory_order_relaxed));
    release.count_down();
    pool.join();
    EXPECT_TRUE(pending_ran.load(std::memory_order_relaxed));
}

TEST(ExecutionTestThreadPool, DestructorClosesAndJoins) {
    auto pool = std::make_unique<test_thread_pool>(1);
    bool completed = false;
    auto op = ex::connect(ex::schedule(pool->get_scheduler()), value_receiver{[&]() noexcept {
        completed = true;
    }});
    ex::start(op);
    pool.reset();
    EXPECT_TRUE(completed);
}

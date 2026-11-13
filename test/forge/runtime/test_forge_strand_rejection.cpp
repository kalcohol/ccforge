#include <forge/execution_extensions.hpp>
#include <gtest/gtest.h>
#include <forge/static_thread_pool.hpp>
#include <forge/strand.hpp>
#include "forge_counting_resource.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <execution>
#include <future>
#include <latch>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

struct results {
    std::mutex mutex;
    std::vector<int> values;
    std::vector<int> stopped;
    std::vector<int> order;
    std::atomic<int> active{0};
    std::atomic<bool> overlap{false};
};

struct receiver {
    using receiver_concept = std::execution::receiver_t;
    results* output;
    int id;

    void complete(bool stopped) noexcept {
        if (output->active.fetch_add(1) != 0) output->overlap = true;
        {
            std::lock_guard lock{output->mutex};
            (stopped ? output->stopped : output->values).push_back(id);
            output->order.push_back(id);
        }
        output->active.fetch_sub(1);
    }
    void set_value() && noexcept { complete(false); }
    void set_stopped() && noexcept { complete(true); }
    auto get_env() const noexcept -> forge::empty_env { return {}; }
};

void submit(forge::strand::scheduler scheduler, results& output, int id) {
    auto op = std::execution::connect(
        std::execution::schedule(scheduler), receiver{&output, id});
    std::execution::start(op);
}

void submit(forge::strand& strand, results& output, int id) {
    submit(strand.get_scheduler(), output, id);
}

// Start a fixture with one blocked worker and exactly one queued task.
// Cleanup always releases the worker, even after a failed expectation.
struct saturated_pool {
    std::latch entered{1};
    std::latch release{1};
    std::atomic<bool> released{false};
    forge::static_thread_pool pool{forge::static_thread_pool_options{
        .thread_count = 1, .queue_capacity = 1}};

    saturated_pool() {
        EXPECT_TRUE(pool.__submit([this] { entered.count_down(); release.wait(); }));
        entered.wait();
        EXPECT_TRUE(pool.__submit([] {}));
    }
    void release_worker() {
        if (!released.exchange(true)) release.count_down();
    }
    ~saturated_pool() {
        release_worker();
        pool.wait();
    }
};

// Arm only after strand work is parked: its next allocation is the runner
// relaunched by a capacity notification. No production test hooks are needed.
class allocation_gate_resource final : public std::pmr::memory_resource {
public:
    void arm() noexcept { armed_.store(true); }
    bool wait_for_allocation() {
        return entered_future_.wait_for(std::chrono::seconds{2}) ==
            std::future_status::ready;
    }
    void release() noexcept {
        if (!released_.exchange(true)) release_.count_down();
    }
    auto outstanding() const noexcept -> std::size_t { return upstream_.outstanding(); }

private:
    auto do_allocate(std::size_t bytes, std::size_t alignment) -> void* override {
        if (armed_.exchange(false)) {
            entered_.set_value();
            release_.wait();
        }
        return upstream_.allocate(bytes, alignment);
    }
    void do_deallocate(void* ptr, std::size_t bytes, std::size_t alignment) override {
        upstream_.deallocate(ptr, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    forge_test::counting_resource upstream_;
    std::atomic<bool> armed_{false};
    std::atomic<bool> released_{false};
    std::promise<void> entered_;
    std::future<void> entered_future_ = entered_.get_future();
    std::latch release_{1};
};

// The second blocking task starts only after the first notification returns.
// It keeps the worker from popping the competitor until re-parking is checked.
struct retrying_pool {
    allocation_gate_resource& memory;
    std::latch first_entered{1}, first_release{1}, second_release{1};
    std::promise<void> second_entered;
    std::future<void> second_future = second_entered.get_future();
    std::atomic<bool> first_released{false}, second_released{false};
    forge::static_thread_pool pool{forge::static_thread_pool_options{
        .thread_count = 1, .queue_capacity = 1}};

    explicit retrying_pool(allocation_gate_resource& memory) : memory(memory) {
        EXPECT_TRUE(pool.__submit([this] {
            first_entered.count_down();
            first_release.wait();
        }));
        first_entered.wait();
        EXPECT_TRUE(pool.__submit([this] {
            second_entered.set_value();
            second_release.wait();
        }));
    }
    bool notify_and_refill() {
        memory.arm();
        if (!first_released.exchange(true)) first_release.count_down();
        if (!memory.wait_for_allocation()) return false;
        bool accepted = false;
        std::thread competitor{[&] { accepted = pool.__submit([] {}); }};
        competitor.join();
        return accepted;
    }
    bool finish_retry() {
        memory.release();
        return second_future.wait_for(std::chrono::seconds{2}) ==
            std::future_status::ready;
    }
    auto parked_node() -> forge::__pool_detail::__capacity_waiter* {
        auto state = pool.get_scheduler().__capacity_state();
        std::lock_guard lock{state->__mtx_};
        EXPECT_EQ(state->__waiters_head_, state->__waiters_tail_);
        return state->__waiters_head_;
    }
    void release_worker() {
        if (!second_released.exchange(true)) second_release.count_down();
    }
    ~retrying_pool() {
        memory.release();
        if (!first_released.exchange(true)) first_release.count_down();
        release_worker();
        pool.wait();
    }
};

void expect_prompt_wait(forge::strand& strand) {
    auto waiter = std::async(std::launch::async, [&] { strand.wait(); });
    const auto status = waiter.wait_for(std::chrono::seconds{2});
    EXPECT_EQ(status, std::future_status::ready);
    // Make a missing retry a test failure, without hanging cleanup.
    if (status != std::future_status::ready) strand.shutdown();
    waiter.wait();
}

struct rejection_state {
    std::atomic<int> attempts{0};
    std::atomic<int> reject_count{0};
    std::atomic<bool> closed{false};
};

struct rejecting_sender {
    using sender_concept = std::execution::sender_t;
    std::shared_ptr<rejection_state> state;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(), std::execution::set_stopped_t()> { return {}; }
    auto get_env() const noexcept -> forge::empty_env { return {}; }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;
        std::shared_ptr<rejection_state> state;
        R rcvr;
        void start() & noexcept {
            // Do not touch the operation after completion can destroy it.
            auto keepalive = state;
            const auto attempt = keepalive->attempts.fetch_add(1);
            if (keepalive->closed || attempt < keepalive->reject_count.load()) {
                std::execution::set_stopped(std::move(rcvr));
            } else {
                std::execution::set_value(std::move(rcvr));
            }
        }
    };
    template<class R>
    auto connect(R rcvr) && -> op<R> { return {std::move(state), std::move(rcvr)}; }
};

template<bool HasQuery = true>
struct rejecting_scheduler {
    using scheduler_concept = std::execution::scheduler_t;
    std::shared_ptr<rejection_state> state;
    bool closed() const noexcept requires HasQuery { return state->closed; }
    auto schedule() const noexcept -> rejecting_sender { return {state}; }
    friend bool operator==(const rejecting_scheduler&, const rejecting_scheduler&) noexcept = default;
};

TEST(StrandRejectionTest, SchedulerWithClosureQueryStillFailsClosed) {
    auto state = std::make_shared<rejection_state>();
    state->reject_count = 2;
    forge::strand strand{rejecting_scheduler<>{state}};
    results output;
    submit(strand, output, 0);
    submit(strand, output, 1);
    strand.wait();
    EXPECT_TRUE(strand.closed());
    EXPECT_EQ(state->attempts.load(), 1);
    EXPECT_TRUE(output.values.empty());
    EXPECT_EQ(output.stopped, (std::vector<int>{0, 1}));
}

TEST(StrandRejectionTest, UnknownSchedulerStillFailsClosed) {
    auto state = std::make_shared<rejection_state>();
    state->reject_count = 1;
    forge::strand strand{rejecting_scheduler<false>{state}};
    results output;
    submit(strand, output, 0);
    submit(strand, output, 1);
    strand.wait();
    EXPECT_TRUE(strand.closed());
    EXPECT_EQ(state->attempts.load(), 1);
    EXPECT_EQ(output.stopped, (std::vector<int>{0, 1}));
}

TEST(StrandRejectionTest, ErasedPoolStillFailsClosed) {
    saturated_pool blocked;
    forge::strand strand{forge::any_scheduler{blocked.pool.get_scheduler()}};
    results output;
    submit(strand, output, 0);
    strand.wait();
    EXPECT_TRUE(strand.closed());
    EXPECT_EQ(output.stopped, (std::vector<int>{0}));
}

TEST(StrandRejectionTest, CapacityOneRecoversWithoutFurtherSubmissionInFifoOrder) {
    saturated_pool blocked;
    forge::strand strand{blocked.pool.get_scheduler()};
    results output;
    for (int id = 0; id < 8; ++id) submit(strand, output, id);
    EXPECT_FALSE(blocked.pool.closed());
    EXPECT_FALSE(blocked.pool.get_scheduler().closed());
    EXPECT_FALSE(strand.closed());
    EXPECT_TRUE(output.values.empty());
    EXPECT_TRUE(output.stopped.empty());
    auto waiter = std::async(std::launch::async, [&] { strand.wait(); });
    EXPECT_EQ(waiter.wait_for(std::chrono::milliseconds{20}), std::future_status::timeout);
    blocked.release_worker();
    const auto status = waiter.wait_for(std::chrono::seconds{2});
    EXPECT_EQ(status, std::future_status::ready);
    if (status != std::future_status::ready) strand.shutdown();
    waiter.wait();
    blocked.pool.wait();
    EXPECT_FALSE(strand.closed());
    EXPECT_FALSE(output.overlap);
    EXPECT_EQ(output.values, (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7}));
    EXPECT_TRUE(output.stopped.empty());
}

TEST(StrandRejectionTest, RunnerTailRejectionRecoversWithoutFurtherSubmission) {
    forge::static_thread_pool pool{forge::static_thread_pool_options{
        .thread_count = 1, .queue_capacity = 1}};
    forge::strand strand{pool.get_scheduler()};
    results output;
    std::latch entered{1}, release{1};
    auto first = std::execution::connect(
        std::execution::schedule(strand.get_scheduler())
        | std::execution::then([&] noexcept { entered.count_down(); release.wait(); }),
        receiver{&output, 0});
    std::execution::start(first);
    entered.wait();
    submit(strand, output, 1);
    submit(strand, output, 2);
    EXPECT_TRUE(pool.__submit([] {}));
    release.count_down();
    expect_prompt_wait(strand);
    pool.wait();
    EXPECT_FALSE(strand.closed());
    EXPECT_EQ(output.values, (std::vector<int>{0, 1, 2}));
    EXPECT_TRUE(output.stopped.empty());
}

TEST(StrandRejectionTest, PoolShutdownStopsParkedWorkWithoutWorkerReleaseOrSubmission) {
    saturated_pool blocked;
    forge::strand strand{blocked.pool.get_scheduler()};
    results output;
    for (int id = 0; id < 8; ++id) submit(strand, output, id);
    EXPECT_FALSE(strand.closed());
    blocked.pool.shutdown();
    EXPECT_TRUE(blocked.pool.closed());
    EXPECT_TRUE(blocked.pool.get_scheduler().closed());
    expect_prompt_wait(strand);
    EXPECT_TRUE(strand.closed());
    EXPECT_TRUE(output.values.empty());
    EXPECT_EQ(output.stopped, (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7}));
}

TEST(StrandRejectionTest, CapacityZeroWaitsForShutdownWithoutSpinning) {
    forge::static_thread_pool pool{forge::static_thread_pool_options{
        .thread_count = 1, .queue_capacity = 0}};
    forge::strand strand{pool.get_scheduler()};
    results output;
    submit(strand, output, 0);
    EXPECT_FALSE(strand.closed());
    EXPECT_TRUE(output.values.empty());
    pool.shutdown();
    expect_prompt_wait(strand);
    EXPECT_TRUE(strand.closed());
    EXPECT_EQ(output.stopped, (std::vector<int>{0}));
}

TEST(StrandRejectionTest, ExplicitShutdownDeregistersParkedWork) {
    saturated_pool blocked;
    forge::strand strand{blocked.pool.get_scheduler()};
    results output;
    submit(strand, output, 0);
    strand.shutdown();
    expect_prompt_wait(strand);
    blocked.release_worker();
    blocked.pool.wait();
    EXPECT_TRUE(output.values.empty());
    EXPECT_EQ(output.stopped, (std::vector<int>{0}));
}

TEST(StrandRejectionTest, DestructionDeregistersNodeAndReleasesPmrKeepalive) {
    forge_test::counting_resource memory;
    saturated_pool blocked;
    results output;
    {
        forge::strand strand{blocked.pool.get_scheduler(),
            forge::strand_options{.memory = &memory}};
        submit(strand, output, 0);
        EXPECT_FALSE(strand.closed());
    }
    // Neither queue capacity nor a pool shutdown was needed for destruction.
    EXPECT_EQ(memory.outstanding(), 0u);
    blocked.release_worker();
    blocked.pool.wait();
    blocked.pool.shutdown();
    EXPECT_TRUE(output.values.empty());
    EXPECT_EQ(output.stopped, (std::vector<int>{0}));
}

TEST(StrandRejectionTest, PoolAllocationFailureStillFailsClosed) {
    forge_test::fail_next_resource memory;
    forge::static_thread_pool pool{forge::static_thread_pool_options{
        .thread_count = 1, .queue_capacity = 1, .memory = &memory}};
    forge::strand strand{pool.get_scheduler()};
    results output;
    memory.fail_next_allocation();
    submit(strand, output, 0);
    expect_prompt_wait(strand);
    EXPECT_TRUE(strand.closed());
    EXPECT_EQ(output.stopped, (std::vector<int>{0}));
}

TEST(StrandRejectionTest, LateCapacityRegistrationNotifiesOnceOutsidePoolMutex) {
    forge::static_thread_pool pool{forge::static_thread_pool_options{
        .thread_count = 1, .queue_capacity = 1}};
    // Model capacity becoming available before the rejected submitter parks:
    // the worker is already idle, so there will be no later queue pop.
    pool.wait();
    struct probe {
        forge::__pool_detail::__capacity_waiter node;
        std::shared_ptr<forge::__pool_detail::__pool_state> pool;
        std::promise<void> done;
        std::atomic<int> calls{0};
        bool closed = true;
    };
    auto state = std::make_shared<probe>();
    state->pool = pool.get_scheduler().__capacity_state();
    state->node.notify = [](std::shared_ptr<void> keepalive, bool closed) noexcept {
        auto state = std::static_pointer_cast<probe>(std::move(keepalive));
        // Reenter the pool mutex to verify dispatch dropped it first.
        state->closed = closed || state->pool->__closed();
        if (state->calls.fetch_add(1) == 0) state->done.set_value();
    };
    auto done = state->done.get_future();
    ASSERT_TRUE(state->pool->__park(state->node, state));
    const auto status = done.wait_for(std::chrono::seconds{2});
    EXPECT_EQ(status, std::future_status::ready);
    pool.shutdown();
    pool.wait();
    if (status == std::future_status::ready) EXPECT_FALSE(state->closed);
    EXPECT_EQ(state->calls.load(), 1);
    EXPECT_FALSE(state->pool->__cancel(state->node));
}

TEST(StrandRejectionTest, ShutdownNotifiesAllWaitersAfterMiddleDeregistration) {
    saturated_pool blocked;
    results output;
    std::array<std::unique_ptr<forge::strand>, 3> strands;
    for (int i = 0; i < 3; ++i) {
        strands[i] = std::make_unique<forge::strand>(blocked.pool.get_scheduler());
        submit(*strands[i], output, i);
    }
    strands[1].reset();
    blocked.pool.shutdown();
    expect_prompt_wait(*strands[0]);
    expect_prompt_wait(*strands[2]);
    EXPECT_TRUE(output.values.empty());
    EXPECT_EQ(output.stopped, (std::vector<int>{1, 0, 2}));
}

TEST(StrandRejectionTest, ReentrantPoolShutdownHasBoundedCompletionDepth) {
    results output;
    saturated_pool blocked;
    int depth = 0, maximum_depth = 0;
    struct shutdown_receiver {
        using receiver_concept = std::execution::receiver_t;
        forge::static_thread_pool* pool;
        receiver result;
        int* depth;
        int* maximum_depth;
        void set_value() && noexcept { result.complete(false); }
        void set_stopped() && noexcept {
            ++*depth;
            if (*depth > *maximum_depth) *maximum_depth = *depth;
            result.complete(true);
            pool->shutdown();
            --*depth;
        }
        auto get_env() const noexcept -> forge::empty_env { return {}; }
    };
    constexpr int count = 512;
    std::vector<std::unique_ptr<forge::strand>> strands;
    strands.reserve(count);
    for (int id = 0; id < count; ++id) {
        auto& strand = strands.emplace_back(
            std::make_unique<forge::strand>(blocked.pool.get_scheduler()));
        auto op = std::execution::connect(
            std::execution::schedule(strand->get_scheduler()),
            shutdown_receiver{&blocked.pool, receiver{&output, id}, &depth, &maximum_depth});
        std::execution::start(op);
    }
    // Keep the worker blocked: every callback runs on this shutdown caller,
    // and the measured nesting is independent of worker scheduling.
    blocked.pool.shutdown();
    blocked.pool.shutdown();
    EXPECT_EQ(depth, 0);
    EXPECT_EQ(maximum_depth, 1);
    EXPECT_TRUE(output.values.empty());
    ASSERT_EQ(output.stopped.size(), static_cast<std::size_t>(count));
    for (int id = 0; id < count; ++id) {
        EXPECT_EQ(output.stopped[id], id);
        strands[id]->wait();
    }
}

TEST(StrandRejectionTest, RejectionAfterCapacityNotificationReregistersAndRecoversInFifoOrder) {
    allocation_gate_resource memory;
    results output;
    {
        retrying_pool blocked{memory};
        forge::strand strand{blocked.pool.get_scheduler(),
            forge::strand_options{.memory = &memory}};
        for (int id = 0; id < 8; ++id) submit(strand, output, id);
        auto* first_registration = blocked.parked_node();
        ASSERT_NE(first_registration, nullptr);
        ASSERT_TRUE(blocked.notify_and_refill());
        // Notification extracted the original registration, but the competing
        // submitter now owns the only queue slot before runner submission.
        EXPECT_EQ(blocked.parked_node(), nullptr);
        ASSERT_TRUE(blocked.finish_retry());
        EXPECT_EQ(blocked.parked_node(), first_registration);
        EXPECT_FALSE(strand.closed());
        EXPECT_TRUE(output.order.empty());
        blocked.release_worker();
        expect_prompt_wait(strand);
        blocked.pool.wait();
        EXPECT_EQ(blocked.parked_node(), nullptr);
        EXPECT_FALSE(strand.closed());
        EXPECT_FALSE(output.overlap);
        EXPECT_EQ(output.values, (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7}));
        EXPECT_TRUE(output.stopped.empty());
    }
    EXPECT_EQ(memory.outstanding(), 0u);
}

TEST(StrandRejectionTest, ReregistrationShutdownAndDestructionRaceStress) {
    for (int round = 0; round < 80; ++round) {
        SCOPED_TRACE(round);
        allocation_gate_resource memory;
        results output;
        std::weak_ptr<forge::__strand_detail::__state> state;
        {
            retrying_pool blocked{memory};
            auto strand = std::make_unique<forge::strand>(blocked.pool.get_scheduler(),
                forge::strand_options{.memory = &memory});
            state = strand->get_scheduler().state;
            for (int id = 0; id < 8; ++id) submit(*strand, output, id);
            auto* first_registration = blocked.parked_node();
            ASSERT_NE(first_registration, nullptr);
            ASSERT_TRUE(blocked.notify_and_refill());
            EXPECT_EQ(blocked.parked_node(), nullptr);
            if (round % 2 == 0) {
                // Guarantee a completed re-registration before racing its
                // extraction against pool closure and strand cancellation.
                ASSERT_TRUE(blocked.finish_retry());
                EXPECT_EQ(blocked.parked_node(), first_registration);
            }
            std::latch go{1};
            std::thread capacity{[&] {
                go.wait();
                if (round % 2 == 0) blocked.release_worker();
                // Odd rounds race the rejected relaunch and its attempted
                // re-registration directly against shutdown/destruction.
                else memory.release();
            }};
            std::thread shutdown{[&] { go.wait(); blocked.pool.shutdown(); }};
            std::thread destroy{[&] { go.wait(); strand.reset(); }};
            go.count_down();
            capacity.join();
            shutdown.join();
            destroy.join();
            blocked.release_worker();
            blocked.pool.wait();
            EXPECT_EQ(blocked.parked_node(), nullptr);
        }
        // The pool has joined, covering an already extracted callback's tail.
        EXPECT_TRUE(state.expired());
        state.reset(); // Release the allocate_shared control block as well.
        EXPECT_EQ(memory.outstanding(), 0u);
        EXPECT_FALSE(output.overlap);
        EXPECT_EQ(output.order, (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7}));
        EXPECT_EQ(output.values.size() + output.stopped.size(), 8u);
        if (round % 2 != 0) EXPECT_TRUE(output.values.empty());
    }
}

TEST(StrandRejectionTest, ConcurrentEnqueueAndCapacityRecoveryPreservesFifo) {
    for (int round = 0; round < 20; ++round) {
        saturated_pool blocked;
        forge::strand strand{blocked.pool.get_scheduler()};
        results output;
        std::array<std::thread, 4> producers;
        std::latch go{1};
        for (int p = 0; p < 4; ++p) {
            producers[p] = std::thread{[&, p] {
                go.wait();
                for (int i = 0; i < 128; ++i) submit(strand, output, p * 128 + i);
            }};
        }
        go.count_down();
        blocked.release_worker();
        for (auto& producer : producers) producer.join();
        expect_prompt_wait(strand);
        blocked.pool.wait();
        EXPECT_FALSE(strand.closed());
        EXPECT_FALSE(output.overlap);
        EXPECT_TRUE(output.stopped.empty());
        ASSERT_EQ(output.values.size(), 512u);
        std::array<int, 4> next{};
        for (int id : output.values) {
            ASSERT_GE(id, 0);
            ASSERT_LT(id, 512);
            EXPECT_EQ(id % 128, next[id / 128]++);
        }
        EXPECT_EQ(next, (std::array<int, 4>{128, 128, 128, 128}));
    }
}

TEST(StrandRejectionTest, ParkingCapacityShutdownAndDestructionRaceStress) {
    forge_test::counting_resource memory;
    for (int round = 0; round < 80; ++round) {
        saturated_pool blocked;
        results output;
        auto strand = std::make_unique<forge::strand>(blocked.pool.get_scheduler(),
            forge::strand_options{.memory = &memory});
        auto scheduler = strand->get_scheduler();
        // Alternate a known parked node with first parking racing shutdown.
        if (round % 2 == 0) submit(scheduler, output, 256);
        std::latch go{1};
        std::array<std::thread, 4> producers;
        for (int p = 0; p < 4; ++p) {
            producers[p] = std::thread{[&, p, scheduler] {
                go.wait();
                for (int i = 0; i < 64; ++i) submit(scheduler, output, p * 64 + i);
            }};
        }
        std::thread capacity{[&] { go.wait(); blocked.release_worker(); }};
        std::thread shutdown{[&] { go.wait(); blocked.pool.shutdown(); }};
        std::thread destroy{[&] { go.wait(); strand.reset(); }};
        go.count_down();
        for (auto& producer : producers) producer.join();
        capacity.join();
        shutdown.join();
        destroy.join();
        scheduler = {};
        blocked.pool.wait();
        EXPECT_FALSE(output.overlap);
        ASSERT_EQ(output.order.size(), round % 2 == 0 ? 257u : 256u);
        std::array<int, 4> next{};
        int initial = 0;
        for (int id : output.order) {
            if (id == 256) { ++initial; continue; }
            ASSERT_GE(id, 0);
            ASSERT_LT(id, 256);
            EXPECT_EQ(id % 64, next[id / 64]++);
        }
        EXPECT_EQ(initial, round % 2 == 0 ? 1 : 0);
        EXPECT_EQ(next, (std::array<int, 4>{64, 64, 64, 64}));
    }
    // Pool joins above also cover callbacks whose node was already extracted.
    EXPECT_EQ(memory.outstanding(), 0u);
}

} // namespace

#include <gtest/gtest.h>
#include <forge/async_scope.hpp>
#include <forge/channel.hpp>
#include <forge/start_detached.hpp>
#include <forge/static_thread_pool.hpp>
#include <forge/strand.hpp>
#include <forge/timer_context.hpp>
#include <execution>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <tuple>
#include <vector>

namespace {

using namespace std::chrono_literals;

void spin_until_go(std::atomic<int>& ready, std::atomic<bool>& go) noexcept {
    ready.fetch_add(1, std::memory_order_acq_rel);
    while (!go.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
}

void perturb(int iteration, int lane) noexcept {
    const int yields = (iteration * 19 + lane * 7) % 5;
    for (int i = 0; i < yields; ++i) {
        std::this_thread::yield();
    }
}

struct stop_env {
    std::inplace_stop_source* source = nullptr;

    friend auto tag_invoke(
        std::execution::get_stop_token_t,
        const stop_env& self) noexcept -> std::inplace_stop_token {
        return self.source->get_token();
    }
};

struct recv_race_state {
    std::atomic<int> value{0};
    std::atomic<int> stopped{0};
};

struct recv_race_receiver {
    using receiver_concept = std::execution::receiver_t;

    recv_race_state* state = nullptr;
    std::inplace_stop_source* source = nullptr;

    void set_value(int) && noexcept {
        state->value.fetch_add(1, std::memory_order_acq_rel);
    }

    void set_stopped() && noexcept {
        state->stopped.fetch_add(1, std::memory_order_acq_rel);
    }

    template<class E>
    void set_error(E&&) && noexcept {}

    auto get_env() const noexcept -> stop_env {
        return stop_env{source};
    }
};

struct send_race_state {
    std::atomic<int> value{0};
    std::atomic<int> stopped{0};
};

struct send_race_receiver {
    using receiver_concept = std::execution::receiver_t;

    send_race_state* state = nullptr;
    std::inplace_stop_source* source = nullptr;

    void set_value() && noexcept {
        state->value.fetch_add(1, std::memory_order_acq_rel);
    }

    void set_stopped() && noexcept {
        state->stopped.fetch_add(1, std::memory_order_acq_rel);
    }

    template<class E>
    void set_error(E&&) && noexcept {}

    auto get_env() const noexcept -> stop_env {
        return stop_env{source};
    }
};

struct timer_race_state {
    std::mutex mtx;
    std::condition_variable cv;
    int value = 0;
    int stopped = 0;

    [[nodiscard]] bool done() const noexcept {
        return value + stopped == 1;
    }
};

struct timer_race_receiver {
    using receiver_concept = std::execution::receiver_t;

    timer_race_state* state = nullptr;
    std::inplace_stop_source* source = nullptr;

    void set_value() && noexcept {
        {
            std::lock_guard lk{state->mtx};
            ++state->value;
            state->cv.notify_all();
        }
    }

    void set_stopped() && noexcept {
        {
            std::lock_guard lk{state->mtx};
            ++state->stopped;
            state->cv.notify_all();
        }
    }

    void set_error(std::exception_ptr) && noexcept {
        {
            std::lock_guard lk{state->mtx};
            ++state->stopped;
            state->cv.notify_all();
        }
    }

    auto get_env() const noexcept -> stop_env {
        return stop_env{source};
    }
};

bool wait_timer_done(timer_race_state& state) {
    std::unique_lock lk{state.mtx};
    return state.cv.wait_for(lk, 2s, [&] { return state.done(); });
}

} // namespace

TEST(StrandStressTest, ConcurrentSchedulePreservesSerialCompletion) {
    constexpr int kIterations = 32;
    constexpr int kProducers = 4;
    constexpr int kTasksPerProducer = 8;

    for (int iteration = 0; iteration < kIterations; ++iteration) {
        forge::static_thread_pool pool{4};
        forge::strand strand{pool.get_scheduler()};
        auto scheduler = strand.get_scheduler();

        std::atomic<int> active{0};
        std::atomic<int> max_active{0};
        std::atomic<int> completed{0};
        std::atomic<int> ready{0};
        std::atomic<bool> go{false};
        std::vector<std::thread> producers;

        for (int p = 0; p < kProducers; ++p) {
            producers.emplace_back([&, p] {
                spin_until_go(ready, go);
                perturb(iteration, p);
                for (int i = 0; i < kTasksPerProducer; ++i) {
                    const int task_index = i;
                    forge::start_detached(
                        std::execution::schedule(scheduler) |
                        std::execution::then([&, task_index] noexcept {
                            const int now =
                                active.fetch_add(1, std::memory_order_acq_rel) + 1;
                            int observed = max_active.load(std::memory_order_acquire);
                            while (observed < now &&
                                   !max_active.compare_exchange_weak(
                                       observed,
                                       now,
                                       std::memory_order_acq_rel)) {}
                            perturb(iteration, task_index);
                            active.fetch_sub(1, std::memory_order_acq_rel);
                            completed.fetch_add(1, std::memory_order_acq_rel);
                        }));
                }
            });
        }

        while (ready.load(std::memory_order_acquire) != kProducers) {
            std::this_thread::yield();
        }
        go.store(true, std::memory_order_release);

        for (auto& producer : producers) {
            producer.join();
        }
        strand.wait();
        pool.wait();

        EXPECT_EQ(completed.load(std::memory_order_acquire), kProducers * kTasksPerProducer);
        EXPECT_EQ(max_active.load(std::memory_order_acquire), 1);
    }
}

TEST(ChannelStressTest, PendingRecvStopRacesDirectSend) {
    constexpr int kIterations = 256;

    for (int iteration = 0; iteration < kIterations; ++iteration) {
        forge::bounded_channel<int> channel{0};
        std::inplace_stop_source source;
        recv_race_state state;

        auto recv = channel.async_recv();
        auto op = std::execution::connect(
            std::move(recv),
            recv_race_receiver{&state, &source});
        std::execution::start(op);

        std::atomic<int> ready{0};
        std::atomic<bool> go{false};
        std::atomic<bool> sent{false};

        std::thread stopper{[&] {
            spin_until_go(ready, go);
            perturb(iteration, 0);
            source.request_stop();
        }};
        std::thread sender{[&] {
            spin_until_go(ready, go);
            perturb(iteration, 1);
            sent.store(channel.try_send(7), std::memory_order_release);
        }};

        while (ready.load(std::memory_order_acquire) != 2) {
            std::this_thread::yield();
        }
        go.store(true, std::memory_order_release);

        stopper.join();
        sender.join();

        const int value = state.value.load(std::memory_order_acquire);
        const int stopped = state.stopped.load(std::memory_order_acquire);
        EXPECT_EQ(value + stopped, 1);
        if (sent.load(std::memory_order_acquire)) {
            EXPECT_EQ(value, 1);
        }
    }
}

TEST(ChannelStressTest, PendingSendStopRacesDirectRecv) {
    constexpr int kIterations = 256;

    for (int iteration = 0; iteration < kIterations; ++iteration) {
        forge::bounded_channel<int> channel{0};
        std::inplace_stop_source source;
        send_race_state state;

        auto send = channel.async_send(7);
        auto op = std::execution::connect(
            std::move(send),
            send_race_receiver{&state, &source});
        std::execution::start(op);

        std::atomic<int> ready{0};
        std::atomic<bool> go{false};
        std::optional<int> received;

        std::thread stopper{[&] {
            spin_until_go(ready, go);
            perturb(iteration, 0);
            source.request_stop();
        }};
        std::thread receiver{[&] {
            spin_until_go(ready, go);
            perturb(iteration, 1);
            received = channel.try_recv();
        }};

        while (ready.load(std::memory_order_acquire) != 2) {
            std::this_thread::yield();
        }
        go.store(true, std::memory_order_release);

        stopper.join();
        receiver.join();

        const int value = state.value.load(std::memory_order_acquire);
        const int stopped = state.stopped.load(std::memory_order_acquire);
        EXPECT_EQ(value + stopped, 1);
        if (received.has_value()) {
            EXPECT_EQ(value, 1);
            EXPECT_EQ(*received, 7);
        }
    }
}

TEST(TimerContextStressTest, StopRacesShortDeadline) {
    constexpr int kIterations = 128;
    forge::timer_context ctx;

    for (int iteration = 0; iteration < kIterations; ++iteration) {
        timer_race_state state;
        std::inplace_stop_source source;
        auto op = std::execution::connect(
            ctx.schedule_after(1ms),
            timer_race_receiver{&state, &source});

        std::execution::start(op);
        std::thread stopper{[&] {
            perturb(iteration, 0);
            source.request_stop();
        }};

        ASSERT_TRUE(wait_timer_done(state));
        stopper.join();

        std::lock_guard lk{state.mtx};
        EXPECT_EQ(state.value + state.stopped, 1);
    }
}

TEST(AsyncScopeStressTest, WaitRacesLastScheduledCompletion) {
    constexpr int kIterations = 128;
    constexpr int kTasks = 4;

    for (int iteration = 0; iteration < kIterations; ++iteration) {
        forge::static_thread_pool pool{4};
        forge::async_scope scope;
        std::atomic<int> completed{0};
        std::atomic<int> ready{0};
        std::atomic<bool> go{false};
        std::atomic<bool> wait_returned{false};

        for (int i = 0; i < kTasks; ++i) {
            ASSERT_TRUE(scope.spawn(
                std::execution::schedule(pool.get_scheduler()) |
                std::execution::then([&, i] noexcept {
                    spin_until_go(ready, go);
                    perturb(iteration, i);
                    completed.fetch_add(1, std::memory_order_acq_rel);
                })));
        }

        std::thread waiter{[&] {
            spin_until_go(ready, go);
            scope.wait();
            wait_returned.store(true, std::memory_order_release);
        }};

        while (ready.load(std::memory_order_acquire) != kTasks + 1) {
            std::this_thread::yield();
        }
        go.store(true, std::memory_order_release);

        waiter.join();
        pool.wait();

        EXPECT_TRUE(wait_returned.load(std::memory_order_acquire));
        EXPECT_EQ(completed.load(std::memory_order_acquire), kTasks);
    }
}

TEST(StaticThreadPoolStressTest, ScheduleRacesShutdown) {
    constexpr int kIterations = 128;
    constexpr int kCallers = 8;

    for (int iteration = 0; iteration < kIterations; ++iteration) {
        forge::static_thread_pool pool{forge::static_thread_pool_options{
            .thread_count = 2,
            .queue_capacity = 4,
        }};
        auto scheduler = pool.get_scheduler();

        std::atomic<int> ready{0};
        std::atomic<bool> go{false};
        std::atomic<int> completed{0};
        std::atomic<int> value{0};
        std::atomic<int> stopped{0};
        std::vector<std::thread> callers;

        for (int i = 0; i < kCallers; ++i) {
            callers.emplace_back([&, i] {
                spin_until_go(ready, go);
                perturb(iteration, i);
                auto result = std::execution::sync_wait(
                    std::execution::schedule(scheduler));
                if (result.has_value()) {
                    value.fetch_add(1, std::memory_order_acq_rel);
                } else {
                    stopped.fetch_add(1, std::memory_order_acq_rel);
                }
                completed.fetch_add(1, std::memory_order_acq_rel);
            });
        }

        std::thread shutdown_thread{[&] {
            spin_until_go(ready, go);
            perturb(iteration, kCallers);
            pool.shutdown();
        }};

        while (ready.load(std::memory_order_acquire) != kCallers + 1) {
            std::this_thread::yield();
        }
        go.store(true, std::memory_order_release);

        for (auto& caller : callers) {
            caller.join();
        }
        shutdown_thread.join();
        pool.wait();

        EXPECT_EQ(completed.load(std::memory_order_acquire), kCallers);
        EXPECT_EQ(
            value.load(std::memory_order_acquire) +
                stopped.load(std::memory_order_acquire),
            kCallers);
    }
}

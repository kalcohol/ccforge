#include <gtest/gtest.h>
#include <forge/start_detached.hpp>
#include <forge/static_thread_pool.hpp>
#include <forge/system_context.hpp>
#include "forge_counting_resource.hpp"
#include <execution>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory_resource>
#include <mutex>
#include <new>
#include <tuple>

static_assert(std::execution::scheduler<forge::static_thread_pool::scheduler>);

namespace {

struct stopped_receiver {
    using receiver_concept = std::execution::receiver_t;

    bool* stopped;

    void set_value() && noexcept { *stopped = false; }
    void set_error(std::exception_ptr) && noexcept { *stopped = false; }
    void set_stopped() && noexcept { *stopped = true; }
    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
};

struct pre_stopped_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::inplace_stop_source* source;
    bool* value;
    bool* stopped;

    void set_value() && noexcept { *value = true; }
    void set_error(std::exception_ptr) && noexcept { *value = true; }
    void set_stopped() && noexcept { *stopped = true; }

    auto get_env() const noexcept {
        return std::execution::make_env(
            std::execution::make_prop(
                std::execution::get_stop_token_t{}, source->get_token()));
    }
};

class throwing_resource final : public std::pmr::memory_resource {
public:
    void fail_allocations(bool value) noexcept {
        fail_ = value;
    }

private:
    auto do_allocate(std::size_t bytes, std::size_t alignment) -> void* override {
        if (fail_) {
            throw std::bad_alloc{};
        }
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(
        void* p,
        std::size_t bytes,
        std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(
        const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    bool fail_ = false;
};

} // namespace

TEST(StaticThreadPoolTest, BasicSchedule) {
    forge::static_thread_pool pool(2);
    auto sch = pool.get_scheduler();
    auto result = std::execution::sync_wait(
        std::execution::schedule(sch) | std::execution::then([]{ return 42; }));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(StaticThreadPoolTest, ReportsParallelForwardProgress) {
    forge::static_thread_pool pool(2);
    auto sch = pool.get_scheduler();

    EXPECT_EQ(
        std::execution::get_forward_progress_guarantee(sch),
        std::execution::forward_progress_guarantee::parallel);
}

TEST(StaticThreadPoolTest, ConcurrentTasks) {
    forge::static_thread_pool pool(4);
    auto sch = pool.get_scheduler();
    std::atomic<int> counter{0};
    for (int i = 0; i < 10; ++i) {
        forge::start_detached(
            std::execution::schedule(sch) | std::execution::then([&counter]{
                counter.fetch_add(1, std::memory_order_relaxed);
            }));
    }
    pool.wait();
    EXPECT_EQ(counter.load(), 10);
}

TEST(StaticThreadPoolTest, WaitFromWorkerReturnsWithoutSelfDeadlock) {
    forge::static_thread_pool pool(1);
    auto sch = pool.get_scheduler();
    std::atomic<bool> reached{false};

    auto result = std::execution::sync_wait(
        std::execution::schedule(sch) | std::execution::then([&] {
            pool.wait();
            reached.store(true, std::memory_order_release);
        }));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(reached.load(std::memory_order_acquire));
}

TEST(StaticThreadPoolTest, ScheduleAfterShutdownCompletesStopped) {
    forge::static_thread_pool pool(1);
    auto sch = pool.get_scheduler();
    pool.shutdown();

    auto result = std::execution::sync_wait(std::execution::schedule(sch));

    EXPECT_FALSE(result.has_value());
}

TEST(StaticThreadPoolTest, DirectScheduleAfterShutdownCompletesStopped) {
    forge::static_thread_pool pool(1);
    auto sch = pool.get_scheduler();
    pool.shutdown();

    bool stopped = false;
    auto op = std::execution::connect(
        std::execution::schedule(sch),
        stopped_receiver{&stopped});

    std::execution::start(op);

    EXPECT_TRUE(stopped);
}

TEST(StaticThreadPoolTest, ShutdownDrainsAcceptedWork) {
    forge::static_thread_pool pool(1);
    auto sch = pool.get_scheduler();

    std::mutex mtx;
    std::condition_variable cv;
    bool first_started = false;
    bool release_first = false;
    std::atomic<int> completed{0};

    forge::start_detached(
        std::execution::schedule(sch) | std::execution::then([&] {
            {
                std::lock_guard lk{mtx};
                first_started = true;
            }
            cv.notify_all();

            std::unique_lock lk{mtx};
            cv.wait(lk, [&] { return release_first; });
            completed.fetch_add(1, std::memory_order_relaxed);
        }));

    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(lk, std::chrono::seconds(2), [&] {
            return first_started;
        }));
    }

    forge::start_detached(
        std::execution::schedule(sch) | std::execution::then([&] {
            completed.fetch_add(1, std::memory_order_relaxed);
        }));

    pool.shutdown();
    {
        std::lock_guard lk{mtx};
        release_first = true;
    }
    cv.notify_all();

    pool.wait();

    EXPECT_EQ(completed.load(std::memory_order_relaxed), 2);
}

TEST(StaticThreadPoolTest, PreStoppedReceiverCompletesStoppedWithoutRunningThen) {
    forge::static_thread_pool pool(1);
    auto sch = pool.get_scheduler();
    std::inplace_stop_source source;
    source.request_stop();

    bool ran = false;
    bool value = false;
    bool stopped = false;
    auto op = std::execution::connect(
        std::execution::schedule(sch)
            | std::execution::then([&] { ran = true; }),
        pre_stopped_receiver{&source, &value, &stopped});

    std::execution::start(op);
    pool.wait();

    EXPECT_FALSE(ran);
    EXPECT_FALSE(value);
    EXPECT_TRUE(stopped);
}

TEST(StaticThreadPoolTest, ThreadCount) {
    forge::static_thread_pool pool(3);
    EXPECT_EQ(pool.thread_count(), 3u);
}

TEST(StaticThreadPoolTest, ZeroThreadCountNormalizesToOne) {
    forge::static_thread_pool pool{0};
    EXPECT_EQ(pool.thread_count(), 1u);
}

TEST(StaticThreadPoolTest, OptionsConstructorKeepsUnboundedDefault) {
    forge::static_thread_pool pool{forge::static_thread_pool_options{
        .thread_count = 1,
        .queue_capacity = std::nullopt,
    }};
    auto sch = pool.get_scheduler();
    std::atomic<int> completed{0};

    for (int i = 0; i < 64; ++i) {
        forge::start_detached(
            std::execution::schedule(sch)
            | std::execution::then([&] noexcept {
                completed.fetch_add(1, std::memory_order_relaxed);
            }));
    }

    pool.wait();
    EXPECT_EQ(completed.load(std::memory_order_relaxed), 64);
}

TEST(StaticThreadPoolTest, OptionsConstructorUsesCustomMemoryResourceForQueue) {
    forge_test::counting_resource resource;

    {
        forge::static_thread_pool pool{forge::static_thread_pool_options{
            .thread_count = 1,
            .queue_capacity = std::nullopt,
            .memory = &resource,
        }};
        auto sch = pool.get_scheduler();

        std::mutex mtx;
        std::condition_variable cv;
        bool first_started = false;
        bool release_first = false;
        std::atomic<int> completed{0};

        forge::start_detached(
            std::execution::schedule(sch)
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
            ASSERT_TRUE(cv.wait_for(lk, std::chrono::seconds(2), [&] {
                return first_started;
            }));
        }

        for (int i = 0; i < 16; ++i) {
            forge::start_detached(
                std::execution::schedule(sch)
                | std::execution::then([&] noexcept {
                    completed.fetch_add(1, std::memory_order_relaxed);
                }));
        }

        EXPECT_GT(resource.allocations(), 0u);

        {
            std::lock_guard lk{mtx};
            release_first = true;
        }
        cv.notify_all();
        pool.wait();

        EXPECT_EQ(completed.load(std::memory_order_relaxed), 16);
    }

    EXPECT_EQ(resource.allocations(), resource.deallocations());
}

TEST(StaticThreadPoolTest, QueueTaskAllocationFailureCompletesStopped) {
    throwing_resource resource;
    forge::static_thread_pool pool{forge::static_thread_pool_options{
        .thread_count = 1,
        .queue_capacity = std::nullopt,
        .memory = &resource,
    }};
    auto sch = pool.get_scheduler();
    resource.fail_allocations(true);

    bool stopped = false;
    auto op = std::execution::connect(
        std::execution::schedule(sch),
        stopped_receiver{&stopped});

    std::execution::start(op);
    pool.wait();

    EXPECT_TRUE(stopped);
}

TEST(StaticThreadPoolTest, BoundedQueueRejectsOverflowWithStopped) {
    forge::static_thread_pool pool{forge::static_thread_pool_options{
        .thread_count = 1,
        .queue_capacity = 1,
    }};
    auto sch = pool.get_scheduler();

    std::mutex mtx;
    std::condition_variable cv;
    bool first_started = false;
    bool release_first = false;

    forge::start_detached(
        std::execution::schedule(sch)
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
        ASSERT_TRUE(cv.wait_for(lk, std::chrono::seconds(2), [&] {
            return first_started;
        }));
    }

    bool second_stopped = false;
    auto second = std::execution::schedule(sch);
    auto second_op = std::execution::connect(
        std::move(second),
        stopped_receiver{&second_stopped});
    std::execution::start(second_op);
    EXPECT_FALSE(second_stopped);

    bool third_stopped = false;
    auto third = std::execution::schedule(sch);
    auto third_op = std::execution::connect(
        std::move(third),
        stopped_receiver{&third_stopped});
    std::execution::start(third_op);
    EXPECT_TRUE(third_stopped);

    {
        std::lock_guard lk{mtx};
        release_first = true;
    }
    cv.notify_all();

    pool.wait();
    EXPECT_FALSE(second_stopped);
}

TEST(StaticThreadPoolTest, BoundedQueueShutdownDrainsAcceptedWork) {
    forge::static_thread_pool pool{forge::static_thread_pool_options{
        .thread_count = 1,
        .queue_capacity = 2,
    }};
    auto sch = pool.get_scheduler();
    std::atomic<int> completed{0};

    for (int i = 0; i < 2; ++i) {
        forge::start_detached(
            std::execution::schedule(sch)
            | std::execution::then([&] noexcept {
                completed.fetch_add(1, std::memory_order_relaxed);
            }));
    }

    pool.shutdown();
    pool.wait();

    EXPECT_EQ(completed.load(std::memory_order_relaxed), 2);
}

TEST(SystemContextTest, GlobalScheduler) {
    auto& ctx = forge::system_context::get();
    auto sch = ctx.get_scheduler();
    static_assert(std::execution::scheduler<decltype(sch)>);
    auto result = std::execution::sync_wait(
        std::execution::schedule(sch) | std::execution::then([]{ return 99; }));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 99);
}

TEST(SystemContextTest, FreeFunctionReturnsGlobalScheduler) {
    auto sch = forge::get_system_scheduler();
    static_assert(std::execution::scheduler<decltype(sch)>);
    auto result = std::execution::sync_wait(
        std::execution::schedule(sch) | std::execution::then([]{ return 100; }));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 100);
}

#include <gtest/gtest.h>
#include <forge/async_scope.hpp>
#include <forge/static_thread_pool.hpp>
#include <forge/task.hpp>
#include "forge_counting_resource.hpp"
#include <execution>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {

struct stop_probe {
    bool possible = false;
    bool requested = false;
};

struct stop_probe_sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<stop_probe> probe;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;
        std::shared_ptr<stop_probe> probe;

        op(R r, std::shared_ptr<stop_probe> p)
            : rcvr(std::move(r)), probe(std::move(p)) {}
        op(op&&) = delete;
        op& operator=(op&&) = delete;
        op(const op&) = delete;
        op& operator=(const op&) = delete;

        void start() & noexcept {
            auto token = std::execution::get_stop_token(std::execution::get_env(rcvr));
            probe->possible = token.stop_possible();
            probe->requested = token.stop_requested();
            std::execution::set_value(std::move(rcvr));
        }
    };

    template<class R>
    auto connect(R rcvr) && -> op<R> {
        return op<R>{std::move(rcvr), std::move(probe)};
    }
};

struct destroyed_state {
    std::atomic<int> destroyed{0};
};

struct destruction_sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<destroyed_state> state;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;
        std::shared_ptr<destroyed_state> state;

        op(R r, std::shared_ptr<destroyed_state> s)
            : rcvr(std::move(r)), state(std::move(s)) {}
        op(op&&) = delete;
        op& operator=(op&&) = delete;
        op(const op&) = delete;
        op& operator=(const op&) = delete;

        ~op() {
            state->destroyed.fetch_add(1, std::memory_order_acq_rel);
        }

        void start() & noexcept {
            std::execution::set_value(std::move(rcvr));
        }
    };

    template<class R>
    auto connect(R rcvr) && -> op<R> {
        return op<R>{std::move(rcvr), std::move(state)};
    }
};

struct move_only_lvalue_sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<std::atomic<int>> starts;

    explicit move_only_lvalue_sender(std::shared_ptr<std::atomic<int>> s)
        : starts(std::move(s)) {}
    move_only_lvalue_sender(move_only_lvalue_sender&&) noexcept = default;
    move_only_lvalue_sender& operator=(move_only_lvalue_sender&&) noexcept = default;
    move_only_lvalue_sender(const move_only_lvalue_sender&) = delete;
    move_only_lvalue_sender& operator=(const move_only_lvalue_sender&) = delete;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;
        std::shared_ptr<std::atomic<int>> starts;

        op(R r, std::shared_ptr<std::atomic<int>> s)
            : rcvr(std::move(r)), starts(std::move(s)) {}
        op(op&&) = delete;
        op& operator=(op&&) = delete;
        op(const op&) = delete;
        op& operator=(const op&) = delete;

        void start() & noexcept {
            starts->fetch_add(1, std::memory_order_acq_rel);
            std::execution::set_value(std::move(rcvr));
        }
    };

    template<class R>
    auto connect(R rcvr) && -> op<R> {
        return op<R>{std::move(rcvr), std::move(starts)};
    }
};

struct boom_error {};

struct error_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_error_t(boom_error)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;

        explicit op(R r) : rcvr(std::move(r)) {}
        op(op&&) = delete;
        op& operator=(op&&) = delete;
        op(const op&) = delete;
        op& operator=(const op&) = delete;

        void start() & noexcept {
            std::execution::set_error(std::move(rcvr), boom_error{});
        }
    };

    template<class R>
    auto connect(R rcvr) && -> op<R> {
        return op<R>{std::move(rcvr)};
    }
};

} // namespace

TEST(AsyncScopeTest, SpawnReturnsFalseAfterClose) {
    forge::async_scope scope;
    scope.close();

    EXPECT_FALSE(scope.spawn(std::execution::just()));
}

TEST(AsyncScopeTest, WaitBlocksUntilScheduledWorkCompletes) {
    forge::static_thread_pool pool{1};
    forge::async_scope scope;
    std::atomic<bool> completed{false};
    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool release = false;
    bool wait_returned = false;

    ASSERT_TRUE(scope.spawn(
        std::execution::schedule(pool.get_scheduler()) |
        std::execution::then([&] noexcept {
            std::unique_lock lk{mtx};
            started = true;
            cv.notify_all();
            cv.wait(lk, [&] { return release; });
            completed.store(true, std::memory_order_release);
        })));

    bool worker_started = false;
    {
        std::unique_lock lk{mtx};
        worker_started = cv.wait_for(
            lk,
            std::chrono::seconds{2},
            [&] { return started; });
    }
    ASSERT_TRUE(worker_started);

    std::thread waiter{[&] {
        scope.wait();
        std::lock_guard lk{mtx};
        wait_returned = true;
        cv.notify_all();
    }};

    bool returned_before_release = false;
    {
        std::unique_lock lk{mtx};
        returned_before_release = cv.wait_for(
            lk,
            std::chrono::milliseconds{50},
            [&] { return wait_returned; });
        release = true;
        cv.notify_all();
    }

    waiter.join();
    pool.wait();

    EXPECT_FALSE(returned_before_release);
    EXPECT_TRUE(completed.load(std::memory_order_acquire));
}

TEST(AsyncScopeTest, StopTokenIsVisibleToSpawnedSender) {
    forge::async_scope scope;
    auto first = std::make_shared<stop_probe>();
    auto second = std::make_shared<stop_probe>();

    ASSERT_TRUE(scope.spawn(stop_probe_sender{first}));
    scope.request_stop();
    ASSERT_TRUE(scope.spawn(stop_probe_sender{second}));
    scope.wait();

    EXPECT_TRUE(first->possible);
    EXPECT_FALSE(first->requested);
    EXPECT_TRUE(second->possible);
    EXPECT_TRUE(second->requested);
}

TEST(AsyncScopeTest, CloseDoesNotRequestStop) {
    forge::async_scope scope;
    auto probe = std::make_shared<stop_probe>();

    ASSERT_TRUE(scope.spawn(stop_probe_sender{probe}));
    scope.close();
    scope.wait();

    EXPECT_TRUE(scope.closed());
    EXPECT_FALSE(scope.stop_requested());
    EXPECT_TRUE(probe->possible);
    EXPECT_FALSE(probe->requested);
}

TEST(AsyncScopeTest, ShutdownClosesAndRequestsStop) {
    forge::async_scope scope;
    scope.shutdown();

    EXPECT_TRUE(scope.closed());
    EXPECT_TRUE(scope.stop_requested());
    EXPECT_FALSE(scope.spawn(std::execution::just()));
}

TEST(AsyncScopeTest, CapturesFirstError) {
    forge::async_scope scope;

    ASSERT_TRUE(scope.spawn(error_sender{}));
    scope.wait();

    EXPECT_TRUE(scope.first_error());
    EXPECT_THROW(scope.rethrow_if_error(), boom_error);
}

TEST(AsyncScopeTest, ReclaimsCompletedOperationState) {
    forge::async_scope scope;
    auto state = std::make_shared<destroyed_state>();

    ASSERT_TRUE(scope.spawn(destruction_sender{state}));
    scope.wait();

    EXPECT_EQ(state->destroyed.load(std::memory_order_acquire), 1);
    EXPECT_TRUE(scope.spawn(std::execution::just()));
    scope.wait();
}

TEST(AsyncScopeTest, SpawnConsumesMoveOnlyLvalueSender) {
    forge::async_scope scope;
    auto starts = std::make_shared<std::atomic<int>>(0);
    move_only_lvalue_sender sender{starts};

    ASSERT_TRUE(scope.spawn(sender));
    scope.wait();

    EXPECT_EQ(starts->load(std::memory_order_acquire), 1);
    EXPECT_EQ(sender.starts, nullptr);
}

TEST(AsyncScopeTest, MultipleTasksCompleteOnce) {
    forge::async_scope scope;
    std::atomic<int> count{0};

    for (int i = 0; i < 32; ++i) {
        ASSERT_TRUE(scope.spawn(std::execution::just() |
            std::execution::then([&] noexcept {
                count.fetch_add(1, std::memory_order_acq_rel);
            })));
    }

    scope.wait();
    EXPECT_EQ(count.load(std::memory_order_acquire), 32);
}

TEST(AsyncScopeTest, OptionsConstructorUsesCustomMemoryResourceForSpawnNode) {
    forge_test::counting_resource resource;

    {
        forge::async_scope scope{forge::async_scope_options{.memory = &resource}};
        auto before_spawn = resource.allocations();

        ASSERT_TRUE(scope.spawn(std::execution::just()));
        scope.wait();

        EXPECT_GT(resource.allocations(), before_spawn);
    }

    EXPECT_EQ(resource.allocations(), resource.deallocations());
}

TEST(AsyncScopeTest, DestructorWaitsForOwnedWork) {
    forge::static_thread_pool pool{1};
    std::atomic<bool> completed{false};
    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool release = false;
    bool before_scope_destructor = false;
    bool destructor_returned = false;
    std::atomic<bool> spawned{false};

    struct destructor_probe {
        std::mutex* mtx;
        std::condition_variable* cv;
        bool* flag;

        ~destructor_probe() {
            std::lock_guard lk{*mtx};
            *flag = true;
            cv->notify_all();
        }
    };

    std::thread owner{[&] {
        {
            forge::async_scope scope;
            destructor_probe probe{&mtx, &cv, &before_scope_destructor};
            spawned.store(scope.spawn(
                std::execution::schedule(pool.get_scheduler()) |
                std::execution::then([&] noexcept {
                    std::unique_lock lk{mtx};
                    started = true;
                    cv.notify_all();
                    cv.wait(lk, [&] { return release; });
                    completed.store(true, std::memory_order_release);
                })),
                std::memory_order_release);
        }
        std::lock_guard lk{mtx};
        destructor_returned = true;
        cv.notify_all();
    }};

    struct owner_thread_guard {
        std::thread* owner;
        std::mutex* mtx;
        std::condition_variable* cv;
        bool* release;

        ~owner_thread_guard() {
            if (owner != nullptr && owner->joinable()) {
                {
                    std::lock_guard lk{*mtx};
                    *release = true;
                }
                cv->notify_all();
                owner->join();
            }
        }
    };
    [[maybe_unused]] owner_thread_guard owner_guard{&owner, &mtx, &cv, &release};

    {
        std::unique_lock lk{mtx};
        ASSERT_TRUE(cv.wait_for(
            lk,
            std::chrono::seconds{2},
            [&] { return started && before_scope_destructor; }));
        EXPECT_TRUE(spawned.load(std::memory_order_acquire));
        EXPECT_FALSE(cv.wait_for(
            lk,
            std::chrono::milliseconds{50},
            [&] { return destructor_returned; }));
        release = true;
        cv.notify_all();
    }

    owner.join();
    pool.wait();
    EXPECT_TRUE(spawned.load(std::memory_order_acquire));
    EXPECT_TRUE(completed.load(std::memory_order_acquire));
}

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
namespace {

forge::task<void> immediate_task(std::atomic<bool>* completed) {
    completed->store(true, std::memory_order_release);
    co_return;
}

} // namespace

TEST(AsyncScopeTest, SupportsImmediateTaskCompletion) {
    forge::async_scope scope;
    std::atomic<bool> completed{false};

    ASSERT_TRUE(scope.spawn(immediate_task(&completed)));
    scope.wait();

    EXPECT_TRUE(completed.load(std::memory_order_acquire));
}
#endif

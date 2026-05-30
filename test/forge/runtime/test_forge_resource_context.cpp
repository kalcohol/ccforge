#include <gtest/gtest.h>
#include <forge/channel.hpp>
#include <forge/resource_context.hpp>
#include <execution>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <tuple>

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

} // namespace

static_assert(std::execution::scheduler<forge::resource_context::scheduler>);

TEST(ResourceContextTest, SchedulerWorkRuns) {
    forge::resource_context ctx{1};

    auto result = std::execution::sync_wait(
        std::execution::schedule(ctx.get_scheduler())
        | std::execution::then([] { return 42; }));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(ResourceContextTest, TimerWorkRuns) {
    forge::resource_context ctx{1};

    auto result = std::execution::sync_wait(ctx.schedule_after(0ms));

    EXPECT_TRUE(result.has_value());
}

TEST(ResourceContextTest, SpawnedScopeWorkIsWaited) {
    forge::resource_context ctx{1};
    std::atomic<bool> completed{false};

    ASSERT_TRUE(ctx.spawn(
        std::execution::schedule(ctx.get_scheduler())
        | std::execution::then([&] noexcept {
            completed.store(true, std::memory_order_release);
        })));

    ctx.wait();

    EXPECT_TRUE(completed.load(std::memory_order_acquire));
}

TEST(ResourceContextTest, ShutdownRejectsNewSpawn) {
    forge::resource_context ctx{1};
    ctx.shutdown();

    EXPECT_FALSE(ctx.spawn(std::execution::just()));
}

TEST(ResourceContextTest, RequestStopIsVisibleToSpawnedWork) {
    forge::resource_context ctx{1};
    auto first = std::make_shared<stop_probe>();
    auto second = std::make_shared<stop_probe>();

    ASSERT_TRUE(ctx.spawn(stop_probe_sender{first}));
    ctx.request_stop();
    ASSERT_TRUE(ctx.spawn(stop_probe_sender{second}));
    ctx.wait();

    EXPECT_TRUE(first->possible);
    EXPECT_FALSE(first->requested);
    EXPECT_TRUE(second->possible);
    EXPECT_TRUE(second->requested);
}

TEST(ResourceContextTest, TimerCallbackCanSpawnCpuWork) {
    forge::resource_context ctx{1};
    std::atomic<bool> completed{false};

    std::execution::start_detached(
        ctx.schedule_after(0ms)
        | std::execution::then([&] noexcept {
            (void)ctx.spawn(
                std::execution::schedule(ctx.get_scheduler())
                | std::execution::then([&] noexcept {
                    completed.store(true, std::memory_order_release);
                }));
        }));

    ctx.wait();

    EXPECT_TRUE(completed.load(std::memory_order_acquire));
}

TEST(ResourceContextTest, DestructorWaitsForScopedWork) {
    std::atomic<bool> completed{false};

    {
        forge::resource_context ctx{1};
        ASSERT_TRUE(ctx.spawn(
            std::execution::schedule(ctx.get_scheduler())
            | std::execution::then([&] noexcept {
                completed.store(true, std::memory_order_release);
            })));
    }

    EXPECT_TRUE(completed.load(std::memory_order_acquire));
}

TEST(ResourceContextTest, FakeCommandLoopExitsWhenCommandChannelCloses) {
    forge::resource_context ctx{1};
    forge::bounded_channel<int> commands{2};
    forge::bounded_channel<int> events{2};

    ASSERT_TRUE(ctx.spawn(
        std::execution::schedule(ctx.get_scheduler())
        | std::execution::then([&] noexcept {
            while (true) {
                auto command = std::execution::sync_wait(commands.async_recv());
                if (!command) {
                    break;
                }
                (void)std::execution::sync_wait(
                    events.async_send(std::get<0>(*command) + 1));
            }
        })));

    ASSERT_TRUE(std::execution::sync_wait(commands.async_send(41)).has_value());
    auto event = std::execution::sync_wait(events.async_recv());
    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(std::get<0>(*event), 42);

    commands.close();
    ctx.wait();
}


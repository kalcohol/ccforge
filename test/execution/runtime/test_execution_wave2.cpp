#include <gtest/gtest.h>
#include <execution>
#include <forge/static_thread_pool.hpp>
#include <atomic>
#include <thread>
#include <tuple>
#include <utility>

namespace {

struct counting_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::atomic<int>* count;

    friend void tag_invoke(std::execution::set_value_t, counting_receiver&& self) noexcept {
        self.count->fetch_add(1, std::memory_order_relaxed);
    }

    template<class E>
    friend void tag_invoke(std::execution::set_error_t, counting_receiver&&, E&&) noexcept {}

    friend void tag_invoke(std::execution::set_stopped_t, counting_receiver&&) noexcept {}

    friend auto tag_invoke(std::execution::get_env_t, const counting_receiver&) noexcept
        -> std::execution::empty_env {
        return {};
    }
};

struct oversized_value_sender {
    using sender_concept = std::execution::sender_t;

    int value;

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;
        int value;
        unsigned char padding[2048]{};

        op(R r, int v) : rcvr(std::move(r)), value(v) {}
        op(const op&) = delete;
        op(op&&) = delete;
        op& operator=(const op&) = delete;
        op& operator=(op&&) = delete;

        friend void tag_invoke(std::execution::start_t, op& self) noexcept {
            std::execution::set_value(std::move(self.rcvr), self.value);
        }
    };

    friend auto tag_invoke(std::execution::get_completion_signatures_t,
                           const oversized_value_sender&, auto) noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t(int)> {
        return {};
    }

    template<std::execution::receiver R>
    friend auto tag_invoke(std::execution::connect_t, oversized_value_sender self, R r)
        -> op<R> {
        return op<R>{std::move(r), self.value};
    }

    friend auto tag_invoke(std::execution::get_env_t, const oversized_value_sender&) noexcept
        -> std::execution::empty_env {
        return {};
    }
};

} // namespace

TEST(SplitTest, HappyPath) {
    auto sndr = std::execution::split(std::execution::just(42));
    auto r1 = std::execution::sync_wait(sndr);
    auto r2 = std::execution::sync_wait(sndr);
    auto r3 = std::execution::sync_wait(sndr);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    ASSERT_TRUE(r3.has_value());
    EXPECT_EQ(std::get<0>(*r1), 42);
    EXPECT_EQ(std::get<0>(*r2), 42);
    EXPECT_EQ(std::get<0>(*r3), 42);
}

TEST(SplitTest, ErrorPath) {
    auto sndr = std::execution::split(std::execution::just_error(99));
    EXPECT_THROW(std::execution::sync_wait(sndr), int);
    EXPECT_THROW(std::execution::sync_wait(sndr), int);
}

TEST(SplitTest, StoppedPath) {
    auto sndr = std::execution::split(std::execution::just_stopped());
    auto r1 = std::execution::sync_wait(sndr);
    auto r2 = std::execution::sync_wait(sndr);
    EXPECT_FALSE(r1.has_value());
    EXPECT_FALSE(r2.has_value());
}

TEST(SplitTest, ConcurrentStart) {
    auto sndr = std::execution::split(
        std::execution::just(42) | std::execution::then([](int x) { return x * 2; }));

    std::atomic<int> r1{-1}, r2{-1};
    std::thread t1([&] {
        auto result = std::execution::sync_wait(sndr);
        if (result) r1.store(std::get<0>(*result));
    });
    std::thread t2([&] {
        auto result = std::execution::sync_wait(sndr);
        if (result) r2.store(std::get<0>(*result));
    });
    t1.join();
    t2.join();
    EXPECT_EQ(r1.load(), 84);
    EXPECT_EQ(r2.load(), 84);
}

TEST(SplitTest, DestroyedSubscriberIgnoredOnAsyncCompletion) {
    std::execution::run_loop loop;
    auto sch = loop.get_scheduler();
    auto sndr = std::execution::split(std::execution::schedule(sch));
    std::atomic<int> count{0};

    {
        auto op = std::execution::connect(sndr, counting_receiver{&count});
        std::execution::start(op);
    }

    loop.finish();
    loop.run();

    EXPECT_EQ(count.load(), 0);
}

TEST(SplitTest, OversizedInnerOperationUsesHeapFallback) {
    auto sndr = std::execution::split(oversized_value_sender{42});
    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(StaticThreadPoolStressTest, WhenAllSplitAndContinuesOnCompleteConcurrently) {
    forge::static_thread_pool pool(4);
    auto scheduler = pool.get_scheduler();

    for (int i = 0; i < 64; ++i) {
        std::atomic<int> when_all_ran{0};
        auto child = [&](int value) {
            return std::execution::schedule(scheduler)
                | std::execution::then([&, value] {
                      when_all_ran.fetch_add(1, std::memory_order_relaxed);
                      return value;
                  });
        };

        auto joined = std::execution::sync_wait(
            std::execution::when_all(child(1), child(2), child(3), child(4)));
        ASSERT_TRUE(joined.has_value());
        EXPECT_EQ(std::get<0>(*joined) + std::get<1>(*joined)
                    + std::get<2>(*joined) + std::get<3>(*joined), 10);
        EXPECT_EQ(when_all_ran.load(std::memory_order_relaxed), 4);

        std::atomic<int> continued{0};
        auto continued_result = std::execution::sync_wait(
            std::execution::continues_on(std::execution::just(5), scheduler)
                | std::execution::then([&](int x) {
                      continued.fetch_add(1, std::memory_order_relaxed);
                      return x + 1;
                  }));
        ASSERT_TRUE(continued_result.has_value());
        EXPECT_EQ(std::get<0>(*continued_result), 6);
        EXPECT_EQ(continued.load(std::memory_order_relaxed), 1);

        std::atomic<int> producer_runs{0};
        auto shared = std::execution::split(
            std::execution::schedule(scheduler)
                | std::execution::then([&] {
                      producer_runs.fetch_add(1, std::memory_order_relaxed);
                      return 42;
                  }));

        std::atomic<int> sum{0};
        std::atomic<int> failures{0};
        std::thread t1([&] {
            auto result = std::execution::sync_wait(shared);
            if (result.has_value()) sum.fetch_add(std::get<0>(*result), std::memory_order_relaxed);
            else failures.fetch_add(1, std::memory_order_relaxed);
        });
        std::thread t2([&] {
            auto result = std::execution::sync_wait(shared);
            if (result.has_value()) sum.fetch_add(std::get<0>(*result), std::memory_order_relaxed);
            else failures.fetch_add(1, std::memory_order_relaxed);
        });
        std::thread t3([&] {
            auto result = std::execution::sync_wait(shared);
            if (result.has_value()) sum.fetch_add(std::get<0>(*result), std::memory_order_relaxed);
            else failures.fetch_add(1, std::memory_order_relaxed);
        });
        std::thread t4([&] {
            auto result = std::execution::sync_wait(shared);
            if (result.has_value()) sum.fetch_add(std::get<0>(*result), std::memory_order_relaxed);
            else failures.fetch_add(1, std::memory_order_relaxed);
        });
        t1.join();
        t2.join();
        t3.join();
        t4.join();

        EXPECT_EQ(failures.load(std::memory_order_relaxed), 0);
        EXPECT_EQ(sum.load(std::memory_order_relaxed), 168);
        EXPECT_EQ(producer_runs.load(std::memory_order_relaxed), 1);
    }
}

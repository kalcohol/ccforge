#include <forge/split.hpp>
#include <gtest/gtest.h>
#include "observe_completion.hpp"
#include "sender_to.hpp"
#include "thread_pool_stress.hpp"
#include <execution>
#include <forge/static_thread_pool.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

struct counting_receiver {
    using receiver_concept = std::execution::receiver_tag;

    std::atomic<int>* count;

    void set_value() && noexcept {
        this->count->fetch_add(1, std::memory_order_relaxed);
    }

    template<class E>
    void set_error(E&&) && noexcept {}

    void set_stopped() && noexcept {}

    auto get_env() const noexcept
        -> std::execution::env<> {
        return {};
    }
};

struct cached_value_only_receiver {
    using receiver_concept = std::execution::receiver_tag;
    int* result;

    void set_value(const int& value) && noexcept { *result = value; }
    void set_error(std::exception_ptr) && noexcept { *result = -1; }
};

struct cached_error_only_receiver {
    using receiver_concept = std::execution::receiver_tag;
    bool* completed;

    void set_error(std::exception_ptr) && noexcept { *completed = true; }
};

struct cached_stopped_receiver {
    using receiver_concept = std::execution::receiver_tag;
    bool* completed;

    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept { *completed = true; }
};

struct oversized_value_sender {
    using sender_concept = std::execution::sender_tag;

    int value;

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_tag;

        R rcvr;
        int value;
        unsigned char padding[2048]{};

        op(R r, int v) : rcvr(std::move(r)), value(v) {}
        op(const op&) = delete;
        op(op&&) = delete;
        op& operator=(const op&) = delete;
        op& operator=(op&&) = delete;

        void start() & noexcept {
            std::execution::set_value(std::move(this->rcvr), this->value);
        }
    };

    template<class Self, class Env = std::execution::env<>>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t(int)> {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R r) const
        -> op<R> {
        return op<R>{std::move(r), value};
    }

    auto get_env() const noexcept
        -> std::execution::env<> {
        return {};
    }
};

struct cached_string_receiver {
    using receiver_concept = std::execution::receiver_tag;

    bool* received_mutable;

    void set_value(std::string& value) && noexcept {
        *received_mutable = true;
        value.clear();
    }

    void set_value(const std::string&) && noexcept {
        *received_mutable = false;
    }

    void set_value(std::string&&) && noexcept {
        *received_mutable = true;
    }

    template<class E>
    void set_error(E&&) && noexcept {}

    void set_stopped() && noexcept {}

    auto get_env() const noexcept -> std::execution::env<> {
        return {};
    }
};

template<class Pred>
bool wait_until(Pred pred) {
    for (int i = 0; i < 100; ++i) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

} // namespace

TEST(SplitTest, HappyPath) {
    auto sndr = forge::split(std::execution::just(42));
    auto r1 = std::this_thread::sync_wait(sndr);
    auto r2 = std::this_thread::sync_wait(sndr);
    auto r3 = std::this_thread::sync_wait(sndr);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    ASSERT_TRUE(r3.has_value());
    EXPECT_EQ(std::get<0>(*r1), 42);
    EXPECT_EQ(std::get<0>(*r2), 42);
    EXPECT_EQ(std::get<0>(*r3), 42);
}

TEST(SplitTest, InstantiatesOnlyDeclaredCompletionChannels) {
    int value = 0;
    auto values = forge::split(std::execution::just(42));
    static_assert(test_execution::sender_to<
        decltype(values), cached_value_only_receiver>);
    auto value_op = std::execution::connect(
        values, cached_value_only_receiver{&value});
    std::execution::start(value_op);
    EXPECT_EQ(value, 42);

    bool error = false;
    auto errors = forge::split(std::execution::just_error(7));
    static_assert(test_execution::sender_to<
        decltype(errors), cached_error_only_receiver>);
    auto error_op = std::execution::connect(
        errors, cached_error_only_receiver{&error});
    std::execution::start(error_op);
    EXPECT_TRUE(error);

    bool stopped = false;
    auto stops = forge::split(std::execution::just_stopped());
    static_assert(test_execution::sender_to<
        decltype(stops), cached_stopped_receiver>);
    auto stop_op = std::execution::connect(
        stops, cached_stopped_receiver{&stopped});
    std::execution::start(stop_op);
    EXPECT_TRUE(stopped);
}

TEST(SplitTest, CachesMoveOnlyValueForMultipleSubscribers) {
    auto sender = forge::split(
        std::execution::just(std::make_unique<int>(42)));
    auto read_cached = std::execution::then(
        [](const std::unique_ptr<int>& value) noexcept {
            return value.get();
        });

    auto first = std::this_thread::sync_wait(sender | read_cached);
    auto second = std::this_thread::sync_wait(sender | read_cached);

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_NE(std::get<0>(*first), nullptr);
    EXPECT_EQ(std::get<0>(*first), std::get<0>(*second));
    EXPECT_EQ(*std::get<0>(*first), 42);
}

TEST(SplitTest, ContinuesOnCopiesCachedReferenceValuesAcrossHop) {
    auto sender = std::execution::continues_on(
        forge::split(std::execution::just(std::string{"cached"})),
        std::execution::inline_scheduler{});
    using signatures_t = decltype(std::execution::get_completion_signatures(
        sender,
        std::execution::env<>{}));
    static_assert(std::is_same_v<
        signatures_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(std::string),
            std::execution::set_error_t(std::exception_ptr)>>);

    auto result = std::this_thread::sync_wait(std::move(sender));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), "cached");
}

TEST(SplitTest, SubscribersCannotMutateCachedValues) {
    auto sndr = forge::split(std::execution::just(std::string{"cached"}));
    bool received_mutable = false;

    auto first = std::execution::connect(
        sndr, cached_string_receiver{&received_mutable});
    std::execution::start(first);

    EXPECT_FALSE(received_mutable);
    auto second = std::this_thread::sync_wait(sndr);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(std::get<0>(*second), "cached");
}

TEST(SplitTest, ErrorPath) {
    auto sndr = forge::split(std::execution::just_error(99));
    for (int i = 0; i < 2; ++i) {
        auto result = forge_test::observe_completion(sndr);
        ASSERT_TRUE(result.error);
        try {
            std::rethrow_exception(*result.error);
        } catch (int error) {
            EXPECT_EQ(error, 99);
        }
    }
}

TEST(SplitTest, StoppedPath) {
    auto sndr = forge::split(std::execution::just_stopped());
    EXPECT_TRUE(forge_test::observe_completion(sndr).stopped);
    EXPECT_TRUE(forge_test::observe_completion(sndr).stopped);
}

TEST(SplitTest, ConcurrentStart) {
    auto sndr = forge::split(
        std::execution::just(42) | std::execution::then([](int x) { return x * 2; }));

    std::atomic<int> r1{-1}, r2{-1};
    std::thread t1([&] {
        auto result = std::this_thread::sync_wait(sndr);
        if (result) r1.store(std::get<0>(*result));
    });
    std::thread t2([&] {
        auto result = std::this_thread::sync_wait(sndr);
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
    auto sndr = forge::split(std::execution::schedule(sch));
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
    auto sndr = forge::split(oversized_value_sender{42});
    auto result = std::this_thread::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(StaticThreadPoolStressTest, WhenAllSplitAndContinuesOnCompleteConcurrently) {
    test_execution::test_thread_pool standard_pool{4};
    auto scheduler = standard_pool.get_scheduler();
    forge::static_thread_pool split_pool{4};
    auto split_scheduler = split_pool.get_scheduler();

    for (int i = 0; i < 64; ++i) {
        ASSERT_NO_FATAL_FAILURE(test_execution::check_standard_pool_composition(scheduler));

        std::atomic<int> producer_runs{0};
        auto shared = forge::split(
            std::execution::schedule(split_scheduler)
                | std::execution::then([&] {
                      producer_runs.fetch_add(1, std::memory_order_relaxed);
                      return 42;
                  }));

        std::atomic<int> sum{0};
        std::atomic<int> failures{0};
        std::thread t1([&] {
            auto result = std::this_thread::sync_wait(shared);
            if (result.has_value()) sum.fetch_add(std::get<0>(*result), std::memory_order_relaxed);
            else failures.fetch_add(1, std::memory_order_relaxed);
        });
        std::thread t2([&] {
            auto result = std::this_thread::sync_wait(shared);
            if (result.has_value()) sum.fetch_add(std::get<0>(*result), std::memory_order_relaxed);
            else failures.fetch_add(1, std::memory_order_relaxed);
        });
        std::thread t3([&] {
            auto result = std::this_thread::sync_wait(shared);
            if (result.has_value()) sum.fetch_add(std::get<0>(*result), std::memory_order_relaxed);
            else failures.fetch_add(1, std::memory_order_relaxed);
        });
        std::thread t4([&] {
            auto result = std::this_thread::sync_wait(shared);
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

#include <gtest/gtest.h>
#include <execution>
#include <atomic>
#include <thread>
#include <tuple>

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

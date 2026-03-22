#include <gtest/gtest.h>
#include <execution>
#include <forge/any_sender.hpp>
#include <thread>
#include <atomic>

// ─── T6: domain tests ───────────────────────────────────────────────────────

TEST(DefaultDomainTest, GetDomainFromEmptyEnv) {
    std::execution::empty_env env{};
    auto domain = std::execution::get_domain(env);
    static_assert(std::is_same_v<decltype(domain), std::execution::default_domain>);
    SUCCEED();
}

TEST(DefaultDomainTest, TransformSenderIsIdentity) {
    auto sndr = std::execution::just(42);
    std::execution::empty_env env{};
    std::execution::default_domain domain{};
    auto& result = domain.transform_sender(sndr, env);
    (void)result;
    SUCCEED();
}

// ─── T7: counting_scope tests ───────────────────────────────────────────────

TEST(SimpleCountingScopeTest, SpawnAndJoin) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    std::atomic<int> counter{0};
    token.spawn(std::execution::just() | std::execution::then([&counter] {
        counter.fetch_add(1, std::memory_order_relaxed);
    }));

    // inline_scheduler runs synchronously, so counter is already 1
    EXPECT_EQ(counter.load(), 1);
    scope.join();
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, ClosePreventsFurtherSpawns) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    scope.close();
    EXPECT_TRUE(scope.is_closed());

    std::atomic<int> counter{0};
    token.spawn(std::execution::just() | std::execution::then([&counter] {
        counter.fetch_add(1, std::memory_order_relaxed);
    }));
    // spawn should silently ignore (scope closed)
    EXPECT_EQ(counter.load(), 0);
    scope.join();
}

TEST(SimpleCountingScopeTest, MultipleSpawns) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    std::atomic<int> counter{0};
    for (int i = 0; i < 5; ++i) {
        token.spawn(std::execution::just() | std::execution::then([&counter] {
            counter.fetch_add(1, std::memory_order_relaxed);
        }));
    }
    // inline_scheduler is synchronous
    EXPECT_EQ(counter.load(), 5);
    scope.join();
    EXPECT_EQ(scope.count(), 0u);
}

// ─── T5: forge::any_sender_of tests ─────────────────────────────────────────

using cs_int = std::execution::completion_signatures<
    std::execution::set_value_t(int),
    std::execution::set_error_t(std::exception_ptr),
    std::execution::set_stopped_t()>;

TEST(AnySenderTest, IsASender) {
    static_assert(std::execution::sender<forge::any_sender_of<cs_int>>);
    SUCCEED();
}

TEST(AnySenderTest, DefaultEmpty) {
    forge::any_sender_of<cs_int> s;
    EXPECT_FALSE(bool(s));
}

TEST(AnySenderTest, HoldsJustSender) {
    forge::any_sender_of<cs_int> s = std::execution::just(42);
    EXPECT_TRUE(bool(s));
}

TEST(AnySenderTest, SyncWait) {
    forge::any_sender_of<cs_int> s = std::execution::just(42);
    auto result = s.sync_wait();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(AnySenderTest, MoveSemantics) {
    forge::any_sender_of<cs_int> s1 = std::execution::just(99);
    forge::any_sender_of<cs_int> s2 = std::move(s1);
    EXPECT_FALSE(bool(s1));
    EXPECT_TRUE(bool(s2));
    auto result = s2.sync_wait();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 99);
}

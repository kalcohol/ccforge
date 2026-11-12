#include <gtest/gtest.h>

#include <execution>
#include <forge/static_thread_pool.hpp>

#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <thread>
#include <type_traits>

namespace {

template<class Scope>
void expect_cross_thread_join_then_destroy() {
    static_assert(std::is_nothrow_destructible_v<Scope>);
    forge::static_thread_pool pool{1};
    const auto owner = std::this_thread::get_id();
    for (int iteration = 0; iteration < 1024; ++iteration) {
        SCOPED_TRACE(iteration);
        std::thread::id worker;
        {
            Scope scope;
            std::execution::spawn(
                std::execution::schedule(pool.get_scheduler())
                    | std::execution::then([&worker]() noexcept {
                          worker = std::this_thread::get_id();
                      }),
                scope.get_token());

            // Also exercise join's zero-count path racing the final unlock.
            if (iteration % 2 == 0) {
                while (scope.count() != 0) {
                    std::this_thread::yield();
                }
            }
            auto joined = std::this_thread::sync_wait(scope.join());
            EXPECT_TRUE(joined.has_value());
            EXPECT_EQ(scope.count(), 0u);
            EXPECT_NE(worker, std::thread::id{});
            EXPECT_NE(worker, owner);
        } // Destroy immediately after join, before waiting for the worker pool.
    }
    pool.wait();
}

template<class Scope>
void destroy_drained_without_join(bool close) {
    std::set_terminate([] { std::_Exit(86); });
    Scope scope;
    std::execution::spawn(std::execution::just(), scope.get_token());
    // A different exit status prevents an outstanding-count check from
    // accidentally satisfying this regression test.
    if (scope.count() != 0) std::_Exit(87);
    if (close) scope.close();
}

template<class Scope>
void expect_unused_destruction() {
    { Scope unused; }
    {
        Scope unused_and_closed;
        unused_and_closed.close();
    }
}

} // namespace

TEST(SimpleCountingScopeLifetimeTest, CrossThreadJoinThenDestroy) {
    expect_cross_thread_join_then_destroy<std::execution::simple_counting_scope>();
}

TEST(CountingScopeLifetimeTest, CrossThreadJoinThenDestroy) {
    expect_cross_thread_join_then_destroy<std::execution::counting_scope>();
}

TEST(SimpleCountingScopeLifetimeTest, UnusedDestruction) {
    expect_unused_destruction<std::execution::simple_counting_scope>();
}

TEST(CountingScopeLifetimeTest, UnusedDestruction) {
    expect_unused_destruction<std::execution::counting_scope>();
}

TEST(SimpleCountingScopeDeathTest, DrainedWithoutJoinTerminates) {
#if GTEST_HAS_DEATH_TEST
    for (bool close : {false, true}) {
        EXPECT_EXIT(destroy_drained_without_join<std::execution::simple_counting_scope>(close),
                    ::testing::ExitedWithCode(86), "");
    }
#else
    GTEST_SKIP() << "death tests are not supported by this gtest build";
#endif
}

TEST(CountingScopeDeathTest, DrainedWithoutJoinTerminates) {
#if GTEST_HAS_DEATH_TEST
    for (bool close : {false, true}) {
        EXPECT_EXIT(destroy_drained_without_join<std::execution::counting_scope>(close),
                    ::testing::ExitedWithCode(86), "");
    }
#else
    GTEST_SKIP() << "death tests are not supported by this gtest build";
#endif
}

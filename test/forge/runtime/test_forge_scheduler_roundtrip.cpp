#include <gtest/gtest.h>
#include <forge/any_scheduler.hpp>
#include <forge/runtime_context.hpp>
#include <forge/single_thread_context.hpp>
#include <forge/static_thread_pool.hpp>
#include <execution>
#include <type_traits>

namespace {

template<class Scheduler>
auto completion_scheduler_of(const Scheduler& scheduler) {
    auto sender = std::execution::schedule(scheduler);
    return std::execution::get_completion_scheduler<std::execution::set_value_t>(
        std::execution::get_env(sender));
}

template<class Scheduler>
auto member_completion_scheduler_of(const Scheduler& scheduler) {
    auto sender = std::execution::schedule(scheduler);
    auto env = std::execution::get_env(sender);
    return env.query(
        std::execution::get_completion_scheduler_t<
            std::execution::set_value_t>{});
}

} // namespace

TEST(ForgeSchedulerRoundtripTest, StaticThreadPoolSchedulerRoundtrips) {
    forge::static_thread_pool pool{1};
    auto scheduler = pool.get_scheduler();
    auto roundtrip = completion_scheduler_of(scheduler);

    static_assert(std::is_same_v<decltype(roundtrip), decltype(scheduler)>);
    EXPECT_TRUE(roundtrip == scheduler);
}

TEST(ForgeSchedulerRoundtripTest, SingleThreadContextSchedulerRoundtrips) {
    forge::single_thread_context context;
    auto scheduler = context.get_scheduler();
    auto roundtrip = completion_scheduler_of(scheduler);

    static_assert(std::is_same_v<decltype(roundtrip), decltype(scheduler)>);
    EXPECT_TRUE(roundtrip == scheduler);
}

TEST(ForgeSchedulerRoundtripTest, RuntimeContextSchedulerRoundtrips) {
    forge::runtime_context context{1};
    auto scheduler = context.get_scheduler();
    auto roundtrip = completion_scheduler_of(scheduler);

    static_assert(std::is_same_v<decltype(roundtrip), decltype(scheduler)>);
    EXPECT_TRUE(roundtrip == scheduler);
}

TEST(ForgeSchedulerRoundtripTest, AnySchedulerRoundtripsWithSharedIdentity) {
    forge::static_thread_pool pool{1};
    forge::any_scheduler scheduler{pool.get_scheduler()};
    auto roundtrip = completion_scheduler_of(scheduler);

    static_assert(std::is_same_v<decltype(roundtrip), forge::any_scheduler>);
    EXPECT_TRUE(roundtrip == scheduler);
    EXPECT_TRUE(bool(roundtrip));

    auto member_roundtrip = member_completion_scheduler_of(scheduler);
    static_assert(std::is_same_v<
                  decltype(member_roundtrip),
                  forge::any_scheduler>);
    EXPECT_TRUE(member_roundtrip == scheduler);
}

TEST(ForgeSchedulerRoundtripTest, EmptyAnySchedulerRoundtripsAsEmpty) {
    forge::any_scheduler scheduler;
    auto roundtrip = completion_scheduler_of(scheduler);

    static_assert(std::is_same_v<decltype(roundtrip), forge::any_scheduler>);
    EXPECT_TRUE(roundtrip == scheduler);
    EXPECT_FALSE(bool(roundtrip));
}

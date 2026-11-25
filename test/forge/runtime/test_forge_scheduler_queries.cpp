#include <execution>
#include <forge/any_scheduler.hpp>
#include <forge/static_thread_pool.hpp>
#include <forge/strand.hpp>
#include <forge/task.hpp>
#include <gtest/gtest.h>

#include <concepts>

namespace {
namespace ex = std::execution;

template<class Tag, class S>
void check_parity(const S& scheduler) {
    auto attributes = ex::get_env(ex::schedule(scheduler));
    EXPECT_EQ(ex::get_completion_scheduler<Tag>(scheduler), scheduler);
    EXPECT_EQ(ex::get_completion_scheduler<Tag>(attributes), scheduler);
    EXPECT_EQ(ex::get_completion_scheduler<Tag>(scheduler, ex::env<>{}),
              ex::get_completion_scheduler<Tag>(attributes, ex::env<>{}));
    EXPECT_EQ(ex::get_completion_scheduler<Tag>(scheduler, ex::env<>{}, ex::env<>{}),
              ex::get_completion_scheduler<Tag>(attributes, ex::env<>{}, ex::env<>{}));
    static_assert(std::same_as<decltype(ex::get_completion_domain<Tag>(attributes, ex::env<>{})),
                               decltype(ex::get_completion_domain<Tag>(scheduler, ex::env<>{}))>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<Tag>(attributes, ex::env<>{}, ex::env<>{})),
                               decltype(ex::get_completion_domain<Tag>(scheduler, ex::env<>{}, ex::env<>{}))>);
    EXPECT_EQ(ex::get_forward_progress_guarantee(scheduler),
              scheduler.query(ex::get_forward_progress_guarantee));
}

TEST(QueryConformanceTest, ForgeSchedulersPreserveResourceIdentity) {
    forge::static_thread_pool first{1};
    forge::static_thread_pool second{1};
    auto scheduler = first.get_scheduler();
    check_parity<ex::set_value_t>(scheduler);
    check_parity<ex::set_stopped_t>(scheduler);
    EXPECT_NE(scheduler, second.get_scheduler());
    forge::strand serial{scheduler};
    check_parity<ex::set_value_t>(serial.get_scheduler());
    check_parity<ex::set_stopped_t>(serial.get_scheduler());
    forge::any_scheduler erased{scheduler};
    check_parity<ex::set_value_t>(erased);
    check_parity<ex::set_stopped_t>(erased);
    check_parity<ex::set_error_t>(erased);
    EXPECT_NE(erased, forge::any_scheduler{scheduler});
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
    check_parity<ex::set_value_t>(forge::__task_detail::__inline_fallback_scheduler{});
#endif
}
} // namespace

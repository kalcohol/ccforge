#include <execution>
#include <forge/static_thread_pool.hpp>
#include <forge/strand.hpp>
#include <gtest/gtest.h>

#include <future>
#include <utility>

namespace {
namespace ex = std::execution;

struct promise_receiver {
    using receiver_concept = ex::receiver_tag;
    std::promise<int>* promise;
    void set_value() && noexcept { promise->set_value(1); }
    void set_stopped() && noexcept { promise->set_value(2); }
    auto get_env() const noexcept -> ex::env<> { return {}; }
};
TEST(ExecutionSchedulingConformance, ForgeUnstoppableScheduleCompletesValue) {
    forge::static_thread_pool pool{1};
    std::promise<int> pool_value;
    auto pool_future = pool_value.get_future();
    auto pool_op = ex::connect(ex::schedule(pool.get_scheduler()), promise_receiver{&pool_value});
    ex::start(pool_op);
    EXPECT_EQ(pool_future.get(), 1);
    forge::strand strand{pool.get_scheduler()};
    std::promise<int> strand_value;
    auto strand_future = strand_value.get_future();
    auto strand_op = ex::connect(ex::schedule(strand.get_scheduler()), promise_receiver{&strand_value});
    ex::start(strand_op);
    EXPECT_EQ(strand_future.get(), 1);
    pool.wait();
}
struct rejected_receiver : promise_receiver {
    void set_stopped() && noexcept { promise->set_value(2); }
};
TEST(ExecutionSchedulingConformance, ForgeRejectionCompletesStoppedWithoutStopToken) {
    forge::static_thread_pool pool{1};
    pool.shutdown();
    std::promise<int> completion;
    auto future = completion.get_future();
    auto op = ex::connect(ex::schedule(pool.get_scheduler()), rejected_receiver{{&completion}});
    ex::start(op);
    EXPECT_EQ(future.get(), 2);
}
} // namespace

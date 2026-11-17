#include <execution>
#include <forge/static_thread_pool.hpp>
#include <forge/strand.hpp>
#include <gtest/gtest.h>
#include <future>
#include <utility>
namespace {
namespace ex = std::execution;
struct result { int value = 0; int hops = 0; };
struct receiver {
    using receiver_concept = ex::receiver_tag;
    result* out;
    void set_value(int v) && noexcept { out->value = v; }
    void set_error(std::exception_ptr) && noexcept { out->value = -1; }
    void set_stopped() && noexcept { out->value = -2; }
    auto get_env() const noexcept { return ex::prop{ex::get_start_scheduler, ex::inline_scheduler{}}; }
};
struct customized {
    using sender_concept = ex::sender_tag;
    result* out;
    auto affine() && { ++out->hops; return ex::just(42); }
};
TEST(ExecutionSchedulingConformance, AffineUsesMemberCustomization) {
    result out;
    auto op = ex::connect(ex::affine(customized{&out}), receiver{&out});
    EXPECT_EQ(out.hops, 1);
    ex::start(op);
    EXPECT_EQ(out.value, 42);
}
struct categorized {
    using sender_concept = ex::sender_tag;
    auto affine() & { return ex::just(char{1}); }
    auto affine() const& { return ex::just(2L); }
    auto affine() && { return ex::just(3); }
    auto affine() const&& { return ex::just(short{4}); }
};
TEST(ExecutionSchedulingConformance, AffineUsesTheSameCustomizationForEveryCategory) {
    result out;
    auto sender = ex::affine(categorized{});
    auto lvalue = ex::connect(sender, receiver{&out});
    ex::start(lvalue);
    EXPECT_EQ(out.value, 1);
    auto const_lvalue = ex::connect(std::as_const(sender), receiver{&out});
    ex::start(const_lvalue);
    EXPECT_EQ(out.value, 2);
    auto rvalue = ex::connect(std::move(sender), receiver{&out});
    ex::start(rvalue);
    EXPECT_EQ(out.value, 3);
    const auto constant = ex::affine(categorized{});
    auto const_rvalue = ex::connect(std::move(constant), receiver{&out});
    ex::start(const_rvalue);
    EXPECT_EQ(out.value, 4);
}
struct mutable_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    int* calls;
    auto schedule() & noexcept { ++*calls; return ex::just(); }
    auto schedule() && noexcept { *calls += 10; return ex::just(); }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const mutable_scheduler&) const noexcept = default;
};
struct mutable_env {
    int* calls;
    auto query(ex::get_start_scheduler_t) const noexcept -> mutable_scheduler { return {calls}; }
};
struct mutable_receiver : receiver {
    int* calls;
    auto get_env() const noexcept -> mutable_env { return {calls}; }
};
// [exec.prop] returns const ValueType&, which fails the scheduler requirement
// of [exec.get.start.scheduler]/2 and hence [exec.affine]/7 for this scheduler.
using mutable_prop_env = decltype(ex::prop{ex::get_start_scheduler, mutable_scheduler{nullptr}});
static_assert(ex::scheduler<mutable_scheduler> && !ex::scheduler<const mutable_scheduler&>);
static_assert(std::same_as<decltype(std::declval<const mutable_prop_env&>().query(ex::get_start_scheduler)),
    const mutable_scheduler&>);
static_assert(!ex::sender_in<decltype(ex::affine(ex::just(42))), mutable_prop_env>);
static_assert(std::same_as<decltype(ex::get_start_scheduler(std::declval<const mutable_env&>())),
    mutable_scheduler>);
static_assert(ex::sender_in<decltype(ex::affine(ex::just(42))), mutable_env>);
TEST(ExecutionSchedulingConformance, AffineSchedulesThroughMutableOwnedScheduler) {
    int calls = 0;
    result out;
    auto op = ex::connect(ex::affine(ex::just(42)), mutable_receiver{{&out}, &calls});
    ex::start(op);
    EXPECT_EQ(out.value, 42);
    EXPECT_EQ(calls, 1);
    ex::__forge_affine::__unstoppable_scheduler<mutable_scheduler> wrapper{{&calls}};
    (void)ex::schedule(wrapper);
    EXPECT_EQ(calls, 2);
    (void)ex::schedule(std::move(wrapper));
    EXPECT_EQ(calls, 12);
}
struct domain_source {
    using sender_concept = ex::sender_tag;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(int)> { return {}; }
};
struct scheduler_domain {
    template<class Env>
    auto transform_sender(ex::start_t, domain_source&&, const Env&) const noexcept
        -> decltype(ex::just(73L)) { return ex::just(73L); }
};
struct domain_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    auto schedule() const noexcept { return ex::just(); }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    template<class Env>
    auto query(ex::get_completion_domain_t<ex::set_value_t>, const Env&) const noexcept
        -> scheduler_domain { return {}; }
    bool operator==(const domain_scheduler&) const noexcept = default;
};
struct addressless_env {
    addressless_env* operator&() = delete;
    const addressless_env* operator&() const = delete;
    auto query(ex::get_scheduler_t) const noexcept -> domain_scheduler { return {}; }
};
struct addressless_receiver : receiver {
    auto get_env() const noexcept -> addressless_env { return {}; }
};
TEST(ExecutionSchedulingConformance, DeletedEnvironmentAddressPreservesSchedulerDomainTransform) {
    static_assert(std::same_as<decltype(ex::get_domain(std::declval<const addressless_env&>())),
        scheduler_domain>);
    static_assert(std::same_as<ex::completion_signatures_of_t<domain_source, addressless_env>,
        ex::completion_signatures<ex::set_value_t(long)>>);
    result out;
    auto op = ex::connect(domain_source{}, addressless_receiver{{&out}});
    ex::start(op);
    EXPECT_EQ(out.value, 73);
}
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
struct distinct_schedule_sender {
    using sender_concept = ex::sender_tag;
    bool fail;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(), ex::set_error_t(long)> { return {}; }
    template<class R> struct operation {
        using operation_state_concept = ex::operation_state_tag;
        R receiver;
        bool fail;
        void start() & noexcept {
            if (fail) ex::set_error(std::move(receiver), 9L);
            else ex::set_value(std::move(receiver));
        }
    };
    template<class R> auto connect(R r) const noexcept -> operation<R> { return {std::move(r), fail}; }
};
struct distinct_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    int* calls;
    bool fail;
    auto schedule() & noexcept -> distinct_schedule_sender { ++*calls; return {fail}; }
    auto schedule() && noexcept { *calls += 10; return ex::just_error(short{1}); }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const distinct_scheduler&) const noexcept = default;
};
struct category_receiver : receiver {
    void set_error(long error) && noexcept { out->value = -static_cast<int>(error); }
    using receiver::set_error;
};
TEST(ExecutionSchedulingConformance, StartAndContinuationUseDistinctLvalueScheduleResult) {
    for (bool fail : {false, true}) {
        int calls = 0;
        result out;
        auto start = ex::connect(ex::starts_on(distinct_scheduler{&calls, fail}, ex::just(42)),
            category_receiver{{&out}});
        ex::start(start);
        EXPECT_EQ(calls, 1);
        EXPECT_EQ(out.value, fail ? -9 : 42);
        auto continuation = ex::connect(ex::continues_on(ex::just(42), distinct_scheduler{&calls, fail}),
            category_receiver{{&out}});
        ex::start(continuation);
        EXPECT_EQ(calls, 2);
        EXPECT_EQ(out.value, fail ? -9 : 42);
    }
}
} // namespace

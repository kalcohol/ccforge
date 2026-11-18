#include "../scheduler_fixture.hpp"

#include <forge/bulk.hpp>
#include <forge/execution_extensions.hpp>
#include <gtest/gtest.h>
#include <forge/start_detached.hpp>
#include <execution>
#include <atomic>
#include <future>
#include <memory>
#include <optional>
#include <thread>
#include <type_traits>
#include <variant>
#include <tuple>
#include <stdexcept>

namespace {

struct throwing_move_value {
    throwing_move_value() = default;
    throwing_move_value(const throwing_move_value&) = default;
    throwing_move_value& operator=(const throwing_move_value&) = default;
    throwing_move_value(throwing_move_value&&) {
        throw std::runtime_error("move failed");
    }
    throwing_move_value& operator=(throwing_move_value&&) = delete;
};

template<class R>
struct throwing_value_op {
    using operation_state_concept = std::execution::operation_state_tag;

    explicit throwing_value_op(R r) : rcvr(std::move(r)) {}
    throwing_value_op(const throwing_value_op&) = delete;
    throwing_value_op& operator=(const throwing_value_op&) = delete;
    throwing_value_op(throwing_value_op&&) = delete;
    throwing_value_op& operator=(throwing_value_op&&) = delete;

    R rcvr;

    void start() & noexcept {
        std::execution::set_value(std::move(this->rcvr), throwing_move_value{});
    }
};

struct throwing_value_sender {
    using sender_concept = std::execution::sender_tag;

    template<class Self, class Env = forge::empty_env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(throwing_move_value)> {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R r) const
        -> throwing_value_op<R> {
        return throwing_value_op<R>{std::move(r)};
    }

    auto get_env() const noexcept
        -> forge::empty_env {
        return {};
    }
};

template<class R>
struct scheduler_value_op {
    using operation_state_concept = std::execution::operation_state_tag;

    explicit scheduler_value_op(R r) : rcvr(std::move(r)) {}
    scheduler_value_op(const scheduler_value_op&) = delete;
    scheduler_value_op& operator=(const scheduler_value_op&) = delete;

    R rcvr;

    void start() & noexcept {
        std::execution::set_value(std::move(this->rcvr),
            std::execution::get_scheduler(std::execution::get_env(this->rcvr)));
    }
};

struct scheduler_value_sender {
    using sender_concept = std::execution::sender_tag;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t(
            decltype(std::execution::get_scheduler(std::declval<Env>())))> {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R r) const
        -> scheduler_value_op<R> {
        return scheduler_value_op<R>{std::move(r)};
    }

    auto get_env() const noexcept
        -> forge::empty_env {
        return {};
    }
};

struct member_allocator_attrs {
    auto query(std::get_allocator_t) const noexcept
        -> std::allocator<std::byte> {
        return {};
    }
};

struct member_allocator_sender {
    using sender_concept = std::execution::sender_tag;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t()> {
        return {};
    }

    auto get_env() const noexcept -> member_allocator_attrs {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R rcvr) const {
        return std::execution::connect(std::execution::just(), std::move(rcvr));
    }
};

struct scheduler_value_receiver {
    using receiver_concept = std::execution::receiver_tag;

    std::execution::inline_scheduler expected;
    bool* matched;
    bool* completed;

    void set_value(std::execution::inline_scheduler sch) && noexcept {
        *this->matched = (sch == this->expected);
        *this->completed = true;
    }

    template<class E>
    void set_error(E&&) && noexcept {
        *this->completed = false;
    }

    void set_stopped() && noexcept {
        *this->completed = false;
    }

    auto get_env() const noexcept {
        return std::execution::env{
            std::execution::prop(std::execution::get_scheduler_t{}, this->expected)};
    }
};

template<class R>
struct never_started_op {
    using operation_state_concept = std::execution::operation_state_tag;
    void start() & noexcept {}
};

struct throwing_connect_sender {
    using sender_concept = std::execution::sender_tag;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t()> {
        return {};
    }

    auto get_env() const noexcept -> forge::empty_env { return {}; }

    template<std::execution::receiver R>
    auto connect(R) && -> never_started_op<R> {
        throw std::runtime_error{"connect failed"};
    }

    template<std::execution::receiver R>
    auto connect(R) const& -> never_started_op<R> {
        throw std::runtime_error{"connect failed"};
    }
};

struct throwing_scheduler {
    using scheduler_concept = std::execution::scheduler_tag;
    constexpr auto query(std::execution::get_forward_progress_guarantee_t) const noexcept
        -> std::execution::forward_progress_guarantee {
        return std::execution::forward_progress_guarantee::weakly_parallel;
    }

    auto schedule() const noexcept -> throwing_connect_sender {
        return {};
    }

    friend bool operator==(throwing_scheduler, throwing_scheduler) noexcept {
        return true;
    }
};

static_assert(std::execution::scheduler<throwing_scheduler>);

struct mutable_schedule_sender;

struct mutable_schedule_scheduler {
    using scheduler_concept = std::execution::scheduler_tag;
    constexpr auto query(std::execution::get_forward_progress_guarantee_t) const noexcept
        -> std::execution::forward_progress_guarantee {
        return std::execution::forward_progress_guarantee::weakly_parallel;
    }

    int* started = nullptr;

    auto schedule() noexcept -> mutable_schedule_sender;

    friend bool operator==(
        const mutable_schedule_scheduler&,
        const mutable_schedule_scheduler&) noexcept = default;
};

template<class R>
struct mutable_schedule_op {
    using operation_state_concept = std::execution::operation_state_tag;

    mutable_schedule_op(R r, int* count) : rcvr(std::move(r)), started(count) {}
    mutable_schedule_op(const mutable_schedule_op&) = delete;
    mutable_schedule_op& operator=(const mutable_schedule_op&) = delete;

    R rcvr;
    int* started;

    void start() & noexcept {
        if (started) {
            ++*started;
        }
        std::execution::set_value(std::move(rcvr));
    }
};

struct mutable_schedule_sender {
    using sender_concept = std::execution::sender_tag;

    mutable_schedule_scheduler scheduler;

    struct attrs {
        mutable_schedule_scheduler scheduler;

        auto query(std::execution::get_completion_scheduler_t<
                       std::execution::set_value_t>) const noexcept
            -> mutable_schedule_scheduler {
            return scheduler;
        }
    };

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t()> {
        return {};
    }

    auto get_env() const noexcept -> attrs { return {scheduler}; }

    template<std::execution::receiver R>
    auto connect(R r) const -> mutable_schedule_op<R> {
        return mutable_schedule_op<R>{std::move(r), scheduler.started};
    }
};

auto mutable_schedule_scheduler::schedule() noexcept -> mutable_schedule_sender {
    return {*this};
}

static_assert(std::execution::scheduler<mutable_schedule_scheduler>);
static_assert(std::same_as<
    decltype(std::execution::get_completion_scheduler<std::execution::set_value_t>(
        std::execution::get_env(std::declval<const mutable_schedule_sender&>()))),
    mutable_schedule_scheduler>);

template<class Scheduler>
concept const_schedule_callable =
    requires(const Scheduler& scheduler) { scheduler.schedule(); };

template<class Scheduler>
concept rvalue_schedule_callable =
    requires(Scheduler&& scheduler) { std::execution::schedule(std::move(scheduler)); };

static_assert(!const_schedule_callable<mutable_schedule_scheduler>);
static_assert(rvalue_schedule_callable<mutable_schedule_scheduler>);

template<class R>
struct error_stopped_schedule_op {
    using operation_state_concept = std::execution::operation_state_tag;

    R rcvr;

    void start() & noexcept {
        std::execution::set_error(std::move(rcvr), 17);
    }
};

struct error_stopped_scheduler;

struct error_stopped_schedule_sender {
    using sender_concept = std::execution::sender_tag;

    template<class Self, class... Envs>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(),
            std::execution::set_error_t(int),
            std::execution::set_stopped_t()> {
        return {};
    }

    constexpr auto get_env() const noexcept -> error_stopped_scheduler;

    template<std::execution::receiver R>
    auto connect(R r) && -> error_stopped_schedule_op<R> {
        return error_stopped_schedule_op<R>{std::move(r)};
    }

    template<std::execution::receiver R>
    auto connect(R r) const& -> error_stopped_schedule_op<R> {
        return error_stopped_schedule_op<R>{std::move(r)};
    }
};

struct error_stopped_scheduler {
    using scheduler_concept = std::execution::scheduler_tag;
    constexpr auto query(std::execution::get_forward_progress_guarantee_t) const noexcept
        -> std::execution::forward_progress_guarantee {
        return std::execution::forward_progress_guarantee::weakly_parallel;
    }

    constexpr auto schedule() const noexcept -> error_stopped_schedule_sender {
        return {};
    }

    constexpr bool operator==(const error_stopped_scheduler&) const noexcept = default;
};

constexpr auto error_stopped_schedule_sender::get_env() const noexcept -> error_stopped_scheduler {
    return {};
}

static_assert(std::execution::scheduler<error_stopped_scheduler>);
[[maybe_unused]] constexpr auto envless_schedule_signatures =
    error_stopped_schedule_sender::get_completion_signatures<error_stopped_schedule_sender>();
static_assert(scheduler_fixture::agrees_with_schedule(error_stopped_scheduler{}));
static_assert(scheduler_fixture::agrees_with_schedule(error_stopped_scheduler{}, forge::empty_env{}));

struct affine_error_receiver {
    using receiver_concept = std::execution::receiver_tag;

    int* error;

    void set_value(int) && noexcept {
        *error = 0;
    }

    void set_error(int value) && noexcept {
        *error = value;
    }

    void set_error(std::exception_ptr) && noexcept {
        *error = -1;
    }

    void set_stopped() && noexcept {
        *error = -2;
    }

    auto get_env() const noexcept {
        return std::execution::env{std::execution::prop(
            std::execution::get_start_scheduler_t{},
            error_stopped_scheduler{})};
    }
};

struct destination_completion_domain {
    int identity = 0;
};

struct child_completion_domain {
    int identity = 0;
};

struct non_forwarding_child_query {};

struct child_domain_env {
    int identity = 0;

    template<class CPO, class Env>
    auto query(std::execution::get_completion_domain_t<CPO>, const Env&) const noexcept -> child_completion_domain {
        return {this->identity};
    }

    auto query(non_forwarding_child_query) const noexcept -> int {
        return this->identity;
    }
};

struct member_completion_domain_env {
    int identity = 0;

    template<class CPO, class Env>
    auto query(
        std::execution::get_completion_domain_t<CPO>,
        const Env&) const noexcept -> destination_completion_domain {
        return {identity};
    }
};

struct member_domain_env {
    int identity = 0;

    auto query(std::execution::get_domain_t) const noexcept
        -> destination_completion_domain {
        return {identity};
    }
};

struct child_domain_sender {
    using sender_concept = std::execution::sender_tag;

    int identity = 0;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t()> {
        return {};
    }

    auto get_env() const noexcept -> child_domain_env {
        return {identity};
    }

    template<std::execution::receiver R>
    auto connect(R r) && {
        return std::execution::connect(
            std::execution::just(),
            std::move(r));
    }

    template<std::execution::receiver R>
    auto connect(R r) const& {
        return std::execution::connect(
            std::execution::just(),
            std::move(r));
    }
};

struct rvalue_only_let_fn {
    auto operator()(int& value) && noexcept {
        return std::execution::just(value + 1);
    }

    auto operator()(int&) & = delete;
};

struct domain_scheduler {
    using scheduler_concept = std::execution::scheduler_tag;
    constexpr auto query(std::execution::get_forward_progress_guarantee_t) const noexcept
        -> std::execution::forward_progress_guarantee {
        return std::execution::forward_progress_guarantee::weakly_parallel;
    }

    int identity = 0;

    constexpr auto schedule() const noexcept
        -> scheduler_fixture::schedule_sender<domain_scheduler> { return {*this}; }

    constexpr auto query(std::execution::get_completion_scheduler_t<std::execution::set_value_t>)
        const noexcept -> domain_scheduler { return *this; }

    template<class CPO, class Env>
    auto query(std::execution::get_completion_domain_t<CPO>, const Env&) const noexcept -> destination_completion_domain {
        return {this->identity};
    }

    friend constexpr bool operator==(domain_scheduler, domain_scheduler) noexcept =
        default;
};

static_assert(std::execution::scheduler<domain_scheduler>);
static_assert(scheduler_fixture::agrees_with_schedule(domain_scheduler{37}));
static_assert(scheduler_fixture::agrees_with_schedule(domain_scheduler{37}, forge::empty_env{}));
static_assert(scheduler_fixture::agrees_with_schedule(domain_scheduler{37},
    std::execution::__forge_detail::__fwd_env_t<forge::empty_env>::__borrow(forge::empty_env{})));

struct member_domain_scheduler {
    using scheduler_concept = std::execution::scheduler_tag;
    constexpr auto query(std::execution::get_forward_progress_guarantee_t) const noexcept
        -> std::execution::forward_progress_guarantee {
        return std::execution::forward_progress_guarantee::weakly_parallel;
    }

    int identity = 0;

    constexpr auto schedule() const noexcept
        -> scheduler_fixture::schedule_sender<member_domain_scheduler> { return {*this}; }

    constexpr auto query(std::execution::get_completion_scheduler_t<std::execution::set_value_t>)
        const noexcept -> member_domain_scheduler { return *this; }

    template<class CPO, class Env>
    auto query(
        std::execution::get_completion_domain_t<CPO>,
        const Env&) const noexcept -> destination_completion_domain {
        return {identity};
    }

    friend constexpr bool operator==(
        member_domain_scheduler,
        member_domain_scheduler) noexcept = default;
};

static_assert(std::execution::scheduler<member_domain_scheduler>);

struct run_loop_value_only_receiver {
    using receiver_concept = std::execution::receiver_tag;

    std::execution::run_loop* loop;
    bool* completed;

    void set_value() && noexcept {
        *completed = true;
        loop->finish();
    }

    auto get_env() const noexcept -> forge::empty_env {
        return {};
    }
};

} // namespace

TEST(RunLoopTest, UnstoppableEnvironmentOnlyRequiresValueCompletion) {
    std::execution::run_loop loop;
    bool completed = false;
    auto sender = std::execution::schedule(loop.get_scheduler());
    using signatures_t = std::execution::completion_signatures_of_t<
        decltype(sender), forge::empty_env>;
    static_assert(std::same_as<signatures_t,
        std::execution::completion_signatures<std::execution::set_value_t()>>);
    static_assert(std::execution::__forge_sender_to<
        decltype(sender), run_loop_value_only_receiver>);
    auto op = std::execution::connect(
        std::move(sender), run_loop_value_only_receiver{&loop, &completed});

    std::execution::start(op);
    loop.run();

    EXPECT_TRUE(completed);
}

TEST(IntoVariantTest, WrapsValue) {
    auto sndr = std::execution::into_variant(std::execution::just(42));
    using var_t = std::variant<std::tuple<int>>;
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, forge::empty_env{}));
    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(var_t)>>);

    auto result = forge::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    auto& var = std::get<0>(*result);
    EXPECT_EQ(std::get<std::tuple<int>>(var), std::make_tuple(42));
}

TEST(IntoVariantTest, SupportsPipeForm) {
    auto result = forge::sync_wait(
        std::execution::just(42) | std::execution::into_variant);

    ASSERT_TRUE(result.has_value());
    auto& var = std::get<0>(*result);
    EXPECT_EQ(std::get<std::tuple<int>>(var), std::make_tuple(42));
}

TEST(IntoVariantTest, ReportsConstructionFailureAsError) {
    auto sndr = std::execution::into_variant(throwing_value_sender{});
    using var_t = std::variant<std::tuple<throwing_move_value>>;
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, forge::empty_env{}));
    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(var_t),
            std::execution::set_error_t(std::exception_ptr)>>);

    EXPECT_THROW(forge::sync_wait(std::move(sndr)), std::runtime_error);
}

TEST(IntoVariantTest, StoppedOnlySenderDoesNotInstantiateEmptyVariant) {
    auto sndr = std::execution::into_variant(std::execution::just_stopped());
    auto result = forge::sync_wait(std::move(sndr));

    EXPECT_FALSE(result.has_value());
}

TEST(IntoVariantTest, ErrorOnlySenderDoesNotInstantiateEmptyVariant) {
    auto sndr = std::execution::into_variant(std::execution::just_error(42));

    EXPECT_THROW((void)forge::sync_wait(std::move(sndr)), int);
}

TEST(IntoVariantTest, DoesNotDuplicateExceptionPtrErrorSignature) {
    auto sndr = std::execution::into_variant(
        std::execution::just_error(std::exception_ptr{}));
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, forge::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_error_t(std::exception_ptr)>>);
}

TEST(SyncWaitWithVariantTest, Works) {
    auto result = std::this_thread::sync_wait_with_variant(std::execution::just(42));
    static_assert(std::is_same_v<
        decltype(result),
        std::optional<std::variant<std::tuple<int>>>>);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<std::tuple<int>>(*result), std::make_tuple(42));
}

TEST(SyncWaitWithVariantTest, SupportsStoppedOnlySender) {
    auto result = std::this_thread::sync_wait_with_variant(
        std::execution::just_stopped());

    EXPECT_FALSE(result.has_value());
}

TEST(SyncWaitWithVariantTest, SupportsErrorOnlySender) {
    EXPECT_THROW(
        (void)std::this_thread::sync_wait_with_variant(
            std::execution::just_error(42)),
        int);
}

TEST(SyncWaitWithVariantTest, SuppliesTheStartScheduler) {
    auto result = std::this_thread::sync_wait_with_variant(
        std::execution::on(
            std::execution::inline_scheduler{},
            std::execution::just(42)));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<std::tuple<int>>(*result), std::make_tuple(42));
}

TEST(BulkTest, SerialExecution) {
    int sum = 0;
    auto sndr = std::execution::just(0)
              | forge::bulk(5, [&sum](int idx, int& v) {
                    sum += idx; v += idx;
                });
    auto result = forge::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(sum, 0+1+2+3+4);
    EXPECT_EQ(std::get<0>(*result), 0+1+2+3+4);
}

TEST(StartDetachedTest, Executes) {
    std::atomic<int> counter{0};
    forge::start_detached(
        std::execution::just() | std::execution::then([&counter] { counter++; }));
    EXPECT_EQ(counter.load(), 1);
}

TEST(StartDetachedTest, HandlesSynchronousWhenAllCompletion) {
    std::atomic<int> counter{0};
    forge::start_detached(
        std::execution::when_all(std::execution::just(), std::execution::just())
        | std::execution::then([&counter] { counter.fetch_add(1, std::memory_order_relaxed); }));
    EXPECT_EQ(counter.load(), 1);
}

TEST(StartDetachedTest, HandlesSynchronousStartsOnCompletion) {
    std::atomic<int> counter{0};
    std::execution::inline_scheduler sch;
    forge::start_detached(
        std::execution::starts_on(
            sch,
            std::execution::just()
            | std::execution::then([&counter] { counter.fetch_add(1, std::memory_order_relaxed); })));
    EXPECT_EQ(counter.load(), 1);
}

TEST(StartsOnTest, SupportsMutableLvalueSchedule) {
    int started = 0;
    int observed_starts = 0;
    mutable_schedule_scheduler scheduler{&started};
    auto schedule_sender = std::execution::schedule(scheduler);
    EXPECT_EQ(
        std::execution::get_completion_scheduler<std::execution::set_value_t>(
            std::execution::get_env(schedule_sender)),
        scheduler);

    auto sender = std::execution::starts_on(
        scheduler,
        std::execution::just(42) | std::execution::then([&](int value) {
            observed_starts = started;
            return value;
        }));
    EXPECT_EQ(started, 0);

    auto result = forge::sync_wait(std::move(sender));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(started, 1);
    EXPECT_EQ(observed_starts, 1);
}

TEST(OnTest, SupportsMutableLvalueSchedule) {
    int started = 0;
    int observed_starts = 0;
    auto sender = std::execution::on(
        mutable_schedule_scheduler{&started},
        std::execution::just(42) | std::execution::then([&](int value) {
            observed_starts = started;
            return value;
        }));
    EXPECT_EQ(started, 0);

    auto result = forge::sync_wait(std::move(sender));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(started, 1);
    EXPECT_EQ(observed_starts, 1);
}

TEST(StartsOnTest, SourceConnectFailureIsAnErrorCompletionAfterStart) {
    std::execution::inline_scheduler sch;
    auto sndr = std::execution::starts_on(sch, throwing_connect_sender{});
    using cs_t = std::execution::completion_signatures_of_t<
        decltype(sndr),
        forge::empty_env>;
    static_assert(std::is_same_v<
        cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(),
            std::execution::set_error_t(std::exception_ptr)>>);

    EXPECT_THROW(
        (void)forge::sync_wait(std::move(sndr)),
        std::runtime_error);
}

TEST(StartsOnTest, DeclaresOnlySchedulerErrors) {
    auto sndr = std::execution::starts_on(
        error_stopped_scheduler{},
        std::execution::just(42));
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, forge::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_error_t(int),
            std::execution::set_stopped_t()>>);

    try {
        (void)forge::sync_wait(std::move(sndr));
        FAIL() << "expected scheduler set_error(int) to propagate";
    } catch (int value) {
        EXPECT_EQ(value, 17);
    }
}

TEST(StartDetachedTest, HandlesSynchronousContinuesOnCompletion) {
    std::atomic<int> counter{0};
    std::execution::inline_scheduler sch;
    forge::start_detached(
        std::execution::continues_on(
            std::execution::just()
            | std::execution::then([&counter] { counter.fetch_add(1, std::memory_order_relaxed); }),
            sch));
    EXPECT_EQ(counter.load(), 1);
}

TEST(ContinuesOnTest, TransfersToScheduler) {
    std::execution::run_loop loop;
    auto sch = loop.get_scheduler();
    int result = -1;
    std::thread worker([&loop] { loop.run(); });

    auto sndr = std::execution::continues_on(
                  std::execution::just(42) | std::execution::then([&result](int x) { result = x; }),
                  sch);
    forge::sync_wait(std::move(sndr));

    loop.finish();
    worker.join();
    EXPECT_EQ(result, 42);
}

TEST(ContinuesOnTest, ReportsDestinationSchedulerOnlyForTransferredDispositions) {
    std::execution::run_loop source_loop;
    std::execution::run_loop destination_loop;
    auto source = source_loop.get_scheduler();
    auto destination = destination_loop.get_scheduler();
    auto sndr = std::execution::continues_on(
        std::execution::schedule(source),
        destination);
    auto env = std::execution::get_env(sndr);

    EXPECT_TRUE(
        std::execution::get_completion_scheduler<std::execution::set_value_t>(env)
        == destination);
    static_assert(!std::invocable<
        std::execution::get_completion_scheduler_t<std::execution::set_error_t>,
        const decltype(env)&>);
    static_assert(!std::invocable<
        std::execution::get_completion_scheduler_t<std::execution::set_stopped_t>,
        const decltype(env)&>);
    auto stoppable = std::execution::prop{std::get_stop_token, std::inplace_stop_token{}};
    EXPECT_TRUE(std::execution::get_completion_scheduler<std::execution::set_stopped_t>(
        env, stoppable) == destination);
}

TEST(ContinuesOnTest, PreservesMemberQueriedChildAttributes) {
    auto sndr = std::execution::continues_on(
        member_allocator_sender{},
        std::execution::inline_scheduler{});
    auto attrs = std::execution::get_env(sndr);

    auto allocator = std::get_allocator(attrs);

    static_assert(std::same_as<decltype(allocator), std::allocator<std::byte>>);
}

TEST(ContinuesOnTest, SupportsMutableLvalueSchedule) {
    auto sender = std::execution::continues_on(
        std::execution::just(42),
        mutable_schedule_scheduler{});

    auto result = forge::sync_wait(std::move(sender));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(ContinuesOnTest, ReportsDestinationCompletionDomain) {
    auto sndr = std::execution::continues_on(
        std::execution::just(),
        domain_scheduler{37});
    auto attrs = std::execution::get_env(sndr);

    auto domain = std::execution::get_completion_domain<>(
        attrs,
        forge::empty_env{});

    EXPECT_EQ(domain.identity, 0);
    static_assert(!std::execution::__forge_env_detail::__member_query<
        std::execution::get_completion_domain_t<std::execution::set_error_t>,
        const decltype(attrs)&, forge::empty_env>);
}

TEST(ContinuesOnTest, ReportsDestinationStoppedScheduler) {
    auto sndr = std::execution::continues_on(
        std::execution::just_stopped(),
        domain_scheduler{41});
    auto attrs = std::execution::get_env(sndr);

    auto scheduler = std::execution::get_completion_scheduler<
        std::execution::set_stopped_t>(attrs);

    EXPECT_EQ(scheduler.identity, 41);
    static_assert(!std::execution::__forge_env_detail::__queryable<
                  std::execution::get_completion_scheduler_t<
                      std::execution::set_error_t>,
                  const decltype(attrs)&>);
}

TEST(ContinuesOnTest, FiltersNonForwardingChildAttributes) {
    auto sndr = std::execution::continues_on(
        child_domain_sender{29},
        std::execution::inline_scheduler{});
    using attrs_t = std::execution::env_of_t<decltype(sndr)>;

    static_assert(!std::execution::__forge_env_detail::__queryable<
                  non_forwarding_child_query,
                  const attrs_t&>);
}

TEST(CompletionDomainTest, SupportsMemberQueryProtocol) {
    auto domain = std::execution::get_completion_domain<
        std::execution::set_value_t>(
        member_completion_domain_env{43},
        forge::empty_env{});

    EXPECT_EQ(domain.identity, 0);
}

TEST(CompletionDomainTest, GetDomainSupportsMemberQueryProtocol) {
    auto domain = std::execution::get_domain(member_domain_env{47});

    EXPECT_EQ(domain.identity, 0);
}

TEST(ContinuesOnTest, UsesMemberQueriedDestinationDomain) {
    constexpr member_domain_scheduler target{53};
    static_assert(scheduler_fixture::agrees_with_schedule(target));
    static_assert(scheduler_fixture::agrees_with_schedule(target, forge::empty_env{}));
    static_assert(scheduler_fixture::agrees_with_schedule(target,
        std::execution::__forge_detail::__fwd_env_t<forge::empty_env>::__borrow(forge::empty_env{})));
    auto sndr = std::execution::continues_on(std::execution::just(), target);
    auto attrs = std::execution::get_env(sndr);

    auto domain = std::execution::get_completion_domain<>(
        attrs,
        forge::empty_env{});

    EXPECT_EQ(domain.identity, 0);
}

TEST(ContinuesOnTest, DoesNotLeakChildDomainWhenDestinationHasNone) {
    auto sndr = std::execution::continues_on(
        child_domain_sender{29},
        std::execution::inline_scheduler{});
    auto attrs = std::execution::get_env(sndr);

    static_assert(!std::execution::__forge_env_detail::__member_query<
        std::execution::get_completion_domain_t<>,
        const decltype(attrs)&, forge::empty_env>);
}

TEST(ContinuesOnTest, ScheduleConnectFailurePropagates) {
    auto sndr = std::execution::continues_on(
        std::execution::just(),
        throwing_scheduler{});

    EXPECT_THROW(
        (void)forge::sync_wait(std::move(sndr)),
        std::runtime_error);
}

TEST(LetValueTest, InnerSenderSeesChildCompletionScheduler) {
    std::execution::run_loop child_loop;
    auto child_scheduler = child_loop.get_scheduler();
    std::thread child_worker([&] { child_loop.run(); });

    auto sender = std::execution::let_value(
        std::execution::schedule(child_scheduler),
        []() noexcept {
            return std::execution::read_env(
                std::execution::get_start_scheduler);
        });
    auto result = forge::sync_wait(std::move(sender));

    std::execution::run_loop receiver_loop;
    auto receiver_scheduler = receiver_loop.get_scheduler();
    // [exec.let]/8.2 forwards the receiver's get_scheduler after SCHED-ENV.
    auto forwarding_sender = std::execution::let_value(
        std::execution::schedule(child_scheduler),
        []() noexcept {
            return std::execution::read_env(std::execution::get_scheduler);
        });
    auto forwarding_result = forge::sync_wait(std::execution::write_env(
        std::move(forwarding_sender),
        std::execution::prop{std::execution::get_scheduler, receiver_scheduler}));

    child_loop.finish();
    child_worker.join();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), child_scheduler);
    ASSERT_TRUE(forwarding_result.has_value());
    EXPECT_EQ(std::get<0>(*forwarding_result), receiver_scheduler);
}

TEST(LetValueTest, InnerSenderSeesDefaultConstructedChildCompletionDomain) {
    auto sender = std::execution::let_value(
        child_domain_sender{41},
        []() noexcept {
            return std::execution::read_env(std::execution::get_domain);
        });
    auto result = forge::sync_wait(std::move(sender));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result).identity, 0);
}

TEST(LetValueTest, InvokesStoredFunctionAsRvalue) {
    auto result = forge::sync_wait(std::execution::let_value(
        std::execution::just(41),
        rvalue_only_let_fn{}));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(ContinuesOnTest, DeclaresSchedulerErrorsWithoutSetupError) {
    auto sndr = std::execution::continues_on(
        std::execution::just(42),
        error_stopped_scheduler{});
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, forge::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_error_t(int),
            std::execution::set_stopped_t()>>);

    try {
        (void)forge::sync_wait(std::move(sndr));
        FAIL() << "expected scheduler set_error(int) to propagate";
    } catch (int value) {
        EXPECT_EQ(value, 17);
    }
}

TEST(AffineTest, RejectsFallibleScheduler) {
    using sender_t = decltype(std::execution::affine(std::execution::just(7)));
    using env_t = decltype(std::declval<affine_error_receiver>().get_env());
    static_assert(!std::execution::sender_in<sender_t, env_t>);
    SUCCEED();
}

TEST(UnstoppableTest, WrappedSchedulerErrorStillPropagates) {
    auto sndr = std::execution::unstoppable(
        std::execution::continues_on(
            std::execution::just(8),
            error_stopped_scheduler{}));
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, forge::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_error_t(int),
            std::execution::set_stopped_t()>>);

    try {
        (void)forge::sync_wait(std::move(sndr));
        FAIL() << "expected scheduler set_error(int) to propagate";
    } catch (int value) {
        EXPECT_EQ(value, 17);
    }
}

TEST(ContinuesOnTest, UsesConnectedReceiverEnvForUpstreamSignatures) {
    std::execution::inline_scheduler sch;
    bool matched = false;
    bool completed = false;

    auto op = std::execution::connect(
        std::execution::continues_on(scheduler_value_sender{}, sch),
        scheduler_value_receiver{sch, &matched, &completed});
    std::execution::start(op);

    EXPECT_TRUE(completed);
    EXPECT_TRUE(matched);
}

TEST(ContinuesOnTest, TransfersErrorToScheduler) {
    std::execution::run_loop loop;
    auto sch = loop.get_scheduler();
    std::promise<std::thread::id> worker_id_promise;
    auto worker_id_future = worker_id_promise.get_future();
    std::thread worker([&] {
        worker_id_promise.set_value(std::this_thread::get_id());
        loop.run();
    });
    auto worker_id = worker_id_future.get();
    std::thread::id observed_id;

    auto sndr = std::execution::continues_on(std::execution::just_error(42), sch)
              | std::execution::upon_error([&](auto error) {
                    using error_t = std::remove_cvref_t<decltype(error)>;
                    if constexpr (std::same_as<error_t, int>) {
                        observed_id = std::this_thread::get_id();
                        EXPECT_EQ(error, 42);
                    } else {
                        FAIL() << "unexpected scheduler error";
                    }
                });
    auto result = forge::sync_wait(std::move(sndr));

    loop.finish();
    worker.join();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(observed_id, worker_id);
}

TEST(ContinuesOnTest, TransfersStoppedToScheduler) {
    std::execution::run_loop loop;
    auto sch = loop.get_scheduler();
    std::promise<std::thread::id> worker_id_promise;
    auto worker_id_future = worker_id_promise.get_future();
    std::thread worker([&] {
        worker_id_promise.set_value(std::this_thread::get_id());
        loop.run();
    });
    auto worker_id = worker_id_future.get();
    std::thread::id observed_id;

    auto sndr = std::execution::continues_on(std::execution::just_stopped(), sch)
              | std::execution::upon_stopped([&] {
                    observed_id = std::this_thread::get_id();
                });
    auto result = forge::sync_wait(std::move(sndr));

    loop.finish();
    worker.join();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(observed_id, worker_id);
}

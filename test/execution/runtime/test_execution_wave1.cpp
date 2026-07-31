#include <gtest/gtest.h>
#include <forge/start_detached.hpp>
#include <execution>
#include <atomic>
#include <future>
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
    using operation_state_concept = std::execution::operation_state_t;

    explicit throwing_value_op(R r) : rcvr(std::move(r)) {}
    throwing_value_op(const throwing_value_op&) = delete;
    throwing_value_op& operator=(const throwing_value_op&) = delete;
    throwing_value_op(throwing_value_op&&) = delete;
    throwing_value_op& operator=(throwing_value_op&&) = delete;

    R rcvr;

    friend void tag_invoke(std::execution::start_t, throwing_value_op& self) noexcept {
        std::execution::set_value(std::move(self.rcvr), throwing_move_value{});
    }
};

struct throwing_value_sender {
    using sender_concept = std::execution::sender_t;

    friend auto tag_invoke(std::execution::get_completion_signatures_t,
                           const throwing_value_sender&, auto) noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(throwing_move_value)> {
        return {};
    }

    template<std::execution::receiver R>
    friend auto tag_invoke(std::execution::connect_t, throwing_value_sender, R r)
        -> throwing_value_op<R> {
        return throwing_value_op<R>{std::move(r)};
    }

    friend auto tag_invoke(std::execution::get_env_t, const throwing_value_sender&) noexcept
        -> std::execution::empty_env {
        return {};
    }
};

template<class R>
struct scheduler_value_op {
    using operation_state_concept = std::execution::operation_state_t;

    explicit scheduler_value_op(R r) : rcvr(std::move(r)) {}
    scheduler_value_op(const scheduler_value_op&) = delete;
    scheduler_value_op& operator=(const scheduler_value_op&) = delete;

    R rcvr;

    friend void tag_invoke(std::execution::start_t, scheduler_value_op& self) noexcept {
        std::execution::set_value(std::move(self.rcvr),
            std::execution::get_scheduler(std::execution::get_env(self.rcvr)));
    }
};

struct scheduler_value_sender {
    using sender_concept = std::execution::sender_t;

    template<class Env>
    friend auto tag_invoke(std::execution::get_completion_signatures_t,
                           const scheduler_value_sender&, Env&&) noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t(
            decltype(std::execution::get_scheduler(std::declval<Env>())))> {
        return {};
    }

    template<std::execution::receiver R>
    friend auto tag_invoke(std::execution::connect_t, scheduler_value_sender, R r)
        -> scheduler_value_op<R> {
        return scheduler_value_op<R>{std::move(r)};
    }

    friend auto tag_invoke(std::execution::get_env_t, const scheduler_value_sender&) noexcept
        -> std::execution::empty_env {
        return {};
    }
};

struct scheduler_value_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::execution::inline_scheduler expected;
    bool* matched;
    bool* completed;

    friend void tag_invoke(std::execution::set_value_t, scheduler_value_receiver&& self,
                           std::execution::inline_scheduler sch) noexcept {
        *self.matched = (sch == self.expected);
        *self.completed = true;
    }

    template<class E>
    friend void tag_invoke(std::execution::set_error_t, scheduler_value_receiver&& self, E&&) noexcept {
        *self.completed = false;
    }

    friend void tag_invoke(std::execution::set_stopped_t, scheduler_value_receiver&& self) noexcept {
        *self.completed = false;
    }

    friend auto tag_invoke(std::execution::get_env_t, const scheduler_value_receiver& self) noexcept {
        return std::execution::make_env(
            std::execution::make_prop(std::execution::get_scheduler_t{}, self.expected));
    }
};

template<class R>
struct never_started_op {
    using operation_state_concept = std::execution::operation_state_t;
    void start() & noexcept {}
};

struct throwing_connect_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(),
            std::execution::set_error_t(std::exception_ptr)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

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
    using scheduler_concept = std::execution::scheduler_t;

    auto schedule() const noexcept -> throwing_connect_sender {
        return {};
    }

    friend bool operator==(throwing_scheduler, throwing_scheduler) noexcept {
        return true;
    }
};

static_assert(std::execution::scheduler<throwing_scheduler>);

struct mutable_schedule_scheduler {
    using scheduler_concept = std::execution::scheduler_t;

    auto schedule() & noexcept {
        return std::execution::just();
    }

    friend bool operator==(
        const mutable_schedule_scheduler&,
        const mutable_schedule_scheduler&) noexcept = default;
};

static_assert(std::execution::scheduler<mutable_schedule_scheduler>);

template<class Scheduler>
concept const_schedule_callable =
    requires(const Scheduler& scheduler) { scheduler.schedule(); };

static_assert(!const_schedule_callable<mutable_schedule_scheduler>);

template<class R>
struct error_stopped_schedule_op {
    using operation_state_concept = std::execution::operation_state_t;

    R rcvr;

    void start() & noexcept {
        std::execution::set_error(std::move(rcvr), 17);
    }
};

struct error_stopped_schedule_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(),
            std::execution::set_error_t(int),
            std::execution::set_stopped_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

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
    using scheduler_concept = std::execution::scheduler_t;

    auto schedule() const noexcept -> error_stopped_schedule_sender {
        return {};
    }

    friend bool operator==(error_stopped_scheduler, error_stopped_scheduler) noexcept {
        return true;
    }
};

static_assert(std::execution::scheduler<error_stopped_scheduler>);

struct destination_completion_domain {
    int identity = 0;
};

struct child_completion_domain {
    int identity = 0;
};

struct child_domain_env {
    int identity = 0;

    template<class CPO, class Env>
    friend auto tag_invoke(
        std::execution::get_completion_domain_t<CPO>,
        const child_domain_env& self,
        const Env&) noexcept -> child_completion_domain {
        return {self.identity};
    }
};

struct child_domain_sender {
    using sender_concept = std::execution::sender_t;

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

struct domain_scheduler {
    using scheduler_concept = std::execution::scheduler_t;

    int identity = 0;

    auto schedule() const noexcept {
        return std::execution::just();
    }

    template<class CPO, class Env>
    friend auto tag_invoke(
        std::execution::get_completion_domain_t<CPO>,
        domain_scheduler self,
        const Env&) noexcept -> destination_completion_domain {
        return {self.identity};
    }

    friend bool operator==(domain_scheduler, domain_scheduler) noexcept =
        default;
};

static_assert(std::execution::scheduler<domain_scheduler>);

} // namespace

TEST(IntoVariantTest, WrapsValue) {
    auto sndr = std::execution::into_variant(std::execution::just(42));
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    using var_t = std::variant<std::tuple<int>>;
    auto& var = std::get<0>(*result);
    EXPECT_EQ(std::get<std::tuple<int>>(var), std::make_tuple(42));
}

TEST(IntoVariantTest, SupportsPipeForm) {
    auto result = std::execution::sync_wait(
        std::execution::just(42) | std::execution::into_variant());

    ASSERT_TRUE(result.has_value());
    auto& var = std::get<0>(*result);
    EXPECT_EQ(std::get<std::tuple<int>>(var), std::make_tuple(42));
}

TEST(IntoVariantTest, ReportsConstructionFailureAsError) {
    auto sndr = std::execution::into_variant(throwing_value_sender{});
    EXPECT_THROW(std::execution::sync_wait(std::move(sndr)), std::runtime_error);
}

TEST(IntoVariantTest, StoppedOnlySenderDoesNotInstantiateEmptyVariant) {
    auto sndr = std::execution::into_variant(std::execution::just_stopped());
    auto result = std::execution::sync_wait(std::move(sndr));

    EXPECT_FALSE(result.has_value());
}

TEST(IntoVariantTest, ErrorOnlySenderDoesNotInstantiateEmptyVariant) {
    auto sndr = std::execution::into_variant(std::execution::just_error(42));

    EXPECT_THROW((void)std::execution::sync_wait(std::move(sndr)), int);
}

TEST(IntoVariantTest, DoesNotDuplicateExceptionPtrErrorSignature) {
    auto sndr = std::execution::into_variant(
        std::execution::just_error(std::exception_ptr{}));
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));

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
              | std::execution::bulk(5, [&sum](int idx, int& v) {
                    sum += idx; v += idx;
                });
    auto result = std::execution::sync_wait(std::move(sndr));
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

TEST(StartsOnTest, SourceConnectFailureBecomesError) {
    std::execution::inline_scheduler sch;
    auto sndr = std::execution::starts_on(sch, throwing_connect_sender{});

    EXPECT_THROW(
        (void)std::execution::sync_wait(std::move(sndr)),
        std::runtime_error);
}

TEST(StartsOnTest, DeclaresSchedulerErrorsAndSetupError) {
    auto sndr = std::execution::starts_on(
        error_stopped_scheduler{},
        std::execution::just(42));
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_error_t(int),
            std::execution::set_stopped_t(),
            std::execution::set_error_t(std::exception_ptr)>>);

    try {
        (void)std::execution::sync_wait(std::move(sndr));
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
    std::execution::sync_wait(std::move(sndr));

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
    static_assert(!std::execution::__forge_detail::tag_invocable<
        std::execution::get_completion_scheduler_t<std::execution::set_error_t>,
        const decltype(env)&>);
    static_assert(!std::execution::__forge_detail::tag_invocable<
        std::execution::get_completion_scheduler_t<std::execution::set_stopped_t>,
        const decltype(env)&>);
}

TEST(ContinuesOnTest, SupportsMutableLvalueSchedule) {
    auto sender = std::execution::continues_on(
        std::execution::just(42),
        mutable_schedule_scheduler{});

    auto result = std::execution::sync_wait(std::move(sender));

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
        std::execution::empty_env{});

    EXPECT_EQ(domain.identity, 37);
    static_assert(!std::execution::__forge_detail::tag_invocable<
        std::execution::get_completion_domain_t<std::execution::set_error_t>,
        const decltype(attrs)&,
        std::execution::empty_env>);
}

TEST(ContinuesOnTest, PreservesChildDomainWhenDestinationHasNone) {
    auto sndr = std::execution::continues_on(
        child_domain_sender{29},
        std::execution::inline_scheduler{});
    auto attrs = std::execution::get_env(sndr);

    auto domain = std::execution::get_completion_domain<>(
        attrs,
        std::execution::empty_env{});

    EXPECT_EQ(domain.identity, 29);
}

TEST(AffineTest, ReportsDestinationCompletionDomain) {
    auto sndr = std::execution::affine(
        std::execution::just(),
        domain_scheduler{41});
    auto attrs = std::execution::get_env(sndr);

    auto domain = std::execution::get_completion_domain<>(
        attrs,
        std::execution::empty_env{});

    EXPECT_EQ(domain.identity, 41);
}

TEST(ContinuesOnTest, ScheduleConnectFailureBecomesError) {
    auto sndr = std::execution::continues_on(
        std::execution::just(),
        throwing_scheduler{});

    EXPECT_THROW(
        (void)std::execution::sync_wait(std::move(sndr)),
        std::runtime_error);
}

TEST(ContinuesOnTest, DeclaresSchedulerErrorsAndSetupError) {
    auto sndr = std::execution::continues_on(
        std::execution::just(42),
        error_stopped_scheduler{});
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_error_t(int),
            std::execution::set_stopped_t(),
            std::execution::set_error_t(std::exception_ptr)>>);

    try {
        (void)std::execution::sync_wait(std::move(sndr));
        FAIL() << "expected scheduler set_error(int) to propagate";
    } catch (int value) {
        EXPECT_EQ(value, 17);
    }
}

TEST(AffineTest, DeclaresSchedulerErrorsAndSetupError) {
    auto sndr = std::execution::affine(
        std::execution::just(7),
        error_stopped_scheduler{});
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_error_t(int),
            std::execution::set_stopped_t(),
            std::execution::set_error_t(std::exception_ptr)>>);

    try {
        (void)std::execution::sync_wait(std::move(sndr));
        FAIL() << "expected scheduler set_error(int) to propagate";
    } catch (int value) {
        EXPECT_EQ(value, 17);
    }
}

TEST(UnstoppableTest, WrappedSchedulerErrorStillPropagates) {
    auto sndr = std::execution::unstoppable(
        std::execution::continues_on(
            std::execution::just(8),
            error_stopped_scheduler{}));
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_error_t(int),
            std::execution::set_stopped_t(),
            std::execution::set_error_t(std::exception_ptr)>>);

    try {
        (void)std::execution::sync_wait(std::move(sndr));
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
    auto result = std::execution::sync_wait(std::move(sndr));

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
    auto result = std::execution::sync_wait(std::move(sndr));

    loop.finish();
    worker.join();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(observed_id, worker_id);
}

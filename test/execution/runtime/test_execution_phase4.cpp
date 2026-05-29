#include <gtest/gtest.h>
#include <execution>
#include <forge/any_sender.hpp>
#include <thread>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>

namespace {

struct tracking_domain {
    static inline bool transformed = false;

    template<class Sender, class Env>
    Sender&& transform_sender(Sender&& sndr, const Env&) const noexcept {
        transformed = true;
        return static_cast<Sender&&>(sndr);
    }
};

struct tracking_env {
    friend auto tag_invoke(std::execution::get_domain_t, const tracking_env&) noexcept
        -> tracking_domain {
        return {};
    }
};

template<class R>
struct tracking_op {
    using operation_state_concept = std::execution::operation_state_t;

    R rcvr;

    explicit tracking_op(R r) : rcvr(std::move(r)) {}
    tracking_op(const tracking_op&) = delete;
    tracking_op& operator=(const tracking_op&) = delete;

    friend void tag_invoke(std::execution::start_t, tracking_op& self) noexcept {
        std::execution::set_value(std::move(self.rcvr), 42);
    }
};

struct tracking_sender {
    using sender_concept = std::execution::sender_t;

    friend auto tag_invoke(std::execution::get_completion_signatures_t,
                           const tracking_sender&, auto) noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t(int)> {
        return {};
    }

    template<std::execution::receiver R>
    friend auto tag_invoke(std::execution::connect_t, tracking_sender, R r)
        -> tracking_op<R> {
        return tracking_op<R>{std::move(r)};
    }

    friend auto tag_invoke(std::execution::get_env_t, const tracking_sender&) noexcept
        -> tracking_env {
        return {};
    }
};

struct scope_probe_receiver {
    using receiver_concept = std::execution::receiver_t;

    bool* completed = nullptr;

    void set_value() && noexcept {
        if (completed) *completed = true;
    }

    template<class E>
    void set_error(E&&) && noexcept {}

    void set_stopped() && noexcept {}

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

struct pending_sender {
    using sender_concept = std::execution::sender_t;

    bool* started = nullptr;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;
        bool* started;

        void start() & noexcept {
            if (started) *started = true;
        }
    };

    template<std::execution::receiver R>
    auto connect(R r) && -> op<R> {
        return op<R>{std::move(r), started};
    }

    template<std::execution::receiver R>
    auto connect(R r) const& -> op<R> {
        return op<R>{std::move(r), started};
    }
};

struct spawn_future_marker_error {};

struct spawn_future_manual_state {
    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool stop_requested = false;
    bool completed = false;
    std::function<void(int)> complete_value;
};

template<class R>
struct spawn_future_manual_op {
    using operation_state_concept = std::execution::operation_state_t;

    struct stop_callback {
        spawn_future_manual_op* self;

        void operator()() noexcept {
            self->complete_stopped();
        }
    };

    using env_t = std::execution::env_of_t<R>;
    using token_t = decltype(std::execution::get_stop_token(std::declval<env_t>()));
    using callback_t = std::stop_callback_for_t<token_t, stop_callback>;

    R rcvr;
    std::shared_ptr<spawn_future_manual_state> state;
    std::optional<callback_t> callback;
    std::atomic<bool> done{false};

    void start() & noexcept {
        auto token = std::execution::get_stop_token(std::execution::get_env(rcvr));
        {
            std::lock_guard lk{state->mtx};
            state->started = true;
            state->complete_value = [this](int value) noexcept {
                complete_value(value);
            };
        }
        state->cv.notify_all();

        if (token.stop_possible()) {
            callback.emplace(token, stop_callback{this});
        }
    }

    void complete_value(int value) noexcept {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        {
            std::lock_guard lk{state->mtx};
            state->completed = true;
            state->complete_value = {};
        }
        state->cv.notify_all();
        callback.reset();
        std::execution::set_value(std::move(rcvr), value);
    }

    void complete_stopped() noexcept {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        {
            std::lock_guard lk{state->mtx};
            state->stop_requested = true;
            state->completed = true;
            state->complete_value = {};
        }
        state->cv.notify_all();
        std::execution::set_stopped(std::move(rcvr));
    }
};

struct spawn_future_manual_sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<spawn_future_manual_state> state;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_stopped_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R r) && -> spawn_future_manual_op<R> {
        return spawn_future_manual_op<R>{std::move(r), std::move(state)};
    }

    template<std::execution::receiver R>
    auto connect(R r) const& -> spawn_future_manual_op<R> {
        return spawn_future_manual_op<R>{std::move(r), state};
    }
};

struct spawn_future_stop_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::inplace_stop_source* source = nullptr;
    std::atomic<bool>* stopped = nullptr;

    void set_value(int) && noexcept {}

    template<class E>
    void set_error(E&&) && noexcept {}

    void set_stopped() && noexcept {
        stopped->store(true, std::memory_order_release);
    }

    auto get_env() const noexcept {
        return std::execution::make_env(
            std::execution::make_prop(
                std::execution::get_stop_token_t{}, source->get_token()));
    }
};

bool wait_until_started(const std::shared_ptr<spawn_future_manual_state>& state) {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, std::chrono::seconds(2), [&] {
        return state->started;
    });
}

bool wait_until_completed(const std::shared_ptr<spawn_future_manual_state>& state) {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, std::chrono::seconds(2), [&] {
        return state->completed;
    });
}

bool wait_until_stop_requested(const std::shared_ptr<spawn_future_manual_state>& state) {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, std::chrono::seconds(2), [&] {
        return state->stop_requested;
    });
}

void complete_manual_value(const std::shared_ptr<spawn_future_manual_state>& state, int value) {
    std::function<void(int)> complete;
    {
        std::lock_guard lk{state->mtx};
        complete = state->complete_value;
    }
    ASSERT_TRUE(static_cast<bool>(complete));
    complete(value);
}

} // namespace

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

TEST(DefaultDomainTest, ConnectUsesSenderDomainTransform) {
    tracking_domain::transformed = false;

    auto result = std::execution::sync_wait(tracking_sender{});

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_TRUE(tracking_domain::transformed);
}

// ─── T7: counting_scope tests ───────────────────────────────────────────────

TEST(SimpleCountingScopeTest, AssociationLifecycle) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    using association_t = decltype(token.try_associate());

    static_assert(std::execution::scope_association<association_t>);
    static_assert(std::execution::scope_token<decltype(token)>);

    association_t empty;
    EXPECT_FALSE(static_cast<bool>(empty));
    EXPECT_FALSE(static_cast<bool>(empty.try_associate()));
    EXPECT_EQ(scope.count(), 0u);

    {
        auto assoc = token.try_associate();
        EXPECT_TRUE(static_cast<bool>(assoc));
        EXPECT_EQ(scope.count(), 1u);

        {
            auto nested = assoc.try_associate();
            EXPECT_TRUE(static_cast<bool>(nested));
            EXPECT_EQ(scope.count(), 2u);
        }

        EXPECT_EQ(scope.count(), 1u);
    }

    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, AssociationMoveTransfersOwnership) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    using association_t = decltype(token.try_associate());

    {
        auto first = token.try_associate();
        association_t second = std::move(first);

        EXPECT_FALSE(static_cast<bool>(first));
        EXPECT_TRUE(static_cast<bool>(second));
        EXPECT_EQ(scope.count(), 1u);

        auto third = token.try_associate();
        EXPECT_EQ(scope.count(), 2u);

        third = std::move(second);
        EXPECT_FALSE(static_cast<bool>(second));
        EXPECT_TRUE(static_cast<bool>(third));
        EXPECT_EQ(scope.count(), 1u);
    }

    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, ClosedScopeReturnsDisengagedAssociation) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    scope.close();
    auto assoc = token.try_associate();

    EXPECT_FALSE(static_cast<bool>(assoc));
    EXPECT_EQ(scope.count(), 0u);
}

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

TEST(SimpleCountingScopeTest, AssociateCompletesAndDisassociates) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    auto result = std::execution::sync_wait(token.associate(std::execution::just(42)));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, WrapCompletesAndDisassociates) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    auto result = std::execution::sync_wait(token.wrap(std::execution::just(42)));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, AssociateClosedScopeCompletesStopped) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    scope.close();

    auto result = std::execution::sync_wait(token.associate(std::execution::just(42)));

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, WrapClosedScopeCompletesStopped) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    scope.close();

    auto result = std::execution::sync_wait(token.wrap(std::execution::just(42)));

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, AssociateDisassociatesOnError) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    EXPECT_THROW((void)std::execution::sync_wait(
        token.associate(std::execution::just_error(42))), int);
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, SpawnDisassociatesOnErrorAndStopped) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    token.spawn(std::execution::just_error(42));
    token.spawn(std::execution::just_stopped());

    scope.join();
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, WrapAcquiresAtStartAndReleasesOnOperationDestruction) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    bool started = false;
    bool completed = false;

    {
        auto op = std::execution::connect(
            token.wrap(pending_sender{&started}),
            scope_probe_receiver{&completed});

        EXPECT_EQ(scope.count(), 0u);
        EXPECT_FALSE(started);

        std::execution::start(op);

        EXPECT_TRUE(started);
        EXPECT_FALSE(completed);
        EXPECT_EQ(scope.count(), 1u);
    }

    EXPECT_EQ(scope.count(), 0u);
}

TEST(SimpleCountingScopeTest, UnstartedWrappedOperationDoesNotAssociate) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    bool started = false;
    bool completed = false;

    {
        auto op = std::execution::connect(
            token.wrap(pending_sender{&started}),
            scope_probe_receiver{&completed});
        (void)op;
        EXPECT_EQ(scope.count(), 0u);
    }

    EXPECT_FALSE(started);
    EXPECT_FALSE(completed);
    EXPECT_EQ(scope.count(), 0u);
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

TEST(CountingScopeTest, IsDistinctAndAssociatesWork) {
    static_assert(!std::is_same_v<
                  std::execution::counting_scope,
                  std::execution::simple_counting_scope>);

    std::execution::counting_scope scope;
    auto token = scope.get_token();
    using association_t = decltype(token.try_associate());

    static_assert(std::execution::scope_association<association_t>);
    static_assert(std::execution::scope_token<decltype(token)>);

    {
        auto assoc = token.try_associate();
        EXPECT_TRUE(static_cast<bool>(assoc));
        EXPECT_EQ(scope.count(), 1u);

        auto nested = assoc.try_associate();
        EXPECT_TRUE(static_cast<bool>(nested));
        EXPECT_EQ(scope.count(), 2u);
    }

    EXPECT_EQ(scope.count(), 0u);
}

TEST(CountingScopeTest, WrapPreservesCompletionResults) {
    std::execution::counting_scope scope;
    auto token = scope.get_token();

    auto value = std::execution::sync_wait(token.wrap(std::execution::just(42)));
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(std::get<0>(*value), 42);
    EXPECT_EQ(scope.count(), 0u);

    EXPECT_THROW((void)std::execution::sync_wait(
        token.wrap(std::execution::just_error(42))), int);
    EXPECT_EQ(scope.count(), 0u);

    auto stopped = std::execution::sync_wait(token.wrap(std::execution::just_stopped()));
    EXPECT_FALSE(stopped.has_value());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(CountingScopeTest, CloseRejectsNewWrappedWork) {
    std::execution::counting_scope scope;
    auto token = scope.get_token();

    scope.close();
    auto result = std::execution::sync_wait(token.wrap(std::execution::just(42)));

    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(scope.is_closed());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(CountingScopeTest, WrapInjectsScopeStopToken) {
    std::execution::counting_scope scope;
    auto token = scope.get_token();

    auto result = std::execution::sync_wait(
        token.wrap(std::execution::read_env(std::execution::get_stop_token)));

    ASSERT_TRUE(result.has_value());
    auto stop_token = std::get<0>(*result);
    EXPECT_TRUE(stop_token.stop_possible());
    EXPECT_FALSE(stop_token.stop_requested());

    EXPECT_TRUE(scope.request_stop());
    EXPECT_TRUE(stop_token.stop_requested());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(CountingScopeTest, RequestStopCancelsSpawnedWrappedWork) {
    std::execution::counting_scope scope;
    auto token = scope.get_token();
    auto state = std::make_shared<spawn_future_manual_state>();

    token.spawn(spawn_future_manual_sender{state});

    ASSERT_TRUE(wait_until_started(state));
    EXPECT_EQ(scope.count(), 1u);

    EXPECT_TRUE(scope.request_stop());
    EXPECT_TRUE(wait_until_stop_requested(state));
    EXPECT_TRUE(wait_until_completed(state));

    scope.join();
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, CompletedBeforeConsumerDeliversValue) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    auto state = std::make_shared<spawn_future_manual_state>();

    auto future = std::execution::spawn_future(
        spawn_future_manual_sender{state}, token);

    ASSERT_TRUE(wait_until_started(state));
    EXPECT_EQ(scope.count(), 1u);

    complete_manual_value(state, 42);
    ASSERT_TRUE(wait_until_completed(state));
    EXPECT_EQ(scope.count(), 0u);

    auto result = std::execution::sync_wait(std::move(future));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, ConsumerBeforeCompletionWaitsForValue) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    auto state = std::make_shared<spawn_future_manual_state>();

    auto future = std::execution::spawn_future(
        spawn_future_manual_sender{state}, token);

    std::optional<int> observed;
    std::exception_ptr failure;
    std::thread consumer{[future = std::move(future), &observed, &failure]() mutable {
        try {
            auto result = std::execution::sync_wait(std::move(future));
            if (result.has_value()) {
                observed = std::get<0>(*result);
            }
        } catch (...) {
            failure = std::current_exception();
        }
    }};

    ASSERT_TRUE(wait_until_started(state));
    EXPECT_EQ(scope.count(), 1u);

    complete_manual_value(state, 7);
    consumer.join();

    if (failure) {
        std::rethrow_exception(failure);
    }
    ASSERT_TRUE(observed.has_value());
    EXPECT_EQ(*observed, 7);
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, ErrorAndStoppedResultsPropagate) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    EXPECT_THROW((void)std::execution::sync_wait(
        std::execution::spawn_future(
            std::execution::just_error(spawn_future_marker_error{}), token)),
        spawn_future_marker_error);
    EXPECT_EQ(scope.count(), 0u);

    auto stopped = std::execution::sync_wait(
        std::execution::spawn_future(std::execution::just_stopped(), token));

    EXPECT_FALSE(stopped.has_value());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, ClosedScopeDoesNotStartWork) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    scope.close();

    std::atomic<int> started{0};
    auto future = std::execution::spawn_future(
        std::execution::just() | std::execution::then([&started] {
            started.fetch_add(1, std::memory_order_relaxed);
        }),
        token);

    auto result = std::execution::sync_wait(std::move(future));

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(started.load(std::memory_order_relaxed), 0);
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, AbandonedFutureRequestsStop) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    auto state = std::make_shared<spawn_future_manual_state>();

    {
        auto future = std::execution::spawn_future(
            spawn_future_manual_sender{state}, token);

        ASSERT_TRUE(wait_until_started(state));
        EXPECT_EQ(scope.count(), 1u);
    }

    EXPECT_TRUE(wait_until_stop_requested(state));
    EXPECT_TRUE(wait_until_completed(state));
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, DownstreamStopRequestsCancelSpawnedWork) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    auto state = std::make_shared<spawn_future_manual_state>();
    std::inplace_stop_source downstream_stop;
    std::atomic<bool> receiver_stopped{false};

    auto future = std::execution::spawn_future(
        spawn_future_manual_sender{state}, token);
    auto op = std::execution::connect(
        std::move(future),
        spawn_future_stop_receiver{&downstream_stop, &receiver_stopped});

    std::execution::start(op);
    ASSERT_TRUE(wait_until_started(state));
    EXPECT_EQ(scope.count(), 1u);

    downstream_stop.request_stop();

    EXPECT_TRUE(wait_until_stop_requested(state));
    EXPECT_TRUE(wait_until_completed(state));
    EXPECT_TRUE(receiver_stopped.load(std::memory_order_acquire));
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

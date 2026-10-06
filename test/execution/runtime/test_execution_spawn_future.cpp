#include <gtest/gtest.h>
#include <execution>
#include "test_execution_manual_sender.hpp"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

using forge_execution_test::manual_sender;
using forge_execution_test::manual_state;
using forge_execution_test::manual_value_completer;
using forge_execution_test::wait_until_completed;
using forge_execution_test::wait_until_started;
using forge_execution_test::wait_until_stop_requested;
using namespace std::chrono_literals;

struct spawn_future_marker_error {};

template<class CompletionSignatures, class Signature>
struct contains_completion_signature : std::false_type {};

template<class... Signatures, class Signature>
struct contains_completion_signature<
    std::execution::completion_signatures<Signatures...>,
    Signature>
    : std::bool_constant<(std::same_as<Signatures, Signature> || ...)> {};

struct reference_value_sender {
    using sender_concept = std::execution::sender_t;

    int* value = nullptr;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int&)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;
        int* value;

        void start() & noexcept {
            std::execution::set_value(std::move(rcvr), *value);
        }
    };

    template<std::execution::receiver R>
    auto connect(R rcvr) const -> op<R> {
        return op<R>{std::move(rcvr), value};
    }
};

struct reference_error_sender {
    using sender_concept = std::execution::sender_t;

    const spawn_future_marker_error* error = nullptr;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_error_t(const spawn_future_marker_error&)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;
        const spawn_future_marker_error* error;

        void start() & noexcept {
            std::execution::set_error(std::move(rcvr), *error);
        }
    };

    template<std::execution::receiver R>
    auto connect(R rcvr) const -> op<R> {
        return op<R>{std::move(rcvr), error};
    }
};

struct deref_unique {
    int operator()(std::unique_ptr<int> value) const noexcept {
        return *value;
    }
};

struct allocation_counts {
    std::atomic<int> attempts{0};
    std::atomic<int> allocations{0};
    std::atomic<int> deallocations{0};
    std::atomic<int> fail_on_attempt{0};
};

template<class T>
struct counting_allocator {
    using value_type = T;

    std::shared_ptr<allocation_counts> counts;

    counting_allocator() noexcept = default;

    explicit counting_allocator(std::shared_ptr<allocation_counts> c) noexcept
        : counts(std::move(c)) {}

    template<class U>
    counting_allocator(const counting_allocator<U>& other) noexcept
        : counts(other.counts) {}

    [[nodiscard]] T* allocate(std::size_t n) {
        if (counts) {
            const int attempt =
                counts->attempts.fetch_add(1, std::memory_order_relaxed) + 1;
            if (counts->fail_on_attempt.load(std::memory_order_relaxed) ==
                attempt) {
                throw std::bad_alloc{};
            }
            counts->allocations.fetch_add(1, std::memory_order_relaxed);
        }
        return std::allocator<T>{}.allocate(n);
    }

    void deallocate(T* ptr, std::size_t n) noexcept {
        if (counts) {
            counts->deallocations.fetch_add(1, std::memory_order_relaxed);
        }
        std::allocator<T>{}.deallocate(ptr, n);
    }

    template<class U>
    bool operator==(const counting_allocator<U>& other) const noexcept {
        return counts == other.counts;
    }
};

struct member_allocator_env {
    counting_allocator<std::byte> allocator;

    auto query(std::execution::get_allocator_t) const noexcept
        -> counting_allocator<std::byte> {
        return allocator;
    }
};

template<class T>
struct nondefault_allocator : counting_allocator<T> {
    nondefault_allocator() = delete;

    explicit nondefault_allocator(std::shared_ptr<allocation_counts> counts) noexcept
        : counting_allocator<T>(std::move(counts)) {}

    template<class U>
    nondefault_allocator(const nondefault_allocator<U>& other) noexcept
        : counting_allocator<T>(other.counts) {}
};

static_assert(!std::is_default_constructible_v<nondefault_allocator<std::byte>>);
static_assert(std::is_copy_constructible_v<nondefault_allocator<std::byte>>);

template<class S, class Alloc>
struct allocator_attrs_sender {
    using sender_concept = std::execution::sender_t;

    struct attrs {
        Alloc allocator;
        int* queries;

        attrs(Alloc alloc, int* calls) : allocator(std::move(alloc)), queries(calls) {}
        attrs(const attrs&) = delete;
        attrs(attrs&&) = default;

        auto query(std::execution::get_allocator_t) const noexcept -> Alloc {
            if (queries) {
                ++*queries;
            }
            return allocator;
        }
    };

    S source;
    attrs attributes;

    allocator_attrs_sender(S sndr, Alloc alloc, int* queries = nullptr)
        : source(std::move(sndr)), attributes(std::move(alloc), queries) {}

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures_of_t<S, Env> {
        return {};
    }

    auto get_env() const noexcept -> const attrs& { return attributes; }

    template<std::execution::receiver R>
    auto connect(R rcvr) && {
        return std::execution::connect(std::move(source), std::move(rcvr));
    }
};

struct allocator_scope_token {
    std::execution::simple_counting_scope::scope_token inner;
    counting_allocator<std::byte> allocator;

    auto try_associate() const noexcept { return inner.try_associate(); }

    template<std::execution::sender S>
    auto wrap(S&& sndr) const {
        return allocator_attrs_sender{static_cast<S&&>(sndr), allocator};
    }
};

static_assert(std::execution::scope_token<allocator_scope_token>);

struct move_only_scheduler_env {
    std::unique_ptr<std::execution::run_loop::scheduler> scheduler;

    auto query(std::execution::get_scheduler_t) const noexcept
        -> std::execution::run_loop::scheduler {
        return *scheduler;
    }
};

static_assert(!std::is_copy_constructible_v<move_only_scheduler_env>);

struct observed_stop_token {
    std::inplace_stop_token token;
    std::atomic<int>* registrations;

    struct registration_credit {
        std::atomic<int>* count;
        explicit registration_credit(std::atomic<int>* value) : count(value) {
            count->fetch_add(1, std::memory_order_relaxed);
        }
        ~registration_credit() { count->fetch_sub(1, std::memory_order_relaxed); }
    };

    template<class Callback>
    struct callback_type {
        registration_credit credit;
        std::inplace_stop_callback<Callback> registration;

        callback_type(observed_stop_token token, Callback fn)
            : credit(token.registrations)
            , registration(token.token, std::move(fn)) {}
    };

    bool stop_possible() const noexcept { return token.stop_possible(); }
    bool stop_requested() const noexcept { return token.stop_requested(); }
    bool operator==(const observed_stop_token& other) const noexcept {
        return token == other.token;
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

struct join_probe_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::atomic<bool>* completed = nullptr;

    void set_value() && noexcept {
        completed->store(true, std::memory_order_release);
    }

    template<class E>
    void set_error(E&&) && noexcept {
        completed->store(true, std::memory_order_release);
    }

    void set_stopped() && noexcept {
        completed->store(true, std::memory_order_release);
    }

    auto get_env() const noexcept {
        return std::execution::make_env(std::execution::make_prop(
            std::execution::get_start_scheduler_t{},
            std::execution::inline_scheduler{}));
    }
};

bool wait_for_flag(
    const std::atomic<bool>& flag,
    std::chrono::milliseconds timeout = 500ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (flag.load(std::memory_order_acquire)) {
            return true;
        }
        std::this_thread::yield();
    }
    return flag.load(std::memory_order_acquire);
}

void complete_manual_value(const std::shared_ptr<manual_state>& state, int value) {
    auto complete = manual_value_completer(state);
    ASSERT_TRUE(static_cast<bool>(complete));
    complete(value);
}

} // namespace

TEST(SpawnFutureTest, CompletedBeforeConsumerDeliversValue) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    auto state = std::make_shared<manual_state>();

    auto future = std::execution::spawn_future(
        manual_sender{state}, token);

    ASSERT_TRUE(wait_until_started(state));
    EXPECT_EQ(scope.count(), 1u);

    complete_manual_value(state, 42);
    ASSERT_TRUE(wait_until_completed(state));
    EXPECT_EQ(scope.count(), 1u);

    auto result = std::execution::sync_wait(std::move(future));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, ConsumerBeforeCompletionWaitsForValue) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    auto state = std::make_shared<manual_state>();

    auto future = std::execution::spawn_future(
        manual_sender{state}, token);

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

TEST(SpawnFutureTest, NonCopyableLvaluePipelineConsumesSource) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    auto sndr = std::execution::just(std::make_unique<int>(31))
        | std::execution::then(deref_unique{});

    auto future = std::execution::spawn_future(std::move(sndr), token);
    auto result = std::execution::sync_wait(std::move(future));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 31);
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, AdvertisesStoredReferenceCompletionsAsDecayedValues) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();

    int value = 42;
    auto value_future = std::execution::spawn_future(
        reference_value_sender{&value}, token);
    using value_cs_t = std::execution::completion_signatures_of_t<
        decltype(value_future)>;
    static_assert(contains_completion_signature<
                  value_cs_t,
                  std::execution::set_value_t(int)>::value);
    static_assert(!contains_completion_signature<
                  value_cs_t,
                  std::execution::set_value_t(int&)>::value);

    auto value_result = std::execution::sync_wait(std::move(value_future));
    ASSERT_TRUE(value_result.has_value());
    EXPECT_EQ(std::get<0>(*value_result), 42);

    const spawn_future_marker_error error{};
    auto error_future = std::execution::spawn_future(
        reference_error_sender{&error}, token);
    using error_cs_t = std::execution::completion_signatures_of_t<
        decltype(error_future)>;
    static_assert(contains_completion_signature<
                  error_cs_t,
                  std::execution::set_error_t(spawn_future_marker_error)>::value);
    static_assert(!contains_completion_signature<
                  error_cs_t,
                  std::execution::set_error_t(
                      const spawn_future_marker_error&)>::value);

    EXPECT_THROW(
        (void)std::execution::sync_wait(std::move(error_future)),
        spawn_future_marker_error);
    EXPECT_EQ(scope.count(), 0u);

    auto joined = std::execution::sync_wait(scope.join());
    EXPECT_TRUE(joined.has_value());
}

TEST(SpawnFutureTest, UsesAllocatorFromEnvironmentForStateAndConsumerRecord) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    auto counts = std::make_shared<allocation_counts>();

    {
        auto env = std::execution::make_env(
            std::execution::make_prop(
                std::execution::get_allocator_t{},
                counting_allocator<std::byte>{counts}));
        auto future = std::execution::spawn_future(
            std::execution::just(42), token, env);
        auto result = std::execution::sync_wait(std::move(future));

        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(std::get<0>(*result), 42);
    }

    EXPECT_GE(counts->allocations.load(std::memory_order_relaxed), 2);
    EXPECT_EQ(counts->allocations.load(std::memory_order_relaxed),
              counts->deallocations.load(std::memory_order_relaxed));
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, ForwardsMemberQueriedEnvironmentToWrappedSender) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    auto counts = std::make_shared<allocation_counts>();

    {
        auto future = std::execution::spawn_future(
            std::execution::read_env(std::execution::get_allocator),
            token,
            member_allocator_env{counting_allocator<std::byte>{counts}});
        auto result = std::execution::sync_wait(std::move(future));

        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(std::get<0>(*result).counts, counts);
    }

    EXPECT_GE(counts->allocations.load(std::memory_order_relaxed), 2);
    EXPECT_EQ(counts->allocations.load(std::memory_order_relaxed),
              counts->deallocations.load(std::memory_order_relaxed));
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, SuppliedAllocatorOverridesSenderAttributes) {
    std::execution::simple_counting_scope scope;
    auto receiver_counts = std::make_shared<allocation_counts>();
    auto sender_counts = std::make_shared<allocation_counts>();
    sender_counts->fail_on_attempt.store(1, std::memory_order_relaxed);
    int sender_queries = 0;

    {
        auto sender = allocator_attrs_sender{
            std::execution::read_env(std::execution::get_allocator),
            counting_allocator<std::byte>{sender_counts},
            &sender_queries};
        auto future = std::execution::spawn_future(
            std::move(sender), scope.get_token(),
            member_allocator_env{counting_allocator<std::byte>{receiver_counts}});
        auto result = std::execution::sync_wait(std::move(future));

        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(std::get<0>(*result).counts, receiver_counts);
    }

    EXPECT_EQ(sender_queries, 0);
    EXPECT_EQ(sender_counts->attempts.load(std::memory_order_relaxed), 0);
    EXPECT_GE(receiver_counts->allocations.load(std::memory_order_relaxed), 2);
    EXPECT_EQ(receiver_counts->allocations.load(std::memory_order_relaxed),
              receiver_counts->deallocations.load(std::memory_order_relaxed));
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, FallsBackToSenderAllocatorWithAndWithoutExplicitEnvironment) {
    for (bool explicit_env : {false, true}) {
        std::execution::simple_counting_scope scope;
        auto counts = std::make_shared<allocation_counts>();
        int queries = 0;

        {
            auto sender = allocator_attrs_sender{
                std::execution::read_env(std::execution::get_allocator),
                counting_allocator<std::byte>{counts},
                &queries};
            auto future = explicit_env
                ? std::execution::spawn_future(
                    std::move(sender), scope.get_token(), std::execution::empty_env{})
                : std::execution::spawn_future(std::move(sender), scope.get_token());
            auto result = std::execution::sync_wait(std::move(future));

            ASSERT_TRUE(result.has_value());
            EXPECT_EQ(std::get<0>(*result).counts, counts);
        }

        EXPECT_EQ(queries, 1);
        EXPECT_GE(counts->allocations.load(std::memory_order_relaxed), 2);
        EXPECT_EQ(counts->allocations.load(std::memory_order_relaxed),
                  counts->deallocations.load(std::memory_order_relaxed));
        EXPECT_EQ(scope.count(), 0u);
    }
}

TEST(SpawnFutureTest, SelectsAllocatorFromTokenWrappedSender) {
    std::execution::simple_counting_scope scope;
    auto counts = std::make_shared<allocation_counts>();

    {
        allocator_scope_token token{
            scope.get_token(), counting_allocator<std::byte>{counts}};
        auto future = std::execution::spawn_future(
            std::execution::read_env(std::execution::get_allocator), token);
        auto result = std::execution::sync_wait(std::move(future));

        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(std::get<0>(*result).counts, counts);
    }

    EXPECT_GE(counts->allocations.load(std::memory_order_relaxed), 2);
    EXPECT_EQ(counts->allocations.load(std::memory_order_relaxed),
              counts->deallocations.load(std::memory_order_relaxed));
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, DefaultAllocatorDoesNotAddAnAllocatorQueryToSpawnEnvironment) {
    for (bool explicit_env : {false, true}) {
        std::execution::simple_counting_scope scope;
        auto sender = std::execution::read_env([](const auto& env) noexcept {
            return requires { std::execution::get_allocator(env); };
        });
        auto future = explicit_env
            ? std::execution::spawn_future(
                std::move(sender), scope.get_token(), std::execution::empty_env{})
            : std::execution::spawn_future(std::move(sender), scope.get_token());
        using allocator_t = typename decltype(future)::state_t::allocator_t;
        static_assert(std::same_as<allocator_t, std::allocator<std::byte>>);
        auto result = std::execution::sync_wait(std::move(future));

        ASSERT_TRUE(result.has_value());
        EXPECT_FALSE(std::get<0>(*result));
        EXPECT_EQ(scope.count(), 0u);
    }
}

TEST(SpawnFutureTest, SenderAllocatorFallbackPreservesMoveOnlyPayloadAndEnvironment) {
    std::execution::simple_counting_scope scope;
    std::execution::run_loop loop;
    auto counts = std::make_shared<allocation_counts>();

    {
        auto source = std::execution::read_env(
            [value = std::make_unique<int>(42)](const auto& env) mutable noexcept {
                auto allocator = std::execution::get_allocator(env);
                auto scheduler = std::execution::get_scheduler(env);
                return std::tuple{std::move(value), allocator.counts, scheduler};
            });
        static_assert(!std::is_copy_constructible_v<decltype(source)>);
        auto sender = allocator_attrs_sender{
            std::move(source), nondefault_allocator<std::byte>{counts}};
        auto future = std::execution::spawn_future(
            std::move(sender), scope.get_token(),
            move_only_scheduler_env{
                std::make_unique<std::execution::run_loop::scheduler>(loop.get_scheduler())});
        auto result = std::execution::sync_wait(std::move(future));

        ASSERT_TRUE(result.has_value());
        const auto& value = std::get<0>(*result);
        ASSERT_TRUE(std::get<0>(value));
        EXPECT_EQ(*std::get<0>(value), 42);
        EXPECT_EQ(std::get<1>(value), counts);
        EXPECT_EQ(std::get<2>(value), loop.get_scheduler());
    }

    EXPECT_GE(counts->allocations.load(std::memory_order_relaxed), 2);
    EXPECT_EQ(counts->allocations.load(std::memory_order_relaxed),
              counts->deallocations.load(std::memory_order_relaxed));
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, SenderAllocatorFailureReleasesAssociationWithoutStartingWork) {
    std::execution::simple_counting_scope scope;
    auto counts = std::make_shared<allocation_counts>();
    counts->fail_on_attempt.store(1, std::memory_order_relaxed);
    bool started = false;
    auto sender = allocator_attrs_sender{
        std::execution::just() | std::execution::then([&] { started = true; }),
        counting_allocator<std::byte>{counts}};

    EXPECT_THROW(
        (void)std::execution::spawn_future(std::move(sender), scope.get_token()),
        std::bad_alloc);

    EXPECT_FALSE(started);
    EXPECT_EQ(counts->attempts.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(counts->allocations.load(std::memory_order_relaxed), 0);
    EXPECT_EQ(counts->deallocations.load(std::memory_order_relaxed), 0);
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, FusesPrerequestedEnvironmentStopToken) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    std::inplace_stop_source source;
    source.request_stop();
    auto env = std::execution::make_env(
        std::execution::make_prop(
            std::execution::get_stop_token_t{}, source.get_token()));

    auto future = std::execution::spawn_future(
        std::execution::read_env(std::execution::get_stop_token)
            | std::execution::then([](auto observed) noexcept {
                  return observed.stop_requested();
              }),
        token,
        std::move(env));
    auto result = std::execution::sync_wait(std::move(future));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result));
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, EnvironmentStopAfterStartCancelsSpawnedWork) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    std::inplace_stop_source source;
    auto env = std::execution::make_env(
        std::execution::make_prop(
            std::execution::get_stop_token_t{}, source.get_token()));
    auto state = std::make_shared<manual_state>();

    auto future = std::execution::spawn_future(
        manual_sender{state}, token, std::move(env));
    ASSERT_TRUE(wait_until_started(state));

    source.request_stop();

    EXPECT_TRUE(wait_until_stop_requested(state));
    EXPECT_TRUE(wait_until_completed(state));
    auto result = std::execution::sync_wait(std::move(future));
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, InlineCompletionRemovesEnvironmentStopRegistration) {
    std::execution::simple_counting_scope scope;
    std::inplace_stop_source source;
    std::atomic<int> registrations{0};
    auto env = std::execution::make_env(std::execution::make_prop(
        std::execution::get_stop_token,
        observed_stop_token{source.get_token(), &registrations}));
    auto check = [&](auto sender) {
        auto future = std::execution::spawn_future(
            std::move(sender), scope.get_token(), env);
        EXPECT_EQ(registrations.load(std::memory_order_relaxed), 0);
    };

    check(std::execution::just(42));
    check(std::execution::just_error(spawn_future_marker_error{}));
    check(std::execution::just_stopped());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, DeferredCompletionRemovesEnvironmentStopRegistration) {
    for (bool stop : {false, true}) {
        std::execution::simple_counting_scope scope;
        std::inplace_stop_source source;
        std::atomic<int> registrations{0};
        auto env = std::execution::make_env(std::execution::make_prop(
            std::execution::get_stop_token,
            observed_stop_token{source.get_token(), &registrations}));
        auto state = std::make_shared<manual_state>();
        auto future = std::execution::spawn_future(
            manual_sender{state}, scope.get_token(), env);
        ASSERT_TRUE(wait_until_started(state));
        EXPECT_EQ(registrations.load(std::memory_order_relaxed), 1);

        if (stop) {
            EXPECT_TRUE(source.request_stop());
        } else {
            complete_manual_value(state, 42);
        }
        EXPECT_TRUE(wait_until_completed(state));
        EXPECT_EQ(registrations.load(std::memory_order_relaxed), 0);
        auto result = std::execution::sync_wait(std::move(future));
        EXPECT_EQ(result.has_value(), !stop);
        EXPECT_EQ(scope.count(), 0u);
    }
}

TEST(SpawnFutureTest, ConsumerAllocationFailureAbandonsFuture) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    auto source = std::make_shared<manual_state>();
    auto counts = std::make_shared<allocation_counts>();
    counts->fail_on_attempt.store(2, std::memory_order_relaxed);
    auto env = std::execution::make_env(
        std::execution::make_prop(
            std::execution::get_allocator_t{},
            counting_allocator<std::byte>{counts}));
    auto future = std::execution::spawn_future(
        manual_sender{source},
        token,
        env);

    ASSERT_TRUE(wait_until_started(source));
    EXPECT_EQ(scope.count(), 1u);

    std::inplace_stop_source downstream_stop;
    std::atomic<bool> receiver_stopped{false};
    EXPECT_THROW(
        (void)std::execution::connect(
            std::move(future),
            spawn_future_stop_receiver{
                &downstream_stop,
                &receiver_stopped}),
        std::bad_alloc);

    const bool stop_requested = wait_until_stop_requested(source);
    EXPECT_TRUE(stop_requested);
    if (!stop_requested) {
        complete_manual_value(source, 0);
    }

    EXPECT_TRUE(wait_until_completed(source));
    EXPECT_FALSE(receiver_stopped.load(std::memory_order_acquire));
    EXPECT_EQ(scope.count(), 0u);

    auto joined = std::execution::sync_wait(scope.join());
    EXPECT_TRUE(joined.has_value());
    EXPECT_EQ(
        counts->allocations.load(std::memory_order_relaxed),
        counts->deallocations.load(std::memory_order_relaxed));
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
    auto state = std::make_shared<manual_state>();

    {
        auto future = std::execution::spawn_future(
            manual_sender{state}, token);

        ASSERT_TRUE(wait_until_started(state));
        EXPECT_EQ(scope.count(), 1u);
    }

    EXPECT_TRUE(wait_until_stop_requested(state));
    EXPECT_TRUE(wait_until_completed(state));
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, UnstartedConsumerOperationRequestsStopOnDestruction) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    auto state = std::make_shared<manual_state>();
    std::inplace_stop_source downstream_stop;
    std::atomic<bool> receiver_stopped{false};

    {
        auto future = std::execution::spawn_future(
            manual_sender{state}, token);
        ASSERT_TRUE(wait_until_started(state));
        EXPECT_EQ(scope.count(), 1u);

        auto op = std::execution::connect(
            std::move(future),
            spawn_future_stop_receiver{&downstream_stop, &receiver_stopped});
        (void)op;
    }

    const bool stop_requested = wait_until_stop_requested(state);
    EXPECT_TRUE(stop_requested);
    if (!stop_requested) {
        complete_manual_value(state, 0);
    }

    EXPECT_TRUE(wait_until_completed(state));
    EXPECT_FALSE(receiver_stopped.load(std::memory_order_acquire));
    EXPECT_EQ(scope.count(), 0u);

    auto joined = std::execution::sync_wait(scope.join());
    EXPECT_TRUE(joined.has_value());
}

TEST(SpawnFutureTest, AssociationOutlivesProducerUntilFutureIsReleased) {
    std::execution::counting_scope scope;
    auto token = scope.get_token();
    auto state = std::make_shared<manual_state>();

    using future_t = decltype(std::execution::spawn_future(
        manual_sender{state}, token));
    std::optional<future_t> future;
    future.emplace(std::execution::spawn_future(manual_sender{state}, token));

    ASSERT_TRUE(wait_until_started(state));
    EXPECT_EQ(scope.count(), 1u);

    std::atomic<bool> joined{false};
    auto join_op = std::execution::connect(
        scope.join(),
        join_probe_receiver{&joined});
    std::execution::start(join_op);

    complete_manual_value(state, 5);

    EXPECT_TRUE(wait_until_completed(state));
    EXPECT_FALSE(joined.load(std::memory_order_acquire));
    EXPECT_EQ(scope.count(), 1u);

    future.reset();

    EXPECT_TRUE(wait_for_flag(joined));
    EXPECT_FALSE(future.has_value());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, DownstreamStopRequestsCancelSpawnedWork) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    auto state = std::make_shared<manual_state>();
    std::inplace_stop_source downstream_stop;
    std::atomic<bool> receiver_stopped{false};

    {
        auto future = std::execution::spawn_future(
            manual_sender{state}, token);
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
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, DownstreamStopCompletesBeforeStopIgnoringWorkFinishes) {
    std::execution::simple_counting_scope scope;
    auto token = scope.get_token();
    auto state = std::make_shared<manual_state>();
    state->stop_completes = false;
    std::inplace_stop_source downstream_stop;
    std::atomic<bool> receiver_stopped{false};

    auto future = std::execution::spawn_future(manual_sender{state}, token);
    auto op = std::execution::connect(
        std::move(future),
        spawn_future_stop_receiver{&downstream_stop, &receiver_stopped});

    std::execution::start(op);
    ASSERT_TRUE(wait_until_started(state));

    downstream_stop.request_stop();

    EXPECT_TRUE(receiver_stopped.load(std::memory_order_acquire));
    EXPECT_TRUE(wait_until_stop_requested(state));
    {
        std::lock_guard lk{state->mtx};
        EXPECT_FALSE(state->completed);
    }
    EXPECT_EQ(scope.count(), 1u);

    complete_manual_value(state, 42);

    EXPECT_TRUE(wait_until_completed(state));
    auto joined = std::execution::sync_wait(scope.join());
    EXPECT_TRUE(joined.has_value());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(SpawnFutureTest, ConsumerAttachRacesWithProducerCompletion) {
    constexpr int iterations = 128;

    for (int i = 0; i < iterations; ++i) {
        std::execution::simple_counting_scope scope;
        auto token = scope.get_token();
        auto state = std::make_shared<manual_state>();
        auto future = std::execution::spawn_future(manual_sender{state}, token);

        ASSERT_TRUE(wait_until_started(state));
        auto complete = manual_value_completer(state);
        ASSERT_TRUE(static_cast<bool>(complete));

        std::atomic<int> ready{0};
        std::atomic<bool> go{false};
        std::optional<int> observed;
        std::exception_ptr failure;

        std::thread consumer{[&] {
            ready.fetch_add(1, std::memory_order_acq_rel);
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            try {
                auto result = std::execution::sync_wait(std::move(future));
                if (result.has_value()) {
                    observed = std::get<0>(*result);
                }
            } catch (...) {
                failure = std::current_exception();
            }
        }};

        std::thread producer{[&, value = i] {
            ready.fetch_add(1, std::memory_order_acq_rel);
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            complete(value);
        }};

        while (ready.load(std::memory_order_acquire) != 2) {
            std::this_thread::yield();
        }
        go.store(true, std::memory_order_release);

        consumer.join();
        producer.join();

        if (failure) {
            std::rethrow_exception(failure);
        }
        ASSERT_TRUE(observed.has_value());
        EXPECT_EQ(*observed, i);
        EXPECT_EQ(scope.count(), 0u);
    }
}

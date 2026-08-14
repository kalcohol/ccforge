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

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
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

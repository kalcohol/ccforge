// The stop token of std::execution::task: [task.state]/7 and the stop token
// query of [task.promise]/12, with the W2 disposition for a token type that
// is not the task's own.
// Test names carry the pilot acceptance case they cover (G2-05 and G2-06).

#include <execution>

// The two-thread fixtures synchronize through the backport's private L0
// shim, and three cases drive the stop adapter directly with an observer.
#include <cpp26/execution/detail/proxy_stop_adapter.hpp>
#include <cpp26/execution/detail/sync_shim.hpp>

#include "test_execution_task_environment_support.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace ex = std::execution;
namespace sync_ops = std::execution::__forge_sync;
namespace adapters = std::execution::__forge_parsched_detail;
using namespace task_environment_test;

void pause_briefly() {
    const auto until = sync_ops::steady_clock::now() + std::chrono::milliseconds(2);
    while (sync_ops::steady_clock::now() < until) std::this_thread::yield();
}

// Waits for a condition another thread establishes. A schedule that never
// reaches it fails the test instead of hanging it.
template<class Condition>
bool wait_for(Condition&& reached) {
    const auto until = sync_ops::steady_clock::now() + std::chrono::seconds(20);
    while (!reached()) {
        if (sync_ops::steady_clock::now() > until) return false;
        std::this_thread::yield();
    }
    return true;
}

// Everything a translation does to a source and to a token is counted:
// constructions, destructions, calls of each token factory, requests and
// registrations.
struct stop_counts {
    sync_ops::atomic<int> sources{0}, live_sources{0}, token_calls{0}, const_token_calls{0}, requests{0},
        registrations{0}, live_registrations{0}, destroyed_in_request{0};
    sync_ops::atomic<bool> in_request{false};
    // Runs in a callback's constructor, before it is registered.
    std::function<void()> during_registration;
    // Runs in a callback's destructor, before it is removed.
    std::function<void()> entering_removal;
    // Runs in request_stop after the callbacks have run.
    std::function<void()> after_forward;
};

template<class Callback>
struct counting_callback {
    template<class Init>
    counting_callback(stop_counts& seen, std::inplace_stop_token token, Init&& init) : counts(&seen) {
        ++counts->registrations;
        ++counts->live_registrations;
        if (auto hook = std::exchange(counts->during_registration, nullptr)) hook();
        inner.emplace(token, std::forward<Init>(init));
    }
    counting_callback(counting_callback&&) = delete;
    ~counting_callback() {
        if (counts->entering_removal) counts->entering_removal();
        inner.reset();
        --counts->live_registrations;
    }

    stop_counts* counts;
    std::optional<std::inplace_stop_callback<Callback>> inner;
};

template<class Side>
struct basic_source;

// A token that only its source can make, so it always has a stop state. It
// has no default constructor and no disengaged value. Two tokens are equal
// when they share the stop state.
template<class Side>
struct basic_token {
    template<class Callback>
    struct callback_type : counting_callback<Callback> {
        template<class Init>
        callback_type(const basic_token& token, Init&& init)
            : counting_callback<Callback>(*token.counts, token.inner, std::forward<Init>(init)) {}
    };

    bool stop_requested() const noexcept { return inner.stop_requested(); }
    bool stop_possible() const noexcept { return inner.stop_possible(); }
    bool operator==(const basic_token& other) const noexcept { return inner == other.inner; }

private:
    friend struct basic_source<Side>;
    basic_token(stop_counts& counts, std::inplace_stop_token inner) noexcept : counts(&counts), inner(inner) {}

    stop_counts* counts;
    std::inplace_stop_token inner;
};

// The source has a token factory for a mutable source and one for a const
// source. The draft issues the token from the stored, mutable source.
template<class Side>
struct basic_source {
    static inline stop_counts* current = nullptr;

    basic_source() : basic_source(*current) {}
    explicit basic_source(stop_counts& counts) : counts(&counts) {
        ++counts.sources;
        ++counts.live_sources;
    }
    basic_source(const basic_source&) = delete;
    ~basic_source() {
        if (counts->in_request.load()) ++counts->destroyed_in_request;
        --counts->live_sources;
    }

    basic_token<Side> get_token() noexcept {
        ++counts->token_calls;
        return {*counts, inner.get_token()};
    }
    basic_token<Side> get_token() const noexcept {
        ++counts->const_token_calls;
        return {*counts, inner.get_token()};
    }
    bool stop_possible() const noexcept { return true; }
    bool stop_requested() const noexcept { return inner.stop_requested(); }
    bool request_stop() noexcept {
        stop_counts* const seen = counts;
        ++seen->requests;
        seen->in_request = true;
        const bool first = inner.request_stop();
        if (seen->after_forward) seen->after_forward();
        seen->in_request = false;
        return first;
    }

    stop_counts* counts;
    std::inplace_stop_source inner;
};

// A token that wraps whatever in-place token it is given, an empty one
// included. It has no default constructor.
template<class Side>
struct loose_token {
    template<class Callback>
    struct callback_type : counting_callback<Callback> {
        template<class Init>
        callback_type(const loose_token& token, Init&& init)
            : counting_callback<Callback>(*token.counts, token.inner, std::forward<Init>(init)) {}
    };

    loose_token(stop_counts& counts, std::inplace_stop_token inner) noexcept : counts(&counts), inner(inner) {}

    bool stop_requested() const noexcept { return inner.stop_requested(); }
    bool stop_possible() const noexcept { return inner.stop_possible(); }
    bool operator==(const loose_token& other) const noexcept { return inner == other.inner; }

    stop_counts* counts;
    std::inplace_stop_token inner;
};

// The receiver's side and the task's side are different types.
struct upstream_side;
struct task_side;
using upstream_source = basic_source<upstream_side>;
using upstream_token = basic_token<upstream_side>;
using impossible_upstream_token = loose_token<upstream_side>;
using task_source = basic_source<task_side>;
using task_token = basic_token<task_side>;

static_assert(std::stoppable_token<upstream_token>);
static_assert(std::stoppable_token<impossible_upstream_token>);
static_assert(!std::default_initializable<task_token>);
static_assert(!std::constructible_from<task_token, stop_counts&, std::inplace_stop_token>);

// The task's own source is default constructed by the translation.
struct task_counts_scope {
    explicit task_counts_scope(stop_counts& counts) { task_source::current = &counts; }
    ~task_counts_scope() { task_source::current = nullptr; }
};

// A source that can only be engaged, with a token that has no disengaged value.
struct counted_stop_environment {
    using start_scheduler_type = ex::inline_scheduler;
    using stop_source_type = task_source;
};

static_assert(std::same_as<ex::task<int, counted_stop_environment>::stop_token_type, task_token>);

template<class Token>
struct token_env {
    Token query(std::get_stop_token_t) const noexcept { return token; }
    Token token;
};

// What one look at the promise's token saw.
struct observation {
    bool requested = false, possible = false;
};

template<class Token>
observation observe(const Token& token) { return {token.stop_requested(), token.stop_possible()}; }

template<class Environment>
using token_of = typename ex::task<int, Environment>::stop_token_type;

template<class Environment>
ex::task<int, Environment> watches_token(gate* held, std::function<void(const token_of<Environment>&, int)> look) {
    auto first = co_await ex::read_env(std::get_stop_token);
    look(first, 1);
    co_await gate_sender{held};
    auto second = co_await ex::read_env(std::get_stop_token);
    look(second, 2);
    co_return first == second ? 1 : 0;
}

struct watch {
    observation first, second;
    int equal = -1;
};

// Runs watches_token with an upstream token expression, and lets the case
// act between the two looks.
template<class Environment, class MakeEnv, class Between>
watch watch_with(MakeEnv make_env, Between between) {
    watch seen;
    gate held;
    outcome done;
    {
        auto operation = ex::connect(
            watches_token<Environment>(&held, [&](const token_of<Environment>& token, int which) {
                (which == 1 ? seen.first : seen.second) = observe(token);
            }),
            done.receiver(make_env));
        ex::start(operation);
        between();
        held.open();
    }
    EXPECT_EQ(done.terminals, 1);
    seen.equal = done.result.value_or(-1);
    return seen;
}

void expect_seen(const observation& seen, bool requested, bool possible) {
    EXPECT_EQ(seen.requested, requested);
    EXPECT_EQ(seen.possible, possible);
}

// [task.promise]/12, third item: the stop token query has its own path, for
// exactly its tag type, and never reaches the Environment.
struct to_stop_token_query_t {
    constexpr operator std::get_stop_token_t() const noexcept { return {}; }
};

struct shadowing_environment {
    using start_scheduler_type = ex::inline_scheduler;

    void query(std::get_stop_token_t) const = delete;
    int query(to_stop_token_query_t) const noexcept { return -4; }
};

template<class Env, class Query>
concept answers = requires(const Env& env, Query&& query) { env.query(static_cast<Query&&>(query)); };

ex::task<int, shadowing_environment> asks_the_promise_for_its_token() {
    co_return co_await inspect_sender{[](const auto& env) {
        using env_t = std::remove_cvref_t<decltype(env)>;
        static_assert(std::same_as<decltype(env.query(std::get_stop_token)), std::inplace_stop_token>);
        static_assert(std::same_as<decltype(env.query(std::get_stop_token_t{})), std::inplace_stop_token>);
        static_assert(noexcept(env.query(std::get_stop_token)));
        static_assert(answers<env_t, const std::get_stop_token_t&>);
        // Not forwarding, and only convertible to the reserved tag.
        static_assert(!answers<env_t, to_stop_token_query_t>);
        static_assert(!answers<env_t, const to_stop_token_query_t&>);
        return env.query(std::get_stop_token) == std::inplace_stop_token{} ? 1 : 0;
    }};
}

TEST(ExecutionTaskStop, G205_PromiseAnswersTheStopTokenQueryItself) {
    outcome done;
    {
        auto operation = ex::connect(asks_the_promise_for_its_token(), done.receiver([] { return ex::env<>{}; }));
        ex::start(operation);
    }
    EXPECT_EQ(done.result, 1);
}

// A source that no translation may construct: with an exact token there is
// nothing to translate.
struct exact_side;
struct exact_only_source {
    exact_only_source() = delete;

    loose_token<exact_side> get_token() const noexcept;
    bool stop_possible() const noexcept;
    bool stop_requested() const noexcept;
    bool request_stop() noexcept;
};

struct exact_only_environment {
    using start_scheduler_type = ex::inline_scheduler;
    using stop_source_type = exact_only_source;
};

TEST(ExecutionTaskStop, G205_ExactTokenIsTheReceiversToken) {
    {
        // The in-place token, returned by value: the same type exactly.
        std::inplace_stop_source upstream;
        std::optional<bool> same_as_upstream;
        gate held;
        outcome done;
        auto operation = ex::connect(
            watches_token<inline_environment>(&held, [&](const std::inplace_stop_token& token, int) {
                same_as_upstream = token == upstream.get_token();
            }),
            done.receiver([&] { return token_env<std::inplace_stop_token>{upstream.get_token()}; }));
        ex::start(operation);
        EXPECT_EQ(same_as_upstream, true);
        upstream.request_stop();
        held.open();
        EXPECT_EQ(done.result, 1);
    }
    {
        // A token that is not default constructible, with its own source type.
        stop_counts task_counts, receiver_counts;
        task_counts_scope scope{task_counts};
        task_source upstream{receiver_counts};
        const auto seen = watch_with<counted_stop_environment>(
            [&] { return token_env<task_token>{std::as_const(upstream).get_token()}; },
            [&] { upstream.request_stop(); });
        expect_seen(seen.first, false, true);
        expect_seen(seen.second, true, true);
        EXPECT_EQ(seen.equal, 1);
        // Direct: the task made no source and registered nothing.
        EXPECT_EQ(task_counts.sources, 0);
        EXPECT_EQ(receiver_counts.registrations, 0);
    }
    {
        // A source type that cannot be constructed at all still connects,
        // starts and answers, because no source is needed.
        stop_counts receiver_counts;
        std::inplace_stop_source upstream;
        const auto seen = watch_with<exact_only_environment>(
            [&] { return token_env<loose_token<exact_side>>{{receiver_counts, upstream.get_token()}}; },
            [&] { upstream.request_stop(); });
        expect_seen(seen.first, false, true);
        expect_seen(seen.second, true, true);
        EXPECT_EQ(seen.equal, 1);
        EXPECT_EQ(receiver_counts.registrations, 0);
    }
}

TEST(ExecutionTaskStop, G205_NoStopQueryGivesADisengagedInPlaceToken) {
    // never_stop_token upstream, in-place source: nothing is created.
    std::optional<bool> is_default;
    gate held;
    outcome done;
    auto operation = ex::connect(
        watches_token<inline_environment>(&held, [&](const std::inplace_stop_token& token, int) {
            is_default = token == std::inplace_stop_token{};
            EXPECT_FALSE(token.stop_requested());
            EXPECT_FALSE(token.stop_possible());
        }),
        done.receiver([] { return ex::env<>{}; }));
    ex::start(operation);
    held.open();
    EXPECT_EQ(is_default, true);
    EXPECT_EQ(done.result, 1);
}

ex::task<int> watches_under_sync_wait() {
    auto token = co_await ex::read_env(std::get_stop_token);
    co_return (token.stop_requested() ? 1 : 0) + (token.stop_possible() ? 2 : 0) +
              (token == std::inplace_stop_token{} ? 4 : 0);
}

TEST(ExecutionTaskStop, G205_DefaultTaskUnderTokenlessSyncWait) {
    // sync_wait's environment has no stop token query. This is a passing case.
    auto result = std::this_thread::sync_wait(watches_under_sync_wait());
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 4);
}

// A body that asks for its stop token or never does, ending on each channel.
// A typed error goes through a different delivery than an exception.
enum class ending { value, exception, typed_error, stopped };

struct stopped_at_gate {
    using sender_concept = ex::sender_tag;

    template<class Self, class... Env>
    static consteval auto get_completion_signatures() noexcept {
        return ex::completion_signatures<ex::set_stopped_t()>{};
    }

    template<class Receiver>
    struct operation {
        using operation_state_concept = ex::operation_state_tag;

        void start() & noexcept {
            target->open = [this] { ex::set_stopped(std::move(receiver)); };
        }

        gate* target;
        Receiver receiver;
    };

    template<ex::receiver Receiver>
    auto connect(Receiver receiver) const -> operation<Receiver> { return {target, std::move(receiver)}; }

    gate* target;
};

struct typed_error_environment {
    using start_scheduler_type = ex::inline_scheduler;
    using stop_source_type = task_source;
    using error_types = ex::completion_signatures<ex::set_error_t(int), ex::set_error_t(std::exception_ptr)>;
};

ex::task<int, typed_error_environment> ends_on(gate* held, ending how, bool asks) {
    if (asks) (void)co_await ex::read_env(std::get_stop_token);
    if (how == ending::stopped) co_await stopped_at_gate{held};
    co_await gate_sender{held};
    if (how == ending::exception) throw std::runtime_error("ends_on");
    if (how == ending::typed_error) co_yield ex::with_error{7};
    co_return 5;
}

// The translation's state at connect, after start and inside the terminal.
struct translation_sample {
    int sources = -1, live_sources = -1, token_calls = -1, registrations = -1, live_registrations = -1;
};

translation_sample sample(const stop_counts& task_counts, const stop_counts& upstream_counts) {
    return {task_counts.sources.load(), task_counts.live_sources.load(), task_counts.token_calls.load(),
            upstream_counts.registrations.load(), upstream_counts.live_registrations.load()};
}

void expect_sample(const translation_sample& seen, int sources, int live_sources, int token_calls,
                   int registrations, int live_registrations) {
    EXPECT_EQ(seen.sources, sources);
    EXPECT_EQ(seen.live_sources, live_sources);
    EXPECT_EQ(seen.token_calls, token_calls);
    EXPECT_EQ(seen.registrations, registrations);
    EXPECT_EQ(seen.live_registrations, live_registrations);
}

TEST(ExecutionTaskStop, G205_MismatchedTokenIsTranslatedOnlyWhenAsked) {
    for (const bool asks : {false, true}) {
        for (const ending how : {ending::value, ending::exception, ending::typed_error, ending::stopped}) {
            SCOPED_TRACE(testing::Message() << "asks=" << asks << " ending=" << static_cast<int>(how));
            stop_counts task_counts, upstream_counts;
            task_counts_scope scope{task_counts};
            upstream_source upstream{upstream_counts};
            translation_sample connected, started, at_terminal;
            gate held;
            outcome done;
            {
                // A requestable upstream of another token type.
                auto operation = ex::connect(
                    ends_on(&held, how, asks),
                    done.receiver([&] { return token_env<upstream_token>{upstream.get_token()}; },
                                  [&] { at_terminal = sample(task_counts, upstream_counts); }));
                connected = sample(task_counts, upstream_counts);
                ex::start(operation);
                started = sample(task_counts, upstream_counts);
                held.open();
                EXPECT_EQ(done.terminals, 1);
            }
            // Nothing exists before the first query, and nothing at all
            // without one. One query makes one source, issues one token from
            // the mutable source and registers once.
            const int made = asks ? 1 : 0;
            expect_sample(connected, 0, 0, 0, 0, 0);
            expect_sample(started, made, made, made, made, made);
            // The registration is gone before the receiver is entered, on
            // every channel. The source is still there.
            expect_sample(at_terminal, made, made, made, made, 0);
            expect_sample(sample(task_counts, upstream_counts), made, 0, made, made, 0);
            EXPECT_EQ(task_counts.const_token_calls, 0);
            EXPECT_EQ(task_counts.requests, 0);
            EXPECT_EQ(done.terminal, how == ending::value ? "" : how == ending::stopped ? "stopped" : "error");
            EXPECT_EQ(done.terminals, 1);
        }
    }
}

TEST(ExecutionTaskStop, G205_MismatchedTokenForwardsThroughOneLazySource) {
    {
        // Requested before the first query.
        stop_counts task_counts, upstream_counts;
        task_counts_scope scope{task_counts};
        upstream_source upstream{upstream_counts};
        upstream.request_stop();
        const auto seen = watch_with<counted_stop_environment>(
            [&] { return token_env<upstream_token>{upstream.get_token()}; }, [] {});
        expect_seen(seen.first, true, true);
        expect_seen(seen.second, true, true);
        EXPECT_EQ(seen.equal, 1);
        EXPECT_EQ(upstream_counts.registrations, 1);
        EXPECT_EQ(upstream_counts.live_registrations, 0);
        EXPECT_EQ(task_counts.sources, 1);
        EXPECT_EQ(task_counts.requests, 1);
    }
    {
        // Requested after the first query: the same token now reports it.
        stop_counts task_counts, upstream_counts;
        task_counts_scope scope{task_counts};
        upstream_source upstream{upstream_counts};
        const auto seen = watch_with<counted_stop_environment>(
            [&] { return token_env<upstream_token>{upstream.get_token()}; }, [&] { upstream.request_stop(); });
        expect_seen(seen.first, false, true);
        expect_seen(seen.second, true, true);
        EXPECT_EQ(seen.equal, 1);
        EXPECT_EQ(upstream_counts.registrations, 1);
        EXPECT_EQ(task_counts.sources, 1);
        EXPECT_EQ(task_counts.token_calls, 1);
        EXPECT_EQ(task_counts.requests, 1);
    }
    {
        // Never requested, with the engaged-only pair as an upstream that is
        // always possible: repeated queries agree and stay unrequested.
        stop_counts upstream_counts;
        upstream_source upstream{upstream_counts};
        const auto seen = watch_with<inline_environment>(
            [&] { return token_env<upstream_token>{upstream.get_token()}; }, [] {});
        expect_seen(seen.first, false, true);
        expect_seen(seen.second, false, true);
        EXPECT_EQ(seen.equal, 1);
        EXPECT_EQ(upstream_counts.registrations, 1);
        EXPECT_EQ(upstream_counts.live_registrations, 0);
    }
    {
        // A prop holds its token by reference in the query's type, so even an
        // in-place token is not the exact type and is translated.
        std::inplace_stop_source upstream;
        std::optional<bool> same_as_upstream;
        gate held;
        outcome done;
        auto operation = ex::connect(
            watches_token<inline_environment>(&held, [&](const std::inplace_stop_token& token, int which) {
                if (which == 1) same_as_upstream = token == upstream.get_token();
                EXPECT_EQ(token.stop_requested(), which == 2);
                EXPECT_TRUE(token.stop_possible());
            }),
            done.receiver([&] { return ex::prop{std::get_stop_token, upstream.get_token()}; }));
        ex::start(operation);
        upstream.request_stop();
        held.open();
        EXPECT_EQ(same_as_upstream, false);
        EXPECT_EQ(done.result, 1);
    }
}

// A custom source whose token type is the in-place token. Its default
// constructor makes a stop state and its tag constructor does not. The
// disengaged answer for this pair is known: the token type's own.
struct optional_inplace_source {
    static inline int constructed = 0;
    struct disengaged_t {};

    optional_inplace_source() {
        ++constructed;
        inner.emplace();
    }
    explicit optional_inplace_source(disengaged_t) noexcept {}
    // Accepts nostopstate_t by signature and cannot be instantiated. With the
    // in-place token type no candidate is ever searched, so it never is.
    template<class Tag>
    explicit optional_inplace_source(Tag) {
        typename Tag::missing unusable;
    }

    std::inplace_stop_token get_token() const noexcept {
        return inner ? inner->get_token() : std::inplace_stop_token{};
    }
    bool stop_possible() const noexcept { return inner.has_value(); }
    bool stop_requested() const noexcept { return inner && inner->stop_requested(); }
    bool request_stop() noexcept { return inner && inner->request_stop(); }

    std::optional<std::inplace_stop_source> inner;
};

template<class Source>
struct source_environment {
    using start_scheduler_type = ex::inline_scheduler;
    using stop_source_type = Source;
};

TEST(ExecutionTaskStop, G205_ImpossibleMismatchedToken) {
    {
        // In-place source: the disengaged token, both observers false.
        stop_counts counts;
        const impossible_upstream_token impossible{counts, std::inplace_stop_token{}};
        std::optional<bool> is_default;
        gate held;
        outcome done;
        auto operation = ex::connect(
            watches_token<inline_environment>(&held, [&](const std::inplace_stop_token& token, int) {
                is_default = token == std::inplace_stop_token{};
                EXPECT_FALSE(token.stop_requested());
                EXPECT_FALSE(token.stop_possible());
            }),
            done.receiver([&] { return token_env<impossible_upstream_token>{impossible}; }));
        ex::start(operation);
        held.open();
        EXPECT_EQ(is_default, true);
        EXPECT_EQ(done.result, 1);
        EXPECT_EQ(counts.registrations, 0);
    }
    {
        // A custom source with the in-place token type. Its disengaged
        // answer is representable, so no source is made and both observers
        // are false, at run time and for a receiver with no stop query.
        stop_counts counts;
        const impossible_upstream_token impossible{counts, std::inplace_stop_token{}};
        optional_inplace_source::constructed = 0;
        const auto seen = watch_with<source_environment<optional_inplace_source>>(
            [&] { return token_env<impossible_upstream_token>{impossible}; }, [] {});
        expect_seen(seen.first, false, false);
        expect_seen(seen.second, false, false);
        EXPECT_EQ(seen.equal, 1);
        const auto tokenless =
            watch_with<source_environment<optional_inplace_source>>([] { return ex::env<>{}; }, [] {});
        expect_seen(tokenless.first, false, false);
        EXPECT_EQ(tokenless.equal, 1);
        EXPECT_EQ(optional_inplace_source::constructed, 0);
        EXPECT_EQ(counts.registrations, 0);
    }
    {
        // The restoring control for that pair: an upstream that can stop
        // makes one source and reaches it.
        stop_counts counts;
        upstream_source upstream{counts};
        optional_inplace_source::constructed = 0;
        const auto seen = watch_with<source_environment<optional_inplace_source>>(
            [&] { return token_env<upstream_token>{upstream.get_token()}; }, [&] { upstream.request_stop(); });
        expect_seen(seen.first, false, true);
        expect_seen(seen.second, true, true);
        EXPECT_EQ(seen.equal, 1);
        EXPECT_EQ(optional_inplace_source::constructed, 1);
        EXPECT_EQ(counts.registrations, 1);
    }
    {
        // A pair that can only be engaged: the source has a default
        // constructor alone and only it can make its token. The upstream
        // cannot stop, so nothing is registered, but the token reports
        // possible, with no request. This is the recorded deviation ST-07.2.
        stop_counts task_counts;
        task_counts_scope scope{task_counts};
        const auto seen = watch_with<counted_stop_environment>(
            [] { return token_env<std::inplace_stop_token>{}; }, [] {});
        expect_seen(seen.first, false, true);
        expect_seen(seen.second, false, true);
        EXPECT_EQ(seen.equal, 1);
        EXPECT_EQ(task_counts.sources, 1);
        EXPECT_EQ(task_counts.token_calls, 1);
        EXPECT_EQ(task_counts.requests, 0);
    }
}

// A source that can make a disengaged token, but only through a constructor
// the translation knows nothing about. Its token has no default constructor.
struct tagged_side;

struct tagged_source {
    static inline stop_counts* current = nullptr;
    struct disengaged_t {};

    tagged_source() : counts(current) {
        ++counts->sources;
        inner.emplace();
    }
    explicit tagged_source(disengaged_t, stop_counts& seen) noexcept : counts(&seen) {}

    loose_token<tagged_side> get_token() const noexcept {
        return {*counts, inner ? inner->get_token() : std::inplace_stop_token{}};
    }
    bool stop_possible() const noexcept { return inner.has_value(); }
    bool stop_requested() const noexcept { return inner && inner->stop_requested(); }
    bool request_stop() noexcept { return inner && inner->request_stop(); }

    stop_counts* counts;
    std::optional<std::inplace_stop_source> inner;
};

// A prop answers with a reference, so a token of the task's own type is
// still a mismatch by the literal comparison. When that token cannot stop,
// it is itself the disengaged answer, and no source is made.
TEST(ExecutionTaskStop, G205_ImpossibleTokenOfTheIssuedTypeIsTheAnswer) {
    using environment = source_environment<tagged_source>;
    using token_t = loose_token<tagged_side>;
    {
        stop_counts task_counts, upstream_counts;
        tagged_source::current = &task_counts;
        const tagged_source upstream{tagged_source::disengaged_t{}, upstream_counts};
        const token_t disengaged = upstream.get_token();
        std::vector<bool> same_as_upstream;
        gate held;
        outcome done;
        watch seen;
        {
            auto operation = ex::connect(
                watches_token<environment>(&held, [&](const token_t& token, int which) {
                    (which == 1 ? seen.first : seen.second) = observe(token);
                    same_as_upstream.push_back(token == disengaged);
                }),
                done.receiver([&] { return ex::prop{std::get_stop_token, disengaged}; }));
            ex::start(operation);
            held.open();
        }
        expect_seen(seen.first, false, false);
        expect_seen(seen.second, false, false);
        EXPECT_EQ(done.result, 1);
        EXPECT_EQ(same_as_upstream, (std::vector<bool>{true, true}));
        EXPECT_EQ(task_counts.sources, 0);
        EXPECT_EQ(upstream_counts.registrations, 0);
    }
    {
        // The paired case: the same prop with a token that can stop is
        // translated through one source and one registration.
        stop_counts task_counts, upstream_counts;
        tagged_source::current = &upstream_counts;
        tagged_source upstream;
        tagged_source::current = &task_counts;
        const token_t requestable = upstream.get_token();
        std::vector<bool> same_as_upstream;
        gate held;
        outcome done;
        watch seen;
        {
            auto operation = ex::connect(
                watches_token<environment>(&held, [&](const token_t& token, int which) {
                    (which == 1 ? seen.first : seen.second) = observe(token);
                    same_as_upstream.push_back(token == requestable);
                }),
                done.receiver([&] { return ex::prop{std::get_stop_token, requestable}; }));
            ex::start(operation);
            upstream.request_stop();
            held.open();
        }
        expect_seen(seen.first, false, true);
        expect_seen(seen.second, true, true);
        EXPECT_EQ(done.result, 1);
        EXPECT_EQ(same_as_upstream, (std::vector<bool>{false, false}));
        EXPECT_EQ(task_counts.sources, 1);
        EXPECT_EQ(upstream_counts.registrations, 1);
        EXPECT_EQ(upstream_counts.live_registrations, 0);
    }
    tagged_source::current = nullptr;
}

// A custom token whose default constructor makes the disengaged value, with
// a source that has no disengaged form of its own.
struct defaulting_token {
    template<class Callback>
    struct callback_type {
        template<class Init>
        callback_type(const defaulting_token& token, Init&& init) : inner(token.inner, std::forward<Init>(init)) {}

        std::inplace_stop_callback<Callback> inner;
    };

    defaulting_token() noexcept = default;
    explicit defaulting_token(std::inplace_stop_token inner) noexcept : inner(inner) {}

    bool stop_requested() const noexcept { return inner.stop_requested(); }
    bool stop_possible() const noexcept { return inner.stop_possible(); }
    bool operator==(const defaulting_token&) const noexcept = default;

    std::inplace_stop_token inner;
};

struct defaulting_source {
    static inline int constructed = 0, live = 0;

    defaulting_source() { ++constructed, ++live; }
    defaulting_source(const defaulting_source&) = delete;
    ~defaulting_source() { --live; }

    defaulting_token get_token() const noexcept { return defaulting_token{inner.get_token()}; }
    bool stop_possible() const noexcept { return true; }
    bool stop_requested() const noexcept { return inner.stop_requested(); }
    bool request_stop() noexcept { return inner.request_stop(); }

    std::inplace_stop_source inner;
};

// A token type that can never stop, but whose objects have identities: each
// default constructed one is a different token.
struct wandering_token {
    static inline int made = 0;

    template<class Callback>
    struct callback_type {
        template<class Init>
        callback_type(const wandering_token&, Init&&) noexcept {}
    };

    wandering_token() noexcept : id(++made) {}
    // The one token of a source without a stop state.
    struct canonical_t {};
    explicit wandering_token(canonical_t) noexcept : id(0) {}

    static constexpr bool stop_requested() noexcept { return false; }
    static constexpr bool stop_possible() noexcept { return false; }
    bool operator==(const wandering_token&) const noexcept = default;

    int id;
};

struct wandering_source {
    wandering_source() = default;
    // Accepts nostopstate_t by signature and cannot be instantiated. The
    // token default answers first, so it never is.
    template<class Tag>
    explicit wandering_source(Tag) {
        typename Tag::missing unusable;
    }

    wandering_token get_token() const noexcept { return wandering_token{wandering_token::canonical_t{}}; }
    static constexpr bool stop_possible() noexcept { return false; }
    static constexpr bool stop_requested() noexcept { return false; }
    bool request_stop() noexcept { return false; }
};

static_assert(std::unstoppable_token<wandering_token>);

TEST(ExecutionTaskStop, G205_DisengagedAnswerIsFoundAndIsOneToken) {
    {
        // The token type's default value is disengaged, and is the answer.
        // No source is made and nothing is registered.
        stop_counts counts;
        const impossible_upstream_token impossible{counts, std::inplace_stop_token{}};
        defaulting_source::constructed = 0;
        const auto seen = watch_with<source_environment<defaulting_source>>(
            [&] { return token_env<impossible_upstream_token>{impossible}; }, [] {});
        expect_seen(seen.first, false, false);
        expect_seen(seen.second, false, false);
        EXPECT_EQ(seen.equal, 1);
        // The same for a receiver with no stop query.
        const auto tokenless = watch_with<source_environment<defaulting_source>>([] { return ex::env<>{}; }, [] {});
        expect_seen(tokenless.first, false, false);
        EXPECT_EQ(tokenless.equal, 1);
        EXPECT_EQ(defaulting_source::constructed, 0);
        EXPECT_EQ(counts.registrations, 0);
    }
    {
        // The restoring control: an upstream that can stop.
        stop_counts counts;
        upstream_source upstream{counts};
        defaulting_source::constructed = 0;
        const auto seen = watch_with<source_environment<defaulting_source>>(
            [&] { return token_env<upstream_token>{upstream.get_token()}; }, [&] { upstream.request_stop(); });
        expect_seen(seen.first, false, true);
        expect_seen(seen.second, true, true);
        EXPECT_EQ(seen.equal, 1);
        EXPECT_EQ(defaulting_source::constructed, 1);
        EXPECT_EQ(defaulting_source::live, 0);
        EXPECT_EQ(counts.registrations, 1);
    }
    {
        // Every query returns the one answer, also for a token type whose
        // default constructed objects differ from each other. The source
        // itself always issues the same token.
        const auto seen = watch_with<source_environment<wandering_source>>([] { return ex::env<>{}; }, [] {});
        expect_seen(seen.first, false, false);
        expect_seen(seen.second, false, false);
        EXPECT_EQ(seen.equal, 1);
        EXPECT_EQ(wandering_source{}.get_token(), wandering_source{}.get_token());
        EXPECT_NE(wandering_token{}, wandering_token{});
    }
}

// A source whose default constructor makes no stop state. Translating a
// token that can stop needs a fresh state, which only the default
// constructor is asked for.
struct stateless_side;
struct requested_side;
stop_counts unused_counts;

struct stateless_default_source {
    static inline int made = 0, factory = 0;
    struct fresh_t {};

    stateless_default_source() noexcept { ++made; }
    explicit stateless_default_source(fresh_t) { inner.emplace(); }

    loose_token<stateless_side> get_token() const noexcept {
        ++factory;
        return {unused_counts, inner ? inner->get_token() : std::inplace_stop_token{}};
    }
    bool stop_possible() const noexcept { return inner.has_value(); }
    bool stop_requested() const noexcept { return inner && inner->stop_requested(); }
    bool request_stop() noexcept { return inner && inner->request_stop(); }

    std::optional<std::inplace_stop_source> inner;
};

// A source whose default constructor has already requested a stop.
struct requested_default_source {
    static inline int made = 0, factory = 0;

    requested_default_source() {
        ++made;
        inner.request_stop();
    }

    loose_token<requested_side> get_token() const noexcept {
        ++factory;
        return {unused_counts, inner.get_token()};
    }
    bool stop_possible() const noexcept { return true; }
    bool stop_requested() const noexcept { return inner.stop_requested(); }
    bool request_stop() noexcept { return inner.request_stop(); }

    std::inplace_stop_source inner;
};

// A source whose default constructor throws.
struct throwing_side;

struct throwing_default_source {
    static inline int made = 0, factory = 0;

    throwing_default_source() {
        ++made;
        throw std::runtime_error("throwing_default_source");
    }

    loose_token<throwing_side> get_token() const noexcept {
        ++factory;
        return {unused_counts, std::inplace_stop_token{}};
    }
    bool stop_possible() const noexcept { return true; }
    bool stop_requested() const noexcept { return false; }
    bool request_stop() noexcept { return false; }
};

// What a terminating query had reached: sources constructed and token
// factory calls. The program ends with 86 only if both are as expected.
struct checkpoint {
    const int* made;
    const int* factory;
    int expected_made, expected_factory;
};
checkpoint reached{};

template<class Source>
[[noreturn]] void translate_and_exit(bool upstream_can_stop, int expected_made, int expected_factory) {
    Source::made = Source::factory = 0;
    reached = {&Source::made, &Source::factory, expected_made, expected_factory};
    std::set_terminate([] {
        std::_Exit(*reached.made == reached.expected_made && *reached.factory == reached.expected_factory ? 86 : 90);
    });
    std::inplace_stop_source upstream;
    const auto token = upstream_can_stop ? upstream.get_token() : std::inplace_stop_token{};
    const auto seen = watch_with<source_environment<Source>>(
        [&] { return token_env<std::inplace_stop_token>{token}; }, [] {});
    // Reached only if an answer was published: report what it said.
    std::_Exit(seen.first.requested ? 95 : 94);
}

TEST(ExecutionTaskStopDeathTest, G205_SourceWhoseDefaultHasNoFreshStateCannotTranslateARequest) {
    {
        // The control: for an upstream that cannot stop, the default
        // constructed source is the right answer and both observers are false.
        const auto seen = watch_with<source_environment<stateless_default_source>>(
            [] { return token_env<std::inplace_stop_token>{}; }, [] {});
        expect_seen(seen.first, false, false);
        expect_seen(seen.second, false, false);
        EXPECT_EQ(seen.equal, 1);
    }
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    // T1. For an upstream that can stop, a request would be lost. The
    // program ends after one source and before any token is issued.
    EXPECT_EXIT(translate_and_exit<stateless_default_source>(true, 1, 0), ::testing::ExitedWithCode(86), "");
    // T2. A default that is already requested cannot translate either.
    EXPECT_EXIT(translate_and_exit<requested_default_source>(true, 1, 0), ::testing::ExitedWithCode(86), "");
    // T3. It is no answer for an upstream that cannot stop: its token is
    // issued and examined, and a request is never fabricated.
    EXPECT_EXIT(translate_and_exit<requested_default_source>(false, 1, 1), ::testing::ExitedWithCode(86), "");
    // X4. A live source constructor that throws ends the query before any
    // freshness observation.
    EXPECT_EXIT(translate_and_exit<throwing_default_source>(true, 1, 0), ::testing::ExitedWithCode(86), "");
}

#if defined(__cpp_lib_jthread)
// A source with a disengaged representation of its own, as std::stop_source
// has. Its token has no disengaged value.
struct no_state_side;

template<bool Defaulted>
struct no_state_source {
    static inline int defaulted = 0, disengaged = 0;

    no_state_source()
        requires Defaulted
    {
        ++defaulted;
        inner.emplace();
    }
    explicit no_state_source(std::nostopstate_t) noexcept { ++disengaged; }

    loose_token<no_state_side> get_token() const noexcept {
        return {unused_counts, inner ? inner->get_token() : std::inplace_stop_token{}};
    }
    bool stop_possible() const noexcept { return inner.has_value(); }
    bool stop_requested() const noexcept { return inner && inner->stop_requested(); }
    bool request_stop() noexcept { return inner && inner->request_stop(); }

    std::optional<std::inplace_stop_source> inner;
};

static_assert(!std::default_initializable<no_state_source<false>>);

// A source that accepts nostopstate through a constructor that takes
// anything, and stays engaged. The tag proves nothing about the result.
struct sticky_source {
    static inline int tagged = 0, defaulted = 0, live = 0;

    sticky_source() { ++defaulted, ++live; }
    template<class Tag>
    explicit sticky_source(Tag&&) {
        ++tagged, ++live;
    }
    sticky_source(const sticky_source&) = delete;
    ~sticky_source() { --live; }

    defaulting_token get_token() const noexcept { return defaulting_token{inner.get_token()}; }
    bool stop_possible() const noexcept { return true; }
    bool stop_requested() const noexcept { return inner.stop_requested(); }
    bool request_stop() noexcept { return inner.request_stop(); }

    std::inplace_stop_source inner;
};

// A source with only a nostopstate constructor, which makes a fresh state
// all the same, and a token that only the source can make. Nothing
// disengaged exists for this pair, and the source is usable: its engaged,
// unrequested token is the bounded answer of ST-07.2.
struct tag_only_side;

struct tag_only_engaged_source {
    static inline stop_counts* current = nullptr;

    explicit tag_only_engaged_source(std::nostopstate_t) : inner(*current) {}

    basic_token<tag_only_side> get_token() const noexcept { return inner.get_token(); }
    bool stop_possible() const noexcept { return true; }
    bool stop_requested() const noexcept { return inner.stop_requested(); }
    bool request_stop() noexcept { return inner.request_stop(); }

    basic_source<tag_only_side> inner;
};

static_assert(!std::default_initializable<tag_only_engaged_source>);

TEST(ExecutionTaskStop, G205_TagOnlyEngagedSourceAnswersATokenlessReceiver) {
    stop_counts counts;
    tag_only_engaged_source::current = &counts;
    const auto seen =
        watch_with<source_environment<tag_only_engaged_source>>([] { return ex::env<>{}; }, [] {});
    tag_only_engaged_source::current = nullptr;
    expect_seen(seen.first, false, true);
    expect_seen(seen.second, false, true);
    EXPECT_EQ(seen.equal, 1);
    EXPECT_EQ(counts.sources, 1);
    EXPECT_EQ(counts.live_sources, 0);
    EXPECT_EQ(counts.requests, 0);
    EXPECT_EQ(counts.registrations, 0);
}

TEST(ExecutionTaskStop, G205_SourceWithANoStopStateRepresentation) {
    {
        // No default constructor at all, and a receiver with no stop query.
        // The requestable construction is never instantiated.
        no_state_source<false>::disengaged = 0;
        const auto seen =
            watch_with<source_environment<no_state_source<false>>>([] { return ex::env<>{}; }, [] {});
        expect_seen(seen.first, false, false);
        expect_seen(seen.second, false, false);
        EXPECT_EQ(seen.equal, 1);
        EXPECT_EQ(no_state_source<false>::disengaged, 1);
    }
    {
        // An upstream found impossible at the first query.
        stop_counts counts;
        const impossible_upstream_token impossible{counts, std::inplace_stop_token{}};
        no_state_source<true>::defaulted = no_state_source<true>::disengaged = 0;
        const auto seen = watch_with<source_environment<no_state_source<true>>>(
            [&] { return token_env<impossible_upstream_token>{impossible}; }, [] {});
        expect_seen(seen.first, false, false);
        expect_seen(seen.second, false, false);
        EXPECT_EQ(seen.equal, 1);
        EXPECT_EQ(no_state_source<true>::disengaged, 1);
        EXPECT_EQ(no_state_source<true>::defaulted, 0);
        EXPECT_EQ(counts.registrations, 0);
    }
    {
        // The control: an upstream that can stop takes the default constructor.
        stop_counts counts;
        upstream_source upstream{counts};
        no_state_source<true>::defaulted = no_state_source<true>::disengaged = 0;
        const auto seen = watch_with<source_environment<no_state_source<true>>>(
            [&] { return token_env<upstream_token>{upstream.get_token()}; }, [&] { upstream.request_stop(); });
        expect_seen(seen.first, false, true);
        expect_seen(seen.second, true, true);
        EXPECT_EQ(seen.equal, 1);
        EXPECT_EQ(no_state_source<true>::defaulted, 1);
        EXPECT_EQ(no_state_source<true>::disengaged, 0);
        EXPECT_EQ(counts.registrations, 1);
    }
    {
        // The token type's default value is disengaged and is examined
        // first, so it is the answer and the tag constructor, which would
        // stay engaged, is never called.
        stop_counts counts;
        const impossible_upstream_token impossible{counts, std::inplace_stop_token{}};
        sticky_source::tagged = sticky_source::defaulted = 0;
        const auto seen = watch_with<source_environment<sticky_source>>(
            [&] { return token_env<impossible_upstream_token>{impossible}; }, [] {});
        expect_seen(seen.first, false, false);
        expect_seen(seen.second, false, false);
        EXPECT_EQ(seen.equal, 1);
        EXPECT_EQ(sticky_source::tagged, 0);
        EXPECT_EQ(sticky_source::defaulted, 0);
        EXPECT_EQ(sticky_source::live, 0);
        EXPECT_EQ(counts.registrations, 0);
    }
}

// std::stop_source itself, where the library's std::stop_token models
// stoppable_token.
template<class Token>
struct source_for {
    using type = std::stop_source;
};

template<class Token>
void std_stop_source_cases() {
    if constexpr (std::stoppable_token<Token>) {
        using environment = source_environment<typename source_for<Token>::type>;
        {
            // No stop query: stop_source(nostopstate), whose token is the
            // disengaged std::stop_token.
            std::optional<bool> is_default;
            gate held;
            outcome done;
            auto operation = ex::connect(
                watches_token<environment>(&held, [&](const Token& token, int) {
                    is_default = token == Token{};
                    EXPECT_FALSE(token.stop_requested());
                    EXPECT_FALSE(token.stop_possible());
                }),
                done.receiver([] { return ex::env<>{}; }));
            ex::start(operation);
            held.open();
            EXPECT_EQ(is_default, true);
            EXPECT_EQ(done.result, 1);
        }
        {
            // An in-place upstream that cannot stop, found at the first query.
            const auto seen =
                watch_with<environment>([] { return token_env<std::inplace_stop_token>{}; }, [] {});
            expect_seen(seen.first, false, false);
            expect_seen(seen.second, false, false);
            EXPECT_EQ(seen.equal, 1);
        }
        {
            // An in-place upstream that can: one std::stop_source forwards it.
            std::inplace_stop_source upstream;
            const auto seen = watch_with<environment>(
                [&] { return token_env<std::inplace_stop_token>{upstream.get_token()}; },
                [&] { upstream.request_stop(); });
            expect_seen(seen.first, false, true);
            expect_seen(seen.second, true, true);
            EXPECT_EQ(seen.equal, 1);
        }
        {
            // Later loss with the real thing: the last std::stop_source of
            // the upstream goes away after an in-place token was issued.
            std::optional<typename source_for<Token>::type> upstream{std::in_place};
            const Token upstream_token_value = upstream->get_token();
            std::optional<observation> upstream_after;
            const auto seen = watch_with<inline_environment>(
                [&] { return token_env<Token>{upstream_token_value}; }, [&] {
                    upstream.reset();
                    upstream_after = observe(upstream_token_value);
                });
            ASSERT_TRUE(upstream_after);
            expect_seen(*upstream_after, false, false);
            expect_seen(seen.first, false, true);
            expect_seen(seen.second, false, true);
            EXPECT_EQ(seen.equal, 1);
        }
    } else {
        GTEST_SKIP() << "std::stop_token does not model stoppable_token in this library";
    }
}

TEST(ExecutionTaskStop, G205_StdStopSourceAsTheTasksSource) { std_stop_source_cases<std::stop_token>(); }
#endif

// ---------------------------------------------------------------------------
// The rows of the W2 decision table that the cases above do not pin.

// A body that asks twice and looks at nothing, so that every observer call
// counted below was made by the translation.
template<class Environment>
ex::task<int, Environment> asks_twice(std::function<void(const token_of<Environment>&)> keep = {}) {
    auto first = co_await ex::read_env(std::get_stop_token);
    auto second = co_await ex::read_env(std::get_stop_token);
    if (keep) keep(first);
    co_return first == second ? 1 : 0;
}

struct poll_counts {
    sync_ops::atomic<int> possible{0}, requested{0};
};

// A token that counts the calls of its two observers.
template<class Side>
struct polled_token {
    template<class Callback>
    struct callback_type {
        template<class Init>
        callback_type(const polled_token& token, Init&& init) : inner(token.inner, std::forward<Init>(init)) {}

        std::inplace_stop_callback<Callback> inner;
    };

    polled_token(poll_counts& counts, std::inplace_stop_token inner) noexcept : counts(&counts), inner(inner) {}

    bool stop_requested() const noexcept {
        ++counts->requested;
        return inner.stop_requested();
    }
    bool stop_possible() const noexcept {
        ++counts->possible;
        return inner.stop_possible();
    }
    bool operator==(const polled_token& other) const noexcept { return inner == other.inner; }

    poll_counts* counts;
    std::inplace_stop_token inner;
};

// Its source. The default constructor makes a fresh state. The tag
// constructor template accepts nostopstate_t by signature and cannot be
// instantiated: this source is used only where no candidate is searched.
template<class Side>
struct polled_source {
    static inline poll_counts* current = nullptr;
    static inline int made = 0;

    polled_source() { ++made; }
    template<class Tag>
    explicit polled_source(Tag) {
        typename Tag::missing unusable;
    }

    polled_token<Side> get_token() const noexcept { return {*current, inner.get_token()}; }
    bool stop_possible() const noexcept { return true; }
    bool stop_requested() const noexcept { return inner.stop_requested(); }
    bool request_stop() noexcept { return inner.request_stop(); }

    std::inplace_stop_source inner;
};

// A receiver environment that counts evaluations of its stop token query.
// Answer is the query's result type: the token by value or by reference.
template<class Token, class Answer = Token>
struct reading_env {
    Answer query(std::get_stop_token_t) const noexcept {
        ++*reads;
        return *token;
    }

    const Token* token;
    int* reads;
};

// A token type that can never stop, with static observers that count.
struct never_token {
    static inline int polls = 0;

    template<class Callback>
    struct callback_type {
        template<class Init>
        callback_type(const never_token&, Init&&) noexcept {}
    };

    // Only its source and a receiver can make one. There is no default.
    explicit never_token(int id) noexcept : id(id) {}

    // A call at run time is counted. In a constant expression, where the
    // unstoppable_token concept evaluates them, they only answer.
    static constexpr bool stop_requested() noexcept {
        if (!std::is_constant_evaluated()) ++polls;
        return false;
    }
    static constexpr bool stop_possible() noexcept {
        if (!std::is_constant_evaluated()) ++polls;
        return false;
    }
    bool operator==(const never_token&) const noexcept = default;

    int id;
};

static_assert(std::unstoppable_token<never_token>);
static_assert(!std::default_initializable<never_token>);

// A constraint that cannot even be asked about a tag other than the
// application's: asking is an error, not an unsatisfied constraint.
struct application_tag {};

template<class Tag>
struct only_the_application_tag {
    static_assert(std::same_as<Tag, application_tag>, "this source must not be asked about any other tag");
    static constexpr bool value = true;
};

// Its source has an application constructor only, behind that constraint,
// and a destructor that only the application can call. The translation knows
// no way to construct it, and for a token of the task's own type it must
// neither ask about its constructors nor hold storage for one.
struct never_source {
    template<class Tag>
        requires only_the_application_tag<Tag>::value
    explicit never_source(Tag) noexcept {}

    never_token get_token() const noexcept { return never_token{0}; }
    static constexpr bool stop_possible() noexcept { return false; }
    static constexpr bool stop_requested() noexcept { return false; }
    bool request_stop() noexcept { return false; }

private:
    ~never_source() = default;
};

// The same constraint on a source whose token has a default that is known
// to be impossible. That answer ends the search before any constructor of
// the source is asked about.
struct default_answer_source {
    template<class Tag>
        requires only_the_application_tag<Tag>::value
    explicit default_answer_source(Tag) noexcept {}

    struct token {
        template<class Callback>
        struct callback_type {
            template<class Init>
            callback_type(const token&, Init&&) noexcept {}
        };

        token() noexcept = default;
        static constexpr bool stop_requested() noexcept { return false; }
        static constexpr bool stop_possible() noexcept { return false; }
        bool operator==(const token&) const noexcept = default;
    };

    token get_token() const noexcept { return {}; }
    static constexpr bool stop_possible() noexcept { return false; }
    static constexpr bool stop_requested() noexcept { return false; }
    bool request_stop() noexcept { return false; }
};

struct read_side;
struct other_side;

template<class Environment, class MakeEnv>
int run_asks_twice(MakeEnv make_env, std::function<void(const token_of<Environment>&)> keep = {}) {
    outcome done;
    {
        auto operation = ex::connect(asks_twice<Environment>(std::move(keep)), done.receiver(make_env));
        ex::start(operation);
    }
    EXPECT_EQ(done.terminals, 1);
    return done.result.value_or(-1);
}

TEST(ExecutionTaskStop, G205_ReceiverReadsAndPollsByRow) {
    using polled_environment = source_environment<polled_source<read_side>>;
    using own_token = polled_token<read_side>;
    using other_token = polled_token<other_side>;
    {
        // R1, exact: the receiver's expression is evaluated at each query,
        // and the task polls nothing.
        poll_counts polls;
        std::inplace_stop_source upstream;
        const own_token token{polls, upstream.get_token()};
        int reads = 0;
        polled_source<read_side>::made = 0;
        EXPECT_EQ(run_asks_twice<polled_environment>([&] { return reading_env<own_token>{&token, &reads}; }), 1);
        EXPECT_EQ(reads, 2);
        EXPECT_EQ(polls.possible, 0);
        EXPECT_EQ(polls.requested, 0);
        EXPECT_EQ(polled_source<read_side>::made, 0);
    }
    {
        // R2, statically impossible with the in-place token: the receiver's
        // query is never evaluated.
        const never_token token{1};
        int reads = 0;
        never_token::polls = 0;
        EXPECT_EQ(run_asks_twice<inline_environment>([&] { return reading_env<never_token>{&token, &reads}; }), 1);
        EXPECT_EQ(reads, 0);
        EXPECT_EQ(never_token::polls, 0);
    }
    {
        // R4, statically impossible with a searched candidate: no read either.
        const never_token token{1};
        int reads = 0;
        defaulting_source::constructed = 0;
        EXPECT_EQ(run_asks_twice<source_environment<defaulting_source>>(
                      [&] { return reading_env<never_token>{&token, &reads}; }),
                  1);
        EXPECT_EQ(reads, 0);
        EXPECT_EQ(never_token::polls, 0);
        EXPECT_EQ(defaulting_source::constructed, 0);
    }
    {
        // R4 again, with an upstream answered as a volatile lvalue. It is
        // never read, so no copy from a volatile object is ever formed, and
        // the token default answers both queries.
        volatile never_token token{1};
        int reads = 0;
        struct volatile_env {
            volatile never_token& query(std::get_stop_token_t) const noexcept {
                ++*reads;
                return *token;
            }
            volatile never_token* token;
            int* reads;
        };
        defaulting_source::constructed = 0;
        std::optional<observation> answered;
        EXPECT_EQ(run_asks_twice<source_environment<defaulting_source>>(
                      [&] { return volatile_env{&token, &reads}; },
                      [&](const defaulting_token& seen) { answered = observe(seen); }),
                  1);
        EXPECT_EQ(reads, 0);
        ASSERT_TRUE(answered);
        expect_seen(*answered, false, false);
        EXPECT_EQ(defaulting_source::constructed, 0);
    }
    {
        // R4 with a token default that is known to be impossible: the search
        // ends there, and the source's constructors are never asked about.
        const never_token token{1};
        int reads = 0;
        EXPECT_EQ(run_asks_twice<source_environment<default_answer_source>>(
                      [&] { return reading_env<never_token>{&token, &reads}; }),
                  1);
        EXPECT_EQ(reads, 0);
    }
    {
        // R3, statically impossible and of the issued type: one read, kept
        // as the answer, and no observer call. The source has only an
        // application constructor behind a constraint that must not be
        // asked about nostopstate_t, and no destructor the task could call.
        // Only the answer is stored.
        const never_token token{7};
        int reads = 0;
        never_token::polls = 0;
        std::optional<never_token> answered;
        EXPECT_EQ(run_asks_twice<source_environment<never_source>>(
                      [&] { return reading_env<never_token, const never_token&>{&token, &reads}; },
                      [&](const never_token& seen) { answered = seen; }),
                  1);
        EXPECT_EQ(reads, 1);
        // Not one observer call at run time, on the sample or on anything else.
        EXPECT_EQ(never_token::polls, 0);
        ASSERT_TRUE(answered);
        // The receiver's own token, not one the source would issue.
        EXPECT_EQ(answered->id, 7);
    }
    {
        // R5, a mismatched token that can stop: one read and one poll of
        // stop_possible. stop_requested is never called on it.
        poll_counts upstream_polls;
        stop_counts task_counts;
        task_counts_scope scope{task_counts};
        std::inplace_stop_source upstream;
        const other_token token{upstream_polls, upstream.get_token()};
        int reads = 0;
        EXPECT_EQ(run_asks_twice<counted_stop_environment>([&] { return reading_env<other_token>{&token, &reads}; }),
                  1);
        EXPECT_EQ(reads, 1);
        EXPECT_EQ(upstream_polls.possible, 1);
        EXPECT_EQ(upstream_polls.requested, 0);
        EXPECT_EQ(task_counts.sources, 1);
        EXPECT_EQ(task_counts.token_calls, 1);
    }
    {
        // R6, an impossible token of the issued type through a reference:
        // one read, one poll, and the sample itself is the answer. No source
        // is made, and the candidate search is never instantiated for this
        // source, whose tag constructor could not be.
        poll_counts polls;
        const own_token token{polls, std::inplace_stop_token{}};
        int reads = 0;
        polled_source<read_side>::made = 0;
        std::optional<bool> same_as_upstream;
        EXPECT_EQ(run_asks_twice<polled_environment>(
                      [&] { return reading_env<own_token, const own_token&>{&token, &reads}; },
                      [&](const own_token& seen) { same_as_upstream = seen == token; }),
                  1);
        EXPECT_EQ(reads, 1);
        EXPECT_EQ(polls.possible, 1);
        EXPECT_EQ(polls.requested, 0);
        EXPECT_EQ(same_as_upstream, true);
        EXPECT_EQ(polled_source<read_side>::made, 0);
    }
    {
        // R7, an impossible token of another type: one read and one poll.
        poll_counts polls;
        const other_token token{polls, std::inplace_stop_token{}};
        int reads = 0;
        EXPECT_EQ(run_asks_twice<inline_environment>([&] { return reading_env<other_token>{&token, &reads}; }), 1);
        EXPECT_EQ(reads, 1);
        EXPECT_EQ(polls.possible, 1);
        EXPECT_EQ(polls.requested, 0);
    }
}

// A token whose default value is engaged, and which counts the calls of its
// observers. Its source is default constructed fresh. For an upstream that
// cannot stop, the default token is examined and discarded, and the default
// source is the answer: each is asked both questions, once.
struct eager_token {
    static inline std::inplace_stop_source shared_state;
    static inline int possible_polls = 0, requested_polls = 0;

    template<class Callback>
    struct callback_type {
        template<class Init>
        callback_type(const eager_token& token, Init&& init) : inner(token.inner, std::forward<Init>(init)) {}

        std::inplace_stop_callback<Callback> inner;
    };

    eager_token() noexcept : inner(shared_state.get_token()) {}
    explicit eager_token(std::inplace_stop_token inner) noexcept : inner(inner) {}

    bool stop_requested() const noexcept {
        ++requested_polls;
        return inner.stop_requested();
    }
    bool stop_possible() const noexcept {
        ++possible_polls;
        return inner.stop_possible();
    }
    bool operator==(const eager_token&) const noexcept = default;

    std::inplace_stop_token inner;
};

struct eager_source {
    eager_token get_token() const noexcept { return eager_token{inner.get_token()}; }
    bool stop_possible() const noexcept { return true; }
    bool stop_requested() const noexcept { return inner.stop_requested(); }
    bool request_stop() noexcept { return inner.request_stop(); }

    std::inplace_stop_source inner;
};

TEST(ExecutionTaskStop, G205_EachExaminedCandidateIsAskedBothQuestionsOnce) {
    stop_counts upstream_counts;
    const impossible_upstream_token impossible{upstream_counts, std::inplace_stop_token{}};
    eager_token::possible_polls = eager_token::requested_polls = 0;
    int possible_polls = -1, requested_polls = -1;
    std::optional<observation> answered;
    EXPECT_EQ(run_asks_twice<source_environment<eager_source>>(
                  [&] { return token_env<impossible_upstream_token>{impossible}; },
                  [&](const eager_token& seen) {
                      // Before the test itself asks anything.
                      possible_polls = eager_token::possible_polls;
                      requested_polls = eager_token::requested_polls;
                      answered = observe(seen);
                  }),
              1);
    // The engaged token default, and then the default constructed source.
    EXPECT_EQ(possible_polls, 2);
    EXPECT_EQ(requested_polls, 2);
    ASSERT_TRUE(answered);
    expect_seen(*answered, false, true);
    EXPECT_EQ(upstream_counts.registrations, 0);
}

// R1 for a receiver without a stop token query: a source whose token type is
// never_stop_token is an exact match, and need not be constructible.
struct tokenless_source {
    tokenless_source() = delete;

    std::never_stop_token get_token() const noexcept { return {}; }
    static constexpr bool stop_possible() noexcept { return false; }
    static constexpr bool stop_requested() noexcept { return false; }
    bool request_stop() noexcept { return false; }
};

TEST(ExecutionTaskStop, G205_AbsentQueryIsExactForANeverStopSource) {
    static_assert(
        std::same_as<ex::task<int, source_environment<tokenless_source>>::stop_token_type, std::never_stop_token>);
    EXPECT_EQ(run_asks_twice<source_environment<tokenless_source>>([] { return ex::env<>{}; }), 1);
}

// Q2 and Q3. tagged_source can make a disengaged token, but only through an
// application constructor. That representation is not reachable. A tokenless
// receiver, and an impossible token of a type derived from the task's token
// type, both get the engaged, unrequested default source (ST-07.2).
struct derived_tagged_token : loose_token<tagged_side> {
    using loose_token<tagged_side>::loose_token;
};

static_assert(std::stoppable_token<derived_tagged_token>);

TEST(ExecutionTaskStop, G205_UnreachableRepresentationsAreNotUsed) {
    using environment = source_environment<tagged_source>;
    {
        stop_counts task_counts;
        tagged_source::current = &task_counts;
        const auto seen = watch_with<environment>([] { return ex::env<>{}; }, [] {});
        expect_seen(seen.first, false, true);
        expect_seen(seen.second, false, true);
        EXPECT_EQ(seen.equal, 1);
        EXPECT_EQ(task_counts.sources, 1);
    }
    {
        stop_counts task_counts, upstream_counts;
        tagged_source::current = &task_counts;
        const derived_tagged_token impossible{upstream_counts, std::inplace_stop_token{}};
        const auto seen =
            watch_with<environment>([&] { return token_env<derived_tagged_token>{impossible}; }, [] {});
        // The upstream is not converted to its base, although that base is
        // the task's token type.
        expect_seen(seen.first, false, true);
        expect_seen(seen.second, false, true);
        EXPECT_EQ(seen.equal, 1);
        EXPECT_EQ(task_counts.sources, 1);
        EXPECT_EQ(upstream_counts.registrations, 0);
    }
    tagged_source::current = nullptr;
}

#if defined(__cpp_lib_jthread)
// A source whose two recognized constructors can each be set to make a state
// that is impossible, eligible (possible and not requested) or requested.
// Every token keeps a record alive until its last copy is gone, and a source
// that is destroyed before that counts a violation.
enum class dial { impossible, eligible, requested };

struct dial_record {
    dial tag_result = dial::impossible, default_result = dial::impossible;
    int tag_made = 0, default_made = 0, live = 0, factory = 0, released_before_token = 0;
    int possible_polls = 0, requested_polls = 0;
    // The factory call that throws, counted from one. Zero: none.
    int throwing_factory_call = 0;
};
dial_record* dials = nullptr;

struct dial_token {
    template<class Callback>
    struct callback_type {
        template<class Init>
        callback_type(const dial_token& token, Init&& init) : inner(token.inner, std::forward<Init>(init)) {}

        std::inplace_stop_callback<Callback> inner;
    };

    dial_token(std::inplace_stop_token inner, char origin, std::shared_ptr<void> life) noexcept
        : inner(inner), origin(origin), life(std::move(life)) {}

    bool stop_requested() const noexcept {
        ++dials->requested_polls;
        return inner.stop_requested();
    }
    bool stop_possible() const noexcept {
        ++dials->possible_polls;
        return inner.stop_possible();
    }
    bool operator==(const dial_token& other) const noexcept {
        return inner == other.inner && origin == other.origin;
    }

    std::inplace_stop_token inner;
    char origin;
    std::shared_ptr<void> life;
};

struct dial_source {
    explicit dial_source(std::nostopstate_t) : origin('t') {
        ++dials->tag_made;
        set(dials->tag_result);
    }
    dial_source() : origin('d') {
        ++dials->default_made;
        set(dials->default_result);
    }
    dial_source(const dial_source&) = delete;
    ~dial_source() {
        if (*tokens_alive != 0) ++dials->released_before_token;
        --dials->live;
    }

    dial_token get_token() const {
        if (++dials->factory == dials->throwing_factory_call) throw std::runtime_error("dial_source");
        ++*tokens_alive;
        return {inner ? inner->get_token() : std::inplace_stop_token{}, origin,
                std::shared_ptr<void>(nullptr, [alive = tokens_alive](void*) { --*alive; })};
    }
    bool stop_possible() const noexcept { return inner.has_value(); }
    bool stop_requested() const noexcept { return inner && inner->stop_requested(); }
    bool request_stop() noexcept { return inner && inner->request_stop(); }

    void set(dial result) {
        ++dials->live;
        if (result == dial::impossible) return;
        inner.emplace();
        if (result == dial::requested) inner->request_stop();
    }

    char origin;
    std::optional<std::inplace_stop_source> inner;
    std::shared_ptr<int> tokens_alive = std::make_shared<int>(0);
};

static_assert(std::stoppable_token<dial_token>);
static_assert(!std::default_initializable<dial_token>);

struct dial_outcome {
    char origin = '?';
    observation seen;
    int equal = -1, tag_made = -1, default_made = -1, live_while_running = -1;
    // Observer calls on candidate tokens, before the test asks anything.
    int possible_polls = -1, requested_polls = -1;
};

dial_outcome turn(dial tag_result, dial default_result) {
    dial_record record;
    record.tag_result = tag_result;
    record.default_result = default_result;
    dials = &record;
    dial_outcome result;
    result.equal = run_asks_twice<source_environment<dial_source>>([] { return ex::env<>{}; },
                                                                   [&](const dial_token& token) {
                                                                       result.possible_polls = record.possible_polls;
                                                                       result.requested_polls = record.requested_polls;
                                                                       result.origin = token.origin;
                                                                       result.seen = observe(token);
                                                                       result.live_while_running = record.live;
                                                                   });
    dials = nullptr;
    result.tag_made = record.tag_made;
    result.default_made = record.default_made;
    // Every source is gone, and none went before a token it had issued:
    // not a candidate that was examined and dropped, and not the answer.
    EXPECT_EQ(record.live, 0);
    EXPECT_EQ(record.released_before_token, 0);
    EXPECT_EQ(record.factory, record.tag_made + record.default_made);
    return result;
}

void expect_turn(const dial_outcome& result, char origin, bool possible, int tag_made, int default_made) {
    EXPECT_EQ(result.origin, origin);
    expect_seen(result.seen, false, possible);
    EXPECT_EQ(result.equal, 1);
    EXPECT_EQ(result.tag_made, tag_made);
    EXPECT_EQ(result.default_made, default_made);
    // One source survives the search.
    EXPECT_EQ(result.live_while_running, 1);
    // Every source that was examined had its token asked both questions,
    // once each. This includes a default source examined while an engaged
    // tag source is held.
    EXPECT_EQ(result.possible_polls, tag_made + default_made);
    EXPECT_EQ(result.requested_polls, tag_made + default_made);
}

// The source rows of the candidate table, for a tokenless receiver.
TEST(ExecutionTaskStop, G205_SourceCandidatesAreExaminedInOrder) {
    {
        SCOPED_TRACE("impossible tag source: the default source is not constructed");
        expect_turn(turn(dial::impossible, dial::eligible), 't', false, 1, 0);
    }
    {
        SCOPED_TRACE("eligible tag source held, impossible default source wins");
        expect_turn(turn(dial::eligible, dial::impossible), 'd', false, 1, 1);
    }
    {
        SCOPED_TRACE("eligible tag source held, eligible default source: the first found stays");
        expect_turn(turn(dial::eligible, dial::eligible), 't', true, 1, 1);
    }
    {
        SCOPED_TRACE("eligible tag source held, requested default source");
        expect_turn(turn(dial::eligible, dial::requested), 't', true, 1, 1);
    }
    {
        SCOPED_TRACE("requested tag source dropped, impossible default source");
        expect_turn(turn(dial::requested, dial::impossible), 'd', false, 1, 1);
    }
    {
        SCOPED_TRACE("requested tag source dropped, eligible default source");
        expect_turn(turn(dial::requested, dial::eligible), 'd', true, 1, 1);
    }
}

[[noreturn]] void turn_and_exit(dial tag_result, dial default_result, int throwing_factory_call) {
    static dial_record record;
    record.tag_result = tag_result;
    record.default_result = default_result;
    record.throwing_factory_call = throwing_factory_call;
    dials = &record;
    // Both sources were constructed and the second factory was reached.
    std::set_terminate([] {
        std::_Exit(dials->tag_made == 1 && dials->default_made == 1 && dials->factory == 2 ? 86 : 90);
    });
    (void)run_asks_twice<source_environment<dial_source>>([] { return ex::env<>{}; });
    // Reached only if an answer was published.
    std::_Exit(94);
}

TEST(ExecutionTaskStopDeathTest, G205_SourceCandidatesThatCannotAnswerEndTheQuery) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    // T3: both sources report a request.
    EXPECT_EXIT(turn_and_exit(dial::requested, dial::requested, 0), ::testing::ExitedWithCode(86), "");
    // X4: the second candidate's factory throws while an eligible first
    // candidate is held. The query ends. It does not publish the held one.
    EXPECT_EXIT(turn_and_exit(dial::eligible, dial::eligible, 2), ::testing::ExitedWithCode(86), "");
}

// Q5. The token's default constructor may throw, so it is not examined. A
// source made from nostopstate answers.
struct guarded_side;

struct throwing_default_token : loose_token<guarded_side> {
    static inline int defaults = 0;

    using loose_token<guarded_side>::loose_token;
    throwing_default_token() : loose_token<guarded_side>(unused_counts, std::inplace_stop_token{}) {
        ++defaults;
        throw std::runtime_error("throwing_default_token");
    }
};

struct guarded_source {
    explicit guarded_source(std::nostopstate_t) noexcept {}
    guarded_source() { inner.emplace(); }

    throwing_default_token get_token() const noexcept {
        return {unused_counts, inner ? inner->get_token() : std::inplace_stop_token{}};
    }
    bool stop_possible() const noexcept { return inner.has_value(); }
    bool stop_requested() const noexcept { return inner && inner->stop_requested(); }
    bool request_stop() noexcept { return inner && inner->request_stop(); }

    std::optional<std::inplace_stop_source> inner;
};

static_assert(std::default_initializable<throwing_default_token>);
static_assert(!std::is_nothrow_default_constructible_v<throwing_default_token>);

TEST(ExecutionTaskStop, G205_PotentiallyThrowingTokenDefaultIsNotExamined) {
    throwing_default_token::defaults = 0;
    const auto seen = watch_with<source_environment<guarded_source>>([] { return ex::env<>{}; }, [] {});
    expect_seen(seen.first, false, false);
    expect_seen(seen.second, false, false);
    EXPECT_EQ(seen.equal, 1);
    EXPECT_EQ(throwing_default_token::defaults, 0);
}

// A token type that can never stop, a source made from nostopstate, and a
// zero-argument constructor template that cannot be instantiated. The tag
// source's token is known to be impossible, so the default source after it
// is never instantiated.
struct pruned_source {
    explicit pruned_source(std::nostopstate_t) noexcept {}
    template<class Unused = void>
    pruned_source() {
        typename Unused::missing unusable;
    }

    never_token get_token() const noexcept { return never_token{3}; }
    static constexpr bool stop_possible() noexcept { return false; }
    static constexpr bool stop_requested() noexcept { return false; }
    bool request_stop() noexcept { return false; }
};

TEST(ExecutionTaskStop, G205_CandidatesAfterAGuaranteedAnswerAreNotInstantiated) {
    std::optional<never_token> answered;
    EXPECT_EQ(run_asks_twice<source_environment<pruned_source>>(
                  [] { return ex::env<>{}; }, [&](const never_token& seen) { answered = seen; }),
              1);
    ASSERT_TRUE(answered);
    EXPECT_EQ(answered->id, 3);
}
#endif

// An upstream whose possibility ends when its last owner goes away without a
// request, as std::stop_token's does.
struct fading_state {
    std::inplace_stop_source inner;
    sync_ops::atomic<int> owners{0};
};

struct fading_token {
    template<class Callback>
    struct callback_type {
        template<class Init>
        callback_type(const fading_token& token, Init&& init)
            : inner(token.state->inner.get_token(), std::forward<Init>(init)) {}

        std::inplace_stop_callback<Callback> inner;
    };

    bool stop_requested() const noexcept { return state->inner.stop_requested(); }
    bool stop_possible() const noexcept { return stop_requested() || state->owners.load() != 0; }
    bool operator==(const fading_token&) const noexcept = default;

    std::shared_ptr<fading_state> state;
};

// One owner of the upstream stop state. The state stops being possible when
// the last owner is destroyed.
struct fading_owner {
    explicit fading_owner(std::shared_ptr<fading_state> shared) : state(std::move(shared)) { ++state->owners; }
    fading_owner(const fading_owner&) = delete;
    ~fading_owner() { --state->owners; }

    fading_token get_token() const noexcept { return {state}; }

    std::shared_ptr<fading_state> state;
};

static_assert(std::stoppable_token<fading_token>);

TEST(ExecutionTaskStop, G205_IssuedTokenStaysPossibleAfterTheLastUpstreamSourceIsGone) {
    auto state = std::make_shared<fading_state>();
    std::optional<fading_owner> last_owner{std::in_place, state};
    const fading_token upstream = last_owner->get_token();
    std::optional<observation> upstream_after;
    const auto seen = watch_with<inline_environment>([&] { return token_env<fading_token>{upstream}; }, [&] {
        // The last upstream owner is released. No stop was requested.
        last_owner.reset();
        upstream_after = observe(upstream);
    });
    ASSERT_TRUE(upstream_after);
    expect_seen(*upstream_after, false, false);
    expect_seen(seen.first, false, true);
    // The recorded deviation ST-07.2: the issued in-place token cannot
    // follow the upstream to impossible. It never reports a request.
    expect_seen(seen.second, false, true);
    EXPECT_EQ(seen.equal, 1);
}

// The upstream was requested and then lost its last owner, all before the
// first query. Its token still reports requested and possible, so it is
// translated: the registration runs the forwarder at once and the first
// answer already observes the request.
TEST(ExecutionTaskStop, G205_RequestedUpstreamWithoutOwnersIsStillTranslated) {
    auto state = std::make_shared<fading_state>();
    std::optional<fading_owner> last_owner{std::in_place, state};
    const fading_token upstream = last_owner->get_token();
    state->inner.request_stop();
    last_owner.reset();
    expect_seen(observe(upstream), true, true);
    stop_counts task_counts;
    task_counts_scope scope{task_counts};
    const auto seen =
        watch_with<counted_stop_environment>([&] { return token_env<fading_token>{upstream}; }, [] {});
    expect_seen(seen.first, true, true);
    expect_seen(seen.second, true, true);
    EXPECT_EQ(seen.equal, 1);
    EXPECT_EQ(task_counts.sources, 1);
    EXPECT_EQ(task_counts.requests, 1);
}

// G2-06. The adapter itself, with the task's kind of source and an observer.
// The observer reports the points that the public fixture cannot see: a
// query that found the initialization in progress, and a reader about to
// copy the issued token.
struct adapter_events {
    sync_ops::atomic<int> waiting{0}, reads{0}, reads_before_publication{0};
    sync_ops::atomic<bool> published{false};
};

struct event_observer {
    adapter_events* events;

    void operator()(adapters::__proxy_stop_event event) const noexcept {
        if (event == adapters::__proxy_stop_event::waiting) ++events->waiting;
        if (event == adapters::__proxy_stop_event::publish) events->published = true;
        if (event == adapters::__proxy_stop_event::read_token) {
            // Every successful read comes after the publication.
            if (!events->published.load()) ++events->reads_before_publication;
            ++events->reads;
        }
    }
};

TEST(ExecutionTaskStop, G206_SecondFirstQueryWaitsForTheInitializer) {
    stop_counts task_counts, upstream_counts;
    task_counts_scope scope{task_counts};
    upstream_source upstream{upstream_counts};
    adapter_events events;
    adapters::__proxy_stop_adapter<upstream_token, event_observer, task_source> adapter{event_observer{&events}};
    sync_ops::atomic<bool> in_registration{false};
    sync_ops::atomic<bool> other_returned{false};
    bool other_was_waiting = false, other_returned_while_held = true;
    int sources_while_held = -1, reads_while_held = -1;
    // The initializer stays inside the upstream registration until the other
    // query has reported that it lost the claim and is about to wait. A
    // serial run never reports that, and fails here.
    upstream_counts.during_registration = [&] {
        in_registration = true;
        other_was_waiting = wait_for([&] { return events.waiting.load() == 1; });
        // The other query is at its wait. The answer is already cached, so a
        // query that skipped the wait could return it now, before the
        // publication. It gets ample time to do so, and must not.
        for (int i = 0; i != 100 && !other_returned.load(); ++i) pause_briefly();
        other_returned_while_held = other_returned.load();
        reads_while_held = events.reads.load();
        sources_while_held = task_counts.sources.load();
    };
    const auto read = [&]() noexcept { return upstream.get_token(); };
    std::optional<task_token> first, second;
    sync_ops::thread initializer{[&] { first.emplace(adapter.__query(read)); }};
    ASSERT_TRUE(wait_for([&] { return in_registration.load(); }));
    sync_ops::thread other{[&] {
        second.emplace(adapter.__query(read));
        other_returned = true;
    }};
    initializer.join();
    other.join();
    EXPECT_TRUE(other_was_waiting);
    EXPECT_FALSE(other_returned_while_held);
    EXPECT_EQ(events.waiting, 1);
    EXPECT_EQ(events.reads, 2);
    EXPECT_EQ(events.reads_before_publication, 0);
    EXPECT_EQ(reads_while_held, 0);
    EXPECT_EQ(sources_while_held, 1);
    ASSERT_TRUE(first && second);
    EXPECT_TRUE(*first == *second);
    // One source, one issued token, one upstream read and one registration.
    EXPECT_EQ(task_counts.sources, 1);
    EXPECT_EQ(task_counts.token_calls, 1);
    EXPECT_EQ(task_counts.const_token_calls, 0);
    EXPECT_EQ(upstream_counts.token_calls, 1);
    EXPECT_EQ(upstream_counts.registrations, 1);
    upstream.request_stop();
    EXPECT_EQ(task_counts.requests, 1);
    EXPECT_TRUE(first->stop_requested() && second->stop_requested());
    adapter.__finish();
    EXPECT_EQ(upstream_counts.live_registrations, 0);
    EXPECT_EQ(task_counts.live_sources, 1);
}

// A token whose copy constructor is not safe for two concurrent copies of
// one object. It detects them without racing itself.
struct copy_probe {
    sync_ops::atomic<int> overlaps{0}, copies{0};
    sync_ops::atomic<bool> armed{false};
    std::function<void()> inside_first_copy;
};
copy_probe* fragile = nullptr;

struct fragile_token {
    template<class Callback>
    struct callback_type {
        template<class Init>
        callback_type(const fragile_token& token, Init&& init) : inner(token.inner, std::forward<Init>(init)) {}

        std::inplace_stop_callback<Callback> inner;
    };

    explicit fragile_token(std::inplace_stop_token inner) noexcept : inner(inner) {}
    fragile_token(const fragile_token& other) noexcept : inner(other.inner) {
        if (other.copying.fetch_add(1) != 0) ++fragile->overlaps;
        ++fragile->copies;
        if (fragile->armed.exchange(false)) fragile->inside_first_copy();
        other.copying.fetch_sub(1);
    }
    fragile_token& operator=(const fragile_token& other) noexcept {
        inner = other.inner;
        return *this;
    }

    bool stop_requested() const noexcept { return inner.stop_requested(); }
    bool stop_possible() const noexcept { return inner.stop_possible(); }
    bool operator==(const fragile_token& other) const noexcept { return inner == other.inner; }

    std::inplace_stop_token inner;
    mutable sync_ops::atomic<int> copying{0};
};

struct fragile_source {
    fragile_token get_token() const noexcept { return fragile_token{inner.get_token()}; }
    bool stop_possible() const noexcept { return true; }
    bool stop_requested() const noexcept { return inner.stop_requested(); }
    bool request_stop() noexcept { return inner.request_stop(); }

    std::inplace_stop_source inner;
};

static_assert(std::stoppable_token<fragile_token>);

static_assert(!adapters::__proxy_shareable_token<fragile_token>);
static_assert(adapters::__proxy_shareable_token<std::inplace_stop_token>);

// Two readers copy from one token slot. The first is held inside its copy.
// The second announces itself and then calls the slot.
template<bool Serialized>
struct slot_race {
    copy_probe probe;
    bool second_entered_copy = false, second_was_about_to_copy = false;

    void run() {
        fragile = &probe;
        std::inplace_stop_source state;
        adapters::__proxy_token_slot<fragile_token, Serialized> slot;
        slot.__token.emplace(fragile_token{state.get_token()});
        // Count the two readers' copies only.
        probe.copies = 0;
        sync_ops::atomic<bool> first_inside{false}, second_calling{false};
        probe.inside_first_copy = [&] {
            first_inside = true;
            second_was_about_to_copy = wait_for([&] { return second_calling.load(); });
            if (Serialized) {
                // The second reader is at the lock. It cannot report that,
                // so it gets ample time to enter the copy if it could.
                for (int i = 0; i != 100 && probe.copies.load() < 2; ++i) pause_briefly();
            } else {
                // Without a lock the second reader enters the copy, and the
                // first stays until it has.
                (void)wait_for([&] { return probe.copies.load() == 2; });
            }
            second_entered_copy = probe.copies.load() == 2;
        };
        probe.armed = true;
        sync_ops::thread first{[&] { (void)slot.__copy(); }};
        ASSERT_TRUE(wait_for([&] { return first_inside.load(); }));
        sync_ops::thread second{[&] {
            second_calling = true;
            (void)slot.__copy();
        }};
        first.join();
        second.join();
        fragile = nullptr;
    }
};

TEST(ExecutionTaskStop, G206_CopiesOfTheIssuedTokenAreSerialized) {
    {
        // The control without the lock: the two copies overlap, every time,
        // and the token reports it. This is what the schedule produces when
        // nothing serializes the readers.
        slot_race<false> unlocked;
        unlocked.run();
        EXPECT_TRUE(unlocked.second_was_about_to_copy);
        EXPECT_TRUE(unlocked.second_entered_copy);
        EXPECT_EQ(unlocked.probe.overlaps, 1);
    }
    {
        // The slot the adapter uses for this token: the second reader does
        // not enter the copy while the first is inside.
        slot_race<true> locked;
        locked.run();
        EXPECT_TRUE(locked.second_was_about_to_copy);
        EXPECT_FALSE(locked.second_entered_copy);
        EXPECT_EQ(locked.probe.overlaps, 0);
        EXPECT_EQ(locked.probe.copies, 2);
    }
}

// The adapter with that token, two readers after the publication.
TEST(ExecutionTaskStop, G206_AdapterReadersCopyTheIssuedTokenOneAtATime) {
    copy_probe probe;
    fragile = &probe;
    std::inplace_stop_source upstream;
    adapter_events events;
    adapters::__proxy_stop_adapter<std::inplace_stop_token, event_observer, fragile_source> adapter{
        event_observer{&events}};
    const auto read = [&]() noexcept { return upstream.get_token(); };
    (void)adapter.__query(read);
    const int reads_before = events.reads.load();
    probe.copies = 0;
    bool other_reader_arrived = false, other_entered_copy = false;
    probe.inside_first_copy = [&] {
        other_reader_arrived = wait_for([&] { return events.reads.load() == reads_before + 2; });
        for (int i = 0; i != 100 && probe.copies.load() < 2; ++i) pause_briefly();
        other_entered_copy = probe.copies.load() == 2;
    };
    probe.armed = true;
    sync_ops::thread left{[&] { (void)adapter.__query(read); }};
    sync_ops::thread right{[&] { (void)adapter.__query(read); }};
    left.join();
    right.join();
    fragile = nullptr;
    EXPECT_TRUE(other_reader_arrived);
    EXPECT_FALSE(other_entered_copy);
    EXPECT_EQ(probe.overlaps, 0);
    EXPECT_EQ(events.reads_before_publication, 0);
    adapter.__finish();
}

// The public fixture. Two threads make their first query on the same
// environment while the initializer is held in the upstream registration.
// This run is not forced to overlap: the adapter cases above are.
struct start_line {
    void wait() {
        sync_ops::unique_lock lock{mutex};
        ++waiting;
        arrived.notify_all();
        go.wait(lock, [this] { return open; });
    }

    void release(int runners) {
        sync_ops::unique_lock lock{mutex};
        arrived.wait(lock, [&] { return waiting == runners; });
        open = true;
        go.notify_all();
    }

    sync_ops::mutex mutex;
    sync_ops::condition_variable arrived, go;
    int waiting = 0;
    bool open = false;
};

struct first_query_race {
    sync_ops::atomic<int> finished{0};
    sync_ops::atomic<bool> in_registration{false}, release{false};
    int finished_while_held = -1;
    std::vector<observation> after;
};

ex::task<int, counted_stop_environment> races_first_queries(gate* held, first_query_race* race) {
    std::optional<task_token> first, second;
    co_await inspect_sender{[&](const auto& env) {
        start_line line;
        const auto ask = [&](std::optional<task_token>& answer) {
            line.wait();
            answer.emplace(env.query(std::get_stop_token));
            ++race->finished;
        };
        sync_ops::thread left{[&] { ask(first); }};
        sync_ops::thread right{[&] { ask(second); }};
        line.release(2);
        (void)wait_for([&] { return race->in_registration.load(); });
        pause_briefly();
        race->finished_while_held = race->finished.load();
        race->release = true;
        left.join();
        right.join();
    }};
    const bool equivalent = *first == *second;
    co_await gate_sender{held};
    race->after.push_back(observe(*first));
    race->after.push_back(observe(*second));
    co_return equivalent ? 1 : 0;
}

TEST(ExecutionTaskStop, G206_ConcurrentFirstQueriesShareOneSourceAndOneRegistration) {
    for (int round = 0; round != 20; ++round) {
        stop_counts task_counts, upstream_counts;
        task_counts_scope scope{task_counts};
        upstream_source upstream{upstream_counts};
        first_query_race race;
        upstream_counts.during_registration = [&] {
            race.in_registration = true;
            (void)wait_for([&] { return race.release.load(); });
        };
        translation_sample at_terminal;
        gate held;
        outcome done;
        {
            auto operation = ex::connect(
                races_first_queries(&held, &race),
                done.receiver([&] { return token_env<upstream_token>{upstream.get_token()}; },
                              [&] { at_terminal = sample(task_counts, upstream_counts); }));
            ex::start(operation);
            // No query returned while the initializer was held.
            EXPECT_EQ(race.finished_while_held, 0);
            expect_sample(sample(task_counts, upstream_counts), 1, 1, 1, 1, 1);
            EXPECT_EQ(task_counts.const_token_calls, 0);
            upstream.request_stop();
            // One request reached the one source, once.
            EXPECT_EQ(task_counts.requests, 1);
            held.open();
        }
        EXPECT_EQ(done.result, 1);
        EXPECT_EQ(done.terminals, 1);
        ASSERT_EQ(race.after.size(), 2u);
        expect_seen(race.after[0], true, true);
        expect_seen(race.after[1], true, true);
        expect_sample(at_terminal, 1, 1, 1, 1, 0);
        expect_sample(sample(task_counts, upstream_counts), 1, 0, 1, 1, 0);
        EXPECT_EQ(task_counts.requests, 1);
        EXPECT_EQ(task_counts.destroyed_in_request, 0);
    }
}

// Two threads keep asking a task whose token must not be copied twice at once.
ex::task<int, source_environment<fragile_source>> reads_from_two_threads() {
    (void)co_await ex::read_env(std::get_stop_token);
    co_await inspect_sender{[](const auto& env) {
        start_line line;
        const auto ask = [&] {
            line.wait();
            for (int i = 0; i != 200; ++i) (void)env.query(std::get_stop_token);
        };
        sync_ops::thread left{ask};
        sync_ops::thread right{ask};
        line.release(2);
        left.join();
        right.join();
    }};
    co_return 1;
}

TEST(ExecutionTaskStop, G206_ConcurrentReadersOfACustomToken) {
    copy_probe probe;
    fragile = &probe;
    std::inplace_stop_source upstream;
    outcome done;
    {
        auto operation = ex::connect(
            reads_from_two_threads(),
            done.receiver([&] { return token_env<std::inplace_stop_token>{upstream.get_token()}; }));
        ex::start(operation);
    }
    fragile = nullptr;
    EXPECT_EQ(done.result, 1);
    EXPECT_GE(probe.copies, 400);
    EXPECT_EQ(probe.overlaps, 0);
}

// A child that completes with stopped from inside its own stop callback, on
// the thread that requested the stop.
template<class Receiver>
struct stopping_operation {
    using operation_state_concept = ex::operation_state_tag;
    using token_t = std::stop_token_of_t<ex::env_of_t<Receiver>>;

    struct on_stop {
        stopping_operation* self;
        void operator()() const noexcept {
            self->gate_target->open = nullptr;
            ex::set_stopped(std::move(self->receiver));
        }
    };

    stopping_operation(gate* gate_target, Receiver receiver)
        : gate_target(gate_target), receiver(std::move(receiver)) {}
    stopping_operation(stopping_operation&&) = delete;

    void start() & noexcept {
        auto token = std::get_stop_token(ex::get_env(receiver));
        if (token.stop_requested()) {
            // Completing from inside the registration below would destroy
            // this operation while it constructs its own member.
            ex::set_stopped(std::move(receiver));
            return;
        }
        gate_target->open = [this] {
            callback.reset();
            ex::set_value(std::move(receiver));
        };
        callback.emplace(std::move(token), on_stop{this});
    }

    gate* gate_target;
    Receiver receiver;
    std::optional<std::stop_callback_for_t<token_t, on_stop>> callback;
};

struct stopping_sender {
    using sender_concept = ex::sender_tag;

    template<class Self, class... Env>
    static consteval auto get_completion_signatures() noexcept {
        return ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>{};
    }

    template<ex::receiver Receiver>
    auto connect(Receiver receiver) const -> stopping_operation<Receiver> {
        return {target, std::move(receiver)};
    }

    gate* target;
};

ex::task<int, counted_stop_environment> waits_to_be_stopped(gate* held) {
    co_await stopping_sender{held};
    co_return 5;
}

enum class request_point { before_start, during_registration, after_registration, never };

struct replay {
    std::string terminal;
    std::optional<int> result;
    int terminals = 0, forwards = 0, destroyed_in_request = 0, sources_after_forward = -1;
    translation_sample at_terminal, after;
};

// The receiver either leaves the operation alone or destroys it from inside
// its terminal, which for a stop runs inside the forwarded request.
replay replay_request(request_point when, bool destroy_in_terminal) {
    stop_counts task_counts, upstream_counts;
    task_counts_scope scope{task_counts};
    upstream_source upstream{upstream_counts};
    replay seen;
    gate held;
    outcome done;
    if (when == request_point::before_start) upstream.request_stop();
    if (when == request_point::during_registration) {
        // Runs inside the upstream callback's constructor, before it registers.
        upstream_counts.during_registration = [&] { upstream.request_stop(); };
    }
    // The translated source is still there when the forwarded request
    // returns into it, whatever the receiver destroyed.
    task_counts.after_forward = [&] { seen.sources_after_forward = task_counts.live_sources.load(); };
    {
        std::function<void()> destroy;
        auto make_env = [&] { return token_env<upstream_token>{upstream.get_token()}; };
        using operation_t = ex::connect_result_t<ex::task<int, counted_stop_environment>,
                                                 probe_receiver<decltype(make_env)>>;
        std::unique_ptr<operation_t> operation{new operation_t(ex::connect(
            waits_to_be_stopped(&held), done.receiver(make_env, [&] {
                // Unregistered before the receiver is entered.
                seen.at_terminal = sample(task_counts, upstream_counts);
                if (destroy_in_terminal) destroy();
            })))};
        destroy = [&] { operation.reset(); };
        ex::start(*operation);
        if (when == request_point::after_registration) {
            EXPECT_EQ(done.terminals, 0);
            // The child stops, and the task completes, inside this call: the
            // registration is removed from its own callback's thread.
            upstream.request_stop();
        }
        if (when == request_point::never) held.open();
        EXPECT_EQ(operation == nullptr, destroy_in_terminal);
    }
    seen.terminal = done.terminal;
    seen.result = done.result;
    seen.terminals = done.terminals;
    seen.forwards = task_counts.requests;
    seen.destroyed_in_request = task_counts.destroyed_in_request;
    seen.after = sample(task_counts, upstream_counts);
    return seen;
}

TEST(ExecutionTaskStop, G206_RequestAtEachPointIsDeliveredAndUnregisteredBeforeTheTerminal) {
    for (const bool destroy_in_terminal : {false, true}) {
        for (auto when : {request_point::before_start, request_point::during_registration,
                          request_point::after_registration}) {
            SCOPED_TRACE(testing::Message() << "when=" << static_cast<int>(when)
                                            << " destroy=" << destroy_in_terminal);
            const replay seen = replay_request(when, destroy_in_terminal);
            EXPECT_EQ(seen.terminal, "stopped");
            EXPECT_FALSE(seen.result);
            EXPECT_EQ(seen.terminals, 1);
            EXPECT_EQ(seen.forwards, 1);
            // One source, one token, one registration, removed before the
            // receiver was entered. The source was still alive there.
            expect_sample(seen.at_terminal, 1, 1, 1, 1, 0);
            expect_sample(seen.after, 1, 0, 1, 1, 0);
            EXPECT_EQ(seen.sources_after_forward, 1);
            EXPECT_EQ(seen.destroyed_in_request, 0);
        }
        {
            SCOPED_TRACE(testing::Message() << "never, destroy=" << destroy_in_terminal);
            const replay seen = replay_request(request_point::never, destroy_in_terminal);
            EXPECT_TRUE(seen.terminal.empty());
            EXPECT_EQ(seen.result, 5);
            EXPECT_EQ(seen.terminals, 1);
            EXPECT_EQ(seen.forwards, 0);
            expect_sample(seen.at_terminal, 1, 1, 1, 1, 0);
            expect_sample(seen.after, 1, 0, 1, 1, 0);
        }
    }
}

ex::task<int, counted_stop_environment> asks_then_waits(gate* held) {
    (void)co_await ex::read_env(std::get_stop_token);
    // This child does not react to a stop request.
    co_await gate_sender{held};
    co_return 5;
}

// The upstream request is being forwarded on another thread while the task
// completes on this one. The forward does not return until the terminal has
// entered the removal of the upstream registration, and the removal waits
// for the forward. The receiver is entered only after both.
TEST(ExecutionTaskStop, G206_TerminalWaitsForAForwardRunningOnAnotherThread) {
    for (int round = 0; round != 10; ++round) {
        stop_counts task_counts, upstream_counts;
        task_counts_scope scope{task_counts};
        upstream_source upstream{upstream_counts};
        sync_ops::atomic<bool> in_forward{false}, removal_entered{false}, forward_returning{false};
        bool removal_was_reached = false, removal_overlapped_forward = false;
        // On the requesting thread, inside the forwarded request.
        task_counts.after_forward = [&] {
            in_forward = true;
            removal_was_reached = wait_for([&] { return removal_entered.load(); });
            forward_returning = true;
        };
        // On the completing thread, at the entry of the removal. A serial
        // run, where the forward has returned by now, fails here.
        upstream_counts.entering_removal = [&] {
            removal_overlapped_forward = in_forward.load() && !forward_returning.load();
            removal_entered = true;
        };
        bool forward_returned_before_terminal = false;
        translation_sample at_terminal;
        gate held;
        outcome done;
        sync_ops::thread requester;
        {
            auto operation = ex::connect(
                asks_then_waits(&held),
                done.receiver([&] { return token_env<upstream_token>{upstream.get_token()}; }, [&] {
                    forward_returned_before_terminal = forward_returning.load();
                    at_terminal = sample(task_counts, upstream_counts);
                }));
            ex::start(operation);
            requester = sync_ops::thread{[&] { upstream.request_stop(); }};
            ASSERT_TRUE(wait_for([&] { return in_forward.load(); }));
            held.open();
        }
        requester.join();
        EXPECT_TRUE(removal_was_reached);
        EXPECT_TRUE(removal_overlapped_forward);
        EXPECT_TRUE(forward_returned_before_terminal);
        expect_sample(at_terminal, 1, 1, 1, 1, 0);
        EXPECT_EQ(done.result, 5);
        EXPECT_EQ(done.terminals, 1);
        EXPECT_EQ(task_counts.requests, 1);
        EXPECT_EQ(task_counts.destroyed_in_request, 0);
        EXPECT_EQ(task_counts.live_sources, 0);
    }
}

// The upstream source runs its callbacks one at a time. While another
// callback of the same upstream is running, the task's forwarder is still
// only pending. The task may finish then: the terminal removes the pending
// forwarder, which is never invoked, and the receiver is entered with no
// forward although the upstream request is already latched.
TEST(ExecutionTaskStop, G206_TerminalRemovesAForwardThatIsStillPending) {
    stop_counts task_counts, upstream_counts;
    task_counts_scope scope{task_counts};
    upstream_source upstream{upstream_counts};
    sync_ops::atomic<bool> parked{false}, release{false}, park_returned{false}, park_timed_out{false};
    const auto park = [&] {
        if (task_counts.requests.load() != 0 || parked.exchange(true)) return;
        if (!wait_for([&] { return release.load(); })) park_timed_out = true;
        park_returned = true;
    };
    // The backport's in-place source invokes its callbacks from one end of
    // its list, so one of these two, registered on either side of the task's
    // forwarder, runs before it. This fixture is scoped to that list order.
    // A source that selected the forwarder first would be a different
    // schedule, which the selected-forward case above covers.
    std::inplace_stop_callback before{upstream.inner.get_token(), park};
    int forwards_at_terminal = -1;
    bool requested_at_terminal = false, park_active_at_terminal = false;
    translation_sample at_terminal;
    gate held;
    outcome done;
    sync_ops::thread requester;
    bool parked_in_time = false;
    {
        auto operation = ex::connect(
            asks_then_waits(&held),
            done.receiver([&] { return token_env<upstream_token>{upstream.get_token()}; }, [&] {
                forwards_at_terminal = task_counts.requests.load();
                requested_at_terminal = upstream.stop_requested();
                // The unrelated callback is still running: it was entered
                // and has neither returned nor given up waiting.
                park_active_at_terminal = parked.load() && !park_returned.load() && !park_timed_out.load();
                at_terminal = sample(task_counts, upstream_counts);
            }));
        ex::start(operation);
        std::inplace_stop_callback after{upstream.inner.get_token(), park};
        requester = sync_ops::thread{[&] { upstream.request_stop(); }};
        parked_in_time = wait_for([&] { return parked.load(); });
        if (parked_in_time) {
            // The request is latched and the forwarder has not run.
            EXPECT_EQ(task_counts.requests, 0);
            held.open();
            EXPECT_EQ(done.terminals, 1);
        }
        // Released on every path, so the requester can be joined.
        release = true;
        requester.join();
        if (!parked_in_time) held.open();
    }
    ASSERT_TRUE(parked_in_time);
    EXPECT_FALSE(park_timed_out.load());
    EXPECT_TRUE(park_active_at_terminal);
    EXPECT_TRUE(requested_at_terminal);
    EXPECT_EQ(forwards_at_terminal, 0);
    expect_sample(at_terminal, 1, 1, 1, 1, 0);
    EXPECT_EQ(done.result, 5);
    // The removed forwarder was never invoked.
    EXPECT_EQ(task_counts.requests, 0);
    EXPECT_EQ(task_counts.destroyed_in_request, 0);
}

// A child for two threads: a stop request on one, a value on the other.
// Exactly one of them completes it.
struct contest {
    sync_ops::atomic<bool> decided{false};
    std::function<void()> complete_value;
};

template<class Receiver>
struct contested_operation {
    using operation_state_concept = ex::operation_state_tag;
    using token_t = std::stop_token_of_t<ex::env_of_t<Receiver>>;

    struct on_stop {
        contested_operation* self;
        void operator()() const noexcept {
            if (!self->shared->decided.exchange(true)) ex::set_stopped(std::move(self->receiver));
        }
    };

    contested_operation(contest* shared, Receiver receiver) : shared(shared), receiver(std::move(receiver)) {}
    contested_operation(contested_operation&&) = delete;

    void start() & noexcept {
        // The stop callback may destroy this operation: decide through the
        // contest first, and touch the operation only as the winner.
        shared->complete_value = [this, contest = shared] {
            if (!contest->decided.exchange(true)) {
                // Waits for a stop callback that lost and is still running.
                callback.reset();
                ex::set_value(std::move(receiver));
            }
        };
        callback.emplace(std::get_stop_token(ex::get_env(receiver)), on_stop{this});
    }

    contest* shared;
    Receiver receiver;
    std::optional<std::stop_callback_for_t<token_t, on_stop>> callback;
};

struct contested_sender {
    using sender_concept = ex::sender_tag;

    template<class Self, class... Env>
    static consteval auto get_completion_signatures() noexcept {
        return ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>{};
    }

    template<ex::receiver Receiver>
    auto connect(Receiver receiver) const -> contested_operation<Receiver> {
        return {shared, std::move(receiver)};
    }

    contest* shared;
};

ex::task<int, counted_stop_environment> waits_in_contest(contest* shared) {
    co_await contested_sender{shared};
    co_return 5;
}

// The same two paths left to race freely, for the sanitizer lanes. Whichever
// wins, there is one terminal, and the registration is gone when it runs.
TEST(ExecutionTaskStop, G206_RequestRacingTerminalCleanup) {
    for (int round = 0; round != 300; ++round) {
        stop_counts task_counts, upstream_counts;
        task_counts_scope scope{task_counts};
        upstream_source upstream{upstream_counts};
        contest shared;
        outcome done;
        translation_sample at_terminal;
        start_line line;
        sync_ops::thread requester;
        {
            auto operation = ex::connect(
                waits_in_contest(&shared),
                done.receiver([&] { return token_env<upstream_token>{upstream.get_token()}; },
                              [&] { at_terminal = sample(task_counts, upstream_counts); }));
            ex::start(operation);
            requester = sync_ops::thread{[&] {
                line.wait();
                upstream.request_stop();
            }};
            line.release(1);
            shared.complete_value();
            requester.join();
        }
        EXPECT_EQ(done.terminals, 1);
        if (done.terminal == "stopped") {
            EXPECT_FALSE(done.result);
            EXPECT_EQ(task_counts.requests, 1);
        } else {
            EXPECT_TRUE(done.terminal.empty());
            EXPECT_EQ(done.result, 5);
        }
        expect_sample(at_terminal, 1, 1, 1, 1, 0);
        expect_sample(sample(task_counts, upstream_counts), 1, 0, 1, 1, 0);
        EXPECT_EQ(task_counts.destroyed_in_request, 0);
    }
}

} // namespace

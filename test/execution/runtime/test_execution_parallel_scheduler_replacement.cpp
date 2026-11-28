#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <exception>
#include <execution>
#include <latch>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <type_traits>
#include <utility>

#include "adversarial_schedulers.hpp"
#include "parallel_scheduler_replacement_fixture.hpp"

namespace {
namespace ex = std::execution;
namespace spi = std::execution::parallel_scheduler_replacement;
namespace fixture = test_execution::replacement_fixture;
using spi::bulk_item_receiver_proxy;
using spi::parallel_scheduler_backend;
using spi::receiver_proxy;
using bytes = std::span<std::byte>;
using std::inplace_stop_token;

static_assert(std::is_abstract_v<receiver_proxy> && std::is_abstract_v<bulk_item_receiver_proxy>);
static_assert(std::derived_from<bulk_item_receiver_proxy, receiver_proxy>);
static_assert(std::is_abstract_v<parallel_scheduler_backend>);
static_assert(!std::has_virtual_destructor_v<receiver_proxy>);
static_assert(std::has_virtual_destructor_v<parallel_scheduler_backend>);
static_assert(std::same_as<decltype(&receiver_proxy::set_value), void (receiver_proxy::*)() noexcept>);
static_assert(std::same_as<decltype(&receiver_proxy::set_error),
                           void (receiver_proxy::*)(std::exception_ptr) noexcept>);
static_assert(std::same_as<decltype(&receiver_proxy::set_stopped), void (receiver_proxy::*)() noexcept>);
static_assert(std::same_as<decltype(&bulk_item_receiver_proxy::execute),
                           void (bulk_item_receiver_proxy::*)(std::size_t, std::size_t) noexcept>);
static_assert(std::same_as<decltype(&parallel_scheduler_backend::schedule),
                           void (parallel_scheduler_backend::*)(receiver_proxy&, bytes) noexcept>);
static_assert(std::same_as<decltype(&parallel_scheduler_backend::schedule_bulk_chunked),
                           void (parallel_scheduler_backend::*)(std::size_t, bulk_item_receiver_proxy&, bytes) noexcept>);
static_assert(std::same_as<decltype(&parallel_scheduler_backend::schedule_bulk_unchunked),
                           decltype(&parallel_scheduler_backend::schedule_bulk_chunked)>);
static_assert(std::same_as<decltype(std::declval<const receiver_proxy&>().try_query<inplace_stop_token>(
                               std::get_stop_token)), std::optional<inplace_stop_token>>);
static_assert(noexcept(std::declval<const receiver_proxy&>().try_query<inplace_stop_token>(std::get_stop_token)));

enum class channel { none, value, error, stopped };
enum class route { ordinary, chunked, unchunked };
struct recognizable_error { int code; };

struct record {
    channel terminal = channel::none;
    unsigned terminals = 0;
    unsigned backend_destructions = 0;
    unsigned execute_entries = 0, execute_exits = 0;
    unsigned observed_exits = 0;
    int error_code = 0;
    int datum = 0, observed_datum = 0;
    std::array<int, 3> visited{};
    std::array<std::pair<std::size_t, std::size_t>, 3> intervals{};
    bool scratch_read = false, placed = false, placed_destroyed = false;
    void* owner = nullptr;
    void (*destroy)(void*) noexcept = nullptr;

    void complete(channel ch) noexcept {
        terminal = ch;
        ++terminals;
        observed_exits = execute_exits;
        observed_datum = datum;
        if (destroy) destroy(owner);
    }
};

template<class Token = std::never_stop_token, bool ByValueProp = false>
struct receiver {
    using receiver_concept = ex::receiver_tag;
    record* result;
    Token token;

    // Calls of the environment's stop token query, per receiver type.
    static inline std::execution::__forge_sync::atomic<unsigned> stop_queries{0};

    struct environment {
        Token token;
        Token query(std::get_stop_token_t) const noexcept {
            ++stop_queries;
            return token;
        }
    };
    auto get_env() const noexcept {
        if constexpr (ByValueProp) return ex::prop{std::get_stop_token, token};
        else return environment{token};
    }

    void set_value() && noexcept { result->complete(channel::value); }
    void set_stopped() && noexcept { result->complete(channel::stopped); }
    void set_error(std::exception_ptr error) && noexcept {
        try {
            std::rethrow_exception(error);
        } catch (const recognizable_error& e) {
            result->error_code = e.code;
        } catch (...) {
            result->error_code = -1;
        }
        result->complete(channel::error);
    }
};

struct no_stop_receiver {
    void set_value() && noexcept {}
    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}
};

struct item {
    record* result;
    void operator()(std::size_t begin, std::size_t end) noexcept {
        EXPECT_EQ(result->terminal, channel::none);
        auto i = result->execute_entries++;
        if (i < result->intervals.size()) result->intervals[i] = {begin, end};
        EXPECT_LT(begin, end);
        EXPECT_LE(end, result->visited.size());
        for (; begin < end && begin < result->visited.size(); ++begin) {
            ++result->visited[begin];
            result->datum += 7 + static_cast<int>(begin);
        }
        ++result->execute_exits;
    }
};

struct alignas(std::max_align_t) payload {
    const int* datum;
    bool* destroyed;
    ~payload() { *destroyed = true; }
};

struct job {
    receiver_proxy* proxy;
    bulk_item_receiver_proxy* bulk;
    bytes scratch;
    record* trace;
    std::exception_ptr error;
    channel terminal;
    route entry;
    std::size_t shape;
    const int* datum;
    payload* placed = nullptr;

    void run() noexcept {
        EXPECT_FALSE(scratch.empty());
        if (!scratch.empty()) trace->scratch_read = scratch.front() == std::byte{0x53};
        // Both the placed and short-span paths retain the caller's datum
        // by reference and read it at delivery, not when queued.
        trace->datum = *(placed ? placed->datum : datum);
        if (placed) std::destroy_at(placed);

        if (bulk && shape != 0) {
            const auto count = terminal == channel::value ? shape : shape - 1;
            if (entry == route::chunked && count != 0) {
                const auto middle = count < 2 ? count : 2;
                bulk->execute(0, middle);
                if (middle != count) bulk->execute(middle, count);
            } else {
                for (std::size_t i = 0; i != count; ++i) bulk->execute(i, i + 1);
            }
        }

        // All reads of the placed object, byte span and bulk proxy have ended.
        // The terminal can destroy all three and the backend itself.
        switch (terminal) {
            case channel::value: proxy->set_value(); return;
            case channel::error: proxy->set_error(std::move(error)); return;
            case channel::stopped: proxy->set_stopped(); return;
            default: std::terminate();
        }
    }
};

struct queue {
    std::optional<job> pending;
    void drain() noexcept {
        auto current = std::move(*pending);
        pending.reset();
        current.run();
    }
};

struct recording_backend final : parallel_scheduler_backend {
    record* trace;
    const int* datum;
    queue* deferred;
    channel terminal;
    std::exception_ptr error;

    recording_backend(record& state, const int& input, queue* q, channel ch)
        : trace(&state), datum(&input), deferred(q), terminal(ch),
          error(std::make_exception_ptr(recognizable_error{73})) {}
    ~recording_backend() override { ++trace->backend_destructions; }

    void schedule(receiver_proxy& proxy, bytes scratch) noexcept override {
        launch({&proxy, nullptr, scratch, trace, error, terminal, route::ordinary, 0, datum});
    }
    void schedule_bulk_chunked(std::size_t n, bulk_item_receiver_proxy& proxy, bytes scratch) noexcept override {
        launch({&proxy, &proxy, scratch, trace, error, terminal, route::chunked, n, datum});
    }
    void schedule_bulk_unchunked(std::size_t n, bulk_item_receiver_proxy& proxy, bytes scratch) noexcept override {
        launch({&proxy, &proxy, scratch, trace, error, terminal, route::unchunked, n, datum});
    }

    void launch(job work) noexcept {
        if (!work.scratch.empty()) work.scratch.front() = std::byte{0x53};
        if (work.scratch.size() > 1) {
            auto tail = work.scratch.subspan(1);
            void* first = tail.data();
            auto size = tail.size();
            if (auto* aligned = std::align(alignof(payload), sizeof(payload), first, size)) {
                work.placed = std::construct_at(static_cast<payload*>(aligned), work.datum, &trace->placed_destroyed);
                trace->placed = true;
            }
        }

        if (deferred) {
            deferred->pending.emplace(std::move(work));
        } else {
            work.run();
        }
    }
};

template<class Op>
void delete_at_entry(record& state, std::unique_ptr<Op>& op) {
    state.owner = &op;
    state.destroy = [](void* owner) noexcept { static_cast<std::unique_ptr<Op>*>(owner)->reset(); };
}

TEST(ParallelSchedulerReplacement, C01_PublicSpiUsesLocalBackend) {
    record trace;
    const int datum = 0;
    recording_backend concrete(trace, datum, nullptr, channel::value);
    parallel_scheduler_backend& backend = concrete;
    std::array<std::byte, 1> scratch{};
    fixture::ordinary_proxy<receiver<>> regular{receiver<>{&trace, {}}};
    receiver_proxy& regular_base = regular;
    backend.schedule(regular_base, scratch);
    EXPECT_EQ(trace.terminals, 1u);

    record bulk_trace;
    fixture::bulk_proxy<receiver<>, item> bulk{receiver<>{&bulk_trace, {}}, item{&bulk_trace}};
    backend.schedule_bulk_chunked(0, static_cast<bulk_item_receiver_proxy&>(bulk), scratch);
    EXPECT_EQ(bulk_trace.terminals, 1u);
    record unit_trace;
    fixture::bulk_proxy<receiver<>, item> units{receiver<>{&unit_trace, {}}, item{&unit_trace}};
    backend.schedule_bulk_unchunked(0, static_cast<bulk_item_receiver_proxy&>(units), scratch);
    EXPECT_EQ(unit_trace.terminals, 1u);
}

auto query(const receiver_proxy& proxy) noexcept {
    return proxy.try_query<inplace_stop_token>(std::get_stop_token);
}

TEST(ParallelSchedulerReplacement, C02_ClosedQuerySetAndDisengagedExact) {
    fixture::ordinary_proxy<receiver<inplace_stop_token>> exact{receiver<inplace_stop_token>{nullptr, {}}};
    const receiver_proxy& base = exact;
    auto token = query(base);
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(*token, inplace_stop_token{});
    EXPECT_FALSE(base.try_query<int>(std::get_stop_token).has_value());
    EXPECT_FALSE(base.try_query<inplace_stop_token>(std::get_allocator).has_value());
    // The real stop CPO cannot be non-invocable: exercise the private query
    // dispatch with an invalid expression, through the public nullopt path.
    fixture::invalid_call_proxy invalid;
    EXPECT_FALSE(query(invalid).has_value());
}

struct stop_trace {
    std::atomic<unsigned> registrations{0};
    std::atomic<unsigned> removals{0};
    std::latch* entering_registration = nullptr;
    std::latch* finish_registration = nullptr;
    std::latch* entering_removal = nullptr;
};

struct upstream_token {
    inplace_stop_token actual;
    stop_trace* trace;
    upstream_token() = delete;
    upstream_token(inplace_stop_token token, stop_trace& trace) noexcept : actual(token), trace(&trace) {}
    bool operator==(const upstream_token&) const noexcept = default;
    bool stop_possible() const noexcept { return actual.stop_possible(); }
    bool stop_requested() const noexcept { return actual.stop_requested(); }

    template<class Fn>
    struct callback_type {
        stop_trace* trace;
        std::optional<std::inplace_stop_callback<Fn>> inner;

        callback_type(upstream_token token, Fn fn) noexcept : trace(token.trace) {
            ++trace->registrations;
            if (trace->entering_registration) trace->entering_registration->count_down();
            if (trace->finish_registration) trace->finish_registration->wait();
            inner.emplace(token.actual, std::move(fn));
        }
        ~callback_type() {
            if (trace->entering_removal) trace->entering_removal->count_down();
            inner.reset();
            ++trace->removals;
        }
    };
};
static_assert(std::stoppable_token<upstream_token>);

struct always_impossible_token {
    unsigned* registrations;
    constexpr bool stop_possible() const noexcept { return false; }
    constexpr bool stop_requested() const noexcept { return false; }
    bool operator==(const always_impossible_token&) const noexcept = default;

    template<class Fn>
    struct callback_type {
        callback_type(const always_impossible_token& token, Fn) noexcept { ++*token.registrations; }
    };
};
static_assert(std::stoppable_token<always_impossible_token>);

struct source_pointer_token {
    std::inplace_stop_source* source = nullptr;
    constexpr bool stop_possible() const noexcept { return source != nullptr; }
    bool stop_requested() const noexcept { return source && source->stop_requested(); }
    bool operator==(const source_pointer_token&) const noexcept = default;

    template<class Fn>
    struct callback_type {
        std::inplace_stop_callback<Fn> inner;

        callback_type(source_pointer_token token, Fn fn) noexcept
            : inner(token.source ? token.source->get_token() : inplace_stop_token{}, std::move(fn)) {}
    };
};
static_assert(std::stoppable_token<source_pointer_token>);

TEST(ParallelSchedulerReplacement, C03_ExactTranslatedAndImpossibleToken) {
    std::inplace_stop_source source;
    const auto original = source.get_token();
    fixture::ordinary_proxy<receiver<inplace_stop_token>> exact{receiver<inplace_stop_token>{nullptr, original}};
    EXPECT_EQ(query(exact), std::optional{original});
    EXPECT_EQ(query(exact), std::optional{original});

    for (bool before : {false, true}) {
        stop_trace stats;
        std::inplace_stop_source upstream;
        record done;
        fixture::ordinary_proxy<receiver<upstream_token>> proxy{
            receiver<upstream_token>{&done, {upstream.get_token(), stats}}};
        if (before) upstream.request_stop();
        auto first = query(proxy);
        ASSERT_TRUE(first);
        EXPECT_TRUE(first->stop_possible());
        EXPECT_NE(*first, upstream.get_token());
        EXPECT_EQ(first->stop_requested(), before);
        if (!before) upstream.request_stop();
        EXPECT_TRUE(first->stop_requested());
        EXPECT_EQ(query(proxy), first);
        EXPECT_EQ(stats.registrations.load(), 1u);
        proxy.set_value();
        EXPECT_EQ(stats.removals.load(), 1u);
    }

    stop_trace stats;
    fixture::ordinary_proxy<receiver<upstream_token>> impossible{
        receiver<upstream_token>{nullptr, {inplace_stop_token{}, stats}}};
    EXPECT_EQ(query(impossible), std::optional{inplace_stop_token{}});
    EXPECT_EQ(query(impossible), std::optional{inplace_stop_token{}});
    EXPECT_EQ(stats.registrations.load(), 0u);

    // A non-static constexpr observer is checked at query time: the proxy
    // classifies with the static form only, on every toolchain, so the first
    // query calls the receiver's stop token query exactly once and later
    // queries do not call it.
    unsigned registrations = 0;
    using impossible_receiver = receiver<always_impossible_token>;
    fixture::ordinary_proxy<impossible_receiver> always_impossible{
        impossible_receiver{nullptr, {&registrations}}};
    impossible_receiver::stop_queries = 0;
    EXPECT_EQ(query(always_impossible), std::optional{inplace_stop_token{}});
    EXPECT_EQ(impossible_receiver::stop_queries, 1u);
    EXPECT_EQ(query(always_impossible), std::optional{inplace_stop_token{}});
    EXPECT_EQ(impossible_receiver::stop_queries, 1u);
    EXPECT_EQ(registrations, 0u);

    fixture::ordinary_proxy<no_stop_receiver> never{no_stop_receiver{}};
    EXPECT_EQ(query(never), std::optional{inplace_stop_token{}});
    EXPECT_EQ(query(never), std::optional{inplace_stop_token{}});
}

TEST(ParallelSchedulerReplacement, C03_ByValuePropReturnsOwnedTokens) {
    using exact_receiver = receiver<inplace_stop_token, true>;
    using translated_receiver = receiver<upstream_token, true>;
    // prop::query returns a reference into the temporary. Pin the reader's
    // value return independently of whether a sanitizer detects the lifetime.
    static_assert(std::same_as<decltype(std::declval<fixture::stop_reader<exact_receiver>>()()),
                               inplace_stop_token>);
    static_assert(std::same_as<decltype(std::declval<fixture::stop_reader<translated_receiver>>()()),
                               upstream_token>);

    std::inplace_stop_source source;
    auto original = source.get_token();
    stop_trace stats;
    record done;
    fixture::ordinary_proxy<exact_receiver> exact{exact_receiver{&done, original}};
    fixture::ordinary_proxy<translated_receiver> translated{
        translated_receiver{&done, {original, stats}}};

    auto exact_answer = query(exact);
    auto translated_answer = query(translated);
    ASSERT_EQ(exact_answer, std::optional{original});
    ASSERT_TRUE(translated_answer);
    EXPECT_TRUE(translated_answer->stop_possible());
    EXPECT_NE(*translated_answer, original);
    EXPECT_FALSE(translated_answer->stop_requested());
    EXPECT_TRUE(source.request_stop());
    EXPECT_TRUE(exact_answer->stop_requested());
    EXPECT_TRUE(translated_answer->stop_requested());
    EXPECT_EQ(query(exact), exact_answer);
    EXPECT_EQ(query(translated), translated_answer);
    EXPECT_EQ(stats.registrations.load(), 1u);
    translated.set_value();
    EXPECT_EQ(stats.removals.load(), 1u);
}

TEST(ParallelSchedulerReplacement, C04_OrdinaryInlineAndQueuedSelfDestruction) {
    using op_type = fixture::operation<fixture::ordinary_proxy<receiver<>>>;
    for (bool queued : {false, true}) {
        for (auto ch : {channel::value, channel::error, channel::stopped}) {
            record trace;
            const int datum = 0;
            queue q;
            auto scheduler_copy = std::make_shared<recording_backend>(trace, datum, queued ? &q : nullptr, ch);
            const std::weak_ptr weak = scheduler_copy;
            auto op = std::make_unique<op_type>(scheduler_copy, receiver<>{&trace, {}});
            delete_at_entry(trace, op);
            if (!queued) scheduler_copy.reset();
            fixture::start(*op);
            if (queued) {
                scheduler_copy.reset();
                ASSERT_TRUE(q.pending);
                EXPECT_FALSE(weak.expired());
                EXPECT_EQ(trace.terminals, 0u);
                EXPECT_TRUE(op);
                q.drain();
            }
            EXPECT_FALSE(op);
            EXPECT_TRUE(weak.expired());
            EXPECT_EQ(trace.backend_destructions, 1u);
            EXPECT_TRUE(trace.scratch_read);
            EXPECT_TRUE(trace.placed_destroyed);
            EXPECT_EQ(trace.terminal, ch);
            EXPECT_EQ(trace.terminals, 1u);
            EXPECT_EQ(trace.error_code, ch == channel::error ? 73 : 0);
        }
    }
}

using event = fixture::stop_event;
struct log {
    std::array<event, 32> entries{};
    std::atomic<std::size_t> next{0};
    std::latch* waiting = nullptr;
    void add(event kind) noexcept {
        const auto i = next.fetch_add(1);
        if (i < entries.size()) entries[i] = kind;
        if (kind == event::waiting && waiting) waiting->count_down();
    }
    unsigned count(event kind) const noexcept {
        unsigned result = 0;
        for (std::size_t i = 0; i < next.load() && i < entries.size(); ++i) {
            if (entries[i] == kind) ++result;
        }
        return result;
    }
    std::size_t first(event kind) const noexcept {
        for (std::size_t i = 0; i < next.load() && i < entries.size(); ++i) {
            if (entries[i] == kind) return i;
        }
        return entries.size();
    }
};
struct observer {
    log* trace;
    std::latch* forwarding = nullptr;
    std::latch* resume_forwarding = nullptr;
    void operator()(event kind) const noexcept {
        trace->add(kind);
        if (kind == event::forward_request && forwarding) {
            forwarding->count_down();
            resume_forwarding->wait();
        }
    }
};
using traced_proxy = fixture::ordinary_proxy<receiver<upstream_token>, observer>;

TEST(ParallelSchedulerReplacement, C03_ConstexprObserverDependsOnSource) {
    using stop_receiver = receiver<source_pointer_token>;
    using proxy_type = fixture::ordinary_proxy<stop_receiver, observer>;

    log unbound_trace;
    proxy_type unbound{stop_receiver{nullptr, {}}, observer{&unbound_trace}};
    EXPECT_EQ(query(unbound), std::optional{inplace_stop_token{}});
    EXPECT_EQ(query(unbound), std::optional{inplace_stop_token{}});
    EXPECT_EQ(unbound_trace.count(event::register_callback), 0u);

    std::inplace_stop_source source;
    log bound_trace;
    record done;
    proxy_type bound{stop_receiver{&done, {&source}}, observer{&bound_trace}};
    auto translated = query(bound);
    ASSERT_TRUE(translated);
    ASSERT_TRUE(translated->stop_possible());
    EXPECT_NE(*translated, source.get_token());
    EXPECT_FALSE(translated->stop_requested());
    EXPECT_EQ(query(bound), translated);
    EXPECT_EQ(bound_trace.count(event::register_callback), 1u);

    unsigned deliveries = 0;
    auto on_stop = [&deliveries]() noexcept { ++deliveries; };
    std::inplace_stop_callback<decltype(on_stop)> downstream{*translated, on_stop};
    EXPECT_TRUE(source.request_stop());
    EXPECT_FALSE(source.request_stop());
    EXPECT_TRUE(translated->stop_requested());
    EXPECT_EQ(deliveries, 1u);
    EXPECT_EQ(bound_trace.count(event::forward_request), 1u);

    EXPECT_EQ(bound_trace.count(event::unregister_callback), 0u);
    bound.set_value();
    EXPECT_EQ(done.terminal, channel::value);
    EXPECT_EQ(bound_trace.count(event::unregister_callback), 1u);
}

template<class Fn>
struct driver_receiver {
    using receiver_concept = ex::receiver_tag;
    Fn fn;
    void set_value() && noexcept { fn(); }
};

template<class Fn>
void dispatch(test_execution::schedule_kind kind, Fn fn) {
    test_execution::adversarial_schedules driver({kind});
    auto operation = ex::connect(ex::schedule(driver.get_scheduler()), driver_receiver<Fn>{std::move(fn)});
    ex::start(operation);
    driver.settle();
}

TEST(ParallelSchedulerReplacement, C05_CommonStartFirstQueriesThenStop) {
    std::inplace_stop_source source;
    stop_trace stats;
    log trace;
    record done;
    const auto original = source.get_token();
    traced_proxy proxy{receiver<upstream_token>{&done, {original, stats}}, observer{&trace}};
    std::latch ready{2}, start{1};
    std::optional<inplace_stop_token> first, second;
    auto read = [&](auto& answer) {
        ready.count_down();
        start.wait();
        answer = query(proxy);
    };
    std::thread one([&] { read(first); });
    std::thread two([&] { read(second); });
    ready.wait();
    start.count_down();
    one.join();
    two.join();

    ASSERT_TRUE(first && second);
    EXPECT_EQ(first, second);
    EXPECT_NE(*first, original);
    EXPECT_TRUE(first->stop_possible());
    EXPECT_TRUE(second->stop_possible());
    EXPECT_FALSE(first->stop_requested());
    EXPECT_FALSE(second->stop_requested());
    EXPECT_EQ(stats.registrations.load(), 1u);
    EXPECT_EQ(trace.count(event::first_query), 1u);
    EXPECT_EQ(trace.count(event::read_token), 2u);
    EXPECT_EQ(trace.count(event::forward_request), 0u);
    EXPECT_TRUE(source.request_stop());
    EXPECT_TRUE(first->stop_requested());
    EXPECT_TRUE(second->stop_requested());
    EXPECT_EQ(trace.count(event::forward_request), 1u);
    proxy.set_value();
    EXPECT_EQ(stats.removals.load(), 1u);
}

TEST(ParallelSchedulerReplacement, C05_CompetingFirstQueriesAndStopDuringRegistration) {
    std::inplace_stop_source source;
    stop_trace stats;
    std::latch entering{1}, finish{1}, waiting{1};
    stats.entering_registration = &entering;
    stats.finish_registration = &finish;
    log trace;
    trace.waiting = &waiting;
    record done;
    traced_proxy proxy{receiver<upstream_token>{&done, {source.get_token(), stats}}, observer{&trace}};
    std::optional<inplace_stop_token> first, second;
    std::thread initializer([&] { first = query(proxy); });
    entering.wait();
    std::thread competitor([&] { second = query(proxy); });
    waiting.wait();
    dispatch(test_execution::schedule_kind::other_thread, [&]() noexcept { source.request_stop(); });
    finish.count_down();
    initializer.join();
    competitor.join();

    ASSERT_TRUE(first && second);
    EXPECT_EQ(first, second);
    EXPECT_TRUE(first->stop_requested());
    bool clean_at_entry = false;
    struct terminal_check { stop_trace* stats; log* trace; bool* clean; };
    terminal_check checkpoint{&stats, &trace, &clean_at_entry};
    done.owner = &checkpoint;
    done.destroy = [](void* arg) noexcept {
        auto& check = *static_cast<terminal_check*>(arg);
        *check.clean = check.stats->removals.load() == 1 &&
                       check.trace->count(event::unregister_callback) == 1;
    };
    proxy.set_value();
    EXPECT_TRUE(clean_at_entry);
    EXPECT_EQ(stats.registrations.load(), 1u);
    EXPECT_EQ(trace.count(event::first_query), 1u);
    EXPECT_EQ(trace.count(event::waiting), 1u);
    EXPECT_EQ(trace.count(event::read_token), 2u);
    EXPECT_EQ(trace.count(event::forward_request), 1u);
    EXPECT_LT(trace.first(event::register_callback), trace.first(event::waiting));
    EXPECT_LT(trace.first(event::waiting), trace.first(event::forward_request));
    EXPECT_LT(trace.first(event::forward_request), trace.first(event::publish));
    EXPECT_LT(trace.first(event::publish), trace.first(event::read_token));
    EXPECT_LT(trace.first(event::terminal_commit), trace.first(event::unregister_callback));
}

TEST(ParallelSchedulerReplacement, C05_ForeignThreadCleanupWaitsForForwarding) {
    std::inplace_stop_source source;
    stop_trace stats;
    std::latch forwarding{1}, resume{1}, removing{1};
    stats.entering_removal = &removing;
    log trace;
    record done;
    std::atomic<bool> outward{false};
    done.owner = &outward;
    done.destroy = [](void* arg) noexcept { static_cast<std::atomic<bool>*>(arg)->store(true); };
    traced_proxy proxy{receiver<upstream_token>{&done, {source.get_token(), stats}},
                       observer{&trace, &forwarding, &resume}};
    auto token = query(proxy);
    ASSERT_TRUE(token);
    std::thread requester([&] { source.request_stop(); });
    forwarding.wait();
    std::thread terminator([&] { proxy.set_value(); });
    removing.wait();
    EXPECT_FALSE(outward.load());
    resume.count_down();
    requester.join();
    terminator.join();
    EXPECT_TRUE(outward.load());
    EXPECT_TRUE(token->stop_requested());
    EXPECT_EQ(stats.removals.load(), 1u);
    EXPECT_EQ(done.terminals, 1u);
    EXPECT_LT(trace.first(event::terminal_commit), trace.first(event::unregister_callback));
}

struct stop_from_downstream {
    receiver_proxy* proxy;
    void operator()() const noexcept { proxy->set_stopped(); }
};
struct self_destroying_proxy {
    traced_proxy proxy;
    std::optional<std::inplace_stop_callback<stop_from_downstream>> child;
    self_destroying_proxy(record& done, upstream_token token, log& trace)
        : proxy(receiver<upstream_token>{&done, token}, observer{&trace}) {}
};

TEST(ParallelSchedulerReplacement, C05_SameThreadCallbackRemovalOnInlineAndWorker) {
    using test_execution::schedule_kind;
    for (auto kind : {schedule_kind::inline_immediate, schedule_kind::other_thread}) {
        std::inplace_stop_source source;
        stop_trace stats;
        log trace;
        record done;
        auto op = std::make_unique<self_destroying_proxy>(done, upstream_token{source.get_token(), stats}, trace);
        struct entry_check {
            std::unique_ptr<self_destroying_proxy>* op;
            stop_trace* stats;
            log* trace;
            bool clean = false;
        } checkpoint{&op, &stats, &trace};
        done.owner = &checkpoint;
        done.destroy = [](void* arg) noexcept {
            auto& check = *static_cast<entry_check*>(arg);
            check.clean = check.stats->removals.load() == 1 &&
                          check.trace->count(event::unregister_callback) == 1;
            check.op->reset();
        };
        auto token = query(op->proxy);
        ASSERT_TRUE(token);
        EXPECT_EQ(query(op->proxy), token);
        op->child.emplace(*token, stop_from_downstream{&op->proxy});
        dispatch(kind, [&]() noexcept {
            EXPECT_TRUE(source.request_stop());
            EXPECT_FALSE(source.request_stop());
        });
        EXPECT_TRUE(checkpoint.clean);
        EXPECT_FALSE(op);
        EXPECT_EQ(done.terminals, 1u);
        EXPECT_EQ(done.terminal, channel::stopped);
        EXPECT_EQ(stats.registrations.load(), 1u);
        EXPECT_EQ(trace.count(event::forward_request), 1u);
        EXPECT_LT(trace.first(event::forward_request), trace.first(event::terminal_commit));
        EXPECT_LT(trace.first(event::terminal_commit), trace.first(event::unregister_callback));
    }
}

template<std::size_t Size>
void run_bulk(route entry, std::size_t shape, channel ch, bool queued) {
    using proxy_type = fixture::bulk_proxy<receiver<>, item>;
    using op_type = fixture::operation<proxy_type, Size>;
    record trace;
    int datum = 31 + static_cast<int>(shape);
    queue q;
    auto scheduler_copy = std::make_shared<recording_backend>(trace, datum, queued ? &q : nullptr, ch);
    const std::weak_ptr weak = scheduler_copy;
    auto op = std::make_unique<op_type>(scheduler_copy, receiver<>{&trace, {}}, item{&trace});
    delete_at_entry(trace, op);
    if (!queued) scheduler_copy.reset();
    if (entry == route::chunked) fixture::start_chunked(*op, shape);
    else fixture::start_unchunked(*op, shape);
    if (queued) {
        scheduler_copy.reset();
        ASSERT_TRUE(q.pending);
        EXPECT_TRUE(op);
        EXPECT_FALSE(weak.expired());
        EXPECT_EQ(trace.terminals, 0u);
        datum += 97;
        q.drain();
    }
    const auto visited = shape == 0 ? 0u : ch == channel::value ? 3u : 2u;
    const auto calls = entry == route::unchunked ? visited : visited == 3 ? 2u : visited == 2 ? 1u : 0u;
    EXPECT_FALSE(op);
    EXPECT_TRUE(weak.expired());
    EXPECT_EQ(trace.backend_destructions, 1u);
    EXPECT_EQ(trace.terminal, ch);
    EXPECT_EQ(trace.terminals, 1u);
    EXPECT_EQ(trace.error_code, ch == channel::error ? 73 : 0);
    EXPECT_EQ(trace.execute_entries, calls);
    EXPECT_EQ(trace.observed_exits, calls);
    EXPECT_TRUE(trace.scratch_read);
    EXPECT_EQ(trace.placed, Size != 1);
    EXPECT_EQ(trace.placed_destroyed, Size != 1);
    int expected = datum;
    for (unsigned i = 0; i != trace.visited.size(); ++i) {
        EXPECT_EQ(trace.visited[i], i < visited ? 1 : 0);
        if (i < visited) expected += 7 + static_cast<int>(i);
    }
    EXPECT_EQ(trace.observed_datum, expected);
    for (unsigned i = 0; i != calls; ++i) {
        EXPECT_EQ(trace.intervals[i], (entry == route::unchunked
            ? std::pair<std::size_t, std::size_t>{i, i + 1}
            : i == 0 ? std::pair<std::size_t, std::size_t>{0, 2}
                     : std::pair<std::size_t, std::size_t>{2, 3}));
    }
}

TEST(ParallelSchedulerReplacement, C06_BulkIntervalsStorageAndTerminalOrder) {
    for (bool queued : {false, true}) {
        for (auto entry : {route::chunked, route::unchunked}) {
            for (auto shape : {0u, 3u}) {
                for (auto ch : {channel::value, channel::error, channel::stopped}) {
                    run_bulk<1>(entry, shape, ch, queued);
                    run_bulk<64>(entry, shape, ch, queued);
                }
            }
        }
    }
}

} // namespace

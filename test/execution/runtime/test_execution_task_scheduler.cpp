#include <execution>

#include "adversarial_schedulers.hpp"

// Sanitizer runtimes own the global allocation replacements.
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
#define FORGE_TEST_TASK_SCHEDULER_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#define FORGE_TEST_TASK_SCHEDULER_SANITIZED 1
#endif
#endif

#if !defined(FORGE_TEST_TASK_SCHEDULER_SANITIZED)
#include "global_allocation_probe.hpp"
#endif

#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

namespace {

namespace ex = std::execution;
using test_execution::adversarial_schedules;
using test_execution::schedule_kind;

using erased_sender = ex::schedule_result_t<ex::task_scheduler>;
using stoppable_env = ex::prop<std::get_stop_token_t, std::inplace_stop_token>;
using never_env = ex::prop<std::get_stop_token_t, std::never_stop_token>;
using attrs = ex::env_of_t<erased_sender>;
using domain = decltype(ex::get_completion_domain<ex::set_value_t>(std::declval<attrs>()));

static_assert(ex::scheduler<ex::task_scheduler>);
static_assert(std::same_as<ex::completion_signatures_of_t<erased_sender, ex::env<>>,
                          ex::completion_signatures<ex::set_value_t()>>);
static_assert(std::same_as<ex::completion_signatures_of_t<const erased_sender&, never_env>,
                          ex::completion_signatures<ex::set_value_t()>>);
static_assert(std::same_as<ex::completion_signatures_of_t<erased_sender, stoppable_env>,
                          ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>>);
static_assert(std::derived_from<domain, ex::default_domain>);
static_assert(!std::same_as<domain, ex::default_domain>);

template<class Tag, bool Available, class... Envs>
consteval bool completion_attributes_match() {
    constexpr bool has_scheduler = requires(ex::task_scheduler& s, Envs&&... envs) {
        ex::get_completion_scheduler<Tag>(ex::get_env(ex::schedule(s)), std::forward<Envs>(envs)...);
    };
    constexpr bool has_domain = requires(ex::task_scheduler& s, Envs&&... envs) {
        ex::get_completion_domain<Tag>(ex::get_env(ex::schedule(s)), std::forward<Envs>(envs)...);
    };
    return has_scheduler == Available && has_domain == Available;
}

static_assert(completion_attributes_match<ex::set_value_t, true>());
static_assert(completion_attributes_match<ex::set_value_t, true, ex::env<>>());
static_assert(completion_attributes_match<ex::set_value_t, true, never_env>());
static_assert(completion_attributes_match<ex::set_value_t, true, stoppable_env>());
static_assert(completion_attributes_match<ex::set_stopped_t, false>());
static_assert(completion_attributes_match<ex::set_stopped_t, false, ex::env<>>());
static_assert(completion_attributes_match<ex::set_stopped_t, false, never_env>());
static_assert(completion_attributes_match<ex::set_stopped_t, true, stoppable_env>());
static_assert(completion_attributes_match<ex::set_error_t, false>());
static_assert(completion_attributes_match<ex::set_error_t, false, ex::env<>>());
static_assert(completion_attributes_match<ex::set_error_t, false, never_env>());
static_assert(completion_attributes_match<ex::set_error_t, false, stoppable_env>());

struct allocation_log {
    struct block {
        void* pointer = nullptr;
        std::size_t count = 0, size = 0, alignment = 0;
    };
    std::array<block, 12> blocks{};
    unsigned attempts = 0, allocations = 0, releases = 0, live = 0;
    bool fail = false, wrong_release = false;

    void acquired(block entry) noexcept {
        if (allocations == blocks.size()) std::abort();
        blocks[allocations++] = entry;
        ++live;
    }

    void released(void* pointer, std::size_t count, std::size_t size, std::size_t alignment) noexcept {
        for (auto& item : blocks) {
            if (item.pointer != pointer) continue;
            wrong_release |= item.count != count || item.size != size || item.alignment != alignment;
            item.pointer = nullptr;
            ++releases;
            --live;
            return;
        }
        wrong_release = true;
    }
};

template<class T>
struct tracing_allocator {
    using value_type = T;
    allocation_log* log;

    explicit tracing_allocator(allocation_log& stats) noexcept : log(&stats) {}
    template<class U>
    tracing_allocator(const tracing_allocator<U>& other) noexcept : log(other.log) {}

    T* allocate(std::size_t count) {
        ++log->attempts;
        if (log->fail) throw std::bad_alloc{};
        auto* block = std::allocator<T>{}.allocate(count);
        log->acquired({block, count, sizeof(T), alignof(T)});
        return block;
    }

    void deallocate(T* block, std::size_t count) noexcept {
        log->released(block, count, sizeof(T), alignof(T));
        std::allocator<T>{}.deallocate(block, count);
    }

    template<class U>
    bool operator==(const tracing_allocator<U>& rhs) const noexcept { return log == rhs.log; }
};

allocation_log small_allocations;

template<class T>
struct stateless_allocator {
    using value_type = T;
    stateless_allocator() = default;
    template<class U>
    stateless_allocator(const stateless_allocator<U>&) noexcept {}

    T* allocate(std::size_t count) { return tracing_allocator<T>{small_allocations}.allocate(count); }
    void deallocate(T* block, std::size_t count) noexcept {
        tracing_allocator<T>{small_allocations}.deallocate(block, count);
    }

    template<class U>
    bool operator==(const stateless_allocator<U>&) const noexcept { return true; }
};

struct trace {
    unsigned schedulers = 0, schedules = 0, starts = 0, live_children = 0, child_destructions = 0;
    const void* scheduler_identity = nullptr;
};

template<std::size_t Bytes, bool StopFromCallback = false>
struct traced_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    trace* events;
    int resource;
    std::array<std::byte, Bytes> payload{};

    explicit traced_scheduler(trace& stats, int key = 1) noexcept : events(&stats), resource(key) {
        ++events->schedulers;
    }
    traced_scheduler(const traced_scheduler& other) noexcept
        : traced_scheduler(*other.events, other.resource) {}
    auto operator=(const traced_scheduler& other) noexcept -> traced_scheduler& {
        --events->schedulers;
        events = other.events;
        resource = other.resource;
        ++events->schedulers;
        return *this;
    }
    ~traced_scheduler() { --events->schedulers; }
    bool operator==(const traced_scheduler& other) const noexcept { return resource == other.resource; }

    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        events->scheduler_identity = this;
        return resource == 1 ? ex::forward_progress_guarantee::parallel
                             : ex::forward_progress_guarantee::weakly_parallel;
    }

    template<class R>
    struct operation {
        using operation_state_concept = ex::operation_state_tag;

        struct stop {
            R* rcvr;
            void operator()() const noexcept { ex::set_stopped(std::move(*rcvr)); }
        };
        struct no_callback {};

        trace* events;
        R rcvr;
        std::array<std::byte, Bytes> scratch{};
        [[no_unique_address]] std::conditional_t<StopFromCallback,
            std::optional<std::inplace_stop_callback<stop>>, no_callback> callback;

        operation(trace* events, R&& receiver) noexcept : events(events), rcvr(std::move(receiver)) {
            ++events->live_children;
        }
        operation(const operation&) = delete;
        ~operation() {
            --events->live_children;
            ++events->child_destructions;
        }

        void start() & noexcept {
            ++events->starts;
            const auto token = std::get_stop_token(ex::get_env(rcvr));
            if constexpr (!std::unstoppable_token<std::remove_cvref_t<decltype(token)>>) {
                if (token.stop_requested()) {
                    ex::set_stopped(std::move(rcvr));
                    return;
                }
                if constexpr (StopFromCallback) {
                    // The fixture requests stop only after this call returns.
                    callback.emplace(token, stop{&rcvr});
                    return;
                }
            }
            ex::set_value(std::move(rcvr));
        }
    };

    struct sender {
        using sender_concept = ex::sender_tag;
        trace* events;

        template<class Self, class Env>
        static auto get_completion_signatures() noexcept {
            if constexpr (std::unstoppable_token<std::stop_token_of_t<Env>>) {
                return ex::completion_signatures<ex::set_value_t()>{};
            } else {
                return ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>{};
            }
        }

        template<ex::receiver R>
        auto connect(R receiver) const -> operation<R> { return {events, std::move(receiver)}; }
    };

    sender schedule() const noexcept {
        ++events->schedules;
        return {events};
    }
};

struct rvalue_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    auto schedule() && noexcept { return ex::schedule(ex::inline_scheduler{}); }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::parallel;
    }
    bool operator==(const rvalue_scheduler&) const noexcept = default;
};

static_assert(ex::sender<decltype(ex::schedule(rvalue_scheduler{}))>);
static_assert(!ex::scheduler<rvalue_scheduler> && !ex::scheduler<rvalue_scheduler&>);
static_assert(!std::is_constructible_v<ex::task_scheduler, rvalue_scheduler>);

struct category_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    struct trace {
        unsigned lvalue_calls = 0, rvalue_calls = 0;
        const category_scheduler* progress_target = nullptr;
        const category_scheduler* dispatch_target = nullptr;
    };
    trace* events;

    struct lvalue_sender {
        using sender_concept = ex::sender_tag;

        template<class Self, class Env>
            requires std::unstoppable_token<std::stop_token_of_t<Env>>
        static consteval auto get_completion_signatures() noexcept {
            return ex::completion_signatures<ex::set_value_t()>{};
        }

        template<ex::receiver R>
            requires std::unstoppable_token<std::stop_token_of_t<ex::env_of_t<R>>>
        auto connect(R receiver) const {
            return ex::connect(ex::schedule(ex::inline_scheduler{}), std::move(receiver));
        }
    };

    lvalue_sender schedule() & noexcept {
        ++events->lvalue_calls;
        events->dispatch_target = this;
        return {};
    }

    auto schedule() && noexcept {
        ++events->rvalue_calls;
        return ex::schedule(ex::inline_scheduler{});
    }

    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        events->progress_target = this;
        return ex::forward_progress_guarantee::parallel;
    }
    bool operator==(const category_scheduler&) const noexcept = default;
};

const void* empty_scheduler_identity = nullptr;

struct empty_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    auto schedule() & noexcept {
        empty_scheduler_identity = this;
        return ex::schedule(ex::inline_scheduler{});
    }

    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const empty_scheduler&) const noexcept = default;
};

static_assert(std::is_empty_v<empty_scheduler> && std::is_trivially_copyable_v<empty_scheduler>);

struct result {
    unsigned values = 0, stops = 0;
    std::thread::id thread;
    unsigned live_children_at_entry = 0;
    unsigned live_blocks_after_delete = 0;
    unsigned receiver_destructions = 0;
    void* operation = nullptr;
    void (*destroy)(void*) noexcept = nullptr;
};

struct stop_trace {
    unsigned registrations = 0, removals = 0;
};

struct upstream_token {
    std::inplace_stop_token actual;
    stop_trace* events;

    bool operator==(const upstream_token&) const noexcept = default;
    bool stop_possible() const noexcept { return actual.stop_possible(); }
    bool stop_requested() const noexcept { return actual.stop_requested(); }

    template<class Fn>
    struct callback_type {
        stop_trace* events;
        std::optional<std::inplace_stop_callback<Fn>> inner;

        callback_type(upstream_token token, Fn fn) noexcept : events(token.events) {
            ++events->registrations;
            inner.emplace(token.actual, std::move(fn));
        }
        ~callback_type() {
            inner.reset();
            ++events->removals;
        }
    };
};

static_assert(std::stoppable_token<upstream_token>);

template<class Token = std::never_stop_token>
struct receiver {
    using receiver_concept = ex::receiver_tag;
    result* done;
    Token token;
    trace* events;
    allocation_log* allocations;
    allocation_log* ordinary_allocator;

    explicit receiver(result& output, Token token = {}, trace* stats = nullptr,
                      allocation_log* storage = nullptr, allocation_log* ordinary = nullptr)
        : done(&output), token(std::move(token)), events(stats), allocations(storage),
          ordinary_allocator(ordinary) {}

    receiver(receiver&& other) noexcept
        : done(std::exchange(other.done, nullptr)), token(std::move(other.token)), events(other.events),
          allocations(other.allocations), ordinary_allocator(other.ordinary_allocator) {}
    ~receiver() { if (done) ++done->receiver_destructions; }

    struct environment {
        Token token;
        allocation_log* allocator;

        Token query(std::get_stop_token_t) const noexcept { return token; }
        auto query(std::get_allocator_t) const noexcept {
            if (!allocator) std::abort();
            return tracing_allocator<std::byte>{*allocator};
        }
    };

    auto get_env() const noexcept { return environment{token, ordinary_allocator}; }

    void complete(bool stopped) noexcept {
        auto* output = done;
        auto* storage = allocations;
        if (stopped) ++output->stops;
        else ++output->values;
        output->thread = std::this_thread::get_id();
        if (events) output->live_children_at_entry = events->live_children;
        if (output->destroy) {
            output->destroy(output->operation);
            if (storage) output->live_blocks_after_delete = storage->live;
        }
    }

    void set_value() && noexcept { complete(false); }
    void set_stopped() && noexcept { complete(true); }
};

TEST(ExecutionTaskScheduler, E01_CopyAssignAndCopyFromRvalue) {
    adversarial_schedules queue{{schedule_kind::never_resume}};
    auto concrete = queue.get_scheduler();
    ex::task_scheduler source{concrete};
    auto copy = source;
    ex::task_scheduler from_rvalue{std::move(source)};
    ex::task_scheduler assigned{ex::inline_scheduler{}};
    assigned = source;
    EXPECT_EQ(source, concrete);
    EXPECT_EQ(copy, source);
    EXPECT_EQ(from_rvalue, source);
    EXPECT_EQ(assigned, source);
}

TEST(ExecutionTaskScheduler, E01_DispatchUsesStoredLvalue) {
    static_assert(ex::scheduler<category_scheduler> && ex::scheduler<category_scheduler&>);
    static_assert(ex::sender_in<ex::schedule_result_t<category_scheduler>, stoppable_env>);
    static_assert(ex::sender_in<ex::schedule_result_t<category_scheduler&>, ex::env<>>);
    static_assert(!ex::sender_in<ex::schedule_result_t<category_scheduler&>, stoppable_env>);

    category_scheduler::trace events;
    ex::task_scheduler scheduler{category_scheduler{&events}};
    EXPECT_EQ(ex::get_forward_progress_guarantee(scheduler), ex::forward_progress_guarantee::parallel);
    ASSERT_NE(events.progress_target, nullptr);
    result done;
    std::inplace_stop_source source;
    auto first = ex::connect(ex::schedule(scheduler), receiver<>{done});
    auto second = ex::connect(ex::schedule(scheduler), receiver<std::inplace_stop_token>{done, source.get_token()});
    EXPECT_EQ(events.lvalue_calls + events.rvalue_calls, 0u);
    ex::start(first);
    EXPECT_EQ(events.dispatch_target, events.progress_target);
    events.dispatch_target = nullptr;
    ex::start(second);
    EXPECT_EQ(events.dispatch_target, events.progress_target);
    EXPECT_EQ(done.values, 2u);
    EXPECT_EQ(done.stops, 0u);
    EXPECT_EQ(events.lvalue_calls, 2u);
    EXPECT_EQ(events.rvalue_calls, 0u);
}

TEST(ExecutionTaskScheduler, E02_E04_ConcreteEqualityProgressAndAttributes) {
    trace events;
    const traced_scheduler<16> one{events, 1}, two{events, 2};
    const traced_scheduler<32> other_type{events, 1};
    ex::task_scheduler a{one}, b{one}, c{two}, d{other_type};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
    EXPECT_EQ(a, one);
    EXPECT_EQ(one, a);
    EXPECT_NE(a, two);
    EXPECT_NE(two, a);
    EXPECT_NE(a, other_type);
    EXPECT_NE(other_type, a);
    EXPECT_EQ(ex::get_forward_progress_guarantee(a), ex::get_forward_progress_guarantee(one));
    EXPECT_EQ(ex::get_forward_progress_guarantee(c), ex::get_forward_progress_guarantee(two));

    std::inplace_stop_source source;
    stoppable_env context{std::get_stop_token, source.get_token()};
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(ex::get_env(ex::schedule(a))), a);
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(ex::get_env(ex::schedule(a)), ex::env<>{}),
              ex::get_completion_scheduler<ex::set_value_t>(a, ex::env<>{}));
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(ex::get_env(ex::schedule(a)), context), a);
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_stopped_t>(ex::get_env(ex::schedule(a)), context), a);
    static_assert(std::same_as<decltype(ex::get_completion_domain<ex::set_value_t>(a)), domain>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<ex::set_value_t>(
                                   ex::get_env(ex::schedule(a)), ex::env<>{})),
                               decltype(ex::get_completion_domain<ex::set_value_t>(a, ex::env<>{}))>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<ex::set_stopped_t>(
                                   ex::get_env(ex::schedule(a)), context)), domain>);
}

TEST(ExecutionTaskScheduler, TS07_ComparesWithAffineCompletionScheduler) {
    struct affine_start_env {
        ex::task_scheduler scheduler;

        ex::task_scheduler query(ex::get_start_scheduler_t) const noexcept { return scheduler; }
    };
    trace events;
    const ex::task_scheduler first{traced_scheduler<16>{events, 1}};
    const ex::task_scheduler second{traced_scheduler<16>{events, 2}};
    const auto completion_scheduler = [](const ex::task_scheduler& start_scheduler) {
        const affine_start_env start_env{start_scheduler};
        // In this backport affine lowers at connect. Inspect that same
        // lowering here without naming or constructing its wrapper type.
        const auto lowered = ex::__forge_affine::__lower(ex::just(), start_env);
        return ex::get_env(lowered).query(ex::get_completion_scheduler<ex::set_value_t>, start_env);
    };
    auto wrapper = completion_scheduler(first);
    const auto other = completion_scheduler(second);
    static_assert(ex::scheduler<decltype(wrapper)>);
    const ex::task_scheduler erased{wrapper};

    EXPECT_TRUE(erased == wrapper);
    EXPECT_TRUE(wrapper == erased);
    EXPECT_FALSE(erased == other);
    EXPECT_FALSE(other == erased);
    EXPECT_FALSE(first == wrapper);
    EXPECT_FALSE(wrapper == first);
}

TEST(ExecutionTaskScheduler, TS07_DerivedSchedulerErasesWithOrWithoutAnAllocator) {
    static int derived_progress_calls = 0;
    struct inherited_scheduler : ex::task_scheduler {
        using ex::task_scheduler::task_scheduler;
        // Keep the inherited scheduler queries (completion scheduler and
        // domain) visible next to the counting progress override.
        using ex::task_scheduler::query;

        auto query(ex::get_forward_progress_guarantee_t) const noexcept {
            ++derived_progress_calls;
            // The base's own member: the progress CPO needs a scheduler
            // expression, and a const eraser cannot be scheduled.
            return ex::task_scheduler::query(ex::get_forward_progress_guarantee_t{});
        }
    };
    inherited_scheduler source{ex::inline_scheduler{}};
    ex::task_scheduler sliced{static_cast<const ex::task_scheduler&>(source)};
    ex::task_scheduler erased{source};
    ex::task_scheduler erased_with_allocator{source, {}};

    derived_progress_calls = 0;
    EXPECT_EQ(ex::get_forward_progress_guarantee(sliced), ex::forward_progress_guarantee::weakly_parallel);
    EXPECT_EQ(derived_progress_calls, 0);
    EXPECT_EQ(ex::get_forward_progress_guarantee(erased), ex::forward_progress_guarantee::weakly_parallel);
    EXPECT_EQ(derived_progress_calls, 1);
    EXPECT_EQ(ex::get_forward_progress_guarantee(erased_with_allocator),
              ex::forward_progress_guarantee::weakly_parallel);
    EXPECT_EQ(derived_progress_calls, 2);
    EXPECT_TRUE(sliced == source);
    EXPECT_FALSE(erased == source);
    EXPECT_FALSE(erased_with_allocator == source);
    EXPECT_TRUE(erased == erased_with_allocator);

    // Copying or moving the derived type itself is not an erasure: the
    // inherited constructor template is excluded there ([over.match.funcs]),
    // so the copy still wraps the inline scheduler and adds no progress call.
    inherited_scheduler copy{source};
    inherited_scheduler moved{std::move(copy)};
    EXPECT_TRUE(static_cast<const ex::task_scheduler&>(moved) == ex::inline_scheduler{});
    EXPECT_TRUE(static_cast<const ex::task_scheduler&>(moved) == static_cast<const ex::task_scheduler&>(source));
    derived_progress_calls = 0;
    EXPECT_EQ(ex::get_forward_progress_guarantee(moved), ex::forward_progress_guarantee::weakly_parallel);
    EXPECT_EQ(derived_progress_calls, 1);
}

TEST(ExecutionTaskScheduler, E03_InlineBackendDoesNotAllocate) {
    // Use the const-lvalue construction path so earlier tests that erase an
    // inline_scheduler prvalue do not warm this singleton.
    const ex::inline_scheduler concrete{};
    result done;
    bool equal = false;
    auto exercise = [&] {
        ex::task_scheduler a{concrete};
        auto b = a;
        ex::task_scheduler c{concrete};
        equal = a == b && a == c;
        auto op = ex::connect(ex::schedule(b), receiver<>{done});
        ex::start(op);
    };
#if defined(FORGE_TEST_TASK_SCHEDULER_SANITIZED)
    exercise();
#else
    EXPECT_EQ(allocation_probe::measure(exercise), 0u);
#endif
    EXPECT_TRUE(equal);
    EXPECT_EQ(done.values, 1u);
}

TEST(ExecutionTaskScheduler, E03_EmptyUserSchedulersKeepIndependentIdentity) {
    small_allocations = {};
    empty_scheduler concrete;
    result done;
    {
        ex::task_scheduler a{concrete, stateless_allocator<std::byte>{}};
        EXPECT_EQ(small_allocations.allocations, 1u);
        auto b = a;
        EXPECT_EQ(small_allocations.allocations, 1u);
        ex::task_scheduler c{concrete, stateless_allocator<std::byte>{}};
        EXPECT_EQ(small_allocations.allocations, 2u);
        EXPECT_EQ(a, c);

        auto dispatch = [&](ex::task_scheduler& scheduler) {
            empty_scheduler_identity = nullptr;
            auto op = ex::connect(ex::schedule(scheduler), receiver<>{done});
            ex::start(op);
            return std::exchange(empty_scheduler_identity, nullptr);
        };
        const auto* first = dispatch(a);
        EXPECT_NE(first, nullptr);
        EXPECT_NE(first, static_cast<const void*>(&concrete));
        EXPECT_EQ(dispatch(b), first);
        const auto* second = dispatch(c);
        EXPECT_NE(second, nullptr);
        EXPECT_NE(second, static_cast<const void*>(&concrete));
        EXPECT_NE(second, first);
        EXPECT_EQ(small_allocations.attempts, 2u);
        EXPECT_EQ(small_allocations.live, 2u);
    }
    EXPECT_EQ(done.values, 3u);
    EXPECT_EQ(small_allocations.releases, 2u);
    EXPECT_EQ(small_allocations.live, 0u);
    EXPECT_FALSE(small_allocations.wrong_release);
}

TEST(ExecutionTaskScheduler, E03_E05_LargeBackendAndOverflowUseTheRetainedAllocator) {
    trace events;
    allocation_log retained, other;
    result done;
    {
        traced_scheduler<512> concrete{events};
        static_assert(sizeof(concrete) > 64);
        ex::task_scheduler a{concrete, tracing_allocator<std::byte>{retained}};
        auto a_copy = a;
        ex::get_forward_progress_guarantee(a);
        const auto* shared_identity = events.scheduler_identity;
        ex::get_forward_progress_guarantee(a_copy);
        EXPECT_EQ(events.scheduler_identity, shared_identity);
        ex::task_scheduler distinct{concrete, tracing_allocator<int>{other}};
        EXPECT_EQ(a, distinct);
        ex::get_forward_progress_guarantee(distinct);
        EXPECT_NE(events.scheduler_identity, shared_identity);
        EXPECT_EQ(retained.allocations, 1u);
        EXPECT_EQ(other.allocations, 1u);
        EXPECT_EQ(events.schedulers, 3u);

        auto op = ex::connect(ex::schedule(a_copy), receiver<>{done, {}, &events, &retained, &other});
        EXPECT_EQ(retained.allocations, 1u);
        EXPECT_EQ(events.schedules, 0u);
        ex::start(op);
        EXPECT_EQ(events.schedules, 1u);
        EXPECT_EQ(events.starts, 1u);
        EXPECT_EQ(events.child_destructions, 1u);
        EXPECT_EQ(done.values, 1u);
        EXPECT_EQ(done.stops, 0u);
        EXPECT_EQ(done.live_children_at_entry, 0u);
        EXPECT_EQ(retained.allocations, 2u);
        EXPECT_EQ(retained.releases, 1u);
        EXPECT_EQ(other.allocations, 1u);
    }
    EXPECT_EQ(done.receiver_destructions, 1u);
    EXPECT_EQ(events.schedulers, 0u);
    EXPECT_EQ(retained.live, 0u);
    EXPECT_EQ(other.live, 0u);
    EXPECT_EQ(retained.releases, 2u);
    EXPECT_EQ(other.releases, 1u);
    EXPECT_FALSE(retained.wrong_release);
    EXPECT_FALSE(other.wrong_release);
}

TEST(ExecutionTaskScheduler, E03_ThrowingBackendAllocationDoesNotLeaveAnOwner) {
    trace events;
    traced_scheduler<512> concrete{events};
    allocation_log log;
    log.fail = true;
    EXPECT_THROW((ex::task_scheduler{concrete, tracing_allocator<std::byte>{log}}), std::bad_alloc);
    EXPECT_EQ(log.attempts, 1u);
    EXPECT_EQ(log.live, 0u);
    EXPECT_EQ(events.schedulers, 1u);
    EXPECT_EQ(events.schedules, 0u);
}

TEST(ExecutionTaskScheduler, E05_InlineAndWorkerDispatchOnceWithoutOutsideCopies) {
    auto caller = std::this_thread::get_id();
    for (auto kind : {schedule_kind::inline_immediate, schedule_kind::other_thread}) {
        adversarial_schedules driver{{kind}};
        result done;
        allocation_log alloc;
        {
            auto op = [&] {
                ex::task_scheduler scheduler{driver.get_scheduler(), tracing_allocator<int>{alloc}};
                auto sender = ex::schedule(scheduler);
                return ex::connect(sender, receiver<>{done});
            }();
            EXPECT_EQ(driver.steps(), 0u);
            EXPECT_EQ(alloc.live, 1u);
            ex::start(op);
            driver.settle();
            EXPECT_EQ(driver.steps(), 1u);
            EXPECT_EQ(done.values, 1u);
            EXPECT_EQ(done.stops, 0u);
            EXPECT_EQ(done.receiver_destructions, 0u);
            EXPECT_EQ(alloc.attempts, 1u);
            if (kind == schedule_kind::other_thread) {
                EXPECT_NE(done.thread, caller);
                EXPECT_NE(done.thread, std::thread::id{});
            } else {
                EXPECT_EQ(done.thread, caller);
            }
        }
        EXPECT_EQ(done.receiver_destructions, 1u);
        EXPECT_EQ(alloc.live, 0u);
        EXPECT_FALSE(alloc.wrong_release);
    }
}

TEST(ExecutionTaskScheduler, E05_QueuedStopWithExactTokenNeedsNoTranslationCell) {
    adversarial_schedules driver{{schedule_kind::never_resume}};
    std::inplace_stop_source source;
    result done;
    allocation_log alloc;
    {
        ex::task_scheduler scheduler{driver.get_scheduler(), tracing_allocator<int>{alloc}};
        auto op = ex::connect(ex::schedule(scheduler),
                             receiver<std::inplace_stop_token>{done, source.get_token()});
        ex::start(op);
        EXPECT_EQ(done.values + done.stops, 0u);
        EXPECT_TRUE(source.request_stop());
        driver.settle();
        EXPECT_EQ(driver.steps(), 1u);
        EXPECT_EQ(done.values, 0u);
        EXPECT_EQ(done.stops, 1u);
        EXPECT_EQ(alloc.attempts, 1u);
    }
    EXPECT_EQ(alloc.live, 0u);
    EXPECT_FALSE(alloc.wrong_release);
}

TEST(ExecutionTaskScheduler, E05_InlineReceiverCanDeleteTheOperation) {
    trace events;
    result done;
    allocation_log retained;
    using operation = ex::connect_result_t<erased_sender, receiver<>>;
    auto* op = [&] {
        ex::task_scheduler scheduler{traced_scheduler<1>{events}, tracing_allocator<int>{retained}};
        return new operation(ex::connect(ex::schedule(scheduler), receiver<>{done, {}, &events, &retained}));
    }();
    done.operation = op;
    done.destroy = [](void* ptr) noexcept { delete static_cast<operation*>(ptr); };
    ex::start(*op);

    EXPECT_EQ(done.values, 1u);
    EXPECT_EQ(done.live_children_at_entry, 0u);
    EXPECT_EQ(done.live_blocks_after_delete, 1u); // The start invocation still owns the backend.
    EXPECT_EQ(done.receiver_destructions, 1u);
    EXPECT_EQ(events.child_destructions, 1u);
    EXPECT_EQ(retained.allocations, 1u);
    EXPECT_EQ(retained.live, 0u);
    EXPECT_FALSE(retained.wrong_release);
}

TEST(ExecutionTaskScheduler, E05_TranslatedStopPinsItsCellAcrossReceiverDestruction) {
    std::inplace_stop_source source;
    stop_trace stop_events;
    trace events;
    result done;
    allocation_log retained, ordinary;
    using operation = ex::connect_result_t<erased_sender, receiver<upstream_token>>;
    auto* op = [&] {
        ex::task_scheduler scheduler{traced_scheduler<512, true>{events},
                                     tracing_allocator<int>{retained}};
        return new operation(ex::connect(ex::schedule(scheduler),
            receiver<upstream_token>{done, {source.get_token(), &stop_events}, &events, &retained, &ordinary}));
    }();
    done.operation = op;
    done.destroy = [](void* ptr) noexcept { delete static_cast<operation*>(ptr); };

    EXPECT_EQ(retained.allocations, 2u); // Backend and cell, not child storage.
    EXPECT_EQ(ordinary.attempts, 0u);
    EXPECT_EQ(stop_events.registrations, 0u);
    ex::start(*op);
    EXPECT_EQ(events.schedules, 1u);
    EXPECT_EQ(events.starts, 1u);
    EXPECT_EQ(retained.allocations, 3u); // Includes the child spill.
    EXPECT_EQ(stop_events.registrations, 1u);
    EXPECT_EQ(done.values + done.stops, 0u);
    EXPECT_TRUE(source.request_stop());

    EXPECT_EQ(done.stops, 1u);
    EXPECT_EQ(done.values, 0u);
    EXPECT_EQ(done.live_children_at_entry, 0u);
    EXPECT_EQ(done.live_blocks_after_delete, 1u); // The forwarding invocation still owns the cell.
    EXPECT_EQ(done.receiver_destructions, 1u);
    EXPECT_EQ(stop_events.removals, 1u);
    EXPECT_EQ(events.schedulers, 0u);
    EXPECT_EQ(events.child_destructions, 1u);
    EXPECT_EQ(retained.live, 0u);
    EXPECT_EQ(retained.releases, 3u);
    EXPECT_EQ(ordinary.attempts, 0u);
    EXPECT_FALSE(retained.wrong_release);
}

TEST(ExecutionTaskScheduler, E05_PreRequestedTranslatedStopAndCellAllocationFailure) {
    std::inplace_stop_source source;
    stop_trace stop_events;
    trace events;
    allocation_log retained;
    result done;
    ex::task_scheduler scheduler{traced_scheduler<1>{events}, tracing_allocator<int>{retained}};
    auto sender = ex::schedule(scheduler);
    auto make_receiver = [&] { return receiver<upstream_token>{done, {source.get_token(), &stop_events}}; };
    retained.fail = true;
    EXPECT_THROW(ex::connect(sender, make_receiver()), std::bad_alloc);
    EXPECT_EQ(retained.live, 1u);
    EXPECT_EQ(stop_events.registrations, 0u);
    EXPECT_EQ(events.schedules, 0u);
    EXPECT_EQ(done.values + done.stops, 0u);
    retained.fail = false;
    {
        auto op = ex::connect(sender, make_receiver());
        EXPECT_TRUE(source.request_stop());
        ex::start(op);
        EXPECT_EQ(done.stops, 1u);
        EXPECT_EQ(done.values, 0u);
        EXPECT_EQ(stop_events.registrations, 1u);
        EXPECT_EQ(stop_events.removals, 1u);
        EXPECT_EQ(retained.allocations, 2u);
    }
    EXPECT_EQ(retained.live, 1u);
    EXPECT_FALSE(retained.wrong_release);
}

#if GTEST_HAS_DEATH_TEST
allocation_log* death_allocations = nullptr;

struct exit_on_completion {
    using receiver_concept = ex::receiver_tag;
    void set_value() && noexcept { std::_Exit(91); }
    void set_stopped() && noexcept { std::_Exit(92); }
    void set_error(std::exception_ptr) && noexcept { std::_Exit(93); }
};

[[noreturn]] void fail_overflow() {
    allocation_log stats;
    trace events;
    ex::task_scheduler scheduler{traced_scheduler<512>{events}, tracing_allocator<int>{stats}};
    auto op = ex::connect(ex::schedule(scheduler), exit_on_completion{});
    death_allocations = &stats;
    std::set_terminate([] {
        const auto& log = *death_allocations;
        std::_Exit(log.attempts == 2 && log.allocations == 1 && log.live == 1 ? 86 : 90);
    });
    stats.fail = true;
    ex::start(op);
    std::_Exit(94);
}

TEST(ExecutionTaskSchedulerDeathTest, E05_OverflowFailureTerminatesWithoutReceiverEntry) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_EXIT(fail_overflow(), ::testing::ExitedWithCode(86), "");
}
#endif

} // namespace

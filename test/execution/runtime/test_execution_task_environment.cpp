// The environments of std::execution::task, without the stop token:
// [task.state]/1, /2, /4 and /6, [task.members]/5 and [task.promise]/12.
// Test names carry the pilot acceptance case they cover (G2-01 to G2-04).
// The stop token ([task.state]/7) is in test_execution_task_stop.cpp.

#include <execution>

#include "test_execution_task_environment_support.hpp"

#include <gtest/gtest.h>

#include <concepts>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace ex = std::execution;
using namespace task_environment_test;

// A forwarding query and a query that is not one.
struct forwarded_query_t {
    constexpr bool query(std::forwarding_query_t) const noexcept { return true; }
};
struct local_query_t {};
struct unanswered_query_t {
    constexpr bool query(std::forwarding_query_t) const noexcept { return true; }
};
inline constexpr forwarded_query_t forwarded_query{};
inline constexpr local_query_t local_query{};
inline constexpr unanswered_query_t unanswered_query{};

static_assert(std::forwarding_query(forwarded_query));
static_assert(!std::forwarding_query(local_query));

// ---------------------------------------------------------------------------
// G2-01: own-env, Environment and query forwarding

struct construction_log {
    std::vector<std::string> events;
};

// The receiver's environment. It cannot be copied or moved, so every use of
// it below works on a prvalue with no incidental copy.
struct pinned_receiver_env {
    explicit pinned_receiver_env(construction_log& log) : log(&log) {}
    pinned_receiver_env(const pinned_receiver_env&) = delete;

    construction_log* log;
};

template<class ReceiverEnv>
inline constexpr bool is_pinned_prvalue = std::same_as<ReceiverEnv, pinned_receiver_env>;

// own-env from the receiver's environment, with the alias instantiated on
// decltype(get_env(declval<Rcvr>())).
struct aliased_environment {
    using start_scheduler_type = ex::inline_scheduler;

    template<class ReceiverEnv>
    struct env_type {
        explicit env_type(const ReceiverEnv& source) : log(source.log) {
            log->events.push_back(is_pinned_prvalue<ReceiverEnv> ? "own(receiver-env)" : "own(other)");
        }
        // The default form exists too. The receiver's environment comes first.
        env_type();
        env_type(const env_type&) = delete;

        construction_log* log;
    };

    // Environment(own-env) comes first although the other two forms exist.
    template<class ReceiverEnv>
    explicit aliased_environment(env_type<ReceiverEnv>& own) : log(own.log) {
        log->events.push_back("environment(own-env)");
    }
    explicit aliased_environment(const pinned_receiver_env& source) : log(source.log) {
        log->events.push_back("environment(receiver-env)");
    }
    aliased_environment() { std::terminate(); }
    aliased_environment(const aliased_environment&) = delete;

    // [task.promise]/12, fourth item.
    int query(forwarded_query_t, int first, int second) const noexcept { return first * 10 + second; }
    int query(local_query_t) const noexcept { return -1; }
    // The three reserved queries never reach the Environment.
    void query(ex::get_start_scheduler_t) const = delete;
    void query(std::get_allocator_t) const = delete;
    void query(std::get_stop_token_t) const = delete;

    construction_log* log = nullptr;
};

// own-env by default: the alias exists but cannot take the receiver's
// environment. Environment then cannot take own-env and takes the receiver's.
struct defaulted_own_environment {
    using start_scheduler_type = ex::inline_scheduler;

    template<class ReceiverEnv>
    struct env_type {
        env_type() = default;
        env_type(const env_type&) = delete;
    };

    explicit defaulted_own_environment(const pinned_receiver_env& source) : log(source.log) {
        log->events.push_back("environment(receiver-env)");
    }
    defaulted_own_environment() { std::terminate(); }

    construction_log* log = nullptr;
};

// No env_type that names a type: own-env is env<>. Environment(env<>&) is not
// viable here, nor is Environment(receiver-env), so it is default constructed.
construction_log* default_log = nullptr;

struct missing_alias_environment {
    using start_scheduler_type = ex::inline_scheduler;

    missing_alias_environment() { default_log->events.push_back("environment()"); }
};

struct non_type_alias_environment {
    using start_scheduler_type = ex::inline_scheduler;

    // A member template that is not a type.
    template<class ReceiverEnv>
    static constexpr int env_type = 0;

    explicit non_type_alias_environment(ex::env<>&) { default_log->events.push_back("environment(env<>)"); }
    non_type_alias_environment() { std::terminate(); }
};

template<class ReceiverEnv>
aliased_environment::env_type<ReceiverEnv>::env_type() : log(default_log) {
    log->events.push_back("own()");
}

template<class Environment>
ex::task<int, Environment> returns_seven() { co_return 7; }

template<class Environment>
std::vector<std::string> construct_with(construction_log& log) {
    outcome done;
    default_log = &log;
    {
        auto operation = ex::connect(returns_seven<Environment>(),
                                     done.receiver([&log] { return pinned_receiver_env{log}; }));
        ex::start(operation);
    }
    default_log = nullptr;
    EXPECT_EQ(done.result, 7);
    return log.events;
}

using strings = std::vector<std::string>;

TEST(ExecutionTaskEnvironment, G201_OwnEnvAndEnvironmentLadders) {
    {
        construction_log log;
        EXPECT_EQ(construct_with<aliased_environment>(log), (strings{"own(receiver-env)", "environment(own-env)"}));
    }
    {
        construction_log log;
        EXPECT_EQ(construct_with<defaulted_own_environment>(log), (strings{"environment(receiver-env)"}));
    }
    {
        construction_log log;
        EXPECT_EQ(construct_with<missing_alias_environment>(log), (strings{"environment()"}));
    }
    {
        construction_log log;
        EXPECT_EQ(construct_with<non_type_alias_environment>(log), (strings{"environment(env<>)"}));
    }
}

// Query is the type of the query expression: T for an rvalue, T& or
// const T& for an lvalue.
template<class Env, class Query, class... Args>
concept answers = requires(const Env& env, Query&& query, Args&&... args) {
    env.query(static_cast<Query&&>(query), static_cast<Args&&>(args)...);
};

// Forwarding as a const lvalue and not as an rvalue or a mutable lvalue.
struct const_lvalue_query_t {
    constexpr bool query(std::forwarding_query_t) & noexcept { return false; }
    constexpr bool query(std::forwarding_query_t) && noexcept { return false; }
    constexpr bool query(std::forwarding_query_t) const& noexcept { return true; }
};
// The opposite classification: forwarding only as an rvalue.
struct rvalue_query_t {
    constexpr bool query(std::forwarding_query_t) & noexcept { return false; }
    constexpr bool query(std::forwarding_query_t) && noexcept { return true; }
    constexpr bool query(std::forwarding_query_t) const& noexcept { return false; }
};
// Always forwarding, but its default constructor is no constant expression
// and it cannot be copied: only the supplied object can be used.
struct pinned_query_t : std::forwarding_query_t {
    pinned_query_t() {}
    explicit constexpr pinned_query_t(int) {}
    pinned_query_t(const pinned_query_t&) = delete;
};
// Not forwarding, and each convertible to exactly one reserved query. With
// one tag per conversion, an overload that took a reserved tag by value
// would accept the conversion without ambiguity.
struct to_start_scheduler_query_t {
    constexpr operator ex::get_start_scheduler_t() const noexcept { return {}; }
};
struct to_allocator_query_t {
    constexpr operator std::get_allocator_t() const noexcept { return {}; }
};
struct to_stop_token_query_t {
    constexpr operator std::get_stop_token_t() const noexcept { return {}; }
};

static_assert(!std::forwarding_query(to_start_scheduler_query_t{}));
static_assert(std::convertible_to<to_start_scheduler_query_t, ex::get_start_scheduler_t>);
static_assert(std::convertible_to<to_allocator_query_t, std::get_allocator_t>);
static_assert(std::convertible_to<to_stop_token_query_t, std::get_stop_token_t>);

// A query type that is never completed in this translation unit.
struct opaque_query_t;

struct qualified_environment {
    using start_scheduler_type = ex::inline_scheduler;

    // Every category of each query is answered: only the classification of
    // the supplied query decides.
    int query(const const_lvalue_query_t&) const noexcept { return 1; }
    int query(const_lvalue_query_t&) const noexcept { return -1; }
    int query(const_lvalue_query_t&&) const noexcept { return -1; }
    int query(rvalue_query_t&&) const noexcept { return 2; }
    int query(const rvalue_query_t&) const noexcept { return -2; }
    // The only viable overload takes the supplied object as a const lvalue.
    int query(const pinned_query_t&) const { return 3; }
    // This Environment answers the opaque query, so only its incomplete type
    // keeps it from being classified.
    int query(const opaque_query_t&) const noexcept;
    int query(to_start_scheduler_query_t) const noexcept { return -4; }
    int query(to_allocator_query_t) const noexcept { return -4; }
    int query(to_stop_token_query_t) const noexcept { return -4; }
};

ex::task<int, aliased_environment> forwards_queries() {
    co_return co_await inspect_sender{[](const auto& env) {
        using env_t = std::remove_cvref_t<decltype(env)>;
        // A forwarding query the Environment answers, with its arguments.
        static_assert(answers<env_t, forwarded_query_t, int, int>);
        static_assert(answers<env_t, const forwarded_query_t&, int, int>);
        static_assert(noexcept(env.query(forwarded_query, 1, 2)));
        // Not a forwarding query, not answered, or not answered that way.
        static_assert(!answers<env_t, local_query_t>);
        static_assert(!answers<env_t, unanswered_query_t>);
        static_assert(!answers<env_t, forwarded_query_t>);
        static_assert(!answers<env_t, forwarded_query_t, int, std::string>);
        // The reserved queries take their own paths, not the Environment's
        // deleted members, in every category of the tag.
        static_assert(std::same_as<decltype(env.query(ex::get_start_scheduler)), ex::inline_scheduler>);
        static_assert(std::same_as<decltype(env.query(ex::get_start_scheduler_t{})), ex::inline_scheduler>);
        static_assert(std::same_as<decltype(env.query(std::get_allocator)), std::allocator<std::byte>>);
        static_assert(std::same_as<decltype(env.query(std::get_allocator_t{})), std::allocator<std::byte>>);
        static_assert(noexcept(env.query(std::get_allocator)));
        // A query type that is not complete is not answered, and asking is
        // not an error.
        static_assert(!answers<env_t, const opaque_query_t&>);
        return env.query(forwarded_query, 4, 2);
    }};
}

ex::task<int, qualified_environment> forwards_qualified_queries() {
    co_return co_await inspect_sender{[](const auto& env) {
        using env_t = std::remove_cvref_t<decltype(env)>;
        // The supplied query is classified with its own category.
        static_assert(answers<env_t, const const_lvalue_query_t&>);
        static_assert(!answers<env_t, const_lvalue_query_t&>);
        static_assert(!answers<env_t, const_lvalue_query_t>);
        static_assert(answers<env_t, rvalue_query_t>);
        static_assert(!answers<env_t, const rvalue_query_t&>);
        static_assert(!answers<env_t, rvalue_query_t&>);
        // No stand-in object is default constructed or copied.
        static_assert(answers<env_t, const pinned_query_t&>);
        static_assert(answers<env_t, pinned_query_t&>);
        static_assert(!noexcept(env.query(std::declval<const pinned_query_t&>())));
        // A query that is not forwarding is not answered, and its conversion
        // to a reserved query does not reach that query's path. The genuine
        // tags are answered.
        static_assert(!answers<env_t, to_start_scheduler_query_t>);
        static_assert(!answers<env_t, const to_start_scheduler_query_t&>);
        static_assert(!answers<env_t, to_allocator_query_t>);
        static_assert(!answers<env_t, const to_allocator_query_t&>);
        static_assert(!answers<env_t, to_stop_token_query_t>);
        static_assert(!answers<env_t, const to_stop_token_query_t&>);
        static_assert(answers<env_t, ex::get_start_scheduler_t>);
        static_assert(answers<env_t, const std::get_allocator_t&>);
        // Answered by the Environment, but not complete: not forwarded, and
        // no class trait is asked about it.
        static_assert(!answers<env_t, const opaque_query_t&>);
        const const_lvalue_query_t as_const_lvalue{};
        const pinned_query_t pinned{0};
        return env.query(as_const_lvalue) * 100 + env.query(rvalue_query_t{}) * 10 + env.query(pinned);
    }};
}

TEST(ExecutionTaskEnvironment, G201_ForwardsOnlyForwardingQueriesWithTheirArguments) {
    construction_log log;
    outcome done;
    {
        auto operation =
            ex::connect(forwards_queries(), done.receiver([&log] { return pinned_receiver_env{log}; }));
        ex::start(operation);
    }
    EXPECT_EQ(done.result, 42);
}

TEST(ExecutionTaskEnvironment, G201_ForwardingKeepsTheQuerysOwnTypeAndCategory) {
    outcome done;
    {
        auto operation = ex::connect(forwards_qualified_queries(), done.receiver([] { return ex::env<>{}; }));
        ex::start(operation);
    }
    EXPECT_EQ(done.result, 123);
    EXPECT_EQ(done.terminals, 1);
}

// ---------------------------------------------------------------------------
// G2-02: the promise's ordinary allocator

template<class T>
struct id_allocator {
    using value_type = T;

    constexpr explicit id_allocator(int id) noexcept : id(id) {}
    template<class U>
    constexpr id_allocator(const id_allocator<U>& other) noexcept : id(other.id) {}

    T* allocate(std::size_t count) { return std::allocator<T>{}.allocate(count); }
    void deallocate(T* pointer, std::size_t count) noexcept { std::allocator<T>{}.deallocate(pointer, count); }

    template<class U>
    bool operator==(const id_allocator<U>& other) const noexcept { return id == other.id; }

    int id;
};

// allocator_type with a default identity of its own.
template<class T>
struct defaulted_id_allocator : id_allocator<T> {
    template<class U>
    struct rebind {
        using other = defaulted_id_allocator<U>;
    };

    defaulted_id_allocator() noexcept : id_allocator<T>(-5) {}
    template<class U>
    defaulted_id_allocator(const id_allocator<U>& other) noexcept : id_allocator<T>(other) {}
};

struct allocator_environment {
    using start_scheduler_type = ex::inline_scheduler;
    using allocator_type = defaulted_id_allocator<std::byte>;
};

ex::task<int, allocator_environment> reads_allocator() {
    auto allocator = co_await ex::read_env(std::get_allocator);
    static_assert(std::same_as<decltype(allocator), defaulted_id_allocator<std::byte>>);
    co_return allocator.id;
}

TEST(ExecutionTaskEnvironment, G202_PromiseAllocatorComesFromTheReceiverOrTheDefault) {
    {
        // allocator_type(get_allocator(get_env(rcvr))), with a different
        // allocator type in the receiver's environment.
        outcome done;
        auto operation = ex::connect(reads_allocator(), done.receiver([] {
            return ex::prop{std::get_allocator, id_allocator<long>{31}};
        }));
        ex::start(operation);
        EXPECT_EQ(done.result, 31);
    }
    {
        // No allocator in the receiver's environment: allocator_type().
        outcome done;
        auto operation = ex::connect(reads_allocator(), done.receiver([] { return ex::env<>{}; }));
        ex::start(operation);
        EXPECT_EQ(done.result, -5);
    }
    {
        // The scheduler allocator is a different query and is not consulted.
        outcome done;
        auto operation = ex::connect(reads_allocator(), done.receiver([] {
            return ex::prop{std::get_scheduler_allocator, id_allocator<long>{32}};
        }));
        ex::start(operation);
        EXPECT_EQ(done.result, -5);
    }
}

// [exec.get.scheduler.allocator]: the const scheduler-specific member, then
// the const ordinary member, else no call.
struct both_allocators_env {
    constexpr id_allocator<int> query(std::get_scheduler_allocator_t) const noexcept {
        return id_allocator<int>{2};
    }
    constexpr id_allocator<int> query(std::get_allocator_t) const noexcept { return id_allocator<int>{1}; }
};
struct ordinary_allocator_env {
    constexpr id_allocator<int> query(std::get_allocator_t) const noexcept { return id_allocator<int>{1}; }
};

static_assert(std::get_scheduler_allocator(both_allocators_env{}).id == 2);
static_assert(std::get_scheduler_allocator(ordinary_allocator_env{}).id == 1);
static_assert(std::get_allocator(both_allocators_env{}).id == 1);
static_assert(!std::invocable<std::get_scheduler_allocator_t, ex::env<>>);
static_assert(std::forwarding_query(std::get_scheduler_allocator));
static_assert(noexcept(std::get_scheduler_allocator(both_allocators_env{})));

// ---------------------------------------------------------------------------
// G2-03: the start scheduler

struct query_counts {
    int scheduler = 0, scheduler_allocator = 0, ordinary_allocator = 0;
};

// The scheduler the receiver's environment names. It is not the task's
// start_scheduler_type unless a case says so.
struct named_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    struct sender {
        using sender_concept = ex::sender_tag;

        template<class Self, class... Env>
        static consteval auto get_completion_signatures() noexcept {
            return ex::completion_signatures<ex::set_value_t()>{};
        }

        template<ex::receiver Receiver>
        auto connect(Receiver receiver) const {
            return ex::connect(ex::schedule(ex::inline_scheduler{}), std::move(receiver));
        }
    };

    sender schedule() const noexcept { return {}; }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const named_scheduler&) const noexcept = default;

    int name = 0;
};

// start_scheduler_type. Each initializer leaves its own tag, and Arms says
// which of the four constructing forms exist: bit 0 for S(s, a), bit 1 for
// S(s), bit 2 for S(a), bit 3 for S().
template<unsigned Arms>
struct tagged_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    tagged_scheduler(named_scheduler from, id_allocator<int> allocator) noexcept
        requires ((Arms & 1u) != 0)
        : tag(2), name(from.name), allocator_id(allocator.id) {}
    explicit tagged_scheduler(named_scheduler from) noexcept
        requires ((Arms & 2u) != 0)
        : tag(3), name(from.name) {}
    explicit tagged_scheduler(id_allocator<int> allocator) noexcept
        requires ((Arms & 4u) != 0)
        : tag(4), allocator_id(allocator.id) {}
    tagged_scheduler() noexcept
        requires ((Arms & 8u) != 0)
        : tag(5) {}
    // The exact form: a scheduler made elsewhere, with what it retained.
    struct premade {};
    tagged_scheduler(premade, int retained) noexcept : tag(1), allocator_id(retained) {}
    // S(s, a) is viable for an exact scheduler too, and leaves another tag.
    tagged_scheduler(const tagged_scheduler&, id_allocator<int> allocator) noexcept
        requires ((Arms & 1u) != 0)
        : tag(6), allocator_id(allocator.id) {}
    tagged_scheduler(const tagged_scheduler&) = default;

    named_scheduler::sender schedule() const noexcept { return {}; }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const tagged_scheduler&) const noexcept = default;

    int tag;
    int name = -1;
    int allocator_id = -1;
};

static_assert(ex::scheduler<tagged_scheduler<15>>);
static_assert(ex::scheduler<tagged_scheduler<0>>);

template<unsigned Arms>
struct arms_environment {
    using start_scheduler_type = tagged_scheduler<Arms>;
};

// Every query counts its evaluations. The scheduler allocator (7) differs
// from the ordinary one (3).
template<class Scheduler, class Answer = Scheduler>
struct counting_env {
    Answer query(ex::get_start_scheduler_t) const noexcept {
        ++counts->scheduler;
        return scheduler;
    }
    id_allocator<int> query(std::get_scheduler_allocator_t) const noexcept {
        ++counts->scheduler_allocator;
        return id_allocator<int>{7};
    }
    id_allocator<int> query(std::get_allocator_t) const noexcept {
        ++counts->ordinary_allocator;
        return id_allocator<int>{3};
    }

    query_counts* counts;
    Scheduler scheduler;
};

template<unsigned Arms>
ex::task<int, arms_environment<Arms>> reads_start_scheduler(std::allocator_arg_t, id_allocator<int>,
                                                            tagged_scheduler<Arms>* seen) {
    // The promise answers with a copy of SCHED.
    *seen = co_await ex::read_env(ex::get_start_scheduler);
    co_return seen->tag;
}

template<unsigned Arms, class Scheduler, class Answer = Scheduler>
tagged_scheduler<Arms> select_with(const Scheduler& named, query_counts& counts) {
    tagged_scheduler<Arms> seen{typename tagged_scheduler<Arms>::premade{}, -99};
    outcome done;
    {
        // The frame allocator (11) is yet another identity.
        auto operation = ex::connect(
            reads_start_scheduler<Arms>(std::allocator_arg, id_allocator<int>{11}, &seen),
            done.receiver([&counts, &named] { return counting_env<Scheduler, Answer>{&counts, named}; }));
        ex::start(operation);
    }
    EXPECT_EQ(done.result, seen.tag);
    return seen;
}

// The three query counters of one selection: scheduler, scheduler allocator
// and ordinary allocator. The receiver's environment answers all three.
void expect_queries(const query_counts& counts, int scheduler, int scheduler_allocator) {
    EXPECT_EQ(counts.scheduler, scheduler);
    EXPECT_EQ(counts.scheduler_allocator, scheduler_allocator);
    // The ordinary allocator plays no part in any form.
    EXPECT_EQ(counts.ordinary_allocator, 0);
}

TEST(ExecutionTaskEnvironment, G203_StartSchedulerUsesTheFirstViableInitializer) {
    {
        // Exact type after removing const and the reference: the scheduler
        // itself, although S(s, a) and every later form are viable. It keeps
        // the allocator it had, and the allocator query is not evaluated.
        static_assert(std::constructible_from<tagged_scheduler<15>, const tagged_scheduler<15>&,
                                              id_allocator<int>>);
        query_counts counts;
        const tagged_scheduler<15> premade{tagged_scheduler<15>::premade{}, 9};
        const auto seen =
            select_with<15, tagged_scheduler<15>, const tagged_scheduler<15>&>(premade, counts);
        EXPECT_EQ(seen.tag, 1);
        EXPECT_EQ(seen.allocator_id, 9);
        expect_queries(counts, 1, 0);
    }
    {
        // The same answer by value.
        query_counts counts;
        const tagged_scheduler<15> premade{tagged_scheduler<15>::premade{}, 9};
        const auto seen = select_with<15>(premade, counts);
        EXPECT_EQ(seen.tag, 1);
        EXPECT_EQ(seen.allocator_id, 9);
        expect_queries(counts, 1, 0);
    }
    {
        // S(s, a), with the scheduler allocator and not the ordinary one.
        query_counts counts;
        const auto seen = select_with<15>(named_scheduler{4}, counts);
        EXPECT_EQ(seen.tag, 2);
        EXPECT_EQ(seen.name, 4);
        EXPECT_EQ(seen.allocator_id, 7);
        expect_queries(counts, 1, 1);
    }
    {
        // S(s), with S(a) and S() still viable.
        query_counts counts;
        const auto seen = select_with<14>(named_scheduler{5}, counts);
        EXPECT_EQ(seen.tag, 3);
        EXPECT_EQ(seen.name, 5);
        expect_queries(counts, 1, 0);
    }
    {
        // S(a), with S() still viable. The scheduler query is not evaluated.
        query_counts counts;
        const auto seen = select_with<12>(named_scheduler{6}, counts);
        EXPECT_EQ(seen.tag, 4);
        EXPECT_EQ(seen.allocator_id, 7);
        expect_queries(counts, 0, 1);
    }
    {
        // S(): nothing is evaluated.
        query_counts counts;
        const auto seen = select_with<8>(named_scheduler{7}, counts);
        EXPECT_EQ(seen.tag, 5);
        expect_queries(counts, 0, 0);
    }
}

// ---------------------------------------------------------------------------
// G2-04: the answers stay valid for the operation's lifetime

// A scheduler and an allocator that are handles to a shared resource.
struct resource {
    int schedules = 0, allocations = 0, releases = 0;
};

struct resource_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    struct sender {
        using sender_concept = ex::sender_tag;

        template<class Self, class... Env>
        static consteval auto get_completion_signatures() noexcept {
            return ex::completion_signatures<ex::set_value_t()>{};
        }

        template<ex::receiver Receiver>
        auto connect(Receiver receiver) const {
            ++owner->schedules;
            return ex::connect(ex::schedule(ex::inline_scheduler{}), std::move(receiver));
        }

        std::shared_ptr<resource> owner;
    };

    sender schedule() const noexcept { return {owner}; }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const resource_scheduler&) const noexcept = default;

    std::shared_ptr<resource> owner;
};

template<class T>
struct resource_allocator {
    using value_type = T;

    explicit resource_allocator(std::shared_ptr<resource> owner) noexcept : owner(std::move(owner)) {}
    template<class U>
    resource_allocator(const resource_allocator<U>& other) noexcept : owner(other.owner) {}

    T* allocate(std::size_t count) {
        ++owner->allocations;
        return std::allocator<T>{}.allocate(count);
    }
    void deallocate(T* pointer, std::size_t count) noexcept {
        ++owner->releases;
        std::allocator<T>{}.deallocate(pointer, count);
    }

    template<class U>
    bool operator==(const resource_allocator<U>& other) const noexcept { return owner == other.owner; }

    std::shared_ptr<resource> owner;
};

// Borrowed answers: references to objects that outlive the operation.
struct borrowing_env {
    const resource_scheduler& query(ex::get_start_scheduler_t) const noexcept { return *scheduler; }
    const resource_allocator<int>& query(std::get_scheduler_allocator_t) const noexcept { return *allocator; }
    borrowing_env(const resource_scheduler& scheduler, const resource_allocator<int>& allocator)
        : scheduler(&scheduler), allocator(&allocator) {}
    borrowing_env(const borrowing_env&) = delete;

    const resource_scheduler* scheduler;
    const resource_allocator<int>* allocator;
};

// Prvalue answers: handles made for the query, which own their resource.
struct owning_env {
    resource_scheduler query(ex::get_start_scheduler_t) const noexcept { return {*owner}; }
    resource_allocator<int> query(std::get_scheduler_allocator_t) const noexcept {
        return resource_allocator<int>{*owner};
    }
    explicit owning_env(const std::shared_ptr<resource>& owner) : owner(&owner) {}
    owning_env(const owning_env&) = delete;

    const std::shared_ptr<resource>* owner;
};

// The default task erases its start scheduler with the scheduler allocator.
ex::task<int> schedules_later(gate* held, const resource_scheduler* expected) {
    co_await gate_sender{held};
    // Inspected only after start has returned.
    ex::task_scheduler current = co_await ex::read_env(ex::get_start_scheduler);
    const bool same = current == *expected;
    co_await ex::schedule(current);
    co_return same ? 1 : 0;
}

template<class MakeEnv>
void run_with_answers(const std::shared_ptr<resource>& owner, const resource_scheduler& expected,
                      MakeEnv make_env) {
    gate held;
    outcome done;
    {
        auto operation = ex::connect(schedules_later(&held, &expected), done.receiver(make_env));
        ex::start(operation);
        ASSERT_TRUE(held.open);
        EXPECT_FALSE(done.result);
        EXPECT_EQ(done.terminals, 0);
        // The eraser was allocated through the scheduler allocator at start.
        EXPECT_GE(owner->allocations, 1);
        const int scheduled_at_start = owner->schedules;
        held.open();
        EXPECT_EQ(done.result, 1);
        EXPECT_EQ(done.terminals, 1);
        EXPECT_GT(owner->schedules, scheduled_at_start);
    }
    // One terminal for this input, also after the operation is destroyed.
    EXPECT_EQ(done.terminals, 1);
    EXPECT_TRUE(done.terminal.empty());
    EXPECT_EQ(owner->allocations, owner->releases);
}

TEST(ExecutionTaskEnvironment, G204_QueryAnswersStayValidThroughTheOperation) {
    {
        auto owner = std::make_shared<resource>();
        const resource_scheduler scheduler{owner};
        const resource_allocator<int> allocator{owner};
        run_with_answers(owner, scheduler, [&]() -> borrowing_env { return borrowing_env{scheduler, allocator}; });
    }
    {
        auto owner = std::make_shared<resource>();
        const resource_scheduler scheduler{owner};
        run_with_answers(owner, scheduler, [&]() -> owning_env { return owning_env{owner}; });
    }
}

// A start scheduler that keeps no copy of what it was made from: it refers to
// the operands of its constructor, which are borrowed answers of the
// receiver's environment. Every scheduler and allocator handle below records
// where it lives, so a test can ask whether an object still exists before it
// touches it. A scheduler that kept a reference to a prvalue argument would
// be outside the contract, as for any type.
std::set<const void*>* live_objects = nullptr;

struct tracked {
    tracked() { live_objects->insert(this); }
    tracked(const tracked&) { live_objects->insert(this); }
    ~tracked() { live_objects->erase(this); }
};

struct lent_scheduler : tracked {
    using scheduler_concept = ex::scheduler_tag;

    explicit lent_scheduler(int name) : name(name) {}

    named_scheduler::sender schedule() const noexcept { return {}; }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const lent_scheduler& other) const noexcept { return name == other.name; }

    int name;
};

template<class T>
struct lent_allocator : tracked {
    using value_type = T;

    explicit lent_allocator(int id) noexcept : id(id) {}
    template<class U>
    lent_allocator(const lent_allocator<U>& other) noexcept : id(other.id) {}

    T* allocate(std::size_t count) { return std::allocator<T>{}.allocate(count); }
    void deallocate(T* pointer, std::size_t count) noexcept { std::allocator<T>{}.deallocate(pointer, count); }
    template<class U>
    bool operator==(const lent_allocator<U>& other) const noexcept { return id == other.id; }

    int id;
};

std::vector<bool>* referents_at_destruction = nullptr;

// Arms: bit 0 for S(s, a), bit 1 for S(s), bit 2 for S(a).
template<unsigned Arms>
struct borrowing_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    borrowing_scheduler(const lent_scheduler& from, const lent_allocator<int>& allocator) noexcept
        requires ((Arms & 1u) != 0)
        : scheduler(&from), allocator(&allocator) {}
    explicit borrowing_scheduler(const lent_scheduler& from) noexcept
        requires ((Arms & 2u) != 0)
        : scheduler(&from) {}
    explicit borrowing_scheduler(const lent_allocator<int>& allocator) noexcept
        requires ((Arms & 4u) != 0)
        : allocator(&allocator) {}
    borrowing_scheduler(const borrowing_scheduler&) noexcept = default;
    borrowing_scheduler& operator=(const borrowing_scheduler&) noexcept = default;
    // While a test watches, each destroyed scheduler records whether what it
    // refers to still exists. It asks the registry and touches nothing.
    ~borrowing_scheduler() {
        if (referents_at_destruction == nullptr) return;
        referents_at_destruction->push_back((scheduler == nullptr || live_objects->contains(scheduler)) &&
                                            (allocator == nullptr || live_objects->contains(allocator)));
    }

    named_scheduler::sender schedule() const noexcept { return {}; }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const borrowing_scheduler&) const noexcept = default;

    const lent_scheduler* scheduler = nullptr;
    const lent_allocator<int>* allocator = nullptr;
};

static_assert(ex::scheduler<borrowing_scheduler<7>>);

template<unsigned Arms>
struct borrowing_environment {
    using start_scheduler_type = borrowing_scheduler<Arms>;
};

// A prvalue environment that owns what its queries refer to.
struct holding_env {
    const lent_scheduler& query(ex::get_start_scheduler_t) const noexcept {
        ++counts->scheduler;
        return scheduler;
    }
    const lent_allocator<int>& query(std::get_scheduler_allocator_t) const noexcept {
        ++counts->scheduler_allocator;
        return allocator;
    }
    explicit holding_env(query_counts& counts) : counts(&counts) {}
    holding_env(const holding_env&) = delete;

    query_counts* counts;
    lent_scheduler scheduler{21};
    lent_allocator<int> allocator{22};
};

// What the body found behind SCHED after start had returned. A referent is
// looked at only if it still exists.
struct borrowed_view {
    bool scheduler_set = false, scheduler_live = false, allocator_set = false, allocator_live = false;
    int name = -1, id = -1;
};

template<unsigned Arms>
ex::task<int, borrowing_environment<Arms>> inspects_borrowed_operands(gate* held, borrowed_view* view) {
    co_await gate_sender{held};
    const borrowing_scheduler<Arms> current = co_await ex::read_env(ex::get_start_scheduler);
    view->scheduler_set = current.scheduler != nullptr;
    view->allocator_set = current.allocator != nullptr;
    view->scheduler_live = live_objects->contains(current.scheduler);
    view->allocator_live = live_objects->contains(current.allocator);
    if (view->scheduler_live) view->name = current.scheduler->name;
    if (view->allocator_live) view->id = current.allocator->id;
    if ((view->scheduler_set && !view->scheduler_live) || (view->allocator_set && !view->allocator_live)) {
        co_return -1;
    }
    co_await ex::schedule(current);
    co_return 1;
}

TEST(ExecutionTaskEnvironment, G204_SchedulerMayReferToItsSelectedOperands) {
    // Borrowed answers: references into a prvalue environment, which the
    // operation keeps. The environment holds both objects, whichever were
    // selected.
    {
        std::set<const void*> live;
        live_objects = &live;
        for (unsigned arms : {7u, 6u, 4u}) {
            SCOPED_TRACE(arms);
            query_counts counts;
            borrowed_view view;
            gate held;
            outcome done;
            std::vector<bool> at_destruction;
            const auto run = [&](auto task) {
                auto operation = ex::connect(
                    std::move(task), done.receiver([&counts]() -> holding_env { return holding_env{counts}; }));
                ex::start(operation);
                held.open();
                EXPECT_EQ(live.size(), 2u);
                // Every copy the body made is gone. What is destroyed from
                // here on is SCHED, with the operation.
                referents_at_destruction = &at_destruction;
            };
            if (arms == 7u) run(inspects_borrowed_operands<7>(&held, &view));
            if (arms == 6u) run(inspects_borrowed_operands<6>(&held, &view));
            if (arms == 4u) run(inspects_borrowed_operands<4>(&held, &view));
            referents_at_destruction = nullptr;
            EXPECT_TRUE(live.empty());
            // SCHED was destroyed before the environment it refers into.
            EXPECT_EQ(at_destruction, (std::vector<bool>{true}));
            EXPECT_EQ(view.scheduler_live, view.scheduler_set);
            EXPECT_EQ(view.allocator_live, view.allocator_set);
            EXPECT_EQ(view.scheduler_set, arms != 4u);
            EXPECT_EQ(view.allocator_set, arms != 6u);
            EXPECT_EQ(view.name, arms != 4u ? 21 : -1);
            EXPECT_EQ(view.id, arms != 6u ? 22 : -1);
            EXPECT_EQ(done.result, 1);
            EXPECT_EQ(done.terminals, 1);
            EXPECT_EQ(counts.scheduler, arms != 4u ? 1 : 0);
            EXPECT_EQ(counts.scheduler_allocator, arms != 6u ? 1 : 0);
        }
        live_objects = nullptr;
    }
}

// The exact form. The scheduler the environment answers with is the start
// scheduler type itself and refers to a handle that the environment owns.
struct pointing_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    named_scheduler::sender schedule() const noexcept { return {}; }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const pointing_scheduler&) const noexcept = default;

    const lent_scheduler* handle = nullptr;
};

struct pointing_environment {
    using start_scheduler_type = pointing_scheduler;
};

// A prvalue environment that owns the handle its exact scheduler refers to.
// It also has a scheduler allocator, which the exact form must not evaluate.
struct pointing_env {
    pointing_scheduler query(ex::get_start_scheduler_t) const noexcept {
        ++counts->scheduler;
        return {&handle};
    }
    lent_allocator<int> query(std::get_scheduler_allocator_t) const noexcept {
        ++counts->scheduler_allocator;
        return lent_allocator<int>{22};
    }
    explicit pointing_env(query_counts& counts) : counts(&counts) {}
    pointing_env(const pointing_env&) = delete;

    query_counts* counts;
    lent_scheduler handle{23};
};

ex::task<int, pointing_environment> inspects_exact_scheduler(gate* held, borrowed_view* view) {
    co_await gate_sender{held};
    const pointing_scheduler current = co_await ex::read_env(ex::get_start_scheduler);
    view->scheduler_set = current.handle != nullptr;
    view->scheduler_live = live_objects->contains(current.handle);
    if (!view->scheduler_live) co_return -1;
    view->name = current.handle->name;
    co_await ex::schedule(current);
    co_return 1;
}

TEST(ExecutionTaskEnvironment, G204_ExactSchedulerMayReferIntoItsPrvalueEnvironment) {
    std::set<const void*> live;
    live_objects = &live;
    query_counts counts;
    borrowed_view view;
    gate held;
    outcome done;
    {
        auto operation = ex::connect(inspects_exact_scheduler(&held, &view),
                                     done.receiver([&counts]() -> pointing_env { return pointing_env{counts}; }));
        ex::start(operation);
        ASSERT_TRUE(held.open);
        // The environment, and the handle in it, outlived start.
        EXPECT_EQ(live.size(), 1u);
        held.open();
        EXPECT_EQ(live.size(), 1u);
    }
    EXPECT_TRUE(live.empty());
    live_objects = nullptr;
    EXPECT_TRUE(view.scheduler_set);
    EXPECT_TRUE(view.scheduler_live);
    EXPECT_EQ(view.name, 23);
    EXPECT_EQ(done.result, 1);
    EXPECT_EQ(done.terminals, 1);
    EXPECT_EQ(counts.scheduler, 1);
    EXPECT_EQ(counts.scheduler_allocator, 0);
}

// How a selected operand reaches the constructor. Copies and moves of the
// operand and of the start scheduler are counted.
struct move_counts {
    int operand_copies = 0, operand_moves = 0, scheduler_copies = 0, scheduler_moves = 0;
    // The query's result: alive inside the selected constructor, and whether
    // it is gone when the coroutine body is entered.
    std::optional<bool> alive_in_constructor, gone_at_body;
};
move_counts* moves = nullptr;
// Watches the marker of the operand made last. Every copy shares the marker,
// so it expires only when the operand and all copies of it are gone.
std::weak_ptr<int> operand_watch;

struct counted_operand {
    using scheduler_concept = ex::scheduler_tag;

    explicit counted_operand(int name) : name(name), marker(std::make_shared<int>(name)) { operand_watch = marker; }
    counted_operand(const counted_operand& other) noexcept : name(other.name), marker(other.marker) {
        ++moves->operand_copies;
    }
    counted_operand(counted_operand&& other) noexcept : name(other.name), marker(other.marker) {
        ++moves->operand_moves;
    }
    counted_operand& operator=(const counted_operand&) noexcept = default;

    named_scheduler::sender schedule() const noexcept { return {}; }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const counted_operand& other) const noexcept { return name == other.name; }

    int name;
    std::shared_ptr<int> marker;
};

// ByValue: the constructor takes its operand by value, or by const reference.
template<bool ByValue>
struct counted_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    explicit counted_scheduler(counted_operand from) noexcept
        requires ByValue
        : name(from.name) {
        moves->alive_in_constructor = !operand_watch.expired();
    }
    explicit counted_scheduler(const counted_operand& from) noexcept
        requires (!ByValue)
        : name(from.name) {
        moves->alive_in_constructor = !operand_watch.expired();
    }
    counted_scheduler(const counted_scheduler& other) noexcept : name(other.name) {
        if (moves) ++moves->scheduler_copies;
    }
    counted_scheduler(counted_scheduler&& other) noexcept : name(other.name) {
        if (moves) ++moves->scheduler_moves;
    }
    counted_scheduler& operator=(const counted_scheduler&) noexcept = default;

    named_scheduler::sender schedule() const noexcept { return {}; }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const counted_scheduler& other) const noexcept { return name == other.name; }

    int name;
};

template<bool ByValue>
struct counted_environment {
    using start_scheduler_type = counted_scheduler<ByValue>;
};

// Answer is the type of the query's result: a prvalue, a const prvalue or a
// reference to an object the environment owns.
template<class Answer>
struct counted_operand_env {
    Answer query(ex::get_start_scheduler_t) const noexcept {
        if constexpr (std::is_reference_v<Answer>) {
            return owned;
        } else {
            return counted_operand{31};
        }
    }

    counted_operand owned{31};
};

template<bool ByValue>
ex::task<int, counted_environment<ByValue>> starts_and_returns() {
    // SCHED and the operation are alive here.
    moves->gone_at_body = operand_watch.expired();
    co_return 3;
}

template<bool ByValue, class Answer>
move_counts count_start() {
    move_counts counts;
    outcome done;
    {
        auto operation = ex::connect(starts_and_returns<ByValue>(),
                                     done.receiver([] { return counted_operand_env<Answer>{}; }));
        moves = &counts;
        ex::start(operation);
        moves = nullptr;
    }
    EXPECT_EQ(done.result, 3);
    return counts;
}

void expect_counts(const move_counts& counts, int operand_copies, bool gone_at_body) {
    EXPECT_EQ(counts.alive_in_constructor, true);
    EXPECT_EQ(counts.gone_at_body, gone_at_body);
    EXPECT_EQ(counts.operand_copies, operand_copies);
    EXPECT_EQ(counts.operand_moves, 0);
    EXPECT_EQ(counts.scheduler_copies, 0);
    EXPECT_EQ(counts.scheduler_moves, 0);
}

// The selected expression is evaluated as the draft writes it. A prvalue
// answer initializes the constructor's parameter directly, and SCHED is
// initialized in place: no copy and no move of either. A prvalue answer is
// not kept anywhere: it is alive in the constructor and gone when the body
// starts. A borrowed answer lives in the kept environment.
TEST(ExecutionTaskEnvironment, G204_SelectedOperandsAndSchedAreNotCopied) {
    {
        SCOPED_TRACE("prvalue into a by-value parameter");
        expect_counts(count_start<true, counted_operand>(), 0, true);
    }
    {
        SCOPED_TRACE("const prvalue into a by-value parameter");
        expect_counts(count_start<true, const counted_operand>(), 0, true);
    }
    {
        SCOPED_TRACE("prvalue bound to a reference parameter");
        expect_counts(count_start<false, counted_operand>(), 0, true);
        expect_counts(count_start<false, const counted_operand>(), 0, true);
    }
    {
        SCOPED_TRACE("borrowed answer bound to a reference parameter");
        expect_counts(count_start<false, const counted_operand&>(), 0, false);
    }
    {
        // The one copy the draft's expression itself makes.
        SCOPED_TRACE("borrowed answer into a by-value parameter");
        expect_counts(count_start<true, const counted_operand&>(), 1, false);
    }
}

// MSVC 19.52 does not elide a volatile class prvalue returned through the
// query CPO's own return statement and asks for a copy constructor that takes
// a volatile object (C2440 in get_start_scheduler). GCC, Clang and AppleClang
// elide it, as [dcl.init] requires. The control below cannot be written
// without that prvalue, so it is compiled on every compiler except MSVC.
// clang-cl is not excluded.
#if !defined(_MSC_VER) || defined(__clang__)
// A query that answers with a volatile prvalue. S(Q) by value is initialized
// from it directly, which no stored object of that type could do.
struct volatile_operand {
    using scheduler_concept = ex::scheduler_tag;

    volatile_operand() = default;
    volatile_operand(const volatile_operand&) = default;

    named_scheduler::sender schedule() const volatile noexcept { return {}; }
    auto query(ex::get_forward_progress_guarantee_t) const volatile noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    friend bool operator==(const volatile volatile_operand&, const volatile volatile_operand&) noexcept {
        return true;
    }
};

struct from_volatile_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    explicit from_volatile_scheduler(volatile_operand) noexcept : made(true) {}

    named_scheduler::sender schedule() const noexcept { return {}; }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const from_volatile_scheduler&) const noexcept = default;

    bool made = false;
};

static_assert(std::constructible_from<from_volatile_scheduler, volatile_operand>);
static_assert(!std::constructible_from<from_volatile_scheduler, volatile volatile_operand&&>);

struct from_volatile_environment {
    using start_scheduler_type = from_volatile_scheduler;
};

struct volatile_operand_env {
    volatile volatile_operand query(ex::get_start_scheduler_t) const noexcept {
        ++*evaluations;
        return {};
    }
    int* evaluations;
};

ex::task<int, from_volatile_environment> reads_made_flag() {
    const from_volatile_scheduler current = co_await ex::read_env(ex::get_start_scheduler);
    co_return current.made ? 1 : 0;
}

TEST(ExecutionTaskEnvironment, G204_VolatilePrvalueOperandIsPassedDirectly) {
    int evaluations = 0;
    outcome done;
    {
        auto operation =
            ex::connect(reads_made_flag(), done.receiver([&] { return volatile_operand_env{&evaluations}; }));
        ex::start(operation);
    }
    EXPECT_EQ(done.result, 1);
    EXPECT_EQ(evaluations, 1);
}
#endif

// The exact form with a prvalue answer that owns its resource: the scheduler
// holds the last strong reference to a lease. The lease must last until the
// outward completion begins, which for a yielded error is after the frame is
// destroyed and while the error is converted.
int leases_destroyed = 0;

struct lease {
    lease() = default;
    lease(const lease&) = delete;
    ~lease() { ++leases_destroyed; }
};

struct leasing_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    named_scheduler::sender schedule() const noexcept { return {}; }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const leasing_scheduler&) const noexcept = default;

    std::shared_ptr<lease> held;
};

// The environment is a factory. It keeps no reference to what it hands out.
struct leasing_env {
    leasing_scheduler query(ex::get_start_scheduler_t) const noexcept {
        ++counts->scheduler;
        return {std::make_shared<lease>()};
    }
    std::allocator<int> query(std::get_scheduler_allocator_t) const noexcept {
        ++counts->scheduler_allocator;
        return {};
    }

    query_counts* counts;
};

struct lease_observations {
    int frames_alive_at_conversion = -1, leases_destroyed_at_conversion = -1;
    std::optional<bool> alive_at_conversion;
};
lease_observations* lease_seen = nullptr;
int lease_frames = 0;

struct lease_probe {
    std::weak_ptr<lease> watched;
};

// The task's error type. Its conversion from the yielded payload runs in the
// terminal, after the frame is gone.
struct lease_report {
    lease_report(lease_probe probe) {
        lease_seen->frames_alive_at_conversion = lease_frames;
        lease_seen->leases_destroyed_at_conversion = leases_destroyed;
        lease_seen->alive_at_conversion = !probe.watched.expired();
    }
};

struct leasing_environment {
    using start_scheduler_type = leasing_scheduler;
    using error_types = ex::completion_signatures<ex::set_error_t(lease_report)>;
};

struct frame_marker {
    frame_marker() { ++lease_frames; }
    frame_marker(const frame_marker&) = delete;
    ~frame_marker() { --lease_frames; }
};

ex::task<int, leasing_environment> yields_a_lease_probe(std::weak_ptr<lease>* watched) {
    frame_marker in_frame;
    {
        // The body's own copy of SCHED is gone before the yield.
        const leasing_scheduler current = co_await ex::read_env(ex::get_start_scheduler);
        *watched = current.held;
    }
    co_yield ex::with_error{lease_probe{*watched}};
    co_return 0;
}

TEST(ExecutionTaskEnvironment, G204_ExactPrvalueSchedulerOutlivesTheFrameUntilCompletion) {
    lease_observations seen;
    lease_seen = &seen;
    lease_frames = 0;
    leases_destroyed = 0;
    query_counts counts;
    std::weak_ptr<lease> watched;
    std::optional<bool> alive_at_terminal;
    outcome done;
    {
        auto operation = ex::connect(yields_a_lease_probe(&watched),
                                     done.receiver([&counts] { return leasing_env{&counts}; },
                                                   [&] { alive_at_terminal = !watched.expired(); }));
        ex::start(operation);
        EXPECT_EQ(done.terminal, "error");
        EXPECT_EQ(done.terminals, 1);
        // The frame was gone when the error was converted, and the lease was
        // not: SCHED lives in the operation.
        EXPECT_EQ(seen.frames_alive_at_conversion, 0);
        EXPECT_EQ(seen.leases_destroyed_at_conversion, 0);
        EXPECT_EQ(seen.alive_at_conversion, true);
        EXPECT_EQ(alive_at_terminal, true);
        EXPECT_FALSE(watched.expired());
        EXPECT_EQ(leases_destroyed, 0);
    }
    // Released with the operation.
    EXPECT_TRUE(watched.expired());
    EXPECT_EQ(leases_destroyed, 1);
    lease_seen = nullptr;
    EXPECT_EQ(counts.scheduler, 1);
    EXPECT_EQ(counts.scheduler_allocator, 0);
}

// The object alias with an lvalue answer: SCHED is one copy of the answered
// scheduler, held by the operation. A reference start_scheduler_type is
// rejected at compile time, which a configure probe pins.
struct identity_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    static inline int copies = 0;

    identity_scheduler() = default;
    identity_scheduler(const identity_scheduler&) noexcept { ++copies; }
    identity_scheduler& operator=(const identity_scheduler&) noexcept = default;

    named_scheduler::sender schedule() const noexcept { return {}; }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const identity_scheduler&) const noexcept { return true; }
};

struct identity_environment {
    using start_scheduler_type = identity_scheduler;
};

struct lvalue_answer_env {
    identity_scheduler& query(ex::get_start_scheduler_t) const noexcept {
        ++*evaluations;
        return *scheduler;
    }

    identity_scheduler* scheduler;
    int* evaluations;
};

TEST(ExecutionTaskEnvironment, G203_ObjectSchedulerTypeCopiesAnLvalueAnswerOnce) {
    identity_scheduler external;
    int evaluations = 0;
    outcome done;
    identity_scheduler::copies = 0;
    {
        auto operation = ex::connect(returns_seven<identity_environment>(),
                                     done.receiver([&] { return lvalue_answer_env{&external, &evaluations}; }));
        ex::start(operation);
    }
    EXPECT_EQ(done.result, 7);
    EXPECT_EQ(done.terminals, 1);
    EXPECT_EQ(evaluations, 1);
    EXPECT_EQ(identity_scheduler::copies, 1);
}

// A cv-qualified object start_scheduler_type. SCHED has that type, so the
// promise's query copies from a volatile scheduler through the copy
// constructor that takes one, and not through the ordinary one.
struct dual_copy_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    dual_copy_scheduler() = default;
    dual_copy_scheduler(const dual_copy_scheduler& other) noexcept
        : from(other.from), first_copy(other.first_copy != 0 ? other.first_copy : 1) {}
    dual_copy_scheduler(const volatile dual_copy_scheduler& other) noexcept
        : from(other.from), first_copy(other.first_copy != 0 ? other.first_copy : 2) {}
    dual_copy_scheduler& operator=(const dual_copy_scheduler&) noexcept = default;

    named_scheduler::sender schedule() const volatile noexcept { return {}; }
    auto query(ex::get_forward_progress_guarantee_t) const volatile noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    friend bool operator==(const volatile dual_copy_scheduler&, const volatile dual_copy_scheduler&) noexcept {
        return true;
    }

    int from = 0;
    // Which copy constructor made the first copy in this object's history:
    // 1 the ordinary one, 2 the one from a volatile object. Later copies
    // keep it. The scheduler in SCHED is no copy at all, so the first copy
    // is the one the promise's query makes from SCHED.
    int first_copy = 0;
};

template<class Alias>
struct qualified_scheduler_environment {
    using start_scheduler_type = Alias;
};

struct dual_copy_env {
    dual_copy_scheduler query(ex::get_start_scheduler_t) const noexcept {
        dual_copy_scheduler made;
        made.from = 9;
        return made;
    }
};

// A sender that asks its receiver's environment for the start scheduler when
// it is started, and completes with no value. The query is a forwarding
// query, so it reaches the promise, whose answer is a copy of SCHED.
template<class Receiver>
struct sched_probe_operation {
    using operation_state_concept = ex::operation_state_tag;

    void start() & noexcept {
        *first_copy = ex::get_start_scheduler(ex::get_env(receiver)).first_copy;
        ex::set_value(std::move(receiver));
    }

    Receiver receiver;
    int* first_copy;
};

struct sched_probe_sender {
    using sender_concept = ex::sender_tag;

    template<class Self, class... Env>
    static consteval auto get_completion_signatures() noexcept {
        return ex::completion_signatures<ex::set_value_t()>{};
    }

    template<ex::receiver Receiver>
    auto connect(Receiver receiver) const -> sched_probe_operation<Receiver> {
        return {std::move(receiver), first_copy};
    }

    int* first_copy;
};

template<class Alias>
ex::task<int, qualified_scheduler_environment<Alias>> reads_how_sched_is_copied() {
    int first_copy = -1;
    co_await sched_probe_sender{&first_copy};
    co_return first_copy;
}

TEST(ExecutionTaskEnvironment, G203_CvQualifiedSchedulerTypeKeepsItsQualificationInSched) {
    const auto copied_as = [](auto task) {
        outcome done;
        {
            auto operation = ex::connect(std::move(task), done.receiver([] { return dual_copy_env{}; }));
            ex::start(operation);
        }
        EXPECT_EQ(done.terminals, 1);
        return done.result.value_or(-1);
    };
    // Each alias copies SCHED as a direct copy from an object of that type would.
    EXPECT_EQ(copied_as(reads_how_sched_is_copied<dual_copy_scheduler>()), 1);
    EXPECT_EQ(copied_as(reads_how_sched_is_copied<const dual_copy_scheduler>()), 1);
    EXPECT_EQ(copied_as(reads_how_sched_is_copied<volatile dual_copy_scheduler>()), 2);
    EXPECT_EQ(copied_as(reads_how_sched_is_copied<const volatile dual_copy_scheduler>()), 2);
}

// The stop token query is reserved for the promise. An Environment that has
// a usable stop token member of its own is never asked through the promise,
// although get_stop_token is a forwarding query.
struct stop_answering_environment {
    using start_scheduler_type = ex::inline_scheduler;

    std::inplace_stop_token query(std::get_stop_token_t) const noexcept {
        ++*calls;
        return source->get_token();
    }

    explicit stop_answering_environment(const ex::env<>&) : calls(&shared_calls), source(&shared_source) {}

    static inline int shared_calls = 0;
    static inline std::inplace_stop_source shared_source;

    int* calls;
    std::inplace_stop_source* source;
};

static_assert(std::forwarding_query(std::get_stop_token));

ex::task<int, stop_answering_environment> asks_for_the_reserved_stop_token() {
    co_return co_await inspect_sender{[](const auto& env) {
        // Whatever the promise answers, it is not the Environment's token,
        // which can stop: the receiver has no stop token at all.
        const auto token = std::get_stop_token(env);
        return token.stop_possible() ? 1 : 0;
    }};
}

TEST(ExecutionTaskEnvironment, G201_StopTokenQueryIsReservedForThePromise) {
    stop_answering_environment::shared_calls = 0;
    outcome done;
    {
        auto operation =
            ex::connect(asks_for_the_reserved_stop_token(), done.receiver([] { return ex::env<>{}; }));
        ex::start(operation);
    }
    EXPECT_EQ(done.result, 0);
    EXPECT_EQ(stop_answering_environment::shared_calls, 0);
    // The positive control: the Environment itself answers, with a token
    // that can stop.
    const stop_answering_environment direct{ex::env<>{}};
    EXPECT_TRUE(std::get_stop_token(direct).stop_possible());
    EXPECT_EQ(stop_answering_environment::shared_calls, 1);
}

} // namespace

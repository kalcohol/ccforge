#include <execution>
#include <forge/static_thread_pool.hpp>
#include <forge/strand.hpp>
#include <forge/task.hpp>
#include <gtest/gtest.h>
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include "../scheduler_fixture.hpp"
namespace ex = std::execution;
namespace {
// As in the scheduler-allocator compile control, this conversion must be
// constant-evaluable even though the enclosing member call is at runtime.
template<class Tag>
struct query_address {
    const Tag* tag;
    consteval query_address(const Tag& query) noexcept : tag(&query) {}
};
template<const auto& Query, class Result>
struct address_env {
    using tag_t = std::remove_cvref_t<decltype(Query)>;
    Result result;
    mutable const tag_t* observed = nullptr;
    mutable int calls = 0;

    const Result& query(query_address<tag_t> address) const noexcept {
        observed = address.tag;
        ++calls;
        return result;
    }
};
struct address_progress_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    const ex::get_forward_progress_guarantee_t** observed;
    auto schedule() const noexcept -> scheduler_fixture::schedule_sender<address_progress_scheduler> {
        return {*this};
    }
    auto query(query_address<ex::get_forward_progress_guarantee_t> address) const noexcept {
        *observed = address.tag;
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const address_progress_scheduler&) const noexcept = default;
};
using error_scheduler_query = ex::get_completion_scheduler_t<ex::set_error_t>;
using value_scheduler_query = ex::get_completion_scheduler_t<ex::set_value_t>;
struct scheduler_query_trace {
    const error_scheduler_query* initial = nullptr;
    const value_scheduler_query* bridge = nullptr;
    const value_scheduler_query* link = nullptr;
    int pack_calls = 0;
    int unary_calls = 0;
    int bridge_calls = 0;
    int link_calls = 0;
};
struct address_link {
    using scheduler_concept = ex::scheduler_tag;
    int identity;
    const address_link* next;
    scheduler_query_trace* trace;

    auto schedule() const noexcept -> scheduler_fixture::schedule_sender<address_link> {
        return {*this};
    }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    const address_link& query(query_address<value_scheduler_query> address,
                              const ex::env<>&, const ex::env<>&) const noexcept {
        trace->link = address.tag;
        ++trace->link_calls;
        return *next;
    }
    bool operator==(const address_link& other) const noexcept { return identity == other.identity; }
};
struct address_bridge {
    using scheduler_concept = ex::scheduler_tag;
    const address_link* first;
    scheduler_query_trace* trace;

    auto schedule() const noexcept -> scheduler_fixture::schedule_sender<address_bridge> {
        return {*this};
    }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    const address_link& query(query_address<value_scheduler_query> address) const noexcept {
        trace->bridge = address.tag;
        ++trace->bridge_calls;
        return *first;
    }
    bool operator==(const address_bridge&) const noexcept = default;
};
struct address_attrs {
    address_bridge bridge;

    address_bridge query(query_address<error_scheduler_query> address,
                         const ex::env<>&, const ex::env<>&) const noexcept {
        bridge.trace->initial = address.tag;
        ++bridge.trace->pack_calls;
        return bridge;
    }
    address_bridge query(query_address<error_scheduler_query> address) const noexcept {
        bridge.trace->initial = address.tag;
        ++bridge.trace->unary_calls;
        return bridge;
    }
};
struct fancy_pointer {
    int* value;
    int& operator*() const noexcept { return *value; }
};
struct fancy_allocator {
    using value_type = int;
    int identity;

    fancy_pointer allocate(std::size_t n) { return {std::allocator<int>{}.allocate(n)}; }
    bool deallocate(fancy_pointer ptr, std::size_t n) noexcept {
        std::allocator<int>{}.deallocate(ptr.value, n);
        return true;
    }
    bool operator==(const fancy_allocator&) const noexcept = default;
};
struct domain {};
struct resource_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    int identity;
    auto schedule() const noexcept { return ex::just(); }
    constexpr auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    auto query(ex::get_completion_scheduler_t<ex::set_value_t>) const noexcept {
        return resource_scheduler{identity < 3 ? identity + 1 : identity};
    }
    template<class E>
    domain query(ex::get_completion_domain_t<ex::set_value_t>, const E&) const noexcept { return {}; }
    bool operator==(const resource_scheduler&) const noexcept = default;
};
struct bridge_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    auto schedule() const noexcept { return ex::just(); }
    constexpr auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    auto query(ex::get_completion_scheduler_t<ex::set_value_t>) const noexcept {
        return resource_scheduler{0};
    }
    bool operator==(const bridge_scheduler&) const noexcept = default;
};
struct attrs {
    bridge_scheduler query(ex::get_completion_scheduler_t<ex::set_error_t>) const noexcept { return {}; }
};
struct borrowed_attrs {
    ex::inline_scheduler value;
    const ex::inline_scheduler& query(ex::get_completion_scheduler_t<ex::set_value_t>) const noexcept {
        return value;
    }
};
template<class Tag, class S>
void check_parity(const S& scheduler) {
    auto attributes = ex::get_env(ex::schedule(scheduler));
    EXPECT_EQ(ex::get_completion_scheduler<Tag>(scheduler), scheduler);
    EXPECT_EQ(ex::get_completion_scheduler<Tag>(attributes), scheduler);
    EXPECT_EQ(ex::get_completion_scheduler<Tag>(scheduler, ex::env<>{}),
              ex::get_completion_scheduler<Tag>(attributes, ex::env<>{}));
    EXPECT_EQ(ex::get_completion_scheduler<Tag>(scheduler, ex::env<>{}, ex::env<>{}),
              ex::get_completion_scheduler<Tag>(attributes, ex::env<>{}, ex::env<>{}));
    static_assert(std::same_as<decltype(ex::get_completion_domain<Tag>(attributes, ex::env<>{})),
                               decltype(ex::get_completion_domain<Tag>(scheduler, ex::env<>{}))>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<Tag>(attributes, ex::env<>{}, ex::env<>{})),
                               decltype(ex::get_completion_domain<Tag>(scheduler, ex::env<>{}, ex::env<>{}))>);
    EXPECT_EQ(ex::get_forward_progress_guarantee(scheduler),
              scheduler.query(ex::get_forward_progress_guarantee));
}
}
TEST(QueryConformanceTest, NamedQueriesPassGlobalTagsToRuntimeMembers) {
    const address_env<std::get_allocator, std::allocator<int>> allocator{};
    const auto allocator_copy = std::get_allocator;
    EXPECT_EQ(&allocator_copy(allocator), &allocator.result);
    EXPECT_EQ(allocator.observed, &std::get_allocator);

    const address_env<std::get_stop_token, std::never_stop_token> token{};
    EXPECT_EQ(&std::get_stop_token(token), &token.result);
    EXPECT_EQ(token.observed, &std::get_stop_token);
    static_assert(std::same_as<decltype(std::get_stop_token(ex::env<>{})), std::never_stop_token>);

    const address_env<ex::get_start_scheduler, ex::inline_scheduler> start{};
    EXPECT_EQ(&ex::get_start_scheduler(start), &start.result);
    EXPECT_EQ(start.observed, &ex::get_start_scheduler);

    const address_env<ex::get_delegation_scheduler, ex::inline_scheduler> delegation{};
    EXPECT_EQ(&ex::get_delegation_scheduler(delegation), &delegation.result);
    EXPECT_EQ(delegation.observed, &ex::get_delegation_scheduler);

    const address_env<ex::get_scheduler, ex::inline_scheduler> current{};
    EXPECT_EQ(ex::get_scheduler(current), current.result);
    EXPECT_EQ(current.observed, &ex::get_scheduler);
    EXPECT_EQ(current.calls, 1);

    const ex::get_forward_progress_guarantee_t* progress_tag = nullptr;
    const address_progress_scheduler progress{&progress_tag};
    EXPECT_EQ(ex::get_forward_progress_guarantee(progress), ex::forward_progress_guarantee::weakly_parallel);
    EXPECT_EQ(progress_tag, &ex::get_forward_progress_guarantee);

    const address_env<ex::get_await_completion_adaptor, int> await_env{42};
    EXPECT_EQ(&ex::get_await_completion_adaptor(await_env), &await_env.result);
    EXPECT_EQ(await_env.observed, &ex::get_await_completion_adaptor);
}
TEST(QueryConformanceTest, CompletionSchedulerPassesGlobalTagsAtEveryHop) {
    scheduler_query_trace trace;
    const address_link equal{2, &equal, &trace};
    const address_link second{2, &equal, &trace};
    const address_link first{1, &second, &trace};
    const address_attrs attrs{{&first, &trace}};
    const auto copied_query = ex::get_completion_scheduler<ex::set_error_t>;
    int evaluations = 0;
    auto environment = [&] { ++evaluations; return ex::env<>{}; };

    // The initial pack wins, the bridge retries unary and changes type, and
    // an equal next scheduler must leave the borrowed answer at "second".
    decltype(auto) packed = copied_query(attrs, environment(), environment());
    static_assert(std::same_as<decltype(packed), const address_link&>);
    EXPECT_EQ(&packed, &second);
    EXPECT_EQ(trace.initial, &ex::get_completion_scheduler<ex::set_error_t>);
    EXPECT_EQ(trace.bridge, &ex::get_completion_scheduler<ex::set_value_t>);
    EXPECT_EQ(trace.link, &ex::get_completion_scheduler<ex::set_value_t>);
    EXPECT_EQ(trace.pack_calls, 1);
    EXPECT_EQ(trace.unary_calls, 0);
    EXPECT_EQ(trace.bridge_calls, 1);
    EXPECT_EQ(trace.link_calls, 2);
    EXPECT_EQ(evaluations, 2);

    trace = {};
    decltype(auto) unary = copied_query(attrs, environment());
    static_assert(std::same_as<decltype(unary), const address_link&>);
    EXPECT_EQ(&unary, &first);
    EXPECT_EQ(trace.initial, &ex::get_completion_scheduler<ex::set_error_t>);
    EXPECT_EQ(trace.bridge, &ex::get_completion_scheduler<ex::set_value_t>);
    EXPECT_EQ(trace.pack_calls, 0);
    EXPECT_EQ(trace.unary_calls, 1);
    EXPECT_EQ(trace.bridge_calls, 1);
    EXPECT_EQ(trace.link_calls, 0);
    EXPECT_EQ(evaluations, 3);

    EXPECT_EQ(copied_query(ex::inline_scheduler{}, environment()), ex::inline_scheduler{});
    EXPECT_EQ(evaluations, 4);
}
TEST(QueryConformanceTest, PropMovesItsMoveOnlyValue) {
    struct value_tag {};
    auto property = ex::prop(value_tag{}, std::make_unique<int>(39));
    auto moved = std::move(property);
    EXPECT_FALSE(property.query(value_tag{}));
    ASSERT_TRUE(moved.query(value_tag{}));
    EXPECT_EQ(*moved.query(value_tag{}), 39);
}
TEST(QueryConformanceTest, FancyAllocatorWorksForBothQueries) {
    const fancy_allocator alloc{7};
    const auto ordinary = ex::prop{std::get_allocator, std::cref(alloc)};
    const auto preferred = ex::prop{std::get_scheduler_allocator, std::cref(alloc)};
    static_assert(std::same_as<decltype(std::get_allocator(ordinary)), const fancy_allocator&>);
    static_assert(std::same_as<decltype(std::get_scheduler_allocator(preferred)), const fancy_allocator&>);
    EXPECT_EQ(&std::get_allocator(ordinary), &alloc);
    EXPECT_EQ(&std::get_scheduler_allocator(ordinary), &alloc);
    EXPECT_EQ(&std::get_scheduler_allocator(preferred), &alloc);
}
TEST(QueryConformanceTest, SchedulerAllocatorForwardsThroughThen) {
    const fancy_allocator scheduler{9};
    auto overlay = ex::env{ex::prop{std::get_allocator, std::allocator<int>{}},
                           ex::prop{std::get_scheduler_allocator, std::cref(scheduler)}};
    auto result = std::this_thread::sync_wait(ex::write_env(
        ex::then(ex::read_env(std::get_scheduler_allocator),
                 [&scheduler](const fancy_allocator& alloc) noexcept { return &alloc == &scheduler; }),
        overlay));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result));
}
TEST(QueryConformanceTest, CompletionSchedulerResolvesTypeAndValueChains) {
    int evaluations = 0;
    auto environment = [&] { ++evaluations; return ex::env<>{}; };
    auto resolved = ex::get_completion_scheduler<ex::set_error_t>(attrs{}, environment());
    static_assert(std::same_as<decltype(resolved), resource_scheduler>);
    EXPECT_EQ(resolved.identity, 3);
    EXPECT_EQ(evaluations, 1);
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_error_t>(attrs{}).identity, 3);
    static_assert(std::same_as<decltype(ex::get_completion_domain<ex::set_error_t>(attrs{}, ex::env<>{})), domain>);
    auto e = ex::prop{ex::get_scheduler, bridge_scheduler{}};
    EXPECT_EQ(ex::get_scheduler(e).identity, 3);
}
TEST(QueryConformanceTest, TerminalBorrowedAnswerRetainsIdentity) {
    borrowed_attrs a;
    const auto& answer = ex::get_completion_scheduler<ex::set_value_t>(a);
    EXPECT_EQ(&answer, &a.value);
}
TEST(QueryConformanceTest, InlineValueParityIncludesSchedulerlessEnvironments) {
    ex::inline_scheduler scheduler;
    auto attributes = ex::get_env(ex::schedule(scheduler));
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(scheduler, ex::env<>{}), scheduler);
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(attributes, ex::env<>{}), scheduler);
    ex::run_loop loop;
    auto e = ex::prop{ex::get_scheduler, loop.get_scheduler()};
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(scheduler, e), loop.get_scheduler());
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(attributes, e), loop.get_scheduler());
}
TEST(QueryConformanceTest, ForgeSchedulersPreserveResourceIdentity) {
    forge::static_thread_pool first{1};
    forge::static_thread_pool second{1};
    auto scheduler = first.get_scheduler();
    check_parity<ex::set_value_t>(scheduler);
    check_parity<ex::set_stopped_t>(scheduler);
    EXPECT_NE(scheduler, second.get_scheduler());
    forge::strand serial{scheduler};
    check_parity<ex::set_value_t>(serial.get_scheduler());
    check_parity<ex::set_stopped_t>(serial.get_scheduler());
    forge::any_scheduler erased{scheduler};
    check_parity<ex::set_value_t>(erased);
    check_parity<ex::set_stopped_t>(erased);
    check_parity<ex::set_error_t>(erased);
    EXPECT_NE(erased, forge::any_scheduler{scheduler});
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
    check_parity<ex::set_value_t>(forge::__task_detail::__inline_fallback_scheduler{});
#endif
}

namespace {
struct domain_a {};
struct domain_b {};
struct indeterminate_sender {
    using sender_concept = ex::sender_tag;
    struct attributes {
        template<class Tag, class... Es>
        ex::indeterminate_domain<domain_a, domain_b>
        query(ex::get_completion_domain_t<Tag>, Es&&...) const noexcept { return {}; }
    };
    attributes get_env() const noexcept { return {}; }
    template<class Self, class E>
    static auto get_completion_signatures() -> ex::completion_signatures<ex::set_value_t(int)> { return {}; }
    template<class R>
    struct operation : ex::__forge_detail::__immovable {
        using operation_state_concept = ex::operation_state_tag;
        R receiver;
        explicit operation(R r) : receiver(std::move(r)) {}
        void start() & noexcept { ex::set_value(std::move(receiver), 42); }
    };
    template<ex::receiver R>
    auto connect(R r) const -> operation<R> { return operation<R>{std::move(r)}; }
};
}
TEST(QueryConformanceTest, IndeterminateDomainUsesDefaultTransformAtConnect) {
    auto transformed = ex::transform_sender(indeterminate_sender{}, ex::env<>{});
    static_assert(std::same_as<decltype(transformed), indeterminate_sender>);
    auto result = std::this_thread::sync_wait(std::move(transformed));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

namespace {
struct hidden_domain {
    static inline int transforms = 0;
    template<ex::sender S, std::destructible E>
    static auto transform_sender(ex::start_t, S&& sender, const E&) noexcept -> S {
        ++transforms;
        return static_cast<S&&>(sender);
    }
};
struct hidden_domain_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    auto schedule() const noexcept { return ex::just(); }
    constexpr auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    template<class E>
        requires (!requires(const E& e) { ex::get_scheduler(e); })
    hidden_domain query(ex::get_completion_domain_t<ex::set_value_t>, const E&) const noexcept { return {}; }
    bool operator==(const hidden_domain_scheduler&) const noexcept = default;
};
struct hidden_domain_env {
    hidden_domain_scheduler query(ex::get_scheduler_t) const noexcept { return {}; }
};
struct hidden_domain_receiver {
    using receiver_concept = ex::receiver_tag;
    int* value;
    hidden_domain_env get_env() const noexcept { return {}; }
    void set_value(int n) && noexcept { *value = n; }
    void set_error(std::exception_ptr) && noexcept { *value = -1; }
    void set_stopped() && noexcept { *value = -2; }
};
}
TEST(QueryConformanceTest, HiddenSchedulerEnvironmentSelectsTheRuntimeStartDomain) {
    static_assert(std::same_as<decltype(ex::get_domain(hidden_domain_env{})), hidden_domain>);
    hidden_domain::transforms = 0;
    int value = 0;
    auto operation = ex::connect(ex::just(42), hidden_domain_receiver{&value});
    ex::start(operation);
    EXPECT_EQ(value, 42);
    EXPECT_GT(hidden_domain::transforms, 0);
}
namespace {
// Scheduler copy/move operations never throw, but their declarations need not
// promise noexcept. Only the specified query and comparison expressions do.
template<bool Recurse>
struct stored_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    int identity;
    explicit stored_scheduler(int n = 0) noexcept : identity(n) {}
    stored_scheduler(const stored_scheduler& other) : identity(other.identity) {}
    stored_scheduler(stored_scheduler&& other) : identity(other.identity) {}
    stored_scheduler& operator=(const stored_scheduler& other) {
        identity = other.identity;
        return *this;
    }
    stored_scheduler& operator=(stored_scheduler&& other) {
        identity = other.identity;
        return *this;
    }
    auto schedule() const noexcept { return ex::just(); }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    auto query(ex::get_completion_scheduler_t<ex::set_value_t>) const noexcept
        -> stored_scheduler requires Recurse {
        return stored_scheduler{identity < 3 ? identity + 1 : identity};
    }
    bool operator==(const stored_scheduler&) const noexcept = default;
};
template<bool Recurse>
struct stored_attrs {
    stored_scheduler<Recurse> query(ex::get_completion_scheduler_t<ex::set_value_t>) const noexcept {
        return stored_scheduler<Recurse>{0};
    }
};
static_assert(ex::scheduler<stored_scheduler<false>> && ex::scheduler<stored_scheduler<true>>);
static_assert(!std::is_nothrow_move_constructible_v<stored_scheduler<false>>);
static_assert(!std::is_nothrow_copy_constructible_v<stored_scheduler<false>>);
static_assert(!std::is_nothrow_move_assignable_v<stored_scheduler<true>>);

struct category_query {
    bool fail = false;
    long operator()(ex::env<>&) && noexcept { return -10; }
    int operator()(ex::env<>&&) & {
        if (fail) throw 7;
        return 42;
    }
    short operator()(ex::env<>&) & noexcept { return 7; }
};
struct reverse_category_query {
    long operator()(ex::env<>&) && { return -10; }
    int operator()(ex::env<>&&) & noexcept { return 24; }
};
template<bool Borrowed>
struct category_receiver {
    using receiver_concept = ex::receiver_tag;
    ex::env<>* environment;
    int* value;
    bool* error;
    decltype(auto) get_env() const noexcept {
        if constexpr (Borrowed) return (*environment);
        else return ex::env<>{};
    }
    void set_value(std::conditional_t<Borrowed, short, int> n) && noexcept { *value = n; }
    void set_error(std::exception_ptr exception) && noexcept {
        try { std::rethrow_exception(exception); }
        catch (int n) { *error = n == 7; }
        catch (...) { *error = false; }
    }
};
}
TEST(QueryConformanceTest, SchedulerStorageDoesNotAddNothrowMandates) {
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(stored_attrs<false>{}).identity, 0);
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(stored_attrs<true>{}).identity, 3);
    stored_scheduler<false> terminal{9};
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_error_t>(terminal, ex::env<>{}).identity, 9);
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_error_t>(stored_scheduler<false>{8}, ex::env<>{}).identity, 8);
    auto environment = ex::prop{ex::get_scheduler, terminal};
    EXPECT_EQ(ex::get_scheduler(environment).identity, 9);
}
TEST(QueryConformanceTest, ReadEnvSignaturesFollowTheExecutedExpression) {
    using sender = decltype(ex::read_env(category_query{}));
    static_assert(std::same_as<ex::completion_signatures_of_t<sender, ex::env<>>,
        ex::completion_signatures<ex::set_value_t(int), ex::set_error_t(std::exception_ptr)>>);
    static_assert(std::same_as<ex::completion_signatures_of_t<sender, ex::env<>&>,
        ex::completion_signatures<ex::set_value_t(short)>>);
    static_assert(std::same_as<ex::completion_signatures_of_t<
        decltype(ex::read_env(reverse_category_query{})), ex::env<>>,
        ex::completion_signatures<ex::set_value_t(int)>>);
    ex::env<> environment;
    int value = 0;
    bool error = false;
    auto value_op = ex::connect(ex::read_env(category_query{}), category_receiver<false>{&environment, &value, &error});
    ex::start(value_op);
    EXPECT_EQ(value, 42);
    EXPECT_FALSE(error);
    auto borrowed_op = ex::connect(ex::read_env(category_query{}), category_receiver<true>{&environment, &value, &error});
    ex::start(borrowed_op);
    EXPECT_EQ(value, 7);
    EXPECT_FALSE(error);
    auto error_op = ex::connect(ex::read_env(category_query{true}), category_receiver<false>{&environment, &value, &error});
    ex::start(error_op);
    EXPECT_TRUE(error);
    auto reverse_op = ex::connect(ex::read_env(reverse_category_query{}), category_receiver<false>{&environment, &value, &error});
    ex::start(reverse_op);
    EXPECT_EQ(value, 24);
}
TEST(QueryConformanceTest, IndeterminateMergeOrdersHaveTheSameTransformBehavior) {
    using forward = std::common_type_t<ex::indeterminate_domain<domain_a>, ex::indeterminate_domain<domain_b, domain_a>>;
    using reverse = std::common_type_t<ex::indeterminate_domain<domain_b, domain_a>, ex::indeterminate_domain<domain_a>>;
    auto first = forward::transform_sender(ex::start, ex::just(42), ex::env<>{});
    auto second = reverse::transform_sender(ex::start, ex::just(42), ex::env<>{});
    static_assert(std::same_as<decltype(first), decltype(second)>);
    EXPECT_EQ(std::this_thread::sync_wait(std::move(first)), std::this_thread::sync_wait(std::move(second)));
}
namespace {
struct on_receiver_key {};
struct on_environment {
    int* queries;
    int query(on_receiver_key) const noexcept {
        ++*queries;
        return 5;
    }
};
struct on_child {
    using sender_concept = ex::sender_tag;
    struct attributes {
        // This key is deliberately nonforwarding: on must use the receiver
        // environment itself rather than a FWD-ENV view of it.
        template<class E>
            requires requires(const E& e) { e.query(on_receiver_key{}); }
        stored_scheduler<false> query(ex::get_completion_scheduler_t<ex::set_value_t>, const E& e) const noexcept {
            return stored_scheduler<false>{e.query(on_receiver_key{})};
        }
    };
    attributes get_env() const noexcept { return {}; }
    template<class Self, class E>
    static auto get_completion_signatures() -> ex::completion_signatures<ex::set_value_t(int)> { return {}; }
    template<ex::receiver R>
    auto connect(R r) const { return ex::connect(ex::just(42), std::move(r)); }
};
template<class E>
struct on_test_receiver {
    using receiver_concept = ex::receiver_tag;
    E environment;
    int* value;
    const E& get_env() const noexcept { return environment; }
    void set_value(int n) && noexcept { *value = n; }
    void set_error(std::exception_ptr) && noexcept { *value = -1; }
    void set_stopped() && noexcept { *value = -2; }
};
}
template<class S, class R>
concept on_connectable = requires(S&& source, R&& receiver) {
    ex::connect(static_cast<S&&>(source), static_cast<R&&>(receiver));
};
TEST(QueryConformanceTest, OnClosureUsesTheReceiverEnvironmentForTheOriginalScheduler) {
    int queries = 0;
    int value = 0;
    auto sender = ex::on(on_child{}, ex::inline_scheduler{}, ex::then([](int n) noexcept { return n; }));
    using sender_t = decltype(sender);
    using receiver_t = on_test_receiver<on_environment>;
    using missing_receiver_t = on_test_receiver<ex::env<>>;
    using filtered_environment = ex::__forge_env_detail::__fwd_env_view<on_environment>;
    using filtered_receiver_t = on_test_receiver<filtered_environment>;
    static_assert(ex::sender_in<sender_t, on_environment>);
    static_assert(!ex::sender_in<sender_t, ex::env<>>);
    static_assert(!ex::sender_in<sender_t, filtered_environment>);
    static_assert(on_connectable<sender_t, receiver_t>);
    static_assert(!on_connectable<sender_t, missing_receiver_t>);
    static_assert(!on_connectable<sender_t, filtered_receiver_t>);
    static_assert(!on_connectable<sender_t&, missing_receiver_t>);
    static_assert(!on_connectable<const sender_t&, missing_receiver_t>);
    using signatures = ex::completion_signatures_of_t<sender_t, on_environment>;
    static_assert(std::same_as<signatures, ex::completion_signatures<ex::set_value_t(int)>>);
    EXPECT_EQ(queries, 0);
    auto operation = ex::connect(std::move(sender), receiver_t{{&queries}, &value});
    EXPECT_EQ(queries, 1);
    ex::start(operation);
    EXPECT_EQ(value, 42);
}

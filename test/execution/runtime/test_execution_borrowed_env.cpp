#include "test_bulk.hpp"
#include <gtest/gtest.h>
#include <execution>
#include <concepts>
#include <algorithm>
#include <exception>
#include <coroutine>
#include <memory>
#include <optional>
#include <type_traits>
#include <variant>
#include <tuple>
#include <utility>
#include <vector>

namespace {
namespace ex = std::execution;

struct shape_query : std::forwarding_query_t {
    shape_query() = delete;
    explicit constexpr shape_query(int) noexcept {}

    template<class Env>
    constexpr auto operator()(const Env& env) const noexcept(noexcept(env.query(*this)))
        -> decltype(env.query(*this)) {
        return env.query(*this);
    }
};

struct borrowed_env {
    std::inplace_stop_token token;
    explicit borrowed_env(std::inplace_stop_token t) : token(t) {}
    borrowed_env(const borrowed_env&) = delete;
    auto query(std::get_stop_token_t) const noexcept { return token; }
    auto query(shape_query) const noexcept -> bool { return true; }
};

struct owned_env {
    std::inplace_stop_token token;
    auto query(std::get_stop_token_t) const noexcept { return token; }
};

template<class Token, class StartScheduler, bool HasShapeQuery>
struct environment_shape {};

template<class Env, class = void>
struct start_scheduler_answer { using type = void; };

template<class Env>
struct start_scheduler_answer<Env, std::void_t<decltype(
    std::declval<const Env&>().query(ex::get_start_scheduler))>> {
    using type = decltype(std::declval<const Env&>().query(ex::get_start_scheduler));
};

// [exec.let]/9.4 forwards shape_query, but /8.2 requires a default instance.
template<class Env>
concept has_shape_query = requires(const Env& env) { shape_query{0}(env); };

template<class Env>
using child_environment_shape = environment_shape<
    decltype(std::declval<const Env&>().query(std::get_stop_token)),
    typename start_scheduler_answer<Env>::type, has_shape_query<Env>>;

// Carries the query observations that separate [exec.let]/9.4 from /8.2.
struct shape_sender {
    using sender_concept = ex::sender_tag;
    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(child_environment_shape<Env>)> { return {}; }
    auto get_env() const noexcept -> ex::env<> { return {}; }
    template<class R>
    struct operation {
        using operation_state_concept = ex::operation_state_tag;
        R receiver;
        void start() & noexcept {
            using env = ex::env_of_t<R>;
            ex::set_value(std::move(receiver), child_environment_shape<env>{});
        }
    };
    template<class R>
    auto connect(R receiver) const -> operation<R> { return {std::move(receiver)}; }
};

template<class Env>
struct environment_type {};

// Carries the environment type itself: the signature side names the supplied
// Env unmodified, as the base oracle did. The sender and the environment type
// determine the completions, never the receiver type ([exec.async.ops]/13),
// and they cover what start() delivers ([exec.snd.expos]/47.1,
// [exec.getcomplsigs]/5).
struct type_sender {
    using sender_concept = ex::sender_tag;
    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(environment_type<Env>)> { return {}; }
    auto get_env() const noexcept -> ex::env<> { return {}; }
    template<class R>
    struct operation {
        using operation_state_concept = ex::operation_state_tag;
        R receiver;
        void start() & noexcept {
            using env = std::remove_cvref_t<ex::env_of_t<R>>;
            ex::set_value(std::move(receiver), environment_type<env>{});
        }
    };
    template<class R>
    auto connect(R receiver) const -> operation<R> { return {std::move(receiver)}; }
};

struct pass_value {
    template<class Value>
    auto operator()(Value value) const noexcept -> Value { return value; }
};
struct ignore_arguments {
    template<class... Args>
    void operator()(Args&&...) const noexcept {}
};
struct replace_with_int {
    template<class... Args>
    auto operator()(Args&&...) const noexcept -> int { return 42; }
};

template<class Value>
struct exact_shape_receiver {
    using receiver_concept = ex::receiver_tag;
    const borrowed_env* env;
    bool* completed;
    auto get_env() const noexcept -> const borrowed_env& { return *env; }
    void set_value(Value) && noexcept { *completed = true; }
    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}
};

// A second receiver type with the same environment type.
template<class Value>
struct other_exact_receiver {
    using receiver_concept = ex::receiver_tag;
    const borrowed_env* env;
    bool* completed;
    int unrelated = 0;
    auto get_env() const noexcept -> const borrowed_env& { return *env; }
    void set_value(Value) && noexcept { *completed = true; }
    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}
};

// The same check for a stable environment returned as an rvalue reference.
template<class Value>
struct exact_rvalue_receiver {
    using receiver_concept = ex::receiver_tag;
    const borrowed_env* env;
    bool* completed;
    auto get_env() const noexcept -> const borrowed_env&& { return std::move(*env); }
    void set_value(Value) && noexcept { *completed = true; }
    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}
};

// The same check for an environment returned by value.
template<class Value>
struct exact_value_receiver {
    using receiver_concept = ex::receiver_tag;
    std::inplace_stop_token token;
    bool* completed;
    auto get_env() const noexcept -> owned_env { return {token}; }
    void set_value(Value) && noexcept { *completed = true; }
    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}
};

template<class S, class Env>
using advertised_value = std::tuple_element_t<0, std::variant_alternative_t<0,
    ex::value_types_of_t<S, Env, std::tuple, std::variant>>>;

template<class StartScheduler = void, class S>
void check_advertised_shape(S sender) {
    std::inplace_stop_source source;
    borrowed_env env{source.get_token()};
    using value = advertised_value<S, const borrowed_env&>;
    static_assert(has_shape_query<const borrowed_env&>);
    static_assert(std::same_as<value, environment_shape<std::inplace_stop_token, StartScheduler, false>>);
    bool completed = false;
    auto op = ex::connect(std::move(sender), exact_shape_receiver<value>{&env, &completed});
    ex::start(op);
    EXPECT_TRUE(completed);
}

// The successor or child environment type depends on the cv-unqualified
// environment type alone: a borrowed const lvalue, a borrowed rvalue and the
// object type itself name the same completions, whichever category the
// sender is connected in.
template<class S>
void check_advertised_type(S sender) {
    std::inplace_stop_source source;
    borrowed_env env{source.get_token()};
    using value = advertised_value<S, const borrowed_env&>;
    static_assert(std::same_as<value, advertised_value<S, const borrowed_env&&>>);
    static_assert(std::same_as<value, advertised_value<S, borrowed_env>>);
    static_assert(std::same_as<value, advertised_value<S, borrowed_env&>>);
    static_assert(ex::sender_in<S, const borrowed_env&&> && !ex::sends_stopped<S, const borrowed_env&&>);
    bool completed = false;
    auto first = ex::connect(sender, exact_shape_receiver<value>{&env, &completed});
    ex::start(first);
    EXPECT_TRUE(completed);
    completed = false;
    auto second = ex::connect(sender, other_exact_receiver<value>{&env, &completed});
    ex::start(second);
    EXPECT_TRUE(completed);
    completed = false;
    auto rvalue_env = ex::connect(sender, exact_rvalue_receiver<value>{&env, &completed});
    ex::start(rvalue_env);
    EXPECT_TRUE(completed);
    // An rvalue sender with a borrowed environment, as the base oracle ran.
    completed = false;
    auto rvalue_sender = ex::connect(S(sender), exact_shape_receiver<value>{&env, &completed});
    ex::start(rvalue_sender);
    EXPECT_TRUE(completed);

    using owned_value = advertised_value<S, owned_env>;
    static_assert(!std::same_as<value, owned_value>);
    completed = false;
    auto owned = ex::connect(std::move(sender),
        exact_value_receiver<owned_value>{source.get_token(), &completed});
    ex::start(owned);
    EXPECT_TRUE(completed);
}

// A sender that is not part of the library and wraps another one. Its static
// member receives the cv-unqualified environment type, like every sender's.
template<class Inner>
struct user_wrapper {
    using sender_concept = ex::sender_tag;
    Inner inner;
    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> ex::completion_signatures_of_t<Inner, Env> { return {}; }
    auto get_env() const noexcept -> ex::env<> { return {}; }
    template<ex::receiver R>
    auto connect(R receiver) const -> ex::connect_result_t<const Inner&, R> {
        return ex::connect(inner, std::move(receiver));
    }
};
template<class Inner>
user_wrapper(Inner) -> user_wrapper<Inner>;

template<class Env, bool Borrow>
struct env_receiver {
    using receiver_concept = ex::receiver_tag;
    const Env* env;
    int* result;
    decltype(auto) get_env() const noexcept {
        if constexpr (Borrow) return (*env);
        else return Env{*env};
    }
    void set_value(int value) && noexcept { *result = value; }
    void set_error(std::exception_ptr) && noexcept { *result = -1; }
    void set_stopped() && noexcept { *result = -2; }
};

template<class S>
void check_environment(S sender, const borrowed_env& env) {
    int result = 0;
    auto op = ex::connect(std::move(sender), env_receiver<borrowed_env, true>{&env, &result});
    ex::start(op);
    EXPECT_EQ(result, 42);
}

template<class S>
void check_environment(S sender, const owned_env& env) {
    int result = 0;
    auto receiver = env_receiver<owned_env, false>{&env, &result};
    auto op = ex::connect(std::move(sender), std::move(receiver));
    ex::start(op);
    EXPECT_EQ(result, 42);
}

auto inspect_environment(std::inplace_stop_token expected) {
    return ex::read_env(std::get_stop_token) | ex::then([expected](auto token) {
        return token == expected ? 42 : -3;
    });
}

// q(env, args...) is expression-equivalent to env.query(q, args...) wherever
// that member call is well-formed ([exec.queryable.general]/5), so the call operator
// keeps the category of the environment it is given. Every category has an
// answer equal to the const one ([exec.queryable.general]/2); the category-specific
// members differ from it in the reference type or the exception
// specification. A forwarding view queries the environment as a const
// lvalue, so the const answers are the ones a child sees, for a borrowed and
// for an owned environment alike.
template<bool Lvalue>
struct raw_query : std::forwarding_query_t {
    template<class Env, class Arg>
    auto operator()(Env&& env, Arg&& arg) const
        noexcept(noexcept(std::forward<Env>(env).query(*this, std::forward<Arg>(arg))))
        -> decltype(std::forward<Env>(env).query(*this, std::forward<Arg>(arg))) {
        return std::forward<Env>(env).query(*this, std::forward<Arg>(arg));
    }
};

// A unary query for write_env, whose joined environment has unary lookup
// only ([exec.write.env]/4).
struct unary_query : std::forwarding_query_t {
    template<class Env>
    auto operator()(Env&& env) const noexcept(noexcept(std::forward<Env>(env).query(*this)))
        -> decltype(std::forward<Env>(env).query(*this)) {
        return std::forward<Env>(env).query(*this);
    }
};

struct ref_qualified_env {
    int value = 42;
    auto query(unary_query) & noexcept -> int& { return value; }
    auto query(unary_query) const& noexcept -> const int& { return value; }
    auto query(raw_query<true>, int&) & noexcept -> int& { return value; }
    auto query(raw_query<true>, int&) const& noexcept -> const int& { return value; }
    auto query(raw_query<false>, int&&) && noexcept(false) -> long { return value; }
    auto query(raw_query<false>, int&&) const& noexcept -> long { return value; }
    auto query(ex::get_start_scheduler_t) const noexcept -> ex::inline_scheduler { return {}; }
};

template<class Env>
using lvalue_answer = decltype(raw_query<true>{}(std::declval<Env>(), std::declval<int&>()));
template<class Env>
using rvalue_answer = decltype(raw_query<false>{}(std::declval<Env>(), 0));
static_assert(std::same_as<lvalue_answer<ref_qualified_env&>, int&>);
static_assert(std::same_as<lvalue_answer<const ref_qualified_env&>, const int&>);
static_assert(std::same_as<lvalue_answer<ref_qualified_env>, const int&>);
static_assert(std::same_as<rvalue_answer<ref_qualified_env>, long> &&
              std::same_as<rvalue_answer<ref_qualified_env&>, long> &&
              std::same_as<rvalue_answer<const ref_qualified_env&>, long>);
static_assert(!noexcept(raw_query<false>{}(std::declval<ref_qualified_env>(), 0)));
static_assert(noexcept(raw_query<false>{}(std::declval<ref_qualified_env&>(), 0)));
static_assert(noexcept(raw_query<false>{}(std::declval<const ref_qualified_env&>(), 0)));

// The const answers, with their reference type and exception specification.
template<class Env>
concept has_const_lvalue_query = requires(const Env& env, int& arg) {
    { env.query(raw_query<true>{}, arg) } noexcept -> std::same_as<const int&>;
};

template<class Env>
concept has_const_rvalue_query = requires(const Env& env) {
    { env.query(raw_query<false>{}, 0) } noexcept -> std::same_as<long>;
};

template<class Env>
concept has_const_unary_query = requires(const Env& env) {
    { env.query(unary_query{}) } noexcept -> std::same_as<const int&>;
};

// Reports where the const lvalue answer lives.
struct inspect_const_reference {
    template<class Env>
        requires has_const_lvalue_query<Env> && has_const_rvalue_query<Env>
    auto operator()(const Env& env) const noexcept -> const int* {
        int arg = 0;
        return std::addressof(raw_query<true>{}(env, arg));
    }
};

struct inspect_const_value {
    template<class Env>
        requires has_const_lvalue_query<Env> && has_const_rvalue_query<Env>
    auto operator()(const Env& env) const noexcept -> long {
        int arg = 0;
        return raw_query<true>{}(env, arg) + raw_query<false>{}(env, 0);
    }
};

struct inspect_const_unary_reference {
    template<has_const_unary_query Env>
    auto operator()(const Env& env) const noexcept -> const int* {
        return std::addressof(unary_query{}(env));
    }
};

template<class Value, class Env>
struct category_receiver {
    using receiver_concept = ex::receiver_tag;
    std::remove_reference_t<Env>* env;
    std::optional<Value>* result;
    auto get_env() const noexcept -> Env { return static_cast<Env>(*env); }
    void set_value(Value value) && noexcept { result->emplace(std::move(value)); }
    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}
};

// Checks the public alias, the constant-evaluated static member that the
// signature protocol of [exec.getcomplsigs]/1 names, and the delivered value.
template<class Value, class Receiver, class Sender, class... ReceiverArgs>
auto run_category_child(Sender sender, ReceiverArgs&&... args) {
    using environment = ex::env_of_t<Receiver>;
    using expected = ex::completion_signatures<ex::set_value_t(Value)>;
    static_assert(std::same_as<ex::completion_signatures_of_t<Sender, environment>, expected>);
    constexpr auto signatures = Sender::template get_completion_signatures<Sender, environment>();
    static_assert(std::same_as<decltype(signatures), const expected>);
    std::optional<Value> result;
    auto op = ex::connect(std::move(sender), Receiver{std::forward<ReceiverArgs>(args)..., &result});
    ex::start(op);
    return result;
}

template<class Adaptor>
void check_ref_qualified_members(Adaptor adapt, ref_qualified_env& env) {
    // A borrowed environment answers from the receiver's own object.
    using lvalue_receiver = category_receiver<const int*, ref_qualified_env&>;
    auto lvalue = run_category_child<const int*, lvalue_receiver>(
        adapt(ex::read_env(inspect_const_reference{})), &env);
    EXPECT_EQ(lvalue, std::addressof(env.value));
    using rvalue_reference_receiver = category_receiver<const int*, ref_qualified_env&&>;
    auto rvalue_reference = run_category_child<const int*, rvalue_reference_receiver>(
        adapt(ex::read_env(inspect_const_reference{})), &env);
    EXPECT_EQ(rvalue_reference, std::addressof(env.value));
    // An environment returned by value is owned by the adaptor's operation
    // state; the view points at that object.
    using value_receiver = category_receiver<long, ref_qualified_env>;
    auto value = run_category_child<long, value_receiver>(
        adapt(ex::read_env(inspect_const_value{})), &env);
    EXPECT_EQ(value, 84L);
}

template<class Adaptor>
void check_ref_qualified_environment(Adaptor adapt) {
    ref_qualified_env env;
    check_ref_qualified_members(adapt, env);
}

// [exec.queryable.general]/6: a query result is valid while the queryable object is.
// The value registers its lifetime so that the check reads no dead object.
struct tracked_value {
    static inline std::vector<const tracked_value*> live;
    int value;
    explicit tracked_value(int v) : value(v) { live.push_back(this); }
    tracked_value(const tracked_value& other) : value(other.value) { live.push_back(this); }
    ~tracked_value() { std::erase(live, this); }
    static bool is_live(const tracked_value* object) {
        return std::find(live.begin(), live.end(), object) != live.end();
    }
};

struct tracked_query : std::forwarding_query_t {
    template<class Env>
    auto operator()(Env&& env) const noexcept(noexcept(std::forward<Env>(env).query(*this)))
        -> decltype(std::forward<Env>(env).query(*this)) {
        return std::forward<Env>(env).query(*this);
    }
};

// The consumer holds the environment, queries it, and inspects the reference
// after the forwarding call has returned.
struct inspect_tracked_lifetime {
    template<class Env>
        requires requires(const Env& env) {
            { tracked_query{}(env) } noexcept -> std::same_as<const tracked_value&>;
        }
    auto operator()(const Env& env) const noexcept -> int {
        const tracked_value& result = tracked_query{}(env);
        return tracked_value::is_live(std::addressof(result)) ? result.value : -1;
    }
};

using tracked_env = ex::env<ex::prop<tracked_query, tracked_value>,
                            ex::prop<ex::get_start_scheduler_t, ex::inline_scheduler>>;

template<class Value>
struct tracked_receiver {
    using receiver_concept = ex::receiver_tag;
    std::optional<Value>* result;
    // prop::query returns a reference into this prvalue environment.
    auto get_env() const noexcept -> tracked_env {
        return tracked_env{ex::prop{tracked_query{}, tracked_value{42}},
                           ex::prop{ex::get_start_scheduler, ex::inline_scheduler{}}};
    }
    void set_value(Value value) && noexcept { result->emplace(std::move(value)); }
    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}
};

template<class Adaptor>
void check_by_value_environment_lifetime(Adaptor adapt) {
    auto result = run_category_child<int, tracked_receiver<int>>(
        adapt(ex::read_env(inspect_tracked_lifetime{})));
    EXPECT_EQ(result, 42);
    EXPECT_TRUE(tracked_value::live.empty());
}

// The enclosing adaptors that do not filter the environment themselves hand
// their environment to their children. A nested forwarding adaptor reports
// the completions of the view it is connected with, whichever category the
// receiver environment has ([exec.snd.expos]/47.1).
struct start_env {
    int id = 0;
    auto query(ex::get_start_scheduler_t) const noexcept -> ex::inline_scheduler { return {}; }
};

template<class Value, class Env>
struct exact_start_receiver {
    using receiver_concept = ex::receiver_tag;
    start_env* env;
    bool* completed;
    auto get_env() const noexcept -> Env { return static_cast<Env>(*env); }
    void set_value(Value) && noexcept { *completed = true; }
    template<class Error>
    void set_error(Error&&) && noexcept {}
    void set_stopped() && noexcept {}
};

template<class Env, class S>
auto run_exact_start(const S& sender) {
    using value = advertised_value<S, Env>;
    start_env env;
    bool completed = false;
    auto op = ex::connect(sender, exact_start_receiver<value, Env>{&env, &completed});
    ex::start(op);
    EXPECT_TRUE(completed);
    return std::type_identity<value>{};
}

template<class S>
void check_exact_start_environment(S sender) {
    auto lvalue = run_exact_start<start_env&>(sender);
    auto const_lvalue = run_exact_start<const start_env&>(sender);
    auto rvalue = run_exact_start<start_env&&>(sender);
    auto owned = run_exact_start<start_env>(sender);
    static_assert(std::same_as<decltype(lvalue), decltype(const_lvalue)>);
    static_assert(std::same_as<decltype(lvalue), decltype(rvalue)>);
    static_assert(std::same_as<decltype(rvalue), decltype(owned)>);
}

// A sender is a sender in an environment only if its child is: each adaptor
// asks for the child's signatures in the environment that it forwards
// ([exec.then]/4, [exec.continues.on]/6, [exec.starts.on]/4,
// [exec.write.env]/5, [exec.let]/9.4). The child below needs a forwarding
// query that only one of the two environments answers. With one view type per
// environment type, participation and connection use the same view, so there
// is no separate declval<Env>() view to tell apart any more.
struct needed_query : std::forwarding_query_t {
    // q(env) is env.query(q) ([exec.queryable.general]/5).
    template<class Env>
    constexpr auto operator()(const Env& env) const noexcept(noexcept(env.query(*this)))
        -> decltype(env.query(*this)) { return env.query(*this); }
};
struct answering_env {
    auto query(needed_query) const noexcept -> int { return 1; }
};
struct silent_env {};
template<class Env>
concept answers_needed = requires(const Env& env, const needed_query& query) {
    { env.query(query) } -> std::same_as<int>;
    { query(env) } -> std::same_as<int>;
};
static_assert(answers_needed<answering_env> && !answers_needed<silent_env>);
static_assert(answers_needed<ex::__forge_env_detail::__fwd_env_view<answering_env>>);
struct needs_query_sender {
    using sender_concept = ex::sender_tag;
    template<class Self, answers_needed Env>
    static constexpr auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(int)> { return {}; }
    template<ex::receiver R>
    auto connect(R receiver) const
        noexcept(noexcept(ex::connect(ex::just(1), std::move(receiver))))
        -> ex::connect_result_t<decltype(ex::just(1)), R> {
        return ex::connect(ex::just(1), std::move(receiver));
    }
};
// Only the schedule hop of starts_on(needy_scheduler{}, just()) can reject.
struct needy_scheduler;
struct needy_schedule_sender {
    using sender_concept = ex::sender_tag;
    template<class Self, answers_needed Env>
    static constexpr auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t()> { return {}; }
    constexpr auto get_env() const noexcept -> needy_scheduler;
    template<ex::receiver R>
    auto connect(R receiver) const
        noexcept(noexcept(ex::connect(ex::just(), std::move(receiver))))
        -> ex::connect_result_t<decltype(ex::just()), R> {
        return ex::connect(ex::just(), std::move(receiver));
    }
};
struct needy_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    constexpr auto schedule() const noexcept -> needy_schedule_sender { return {}; }
    constexpr auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    constexpr bool operator==(const needy_scheduler&) const noexcept = default;
};
constexpr auto needy_schedule_sender::get_env() const noexcept -> needy_scheduler { return {}; }
static_assert(ex::scheduler<needy_scheduler>);

template<class Sender>
constexpr bool participates_with_child =
    ex::sender_in<Sender, answering_env> && ex::sender_in<Sender, const answering_env&> &&
    !ex::sender_in<Sender, silent_env> && !ex::sender_in<Sender, const silent_env&>;
struct make_needs_query {
    auto operator()() const noexcept -> needs_query_sender { return {}; }
};
static_assert(participates_with_child<decltype(ex::starts_on(needy_scheduler{}, ex::just()))>);
static_assert(participates_with_child<decltype(ex::starts_on(ex::inline_scheduler{}, needs_query_sender{}))>);
static_assert(participates_with_child<decltype(ex::let_value(ex::just(), make_needs_query{}))>);
static_assert(participates_with_child<decltype(ex::continues_on(needs_query_sender{}, ex::inline_scheduler{}))>);
static_assert(participates_with_child<decltype(ex::continues_on(ex::just(), needy_scheduler{}))>);
static_assert(participates_with_child<decltype(ex::schedule_from(needs_query_sender{}))>);
static_assert(participates_with_child<decltype(ex::write_env(needs_query_sender{}, ex::env<>{}))>);
static_assert(participates_with_child<decltype(ex::then(needs_query_sender{}, pass_value{}))>);
static_assert(participates_with_child<decltype(ex::upon_error(needs_query_sender{}, replace_with_int{}))>);
static_assert(participates_with_child<decltype(ex::upon_stopped(needs_query_sender{}, replace_with_int{}))>);
static_assert(participates_with_child<decltype(test_bulk::bulk(needs_query_sender{}, 3, ignore_arguments{}))>);
static_assert(participates_with_child<decltype(test_bulk::bulk_chunked(needs_query_sender{}, 3, ignore_arguments{}))>);
static_assert(participates_with_child<decltype(test_bulk::bulk_unchunked(needs_query_sender{}, 3, ignore_arguments{}))>);
static_assert(participates_with_child<decltype(ex::into_variant(needs_query_sender{}))>);
static_assert(participates_with_child<decltype(ex::stopped_as_optional(needs_query_sender{}))>);

template<class Env>
struct start_receiver {
    using receiver_concept = ex::receiver_tag;
    std::remove_reference_t<Env>* env;
    int* result;
    auto get_env() const noexcept -> Env { return static_cast<Env>(*env); }
    void set_value(int value) && noexcept { *result = value; }
    template<class Error>
    void set_error(Error&&) && noexcept { *result = -1; }
    void set_stopped() && noexcept { *result = -2; }
};
// A schedule sender whose attributes are its scheduler ([exec.sched]/5-6) and
// which counts the operations that its scheduler runs.
template<class Scheduler>
struct scheduler_fixture_sender {
    using sender_concept = ex::sender_tag;
    template<class Self, class... Env>
    static constexpr auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t()> { return {}; }
    constexpr auto get_env() const noexcept -> Scheduler { return {}; }
    template<class R>
    struct operation {
        using operation_state_concept = ex::operation_state_tag;
        R receiver;
        void start() & noexcept {
            ++Scheduler::runs;
            ex::set_value(std::move(receiver));
        }
    };
    template<ex::receiver R>
    auto connect(R receiver) const noexcept -> operation<R> { return {std::move(receiver)}; }
};

// A volatile referent is borrowed as such and answers through its const
// volatile member.
struct volatile_query : std::forwarding_query_t {
    template<class Env>
    auto operator()(Env&& env) const noexcept(noexcept(std::forward<Env>(env).query(*this)))
        -> decltype(std::forward<Env>(env).query(*this)) {
        return std::forward<Env>(env).query(*this);
    }
};
struct volatile_env {
    int value = 42;
    auto query(volatile_query) const volatile noexcept -> int { return value; }
    auto query(ex::get_start_scheduler_t) const volatile noexcept -> ex::inline_scheduler {
        return {};
    }
};

// An environment whose constructor template accepts anything. The view must
// hold the object that get_env returned, not one built from something else.
struct greedy_env {
    int answer;
    struct made {};
    constexpr greedy_env(made, int value) noexcept : answer(value) {}
    template<class Anything>
    constexpr greedy_env(Anything&&) : answer(-1) {}
    auto query(needed_query) const noexcept -> int { return answer; }
};
struct greedy_receiver {
    using receiver_concept = ex::receiver_tag;
    int* calls;
    int* result;
    auto get_env() const noexcept -> greedy_env {
        ++*calls;
        return greedy_env{greedy_env::made{}, 42};
    }
    void set_value(int value) && noexcept { *result = value; }
    void set_error(std::exception_ptr) && noexcept { *result = -1; }
    void set_stopped() && noexcept { *result = -2; }
};

// Any destructible type is an environment, tag types of the library included.
template<class Env>
struct tag_env_receiver {
    using receiver_concept = ex::receiver_tag;
    const std::remove_cvref_t<Env>* env;
    int* result;
    auto get_env() const noexcept -> Env { return static_cast<Env>(*env); }
    void set_value(int value) && noexcept { *result = value; }
    void set_error(std::exception_ptr) && noexcept { *result = -1; }
    void set_stopped() && noexcept { *result = -2; }
};

// A child that needs a copy-constructible environment and uses the copy. A
// view that borrows an environment which cannot be copied is itself copyable,
// as at 8de815c.
struct copies_environment {
    using sender_concept = ex::sender_tag;
    std::inplace_stop_token expected;
    template<class Self, std::copy_constructible Env>
    static constexpr auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(int)> { return {}; }
    template<class R>
    struct operation {
        using operation_state_concept = ex::operation_state_tag;
        R receiver;
        std::inplace_stop_token expected;
        void start() & noexcept {
            auto environment = ex::get_env(receiver);
            static_assert(std::copy_constructible<decltype(environment)>);
            auto copy = environment;
            ex::set_value(std::move(receiver),
                std::get_stop_token(copy) == expected ? 42 : -3);
        }
    };
    template<ex::receiver R>
        requires std::copy_constructible<ex::env_of_t<R>>
    auto connect(R receiver) const noexcept -> operation<R> {
        return {std::move(receiver), expected};
    }
};

// A receiver whose environment is an array, which no function returns by
// value: the type-only probes of let must not declare one that does.
struct array_env_receiver {
    using receiver_concept = ex::receiver_tag;
    const int (*env)[2];
    int* result;
    auto get_env() const noexcept -> const int (&)[2] { return *env; }
    void set_value(int value) && noexcept { *result = value; }
    void set_error(std::exception_ptr) && noexcept { *result = -1; }
    void set_stopped() && noexcept { *result = -2; }
};

template<bool Borrow>
struct env_task {
    struct promise_type : ex::with_awaitable_senders<promise_type> {
        std::conditional_t<Borrow, borrowed_env, owned_env> env;
        promise_type(std::inplace_stop_token token, int*) : env(token) {}
        env_task get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
        decltype(auto) get_env() const noexcept {
            if constexpr (Borrow) return (env);
            else return owned_env{env};
        }
    };
};

template<bool Borrow>
env_task<Borrow> await_environment(std::inplace_stop_token token, int* result) {
    *result = co_await inspect_environment(token);
}
}

TEST(ExecutionBorrowedEnvTest, LetAdaptorsPreserveNoncopyableEnvironment) {
    std::inplace_stop_source source;
    borrowed_env env{source.get_token()};
    auto inner = [&] { return inspect_environment(source.get_token()); };
    check_environment(ex::let_value(ex::just(), inner), env);
    check_environment(ex::let_error(ex::just_error(1), [&](int&) { return inner(); }), env);
    check_environment(ex::let_stopped(ex::just_stopped(), inner), env);
    owned_env owned{source.get_token()};
    check_environment(ex::let_value(ex::just(), inner), owned);
    check_environment(ex::let_error(ex::just_error(1), [&](int&) { return inner(); }), owned);
    check_environment(ex::let_stopped(ex::just_stopped(), inner), owned);
}

TEST(ExecutionBorrowedEnvTest, LetAdvertisesRuntimeEnvironmentShape) {
    check_advertised_shape(ex::let_value(ex::just(), [] { return shape_sender{}; }));
    check_advertised_shape(ex::let_error(ex::just_error(1), [](int&) { return shape_sender{}; }));
    check_advertised_shape(ex::let_stopped(ex::just_stopped(), [] { return shape_sender{}; }));
}

TEST(ExecutionBorrowedEnvTest, LetAdvertisesRuntimeEnvironmentType) {
    check_advertised_type(ex::let_value(ex::just(), [] { return type_sender{}; }));
    check_advertised_type(ex::let_error(ex::just_error(1), [](int&) { return type_sender{}; }));
    check_advertised_type(ex::let_stopped(ex::just_stopped(), [] { return type_sender{}; }));
}

TEST(ExecutionBorrowedEnvTest, SharedAdaptorsAdvertiseRuntimeEnvironmentType) {
    check_advertised_type(ex::then(type_sender{}, pass_value{}));
    check_advertised_type(ex::upon_error(type_sender{}, replace_with_int{}));
    check_advertised_type(ex::upon_stopped(type_sender{}, replace_with_int{}));
    check_advertised_type(test_bulk::bulk(type_sender{}, 3, ignore_arguments{}));
    check_advertised_type(test_bulk::bulk_chunked(type_sender{}, 3, ignore_arguments{}));
    check_advertised_type(test_bulk::bulk_unchunked(type_sender{}, 3, ignore_arguments{}));
    check_advertised_type(ex::into_variant(type_sender{}));
    check_advertised_type(ex::stopped_as_optional(type_sender{}));
}

TEST(ExecutionBorrowedEnvTest, StartsOnBorrowsReferencesAndOwnsValues) {
    std::inplace_stop_source source;
    borrowed_env borrowed{source.get_token()};
    owned_env owned{source.get_token()};
    check_environment(ex::starts_on(ex::inline_scheduler{},
        inspect_environment(source.get_token())), borrowed);
    check_environment(ex::starts_on(ex::inline_scheduler{},
        inspect_environment(source.get_token())), owned);
}

TEST(ExecutionBorrowedEnvTest, StartsOnAdvertisesRuntimeEnvironmentShape) {
    check_advertised_shape<ex::inline_scheduler>(ex::starts_on(ex::inline_scheduler{}, shape_sender{}));
}

TEST(ExecutionBorrowedEnvTest, StartsOnAdvertisesRuntimeEnvironmentType) {
    check_advertised_type(ex::starts_on(ex::inline_scheduler{}, type_sender{}));
}

TEST(ExecutionBorrowedEnvTest, LetForwardsRefQualifiedReceiverEnvironment) {
    check_ref_qualified_environment([](auto child) {
        return ex::let_value(ex::schedule(ex::inline_scheduler{}), [child]() noexcept { return child; });
    });
}

TEST(ExecutionBorrowedEnvTest, StartsOnForwardsRefQualifiedReceiverEnvironment) {
    check_ref_qualified_environment([](auto child) {
        return ex::starts_on(ex::inline_scheduler{}, child);
    });
}

TEST(ExecutionBorrowedEnvTest, FirstFormOnForwardsRefQualifiedReceiverEnvironment) {
    check_ref_qualified_environment([](auto child) {
        return ex::on(ex::inline_scheduler{}, child);
    });
}

TEST(ExecutionBorrowedEnvTest, ContinuesOnAndWriteEnvForwardRefQualifiedReceiverEnvironment) {
    ref_qualified_env env;
    check_ref_qualified_members([](auto child) {
        return ex::continues_on(child, ex::inline_scheduler{});
    }, env);
    check_ref_qualified_members([](auto child) {
        return ex::then(child, pass_value{});
    }, env);
    // The joined environment of write_env has unary lookup only.
    using lvalue_receiver = category_receiver<const int*, ref_qualified_env&>;
    auto written = run_category_child<const int*, lvalue_receiver>(
        ex::write_env(ex::read_env(inspect_const_unary_reference{}), ex::env<>{}), &env);
    EXPECT_EQ(written, std::addressof(env.value));
}

TEST(ExecutionBorrowedEnvTest, AssociateAndUserSendersAdvertiseRuntimeEnvironmentType) {
    // The locked associate and a user sender take the cv-unqualified
    // environment type for their signatures and connect a nested let with a
    // borrowed environment.
    check_advertised_type(user_wrapper{ex::let_value(ex::just(), [] { return type_sender{}; })});
    check_advertised_type(user_wrapper{ex::then(type_sender{}, pass_value{})});
    ex::simple_counting_scope scope;
    {
        std::inplace_stop_source source;
        borrowed_env env{source.get_token()};
        auto sender = ex::associate(
            ex::let_value(ex::just(), [] { return type_sender{}; }), scope.get_token());
        using value = advertised_value<decltype(sender), const borrowed_env&>;
        static_assert(std::same_as<value, advertised_value<decltype(sender), borrowed_env>>);
        bool completed = false;
        auto op = ex::connect(std::move(sender), exact_shape_receiver<value>{&env, &completed});
        ex::start(op);
        EXPECT_TRUE(completed);
    }
    std::this_thread::sync_wait(scope.join());
}

template<class Env, class Sender>
void check_volatile_environment(Sender sender) {
    using receiver = start_receiver<Env>;
    static_assert(ex::sender_in<Sender, Env>);
    static_assert(std::same_as<ex::completion_signatures_of_t<Sender, Env>,
                               ex::completion_signatures<ex::set_value_t(int)>>);
    volatile volatile_env env;
    int result = 0;
    auto op = ex::connect(std::move(sender), receiver{&env, &result});
    ex::start(op);
    EXPECT_EQ(result, 42);
}

template<class Env>
void check_volatile_environments() {
    check_volatile_environment<Env>(ex::then(ex::read_env(volatile_query{}), pass_value{}));
    check_volatile_environment<Env>(
        ex::let_value(ex::just(), []() noexcept { return ex::read_env(volatile_query{}); }));
    check_volatile_environment<Env>(
        ex::continues_on(ex::read_env(volatile_query{}), ex::inline_scheduler{}));
    check_volatile_environment<Env>(
        ex::write_env(ex::read_env(volatile_query{}), ex::env<>{}));
}

TEST(ExecutionBorrowedEnvTest, VolatileEnvironmentIsBorrowedAsVolatile) {
    check_volatile_environments<volatile volatile_env&>();
    // affine lowers through a const reference parameter, which binds a
    // volatile lvalue only; that is its 8de815c behaviour.
    check_volatile_environment<volatile volatile_env&>(
        ex::affine(ex::then(ex::read_env(volatile_query{}), pass_value{})));
    check_volatile_environments<volatile volatile_env&&>();
    check_volatile_environments<const volatile volatile_env&&>();
}

// affine checks and lowers with the scheduler that the receiver's actual
// environment selects, as at 8de815c. A const volatile referent selects
// another infallible scheduler type than the cv-unqualified type that the
// signature query sees; both have the same completions ([exec.affine]/5, /7).
template<int Tag>
struct tagged_start_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    static inline int runs = 0;
    constexpr auto schedule() const noexcept
        -> scheduler_fixture_sender<tagged_start_scheduler> { return {}; }
    constexpr auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    constexpr bool operator==(const tagged_start_scheduler&) const noexcept = default;
};
struct dual_start_env {
    auto query(ex::get_start_scheduler_t) const noexcept -> tagged_start_scheduler<1> { return {}; }
    auto query(ex::get_start_scheduler_t) const volatile noexcept -> tagged_start_scheduler<2> {
        return {};
    }
};

TEST(ExecutionBorrowedEnvTest, AffineLowersWithTheSchedulerOfTheActualEnvironment) {
    using sender_t = decltype(ex::affine(ex::just(5)));
    using expected = ex::completion_signatures<ex::set_value_t(int)>;
    static_assert(std::same_as<decltype(ex::get_start_scheduler(std::declval<dual_start_env&>())),
                               tagged_start_scheduler<1>>);
    static_assert(std::same_as<decltype(ex::get_start_scheduler(
        std::declval<volatile dual_start_env&>())), tagged_start_scheduler<2>>);
    static_assert(ex::sender_in<sender_t, volatile dual_start_env&>);
    static_assert(std::same_as<ex::completion_signatures_of_t<sender_t, volatile dual_start_env&>,
                               expected>);
    tagged_start_scheduler<1>::runs = 0;
    tagged_start_scheduler<2>::runs = 0;
    volatile dual_start_env env;
    int result = 0;
    auto op = ex::connect(ex::affine(ex::just(5)),
                          start_receiver<volatile dual_start_env&>{&env, &result});
    ex::start(op);
    EXPECT_EQ(result, 5);
    EXPECT_EQ(tagged_start_scheduler<1>::runs, 0);
    EXPECT_EQ(tagged_start_scheduler<2>::runs, 1);
    dual_start_env plain;
    result = 0;
    auto plain_op = ex::connect(ex::affine(ex::just(5)),
                                start_receiver<dual_start_env&>{&plain, &result});
    ex::start(plain_op);
    EXPECT_EQ(result, 5);
    EXPECT_EQ(tagged_start_scheduler<1>::runs, 1);
    EXPECT_EQ(tagged_start_scheduler<2>::runs, 1);
}

// A child may keep a copy of its environment view for as long as its receiver
// is valid ([exec.get.env]/2, [exec.queryable.general]/6). A view is a pointer
// and owns nothing. These receivers return the environment by value, so the
// adaptor's operation state owns the object that the view points at; a
// reference result would point at the receiver's externally owned referent.
// Either way the copy outlives the view it was made from and never touches
// the environment's own copy or move.
struct retained_env {
    static inline int copies = 0;
    tracked_value value{42};
    retained_env() = default;
    retained_env(const retained_env& other) noexcept(false) : value(other.value) { ++copies; }
    retained_env(retained_env&& other) noexcept : value(other.value) {}
    auto query(tracked_query) const noexcept -> const tracked_value& { return value; }
    auto query(ex::get_start_scheduler_t) const noexcept -> ex::inline_scheduler { return {}; }
};
struct pinned_retained_env {
    tracked_value value{42};
    pinned_retained_env() = default;
    pinned_retained_env(const pinned_retained_env&) = delete;
    auto query(tracked_query) const noexcept -> const tracked_value& { return value; }
    auto query(ex::get_start_scheduler_t) const noexcept -> ex::inline_scheduler { return {}; }
};

struct retains_view_copy {
    using sender_concept = ex::sender_tag;
    template<class Self, class Env>
        requires std::is_nothrow_copy_constructible_v<Env> &&
                 std::is_nothrow_move_constructible_v<Env>
    static constexpr auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(int)> { return {}; }
    template<class R>
    struct operation {
        using operation_state_concept = ex::operation_state_tag;
        R receiver;
        void start() & noexcept {
            using view = std::remove_cvref_t<ex::env_of_t<R>>;
            static_assert(std::is_nothrow_copy_constructible_v<view>);
            static_assert(std::is_nothrow_move_constructible_v<view>);
            std::optional<view> copy;
            std::optional<view> moved;
            const tracked_value* backing = nullptr;
            {
                view original = ex::get_env(receiver);
                // The identity of the backing, taken while the view is alive.
                backing = std::addressof(tracked_query{}(original));
                copy.emplace(original);
                view second = original;
                moved.emplace(std::move(second));
            }
            // The original view is gone. Its backing must be alive before any
            // copy is queried; the registry compares addresses only.
            int result = -1;
            if (tracked_value::is_live(backing)) {
                const tracked_value& first = tracked_query{}(*copy);
                const tracked_value& second = tracked_query{}(*moved);
                result = std::addressof(first) == backing && std::addressof(second) == backing
                    ? first.value : -2;
            }
            ex::set_value(std::move(receiver), int{result});
        }
    };
    template<ex::receiver R>
    auto connect(R receiver) const noexcept -> operation<R> { return {std::move(receiver)}; }
};

template<class Env>
struct retained_receiver {
    using receiver_concept = ex::receiver_tag;
    std::optional<int>* result;
    auto get_env() const noexcept -> Env { return Env{}; }
    void set_value(int value) && noexcept { result->emplace(value); }
    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}
};

template<class Env, class Adaptor>
void check_retained_view_copy(Adaptor adapt) {
    auto result = run_category_child<int, retained_receiver<Env>>(adapt(retains_view_copy{}));
    EXPECT_EQ(result, 42);
    EXPECT_TRUE(tracked_value::live.empty());
}

template<class Env>
void check_retained_view_copies() {
    check_retained_view_copy<Env>([](auto child) { return ex::then(child, pass_value{}); });
    check_retained_view_copy<Env>([](auto child) {
        return ex::let_value(ex::just(), [child]() noexcept { return child; });
    });
    check_retained_view_copy<Env>([](auto child) {
        return ex::starts_on(ex::inline_scheduler{}, child);
    });
    check_retained_view_copy<Env>([](auto child) {
        return ex::continues_on(child, ex::inline_scheduler{});
    });
    check_retained_view_copy<Env>([](auto child) { return ex::on(ex::inline_scheduler{}, child); });
    check_retained_view_copy<Env>([](auto child) {
        return ex::write_env(child, ex::env<>{});
    });
    check_retained_view_copy<Env>([](auto child) { return ex::schedule_from(child); });
    check_retained_view_copy<Env>([](auto child) {
        return test_bulk::bulk(child, 3, ignore_arguments{});
    });
}

TEST(ExecutionBorrowedEnvTest, RetainedViewCopyOutlivesTheViewItWasCopiedFrom) {
    // An environment that cannot be copied or moved.
    check_retained_view_copies<pinned_retained_env>();
    // A nothrow move and a potentially throwing copy: neither runs.
    retained_env::copies = 0;
    check_retained_view_copies<retained_env>();
    EXPECT_EQ(retained_env::copies, 0);
}

TEST(ExecutionBorrowedEnvTest, OwnedEnvironmentIsTheObjectThatGetEnvReturned) {
    int calls = 0;
    int result = 0;
    auto then_op = ex::connect(ex::then(ex::read_env(needed_query{}), pass_value{}),
                               greedy_receiver{&calls, &result});
    ex::start(then_op);
    EXPECT_EQ(result, 42);
    EXPECT_EQ(calls, 1);
    calls = 0;
    result = 0;
    auto write_op = ex::connect(ex::write_env(ex::read_env(needed_query{}), ex::env<>{}),
                                greedy_receiver{&calls, &result});
    ex::start(write_op);
    EXPECT_EQ(result, 42);
    EXPECT_EQ(calls, 1);
    result = 0;
    auto let_op = ex::connect(
        ex::let_value(ex::just(), []() noexcept { return ex::read_env(needed_query{}); }),
        greedy_receiver{&calls, &result});
    ex::start(let_op);
    EXPECT_EQ(result, 42);
}

template<class Env>
void check_tag_environment(const std::remove_cvref_t<Env>& tag) {
    int result = 0;
    auto let_op = ex::connect(
        ex::let_value(ex::just(), []() noexcept { return ex::just(7); }),
        tag_env_receiver<Env>{&tag, &result});
    ex::start(let_op);
    EXPECT_EQ(result, 7);
    result = 0;
    auto then_op = ex::connect(ex::then(ex::just(7), pass_value{}),
                               tag_env_receiver<Env>{&tag, &result});
    ex::start(then_op);
    EXPECT_EQ(result, 7);
    result = 0;
    auto write_op = ex::connect(ex::write_env(ex::just(7), ex::env<>{}),
                                tag_env_receiver<Env>{&tag, &result});
    ex::start(write_op);
    EXPECT_EQ(result, 7);
}

TEST(ExecutionBorrowedEnvTest, LibraryTagTypesAreEnvironments) {
    check_tag_environment<std::in_place_t>(std::in_place);
    check_tag_environment<const std::in_place_t&>(std::in_place);
    check_tag_environment<std::nullopt_t>(std::nullopt);
    check_tag_environment<const std::nullopt_t&>(std::nullopt);
}

TEST(ExecutionBorrowedEnvTest, BorrowingViewsOfNoncopyableEnvironmentsAreCopyable) {
    std::inplace_stop_source source;
    borrowed_env env{source.get_token()};
    static_assert(!std::copy_constructible<borrowed_env>);
    copies_environment child{source.get_token()};
    auto let_sender = ex::let_value(ex::just(), [child]() noexcept { return child; });
    static_assert(ex::sender_in<decltype(let_sender), const borrowed_env&>);
    check_environment(std::move(let_sender), env);
    auto write_sender = ex::write_env(child, ex::env<>{});
    static_assert(ex::sender_in<decltype(write_sender), const borrowed_env&>);
    check_environment(std::move(write_sender), env);
    check_environment(ex::then(child, pass_value{}), env);
}

TEST(ExecutionBorrowedEnvTest, LetAcceptsAnArrayEnvironment) {
    auto sender = ex::let_value(ex::just(), []() noexcept { return ex::just(7); });
    using expected = ex::completion_signatures<ex::set_value_t(int)>;
    static_assert(std::same_as<ex::completion_signatures_of_t<decltype(sender), int[2]>, expected>);
    static_assert(std::same_as<ex::completion_signatures_of_t<decltype(sender), const int (&)[2]>,
                               expected>);
    const int environment[2] = {1, 2};
    int result = 0;
    auto op = ex::connect(std::move(sender), array_env_receiver{&environment, &result});
    ex::start(op);
    EXPECT_EQ(result, 7);
}

TEST(ExecutionBorrowedEnvTest, EnclosingAdaptorsAdvertiseRuntimeEnvironmentType) {
    check_exact_start_environment(
        ex::stopped_as_error(ex::then(type_sender{}, pass_value{}), 5));
    check_exact_start_environment(ex::affine(ex::then(type_sender{}, pass_value{})));
    check_exact_start_environment(ex::on(ex::schedule(ex::inline_scheduler{}),
        ex::inline_scheduler{},
        ex::let_value([]() noexcept { return type_sender{}; })));
}

TEST(ExecutionBorrowedEnvTest, ByValueEnvironmentOutlivesForwardedReferences) {
    check_by_value_environment_lifetime([](auto child) {
        return ex::let_value(ex::just(), [child]() noexcept { return child; });
    });
    check_by_value_environment_lifetime([](auto child) {
        return ex::starts_on(ex::inline_scheduler{}, child);
    });
    check_by_value_environment_lifetime([](auto child) {
        return ex::on(ex::inline_scheduler{}, child);
    });
    check_by_value_environment_lifetime([](auto child) {
        return ex::continues_on(child, ex::inline_scheduler{});
    });
    check_by_value_environment_lifetime([](auto child) {
        return ex::write_env(child, ex::env<>{});
    });
}

TEST(ExecutionBorrowedEnvTest, WriteEnvBorrowsReferencesAndOwnsValues) {
    std::inplace_stop_source source;
    borrowed_env borrowed{source.get_token()};
    owned_env owned{source.get_token()};
    check_environment(ex::write_env(inspect_environment(source.get_token()), ex::env<>{}), borrowed);
    check_environment(ex::write_env(inspect_environment(source.get_token()), ex::env<>{}), owned);
    std::inplace_stop_source overlay;
    check_environment(ex::write_env(inspect_environment(overlay.get_token()),
        owned_env{overlay.get_token()}), borrowed);
    check_environment(ex::write_env(inspect_environment(overlay.get_token()),
        owned_env{overlay.get_token()}), owned);
}

// A conforming receiver can count its moves. into_variant and schedule_from
// take the receiver by value, which is one move from the caller's xvalue, and
// move that parameter once into their operation state before the child is
// connected. 8de815c also made two moves: the parameter, then the wrapping
// receiver ([exec.snd.expos]/27, /33).
struct move_counting_receiver {
    using receiver_concept = ex::receiver_tag;
    int* moves;
    bool* completed;

    explicit move_counting_receiver(int* count, bool* done) noexcept
        : moves(count), completed(done) {}
    move_counting_receiver(move_counting_receiver&& other) noexcept
        : moves(other.moves), completed(other.completed) { ++*moves; }
    move_counting_receiver(const move_counting_receiver&) = delete;

    template<class... Values>
    void set_value(Values&&...) && noexcept { *completed = true; }
};

// The child takes its receiver by reference and records the moves so far.
struct connect_entry_sender {
    using sender_concept = ex::sender_tag;
    const int* moves;
    int* moves_at_connect;

    template<class Self, class... Env>
    static constexpr auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(int)> { return {}; }

    template<ex::receiver R>
    auto connect(R&& rcvr) const
        -> ex::connect_result_t<decltype(ex::just(1)), R> {
        *moves_at_connect = *moves;
        return ex::connect(ex::just(1), std::forward<R>(rcvr));
    }
};

template<class Adaptor>
void check_receiver_moves_before_child_connect(Adaptor adapt) {
    int moves = 0;
    int moves_at_connect = -1;
    bool completed = false;
    auto sender = adapt(connect_entry_sender{&moves, &moves_at_connect});
    move_counting_receiver receiver{&moves, &completed};
    // Positive control: the count starts at zero and the operation completes.
    ASSERT_EQ(moves, 0);
    auto operation = ex::connect(std::move(sender), std::move(receiver));
    EXPECT_EQ(moves_at_connect, 2);
    EXPECT_EQ(moves, 2);
    ex::start(operation);
    EXPECT_TRUE(completed);
}

TEST(ExecutionBorrowedEnvTest, ReceiverOnlyAdaptorsMoveTheirParameterOnce) {
    check_receiver_moves_before_child_connect(
        [](auto child) { return ex::into_variant(std::move(child)); });
    check_receiver_moves_before_child_connect(
        [](auto child) { return ex::schedule_from(std::move(child)); });
}

TEST(ExecutionBorrowedEnvTest, PromiseBorrowsReferencesAndOwnsValues) {
    std::inplace_stop_source source;
    int result = 0;
    await_environment<true>(source.get_token(), &result);
    EXPECT_EQ(result, 42);
    result = 0;
    await_environment<false>(source.get_token(), &result);
    EXPECT_EQ(result, 42);
}

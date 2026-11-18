#include <forge/bulk.hpp>
#include <execution>

#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

namespace ex = std::execution;
namespace {
struct receiver {
    using receiver_concept = ex::receiver_tag;
    template<class... Ts> void set_value(Ts&&...) && noexcept {}
    template<class E> void set_error(E&&) && noexcept {}
    void set_stopped() && noexcept {}
};
struct identity { int operator()(int v) const noexcept { return v; } };
struct nullary { void operator()() const noexcept {} };
struct wrong { void operator()(int*) const noexcept {} };
struct bulk_fn { void operator()(int, int&) const noexcept {} };
struct chunk_fn { void operator()(int, int, int&) const noexcept {} };
struct successor {
    auto operator()(int&) const noexcept { return ex::just(1); }
};
struct stopped_successor {
    auto operator()() const noexcept { return ex::just(1); }
};
struct throwing_successor {
    auto operator()(int&) const { return ex::just(1); }
};
struct invalid_successor { int operator()(int&) const noexcept { return 0; } };
struct copy_only_callable {
    copy_only_callable() = default;
    copy_only_callable(const copy_only_callable&) = default;
    copy_only_callable(copy_only_callable&&) = delete;
    void operator()(int) const noexcept {}
};
static_assert(std::invocable<ex::then_t, copy_only_callable&>);
using copied_closure = decltype(ex::then(std::declval<copy_only_callable&>()));
static_assert(!std::invocable<copied_closure&, decltype(ex::just(1))>);
using wrong_stopped_closure = decltype(ex::let_stopped(wrong{}));
static_assert(!std::invocable<wrong_stopped_closure, decltype(ex::just_stopped())>);
struct immovable {
    immovable() = default;
    immovable(const immovable&) = delete;
    immovable(immovable&&) = delete;
};
struct throwing_move {
    throwing_move() = default;
    throwing_move(const throwing_move&) = default;
    throwing_move(throwing_move&&) noexcept(false) {}
};
template<class... Sigs>
struct signature_sender {
    using sender_concept = ex::sender_tag;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> ex::completion_signatures<Sigs...> { return {}; }
};

template<class S>
constexpr bool nothrow_connect = noexcept(ex::connect(std::declval<S>(), receiver{}));
using j = decltype(ex::just(1));
static_assert(nothrow_connect<j> && nothrow_connect<const j&>);
static_assert(nothrow_connect<decltype(ex::just_error(1))>);
static_assert(nothrow_connect<decltype(ex::just_stopped())>);
static_assert(nothrow_connect<decltype(ex::read_env(std::get_stop_token))>);
static_assert(nothrow_connect<decltype(ex::then(ex::just(1), identity{}))>);
static_assert(nothrow_connect<decltype(ex::upon_error(ex::just_error(1), identity{}))>);
static_assert(nothrow_connect<decltype(ex::upon_stopped(ex::just_stopped(), nullary{}))>);
static_assert(nothrow_connect<decltype(forge::bulk_chunked(ex::just(1), 3, chunk_fn{}))>);
static_assert(nothrow_connect<decltype(forge::bulk_unchunked(ex::just(1), 3, bulk_fn{}))>);
static_assert(nothrow_connect<decltype(ex::into_variant(ex::just(1)))>);
static_assert(!nothrow_connect<decltype(ex::just(throwing_move{}))>);
static_assert(!nothrow_connect<decltype(forge::bulk(ex::just(1), 3, bulk_fn{}))>);
static_assert(!nothrow_connect<decltype(ex::let_value(ex::just(1), successor{}))>);
static_assert(!nothrow_connect<decltype(ex::when_all(ex::just()))>);
static_assert(!nothrow_connect<decltype(ex::starts_on(ex::inline_scheduler{}, ex::just()))>);

template<class S> using cs = ex::completion_signatures_of_t<S, ex::env<>>;
static_assert(std::same_as<cs<decltype(ex::let_value(ex::just(1), successor{}))>,
    ex::completion_signatures<ex::set_value_t(int)>>);
static_assert(std::same_as<cs<decltype(ex::let_error(ex::just_error(1), successor{}))>,
    ex::completion_signatures<ex::set_value_t(int)>>);
static_assert(std::same_as<cs<decltype(ex::let_stopped(ex::just_stopped(), stopped_successor{}))>,
    ex::completion_signatures<ex::set_value_t(int)>>);
static_assert(std::same_as<cs<decltype(ex::let_value(ex::just(1), throwing_successor{}))>,
    ex::completion_signatures<ex::set_value_t(int), ex::set_error_t(std::exception_ptr)>>);
static_assert(std::same_as<cs<decltype(ex::starts_on(ex::inline_scheduler{}, ex::just(1)))>,
    ex::completion_signatures<ex::set_value_t(int)>>);

// [exec.starts.on]/4 uses the whole outgoing sender, including Sch.
struct declared_throwing_schedule_sender;
struct declared_throwing_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    declared_throwing_scheduler() = default;
    declared_throwing_scheduler(const declared_throwing_scheduler&) noexcept(false) {}
    declared_throwing_scheduler(declared_throwing_scheduler&&) noexcept(false) {}
    declared_throwing_scheduler& operator=(const declared_throwing_scheduler&) = default;
    declared_throwing_scheduler& operator=(declared_throwing_scheduler&&) = default;
    auto schedule() const noexcept -> declared_throwing_schedule_sender;
    auto query(ex::get_completion_scheduler_t<ex::set_value_t>) const noexcept -> declared_throwing_scheduler {
        return *this;
    }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const declared_throwing_scheduler&) const noexcept = default;
};
struct declared_throwing_schedule_sender {
    using sender_concept = ex::sender_tag;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> ex::completion_signatures<ex::set_value_t()> { return {}; }
    auto get_env() const noexcept {
        return ex::prop{ex::get_completion_scheduler<ex::set_value_t>, declared_throwing_scheduler{}};
    }
    template<class R>
    auto connect(R r) const noexcept(noexcept(ex::connect(ex::just(), std::move(r))))
        -> ex::connect_result_t<decltype(ex::just()), R> { return ex::connect(ex::just(), std::move(r)); }
};
inline auto declared_throwing_scheduler::schedule() const noexcept -> declared_throwing_schedule_sender { return {}; }
static_assert(std::same_as<cs<decltype(ex::starts_on(declared_throwing_scheduler{}, ex::just(1)))>,
    ex::completion_signatures<ex::set_value_t(int), ex::set_error_t(std::exception_ptr)>>);
struct allocated_receiver : receiver {
    auto get_env() const noexcept { return ex::prop{std::get_allocator, std::allocator<int>{}}; }
};
static_assert(noexcept(ex::connect(std::declval<j>(), allocated_receiver{})));
static_assert(noexcept(ex::connect(std::declval<const j&>(), allocated_receiver{})));

static_assert(ex::sender_in<decltype(ex::then(ex::just(1), identity{}))>);
static_assert(!ex::sender_in<decltype(ex::then(ex::just(1), wrong{}))>);
static_assert(!ex::sender_in<decltype(ex::upon_error(ex::just_error(1), wrong{}))>);
static_assert(!ex::sender_in<decltype(ex::upon_stopped(ex::just_stopped(), wrong{}))>);
static_assert(!ex::sender_in<decltype(ex::let_value(ex::just(1), wrong{}))>);
static_assert(!ex::sender_in<decltype(ex::let_value(ex::just(1), invalid_successor{}))>);
static_assert(ex::sender_in<decltype(forge::bulk_chunked(ex::just(1), 3, chunk_fn{}))>);
static_assert(!ex::sender_in<decltype(forge::bulk_chunked(ex::just(1), 3, bulk_fn{}))>);
static_assert(ex::sender_in<decltype(forge::bulk_unchunked(ex::just(1), 3, bulk_fn{}))>);
static_assert(!ex::sender_in<decltype(forge::bulk_unchunked(ex::just(1), 3, chunk_fn{}))>);
static_assert(!ex::sender_in<decltype(forge::bulk(ex::just(1), 3, chunk_fn{}))>);
using uncopyable_value = signature_sender<ex::set_value_t(immovable&)>;
using uncopyable_error = signature_sender<ex::set_error_t(immovable&)>;
static_assert(!ex::sender_in<decltype(ex::into_variant(uncopyable_value{}))>);
static_assert(!ex::sender_in<decltype(ex::into_variant(uncopyable_error{}))>);
static_assert(!ex::sender_in<decltype(ex::stopped_as_optional(uncopyable_value{}))>);

template<class S> concept connectable = requires(S&& s) { ex::connect(static_cast<S&&>(s), receiver{}); };
static_assert(!connectable<decltype(ex::then(ex::just(1), wrong{}))>);
static_assert(!connectable<decltype(ex::upon_error(ex::just_error(1), wrong{}))>);
static_assert(!connectable<decltype(ex::upon_stopped(ex::just_stopped(), wrong{}))>);
static_assert(!connectable<decltype(ex::let_value(ex::just(1), invalid_successor{}))>);
static_assert(!connectable<decltype(forge::bulk_chunked(ex::just(1), 3, bulk_fn{}))>);
static_assert(!connectable<decltype(ex::into_variant(uncopyable_value{}))>);

using missing = decltype(ex::read_env(std::get_allocator));
static_assert(!ex::sender_in<missing> && !connectable<missing>);
struct any_function { template<class... As> void operator()(As&&...) const noexcept {} };
struct any_sender { template<class... As> auto operator()(As&&...) const noexcept { return ex::just(1); } };
static_assert(!ex::sender_in<decltype(ex::then(missing{}, any_function{}))>);
static_assert(!ex::sender_in<decltype(ex::upon_error(missing{}, any_function{}))>);
static_assert(!ex::sender_in<decltype(ex::let_value(missing{}, any_sender{}))>);
static_assert(!ex::sender_in<decltype(forge::bulk(missing{}, 3, any_function{}))>);
static_assert(!ex::sender_in<decltype(ex::into_variant(missing{}))>);
static_assert(!ex::sender_in<decltype(ex::write_env(missing{}, ex::env<>{}))>);
static_assert(!ex::sender_in<decltype(ex::stopped_as_error(missing{}, 1))>);
static_assert(!ex::sender_in<decltype(ex::starts_on(ex::inline_scheduler{}, missing{}))>);
static_assert(!ex::sender_in<decltype(ex::on(ex::inline_scheduler{}, ex::just()))>);

// Signatures alone do not establish connection: these sources intentionally
// have no connect member. Every rejection must remain an immediate constraint.
using value_only = signature_sender<ex::set_value_t(int)>;
using error_only = signature_sender<ex::set_error_t(int)>;
using stopped_only = signature_sender<ex::set_stopped_t()>;
static_assert(ex::sender_in<value_only>);
using no_connect_then = decltype(ex::then(value_only{}, identity{}));
using no_connect_error = decltype(ex::upon_error(error_only{}, identity{}));
using no_connect_stopped = decltype(ex::upon_stopped(stopped_only{}, nullary{}));
using no_connect_bulk = decltype(forge::bulk_unchunked(value_only{}, 3, bulk_fn{}));
using no_connect_chunked = decltype(forge::bulk_chunked(value_only{}, 3, chunk_fn{}));
using no_connect_transformed = decltype(forge::bulk(value_only{}, 3, bulk_fn{}));
using no_connect_let = decltype(ex::let_value(value_only{}, successor{}));
using no_connect_let_error = decltype(ex::let_error(error_only{}, successor{}));
using no_connect_let_stopped = decltype(ex::let_stopped(stopped_only{}, stopped_successor{}));
using no_connect_variant = decltype(ex::into_variant(value_only{}));
using no_connect_optional = decltype(ex::stopped_as_optional(value_only{}));
using no_connect_as_error = decltype(ex::stopped_as_error(stopped_only{}, 1));
using no_connect_write = decltype(ex::write_env(value_only{}, ex::env<>{}));
using no_connect_starts = decltype(ex::starts_on(ex::inline_scheduler{}, value_only{}));
using no_connect_continues = decltype(ex::continues_on(value_only{}, ex::inline_scheduler{}));
template<class S>
constexpr bool signatures_without_connection = ex::sender_in<S> &&
    !connectable<S> && !connectable<const S&>;
static_assert(signatures_without_connection<no_connect_then>);
static_assert(signatures_without_connection<no_connect_error>);
static_assert(signatures_without_connection<no_connect_stopped>);
static_assert(signatures_without_connection<no_connect_bulk>);
static_assert(signatures_without_connection<no_connect_chunked>);
static_assert(signatures_without_connection<no_connect_transformed>);
static_assert(signatures_without_connection<no_connect_let>);
static_assert(signatures_without_connection<no_connect_let_error>);
static_assert(signatures_without_connection<no_connect_let_stopped>);
static_assert(signatures_without_connection<no_connect_variant>);
static_assert(signatures_without_connection<no_connect_optional>);
static_assert(signatures_without_connection<no_connect_as_error>);
static_assert(signatures_without_connection<no_connect_write>);
// starts_on includes the child connect expression in its signature formula.
static_assert(!ex::sender_in<no_connect_starts> && !connectable<no_connect_starts>);
static_assert(signatures_without_connection<no_connect_continues>);
static_assert(connectable<decltype(ex::then(ex::just(1), identity{}))>);
static_assert(connectable<decltype(ex::upon_error(ex::just_error(1), identity{}))>);
static_assert(connectable<decltype(ex::upon_stopped(ex::just_stopped(), nullary{}))>);
static_assert(connectable<decltype(forge::bulk_chunked(ex::just(1), 3, chunk_fn{}))>);
static_assert(connectable<decltype(forge::bulk_unchunked(ex::just(1), 3, bulk_fn{}))>);
static_assert(connectable<decltype(forge::bulk(ex::just(1), 3, bulk_fn{}))>);
static_assert(connectable<decltype(ex::let_value(ex::just(1), successor{}))>);
static_assert(connectable<decltype(ex::let_error(ex::just_error(1), successor{}))>);
static_assert(connectable<decltype(ex::let_stopped(ex::just_stopped(), stopped_successor{}))>);
static_assert(connectable<decltype(ex::into_variant(ex::just(1)))>);
static_assert(connectable<decltype(ex::stopped_as_optional(ex::just(1)))>);
static_assert(connectable<decltype(ex::stopped_as_error(ex::just_stopped(), 1))>);
static_assert(connectable<decltype(ex::write_env(ex::just(1), ex::env<>{}))>);
static_assert(connectable<decltype(ex::starts_on(ex::inline_scheduler{}, ex::just(1)))>);
static_assert(connectable<decltype(ex::continues_on(ex::just(1), ex::inline_scheduler{}))>);
struct disconnected_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    auto schedule() const noexcept -> signature_sender<ex::set_value_t()> { return {}; }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const disconnected_scheduler&) const noexcept = default;
};
using no_schedule_starts = decltype(ex::starts_on(disconnected_scheduler{}, ex::just(1)));
using no_schedule_continues = decltype(ex::continues_on(ex::just(1), disconnected_scheduler{}));
static_assert(signatures_without_connection<no_schedule_starts>);
static_assert(signatures_without_connection<no_schedule_continues>);

struct disconnected_successor {
    auto operator()(int&) const noexcept -> value_only { return {}; }
};
using bad_successor = decltype(ex::let_value(ex::just(1), disconnected_successor{}));
static_assert(!ex::sender_in<bad_successor> && !connectable<bad_successor>);
// A successor may accept the signature oracle but reject the actual receiver2.
template<class R> struct signature_oracle : std::false_type {};
template<class Env>
struct signature_oracle<ex::__forge_let::__signature_receiver<Env>> : std::true_type {};
struct oracle_only_sender : value_only {
    template<class R> requires signature_oracle<R>::value
    auto connect(R r) const noexcept -> ex::connect_result_t<j, R> {
        return ex::connect(ex::just(1), std::move(r));
    }
};
struct oracle_successor {
    auto operator()(int&) const noexcept -> oracle_only_sender { return {}; }
};
using guarded_successor = decltype(ex::let_value(ex::just(1), oracle_successor{}));
static_assert(signatures_without_connection<guarded_successor>);

// [exec.snd.expos]/28: prvalue get-state bypasses a possibly throwing
// data move/copy on the allocator path. Valid receivers always move nothrow.
using throwing_data = decltype(ex::just(throwing_move{}));
static_assert(!noexcept(ex::connect(std::declval<throwing_data>(), receiver{})));
static_assert(noexcept(ex::connect(std::declval<throwing_data>(), allocated_receiver{})));
static_assert(noexcept(ex::connect(std::declval<const throwing_data&>(), allocated_receiver{})));
struct throwing_receiver : allocated_receiver {
    throwing_receiver() = default;
    throwing_receiver(throwing_receiver&&) noexcept(false) {}
};
static_assert(ex::receiver<allocated_receiver>);
static_assert(!ex::receiver<throwing_receiver>);
template<class S, class R>
concept connects_to = requires(S&& s, R&& r) {
    ex::connect(static_cast<S&&>(s), static_cast<R&&>(r));
};
static_assert(!connects_to<j, throwing_receiver>);
using allocator_env = ex::env_of_t<allocated_receiver>;
static_assert(std::same_as<ex::completion_signatures_of_t<decltype(ex::let_value(ex::just(1), successor{})), allocator_env>,
    ex::completion_signatures<ex::set_value_t(int)>>);
static_assert(std::same_as<ex::completion_signatures_of_t<decltype(ex::starts_on(ex::inline_scheduler{}, ex::just(1))), allocator_env>,
    ex::completion_signatures<ex::set_value_t(int)>>);

struct local_query {
    template<class E> requires requires(const E& e) { e.query(std::declval<local_query>()); }
    auto operator()(const E& e) const noexcept -> decltype(e.query(*this)) { return e.query(*this); }
};
struct forward_query : std::forwarding_query_t {
    template<class E> requires requires(const E& e) { e.query(std::declval<forward_query>()); }
    auto operator()(const E& e) const noexcept -> decltype(e.query(*this)) { return e.query(*this); }
};
struct environment {
    int query(local_query) const noexcept { return 1; }
    int query(forward_query) const noexcept { return 1; }
};
using local = decltype(ex::read_env(local_query{}));
using forwarded = decltype(ex::read_env(forward_query{}));
static_assert(ex::sender_in<local, environment>);
static_assert(!ex::sender_in<decltype(ex::then(local{}, identity{})), environment>);
static_assert(ex::sender_in<decltype(ex::then(forwarded{}, identity{})), environment>);
static_assert(!ex::sender_in<decltype(ex::upon_error(local{}, identity{})), environment>);
static_assert(!ex::sender_in<decltype(forge::bulk(local{}, 3, bulk_fn{})), environment>);
static_assert(!ex::sender_in<decltype(ex::into_variant(local{})), environment>);
static_assert(!ex::sender_in<decltype(ex::stopped_as_optional(local{})), environment>);
static_assert(!ex::sender_in<decltype(ex::starts_on(ex::inline_scheduler{}, local{})), environment>);
// These two predecessor connections use the unfiltered receiver environment.
static_assert(ex::sender_in<decltype(ex::let_value(local{}, successor{})), environment>);
static_assert(ex::sender_in<decltype(ex::stopped_as_error(local{}, 1)), environment>);
// A nested lvalue connection must reconstruct the child's data directly.
struct allocator_copy_data {
    using allocator_type = std::allocator<int>;
    allocator_copy_data() = default;
    allocator_copy_data(const allocator_copy_data&) noexcept(false) {}
    allocator_copy_data(allocator_copy_data&&) noexcept = default;
    allocator_copy_data(std::allocator_arg_t, allocator_type, const allocator_copy_data&) noexcept {}
    allocator_copy_data(std::allocator_arg_t, allocator_type, allocator_copy_data&&) noexcept {}
};
using copy_child = decltype(ex::just(allocator_copy_data{}));
using copy_error = decltype(ex::just_error(allocator_copy_data{}));
using nested_then = decltype(ex::then(std::declval<copy_child>(), any_function{}));
using nested_error = decltype(ex::upon_error(std::declval<copy_error>(), any_function{}));
using nested_stopped = decltype(ex::upon_stopped(std::declval<copy_child>(), nullary{}));
using nested_bulk = decltype(forge::bulk_unchunked(std::declval<copy_child>(), 1, any_function{}));
using nested_chunked = decltype(forge::bulk_chunked(std::declval<copy_child>(), 1, any_function{}));
using nested_variant = decltype(ex::into_variant(std::declval<copy_child>()));
using nested_write = decltype(ex::write_env(std::declval<copy_child>(), ex::env<>{}));
using nested_departure = decltype(ex::schedule_from(std::declval<copy_child>()));
template<class S>
constexpr bool nested_noexcept =
    !noexcept(ex::connect(std::declval<const S&>(), receiver{})) &&
    noexcept(ex::connect(std::declval<const S&>(), allocated_receiver{}));
static_assert(nested_noexcept<nested_then>);
static_assert(nested_noexcept<nested_error>);
static_assert(nested_noexcept<nested_stopped>);
static_assert(nested_noexcept<nested_bulk>);
static_assert(nested_noexcept<nested_chunked>);
static_assert(nested_noexcept<nested_variant>);
static_assert(nested_noexcept<nested_write>);
static_assert(nested_noexcept<nested_departure>);

// Distinguish the child's const-lvalue connection and completion set from
// its rvalue path; a preliminary copy would select the wrong one.
struct category_child {
    using sender_concept = ex::sender_tag;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> std::conditional_t<
        std::is_lvalue_reference_v<Self>, ex::completion_signatures<ex::set_value_t(long)>,
        ex::completion_signatures<ex::set_value_t(int)>> { return {}; }
    template<class R>
    auto connect(R r) && noexcept -> ex::connect_result_t<decltype(ex::just(1)), R> {
        return ex::connect(ex::just(1), std::move(r));
    }
    template<class R>
    auto connect(R r) const& noexcept -> ex::connect_result_t<decltype(ex::just(2L)), R> {
        return ex::connect(ex::just(2L), std::move(r));
    }
};
struct pass_value { template<class T> T operator()(T v) const noexcept { return v; } };
using category_then = decltype(ex::then(category_child{}, pass_value{}));
using category_error = decltype(ex::upon_error(category_child{}, any_function{}));
using category_stopped = decltype(ex::upon_stopped(category_child{}, nullary{}));
using category_bulk = decltype(forge::bulk_unchunked(category_child{}, 1, any_function{}));
using category_chunked = decltype(forge::bulk_chunked(category_child{}, 1, any_function{}));
using category_write = decltype(ex::write_env(category_child{}, ex::env<>{}));
using category_departure = decltype(ex::schedule_from(category_child{}));
template<class S>
constexpr bool category_signatures = std::same_as<cs<S>, ex::completion_signatures<ex::set_value_t(int)>> &&
    std::same_as<cs<const S&>, ex::completion_signatures<ex::set_value_t(long)>> &&
    connectable<S> && connectable<const S&>;
static_assert(category_signatures<category_then>);
static_assert(category_signatures<category_error>);
static_assert(category_signatures<category_stopped>);
static_assert(category_signatures<category_bulk>);
static_assert(category_signatures<category_chunked>);
static_assert(category_signatures<category_write>);
static_assert(category_signatures<category_departure>);
using category_variant = decltype(ex::into_variant(category_child{}));
static_assert(std::same_as<cs<const category_variant&>, ex::completion_signatures<
    ex::set_value_t(std::variant<std::tuple<long>>)>>);
struct rvalue_child : category_child {
    using category_child::connect;
    template<class R> void connect(R) const& = delete;
};
using guarded_lvalue = decltype(ex::then(rvalue_child{}, pass_value{}));
static_assert(connectable<guarded_lvalue> && !connectable<const guarded_lvalue&>);
struct lvalue_child : category_child {
    using category_child::connect;
    template<class R> void connect(R) && = delete;
};
using guarded_rvalue = decltype(ex::then(lvalue_child{}, pass_value{}));
static_assert(!connectable<guarded_rvalue> && connectable<const guarded_rvalue&>);
} // namespace
int main() {}

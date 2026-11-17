#include <execution>
#include <forge/static_thread_pool.hpp>
#include <forge/strand.hpp>
#include <type_traits>
#include <utility>
namespace ex = std::execution;
namespace {
using empty = ex::env<>;
using start_env = decltype(ex::prop{ex::get_start_scheduler, ex::inline_scheduler{}});
using stop_env = decltype(ex::prop{std::get_stop_token, std::inplace_stop_token{}});
struct receiver {
    using receiver_concept = ex::receiver_tag;
    template<class... As> void set_value(As&&...) && noexcept {}
    template<class E> void set_error(E&&) && noexcept {}
    void set_stopped() && noexcept {}
    auto get_env() const noexcept -> start_env { return {ex::get_start_scheduler, ex::inline_scheduler{}}; }
};
struct member_sender {
    using sender_concept = ex::sender_tag;
    auto affine() & { return ex::just(char{1}); }
    auto affine() && { return ex::just(1); }
    auto affine() const&& { return ex::just(short{3}); }
    auto affine() const& { return ex::just(2L); }
};
using custom = decltype(ex::affine(member_sender{}));
static_assert(ex::sender_in<custom, start_env>);
static_assert(!ex::sender_in<custom, empty>);
static_assert(std::same_as<ex::completion_signatures_of_t<custom, start_env>,
    ex::completion_signatures<ex::set_value_t(int)>>);
static_assert(std::same_as<ex::completion_signatures_of_t<const custom&, start_env>,
    ex::completion_signatures<ex::set_value_t(long)>>);
static_assert(requires(custom s) { ex::connect(std::move(s), receiver{}); });
static_assert(std::same_as<ex::completion_signatures_of_t<custom&, start_env>,
    ex::completion_signatures<ex::set_value_t(char)>>);
static_assert(std::same_as<ex::completion_signatures_of_t<const custom&&, start_env>,
    ex::completion_signatures<ex::set_value_t(short)>>);
static_assert(std::same_as<ex::connect_result_t<const custom&&, receiver>,
    ex::connect_result_t<decltype(ex::just(short{3})), receiver>>);
static_assert(std::same_as<ex::connect_result_t<custom&, receiver>,
    ex::connect_result_t<decltype(ex::just(char{1})), receiver>>);
static_assert(std::same_as<ex::connect_result_t<const custom&, receiver>,
    ex::connect_result_t<decltype(ex::just(2L)), receiver>>);
struct fallible_sender {
    using sender_concept = ex::sender_tag;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(), ex::set_error_t(int)> { return {}; }
};
struct fallible_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    auto schedule() const noexcept -> fallible_sender { return {}; }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept { return ex::forward_progress_guarantee::weakly_parallel; }
    bool operator==(const fallible_scheduler&) const noexcept = default;
};
using bad_env = decltype(ex::prop{ex::get_start_scheduler, fallible_scheduler{}});
static_assert(!ex::sender_in<custom, bad_env>);
static_assert(!ex::sender_in<decltype(ex::affine(ex::just())), bad_env>);
using loop_start = decltype(ex::prop{ex::get_start_scheduler, std::declval<ex::run_loop::scheduler>()});
static_assert(ex::sender_in<decltype(ex::affine(ex::just())), loop_start>);
using pool_sender = ex::schedule_result_t<forge::static_thread_pool::scheduler>;
using strand_sender = ex::schedule_result_t<forge::strand::scheduler>;
static_assert(std::same_as<ex::completion_signatures_of_t<pool_sender, empty>,
    ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>>);
static_assert(std::same_as<ex::completion_signatures_of_t<strand_sender, empty>,
    ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>>);
static_assert(std::same_as<ex::completion_signatures_of_t<pool_sender, stop_env>,
    ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>>);
static_assert(std::same_as<ex::completion_signatures_of_t<strand_sender, stop_env>,
    ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>>);
using pool_env = decltype(ex::prop{ex::get_start_scheduler, std::declval<forge::static_thread_pool::scheduler>()});
using strand_env = decltype(ex::prop{ex::get_start_scheduler, std::declval<forge::strand::scheduler>()});
template<class Env>
using stoppable_start = ex::env<Env, stop_env>;
using never_env = decltype(ex::prop{std::get_stop_token, std::never_stop_token{}});
template<class Env>
using never_start = ex::env<Env, never_env>;
using affine_value = decltype(ex::affine(ex::just()));
static_assert(!ex::sender_in<affine_value, empty>);
static_assert(!ex::sender_in<affine_value, pool_env>);
static_assert(!ex::sender_in<affine_value, strand_env>);
static_assert(!ex::sender_in<affine_value, never_start<pool_env>>);
static_assert(!ex::sender_in<affine_value, never_start<strand_env>>);
static_assert(ex::sender_in<affine_value, stoppable_start<pool_env>>);
static_assert(ex::sender_in<affine_value, stoppable_start<strand_env>>);
static_assert(std::same_as<decltype(ex::unstoppable(ex::just())),
    decltype(ex::write_env(ex::just(), ex::prop{std::get_stop_token, std::never_stop_token{}}))>);

using value = decltype(ex::continues_on(ex::just(1), ex::inline_scheduler{}));
using error = decltype(ex::continues_on(ex::just_error(1), ex::inline_scheduler{}));
using stopped = decltype(ex::continues_on(ex::just_stopped(), ex::inline_scheduler{}));
template<class S, class Tag, class Env = empty>
constexpr bool completion_query = std::invocable<ex::get_completion_scheduler_t<Tag>, ex::env_of_t<S>, Env>;
static_assert(completion_query<value, ex::set_value_t>);
static_assert(!completion_query<value, ex::set_error_t> && !completion_query<value, ex::set_stopped_t>);
static_assert(completion_query<error, ex::set_error_t>);
static_assert(!completion_query<error, ex::set_value_t> && !completion_query<error, ex::set_stopped_t>);
static_assert(completion_query<stopped, ex::set_stopped_t>);
static_assert(!completion_query<stopped, ex::set_value_t> && !completion_query<stopped, ex::set_error_t>);
using can_fail = decltype(ex::continues_on(ex::just_error(1), fallible_scheduler{}));
static_assert(!completion_query<can_fail, ex::set_error_t>);
using cancellable = decltype(ex::continues_on(ex::just(), std::declval<ex::run_loop::scheduler>()));
static_assert(!completion_query<cancellable, ex::set_stopped_t>);
static_assert(completion_query<cancellable, ex::set_stopped_t, stop_env>);
struct immovable { immovable(immovable&&) = delete; immovable(const immovable&) = delete; };
struct uncopyable_sender {
    using sender_concept = ex::sender_tag;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> ex::completion_signatures<ex::set_value_t(immovable&)> { return {}; }
};
static_assert(!ex::sender_in<decltype(ex::continues_on(uncopyable_sender{}, ex::inline_scheduler{}))>);
using missing = decltype(ex::read_env(std::get_allocator));
static_assert(!ex::sender_in<decltype(ex::continues_on(missing{}, ex::inline_scheduler{}))>);
static_assert(!ex::sender_in<decltype(ex::affine(missing{})), start_env>);

struct mutable_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    auto schedule() noexcept { return ex::just(); }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const mutable_scheduler&) const noexcept = default;
};
using mutable_wrapper = ex::__forge_affine::__unstoppable_scheduler<mutable_scheduler>;
template<class Sch> concept schedulable = requires(Sch&& sch) { ex::schedule(static_cast<Sch&&>(sch)); };
static_assert(schedulable<mutable_wrapper&> && schedulable<mutable_wrapper>);
static_assert(!schedulable<const mutable_wrapper&> && !schedulable<const mutable_wrapper>);
struct mutable_env {
    auto query(ex::get_start_scheduler_t) const noexcept -> mutable_scheduler { return {}; }
};
static_assert(ex::scheduler<mutable_scheduler> && !ex::scheduler<const mutable_scheduler&>);
static_assert(std::same_as<decltype(ex::get_start_scheduler(std::declval<const mutable_env&>())),
    mutable_scheduler>);
static_assert(ex::sender_in<affine_value, mutable_env>);
struct mutable_receiver : receiver {
    auto get_env() const noexcept -> mutable_env { return {}; }
};
static_assert(requires { ex::connect(std::declval<affine_value>(), mutable_receiver{}); });
// [exec.prop] returns const ValueType&. This is not a scheduler through
// const lvalue, so [exec.get.start.scheduler]/2 and [exec.affine]/7 reject it.
using mutable_prop_env = decltype(ex::prop{ex::get_start_scheduler, mutable_scheduler{}});
static_assert(std::same_as<decltype(std::declval<const mutable_prop_env&>().query(ex::get_start_scheduler)),
    const mutable_scheduler&>);
static_assert(!ex::sender_in<affine_value, mutable_prop_env>);

struct domain_source {
    using sender_concept = ex::sender_tag;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(int)> { return {}; }
};
struct scheduler_domain {
    template<class Env>
    auto transform_sender(ex::start_t, domain_source&&, const Env&) const noexcept
        -> decltype(ex::just(73L)) { return ex::just(73L); }
};
struct domain_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    auto schedule() const noexcept { return ex::just(); }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    template<class Env>
    auto query(ex::get_completion_domain_t<ex::set_value_t>, const Env&) const noexcept
        -> scheduler_domain { return {}; }
    bool operator==(const domain_scheduler&) const noexcept = default;
};
struct addressless_env {
    addressless_env* operator&() = delete;
    const addressless_env* operator&() const = delete;
    auto query(ex::get_scheduler_t) const noexcept -> domain_scheduler { return {}; }
};
struct addressless_receiver : receiver {
    auto get_env() const noexcept -> addressless_env { return {}; }
};
static_assert(std::same_as<decltype(ex::get_domain(std::declval<const addressless_env&>())),
    scheduler_domain>);
static_assert(std::same_as<ex::completion_signatures_of_t<domain_source, addressless_env>,
    ex::completion_signatures<ex::set_value_t(long)>>);
static_assert(requires { ex::connect(domain_source{}, addressless_receiver{}); });

struct forwarding_key : std::forwarding_query_t {};
struct local_key {};
struct attributes {
    int* value;
    int& query(forwarding_key) const noexcept { return *value; }
    int query(local_key) const noexcept { return 42; }
    auto query(ex::get_completion_scheduler_t<ex::set_value_t>) const noexcept { return ex::inline_scheduler{}; }
};
struct attributed_sender {
    using sender_concept = ex::sender_tag;
    attributes attrs;
    auto get_env() const noexcept -> attributes { return attrs; }
};
using on_attrs = ex::env_of_t<decltype(ex::on(ex::inline_scheduler{}, attributed_sender{}))>;
using departure_attrs = ex::env_of_t<decltype(ex::schedule_from(attributed_sender{}))>;
template<class E, class Q> concept answers = requires(const E& e, Q q) { e.query(q); };
static_assert(answers<on_attrs, forwarding_key> && !answers<on_attrs, local_key>);
static_assert(!answers<on_attrs, ex::get_completion_scheduler_t<ex::set_value_t>>);
static_assert(answers<departure_attrs, forwarding_key> && !answers<departure_attrs, local_key>);
struct environment_source {
    using sender_concept = ex::sender_tag;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        if constexpr (requires(const Env& env) { env.query(forwarding_key{}); }) {
            return ex::completion_signatures<ex::set_value_t()>{};
        } else {
            return ex::completion_signatures<ex::set_stopped_t()>{};
        }
    }
};
using changing = decltype(ex::continues_on(environment_source{}, ex::inline_scheduler{}));
static_assert(completion_query<changing, ex::set_stopped_t>);
static_assert(!completion_query<changing, ex::set_stopped_t, attributes>);
using changed_attrs = ex::env_of_t<decltype(ex::then(std::declval<changing>(), []() noexcept {}))>;
static_assert(!std::invocable<ex::get_completion_scheduler_t<ex::set_stopped_t>, changed_attrs, attributes>);
static_assert(!std::invocable<ex::get_completion_domain_t<ex::set_stopped_t>, changed_attrs, attributes>);
struct query_scheduler : fallible_scheduler {
    int value = 42;
    const int& query(local_key) const noexcept { return value; }
    using fallible_scheduler::query;
};
using wrapper = ex::__forge_affine::__unstoppable_scheduler<query_scheduler>;
static_assert(std::same_as<decltype(std::declval<const wrapper&>().query(local_key{})), const int&>);
static_assert(noexcept(std::declval<const wrapper&>().query(local_key{})));
static_assert(schedulable<wrapper&> && schedulable<const wrapper&> && schedulable<wrapper>);
// Runtime schedules the stored non-const lvalue. The rvalue result has
// different signatures and independent connectability.
template<bool Connected, class Error>
struct category_schedule_sender {
    using sender_concept = ex::sender_tag;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(), ex::set_error_t(Error)> { return {}; }
    template<class R> requires Connected
    auto connect(R r) const noexcept -> ex::connect_result_t<decltype(ex::just()), R> {
        return ex::connect(ex::just(), std::move(r));
    }
};
template<bool LvalueConnected, bool RvalueConnected>
struct category_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    auto schedule() & noexcept -> category_schedule_sender<LvalueConnected, long> { return {}; }
    auto schedule() && noexcept -> category_schedule_sender<RvalueConnected, short> { return {}; }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const category_scheduler&) const noexcept = default;
};
template<class S> concept connects = requires(S&& s) { ex::connect(static_cast<S&&>(s), receiver{}); };
using lvalue_only = category_scheduler<true, false>;
using rvalue_only = category_scheduler<false, true>;
using category_start = decltype(ex::starts_on(lvalue_only{}, ex::just(1)));
using category_continue = decltype(ex::continues_on(ex::just(1), lvalue_only{}));
using category_cs = ex::completion_signatures<ex::set_value_t(int), ex::set_error_t(long)>;
static_assert(std::same_as<ex::completion_signatures_of_t<category_start>, category_cs>);
static_assert(std::same_as<ex::completion_signatures_of_t<category_continue>, category_cs>);
static_assert(connects<category_start> && connects<const category_start&>);
static_assert(connects<category_continue> && connects<const category_continue&>);
using rejected_start = decltype(ex::starts_on(rvalue_only{}, ex::just(1)));
using rejected_continue = decltype(ex::continues_on(ex::just(1), rvalue_only{}));
static_assert(ex::sender_in<rejected_start> && ex::sender_in<rejected_continue>);
static_assert(!connects<rejected_start> && !connects<const rejected_start&>);
static_assert(!connects<rejected_continue> && !connects<const rejected_continue&>);
} // namespace
int main() {}

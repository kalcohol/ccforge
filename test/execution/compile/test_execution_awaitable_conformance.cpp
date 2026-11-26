#include "awaitable_conformance_types.hpp"
#include <tuple>

namespace a = awaitable_tests;
namespace ex = std::execution;
using int_sigs = ex::completion_signatures<ex::set_value_t(int),
    ex::set_error_t(std::exception_ptr), ex::set_stopped_t()>;
using ref_sigs = ex::completion_signatures<ex::set_value_t(int&),
    ex::set_error_t(std::exception_ptr), ex::set_stopped_t()>;
using void_sigs = ex::completion_signatures<ex::set_value_t(),
    ex::set_error_t(std::exception_ptr), ex::set_stopped_t()>;

static_assert(ex::sender<std::suspend_always> && ex::sender<std::suspend_never>);
static_assert(ex::sender_in<std::suspend_always, ex::env<>>);
static_assert(std::same_as<ex::completion_signatures_of_t<a::int_awaiter>, int_sigs>);
static_assert(std::same_as<ex::completion_signatures_of_t<a::int_awaiter, ex::env<>>, int_sigs>);
static_assert(std::same_as<ex::completion_signatures_of_t<a::ref_awaiter>, ref_sigs>);
static_assert(std::same_as<ex::completion_signatures_of_t<a::ref_awaiter, ex::env<>>, ref_sigs>);
static_assert(std::same_as<ex::completion_signatures_of_t<a::void_awaiter>, void_sigs>);
static_assert(std::same_as<ex::completion_signatures_of_t<a::void_awaiter, ex::env<>>, void_sigs>);
static_assert(std::same_as<ex::completion_signatures_of_t<a::cv_void_awaiter, ex::env<>>, void_sigs>);
static_assert(ex::sender<a::member_awaitable> && ex::sender<a::free_awaitable>);
static_assert(ex::sender_in<a::member_adapted, ex::env<>>);
static_assert(!ex::sender_in<a::member_adapted>); // No env-promise in the nullary form.
static_assert(!ex::sender<a::disabled_awaitable>);
static_assert(!ex::sender<a::not_awaitable> && !ex::sender<a::bad_suspend>);
static_assert(!ex::sender<a::ambiguous_awaitable>);
static_assert(std::same_as<decltype(ex::as_awaitable(std::declval<a::ambiguous_awaitable>(),
    std::declval<a::promise&>())), a::ambiguous_awaitable&&>);
static_assert(ex::sender<a::recursive_marked>);
static_assert(std::same_as<ex::completion_signatures_of_t<a::recursive_marked, ex::env<>>,
    ex::completion_signatures<ex::set_value_t(int), ex::set_error_t(std::exception_ptr),
        ex::set_stopped_t(), ex::set_error_t(long)>>);
static_assert(std::same_as<ex::completion_signatures_of_t<a::raw_sender, ex::env<>>, int_sigs>);
static_assert(ex::operation_state<ex::connect_result_t<a::int_awaiter, a::receiver>>);
static_assert(std::is_nothrow_move_constructible_v<ex::connect_result_t<a::int_awaiter, a::receiver>>);
static_assert(!std::is_copy_constructible_v<ex::connect_result_t<a::int_awaiter, a::receiver>>);
static_assert(!noexcept(ex::connect(a::int_awaiter{}, a::receiver{})));
static_assert(!noexcept(ex::connect(a::raw_sender{}, a::receiver{})));
static_assert(ex::__forge_sender_to<a::start_sender, a::start_receiver>);
static_assert(!noexcept(ex::connect(a::start_sender{}, a::start_receiver{})));
static_assert(ex::__forge_sender_to<a::adl_value_awaiter, awaitable_adl::receiver>);
static_assert(!ex::__forge_awaitable::__is_awaitable<a::int_awaiter, a::rejecting_promise>);

// 7.1, 7.2, 7.3, 7.4 and identity have independent noexcept oracles.
static_assert(noexcept(ex::as_awaitable(a::member_adapted{}, std::declval<a::promise&>())));
static_assert(noexcept(ex::as_awaitable(a::adapted_sender<true, true>{}, std::declval<a::promise&>())));
static_assert(!noexcept(ex::as_awaitable(a::adapted_sender<false, true>{}, std::declval<a::promise&>())));
static_assert(!noexcept(ex::as_awaitable(a::adapted_sender<true, false>{}, std::declval<a::promise&>())));
static_assert(!noexcept(ex::as_awaitable(a::throwing_source{}, std::declval<a::throwing_transform_promise&>())));
static_assert(noexcept(ex::as_awaitable(a::int_awaiter{}, std::declval<a::mixin_promise&>())));
static_assert(!noexcept(ex::as_awaitable(ex::just(1), std::declval<a::promise&>())));
static_assert(noexcept(ex::as_awaitable(a::not_awaitable{}, std::declval<a::promise&>())));

struct overriding_promise : ex::with_awaitable_senders<overriding_promise> {
    using ex::with_awaitable_senders<overriding_promise>::await_transform;

    template<ex::sender S>
    auto await_transform(S&&) -> std::suspend_never { return {}; }
};

// The constrained promise overload stays preferred over the unconstrained
// mixin for a valid rvalue sender, without hiding the mixin's identity path.
static_assert(std::same_as<decltype(std::declval<overriding_promise&>().await_transform(
    ex::just(1))), std::suspend_never>);
static_assert(std::same_as<decltype(std::declval<overriding_promise&>().await_transform(
    a::not_awaitable{})), a::not_awaitable&&>);

template<class... ValueSigs>
struct bridge_source {
    using sender_concept = ex::sender_tag;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ValueSigs...> { return {}; }

    template<ex::receiver R>
    auto connect(R r) && -> a::lvalue_sender::operation<R> { return {std::move(r)}; }
};

template<class S>
using awaitable_result = decltype(ex::as_awaitable(
    std::declval<S>(), std::declval<a::promise&>()));

using reference_alternatives = bridge_source<ex::set_value_t(int), ex::set_value_t(int&)>;
struct adapted_reference_alternatives : reference_alternatives {
    auto get_env() const noexcept -> a::adaptor_env<true, true> { return {}; }
};
struct source_adapting_to_multiple_values : bridge_source<ex::set_value_t(int)> {
    auto get_env() const noexcept {
        return ex::prop(ex::get_await_completion_adaptor,
            [](auto&&) noexcept { return reference_alternatives{}; });
    }
};
struct lvalue_adapted_sender : a::lvalue_sender {
    auto get_env() const noexcept -> a::adaptor_env<true, true> { return {}; }
};

// Distinct signatures stay distinct even when their decayed value types match.
static_assert(std::same_as<awaitable_result<reference_alternatives>, reference_alternatives&&>);
static_assert(std::same_as<awaitable_result<adapted_reference_alternatives>,
    adapted_reference_alternatives&&>);
static_assert(std::same_as<awaitable_result<a::adapted_sender<true, true>>, a::int_awaiter>);
// A single-value source still falls back if completion adaptation adds an alternative.
static_assert(std::same_as<awaitable_result<source_adapting_to_multiple_values>,
    source_adapting_to_multiple_values&&>);
// The transformed-member path uses the original sender expression's category.
static_assert(std::same_as<awaitable_result<lvalue_adapted_sender&>, a::int_awaiter>);
static_assert(std::same_as<awaitable_result<lvalue_adapted_sender>, lvalue_adapted_sender&&>);

template<bool SingleForLvalues>
struct category_values : a::lvalue_sender {
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> std::conditional_t<
        std::is_lvalue_reference_v<Self> == SingleForLvalues,
        ex::completion_signatures<ex::set_value_t(int)>,
        ex::completion_signatures<ex::set_value_t(int), ex::set_value_t(int&)>> { return {}; }
};

using adapted_single_lvalue = a::bridge_adaptor_source<category_values<true>>;
using adapted_multiple_lvalue = a::bridge_adaptor_source<category_values<false>>;
using lvalue_bridge = awaitable_result<adapted_single_lvalue>;
static_assert(std::same_as<decltype(std::declval<lvalue_bridge&>().await_resume()), int>);
static_assert(std::same_as<awaitable_result<adapted_multiple_lvalue>, adapted_multiple_lvalue&&>);
static_assert(std::same_as<awaitable_result<category_values<false>&>, category_values<false>&>);

// Keep the prepared sender and promise the same while enlarging the retained source.
struct padded_adapted_lvalue : adapted_single_lvalue {
    alignas(64) unsigned char padding[64]{};
};
using padded_lvalue_bridge = awaitable_result<padded_adapted_lvalue>;
static_assert(std::same_as<decltype(std::declval<padded_lvalue_bridge&>().await_resume()), int>);
static_assert(!std::same_as<lvalue_bridge, padded_lvalue_bridge>);

template<class Bridge, class Source>
concept directly_constructible_bridge = requires(Source&& source, a::promise& promise) {
    Bridge{std::forward<Source>(source), promise};
};
static_assert(!directly_constructible_bridge<lvalue_bridge, padded_adapted_lvalue>);

template<class S>
concept has_as_awaitable = requires(S&& s, a::promise& p) {
    ex::as_awaitable(std::forward<S>(s), p);
};
template<class S>
concept has_await_transform = requires(S&& s, a::mixin_promise& p) {
    p.await_transform(std::forward<S>(s));
};
using move_only_sender = decltype(ex::just(std::unique_ptr<int>{}));
static_assert(std::same_as<awaitable_result<move_only_sender&>, move_only_sender&>);
static_assert(std::same_as<awaitable_result<const move_only_sender&>, const move_only_sender&>);
// An eligible source adapting to an unconnectable lvalue is not an identity
// fallback. Detect the rejection without instantiating the awaitable body.
static_assert(!has_as_awaitable<a::bridge_adaptor_source<move_only_sender>>);
static_assert(!has_await_transform<a::bridge_adaptor_source<move_only_sender>>);

// A prepared rvalue with signatures but no rvalue connect is also rejected
// in dependent requires, instead of producing a hard instantiation error.
using unconnectable_rvalue = category_values<false>;
using connectable_rvalue = bridge_source<ex::set_value_t(int)>;
static_assert(ex::sender_in<unconnectable_rvalue, ex::env<>>);
static_assert(!has_as_awaitable<a::bridge_adaptor_source<unconnectable_rvalue, true>>);
static_assert(!has_await_transform<a::bridge_adaptor_source<unconnectable_rvalue, true>>);
static_assert(has_as_awaitable<a::bridge_adaptor_source<connectable_rvalue, true>>);
static_assert(has_await_transform<a::bridge_adaptor_source<connectable_rvalue, true>>);

struct immovable_operation {
    using operation_state_concept = ex::operation_state_tag;
    immovable_operation() = default;
    immovable_operation(const immovable_operation&) = delete;
    immovable_operation(immovable_operation&&) = delete;
    void start() & noexcept {}
};

struct immovable_operation_sender : a::lvalue_sender {
    template<ex::receiver R>
    auto connect(R) & noexcept -> immovable_operation { return {}; }
};

template<class T>
constexpr bool pinned = !std::is_copy_constructible_v<T> && !std::is_move_constructible_v<T> &&
    !std::is_copy_assignable_v<T> && !std::is_move_assignable_v<T>;

using immovable_op_bridge = awaitable_result<immovable_operation_sender&>;
using owned_bridge = awaitable_result<decltype(ex::just(1))>;
static_assert(ex::operation_state<immovable_operation>);
static_assert(std::same_as<ex::connect_result_t<immovable_operation_sender&, a::receiver>,
    immovable_operation>);
static_assert(pinned<immovable_op_bridge> && pinned<lvalue_bridge> && pinned<owned_bridge>);

template<bool HasAdlOperator>
struct member_co_await_sender : bridge_source<ex::set_value_t(int)> {
    auto operator co_await() && noexcept -> a::int_awaiter { return {}; }
};
auto operator co_await(member_co_await_sender<true>&&) noexcept -> a::int_awaiter {
    return {};
}

// Competing operators cannot win the ordinary-awaitable branch. A valid
// sender still takes the sender bridge, while a sole member is passed through.
using competing_bridge = awaitable_result<member_co_await_sender<true>>;
static_assert(std::same_as<decltype(std::declval<competing_bridge&>().await_resume()), int>);
static_assert(std::same_as<awaitable_result<member_co_await_sender<false>>,
    member_co_await_sender<false>&&>);
static_assert(std::same_as<awaitable_result<a::int_awaiter&>, a::int_awaiter&>);

static_assert(!ex::sender_in<a::lvalue_sender, ex::env<>>);
static_assert(ex::sender_in<a::lvalue_sender&, ex::env<>>);
static_assert(ex::sender<a::unstoppable_awaiter>);

int main() {
    awaitable_adl::result output;
    auto adl_op = ex::connect(a::adl_value_awaiter{}, awaitable_adl::receiver{&output});
    ex::start(adl_op);
    a::promise promise;
    using bridge_t = decltype(ex::as_awaitable(ex::just(1), std::declval<a::promise&>()));
    static_assert(std::destructible<bridge_t>); // Deleted operator& must not be used.
    auto direct = ex::as_awaitable(a::adapted_sender<true, true>{}, promise);
    [[maybe_unused]] auto suspend_immovable = &immovable_op_bridge::await_suspend;
    using stopped_bridge_t = decltype(ex::as_awaitable(ex::just_stopped(),
        std::declval<a::explicit_stopped_promise&>()));
    static_assert(std::destructible<stopped_bridge_t>);
    (void)direct;
}

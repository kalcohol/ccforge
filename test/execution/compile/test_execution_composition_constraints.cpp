#include <execution>
#include <utility>

// Keep the public missing-pipe control ahead of private implementation probes.
namespace composition_public_control {
struct callback {
    int operator()(int value) const noexcept { return value; }
};
using closure = decltype(std::execution::then(callback{}));
using composed = decltype(std::declval<closure>() | std::declval<closure>());
} // namespace composition_public_control

#include "../composition_probe.hpp"

#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

using namespace composition_probe;

using composed = decltype(std::declval<value_closure>() | std::declval<value_closure>());
static_assert(applicable<composed&>);
static_assert(applicable<const composed&>);
static_assert(applicable<composed>);
static_assert(applicable<const composed>);
static_assert(applicable<composed&, source_t&>);
static_assert(applicable<composed&, const source_t&>);
static_assert(rejected<composed&, int>);
static_assert(!ex::sender<composed>);
using expected = decltype(std::declval<value_closure&>()(
    std::declval<value_closure&>()(std::declval<source_t>())));
static_assert(std::same_as<std::invoke_result_t<composed&, source_t>, expected>);

template<class C>
consteval bool copyable_matrix() {
    return composable<C&, value_closure&> && composable<const C&, value_closure&> &&
        composable<C, value_closure&> && composable<const C, value_closure&> &&
        composable<value_closure&, C&> && composable<value_closure&, const C&> &&
        composable<value_closure&, C> && composable<value_closure&, const C>;
}

static_assert(copyable_matrix<value_closure>());
static_assert(copyable_matrix<decltype(ex::upon_error(value_callback{}))>());
static_assert(copyable_matrix<decltype(ex::upon_stopped([] { return 1; }))>());
static_assert(copyable_matrix<decltype(ex::let_value([](int value) { return ex::just(value); }))>());
static_assert(copyable_matrix<decltype(ex::let_error([](int error) { return ex::just(error); }))>());
static_assert(copyable_matrix<decltype(ex::let_stopped([] { return ex::just(1); }))>());
static_assert(copyable_matrix<decltype(ex::bulk(3, [](int, int&) {}))>());
static_assert(copyable_matrix<decltype(ex::bulk_unchunked(3, [](int, int&) {}))>());
static_assert(copyable_matrix<decltype(ex::bulk_chunked(3, [](int, int, int&) {}))>());
static_assert(copyable_matrix<decltype(ex::stopped_as_error(17))>());
static_assert(copyable_matrix<decltype(ex::stopped_as_optional)>());
static_assert(copyable_matrix<decltype(ex::stopped_as_optional())>());
static_assert(copyable_matrix<decltype(ex::into_variant)>());
static_assert(copyable_matrix<decltype(ex::into_variant())>());
static_assert(copyable_matrix<decltype(ex::unstoppable)>());
static_assert(copyable_matrix<decltype(ex::unstoppable())>());
static_assert(copyable_matrix<decltype(ex::affine)>());
static_assert(copyable_matrix<decltype(ex::write_env(ex::empty_env{}))>());
static_assert(copyable_matrix<decltype(ex::continues_on(ex::inline_scheduler{}))>());
static_assert(copyable_matrix<decltype(ex::on(ex::inline_scheduler{}, std::declval<value_closure>()))>());
static_assert(copyable_matrix<decltype(ex::associate(
    std::declval<ex::simple_counting_scope::scope_token>()))>());

template<class C>
consteval bool move_only_matrix() {
    static_assert(!composable<C&, value_closure>);
    static_assert(!composable<const C&, value_closure>);
    static_assert(composable<C, value_closure>);
    static_assert(!composable<const C, value_closure>);
    static_assert(!composable<value_closure, C&>);
    static_assert(!composable<value_closure, const C&>);
    static_assert(composable<value_closure, C>);
    static_assert(!composable<value_closure, const C>);
    using left = decltype(std::declval<C>() | std::declval<value_closure>());
    using right = decltype(std::declval<value_closure>() | std::declval<C>());
    static_assert(rejected<left&> && rejected<const left&> && applicable<left> && rejected<const left>);
    static_assert(rejected<right&> && rejected<const right&> && applicable<right> && rejected<const right>);
    return true;
}

static_assert(move_only_matrix<decltype(ex::then(move_only_callback<false>{}))>());
static_assert(move_only_matrix<decltype(ex::upon_error(move_only_callback<false>{}))>());
static_assert(move_only_matrix<decltype(ex::upon_stopped(move_only_callback<false>{}))>());
static_assert(move_only_matrix<decltype(ex::let_value(move_only_callback<true>{}))>());
static_assert(move_only_matrix<decltype(ex::let_error(move_only_callback<true>{}))>());
static_assert(move_only_matrix<decltype(ex::let_stopped(move_only_callback<true>{}))>());

using move_source = decltype(ex::just(std::unique_ptr<int>{}));
using move_composed = decltype(
    ex::then([](std::unique_ptr<int> value) noexcept { return value; }) |
    ex::then([](std::unique_ptr<int> value) noexcept { return value; }));
static_assert(applicable<move_composed&, move_source>);
static_assert(rejected<move_composed&, move_source&>);
static_assert(rejected<move_composed&, const move_source&>);
static_assert(composable<source_t, value_closure>);
static_assert(!ex::__forge_adaptor::__closure<source_t>);
static_assert(!composable<value_closure, source_t>);
static_assert(!composable<forwarding_identity, value_closure>);
static_assert(!composable<value_closure, forwarding_identity>);

struct fake_bulk {
    using policy_t = int;
    using fn_t = value_callback;
    int __shape_;
    template<ex::sender S> auto operator()(S source) const { return source; }
};
static_assert(!composable<fake_bulk, value_closure>);
static_assert(!composable<value_closure, fake_bulk>);

using reference_core = core<forwarding_identity, forwarding_identity>;
static_assert(std::same_as<std::invoke_result_t<reference_core&, source_t&>, source_t&>);
static_assert(std::same_as<std::invoke_result_t<const reference_core&, const source_t&>, const source_t&>);
static_assert(noexcept(std::declval<reference_core&>()(std::declval<source_t&>())));
static_assert(noexcept(std::declval<source_t&>() | std::declval<reference_core&>()));
static_assert(!noexcept(std::declval<core<category_handler<false>, category_handler<true>>&>()(
    std::declval<source_t&>())));
static_assert(!noexcept(std::declval<core<category_handler<true>, category_handler<false>>&>()(
    std::declval<source_t&>())));
static_assert(applicable<core<rvalue_only_handler, forwarding_identity>>);
static_assert(rejected<core<rvalue_only_handler, forwarding_identity>&>);
static_assert(rejected<const core<rvalue_only_handler, forwarding_identity>&>);
static_assert(rejected<const core<rvalue_only_handler, forwarding_identity>>);

consteval bool constexpr_reference_call() {
    reference_core closure{forwarding_identity{}, forwarding_identity{}};
    literal_source source{3};
    return &closure(source) == &source && closure(source).value == 3;
}
static_assert(constexpr_reference_call());
constexpr auto cpo_composition = ex::into_variant | ex::stopped_as_optional;
static_assert(!ex::sender<decltype(cpo_composition)>);

int main() {}

#include "awaitable_conformance_types.hpp"
#include <forge/any_sender.hpp>
#include <forge/async_scope.hpp>
#include <forge/erased_sender.hpp>
#include <forge/wait_result.hpp>
#include <forge/io/coro.hpp>
#include <forge/resource_context.hpp>
#include <forge/start_detached.hpp>
#include <forge/task.hpp>
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
static_assert(ex::sender_to<a::start_sender, a::start_receiver>);
static_assert(!noexcept(ex::connect(a::start_sender{}, a::start_receiver{})));
static_assert(ex::sender_to<a::adl_value_awaiter, awaitable_adl::receiver>);
static_assert(ex::sender<forge::task<int>> && ex::sender<forge::task<void>>);
static_assert(ex::sender<forge::__task_detail::__final_awaiter>);
static_assert(!ex::sender<forge::io::read_env_awaitable>);
static_assert(!ex::sender<forge::io::io_task<int>>);
static_assert(!ex::sender<forge::io::__coro_detail::sender_awaitable<decltype(ex::just())>>);
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

template<class S> concept scope_spawn = requires(forge::async_scope& scope, S&& s) {
    scope.spawn(static_cast<S&&>(s));
};
template<class S> concept resource_spawn = requires(forge::resource_context& context, S&& s) {
    context.spawn(static_cast<S&&>(s));
};
template<class S> concept detached = requires(S&& s) { forge::start_detached(static_cast<S&&>(s)); };
template<class S> concept io_await = requires(S&& s) { forge::io::await_sender(static_cast<S&&>(s)); };
template<class S> concept splittable = requires(S&& s) { forge::split(static_cast<S&&>(s)); };
using any_int = forge::any_sender_of<ex::completion_signatures<ex::set_value_t(int)>>;
static_assert(scope_spawn<a::int_awaiter> && resource_spawn<a::int_awaiter>);
static_assert(detached<a::int_awaiter> && io_await<a::int_awaiter> && splittable<a::int_awaiter>);
static_assert(!splittable<a::member_awaitable&>);
static_assert(splittable<a::member_awaitable>);
static_assert(!ex::sender_in<a::lvalue_sender, ex::env<>>);
static_assert(ex::sender_in<a::lvalue_sender&, ex::env<>>);
static_assert(std::constructible_from<any_int, a::lvalue_sender>);
static_assert(std::same_as<decltype(std::declval<any_int&>().sync_wait()),
    std::optional<std::tuple<int>>>);
static_assert(std::constructible_from<any_int, a::int_awaiter>);
static_assert(std::constructible_from<any_int, a::address_awaiter>);
static_assert(!std::constructible_from<any_int, a::void_awaiter>);
static_assert(!std::constructible_from<any_int, a::member_awaitable>); // Stored copyable S is borrowed as S&.
static_assert(ex::sender<a::unstoppable_awaiter>);
static_assert(detached<a::unstoppable_awaiter>);
static_assert(!scope_spawn<a::unstoppable_awaiter> && !resource_spawn<a::unstoppable_awaiter>);
static_assert(!io_await<a::unstoppable_awaiter>);
static_assert(!scope_spawn<a::not_awaitable> && !resource_spawn<a::not_awaitable>);
static_assert(!detached<a::not_awaitable> && !io_await<a::not_awaitable> && !splittable<a::not_awaitable>);

using erased_int = forge::erased_sender<int_sigs>;
using erased_narrow = forge::erased_sender<ex::completion_signatures<ex::set_value_t(int)>>;
static_assert(std::constructible_from<erased_int, a::int_awaiter>);
static_assert(!std::constructible_from<erased_narrow, a::int_awaiter>);
static_assert(!std::constructible_from<erased_int, a::member_awaitable>);
template<class S> concept result_waitable = requires(S&& s) {
    forge::wait_result_of(static_cast<S&&>(s));
    forge::wait_result(static_cast<S&&>(s));
};
static_assert(result_waitable<a::int_awaiter>);
static_assert(!result_waitable<a::unstoppable_awaiter>);
static_assert(!result_waitable<a::not_awaitable>);

int main() {
    awaitable_adl::result output;
    auto adl_op = ex::connect(a::adl_value_awaiter{}, awaitable_adl::receiver{&output});
    ex::start(adl_op);
    a::promise promise;
    using bridge_t = decltype(ex::as_awaitable(ex::just(1), std::declval<a::promise&>()));
    static_assert(std::destructible<bridge_t>); // Deleted operator& must not be used.
    auto direct = ex::as_awaitable(a::adapted_sender<true, true>{}, promise);
    using stopped_bridge_t = decltype(ex::as_awaitable(ex::just_stopped(),
        std::declval<a::explicit_stopped_promise&>()));
    static_assert(std::destructible<stopped_bridge_t>);
    (void)direct;
}

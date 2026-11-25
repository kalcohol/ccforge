#include "awaitable_conformance_types.hpp"

#if FORGE_TEST_QUERY_RUNTIME
#include <forge/async_scope.hpp>
#include <forge/resource_context.hpp>
#include <forge/split.hpp>
#include <forge/start_detached.hpp>
#include <forge/task.hpp>
#include <forge/wait_result.hpp>
#endif

#if FORGE_TEST_QUERY_IO
#include <forge/io/coro.hpp>
#endif

#if FORGE_TEST_QUERY_ERASURE
#include <forge/any_sender.hpp>
#include <forge/erased_sender.hpp>
#include <optional>
#include <tuple>
#endif

namespace {
namespace a = awaitable_tests;
namespace ex = std::execution;

#if FORGE_TEST_QUERY_RUNTIME
static_assert(ex::sender<forge::task<int>> && ex::sender<forge::task<void>>);
static_assert(ex::sender<forge::__task_detail::__final_awaiter>);

template<class S> concept scope_spawn = requires(forge::async_scope& scope, S&& s) {
    scope.spawn(static_cast<S&&>(s));
};
template<class S> concept resource_spawn = requires(forge::resource_context& context, S&& s) {
    context.spawn(static_cast<S&&>(s));
};
template<class S> concept detached = requires(S&& s) { forge::start_detached(static_cast<S&&>(s)); };
template<class S> concept splittable = requires(S&& s) { forge::split(static_cast<S&&>(s)); };
static_assert(scope_spawn<a::int_awaiter> && resource_spawn<a::int_awaiter>);
static_assert(detached<a::int_awaiter> && splittable<a::int_awaiter>);
static_assert(!splittable<a::member_awaitable&>);
static_assert(splittable<a::member_awaitable>);
static_assert(detached<a::unstoppable_awaiter>);
static_assert(!scope_spawn<a::unstoppable_awaiter> && !resource_spawn<a::unstoppable_awaiter>);
static_assert(!scope_spawn<a::not_awaitable> && !resource_spawn<a::not_awaitable>);
static_assert(!detached<a::not_awaitable> && !splittable<a::not_awaitable>);

template<class S> concept result_waitable = requires(S&& s) {
    forge::wait_result_of(static_cast<S&&>(s));
    forge::wait_result(static_cast<S&&>(s));
};
static_assert(result_waitable<a::int_awaiter>);
static_assert(!result_waitable<a::unstoppable_awaiter>);
static_assert(!result_waitable<a::not_awaitable>);
#endif

#if FORGE_TEST_QUERY_IO
static_assert(!ex::sender<forge::io::read_env_awaitable>);
static_assert(!ex::sender<forge::io::io_task<int>>);
static_assert(!ex::sender<forge::io::__coro_detail::sender_awaitable<decltype(ex::just())>>);

template<class S> concept io_await = requires(S&& s) { forge::io::await_sender(static_cast<S&&>(s)); };
static_assert(io_await<a::int_awaiter>);
static_assert(!io_await<a::unstoppable_awaiter>);
static_assert(!io_await<a::not_awaitable>);
#endif

#if FORGE_TEST_QUERY_ERASURE
using any_int = forge::any_sender_of<ex::completion_signatures<ex::set_value_t(int)>>;
static_assert(std::constructible_from<any_int, a::lvalue_sender>);
static_assert(std::same_as<decltype(std::declval<any_int&>().sync_wait()),
    std::optional<std::tuple<int>>>);
static_assert(std::constructible_from<any_int, a::int_awaiter>);
static_assert(std::constructible_from<any_int, a::address_awaiter>);
static_assert(!std::constructible_from<any_int, a::void_awaiter>);
static_assert(!std::constructible_from<any_int, a::member_awaitable>); // Stored copyable S is borrowed as S&.

using int_sigs = ex::completion_signatures<ex::set_value_t(int),
    ex::set_error_t(std::exception_ptr), ex::set_stopped_t()>;
using erased_int = forge::erased_sender<int_sigs>;
using erased_narrow = forge::erased_sender<ex::completion_signatures<ex::set_value_t(int)>>;
static_assert(std::constructible_from<erased_int, a::int_awaiter>);
static_assert(!std::constructible_from<erased_narrow, a::int_awaiter>);
static_assert(!std::constructible_from<erased_int, a::member_awaitable>);
#endif
} // namespace

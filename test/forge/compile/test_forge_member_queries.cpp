#include <execution>
#include <concepts>
#include <type_traits>
#include <utility>

// Check direct members as well as CPO dispatch: defaults can silently hide a
// missing stop-token answer in a native implementation.
template<class Env, class Query, class Result>
concept member_answer = requires(const Env& e) {
    { e.query(Query{}) } noexcept -> std::same_as<Result>;
};
using stop_query = std::execution::get_stop_token_t;
using completion_query = std::execution::get_completion_scheduler_t<
    std::execution::set_value_t>;

#if FORGE_TEST_QUERY_RUNTIME
#include <forge/async_scope.hpp>
#include <forge/static_thread_pool.hpp>
#include <forge/strand.hpp>
#include <forge/task.hpp>
static_assert(member_answer<forge::__async_scope_detail::__env,
    stop_query, std::inplace_stop_token>);
static_assert(member_answer<forge::__pool_detail::__env,
    completion_query, forge::static_thread_pool::scheduler>);
static_assert(member_answer<forge::static_thread_pool::scheduler,
    std::execution::get_forward_progress_guarantee_t,
    std::execution::forward_progress_guarantee>);
static_assert(member_answer<forge::__strand_detail::__env,
    completion_query, forge::strand::scheduler>);
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
static_assert(member_answer<forge::__task_detail::__inline_fallback::__env,
    completion_query, forge::__task_detail::__inline_fallback_scheduler>);
static_assert(member_answer<forge::__task_detail::__env,
    stop_query, forge::any_stop_token>);
static_assert(member_answer<forge::__task_detail::__env,
    std::execution::get_start_scheduler_t, forge::any_scheduler>);
#endif
#endif

#if FORGE_TEST_QUERY_ERASURE
#include <forge/any_receiver.hpp>
#include <forge/any_scheduler.hpp>
#include <forge/erased_sender.hpp>
using cs = std::execution::completion_signatures<
    std::execution::set_value_t(), std::execution::set_error_t(std::exception_ptr),
    std::execution::set_stopped_t()>;
using receiver_env = decltype(std::declval<const forge::any_receiver_of<cs>&>().get_env());
static_assert(member_answer<receiver_env, stop_query, forge::any_stop_token>);
static_assert(member_answer<forge::__erased_sender_detail::__receiver<cs>::__env,
    stop_query, forge::any_stop_token>);
static_assert(member_answer<forge::__any_scheduler_detail::__sender::__env,
    completion_query, forge::any_scheduler>);
#endif

#if FORGE_TEST_QUERY_IO
#include <forge/io/detail/combinator_common.hpp>
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
struct child_state {
    auto stop_token() const noexcept -> std::inplace_stop_token { return {}; }
};
using child_env = forge::io::__io_combinator_detail::child_receiver<
    0, child_state, int>::env;
static_assert(member_answer<child_env, stop_query, std::inplace_stop_token>);
using await_env = forge::io::__coro_detail::await_sender_receiver_env;
static_assert(member_answer<await_env, stop_query, std::inplace_stop_token>);
static_assert(member_answer<await_env, std::execution::get_scheduler_t,
    forge::any_scheduler>);
#endif
#endif

#if defined(FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES) && FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES
// This check also proves the backport's test switch disables fallback rather
// than merely preferring members. Native get_stop_token has the same default.
struct legacy_env {
    friend auto tag_invoke(stop_query, const legacy_env&) noexcept
        -> std::inplace_stop_token { return {}; }
};
static_assert(std::same_as<decltype(std::execution::get_stop_token(legacy_env{})),
    std::never_stop_token>);
#endif
int main() {}

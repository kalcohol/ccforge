#include <execution>
#include <forge/static_thread_pool.hpp>
#include <forge/strand.hpp>

#include <concepts>
#include <utility>

namespace {
namespace ex = std::execution;
using empty = ex::env<>;
using stop_env = decltype(ex::prop{std::get_stop_token, std::inplace_stop_token{}});

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
static_assert(!ex::sender_in<affine_value, pool_env>);
static_assert(!ex::sender_in<affine_value, strand_env>);
static_assert(!ex::sender_in<affine_value, never_start<pool_env>>);
static_assert(!ex::sender_in<affine_value, never_start<strand_env>>);
static_assert(ex::sender_in<affine_value, stoppable_start<pool_env>>);
static_assert(ex::sender_in<affine_value, stoppable_start<strand_env>>);
} // namespace

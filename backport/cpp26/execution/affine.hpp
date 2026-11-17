// MIT License
//
// Copyright (c) 2026 CC Forge Project
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "sender_adaptor_closure.hpp"
#include "concepts.hpp"
#include "continues_on.hpp"
#include "env.hpp"
#include "unstoppable.hpp"

#include <type_traits>
#include <utility>

namespace std::execution {

namespace __forge_affine {

template<class Scheduler>
struct __unstoppable_scheduler {
    using scheduler_concept = scheduler_t;

    Scheduler __scheduler;

    template<class Query, class... As>
        requires __forge_env_detail::__queryable<Query, const Scheduler&, As...>
    constexpr auto query(Query q, As&&... as) const
        noexcept(__forge_env_detail::__nothrow_query<Query, const Scheduler&, As...>)
        -> decltype(__forge_env_detail::__query(q, __scheduler, static_cast<As&&>(as)...)) {
        return __forge_env_detail::__query(q, __scheduler, static_cast<As&&>(as)...);
    }
    template<class Query, class... As>
        requires __forge_env_detail::__queryable<Query, const Scheduler&, As...>
    friend constexpr auto tag_invoke(Query q, const __unstoppable_scheduler& self, As&&... as)
        noexcept(noexcept(self.query(q, static_cast<As&&>(as)...)))
        -> decltype(self.query(q, static_cast<As&&>(as)...)) {
        return self.query(q, static_cast<As&&>(as)...);
    }

    template<class Sch = Scheduler>
        requires std::same_as<Sch, Scheduler>
    [[nodiscard]] auto schedule() &
        noexcept(noexcept(std::execution::unstoppable(std::execution::schedule(__scheduler))))
        -> decltype(std::execution::unstoppable(std::execution::schedule(std::declval<Sch&>())))
        requires requires(Scheduler& sch) { std::execution::schedule(sch); } {
        return std::execution::unstoppable(std::execution::schedule(__scheduler));
    }
    template<class Sch = Scheduler>
        requires std::same_as<Sch, Scheduler>
    [[nodiscard]] auto schedule() const&
        noexcept(noexcept(std::execution::unstoppable(std::execution::schedule(__scheduler))))
        -> decltype(std::execution::unstoppable(std::execution::schedule(std::declval<const Sch&>())))
        requires requires(const Scheduler& sch) { std::execution::schedule(sch); } {
        return std::execution::unstoppable(std::execution::schedule(__scheduler));
    }
    template<class Sch = Scheduler>
        requires std::same_as<Sch, Scheduler>
    [[nodiscard]] auto schedule() &&
        noexcept(noexcept(std::execution::unstoppable(std::execution::schedule(std::move(__scheduler)))))
        -> decltype(std::execution::unstoppable(std::execution::schedule(std::declval<Sch&&>())))
        requires requires(Scheduler&& sch) { std::execution::schedule(std::move(sch)); } {
        return std::execution::unstoppable(std::execution::schedule(std::move(__scheduler)));
    }

    friend bool operator==(
        const __unstoppable_scheduler&,
        const __unstoppable_scheduler&) noexcept = default;
};

template<class Sch, class Env>
concept __infallible_scheduler = scheduler<Sch> && sender_in<schedule_result_t<Sch>, Env> &&
    (std::same_as<completion_signatures_of_t<schedule_result_t<Sch>, Env>,
                  completion_signatures<set_value_t()>> ||
     (!std::unstoppable_token<std::remove_cvref_t<std::stop_token_of_t<Env>>> &&
      (std::same_as<completion_signatures_of_t<schedule_result_t<Sch>, Env>,
                    completion_signatures<set_value_t(), set_stopped_t()>> ||
       std::same_as<completion_signatures_of_t<schedule_result_t<Sch>, Env>,
                    completion_signatures<set_stopped_t(), set_value_t()>>)));

template<class Env>
concept __valid_env = requires { std::execution::get_start_scheduler(std::declval<Env>()); } &&
    __infallible_scheduler<decltype(std::execution::get_start_scheduler(std::declval<Env>())), Env>;

template<class S, class Env, bool = requires { std::declval<S>().affine(); }>
struct __result {};

template<class S, class Env>
    requires std::constructible_from<std::decay_t<S>, S>
struct __result<S, Env, false> {
    using scheduler = std::remove_cvref_t<decltype(std::execution::get_start_scheduler(std::declval<Env>()))>;
    using type = decltype(std::execution::continues_on(std::declval<S>(),
        std::declval<__unstoppable_scheduler<scheduler>>()));
};

template<class S, class Env>
struct __result<S, Env, true> { using type = decltype(std::declval<S>().affine()); };

template<class S, class Env>
    requires __valid_env<Env>
using __result_t = typename __result<S, Env>::type;

template<class S, class Env>
auto __lower(S&& source, const Env& environment) -> __result_t<S, Env> {
    if constexpr (requires { static_cast<S&&>(source).affine(); }) {
        return static_cast<S&&>(source).affine();
    } else {
        auto sch = std::execution::get_start_scheduler(environment);
        return std::execution::continues_on(static_cast<S&&>(source),
            __unstoppable_scheduler<std::remove_cvref_t<decltype(sch)>>{std::move(sch)});
    }
}

template<class S>
struct __sender {
    using sender_concept = sender_t;
    using source_t = S;
    S __source;

    template<class Self>
    using __child_t = decltype(std::forward_like<Self>(std::declval<S&>()));

    template<class Self, class Env>
        requires __valid_env<Env> &&
                 (std::copy_constructible<S> ||
                  (std::is_rvalue_reference_v<Self&&> && !std::is_const_v<std::remove_reference_t<Self>>)) &&
                 sender_in<__result_t<__child_t<Self>, Env>, Env>
    static auto get_completion_signatures() noexcept
        -> completion_signatures_of_t<__result_t<__child_t<Self>, Env>, Env> { return {}; }

    template<receiver R>
        requires std::copy_constructible<S> && __valid_env<env_of_t<R>> &&
                 sender_in<__result_t<S&, env_of_t<R>>, env_of_t<R>>
    auto connect(R receiver) & -> connect_result_t<__result_t<S&, env_of_t<R>>, R> {
        return std::execution::connect(
            __lower(__source, std::execution::get_env(receiver)), std::move(receiver));
    }

    template<receiver R>
        requires __valid_env<env_of_t<R>> && sender_in<__result_t<S, env_of_t<R>>, env_of_t<R>>
    auto connect(R receiver) && -> connect_result_t<__result_t<S, env_of_t<R>>, R> {
        return std::execution::connect(
            __lower(std::move(__source), std::execution::get_env(receiver)), std::move(receiver));
    }

    template<receiver R>
        requires std::copy_constructible<S> && __valid_env<env_of_t<R>> &&
                 sender_in<__result_t<const S&, env_of_t<R>>, env_of_t<R>>
    auto connect(R receiver) const& -> connect_result_t<__result_t<const S&, env_of_t<R>>, R> {
        return std::execution::connect(__lower(__source, std::execution::get_env(receiver)), std::move(receiver));
    }
    template<receiver R>
        requires std::copy_constructible<S> && __valid_env<env_of_t<R>> &&
                 sender_in<__result_t<const S, env_of_t<R>>, env_of_t<R>>
    auto connect(R receiver) const&& -> connect_result_t<__result_t<const S, env_of_t<R>>, R> {
        return std::execution::connect(
            __lower(std::move(__source), std::execution::get_env(receiver)), std::move(receiver));
    }
    template<class R> void connect(R) & = delete;
    template<class R> void connect(R) const& = delete;
    template<class R> void connect(R) && = delete;
    template<class R> void connect(R) const&& = delete;
    auto get_env() const noexcept -> __empty_env { return {}; }
};

} // namespace __forge_affine

struct affine_t : sender_adaptor_closure<affine_t> {
    template<sender S>
    [[nodiscard]] auto operator()(S&& sndr) const {
        return __forge_affine::__sender<std::decay_t<S>>{
            __forge_detail::__forward_as_given(std::forward<S>(sndr))};
    }
};

inline constexpr affine_t affine{};

} // namespace std::execution

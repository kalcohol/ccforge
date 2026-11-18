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
#include "env.hpp"
#include "just.hpp"
#include "run_loop.hpp"
#include "schedule_from.hpp"

#include <cstddef>
#include <exception>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace std::execution {

namespace __forge_continues_on {

// Each completion alternative owns its decayed datums until the hop completes.
template<class Sig>
struct __result_sig;

template<class Tag, class... As>
struct __result_sig<Tag(As...)> {
    using tuple = std::tuple<Tag, std::decay_t<As>...>;
    using completion = completion_signatures<Tag(std::decay_t<As>...)>;
    static constexpr bool nothrow =
        (std::is_nothrow_constructible_v<std::decay_t<As>, As> && ...);
};

template<class List>
struct __as_variant;

template<class... Ts>
struct __as_variant<__forge_meta::type_list<Ts...>> {
    using type = __forge_meta::list_to_variant_t<std::variant,
        __forge_meta::list_push_unique_all_t<__forge_meta::type_list<>, Ts...>>;
};

template<class CS>
struct __results;

template<class... Sigs>
struct __results<completion_signatures<Sigs...>> {
    static constexpr bool nothrow = (__result_sig<Sigs>::nothrow && ...);
    using exception_completion = std::conditional_t<nothrow,
        completion_signatures<>, completion_signatures<set_error_t(std::exception_ptr)>>;
    using source_completions = __forge_meta::__concat_unique_cs_t<
        typename __result_sig<Sigs>::completion...>;
    using alternatives = __forge_meta::list_push_unique_all_t<
        __forge_meta::type_list<std::monostate>, typename __result_sig<Sigs>::tuple...>;
    using with_exception = std::conditional_t<nothrow, alternatives,
        __forge_meta::list_push_unique_t<alternatives, std::tuple<set_error_t, std::exception_ptr>>>;
    using variant = typename __as_variant<with_exception>::type;
};

template<class Scheduler, class S, class Env, class = void>
struct __completion_sigs {};

template<class Scheduler, class S, class Env>
    requires sender_in<S, __forge_detail::__fwd_env_t<Env>> &&
             sender_in<schedule_result_t<Scheduler&>, __forge_detail::__fwd_env_t<Env>> &&
             __forge_detail::__decay_copyable_cs<
                 completion_signatures_of_t<S, __forge_detail::__fwd_env_t<Env>>>::value
struct __completion_sigs<Scheduler, S, Env, void> {
    using source = __results<completion_signatures_of_t<S, __forge_detail::__fwd_env_t<Env>>>;
    using scheduled = completion_signatures_of_t<schedule_result_t<Scheduler&>,
                                                __forge_detail::__fwd_env_t<Env>>;
    using type = __forge_meta::__concat_unique_cs_t<typename source::source_completions,
        __forge_meta::__non_value_completion_signatures_t<scheduled>, typename source::exception_completion>;
};

template<class Tag, class Sig>
struct __has_tag : std::false_type {};

template<class Tag, class... As>
struct __has_tag<Tag, Tag(As...)> : std::true_type {};

template<class Tag, class CS>
struct __has_completion : std::false_type {};

template<class Tag, class... Sigs>
struct __has_completion<Tag, completion_signatures<Sigs...>>
    : std::bool_constant<(__has_tag<Tag, Sigs>::value || ...)> {};

template<class... Envs>
struct __first_env { using type = env<>; };

template<class Env, class... Rest>
struct __first_env<Env, Rest...> { using type = Env; };

template<class Scheduler, class S, class R>
struct __op;

template<class Scheduler, class S, class R>
struct __sched_recv {
    using receiver_concept = receiver_tag;
    __op<Scheduler, S, R>* __self;
    R* __outer;

    void set_value() && noexcept {
        // The receiver can destroy the entire operation during this call.
        // Neither visitor nor caller may inspect the state afterwards.
        std::visit([outer = std::addressof(__self->__outer)](auto& result) noexcept {
            using tuple_t = std::remove_cvref_t<decltype(result)>;
            if constexpr (!std::same_as<tuple_t, std::monostate>) {
                std::apply([outer](auto tag, auto&... args) noexcept {
                    tag(std::move(*outer), std::move(args)...);
                }, result);
            }
        }, __self->__result);
    }
    template<class E>
    void set_error(E&& error) && noexcept {
        std::execution::set_error(std::move(__self->__outer), static_cast<E&&>(error));
    }
    void set_stopped() && noexcept {
        std::execution::set_stopped(std::move(__self->__outer));
    }
    auto get_env() const noexcept -> __forge_detail::__fwd_env_t<env_of_t<R>> {
        return {std::execution::get_env(*__outer)};
    }
};

template<class Scheduler, class S, class R>
struct __up_recv {
    using receiver_concept = receiver_tag;
    __op<Scheduler, S, R>* __self;
    R* __outer;

    template<class... As>
    void set_value(As&&... args) && noexcept {
        __self->__save_and_schedule(set_value_t{}, static_cast<As&&>(args)...);
    }
    template<class E>
    void set_error(E&& error) && noexcept {
        __self->__save_and_schedule(set_error_t{}, static_cast<E&&>(error));
    }
    void set_stopped() && noexcept {
        __self->__save_and_schedule(set_stopped_t{});
    }
    auto get_env() const noexcept -> __forge_detail::__fwd_env_t<env_of_t<R>> {
        return {std::execution::get_env(*__outer)};
    }
};

template<class Scheduler, class S, class R>
concept __connectable = requires {
    typename connect_result_t<S, __up_recv<Scheduler, S, R>>;
    typename connect_result_t<decltype(std::execution::schedule(std::declval<Scheduler&>())),
        __sched_recv<Scheduler, S, R>>;
};

template<class Scheduler, class S, class R>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_tag;
    using __env_t = __forge_env_detail::__forwarding_attrs<env_of_t<R>>;
    using __results_t = __results<completion_signatures_of_t<S, __env_t>>;

    using __sched_recv = __forge_continues_on::__sched_recv<Scheduler, S, R>;

    template<class Tag, class... As>
    void __save_and_schedule(Tag tag, As&&... args) noexcept {
        using result_t = std::tuple<Tag, std::decay_t<As>...>;
        constexpr bool nothrow =
            (std::is_nothrow_constructible_v<std::decay_t<As>, As> && ...);
        if constexpr (nothrow) {
            __result.template emplace<result_t>(tag, static_cast<As&&>(args)...);
        } else {
            try {
                __result.template emplace<result_t>(tag, static_cast<As&&>(args)...);
            } catch (...) {
                __result.template emplace<std::tuple<set_error_t, std::exception_ptr>>(
                    set_error_t{}, std::current_exception());
            }
        }
        std::execution::start(__sched_op);
    }

    using __up_recv = __forge_continues_on::__up_recv<Scheduler, S, R>;

    using __sched_op_t = connect_result_t<decltype(
        std::execution::schedule(std::declval<Scheduler&>())), __sched_recv>;
    using __up_op_t = connect_result_t<S, __up_recv>;

    R __outer;
    typename __results_t::variant __result;
    // Destroy both operations before their borrowed result and receiver.
    __sched_op_t __sched_op;
    __up_op_t __up_op;

    __op(Scheduler sch, S sndr, R recv)
        : __outer(std::move(recv))
        , __sched_op(std::execution::connect(std::execution::schedule(sch), __sched_recv{this, std::addressof(__outer)}))
        , __up_op(std::execution::connect(std::move(sndr), __up_recv{this, std::addressof(__outer)}))
    {}

    void start() & noexcept {
        std::execution::start(__up_op);
    }
};

template<class Query>
inline constexpr bool __completion_query_v = false;

template<class CPO>
inline constexpr bool __completion_query_v<get_completion_scheduler_t<CPO>> = true;

template<class CPO>
inline constexpr bool __completion_query_v<get_completion_domain_t<CPO>> = true;

// Borrow query arguments without copying their state, and expose the same
// forwarding queries as the connected receivers.
template<class Env>
using __query_env_t = __forge_env_detail::__forwarding_attrs<Env&&>;

template<class Scheduler, class ChildEnv, class Source>
struct __completion_attrs;

} // namespace __forge_continues_on

namespace __forge_domain {

// An unavailable domain member must not let the public CPO retry the
// destination scheduler with queries hidden from the schedule receiver.
template<class Scheduler, class ChildEnv, class Source, class Env>
struct __completion_domain_env<
    __forge_continues_on::__completion_attrs<Scheduler, ChildEnv, Source>, Env>
    : std::type_identity<__forge_continues_on::__query_env_t<Env>> {};

} // namespace __forge_domain

namespace __forge_continues_on {

template<class Scheduler, class ChildEnv, class Source>
struct __completion_attrs {
    using __schedule_sender_t = decltype(
        std::execution::schedule(std::declval<Scheduler&>()));
    using __schedule_attrs_t = env_of_t<__schedule_sender_t>;

    template<class CPO, class... Envs>
    static consteval bool __domain_query_nothrow() {
        if constexpr (__forge_env_detail::__queryable<
                          get_completion_domain_t<CPO>,
                          const __schedule_attrs_t&,
                          __query_env_t<Envs>...>) {
            return noexcept(std::execution::schedule(
                       std::declval<Scheduler&>())) &&
                std::is_nothrow_copy_constructible_v<Scheduler> &&
                noexcept(std::execution::get_env(
                    std::declval<const __schedule_sender_t&>())) &&
                __forge_env_detail::__nothrow_query<
                    get_completion_domain_t<CPO>,
                    const __schedule_attrs_t&,
                    __query_env_t<Envs>...>;
        } else {
            return __forge_env_detail::__nothrow_query<
                get_completion_domain_t<CPO>,
                const Scheduler&,
                __query_env_t<Envs>...>;
        }
    }

    [[no_unique_address]] Scheduler __sch;
    [[no_unique_address]] ChildEnv __child;

    template<class CPO, class... Envs>
    static consteval bool __can_query() {
        using environment = typename __first_env<Envs...>::type;
        if constexpr (requires { typename __completion_sigs<Scheduler, Source, environment>::type; }) {
            using signatures = typename __completion_sigs<Scheduler, Source, environment>::type;
            using scheduled = typename __completion_sigs<Scheduler, Source, environment>::scheduled;
            if constexpr (std::same_as<CPO, void>) return true;
            else return __has_completion<CPO, signatures>::value &&
                (!std::same_as<CPO, set_error_t> || !__has_completion<set_error_t, scheduled>::value);
        } else return false;
    }

    template<class CPO, class... Envs>
        requires (__can_query<CPO, Envs...>())
    constexpr auto query(get_completion_scheduler_t<CPO>, Envs&&...) const noexcept -> Scheduler {
        return __sch;
    }

    template<class CPO, class... Envs>
        requires (__can_query<CPO, Envs...>()) &&
                 (__forge_env_detail::__queryable<
                      get_completion_domain_t<CPO>,
                      const __schedule_attrs_t&,
                      __query_env_t<Envs>...> ||
                  __forge_env_detail::__queryable<
                      get_completion_domain_t<CPO>,
                      const Scheduler&,
                      __query_env_t<Envs>...>)
    auto query(get_completion_domain_t<CPO>, Envs&&... envs) const
        noexcept(__domain_query_nothrow<CPO, Envs...>()) {
        if constexpr (__forge_env_detail::__queryable<
                          get_completion_domain_t<CPO>,
                          const __schedule_attrs_t&,
                          __query_env_t<Envs>...>) {
            auto scheduler = __sch;
            auto sched_sender = std::execution::schedule(scheduler);
            auto attrs = std::execution::get_env(sched_sender);
            return std::execution::get_completion_domain<CPO>(
                attrs, __query_env_t<Envs>{static_cast<Envs&&>(envs)}...);
        } else if constexpr (__forge_env_detail::__queryable<
                                 get_completion_domain_t<CPO>,
                                 const Scheduler&,
                                 __query_env_t<Envs>...>) {
            return std::execution::get_completion_domain<CPO>(
                __sch, __query_env_t<Envs>{static_cast<Envs&&>(envs)}...);
        }
    }

    // Completion queries belong to the destination even when its answer is
    // unavailable. FWD-ENV alone would also forward the child's domain tags.
    template<class Query, class... Args>
        requires (!__completion_query_v<std::remove_cvref_t<Query>>) &&
                 __forge_env_detail::__fwd_env_query<Query> &&
                 __forge_env_detail::__queryable<Query, const ChildEnv&, Args...>
    constexpr decltype(auto) query(Query q, Args&&... args) const
        noexcept(__forge_env_detail::__nothrow_query<Query, const ChildEnv&, Args...>) {
        return __forge_env_detail::__query(std::move(q), __child,
                                           static_cast<Args&&>(args)...);
    }

    template<class Query, class... Args>
        requires requires(const __completion_attrs& attrs, Query q, Args&&... args) {
            attrs.query(q, static_cast<Args&&>(args)...);
        }
    friend decltype(auto) tag_invoke(Query q, const __completion_attrs& self, Args&&... args)
        noexcept(noexcept(self.query(q, static_cast<Args&&>(args)...))) {
        return self.query(q, static_cast<Args&&>(args)...);
    }
};

// Keep the public CPO fallback in agreement with the environment-specific
// member constraints, including through ordinary forwarding attributes.
} // namespace __forge_continues_on
namespace __forge_env_detail {
template<class Scheduler, class ChildEnv, class Source, class Tag, class... Envs>
struct __completion_query_allowed<
    __forge_continues_on::__completion_attrs<Scheduler, ChildEnv, Source>, Tag, Envs...>
    : std::bool_constant<__forge_continues_on::__completion_attrs<
          Scheduler, ChildEnv, Source>::template __can_query<Tag, Envs...>()> {};
} // namespace __forge_env_detail
namespace __forge_continues_on {

template<class Scheduler, class S>
struct __sender {
    using sender_concept = sender_tag;
    using source_t = S;

    Scheduler __sch;
    S __sndr;

    template<class Env>
    using __cs = typename __completion_sigs<Scheduler, S, Env>::type;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> __cs<Env> { return {}; }

    template<receiver R>
        requires requires { typename __cs<env_of_t<R>>; } && __connectable<Scheduler, S, R>
    auto connect(R r) && -> __op<Scheduler, S, R> {
        return __op<Scheduler, S, R>(std::move(__sch), std::move(__sndr), std::move(r));
    }

    template<receiver R>
        requires std::copy_constructible<Scheduler> && std::copy_constructible<S> &&
                 requires { typename __cs<env_of_t<R>>; } && __connectable<Scheduler, S, R>
    auto connect(R r) const& -> __op<Scheduler, S, R> {
        return __op<Scheduler, S, R>(__sch, __sndr, std::move(r));
    }

    auto get_env() const noexcept {
        return __completion_attrs<Scheduler, env_of_t<S>, S>{
            __sch,
            std::execution::get_env(__sndr)};
    }
};

template<class Scheduler>
struct __closure : sender_adaptor_closure<__closure<Scheduler>> {
    Scheduler __scheduler;

    template<sender S>
        requires std::copy_constructible<Scheduler>
    [[nodiscard]] auto operator()(S&& sndr) const&;

    template<sender S>
    [[nodiscard]] auto operator()(S&& sndr) &&;
};

} // namespace __forge_continues_on

struct continues_on_t {
    template<sender S, class Scheduler>
        requires scheduler<Scheduler>
    [[nodiscard]] auto operator()(S&& sndr, Scheduler&& sch) const {
        auto departure = std::execution::schedule_from(
            std::forward<S>(sndr));
        return __forge_continues_on::__sender<
            std::remove_cvref_t<Scheduler>, decltype(departure)>{
            __forge_detail::__forward_as_given(std::forward<Scheduler>(sch)),
            std::move(departure)};
    }

    template<class Scheduler>
        requires scheduler<Scheduler>
    [[nodiscard]] auto operator()(Scheduler&& sch) const {
        return __forge_continues_on::__closure<std::remove_cvref_t<Scheduler>>{
            {}, __forge_detail::__forward_as_given(std::forward<Scheduler>(sch))};
    }
};

inline constexpr continues_on_t continues_on{};

template<class Scheduler>
template<sender S>
    requires std::copy_constructible<Scheduler>
[[nodiscard]] auto __forge_continues_on::__closure<Scheduler>::operator()(
    S&& sndr) const& {
    return std::execution::continues_on(std::forward<S>(sndr), __scheduler);
}

template<class Scheduler>
template<sender S>
[[nodiscard]] auto __forge_continues_on::__closure<Scheduler>::operator()(
    S&& sndr) && {
    return std::execution::continues_on(
        std::forward<S>(sndr), std::move(__scheduler));
}

template<scheduler Scheduler, class... Vs>
    requires (std::move_constructible<std::decay_t<Vs>> && ...)
[[deprecated("use forge::transfer_just or continues_on(just(...), scheduler)")]]
[[nodiscard]] auto transfer_just(Scheduler sch, Vs&&... vs) {
    return std::execution::continues_on(
        std::execution::just(std::forward<Vs>(vs)...), std::move(sch));
}

} // namespace std::execution

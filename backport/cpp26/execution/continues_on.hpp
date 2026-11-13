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
    using type = std::variant<Ts...>;
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

template<class Scheduler, class S, class R>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;
    using __results_t = __results<completion_signatures_of_t<S, env_of_t<R>>>;

    struct __sched_recv {
        using receiver_concept = receiver_t;
        __op* __self;

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
        auto get_env() const noexcept -> env_of_t<R> {
            return std::execution::get_env(__self->__outer);
        }
    };

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

    struct __up_recv {
        using receiver_concept = receiver_t;
        __op* __self;

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
        auto get_env() const noexcept -> env_of_t<R> {
            return std::execution::get_env(__self->__outer);
        }
    };

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
        , __sched_op(std::execution::connect(std::execution::schedule(sch), __sched_recv{this}))
        , __up_op(std::execution::connect(std::move(sndr), __up_recv{this}))
    {}

    void start() & noexcept {
        std::execution::start(__up_op);
    }
};

template<class Query>
inline constexpr bool __completion_scheduler_query_v =
    std::same_as<
        std::remove_cvref_t<Query>,
        get_completion_scheduler_t<set_value_t>> ||
    std::same_as<
        std::remove_cvref_t<Query>,
        get_completion_scheduler_t<set_error_t>> ||
    std::same_as<
        std::remove_cvref_t<Query>,
        get_completion_scheduler_t<set_stopped_t>>;

template<class Query>
consteval bool __is_forwarding_query() {
    using query_t = std::remove_cvref_t<Query>;
    if constexpr (std::default_initializable<query_t>) {
        return std::forwarding_query(query_t{});
    } else {
        return false;
    }
}

template<class Scheduler, class ChildEnv>
struct __completion_attrs {
    using __schedule_sender_t = decltype(
        std::execution::schedule(std::declval<Scheduler&>()));
    using __schedule_attrs_t = env_of_t<__schedule_sender_t>;

    template<class CPO, class Env>
    static consteval bool __domain_query_nothrow() {
        if constexpr (__forge_env_detail::__queryable<
                          get_completion_domain_t<CPO>,
                          const __schedule_attrs_t&,
                          Env>) {
            return noexcept(std::execution::schedule(
                       std::declval<Scheduler&>())) &&
                std::is_nothrow_copy_constructible_v<Scheduler> &&
                noexcept(std::execution::get_env(
                    std::declval<const __schedule_sender_t&>())) &&
                __forge_env_detail::__nothrow_query<
                    get_completion_domain_t<CPO>,
                    const __schedule_attrs_t&,
                    Env>;
        } else {
            return __forge_env_detail::__nothrow_query<
                get_completion_domain_t<CPO>,
                const Scheduler&,
                Env>;
        }
    }

    [[no_unique_address]] Scheduler __sch;
    [[no_unique_address]] ChildEnv __child;

    friend auto tag_invoke(
        get_completion_scheduler_t<set_value_t>,
        const __completion_attrs& self) noexcept
            -> Scheduler {
        return self.__sch;
    }

    friend auto tag_invoke(
        get_completion_scheduler_t<set_stopped_t>,
        const __completion_attrs& self) noexcept
            -> Scheduler {
        return self.__sch;
    }

    template<class CPO, class Env>
        requires (!std::same_as<CPO, set_error_t>) &&
                 (__forge_env_detail::__queryable<
                      get_completion_domain_t<CPO>,
                      const __schedule_attrs_t&,
                      Env> ||
                  __forge_env_detail::__queryable<
                      get_completion_domain_t<CPO>,
                      const Scheduler&,
                      Env>)
    friend auto tag_invoke(
        get_completion_domain_t<CPO> query,
        const __completion_attrs& self,
        Env&& env)
        noexcept(__domain_query_nothrow<CPO, Env>()) {
        if constexpr (__forge_env_detail::__queryable<
                          get_completion_domain_t<CPO>,
                          const __schedule_attrs_t&,
                          Env>) {
            auto scheduler = self.__sch;
            auto sched_sender = std::execution::schedule(scheduler);
            auto attrs = std::execution::get_env(sched_sender);
            return std::execution::get_completion_domain<CPO>(
                attrs, static_cast<Env&&>(env));
        } else if constexpr (__forge_env_detail::__queryable<
                                 get_completion_domain_t<CPO>,
                                 const Scheduler&,
                                 Env>) {
            return std::execution::get_completion_domain<CPO>(
                self.__sch, static_cast<Env&&>(env));
        }
    }

    template<class Query>
        requires (!__completion_scheduler_query_v<Query>) &&
                 (__is_forwarding_query<Query>()) &&
                 __forge_env_detail::__queryable<Query, const ChildEnv&>
    friend decltype(auto) tag_invoke(
        Query query,
        const __completion_attrs& self)
        noexcept(__forge_env_detail::__nothrow_query<Query, const ChildEnv&>) {
        return __forge_env_detail::__query(std::move(query), self.__child);
    }
};

template<class Scheduler, class S>
struct __sender {
    using sender_concept = sender_t;
    using source_t = S;

    Scheduler __sch;
    S __sndr;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        using self_t = std::remove_cvref_t<Self>;
        using source_cs_t = decltype(std::execution::get_completion_signatures(
            std::declval<typename self_t::source_t>(),
            std::declval<Env>()));
        using results_t = __results<source_cs_t>;
        using sched_sndr_t = decltype(std::execution::schedule(
            std::declval<Scheduler&>()));
        using sched_cs_t = decltype(std::execution::get_completion_signatures(
            std::declval<sched_sndr_t>(),
            std::declval<Env>()));
        using sched_non_value_cs_t =
            __forge_meta::__non_value_completion_signatures_t<sched_cs_t>;
        return __forge_meta::__concat_unique_cs_t<
            typename __results<source_cs_t>::source_completions,
            sched_non_value_cs_t, typename results_t::exception_completion>{};
    }

    template<receiver R>
    auto connect(R r) &&
        -> __op<Scheduler, S, R>
    {
        return __op<Scheduler, S, R>(
            std::move(__sch), std::move(__sndr), std::move(r));
    }

    template<receiver R>
        requires std::copy_constructible<Scheduler> && std::copy_constructible<S>
    auto connect(R r) const&
        -> __op<Scheduler, S, R>
    {
        return __op<Scheduler, S, R>(
            __sch, __sndr, std::move(r));
    }

    auto get_env() const noexcept {
        return __completion_attrs<Scheduler, env_of_t<S>>{
            __sch,
            std::execution::get_env(__sndr)};
    }
};

template<class Scheduler>
struct __closure {
    Scheduler __scheduler;

    template<sender S>
        requires std::copy_constructible<Scheduler>
    [[nodiscard]] auto operator()(S&& sndr) const&;

    template<sender S>
    [[nodiscard]] auto operator()(S&& sndr) &&;

    template<sender S>
        requires std::copy_constructible<Scheduler>
    friend auto operator|(S&& sndr, const __closure& self) {
        return self(std::forward<S>(sndr));
    }

    template<sender S>
    friend auto operator|(S&& sndr, __closure&& self) {
        return std::move(self)(std::forward<S>(sndr));
    }
};

} // namespace __forge_continues_on

struct continues_on_t {
    template<sender S, class Scheduler>
        requires scheduler<std::remove_cvref_t<Scheduler>>
    [[nodiscard]] auto operator()(S&& sndr, Scheduler&& sch) const {
        auto departure = std::execution::schedule_from(
            std::forward<S>(sndr));
        return __forge_continues_on::__sender<
            std::remove_cvref_t<Scheduler>, decltype(departure)>{
            __forge_detail::__forward_as_given(std::forward<Scheduler>(sch)),
            std::move(departure)};
    }

    template<class Scheduler>
        requires scheduler<std::remove_cvref_t<Scheduler>>
    [[nodiscard]] auto operator()(Scheduler&& sch) const {
        return __forge_continues_on::__closure<std::remove_cvref_t<Scheduler>>{
            __forge_detail::__forward_as_given(std::forward<Scheduler>(sch))};
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

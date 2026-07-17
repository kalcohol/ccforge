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
#include "detail/op_storage.hpp"
#include "env.hpp"
#include "run_loop.hpp"

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace std::execution {

namespace __forge_continues_on {

template<class Sig>
struct __is_value_completion : std::false_type {};

template<class... Vs>
struct __is_value_completion<set_value_t(Vs...)> : std::true_type {};

template<class CS>
struct __value_completion_count;

template<class... Sigs>
struct __value_completion_count<completion_signatures<Sigs...>>
    : std::integral_constant<
          std::size_t,
          (std::size_t{0} + ... +
           static_cast<std::size_t>(__is_value_completion<Sigs>::value))> {};

template<class CS>
inline constexpr std::size_t __value_completion_count_v =
    __value_completion_count<CS>::value;

template<class R>
concept __can_set_exception_ptr = requires(R& r, std::exception_ptr ep) {
    std::execution::set_error(std::move(r), std::move(ep));
};

template<class R, class... StoredVs>
struct __sched_value_recv {
    using receiver_concept = receiver_t;
    R* __outer;
    std::tuple<StoredVs...> __vals;

    void set_value() && noexcept {
        std::apply([this](auto&&... vs) {
            std::execution::set_value(std::move(*__outer), static_cast<StoredVs&&>(vs)...);
        }, std::move(__vals));
    }
    template<class E>
    void set_error(E&& e) && noexcept {
        std::execution::set_error(std::move(*__outer), static_cast<E&&>(e));
    }
    void set_stopped() && noexcept {
        std::execution::set_stopped(std::move(*__outer));
    }
    auto get_env() const noexcept -> env_of_t<R> {
        return std::execution::get_env(*__outer);
    }
};

template<class R, class E>
struct __sched_error_recv {
    using receiver_concept = receiver_t;
    R* __outer;
    E __error;

    void set_value() && noexcept {
        std::execution::set_error(std::move(*__outer), std::move(__error));
    }
    template<class Other>
    void set_error(Other&& e) && noexcept {
        std::execution::set_error(std::move(*__outer), static_cast<Other&&>(e));
    }
    void set_stopped() && noexcept {
        std::execution::set_stopped(std::move(*__outer));
    }
    auto get_env() const noexcept -> env_of_t<R> {
        return std::execution::get_env(*__outer);
    }
};

template<class R>
struct __sched_stopped_recv {
    using receiver_concept = receiver_t;
    R* __outer;

    void set_value() && noexcept {
        std::execution::set_stopped(std::move(*__outer));
    }
    template<class E>
    void set_error(E&& e) && noexcept {
        std::execution::set_error(std::move(*__outer), static_cast<E&&>(e));
    }
    void set_stopped() && noexcept {
        std::execution::set_stopped(std::move(*__outer));
    }
    auto get_env() const noexcept -> env_of_t<R> {
        return std::execution::get_env(*__outer);
    }
};

template<class Scheduler, class S, class R, class ValTuple>
struct __op_impl;

template<class Scheduler, class S, class R, class... Vs>
struct __op_impl<Scheduler, S, R, std::tuple<Vs...>> : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;

    using __sched_value_recv_t = __sched_value_recv<R, Vs...>;
    using __sched_sndr_t = decltype(std::execution::schedule(std::declval<Scheduler>()));

    void __deliver_schedule_failure() noexcept {
        if constexpr (__can_set_exception_ptr<R>) {
            auto error = std::current_exception();
            if (!error) {
                error = std::make_exception_ptr(
                    std::runtime_error("continues_on: scheduling failed"));
            }
            std::execution::set_error(std::move(__outer), std::move(error));
        } else {
            std::terminate();
        }
    }

    template<class Recv>
    void __start_scheduled(Recv recv) noexcept {
        using sched_op_t = connect_result_t<__sched_sndr_t, Recv>;
        try {
            auto* op = __sched_storage.template emplace_from<sched_op_t>([&]() -> sched_op_t {
                return std::execution::connect(std::execution::schedule(__sch), std::move(recv));
            });
            std::execution::start(*op);
        } catch (...) {
            __deliver_schedule_failure();
        }
    }

    struct __up_recv {
        using receiver_concept = receiver_t;
        __op_impl* __self;

        void set_value(Vs&&... vs) && noexcept {
            try {
                __self->__start_scheduled(__sched_value_recv_t{
                    &__self->__outer,
                    std::tuple<Vs...>(static_cast<Vs&&>(vs)...)});
            } catch (...) {
                __self->__deliver_schedule_failure();
            }
        }
        template<class E>
        void set_error(E&& e) && noexcept {
            using error_t = std::decay_t<E>;
            try {
                __self->__start_scheduled(
                    __sched_error_recv<R, error_t>{&__self->__outer, static_cast<E&&>(e)});
            } catch (...) {
                __self->__deliver_schedule_failure();
            }
        }
        void set_stopped() && noexcept {
            __self->__start_scheduled(
                __sched_stopped_recv<R>{&__self->__outer});
        }
        auto get_env() const noexcept -> env_of_t<R> {
            return std::execution::get_env(__self->__outer);
        }
    };

    using __up_op_t = connect_result_t<S, __up_recv>;

    R __outer;
    Scheduler __sch;
    __up_op_t __up_op;
    __forge_detail::__op_storage<1024> __sched_storage;

    __op_impl(Scheduler sch, S sndr, R recv)
        : __outer(std::move(recv))
        , __sch(std::move(sch))
        , __up_op(std::execution::connect(std::move(sndr), __up_recv{this}))
    {}

    void start() & noexcept {
        std::execution::start(__up_op);
    }
};

template<class Scheduler, class S, class R>
struct __op_selector {
    using cs_t = decltype(std::execution::get_completion_signatures(
        std::declval<S>(), std::declval<env_of_t<R>>()));
    static_assert(
        __value_completion_count_v<cs_t> <= 1,
        "continues_on supports at most one value completion shape");
    using val_tup_t = __forge_meta::__single_value_tuple_t<cs_t>;
    using type = __op_impl<Scheduler, S, R, val_tup_t>;
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
            std::declval<const typename self_t::source_t&>(),
            std::declval<Env>()));
        static_assert(
            __value_completion_count_v<source_cs_t> <= 1,
            "continues_on supports at most one value completion shape");
        using sched_sndr_t = decltype(std::execution::schedule(
            std::declval<Scheduler&>()));
        using sched_cs_t = decltype(std::execution::get_completion_signatures(
            std::declval<sched_sndr_t>(),
            std::declval<Env>()));
        using sched_non_value_cs_t =
            __forge_meta::__non_value_completion_signatures_t<sched_cs_t>;
        return __forge_meta::__concat_unique_cs_t<
            source_cs_t,
            sched_non_value_cs_t,
            completion_signatures<set_error_t(std::exception_ptr)>>{};
    }

    template<receiver R>
    auto connect(R r) &&
        -> typename __op_selector<Scheduler, S, R>::type
    {
        return typename __op_selector<Scheduler, S, R>::type(
            std::move(__sch), std::move(__sndr), std::move(r));
    }

    template<receiver R>
        requires std::copy_constructible<Scheduler> && std::copy_constructible<S>
    auto connect(R r) const&
        -> typename __op_selector<Scheduler, S, R>::type
    {
        return typename __op_selector<Scheduler, S, R>::type(
            __sch, __sndr, std::move(r));
    }

    auto get_env() const noexcept {
        return std::execution::get_env(__sndr);
    }
};

} // namespace __forge_continues_on

template<sender S, class Scheduler>
    requires scheduler<std::remove_cvref_t<Scheduler>>
[[nodiscard]] auto continues_on(S&& sndr, Scheduler&& sch) {
    return __forge_continues_on::__sender<
        std::remove_cvref_t<Scheduler>, std::decay_t<S>>{
        __forge_detail::__forward_as_given(std::forward<Scheduler>(sch)),
        __forge_detail::__forward_as_given(std::forward<S>(sndr))};
}

template<scheduler Scheduler, class... Vs>
    requires (std::move_constructible<std::decay_t<Vs>> && ...)
[[nodiscard]] auto transfer_just(Scheduler sch, Vs&&... vs) {
    return std::execution::continues_on(
        std::execution::just(std::forward<Vs>(vs)...), std::move(sch));
}

} // namespace std::execution

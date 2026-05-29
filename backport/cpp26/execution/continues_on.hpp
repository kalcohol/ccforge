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

#include <exception>
#include <tuple>
#include <type_traits>
#include <utility>

namespace std::execution {

namespace __forge_continues_on {

template<class R, class... StoredVs>
struct __sched_value_recv {
    using receiver_concept = receiver_t;
    R* __outer;
    std::tuple<StoredVs...> __vals;

    friend void tag_invoke(set_value_t, __sched_value_recv&& self) noexcept {
        std::apply([&self](auto&&... vs) {
            set_value(std::move(*self.__outer), static_cast<StoredVs&&>(vs)...);
        }, std::move(self.__vals));
    }
    template<class E>
    friend void tag_invoke(set_error_t, __sched_value_recv&& self, E&& e) noexcept {
        set_error(std::move(*self.__outer), static_cast<E&&>(e));
    }
    friend void tag_invoke(set_stopped_t, __sched_value_recv&& self) noexcept {
        set_stopped(std::move(*self.__outer));
    }
    friend auto tag_invoke(get_env_t, const __sched_value_recv& self) noexcept
        -> env_of_t<R> {
        return std::execution::get_env(*self.__outer);
    }
};

template<class R, class E>
struct __sched_error_recv {
    using receiver_concept = receiver_t;
    R* __outer;
    E __error;

    friend void tag_invoke(set_value_t, __sched_error_recv&& self) noexcept {
        set_error(std::move(*self.__outer), std::move(self.__error));
    }
    template<class Other>
    friend void tag_invoke(set_error_t, __sched_error_recv&& self, Other&& e) noexcept {
        set_error(std::move(*self.__outer), static_cast<Other&&>(e));
    }
    friend void tag_invoke(set_stopped_t, __sched_error_recv&& self) noexcept {
        set_stopped(std::move(*self.__outer));
    }
    friend auto tag_invoke(get_env_t, const __sched_error_recv& self) noexcept
        -> env_of_t<R> {
        return std::execution::get_env(*self.__outer);
    }
};

template<class R>
struct __sched_stopped_recv {
    using receiver_concept = receiver_t;
    R* __outer;

    friend void tag_invoke(set_value_t, __sched_stopped_recv&& self) noexcept {
        set_stopped(std::move(*self.__outer));
    }
    template<class E>
    friend void tag_invoke(set_error_t, __sched_stopped_recv&& self, E&& e) noexcept {
        set_error(std::move(*self.__outer), static_cast<E&&>(e));
    }
    friend void tag_invoke(set_stopped_t, __sched_stopped_recv&& self) noexcept {
        set_stopped(std::move(*self.__outer));
    }
    friend auto tag_invoke(get_env_t, const __sched_stopped_recv& self) noexcept
        -> env_of_t<R> {
        return std::execution::get_env(*self.__outer);
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
        std::terminate();
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

        friend void tag_invoke(set_value_t, __up_recv&& self, Vs&&... vs) noexcept {
            try {
                self.__self->__start_scheduled(__sched_value_recv_t{
                    &self.__self->__outer,
                    std::tuple<Vs...>(static_cast<Vs&&>(vs)...)});
            } catch (...) {
                self.__self->__deliver_schedule_failure();
            }
        }
        template<class E>
        friend void tag_invoke(set_error_t, __up_recv&& self, E&& e) noexcept {
            using error_t = std::decay_t<E>;
            try {
                self.__self->__start_scheduled(
                    __sched_error_recv<R, error_t>{&self.__self->__outer, static_cast<E&&>(e)});
            } catch (...) {
                self.__self->__deliver_schedule_failure();
            }
        }
        friend void tag_invoke(set_stopped_t, __up_recv&& self) noexcept {
            self.__self->__start_scheduled(
                __sched_stopped_recv<R>{&self.__self->__outer});
        }
        friend auto tag_invoke(get_env_t, const __up_recv& self) noexcept
            -> env_of_t<R> {
            return std::execution::get_env(self.__self->__outer);
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

    friend void tag_invoke(start_t, __op_impl& self) noexcept {
        std::execution::start(self.__up_op);
    }
};

template<class Scheduler, class S, class R>
struct __op_selector {
    using cs_t = decltype(std::execution::get_completion_signatures(
        std::declval<S>(), std::declval<env_of_t<R>>()));
    using val_tup_t = __forge_meta::__single_value_tuple_t<cs_t>;
    using type = __op_impl<Scheduler, S, R, val_tup_t>;
};

template<class Scheduler, class S>
struct __sender {
    using sender_concept = sender_t;
    Scheduler __sch;
    S __sndr;

    friend auto tag_invoke(get_completion_signatures_t,
                           const __sender& self, auto env) noexcept {
        return decltype(std::execution::get_completion_signatures(self.__sndr, env)){};
    }

    template<receiver R>
    friend auto tag_invoke(connect_t, __sender&& self, R r)
        -> typename __op_selector<Scheduler, S, R>::type
    {
        return typename __op_selector<Scheduler, S, R>::type(
            std::move(self.__sch), std::move(self.__sndr), std::move(r));
    }

    template<receiver R>
        requires std::copy_constructible<Scheduler> && std::copy_constructible<S>
    friend auto tag_invoke(connect_t, const __sender& self, R r)
        -> typename __op_selector<Scheduler, S, R>::type
    {
        return typename __op_selector<Scheduler, S, R>::type(
            self.__sch, self.__sndr, std::move(r));
    }

    friend auto tag_invoke(get_env_t, const __sender& self) noexcept {
        return std::execution::get_env(self.__sndr);
    }
};

} // namespace __forge_continues_on

template<sender S, scheduler Scheduler>
[[nodiscard]] auto continues_on(S sndr, Scheduler sch) {
    return __forge_continues_on::__sender<Scheduler, S>{
        std::move(sch), std::move(sndr)};
}

template<scheduler Scheduler, class... Vs>
    requires (std::move_constructible<std::decay_t<Vs>> && ...)
[[nodiscard]] auto transfer_just(Scheduler sch, Vs&&... vs) {
    return std::execution::continues_on(
        std::execution::just(std::forward<Vs>(vs)...), std::move(sch));
}

} // namespace std::execution

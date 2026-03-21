// MIT License
//
// Copyright (c) 2026 Forge Project
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
#include "run_loop.hpp"

namespace std::execution {

namespace __forge_continues_on {

template<class Scheduler, class R, class... StoredVs>
struct __sched_recv {
    using receiver_concept = receiver_t;
    R* __outer;
    std::tuple<StoredVs...> __vals;

    friend void tag_invoke(set_value_t, __sched_recv&& self) noexcept {
        std::apply([&self](auto&&... vs) {
            set_value(std::move(*self.__outer), static_cast<StoredVs&&>(vs)...);
        }, std::move(self.__vals));
    }
    template<class E>
    friend void tag_invoke(set_error_t, __sched_recv&& self, E&& e) noexcept {
        set_error(std::move(*self.__outer), static_cast<E&&>(e));
    }
    friend void tag_invoke(set_stopped_t, __sched_recv&& self) noexcept {
        set_stopped(std::move(*self.__outer));
    }
    friend auto tag_invoke(get_env_t, const __sched_recv& self) noexcept
        -> env_of_t<R> {
        return std::execution::get_env(*self.__outer);
    }
};

template<class Scheduler, class S, class R, class ValTuple>
struct __op_impl;

template<class Scheduler, class S, class R, class... Vs>
struct __op_impl<Scheduler, S, R, std::tuple<Vs...>> : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;

    using __sched_recv_t = __sched_recv<Scheduler, R, Vs...>;
    using __sched_sndr_t = decltype(std::execution::schedule(std::declval<Scheduler>()));
    using __sched_op_t = connect_result_t<__sched_sndr_t, __sched_recv_t>;

    static constexpr std::size_t kBufSize = sizeof(__sched_op_t) + alignof(__sched_op_t);

    struct __up_recv {
        using receiver_concept = receiver_t;
        __op_impl* __self;

        friend void tag_invoke(set_value_t, __up_recv&& self, Vs&&... vs) noexcept {
            auto* p = static_cast<void*>(self.__self->__sched_buf);
            ::new(p) __sched_op_t(std::execution::connect(
                std::execution::schedule(self.__self->__sch),
                __sched_recv_t{&self.__self->__outer, std::tuple<Vs...>(static_cast<Vs&&>(vs)...)}));
            self.__self->__sched_alive = true;
            std::execution::start(*static_cast<__sched_op_t*>(p));
        }
        template<class E>
        friend void tag_invoke(set_error_t, __up_recv&& self, E&& e) noexcept {
            set_error(std::move(self.__self->__outer), static_cast<E&&>(e));
        }
        friend void tag_invoke(set_stopped_t, __up_recv&& self) noexcept {
            set_stopped(std::move(self.__self->__outer));
        }
        friend auto tag_invoke(get_env_t, const __up_recv& self) noexcept
            -> env_of_t<R> {
            return std::execution::get_env(self.__self->__outer);
        }
    };

    using __up_op_t = connect_result_t<S, __up_recv>;

    R __outer;
    Scheduler __sch;
    alignas(std::max_align_t) unsigned char __sched_buf[kBufSize];
    bool __sched_alive = false;
    __up_op_t __up_op;

    __op_impl(Scheduler sch, S sndr, R recv)
        : __outer(std::move(recv))
        , __sch(std::move(sch))
        , __up_op(std::execution::connect(std::move(sndr), __up_recv{this}))
    {}

    ~__op_impl() {
        if (__sched_alive) {
            static_cast<__sched_op_t*>(static_cast<void*>(__sched_buf))->~__sched_op_t();
        }
    }

    friend void tag_invoke(start_t, __op_impl& self) noexcept {
        std::execution::start(self.__up_op);
    }
};

template<class Scheduler, class S, class R>
struct __op_selector {
    using cs_t = decltype(std::execution::get_completion_signatures(
        std::declval<S>(), std::execution::empty_env{}));
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
    friend auto tag_invoke(connect_t, __sender self, R r)
        -> typename __op_selector<Scheduler, S, R>::type
    {
        return typename __op_selector<Scheduler, S, R>::type(
            std::move(self.__sch), std::move(self.__sndr), std::move(r));
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

} // namespace std::execution

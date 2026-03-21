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

namespace __forge_on {

template<class Scheduler, class S, class R>
struct __starts_on_op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;

    R __outer_recv;
    Scheduler __sch;
    S __sndr;

    static constexpr std::size_t kBufSize = 1024;
    alignas(std::max_align_t) unsigned char __sched_buf[kBufSize];
    alignas(std::max_align_t) unsigned char __sndr_buf[kBufSize];

    void (*__sched_start)(void*) noexcept = nullptr;
    void (*__sched_dtor)(void*)  noexcept = nullptr;
    void (*__sndr_dtor)(void*)   noexcept = nullptr;

    struct __sndr_recv {
        using receiver_concept = receiver_t;
        __starts_on_op* __self;
        template<class... Vs>
        friend void tag_invoke(set_value_t, __sndr_recv&& sr, Vs&&... vs) noexcept {
            set_value(std::move(sr.__self->__outer_recv), static_cast<Vs&&>(vs)...);
        }
        template<class E>
        friend void tag_invoke(set_error_t, __sndr_recv&& sr, E&& e) noexcept {
            set_error(std::move(sr.__self->__outer_recv), static_cast<E&&>(e));
        }
        friend void tag_invoke(set_stopped_t, __sndr_recv&& sr) noexcept {
            set_stopped(std::move(sr.__self->__outer_recv));
        }
        friend auto tag_invoke(get_env_t, const __sndr_recv& sr) noexcept
            -> env_of_t<R> {
            return std::execution::get_env(sr.__self->__outer_recv);
        }
    };

    struct __sched_recv {
        using receiver_concept = receiver_t;
        __starts_on_op* __self;

        friend void tag_invoke(set_value_t, __sched_recv&& self) noexcept {
            using inner_op_t = connect_result_t<S, __sndr_recv>;
            static_assert(sizeof(inner_op_t) <= kBufSize,
                "starts_on: inner op too large");
            ::new(static_cast<void*>(self.__self->__sndr_buf))
                inner_op_t(std::execution::connect(
                    std::move(self.__self->__sndr), __sndr_recv{self.__self}));
            self.__self->__sndr_dtor = [](void* p) noexcept {
                static_cast<inner_op_t*>(p)->~inner_op_t();
            };
            std::execution::start(*static_cast<inner_op_t*>(
                static_cast<void*>(self.__self->__sndr_buf)));
        }
        template<class E>
        friend void tag_invoke(set_error_t, __sched_recv&& self, E&& e) noexcept {
            set_error(std::move(self.__self->__outer_recv), static_cast<E&&>(e));
        }
        friend void tag_invoke(set_stopped_t, __sched_recv&& self) noexcept {
            set_stopped(std::move(self.__self->__outer_recv));
        }
        friend auto tag_invoke(get_env_t, const __sched_recv& self) noexcept
            -> env_of_t<R> {
            return std::execution::get_env(self.__self->__outer_recv);
        }
    };

    using __sched_op_t = connect_result_t<
        decltype(std::execution::schedule(std::declval<Scheduler>())), __sched_recv>;
    static_assert(sizeof(__sched_op_t) <= kBufSize,
        "starts_on: sched op too large");

    __starts_on_op(Scheduler sch, S sndr, R recv)
        : __outer_recv(std::move(recv))
        , __sch(std::move(sch))
        , __sndr(std::move(sndr))
    {
        ::new(static_cast<void*>(__sched_buf))
            __sched_op_t(std::execution::connect(
                std::execution::schedule(__sch), __sched_recv{this}));
        __sched_start = [](void* p) noexcept {
            std::execution::start(*static_cast<__sched_op_t*>(p));
        };
        __sched_dtor = [](void* p) noexcept {
            static_cast<__sched_op_t*>(p)->~__sched_op_t();
        };
    }

    ~__starts_on_op() {
        if (__sndr_dtor)  { __sndr_dtor(static_cast<void*>(__sndr_buf)); }
        if (__sched_dtor) { __sched_dtor(static_cast<void*>(__sched_buf)); }
    }

    friend void tag_invoke(start_t, __starts_on_op& self) noexcept {
        self.__sched_start(static_cast<void*>(self.__sched_buf));
    }
};

template<class Scheduler, class S>
struct __starts_on_sender {
    using sender_concept = sender_t;
    Scheduler __sch;
    S __sndr;

    friend auto tag_invoke(get_completion_signatures_t,
                           const __starts_on_sender& self, auto env) noexcept {
        return decltype(std::execution::get_completion_signatures(self.__sndr, env)){};
    }

    template<receiver R>
    friend auto tag_invoke(connect_t, __starts_on_sender self, R r)
        -> __starts_on_op<Scheduler, S, R>
    {
        return __starts_on_op<Scheduler, S, R>(
            std::move(self.__sch), std::move(self.__sndr), std::move(r));
    }

    friend auto tag_invoke(get_env_t, const __starts_on_sender& self) noexcept {
        return std::execution::get_env(self.__sndr);
    }
};

} // namespace __forge_on

template<class Scheduler, class S>
    requires sender<S> && scheduler<Scheduler>
[[nodiscard]] auto starts_on(Scheduler sch, S sndr) {
    return __forge_on::__starts_on_sender<Scheduler, S>{
        std::move(sch), std::move(sndr)};
}

} // namespace std::execution

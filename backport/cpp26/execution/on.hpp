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

namespace std::execution {

namespace __forge_on {

template<class Scheduler, class S, class R>
struct __starts_on_op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;

    R __outer_recv;
    Scheduler __sch;
    S __sndr;

    __forge_detail::__op_storage<1024> __sched_storage;
    __forge_detail::__op_storage<1024> __sndr_storage;

    struct __sndr_recv {
        using receiver_concept = receiver_t;
        __starts_on_op* __self;
        template<class... Vs>
        void set_value(Vs&&... vs) && noexcept {
            std::execution::set_value(std::move(__self->__outer_recv), static_cast<Vs&&>(vs)...);
        }
        template<class E>
        void set_error(E&& e) && noexcept {
            std::execution::set_error(std::move(__self->__outer_recv), static_cast<E&&>(e));
        }
        void set_stopped() && noexcept {
            std::execution::set_stopped(std::move(__self->__outer_recv));
        }
        auto get_env() const noexcept -> env_of_t<R> {
            return std::execution::get_env(__self->__outer_recv);
        }
    };

    struct __sched_recv {
        using receiver_concept = receiver_t;
        __starts_on_op* __self;

        void set_value() && noexcept {
            using inner_op_t = connect_result_t<S, __sndr_recv>;
            auto* op = __self->__sndr_storage.template emplace_from<inner_op_t>([&]() -> inner_op_t {
                return std::execution::connect(
                    std::move(__self->__sndr), __sndr_recv{__self});
            });
            std::execution::start(*op);
        }
        template<class E>
        void set_error(E&& e) && noexcept {
            std::execution::set_error(std::move(__self->__outer_recv), static_cast<E&&>(e));
        }
        void set_stopped() && noexcept {
            std::execution::set_stopped(std::move(__self->__outer_recv));
        }
        auto get_env() const noexcept -> env_of_t<R> {
            return std::execution::get_env(__self->__outer_recv);
        }
    };

    using __sched_op_t = connect_result_t<
        decltype(std::execution::schedule(std::declval<Scheduler>())), __sched_recv>;

    __starts_on_op(Scheduler sch, S sndr, R recv)
        : __outer_recv(std::move(recv))
        , __sch(std::move(sch))
        , __sndr(std::move(sndr))
    {
        __sched_storage.template emplace_from<__sched_op_t>([&]() -> __sched_op_t {
            return std::execution::connect(std::execution::schedule(__sch), __sched_recv{this});
        });
    }

    void start() & noexcept {
        std::execution::start(__sched_storage.template get<__sched_op_t>());
    }
};

template<class Scheduler, class S>
struct __starts_on_sender {
    using sender_concept = sender_t;
    using source_t = S;

    Scheduler __sch;
    S __sndr;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        using self_t = std::remove_cvref_t<Self>;
        return decltype(std::execution::get_completion_signatures(
            std::declval<const typename self_t::source_t&>(),
            std::declval<Env>())){};
    }

    template<receiver R>
    auto connect(R r) && -> __starts_on_op<Scheduler, S, R>
    {
        return __starts_on_op<Scheduler, S, R>(
            std::move(__sch), std::move(__sndr), std::move(r));
    }

    template<receiver R>
        requires std::copy_constructible<Scheduler> && std::copy_constructible<S>
    auto connect(R r) const& -> __starts_on_op<Scheduler, S, R>
    {
        return __starts_on_op<Scheduler, S, R>(
            __sch, __sndr, std::move(r));
    }

    auto get_env() const noexcept {
        return std::execution::get_env(__sndr);
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

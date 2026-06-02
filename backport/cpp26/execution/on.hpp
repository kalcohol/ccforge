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
#include "continues_on.hpp"
#include "detail/op_storage.hpp"
#include "env.hpp"
#include "run_loop.hpp"

#include <exception>

namespace std::execution {

namespace __forge_on {

template<class R>
concept __can_set_exception_ptr = requires(R& r, std::exception_ptr ep) {
    std::execution::set_error(std::move(r), std::move(ep));
};

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
            try {
                using inner_op_t = connect_result_t<S, __sndr_recv>;
                auto* op = __self->__sndr_storage.template emplace_from<inner_op_t>([&]() -> inner_op_t {
                    return std::execution::connect(
                        std::move(__self->__sndr), __sndr_recv{__self});
                });
                std::execution::start(*op);
            } catch (...) {
                if constexpr (__can_set_exception_ptr<R>) {
                    std::execution::set_error(
                        std::move(__self->__outer_recv),
                        std::current_exception());
                } else {
                    std::terminate();
                }
            }
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
        using source_cs_t = decltype(std::execution::get_completion_signatures(
            std::declval<const typename self_t::source_t&>(),
            std::declval<Env>()));
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

template<class Scheduler, sender S>
    requires scheduler<std::remove_cvref_t<Scheduler>>
[[nodiscard]] auto starts_on(Scheduler&& sch, S&& sndr) {
    return __forge_on::__starts_on_sender<
        std::remove_cvref_t<Scheduler>, std::decay_t<S>>{
        __forge_detail::__forward_as_given(std::forward<Scheduler>(sch)),
        __forge_detail::__forward_as_given(std::forward<S>(sndr))};
}

namespace __forge_on_adaptor {

template<class Scheduler, class S, class R>
using __orig_scheduler_t = decltype(
    std::execution::get_start_scheduler(std::execution::get_env(std::declval<const R&>())));

template<class Scheduler, class S, class OrigScheduler>
using __composed_sender_t = decltype(std::execution::continues_on(
    std::execution::starts_on(std::declval<Scheduler>(), std::declval<S>()),
    std::declval<OrigScheduler>()));

template<class Scheduler, class S, class R>
using __inner_op_t = connect_result_t<
    __composed_sender_t<Scheduler, S, __orig_scheduler_t<Scheduler, S, R>>, R>;

template<class Scheduler, class S, class R>
concept __first_form_connectable =
    requires(const R& r) {
        std::execution::get_start_scheduler(std::execution::get_env(r));
    } &&
    requires(Scheduler&& sch, S&& sndr, R&& r) {
        typename __inner_op_t<Scheduler, S, R>;
        std::execution::connect(
            std::execution::continues_on(
                std::execution::starts_on(
                    static_cast<Scheduler&&>(sch),
                    static_cast<S&&>(sndr)),
                std::execution::get_start_scheduler(std::execution::get_env(r))),
            static_cast<R&&>(r));
    };

template<class Scheduler, class S, class R>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;
    using inner_op_t = __inner_op_t<Scheduler, S, R>;

    __forge_detail::__op_storage<1024> __storage;

    __op(Scheduler sch, S sndr, R rcvr) {
        auto orig_sch = std::execution::get_start_scheduler(std::execution::get_env(rcvr));
        auto composed = std::execution::continues_on(
            std::execution::starts_on(std::move(sch), std::move(sndr)),
            std::move(orig_sch));
        __storage.template emplace_from<inner_op_t>([&]() -> inner_op_t {
            return std::execution::connect(std::move(composed), std::move(rcvr));
        });
    }

    void start() & noexcept {
        std::execution::start(__storage.template get<inner_op_t>());
    }
};

template<class Scheduler, class S>
struct __sender {
    using sender_concept = sender_t;
    using scheduler_t = Scheduler;
    using source_t = S;

    Scheduler __sch;
    S __sndr;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        using self_t = std::remove_cvref_t<Self>;
        using scheduler_t = typename self_t::scheduler_t;
        using source_t = typename self_t::source_t;
        using orig_scheduler_t = decltype(std::execution::get_start_scheduler(std::declval<Env>()));
        using composed_t = __composed_sender_t<const scheduler_t&, const source_t&, orig_scheduler_t>;
        return decltype(std::execution::get_completion_signatures(
            std::declval<composed_t>(), std::declval<Env>())){};
    }

    template<receiver R>
        requires __first_form_connectable<Scheduler, S, R>
    auto connect(R r) && -> __op<Scheduler, S, R> {
        return __op<Scheduler, S, R>{
            std::move(__sch), std::move(__sndr), std::move(r)};
    }

    template<receiver R>
        requires std::copy_constructible<Scheduler> && std::copy_constructible<S> &&
                 __first_form_connectable<const Scheduler&, const S&, R>
    auto connect(R r) const& -> __op<Scheduler, S, R> {
        return __op<Scheduler, S, R>{__sch, __sndr, std::move(r)};
    }

    auto get_env() const noexcept {
        return std::execution::get_env(__sndr);
    }
};

struct on_t {
    template<class Scheduler, sender S>
        requires scheduler<std::remove_cvref_t<Scheduler>>
    [[nodiscard]] auto operator()(Scheduler&& sch, S&& sndr) const {
        return __sender<std::remove_cvref_t<Scheduler>, std::decay_t<S>>{
            __forge_detail::__forward_as_given(std::forward<Scheduler>(sch)),
            __forge_detail::__forward_as_given(std::forward<S>(sndr))};
    }
};

} // namespace __forge_on_adaptor

inline constexpr __forge_on_adaptor::on_t on{};

} // namespace std::execution

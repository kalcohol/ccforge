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
#include "let.hpp"
#include "run_loop.hpp"

namespace std::execution {

namespace __forge_on {

template<class Scheduler, class OuterEnv>
auto __make_starts_on_env(Scheduler& scheduler, const OuterEnv& outer) {
    auto forwarding = __forge_detail::__fwd_env_t<const OuterEnv&>{outer};
    auto scheduler_prop = std::execution::prop(
        std::execution::get_scheduler, std::cref(scheduler));
    auto start_prop = std::execution::prop(
        std::execution::get_start_scheduler, std::cref(scheduler));
    // The operation owns value environments; this view has a stable shape.
    auto child_env = std::execution::env<
        decltype(scheduler_prop), decltype(start_prop), decltype(forwarding)>{
        std::move(scheduler_prop), std::move(start_prop),
        std::move(forwarding)};
    // SCHED-ENV exposes only a direct domain query, not a synthesized fallback.
    if constexpr (__forge_env_detail::__queryable<get_domain_t, const Scheduler&>) {
        return std::execution::env{
            std::execution::prop(
                std::execution::get_domain,
                std::execution::get_domain(scheduler)),
            std::move(child_env)};
    } else {
        return child_env;
    }
}

template<class Scheduler, class OuterEnv>
using __starts_on_env_t = decltype(__make_starts_on_env(
    std::declval<Scheduler&>(), std::declval<OuterEnv>()));

template<class Scheduler, class S>
struct __starts_on_sender;

template<class Scheduler, class S>
struct __move_child {
    S* __child;
    auto operator()() const
        noexcept(std::is_nothrow_move_constructible_v<__starts_on_sender<Scheduler, S>>) -> S {
        return std::move(*__child);
    }
};

template<class Scheduler, class S, class Env, class = void>
struct __starts_cs {};

template<class Scheduler, class S, class Env>
    requires sender_in<S, __starts_on_env_t<Scheduler, Env>> &&
             sender_in<schedule_result_t<Scheduler&>, __forge_detail::__fwd_env_t<Env>> &&
             requires { typename connect_result_t<S,
                 __forge_let::__signature_receiver<__starts_on_env_t<Scheduler, Env>>>; }
struct __starts_cs<Scheduler, S, Env, void> {
    using source_env = __starts_on_env_t<Scheduler, Env>;
    static constexpr bool nothrow =
        __forge_let::__nothrow<__move_child<Scheduler, S>,
            __forge_let::__signature_receiver<source_env>>;
    using type = __forge_meta::__concat_unique_cs_t<
        completion_signatures_of_t<S, source_env>,
        __forge_meta::__non_value_completion_signatures_t<completion_signatures_of_t<
            schedule_result_t<Scheduler&>, __forge_detail::__fwd_env_t<Env>>>,
        typename __forge_let::__maybe_eptr_cs<!nothrow>::type>;
};

template<class Scheduler, class S, class R>
struct __starts_on_op;

template<class Scheduler, class R>
struct __sndr_recv {
    using receiver_concept = receiver_tag;
    R* __outer;
    Scheduler* __scheduler;
    const std::remove_reference_t<env_of_t<R>>* __environment;
    template<class... Vs>
    void set_value(Vs&&... vs) && noexcept {
        std::execution::set_value(std::move(*__outer), static_cast<Vs&&>(vs)...);
    }
    template<class E>
    void set_error(E&& e) && noexcept {
        std::execution::set_error(std::move(*__outer), static_cast<E&&>(e));
    }
    void set_stopped() && noexcept {
        std::execution::set_stopped(std::move(*__outer));
    }
    auto get_env() const noexcept -> __starts_on_env_t<Scheduler, env_of_t<R>> {
        return __make_starts_on_env(*__scheduler, *__environment);
    }
};

template<class Scheduler, class S, class R>
struct __sched_recv {
    using receiver_concept = receiver_tag;
    __starts_on_op<Scheduler, S, R>* __self;
    R* __outer;
    void set_value() && noexcept { __self->__start_child(); }
    template<class E>
    void set_error(E&& e) && noexcept {
        std::execution::set_error(std::move(__self->__outer_recv), static_cast<E&&>(e));
    }
    void set_stopped() && noexcept {
        std::execution::set_stopped(std::move(__self->__outer_recv));
    }
    auto get_env() const noexcept -> __forge_detail::__fwd_env_t<env_of_t<R>> {
        return {std::execution::get_env(*__outer)};
    }
};

template<class Scheduler, class S, class R>
concept __starts_connectable = requires {
    typename connect_result_t<S, __sndr_recv<Scheduler, R>>;
    typename connect_result_t<decltype(std::execution::schedule(std::declval<Scheduler&>())),
        __sched_recv<Scheduler, S, R>>;
};

template<class Scheduler, class S, class R>
struct __starts_on_op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_tag;
    using __source_env_t = __starts_on_env_t<Scheduler, env_of_t<R>>;
    using __schedule_env_t = __forge_detail::__fwd_env_t<env_of_t<R>>;

    R __outer_recv;
    Scheduler __sch;
    S __sndr;
    env_of_t<R> __outer_env;

    using __sndr_recv = __forge_on::__sndr_recv<Scheduler, R>;

    using __sndr_op_t = connect_result_t<S, __sndr_recv>;

    void __start_child() noexcept {
        try {
            __ops.template emplace<std::monostate>();
            // This is the move-returning lambda of [exec.starts.on]/4. Its
            // exception specification includes the scheduler in OutSndr.
            auto child = __move_child<Scheduler, S>{std::addressof(__sndr)}();
            auto& holder = __ops.template emplace<__forge_detail::__op_holder<__sndr_op_t>>(
                [&]() -> __sndr_op_t {
                    return std::execution::connect(std::move(child), __sndr_recv{std::addressof(__outer_recv), std::addressof(__sch), std::addressof(__outer_env)});
                });
            std::execution::start(holder.__op);
        } catch (...) {
            constexpr bool nothrow = __forge_let::__nothrow<
                __move_child<Scheduler, S>, __sndr_recv>;
            if constexpr (!nothrow) {
                std::execution::set_error(std::move(__outer_recv), std::current_exception());
            }
        }
    }

    using __sched_recv = __forge_on::__sched_recv<Scheduler, S, R>;

    using __sched_op_t = connect_result_t<schedule_result_t<Scheduler&>, __sched_recv>;
    using __ops_t = typename __forge_let::__as_variant<
        __forge_meta::list_push_unique_t<__forge_meta::type_list<std::monostate,
            __forge_detail::__op_holder<__sched_op_t>>,
            __forge_detail::__op_holder<__sndr_op_t>>>::type;
    __ops_t __ops;

    __starts_on_op(Scheduler sch, S sndr, R recv)
        : __outer_recv(std::move(recv)), __sch(std::move(sch)), __sndr(std::move(sndr))
        , __outer_env(std::execution::get_env(__outer_recv))
        , __ops(std::in_place_type<__forge_detail::__op_holder<__sched_op_t>>,
              [&]() -> __sched_op_t {
                  return std::execution::connect(std::execution::schedule(__sch), __sched_recv{this, std::addressof(__outer_recv)});
              }) {}

    void start() & noexcept {
        std::execution::start(std::get<__forge_detail::__op_holder<__sched_op_t>>(__ops).__op);
    }
};

template<class Scheduler, class S>
struct __starts_on_sender {
    using sender_concept = sender_tag;
    using scheduler_t = Scheduler;
    using source_t = S;
    Scheduler __sch;
    S __sndr;

    template<class Env>
    using __cs = typename __starts_cs<Scheduler, S, Env>::type;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> __cs<Env> { return {}; }

    template<receiver R>
        requires requires { typename __cs<env_of_t<R>>; } && __starts_connectable<Scheduler, S, R>
    auto connect(R r) && -> __starts_on_op<Scheduler, S, R> {
        return __starts_on_op<Scheduler, S, R>(std::move(__sch), std::move(__sndr), std::move(r));
    }
    template<receiver R>
        requires std::copy_constructible<Scheduler> && std::copy_constructible<S> &&
                 requires { typename __cs<env_of_t<R>>; } && __starts_connectable<Scheduler, S, R>
    auto connect(R r) const& -> __starts_on_op<Scheduler, S, R> {
        return __starts_on_op<Scheduler, S, R>(__sch, __sndr, std::move(r));
    }
    auto get_env() const noexcept {
        return __forge_env_detail::__forwarding_attrs<env_of_t<S>>{std::execution::get_env(__sndr)};
    }
};

} // namespace __forge_on

struct starts_on_t {
    template<class Scheduler, sender S>
        requires scheduler<Scheduler>
    [[nodiscard]] auto operator()(Scheduler&& sch, S&& sndr) const {
        return __forge_on::__starts_on_sender<
            std::remove_cvref_t<Scheduler>, std::decay_t<S>>{
            __forge_detail::__forward_as_given(std::forward<Scheduler>(sch)),
            __forge_detail::__forward_as_given(std::forward<S>(sndr))};
    }
};

inline constexpr starts_on_t starts_on{};

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
    using operation_state_concept = operation_state_tag;
    using inner_op_t = __inner_op_t<Scheduler, S, R>;

    inner_op_t __inner_op;

    static auto __connect(Scheduler sch, S sndr, R rcvr) -> inner_op_t {
        auto orig_sch = std::execution::get_start_scheduler(std::execution::get_env(rcvr));
        auto composed = std::execution::continues_on(
            std::execution::starts_on(std::move(sch), std::move(sndr)),
            std::move(orig_sch));
        return std::execution::connect(std::move(composed), std::move(rcvr));
    }

    __op(Scheduler sch, S sndr, R rcvr)
        : __inner_op(__connect(std::move(sch), std::move(sndr), std::move(rcvr)))
    {}

    void start() & noexcept {
        std::execution::start(__inner_op);
    }
};

template<class ChildEnv>
struct __attrs {
    ChildEnv __child;
    template<class Query, class... As>
        requires (!__forge_continues_on::__completion_query_v<std::remove_cvref_t<Query>>) &&
                 __forge_env_detail::__fwd_env_query<Query> &&
                 __forge_env_detail::__queryable<Query, const ChildEnv&, As...>
    constexpr decltype(auto) query(Query q, As&&... as) const
        noexcept(__forge_env_detail::__nothrow_query<Query, const ChildEnv&, As...>) {
        return __forge_env_detail::__query(q, __child, static_cast<As&&>(as)...);
    }
    template<class Query, class... As>
        requires requires(const __attrs& self, Query q, As&&... as) { self.query(q, static_cast<As&&>(as)...); }
    friend decltype(auto) tag_invoke(Query q, const __attrs& self, As&&... as)
        noexcept(noexcept(self.query(q, static_cast<As&&>(as)...))) {
        return self.query(q, static_cast<As&&>(as)...);
    }
};

template<class Scheduler, class S>
struct __sender {
    using sender_concept = sender_tag;
    using scheduler_t = Scheduler;
    using source_t = S;

    Scheduler __sch;
    S __sndr;

    template<class Env>
    using __cs = completion_signatures_of_t<__composed_sender_t<Scheduler, S,
        decltype(std::execution::get_start_scheduler(std::declval<Env>()))>, Env>;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> __cs<Env> { return {}; }

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

    auto get_env() const noexcept -> __attrs<env_of_t<S>> {
        return {std::execution::get_env(__sndr)};
    }
};

template<class S, class Env>
using __completion_scheduler_t = decltype(
    std::execution::get_completion_scheduler<set_value_t>(
        std::execution::get_env(std::declval<const S&>()), std::declval<Env>()));

template<class S, class Scheduler>
using __shifted_sender_t = decltype(std::execution::continues_on(
    std::declval<S>(), std::declval<Scheduler>()));

template<class Closure, class Shifted>
using __closure_result_t = decltype(std::declval<Closure>()(std::declval<Shifted>()));

template<class S, class Scheduler, class Closure, class OrigScheduler>
using __closure_composed_sender_t = decltype(std::execution::continues_on(
    std::declval<__closure_result_t<Closure, __shifted_sender_t<S, Scheduler>>>(),
    std::declval<OrigScheduler>()));

template<class S, class Scheduler, class Closure, class Env>
concept __closure_signatures_available = requires {
    typename __completion_scheduler_t<S, Env>;
    typename __closure_composed_sender_t<
        S, Scheduler, Closure, __completion_scheduler_t<S, Env>>;
} && sender_in<__closure_composed_sender_t<
        S, Scheduler, Closure, __completion_scheduler_t<S, Env>>, Env>;

template<class S, class Scheduler, class Closure, class R>
using __closure_inner_op_t = connect_result_t<
    __closure_composed_sender_t<
        S, Scheduler, Closure, __completion_scheduler_t<S, env_of_t<R>>>, R>;

template<class S, class Scheduler, class Closure, class R>
concept __closure_form_connectable =
    requires {
        typename __completion_scheduler_t<S, env_of_t<R>>;
        typename __closure_inner_op_t<S, Scheduler, Closure, R>;
    } &&
    requires(S&& sndr, Scheduler&& sch, Closure&& closure, R&& r) {
        std::execution::connect(
            std::execution::continues_on(
                static_cast<Closure&&>(closure)(
                    std::execution::continues_on(
                        static_cast<S&&>(sndr),
                        static_cast<Scheduler&&>(sch))),
                std::execution::get_completion_scheduler<set_value_t>(
                    std::execution::get_env(sndr), std::execution::get_env(r))),
            static_cast<R&&>(r));
    };

template<class S, class Scheduler, class Closure, class R>
struct __closure_op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_tag;
    using inner_op_t = __closure_inner_op_t<S, Scheduler, Closure, R>;

    inner_op_t __inner_op;

    static auto __connect(S sndr, Scheduler sch, Closure closure, R rcvr) -> inner_op_t {
        auto orig_sch = std::execution::get_completion_scheduler<set_value_t>(
            std::execution::get_env(sndr), std::execution::get_env(rcvr));
        auto shifted = std::execution::continues_on(std::move(sndr), std::move(sch));
        auto adapted = std::move(closure)(std::move(shifted));
        auto composed = std::execution::continues_on(std::move(adapted), std::move(orig_sch));
        return std::execution::connect(std::move(composed), std::move(rcvr));
    }

    __closure_op(S sndr, Scheduler sch, Closure closure, R rcvr)
        : __inner_op(__connect(std::move(sndr), std::move(sch),
              std::move(closure), std::move(rcvr)))
    {}

    void start() & noexcept {
        std::execution::start(__inner_op);
    }
};

template<class S, class Scheduler, class Closure>
struct __closure_sender {
    using sender_concept = sender_tag;
    using source_t = S;
    using scheduler_t = Scheduler;
    using closure_t = Closure;

    S __sndr;
    Scheduler __sch;
    Closure __closure;

    template<class Self, class Env>
        requires __closure_signatures_available<
            typename std::remove_cvref_t<Self>::source_t,
            typename std::remove_cvref_t<Self>::scheduler_t,
            typename std::remove_cvref_t<Self>::closure_t, Env>
    static auto get_completion_signatures() noexcept {
        using self_t = std::remove_cvref_t<Self>;
        using source_t = typename self_t::source_t;
        using scheduler_t = typename self_t::scheduler_t;
        using closure_t = typename self_t::closure_t;
        using orig_scheduler_t = __completion_scheduler_t<source_t, Env>;
        using composed_t = __closure_composed_sender_t<
            source_t, scheduler_t, closure_t, orig_scheduler_t>;
        return decltype(std::execution::get_completion_signatures(
            std::declval<composed_t>(), std::declval<Env>())){};
    }

    template<receiver R>
        requires __closure_form_connectable<S, Scheduler, Closure, R>
    auto connect(R r) && -> __closure_op<S, Scheduler, Closure, R> {
        return __closure_op<S, Scheduler, Closure, R>{
            std::move(__sndr), std::move(__sch), std::move(__closure), std::move(r)};
    }

    template<receiver R>
        requires std::copy_constructible<S> && std::copy_constructible<Scheduler> &&
                 std::copy_constructible<Closure> &&
                 __closure_form_connectable<const S&, const Scheduler&, const Closure&, R>
    auto connect(R r) const& -> __closure_op<S, Scheduler, Closure, R> {
        return __closure_op<S, Scheduler, Closure, R>{__sndr, __sch, __closure, std::move(r)};
    }

    auto get_env() const noexcept {
        return std::execution::get_env(__sndr);
    }
};

template<class Scheduler, class Closure>
struct __bound_closure : sender_adaptor_closure<__bound_closure<Scheduler, Closure>> {
    Scheduler __sch;
    Closure __closure;

    template<class Sch, class C>
        requires std::constructible_from<Scheduler, Sch> &&
                 std::constructible_from<Closure, C>
    constexpr __bound_closure(Sch&& sch, C&& closure)
        noexcept(std::is_nothrow_constructible_v<Scheduler, Sch> &&
                 std::is_nothrow_constructible_v<Closure, C>)
        : __sch(static_cast<Sch&&>(sch))
        , __closure(static_cast<C&&>(closure))
    {}

    template<sender S>
        requires std::constructible_from<Scheduler, Scheduler&> &&
                 std::constructible_from<Closure, Closure&>
    [[nodiscard]] constexpr auto operator()(S&& sndr) &
        noexcept(std::is_nothrow_constructible_v<std::decay_t<S>, S> &&
                 std::is_nothrow_constructible_v<Scheduler, Scheduler&> &&
                 std::is_nothrow_constructible_v<Closure, Closure&>)
        -> __closure_sender<std::decay_t<S>, Scheduler, Closure> {
        return {std::decay_t<S>(static_cast<S&&>(sndr)),
                Scheduler(__sch), Closure(__closure)};
    }

    template<sender S>
        requires std::constructible_from<Scheduler, const Scheduler&> &&
                 std::constructible_from<Closure, const Closure&>
    [[nodiscard]] constexpr auto operator()(S&& sndr) const&
        noexcept(std::is_nothrow_constructible_v<std::decay_t<S>, S> &&
                 std::is_nothrow_constructible_v<Scheduler, const Scheduler&> &&
                 std::is_nothrow_constructible_v<Closure, const Closure&>)
        -> __closure_sender<std::decay_t<S>, Scheduler, Closure> {
        return {std::decay_t<S>(static_cast<S&&>(sndr)),
                Scheduler(__sch), Closure(__closure)};
    }

    template<sender S>
        requires std::constructible_from<Scheduler, Scheduler&&> &&
                 std::constructible_from<Closure, Closure&&>
    [[nodiscard]] constexpr auto operator()(S&& sndr) &&
        noexcept(std::is_nothrow_constructible_v<std::decay_t<S>, S> &&
                 std::is_nothrow_constructible_v<Scheduler, Scheduler&&> &&
                 std::is_nothrow_constructible_v<Closure, Closure&&>)
        -> __closure_sender<std::decay_t<S>, Scheduler, Closure> {
        return {std::decay_t<S>(static_cast<S&&>(sndr)),
                Scheduler(std::move(__sch)), Closure(std::move(__closure))};
    }

    template<sender S>
        requires std::constructible_from<Scheduler, const Scheduler&&> &&
                 std::constructible_from<Closure, const Closure&&>
    [[nodiscard]] constexpr auto operator()(S&& sndr) const&&
        noexcept(std::is_nothrow_constructible_v<std::decay_t<S>, S> &&
                 std::is_nothrow_constructible_v<Scheduler, const Scheduler&&> &&
                 std::is_nothrow_constructible_v<Closure, const Closure&&>)
        -> __closure_sender<std::decay_t<S>, Scheduler, Closure> {
        return {std::decay_t<S>(static_cast<S&&>(sndr)),
                Scheduler(std::move(__sch)), Closure(std::move(__closure))};
    }

    // Invalid capture categories must not fall back to another cvref overload.
    template<class S> void operator()(S&&) & = delete;
    template<class S> void operator()(S&&) const& = delete;
    template<class S> void operator()(S&&) && = delete;
    template<class S> void operator()(S&&) const&& = delete;
};

template<class S, class Scheduler, class Closure>
using __on_closure_sender_t =
    __closure_sender<std::decay_t<S>, std::remove_cvref_t<Scheduler>, std::decay_t<Closure>>;

template<class S, class Scheduler, class Closure>
concept __on_closure_constructible = requires(S&& sndr, Scheduler&& sch, Closure&& closure) {
    __on_closure_sender_t<S, Scheduler, Closure>{
        std::forward<S>(sndr), std::forward<Scheduler>(sch), std::forward<Closure>(closure)};
};

struct on_t {
    template<class Scheduler, sender S>
        requires scheduler<Scheduler>
    [[nodiscard]] auto operator()(Scheduler&& sch, S&& sndr) const {
        return __sender<std::remove_cvref_t<Scheduler>, std::decay_t<S>>{
            __forge_detail::__forward_as_given(std::forward<Scheduler>(sch)),
            __forge_detail::__forward_as_given(std::forward<S>(sndr))};
    }

    template<class Scheduler, class Closure>
        requires scheduler<Scheduler> &&
                 __forge_adaptor::__closure<Closure> &&
                 std::constructible_from<std::remove_cvref_t<Scheduler>, Scheduler> &&
                 std::constructible_from<std::decay_t<Closure>, Closure>
    [[nodiscard]] constexpr auto operator()(Scheduler&& sch, Closure&& closure) const
        noexcept(std::is_nothrow_constructible_v<std::remove_cvref_t<Scheduler>, Scheduler> &&
                 std::is_nothrow_constructible_v<std::decay_t<Closure>, Closure>)
        -> __bound_closure<std::remove_cvref_t<Scheduler>, std::decay_t<Closure>> {
        return {static_cast<Scheduler&&>(sch), static_cast<Closure&&>(closure)};
    }

    template<class Scheduler, class Closure>
        requires scheduler<Scheduler> &&
                 (!sender<std::decay_t<Closure>>) &&
                 (!__forge_adaptor::__closure<Closure>) &&
                 std::constructible_from<std::remove_cvref_t<Scheduler>, Scheduler> &&
                 std::constructible_from<std::decay_t<Closure>, Closure>
    [[deprecated("pass a sender_adaptor_closure to std::execution::on")]]
    [[nodiscard]] constexpr auto operator()(Scheduler&& sch, Closure&& closure) const
        noexcept(std::is_nothrow_constructible_v<std::remove_cvref_t<Scheduler>, Scheduler> &&
                 std::is_nothrow_constructible_v<std::decay_t<Closure>, Closure>)
        -> __bound_closure<std::remove_cvref_t<Scheduler>, std::decay_t<Closure>> {
        return {static_cast<Scheduler&&>(sch), static_cast<Closure&&>(closure)};
    }

    template<sender S, class Scheduler, class Closure>
        requires scheduler<Scheduler> && __forge_adaptor::__closure<Closure> &&
                 __on_closure_constructible<S, Scheduler, Closure>
    [[nodiscard]] auto operator()(S&& sndr, Scheduler&& sch, Closure&& closure) const
        -> __on_closure_sender_t<S, Scheduler, Closure> {
        return {__forge_detail::__forward_as_given(std::forward<S>(sndr)),
                __forge_detail::__forward_as_given(std::forward<Scheduler>(sch)),
                __forge_detail::__forward_as_given(std::forward<Closure>(closure))};
    }

    template<sender S, class Scheduler, class Closure>
        requires scheduler<Scheduler> && (!__forge_adaptor::__closure<Closure>) &&
                 __on_closure_constructible<S, Scheduler, Closure>
    [[deprecated("pass a sender_adaptor_closure to std::execution::on")]]
    [[nodiscard]] auto operator()(S&& sndr, Scheduler&& sch, Closure&& closure) const
        -> __on_closure_sender_t<S, Scheduler, Closure> {
        return {__forge_detail::__forward_as_given(std::forward<S>(sndr)),
                __forge_detail::__forward_as_given(std::forward<Scheduler>(sch)),
                __forge_detail::__forward_as_given(std::forward<Closure>(closure))};
    }
};

} // namespace __forge_on_adaptor

using on_t = __forge_on_adaptor::on_t;
inline constexpr on_t on{};

} // namespace std::execution

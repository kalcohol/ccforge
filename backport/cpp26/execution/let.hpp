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
#include "detail/op_holder.hpp"
#include "detail/adaptor.hpp"
#include "env.hpp"

#include <exception>
#include <functional>
#include <type_traits>
#include <tuple>
#include <utility>
#include <variant>

namespace std::execution {

namespace __forge_let {

struct __value_tag  {};
struct __error_tag  {};
struct __stopped_tag{};

template<class Which>
struct __set_cpo;

template<>
struct __set_cpo<__value_tag> {
    using type = set_value_t;
};

template<>
struct __set_cpo<__error_tag> {
    using type = set_error_t;
};

template<>
struct __set_cpo<__stopped_tag> {
    using type = set_stopped_t;
};

template<class Which>
using __set_cpo_t = typename __set_cpo<Which>::type;

// FWD-ENV(get_env(rcvr)), the second argument of the completion queries
// in let-env ([exec.let]/2).
template<class Env>
using __forwarding_env = __forge_env_detail::__fwd_env_view<Env>;

// SCHED-ENV(sch) of [exec.snd.expos]/10. The view owns a copy of the selected
// scheduler and queries it as a const lvalue, like every forwarded
// environment: a repeated query sees the same intact scheduler, and an
// answer that refers into it stays valid as long as the view.
template<class Scheduler>
struct __scheduler_env {
    [[no_unique_address]] Scheduler __scheduler;

    // The answer is a prvalue copy. [exec.sched]/3 forbids a scheduler copy
    // from exiting via an exception whatever its exception specification
    // says, so this meets MANDATE-NOTHROW of [exec.get.start.scheduler]/2.
    constexpr auto query(get_start_scheduler_t) const noexcept -> Scheduler {
        return __scheduler;
    }

    constexpr decltype(auto) query(get_domain_t) const
        noexcept(noexcept(std::declval<const Scheduler&>().query(std::execution::get_domain)))
        requires requires(const Scheduler& sch) { sch.query(std::execution::get_domain); } {
        return __scheduler.query(std::execution::get_domain);
    }
};

// Model the outer receiver for type-only signature and connect probes.
template<class Env>
struct __signature_receiver {
    using receiver_concept = receiver_tag;
    template<class... As> void set_value(As&&...) && noexcept {}
    template<class E> void set_error(E&&) && noexcept {}
    void set_stopped() && noexcept {}
    // A reference, so that no function returns an array or abstract type.
    auto get_env() const noexcept -> const std::remove_reference_t<Env>& { std::terminate(); }
};

// let-env answers with the supplied query's own qualification.
template<class Query, class PrimaryEnv, class... Args>
concept __from_primary = requires(const PrimaryEnv& primary, Query&& q, Args&&... args) {
    primary.query(static_cast<Query&&>(q), static_cast<Args&&>(args)...);
};

template<class Query, class PrimaryEnv, class... Args>
concept __nothrow_from_primary = requires(const PrimaryEnv& primary, Query&& q, Args&&... args) {
    { primary.query(static_cast<Query&&>(q), static_cast<Args&&>(args)...) } noexcept;
};

// receiver2 applies the forwarding-query type concept ([exec.let]/8.2); the
// signature view applies FWD-ENV to the supplied object (/9.4).
template<class Query, bool ClassifyObject>
concept __forwards =
    (ClassifyObject && __forge_env_detail::__qualified_fwd_env_query<Query>) ||
    (!ClassifyObject &&
     __forge_env_detail::__forwarding_query_type<std::remove_cvref_t<Query>>);

// The successor environment has two roles. Both types depend only on the
// let-env type and on the type of the outer environment without reference and
// const, never on the receiver type or on whether that environment is borrowed.
//
// Connected (ClassifyObject == false) is the environment of receiver2 in
// [exec.let]/8.2: let-env answers first, then the environment of the outer
// receiver for a query type that satisfies forwarding-query.
//
// Signature (ClassifyObject == true) is JOIN-ENV(let-env, FWD-ENV(env)) of
// [exec.let]/9.4, whose FWD-ENV classifies the supplied query object.
//
// The outer environment is queried as a const lvalue in both.
template<class PrimaryEnv, class OuterEnv, bool ClassifyObject>
struct __successor_env {
    template<class Query, class... Args>
    static consteval bool __query_nothrow() {
        if constexpr (__from_primary<Query, PrimaryEnv, Args...>) {
            return __nothrow_from_primary<Query, PrimaryEnv, Args...>;
        } else {
            return __forge_env_detail::__nothrow_raw_queryable<Query, OuterEnv, Args...>;
        }
    }

    const PrimaryEnv* __primary;
    // Points into the operation state, like __primary.
    const OuterEnv* __outer;

    template<class Query, class... Args>
        requires __from_primary<Query, PrimaryEnv, Args...> ||
                 (__forwards<Query, ClassifyObject> &&
                  __forge_env_detail::__raw_queryable<Query, OuterEnv, Args...>)
    constexpr decltype(auto) query(Query&& query, Args&&... args) const
        noexcept(__query_nothrow<Query, Args...>()) {
        if constexpr (__from_primary<Query, PrimaryEnv, Args...>) {
            return this->__primary->query(
                static_cast<Query&&>(query), static_cast<Args&&>(args)...);
        } else {
            return this->__outer->query(
                static_cast<Query&&>(query), static_cast<Args&&>(args)...);
        }
    }
};

template<class PrimaryEnv, class OuterEnv, bool ClassifyObject = false>
using __inner_env = __successor_env<
    PrimaryEnv, __forge_env_detail::__normalized_env_t<OuterEnv>, ClassifyObject>;

// env is the receiver environment that the operation state keeps.
template<class Which, class S, class OuterEnv>
auto __make_let_env(const S& sndr, const OuterEnv& env) {
    using cpo_t = __set_cpo_t<Which>;
    using fwd_t = __forwarding_env<OuterEnv>;
    decltype(auto) child_env = std::execution::get_env(sndr);
    if constexpr (requires {
        std::execution::get_completion_scheduler<cpo_t>(child_env, fwd_t::__borrow(env));
    }) {
        // Own the result: the query can refer into the temporary view or attrs.
        return __scheduler_env<std::remove_cvref_t<decltype(
            std::execution::get_completion_scheduler<cpo_t>(child_env, fwd_t::__borrow(env)))>>{
            std::execution::get_completion_scheduler<cpo_t>(child_env, fwd_t::__borrow(env))};
    } else if constexpr (requires {
        std::execution::get_completion_domain<cpo_t>(child_env, fwd_t::__borrow(env));
    }) {
        auto domain = std::execution::get_completion_domain<cpo_t>(
            child_env, fwd_t::__borrow(env));
        return std::execution::prop(
            get_domain_t{},
            std::move(domain));
    } else {
        return __empty_env{};
    }
}

// let-env depends on the environment type without reference and const.
template<class Which, class S, class OuterEnv>
using __let_env_t = decltype(__make_let_env<Which>(
    std::declval<const S&>(),
    std::declval<const __forge_env_detail::__normalized_env_t<OuterEnv>&>()));

template<class Sig>
struct __is_value_sig : std::false_type {};

template<class... Vs>
struct __is_value_sig<set_value_t(Vs...)> : std::true_type {};

template<class Sig>
struct __is_error_sig : std::false_type {};

template<class E>
struct __is_error_sig<set_error_t(E)> : std::true_type {};

template<class Which, class Sig>
struct __matches_which : std::false_type {};

template<class... Vs>
struct __matches_which<__value_tag, set_value_t(Vs...)> : std::true_type {};

template<class E>
struct __matches_which<__error_tag, set_error_t(E)> : std::true_type {};

template<>
struct __matches_which<__stopped_tag, set_stopped_t()> : std::true_type {};

template<bool Enabled>
struct __maybe_eptr_cs {
    using type = completion_signatures<>;
};

template<>
struct __maybe_eptr_cs<true> {
    using type = completion_signatures<set_error_t(std::exception_ptr)>;
};

template<class R, class LetEnv>
struct __inner_recv {
    using receiver_concept = receiver_tag;
    R* __outer;
    const LetEnv* __primary;
    // The environment that the operation state keeps for this receiver.
    const __forge_env_detail::__receiver_env<R>* __env = nullptr;
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
    auto get_env() const noexcept -> __inner_env<LetEnv, env_of_t<R>> {
        return {__primary, std::addressof(__env->__get())};
    }
};

// [exec.let]/9.4 checks sender_in against
// JOIN-ENV(let-env(declval<child>(), declval<Env>()), FWD-ENV(declval<Env>())).
template<class Which, class S, class Env>
using __signature_env_t = __inner_env<__let_env_t<Which, S, Env>, Env, true>;

// The environment of every receiver2 whose outer receiver has an environment
// of type Env. The successor's reported completions come from this view, so
// they are the completions of the connection that start() runs
// ([exec.snd.expos]/47.1, [exec.getcomplsigs]/5).
template<class PrimaryEnv, class Env>
using __connected_env_t = __inner_env<PrimaryEnv, Env, false>;

// Type-only stand-in for receiver2 in exception-specification and
// connectability probes. Its environment is __connected_env_t.
template<class PrimaryEnv, class Env>
using __connect_probe_t = __inner_recv<
    __signature_receiver<__forge_env_detail::__normalized_env_t<Env>>, PrimaryEnv>;

template<class Fn, class Recv, class... As>
inline constexpr bool __let_nothrow =
    std::is_nothrow_constructible_v<std::tuple<std::decay_t<As>...>, As...> &&
    std::is_nothrow_invocable_v<Fn, std::decay_t<As>&...> &&
    noexcept(std::execution::connect(
        std::declval<std::invoke_result_t<Fn, std::decay_t<As>&...>>(),
        std::declval<Recv>()));

template<class S, class Fn, class Which, class Env, class Sig,
         bool = __matches_which<Which, Sig>::value, class = void>
struct __let_sig {};

template<class S, class Fn, class Which, class Env, class Sig>
struct __let_sig<S, Fn, Which, Env, Sig, false, void> {
    using type = completion_signatures<Sig>;
};

template<class S, class Fn, class Which, class Env, class Tag, class... As>
    requires (std::constructible_from<std::decay_t<As>, As> && ...) &&
             std::invocable<Fn, std::decay_t<As>&...> &&
             sender_in<std::invoke_result_t<Fn, std::decay_t<As>&...>,
                       __signature_env_t<Which, S, Env>> &&
             sender_in<std::invoke_result_t<Fn, std::decay_t<As>&...>,
                       __connected_env_t<__let_env_t<Which, S, Env>, Env>> &&
             requires {
                 typename connect_result_t<std::invoke_result_t<Fn, std::decay_t<As>&...>,
                     __connect_probe_t<__let_env_t<Which, S, Env>, Env>>;
             }
struct __let_sig<S, Fn, Which, Env, Tag(As...), true, void> {
    using inner_sender_t = std::invoke_result_t<Fn, std::decay_t<As>&...>;
    using primary_env = __let_env_t<Which, S, Env>;
    static constexpr bool nothrow = __let_nothrow<Fn, __connect_probe_t<primary_env, Env>, As...>;
    using type = __forge_meta::__concat_unique_cs_t<
        completion_signatures_of_t<inner_sender_t, __connected_env_t<primary_env, Env>>,
        typename __maybe_eptr_cs<!nothrow>::type>;
};

template<class S, class Fn, class Which, class Env, class CS, class = void>
struct __let_cs {};

template<class S, class Fn, class Which, class Env, class... Sigs>
struct __let_cs<S, Fn, Which, Env, completion_signatures<Sigs...>,
    std::void_t<typename __let_sig<S, Fn, Which, Env, Sigs>::type...>> {
    using type = __forge_meta::__concat_unique_cs_t<
        typename __let_sig<S, Fn, Which, Env, Sigs>::type...>;
};

template<class List>
struct __as_variant;

template<class... Ts>
struct __as_variant<__forge_meta::type_list<Ts...>> {
    using type = std::variant<Ts...>;
};

template<class Fn, class Recv, class Sig, bool Matches>
struct __inner_types {
    using args = __forge_meta::type_list<>;
    using ops = __forge_meta::type_list<>;
};

template<class Fn, class Recv, class Tag, class... As>
struct __inner_types<Fn, Recv, Tag(As...), true> {
    using args = __forge_meta::type_list<std::tuple<std::decay_t<As>...>>;
    using ops = __forge_meta::type_list<__forge_detail::__op_holder<connect_result_t<
        std::invoke_result_t<Fn, std::decay_t<As>&...>, Recv>>>;
};

template<class List, class... Lists>
struct __merge_types;

template<class List>
struct __merge_types<List> {
    using type = List;
};

template<class List, class... Ts, class... Rest>
struct __merge_types<List, __forge_meta::type_list<Ts...>, Rest...>
    : __merge_types<__forge_meta::list_push_unique_all_t<List, Ts...>, Rest...> {};

template<class Fn, class Recv, class Which, class Predecessor, class CS>
struct __state_types;

template<class Fn, class Recv, class Which, class Predecessor, class... Sigs>
struct __state_types<Fn, Recv, Which, Predecessor, completion_signatures<Sigs...>> {
    template<class Sig>
    using __types = __inner_types<Fn, Recv, Sig, __matches_which<Which, Sig>::value>;

    using args = typename __as_variant<typename __merge_types<
        __forge_meta::type_list<std::monostate>, typename __types<Sigs>::args...>::type>::type;
    using ops = typename __as_variant<typename __merge_types<
        __forge_meta::type_list<std::monostate, __forge_detail::__op_holder<Predecessor>>,
        typename __types<Sigs>::ops...>::type>::type;
};

template<class S, class Fn, class R, class Which>
struct __op;

template<class S, class Fn, class R, class Which>
struct __outer_recv {
    using receiver_concept = receiver_tag;
    __op<S, Fn, R, Which>* __self;
    R* __outer;

    template<class... Vs>
    void set_value(Vs&&... vs) && noexcept {
        if constexpr (std::is_same_v<Which, __value_tag>) {
            __self->__start_inner(static_cast<Vs&&>(vs)...);
        } else {
            std::execution::set_value(std::move(__self->__outer_recv_), static_cast<Vs&&>(vs)...);
        }
    }
    template<class E>
    void set_error(E&& e) && noexcept {
        if constexpr (std::is_same_v<Which, __error_tag>) {
            __self->__start_inner(static_cast<E&&>(e));
        } else {
            std::execution::set_error(std::move(__self->__outer_recv_), static_cast<E&&>(e));
        }
    }
    void set_stopped() && noexcept {
        if constexpr (std::is_same_v<Which, __stopped_tag>) {
            __self->__start_inner();
        } else {
            std::execution::set_stopped(std::move(__self->__outer_recv_));
        }
    }
    auto get_env() const noexcept -> env_of_t<R> {
        return std::execution::get_env(*__outer);
    }
};

template<class Fn, class Recv, class Which, class Sig>
struct __successor_connectable : std::true_type {};

template<class Fn, class Recv, class Which, class Tag, class... As>
    requires __matches_which<Which, Tag(As...)>::value
struct __successor_connectable<Fn, Recv, Which, Tag(As...)>
    : std::bool_constant<requires {
        typename connect_result_t<std::invoke_result_t<Fn, std::decay_t<As>&...>, Recv>;
    }> {};

template<class Fn, class Recv, class Which, class CS>
struct __successors_connectable : std::false_type {};

template<class Fn, class Recv, class Which, class... Sigs>
struct __successors_connectable<Fn, Recv, Which, completion_signatures<Sigs...>>
    : std::bool_constant<(__successor_connectable<Fn, Recv, Which, Sigs>::value && ...)> {};

template<class S, class Fn, class R, class Which>
concept __connectable = requires {
    typename connect_result_t<S, __outer_recv<S, Fn, R, Which>>;
} && __successors_connectable<Fn,
    __inner_recv<R, __let_env_t<Which, S, env_of_t<R>>>,
    Which, completion_signatures_of_t<S, env_of_t<R>>>::value;

template<class S, class Fn, class R, class Which>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_tag;
    using outer_env_t = env_of_t<R>;
    using let_env_t = __let_env_t<Which, S, outer_env_t>;
    R __outer_recv_;
    // Outlives every view of the successor's receiver environment.
    __forge_env_detail::__receiver_env<R> __outer_env_;
    let_env_t __let_env_;
    Fn __fn_;

    using __inner_recv = __forge_let::__inner_recv<R, let_env_t>;
    static_assert(std::same_as<env_of_t<__inner_recv>,
                               __connected_env_t<let_env_t, outer_env_t>>);

    template<class... Vs>
    void __start_inner(Vs&&... vs) noexcept {
        try {
            using args_t = std::tuple<std::decay_t<Vs>...>;
            auto& args = __args_.template emplace<args_t>(static_cast<Vs&&>(vs)...);
            __inner_ops_.template emplace<std::monostate>();
            auto&& inner_sndr = std::apply(std::move(__fn_), args);
            using inner_sender_t = std::invoke_result_t<Fn, std::decay_t<Vs>&...>;
            using inner_op_t = connect_result_t<inner_sender_t, __inner_recv>;
            auto& holder = __inner_ops_.template emplace<__forge_detail::__op_holder<inner_op_t>>(
                [&]() -> inner_op_t {
                    return std::execution::connect(
                        static_cast<inner_sender_t&&>(inner_sndr), __inner_recv{
                            std::addressof(__outer_recv_), std::addressof(__let_env_),
                            std::addressof(__outer_env_)});
                });
            std::execution::start(holder.__op);
        } catch (...) {
            using args_t = std::tuple<std::decay_t<Vs>...>;
            using inner_sender_t = std::invoke_result_t<Fn, std::decay_t<Vs>&...>;
            constexpr bool nothrow = __let_nothrow<Fn, __inner_recv, Vs...>;
            if constexpr (!nothrow) {
                std::execution::set_error(std::move(__outer_recv_), std::current_exception());
            }
        }
    }

    using __outer_recv = __forge_let::__outer_recv<S, Fn, R, Which>;

    using __outer_op_t = connect_result_t<S, __outer_recv>;
    using __types = __state_types<Fn, __inner_recv, Which, __outer_op_t,
        completion_signatures_of_t<S, outer_env_t>>;

    // Decay-copy the arguments, then replace the predecessor before invoking
    // the successor function. The same variant owns either operation.
    typename __types::args __args_;
    typename __types::ops __inner_ops_;

    __op(S sndr, Fn fn, R r)
        : __outer_recv_(std::move(r))
        , __outer_env_(__outer_recv_)
        , __let_env_(__make_let_env<Which>(sndr, __outer_env_.__get()))
        , __fn_(std::move(fn))
        , __inner_ops_(std::in_place_type<__forge_detail::__op_holder<__outer_op_t>>,
              [&]() -> __outer_op_t {
                  return std::execution::connect(std::move(sndr), __outer_recv{this, std::addressof(__outer_recv_)});
              })
    {}

    void start() & noexcept {
        std::execution::start(std::get<__forge_detail::__op_holder<__outer_op_t>>(__inner_ops_).__op);
    }
};

template<class S, class Fn, class Which>
struct __sender {
    using sender_concept = sender_tag;
    using source_t = S;
    using fn_t = Fn;
    using which_t = Which;

    S __sndr;
    Fn __fn;

    template<class Env>
    using __cs = typename __let_cs<S, Fn, Which, Env,
        completion_signatures_of_t<S, Env>>::type;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept -> __cs<Env> { return {}; }

    template<receiver R>
        requires requires { typename __cs<env_of_t<R>>; } && __connectable<S, Fn, R, Which>
    auto connect(R r) && -> __op<S, Fn, R, Which> {
        return __op<S, Fn, R, Which>(
            std::move(__sndr), std::move(__fn), std::move(r));
    }

    template<receiver R>
        requires std::copy_constructible<S> && std::copy_constructible<Fn> &&
                 requires { typename __cs<env_of_t<R>>; } && __connectable<S, Fn, R, Which>
    auto connect(R r) const& -> __op<S, Fn, R, Which> {
        return __op<S, Fn, R, Which>(__sndr, __fn, std::move(r));
    }

    auto get_env() const noexcept {
        return __forge_env_detail::__fwd_attrs_view<env_of_t<S>>::__of(__sndr);
    }
};

template<class Fn, class Which>
struct __let_closure : sender_adaptor_closure<__let_closure<Fn, Which>> {
    Fn __fn;
    template<sender S>
        requires std::constructible_from<Fn, Fn&> && std::move_constructible<Fn> &&
                 (!std::same_as<Which, __stopped_tag> || std::invocable<Fn>)
    [[nodiscard]] auto operator()(S&& s) & {
        return __sender<std::decay_t<S>, Fn, Which>{
            __forge_detail::__forward_as_given(std::forward<S>(s)), Fn(__fn)};
    }
    template<sender S>
        requires std::constructible_from<Fn, const Fn&> && std::move_constructible<Fn> &&
                 (!std::same_as<Which, __stopped_tag> || std::invocable<Fn>)
    [[nodiscard]] auto operator()(S&& s) const & {
        return __sender<std::decay_t<S>, Fn, Which>{
            __forge_detail::__forward_as_given(std::forward<S>(s)), Fn(__fn)};
    }
    template<sender S>
        requires std::constructible_from<Fn, Fn&&> && std::move_constructible<Fn> &&
                 (!std::same_as<Which, __stopped_tag> || std::invocable<Fn>)
    [[nodiscard]] auto operator()(S&& s) && {
        return __sender<std::decay_t<S>, Fn, Which>{
            __forge_detail::__forward_as_given(std::forward<S>(s)), Fn(std::move(__fn))};
    }
    template<sender S>
        requires std::constructible_from<Fn, const Fn&&> && std::move_constructible<Fn> &&
                 (!std::same_as<Which, __stopped_tag> || std::invocable<Fn>)
    [[nodiscard]] auto operator()(S&& s) const && {
        return __sender<std::decay_t<S>, Fn, Which>{
            __forge_detail::__forward_as_given(std::forward<S>(s)), Fn(std::move(__fn))};
    }

    // An invalid cvref must not fall back to a different capture category.
    template<class S> void operator()(S&&) & = delete;
    template<class S> void operator()(S&&) const & = delete;
    template<class S> void operator()(S&&) && = delete;
    template<class S> void operator()(S&&) const && = delete;
};

template<class Which>
struct __let_t {
    template<sender S, class Fn>
        requires __forge_detail::__movable_value<Fn> &&
                 (!std::same_as<Which, __stopped_tag> || std::invocable<std::decay_t<Fn>>)
    [[nodiscard]] auto operator()(S&& s, Fn&& fn) const {
        return __sender<std::decay_t<S>, std::decay_t<Fn>, Which>{
            std::decay_t<S>(std::forward<S>(s)),
            std::decay_t<Fn>(std::forward<Fn>(fn))};
    }
    template<class Fn>
        requires std::constructible_from<std::decay_t<Fn>, Fn>
    [[nodiscard]] auto operator()(Fn&& fn) const {
        return __let_closure<std::decay_t<Fn>, Which>{{}, std::decay_t<Fn>(std::forward<Fn>(fn))};
    }
};

} // namespace __forge_let

using let_value_t = __forge_let::__let_t<__forge_let::__value_tag>;
inline constexpr let_value_t let_value{};
using let_error_t = __forge_let::__let_t<__forge_let::__error_tag>;
inline constexpr let_error_t let_error{};
using let_stopped_t = __forge_let::__let_t<__forge_let::__stopped_tag>;
inline constexpr let_stopped_t let_stopped{};

} // namespace std::execution

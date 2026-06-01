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

#include "detail.hpp"
#include "env.hpp"

namespace std::execution {

// Forge limitation: receiver completion callbacks are required to be noexcept,
// including set_value. User-code exceptions should be translated by senders or
// adaptors into set_error(std::exception_ptr); throwing completion callbacks are
// not supported by this backport.
namespace __forge_cpo_detail {

template<class R, class... Vs>
concept __member_set_value = requires(R&& r, Vs&&... vs) {
    static_cast<R&&>(r).set_value(static_cast<Vs&&>(vs)...);
};

template<class R, class... Vs>
concept __nothrow_member_set_value = requires(R&& r, Vs&&... vs) {
    { static_cast<R&&>(r).set_value(static_cast<Vs&&>(vs)...) } noexcept -> std::same_as<void>;
};

template<class R, class E>
concept __member_set_error = requires(R&& r, E&& e) {
    static_cast<R&&>(r).set_error(static_cast<E&&>(e));
};

template<class R, class E>
concept __nothrow_member_set_error = requires(R&& r, E&& e) {
    { static_cast<R&&>(r).set_error(static_cast<E&&>(e)) } noexcept -> std::same_as<void>;
};

template<class R>
concept __member_set_stopped = requires(R&& r) {
    static_cast<R&&>(r).set_stopped();
};

template<class R>
concept __nothrow_member_set_stopped = requires(R&& r) {
    { static_cast<R&&>(r).set_stopped() } noexcept -> std::same_as<void>;
};

} // namespace __forge_cpo_detail

struct set_value_t {
    template<class R, class... Vs>
        requires (__forge_cpo_detail::__member_set_value<R, Vs...> ||
                  __forge_detail::tag_invocable<set_value_t, R, Vs...>)
    void operator()(R&& r, Vs&&... vs) const noexcept {
        if constexpr (__forge_cpo_detail::__member_set_value<R, Vs...>) {
            static_assert(__forge_cpo_detail::__nothrow_member_set_value<R, Vs...>,
                          "set_value(receiver, ...) must be noexcept and return void");
            static_cast<R&&>(r).set_value(static_cast<Vs&&>(vs)...);
        } else {
            static_assert(__forge_detail::nothrow_tag_invocable<set_value_t, R, Vs...>,
                          "set_value(receiver, ...) must be noexcept");
            __forge_detail::tag_invoke_fn(*this, static_cast<R&&>(r), static_cast<Vs&&>(vs)...);
        }
    }
};

struct set_error_t {
    template<class R, class E>
        requires (__forge_cpo_detail::__member_set_error<R, E> ||
                  __forge_detail::tag_invocable<set_error_t, R, E>)
    void operator()(R&& r, E&& e) const noexcept {
        if constexpr (__forge_cpo_detail::__member_set_error<R, E>) {
            static_assert(__forge_cpo_detail::__nothrow_member_set_error<R, E>,
                          "set_error(receiver, error) must be noexcept and return void");
            static_cast<R&&>(r).set_error(static_cast<E&&>(e));
        } else {
            static_assert(__forge_detail::nothrow_tag_invocable<set_error_t, R, E>,
                          "set_error(receiver, error) must be noexcept");
            __forge_detail::tag_invoke_fn(*this, static_cast<R&&>(r), static_cast<E&&>(e));
        }
    }
};

struct set_stopped_t {
    template<class R>
        requires (__forge_cpo_detail::__member_set_stopped<R> ||
                  __forge_detail::tag_invocable<set_stopped_t, R>)
    void operator()(R&& r) const noexcept {
        if constexpr (__forge_cpo_detail::__member_set_stopped<R>) {
            static_assert(__forge_cpo_detail::__nothrow_member_set_stopped<R>,
                          "set_stopped(receiver) must be noexcept and return void");
            static_cast<R&&>(r).set_stopped();
        } else {
            static_assert(__forge_detail::nothrow_tag_invocable<set_stopped_t, R>,
                          "set_stopped(receiver) must be noexcept");
            __forge_detail::tag_invoke_fn(*this, static_cast<R&&>(r));
        }
    }
};

inline constexpr set_value_t set_value{};
inline constexpr set_error_t set_error{};
inline constexpr set_stopped_t set_stopped{};

template<class... Sigs>
struct completion_signatures {};

struct get_completion_signatures_t;

namespace __forge_cpo_detail {

template<class S, class Env>
concept __static_completion_signatures = requires {
    std::remove_cvref_t<S>::template get_completion_signatures<S, std::remove_cvref_t<Env>>();
};

template<class S, class Env>
concept __nothrow_static_completion_signatures = requires {
    { std::remove_cvref_t<S>::template get_completion_signatures<S, std::remove_cvref_t<Env>>() } noexcept;
};

template<class S, class Env>
concept __instance_completion_signatures = requires(S&& s, Env&& env) {
    static_cast<S&&>(s).get_completion_signatures(static_cast<Env&&>(env));
};

template<class S, class Env>
concept __nothrow_instance_completion_signatures = requires(S&& s, Env&& env) {
    { static_cast<S&&>(s).get_completion_signatures(static_cast<Env&&>(env)) } noexcept;
};

template<class S, class Env>
consteval bool __completion_signatures_noexcept() {
    if constexpr (__static_completion_signatures<S, Env>) {
        return __nothrow_static_completion_signatures<S, Env>;
    } else if constexpr (__instance_completion_signatures<S, Env>) {
        return __nothrow_instance_completion_signatures<S, Env>;
    } else {
        return __forge_detail::nothrow_tag_invocable<get_completion_signatures_t, S, Env>;
    }
}

} // namespace __forge_cpo_detail

struct get_completion_signatures_t {
    template<class S, class Env>
        requires (__forge_cpo_detail::__static_completion_signatures<S, Env> ||
                  __forge_cpo_detail::__instance_completion_signatures<S, Env> ||
                  __forge_detail::tag_invocable<get_completion_signatures_t, S, Env>)
    auto operator()(S&& s, Env&& env) const
        noexcept(__forge_cpo_detail::__completion_signatures_noexcept<S, Env>()) {
        if constexpr (__forge_cpo_detail::__static_completion_signatures<S, Env>) {
            return std::remove_cvref_t<S>::template get_completion_signatures<S, std::remove_cvref_t<Env>>();
        } else if constexpr (__forge_cpo_detail::__instance_completion_signatures<S, Env>) {
            return static_cast<S&&>(s).get_completion_signatures(static_cast<Env&&>(env));
        } else {
            return __forge_detail::tag_invoke_fn(*this, static_cast<S&&>(s), static_cast<Env&&>(env));
        }
    }

    template<class S>
        requires (__forge_cpo_detail::__static_completion_signatures<S, empty_env> ||
                  __forge_cpo_detail::__instance_completion_signatures<S, empty_env> ||
                  __forge_detail::tag_invocable<get_completion_signatures_t, S, empty_env>)
    auto operator()(S&& s) const
        noexcept(__forge_cpo_detail::__completion_signatures_noexcept<S, empty_env>()) {
        return (*this)(static_cast<S&&>(s), empty_env{});
    }
};
inline constexpr get_completion_signatures_t get_completion_signatures{};

template<class S, class Env = empty_env>
using completion_signatures_of_t = decltype(
    std::execution::get_completion_signatures(std::declval<S>(), std::declval<Env>()));

namespace __forge_meta {

template<class... Ts>
struct type_list {};

template<class List, class T>
struct list_contains;

template<class... Ts, class T>
struct list_contains<type_list<Ts...>, T> : std::bool_constant<(std::is_same_v<T, Ts> || ...)> {};

template<class List, class T>
inline constexpr bool list_contains_v = list_contains<List, T>::value;

template<class List, class T>
struct list_push_unique;

template<class... Ts, class T>
struct list_push_unique<type_list<Ts...>, T> {
    using type = std::conditional_t<list_contains_v<type_list<Ts...>, T>, type_list<Ts...>, type_list<Ts..., T>>;
};

template<class List, class T>
using list_push_unique_t = typename list_push_unique<List, T>::type;

template<class List, class... Ts>
struct list_push_unique_all;

template<class List>
struct list_push_unique_all<List> {
    using type = List;
};

template<class List, class T, class... Rest>
struct list_push_unique_all<List, T, Rest...> {
    using next = list_push_unique_t<List, T>;
    using type = typename list_push_unique_all<next, Rest...>::type;
};

template<class List, class... Ts>
using list_push_unique_all_t = typename list_push_unique_all<List, Ts...>::type;

template<template<class...> class Variant, class List>
struct list_to_variant;

template<template<class...> class Variant, class... Ts>
struct list_to_variant<Variant, type_list<Ts...>> {
    using type = Variant<Ts...>;
};

template<template<class...> class Variant, class List>
using list_to_variant_t = typename list_to_variant<Variant, List>::type;

template<template<class...> class Tuple, template<class...> class Variant, class... Sigs>
struct value_types_impl;

template<template<class...> class Tuple, template<class...> class Variant>
struct value_types_impl<Tuple, Variant> {
    using type = Variant<Tuple<>>;
};

template<template<class...> class Tuple, template<class...> class Variant, class... Vs, class... Rest>
struct value_types_impl<Tuple, Variant, set_value_t(Vs...), Rest...> {
    using tail_list = typename value_types_impl<Tuple, type_list, Rest...>::type;
    using pushed = list_push_unique_t<tail_list, Tuple<Vs...>>;
    using type = list_to_variant_t<Variant, pushed>;
};

template<template<class...> class Tuple, class Sig, class... Rest>
struct value_types_impl<Tuple, type_list, Sig, Rest...> {
    using type = typename value_types_impl<Tuple, type_list, Rest...>::type;
};

template<template<class...> class Tuple>
struct value_types_impl<Tuple, type_list> {
    using type = type_list<Tuple<>>;
};

template<template<class...> class Tuple, class... Vs, class... Rest>
struct value_types_impl<Tuple, type_list, set_value_t(Vs...), Rest...> {
    using tail = typename value_types_impl<Tuple, type_list, Rest...>::type;
    using type = list_push_unique_t<tail, Tuple<Vs...>>;
};

template<template<class...> class Variant, class... Sigs>
struct error_types_impl;

template<template<class...> class Variant>
struct error_types_impl<Variant> {
    using type = Variant<std::monostate>;
};

template<template<class...> class Variant, class E, class... Rest>
struct error_types_impl<Variant, set_error_t(E), Rest...> {
    using tail_list = typename error_types_impl<type_list, Rest...>::type;
    using pushed = list_push_unique_t<tail_list, E>;
    using type = list_to_variant_t<Variant, pushed>;
};

template<class Sig, class... Rest>
struct error_types_impl<type_list, Sig, Rest...> {
    using type = typename error_types_impl<type_list, Rest...>::type;
};

template<>
struct error_types_impl<type_list> {
    using type = type_list<std::monostate>;
};

template<class E, class... Rest>
struct error_types_impl<type_list, set_error_t(E), Rest...> {
    using tail = typename error_types_impl<type_list, Rest...>::type;
    using type = list_push_unique_t<tail, E>;
};

template<class... Sigs>
constexpr bool has_stopped_v = (std::is_same_v<Sigs, set_stopped_t()> || ...);

template<class... CSList>
struct __concat_cs;

template<>
struct __concat_cs<> {
    using type = completion_signatures<>;
};

template<class... Sigs>
struct __concat_cs<completion_signatures<Sigs...>> {
    using type = completion_signatures<Sigs...>;
};

template<class... Sigs1, class... Sigs2, class... Rest>
struct __concat_cs<completion_signatures<Sigs1...>, completion_signatures<Sigs2...>, Rest...>
    : __concat_cs<completion_signatures<Sigs1..., Sigs2...>, Rest...> {};

template<class... CSList>
using __concat_cs_t = typename __concat_cs<CSList...>::type;

template<class List, class CS>
struct __append_cs_unique;

template<class List, class... Sigs>
struct __append_cs_unique<List, completion_signatures<Sigs...>> {
    using type = list_push_unique_all_t<List, Sigs...>;
};

template<class... CSList>
struct __concat_unique_cs;

template<>
struct __concat_unique_cs<> {
    using type = completion_signatures<>;
};

template<class... CSList>
struct __concat_unique_cs {
private:
    template<class List, class... Rest>
    struct __append_all;

    template<class List>
    struct __append_all<List> {
        using type = List;
    };

    template<class List, class CS, class... Rest>
    struct __append_all<List, CS, Rest...> {
        using next = typename __append_cs_unique<List, CS>::type;
        using type = typename __append_all<next, Rest...>::type;
    };

    using list = typename __append_all<type_list<>, CSList...>::type;

public:
    using type = list_to_variant_t<completion_signatures, list>;
};

template<class... CSList>
using __concat_unique_cs_t = typename __concat_unique_cs<CSList...>::type;

template<class Sig,
         class ValueCompletions,
         class ErrorCompletions,
         class StoppedCompletions>
struct __sig_map {
    using type = completion_signatures<>;
};

template<class... Vs,
         class ValueCompletions,
         class ErrorCompletions,
         class StoppedCompletions>
struct __sig_map<set_value_t(Vs...), ValueCompletions, ErrorCompletions, StoppedCompletions> {
    using type = ValueCompletions;
};

template<class E,
         class ValueCompletions,
         class ErrorCompletions,
         class StoppedCompletions>
struct __sig_map<set_error_t(E), ValueCompletions, ErrorCompletions, StoppedCompletions> {
    using type = ErrorCompletions;
};

template<class ValueCompletions,
         class ErrorCompletions,
         class StoppedCompletions>
struct __sig_map<set_stopped_t(), ValueCompletions, ErrorCompletions, StoppedCompletions> {
    using type = StoppedCompletions;
};

template<class CS,
         class ValueCompletions   = completion_signatures<>,
         class ErrorCompletions   = completion_signatures<set_error_t(std::exception_ptr)>,
         class StoppedCompletions = completion_signatures<set_stopped_t()>>
struct transform_completion_signatures;

template<class... Sigs,
         class ValueCompletions,
         class ErrorCompletions,
         class StoppedCompletions>
struct transform_completion_signatures<
    completion_signatures<Sigs...>,
    ValueCompletions,
    ErrorCompletions,
    StoppedCompletions>
{
    using type = __concat_cs_t<
        typename __sig_map<Sigs, ValueCompletions, ErrorCompletions, StoppedCompletions>::type...>;
};

template<class CS,
         class ValueCompletions   = completion_signatures<>,
         class ErrorCompletions   = completion_signatures<set_error_t(std::exception_ptr)>,
         class StoppedCompletions = completion_signatures<set_stopped_t()>>
using transform_completion_signatures_t =
    typename transform_completion_signatures<CS, ValueCompletions, ErrorCompletions, StoppedCompletions>::type;

template<class Sig>
struct __non_value_completion {
    using type = completion_signatures<>;
};

template<class E>
struct __non_value_completion<set_error_t(E)> {
    using type = completion_signatures<set_error_t(E)>;
};

template<>
struct __non_value_completion<set_stopped_t()> {
    using type = completion_signatures<set_stopped_t()>;
};

template<class CS>
struct __non_value_completion_signatures;

template<class... Sigs>
struct __non_value_completion_signatures<completion_signatures<Sigs...>> {
    using type = __concat_unique_cs_t<typename __non_value_completion<Sigs>::type...>;
};

template<class CS>
using __non_value_completion_signatures_t =
    typename __non_value_completion_signatures<CS>::type;

template<class CS>
struct __single_value_tuple { using type = std::tuple<>; };

template<class... Vs, class... Rest>
struct __single_value_tuple<completion_signatures<set_value_t(Vs...), Rest...>> {
    using type = std::tuple<std::decay_t<Vs>...>;
};

template<class Other, class... Rest>
struct __single_value_tuple<completion_signatures<Other, Rest...>>
    : __single_value_tuple<completion_signatures<Rest...>> {};

template<class CS>
using __single_value_tuple_t = typename __single_value_tuple<CS>::type;

template<class... Tuples>
struct __tuple_cat_type;

template<>
struct __tuple_cat_type<> { using type = std::tuple<>; };

template<class... Ts>
struct __tuple_cat_type<std::tuple<Ts...>> { using type = std::tuple<Ts...>; };

template<class... Ts, class... Us, class... Rest>
struct __tuple_cat_type<std::tuple<Ts...>, std::tuple<Us...>, Rest...>
    : __tuple_cat_type<std::tuple<Ts..., Us...>, Rest...> {};

template<class... Tuples>
using __tuple_cat_t = typename __tuple_cat_type<Tuples...>::type;

template<class... CSList>
struct __cartesian_product_value_sigs {
private:
    using combined_tuple = __tuple_cat_t<__single_value_tuple_t<CSList>...>;

    template<class T>
    struct __to_cs;

    template<class... Vs>
    struct __to_cs<std::tuple<Vs...>> {
        using type = completion_signatures<set_value_t(Vs...)>;
    };

public:
    using type = typename __to_cs<combined_tuple>::type;
};

template<class... CSList>
using __cartesian_product_value_sigs_t = typename __cartesian_product_value_sigs<CSList...>::type;

template<class CompletionSignatures, template<class...> class Tuple, template<class...> class Variant>
struct value_types_from;

template<template<class...> class Tuple, template<class...> class Variant, class... Sigs>
struct value_types_from<completion_signatures<Sigs...>, Tuple, Variant> {
    using type = typename value_types_impl<Tuple, Variant, Sigs...>::type;
};

template<class CompletionSignatures, template<class...> class Variant>
struct error_types_from;

template<template<class...> class Variant, class... Sigs>
struct error_types_from<completion_signatures<Sigs...>, Variant> {
    using type = typename error_types_impl<Variant, Sigs...>::type;
};

template<class CompletionSignatures>
struct sends_stopped_from;

template<class... Sigs>
struct sends_stopped_from<completion_signatures<Sigs...>> : std::bool_constant<has_stopped_v<Sigs...>> {};

} // namespace __forge_meta

template<class Sender, class Env, template<class...> class Tuple = std::tuple, template<class...> class Variant = std::variant>
struct value_types_of {
    using cs_t = decltype(std::execution::get_completion_signatures(std::declval<Sender>(), std::declval<Env>()));
    using type = typename __forge_meta::value_types_from<cs_t, Tuple, Variant>::type;
};

template<class Sender, class Env, template<class...> class Variant = std::variant>
struct error_types_of {
    using cs_t = decltype(std::execution::get_completion_signatures(std::declval<Sender>(), std::declval<Env>()));
    using type = typename __forge_meta::error_types_from<cs_t, Variant>::type;
};

template<class Sender, class Env>
inline constexpr bool sends_stopped_v =
    __forge_meta::sends_stopped_from<decltype(std::execution::get_completion_signatures(
        std::declval<Sender>(), std::declval<Env>()))>::value;

// ──────────────────────────────────────────────────────────────────────────
// queryable concept — [exec.queryable]
// ──────────────────────────────────────────────────────────────────────────

// A type is queryable if it is a destructible object type.
// This is the foundation for env, sender, and receiver constraints.
template<class T>
concept queryable = std::destructible<T>;

// Tag types for concept markers — [exec.snd], [exec.recv], [exec.opstate], [exec.sched]
struct receiver_t {};
struct sender_t {};
struct operation_state_t {};
struct scheduler_t {};

// ──────────────────────────────────────────────────────────────────────────
// default_domain / get_domain — [exec.domain.default], [exec.get.domain]
// Defined before connect_t so domain transform_sender participates in connect.
// ──────────────────────────────────────────────────────────────────────────

struct default_domain {
    template<class Env, class Sender>
    static Sender&& transform_sender(Sender&& sndr, const Env&) noexcept {
        return static_cast<Sender&&>(sndr);
    }

    template<class Env>
    static Env&& transform_env(const auto&, Env&& env) noexcept {
        return static_cast<Env&&>(env);
    }

    bool operator==(const default_domain&) const noexcept = default;
};

struct get_domain_t;

template<class CPO = void>
struct get_completion_domain_t {
    template<class Scheduler, class Env>
        requires __forge_detail::tag_invocable<get_completion_domain_t<CPO>, Scheduler, Env>
    auto operator()(Scheduler&& sched, Env&& env) const
        noexcept(__forge_detail::nothrow_tag_invocable<get_completion_domain_t<CPO>, Scheduler, Env>)
            -> __forge_detail::tag_invoke_result_t<get_completion_domain_t<CPO>, Scheduler, Env> {
        return __forge_detail::tag_invoke_fn(*this, static_cast<Scheduler&&>(sched), static_cast<Env&&>(env));
    }
};

template<class CPO = void>
inline constexpr get_completion_domain_t<CPO> get_completion_domain{};

namespace __forge_domain {

template<class Env>
concept __env_domain = __forge_detail::tag_invocable<get_domain_t, const Env&>;

template<class Env>
concept __scheduler_completion_domain = requires(const Env& env) {
    std::execution::get_completion_domain<set_value_t>(std::execution::get_scheduler(env), env);
};

} // namespace __forge_domain

struct get_domain_t {
    template<class Env>
        requires __forge_domain::__env_domain<Env>
    auto operator()(const Env& env) const noexcept
        -> __forge_detail::tag_invoke_result_t<get_domain_t, const Env&> {
        return __forge_detail::tag_invoke_fn(*this, env);
    }

    template<class Env>
        requires (!__forge_domain::__env_domain<Env> &&
                  __forge_domain::__scheduler_completion_domain<Env>)
    auto operator()(const Env& env) const
        noexcept(noexcept(std::execution::get_completion_domain<set_value_t>(
            std::execution::get_scheduler(env), env)))
            -> decltype(std::execution::get_completion_domain<set_value_t>(
                std::execution::get_scheduler(env), env)) {
        return std::execution::get_completion_domain<set_value_t>(
            std::execution::get_scheduler(env), env);
    }

    template<class Env>
        requires (!__forge_domain::__env_domain<Env> &&
                  !__forge_domain::__scheduler_completion_domain<Env>)
    default_domain operator()(const Env&) const noexcept {
        return {};
    }
};

inline constexpr get_domain_t get_domain{};

struct start_t {
    template<class O>
        requires (requires(O& op) { op.start(); } ||
                  __forge_detail::tag_invocable<start_t, O&>)
    void operator()(O& op) const noexcept {
        if constexpr (requires { op.start(); }) {
            static_assert(noexcept(op.start()), "start() must be noexcept");
            op.start();
        } else {
            static_assert(__forge_detail::nothrow_tag_invocable<start_t, O&>, "start() must be noexcept");
            __forge_detail::tag_invoke_fn(*this, op);
        }
    }
};
inline constexpr start_t start{};

template<class O>
concept operation_state =
    std::destructible<O> && std::is_object_v<O> &&
    !std::move_constructible<O> &&
    requires { typename O::operation_state_concept; } &&
    std::derived_from<typename O::operation_state_concept, operation_state_t> &&
    requires(O& op) { { std::execution::start(op) } noexcept; };

template<class R>
concept receiver =
    std::is_nothrow_move_constructible_v<std::remove_cvref_t<R>> &&
    !std::is_final_v<std::remove_cvref_t<R>> &&
    requires { typename std::remove_cvref_t<R>::receiver_concept; } &&
    std::derived_from<typename std::remove_cvref_t<R>::receiver_concept, receiver_t> &&
    requires(const std::remove_cvref_t<R>& r) {
        { std::execution::get_env(r) } -> queryable;
    };

namespace __forge_concepts {

template<class R, class Sig>
struct receiver_accepts : std::false_type {};

template<class R, class... Vs>
struct receiver_accepts<R, set_value_t(Vs...)>
    : std::bool_constant<
          __forge_cpo_detail::__nothrow_member_set_value<R, Vs...> ||
          __forge_detail::nothrow_tag_invocable<set_value_t, R, Vs...>> {};

template<class R, class E>
struct receiver_accepts<R, set_error_t(E)>
    : std::bool_constant<
          __forge_cpo_detail::__nothrow_member_set_error<R, E> ||
          __forge_detail::nothrow_tag_invocable<set_error_t, R, E>> {};

template<class R>
struct receiver_accepts<R, set_stopped_t()>
    : std::bool_constant<
          __forge_cpo_detail::__nothrow_member_set_stopped<R> ||
          __forge_detail::nothrow_tag_invocable<set_stopped_t, R>> {};

template<class R, class Sig>
inline constexpr bool receiver_accepts_v = receiver_accepts<R, Sig>::value;

template<class R, class Completions>
struct receiver_of_impl : std::false_type {};

template<class R, class... Sigs>
struct receiver_of_impl<R, completion_signatures<Sigs...>>
    : std::bool_constant<receiver<R> && (receiver_accepts_v<std::remove_cvref_t<R>, Sigs> && ...)> {};

} // namespace __forge_concepts

template<class R, class Completions>
concept receiver_of = __forge_concepts::receiver_of_impl<R, Completions>::value;

// enable_sender trait — [exec.snd]
// Defaults to checking the sender_concept marker.
template<class T>
inline constexpr bool enable_sender =
    requires { typename T::sender_concept; } &&
    requires { requires std::derived_from<typename T::sender_concept, sender_t>; };

template<class S>
concept sender =
    enable_sender<std::remove_cvref_t<S>> &&
    std::move_constructible<std::remove_cvref_t<S>> &&
    requires(const std::remove_cvref_t<S>& s) {
        { std::execution::get_env(s) } -> queryable;
    };

template<class S, class Env = empty_env>
concept sender_in = sender<S> && requires(std::remove_cvref_t<S>&& s, Env env) {
    std::execution::get_completion_signatures(static_cast<std::remove_cvref_t<S>&&>(s), env);
};

struct connect_t;

namespace __forge_domain {

template<class S, class R>
concept __member_connect = requires(S&& s, R&& r) {
    static_cast<S&&>(s).connect(static_cast<R&&>(r));
};

template<class S, class R>
concept __nothrow_member_connect = requires(S&& s, R&& r) {
    { static_cast<S&&>(s).connect(static_cast<R&&>(r)) } noexcept;
};

template<class S, class R>
concept __direct_connectable =
    __member_connect<S, R> || __forge_detail::tag_invocable<connect_t, S, R>;

template<class S, class R>
consteval bool __direct_noexcept() {
    if constexpr (__member_connect<S, R>) {
        return __nothrow_member_connect<S, R>;
    } else {
        return __forge_detail::nothrow_tag_invocable<connect_t, S, R>;
    }
}

template<class R>
using __receiver_env_t = decltype(
    std::execution::get_env(std::declval<const std::remove_cvref_t<R>&>()));

template<class R>
using __receiver_domain_t = decltype(
    std::execution::get_domain(std::declval<const __receiver_env_t<R>&>()));

template<class R>
concept __default_receiver_domain =
    receiver<R> &&
    std::same_as<std::remove_cvref_t<__receiver_domain_t<R>>, default_domain>;

template<class D, class TS, class Env>
consteval bool __transform_env_noexcept() {
    if constexpr (requires(D& domain, TS&& sndr, Env& env) {
                      domain.transform_env(static_cast<TS&&>(sndr), env);
                  }) {
        return noexcept(std::declval<D&>().transform_env(std::declval<TS>(), std::declval<Env&>()));
    } else {
        return true;
    }
}

template<class D, class TS, class Env>
decltype(auto) __transform_env(D& domain, TS&& sndr, Env& env)
    noexcept(__transform_env_noexcept<D, TS, Env>()) {
    if constexpr (requires { domain.transform_env(static_cast<TS&&>(sndr), env); }) {
        return domain.transform_env(static_cast<TS&&>(sndr), env);
    } else {
        return (env);
    }
}

template<class R, class Env>
struct __domain_recv {
    using receiver_concept = receiver_t;

    [[no_unique_address]] R __rcvr;
    [[no_unique_address]] Env __env;

    template<class... Vs>
    void set_value(Vs&&... vs) && noexcept {
        std::execution::set_value(std::move(__rcvr), static_cast<Vs&&>(vs)...);
    }

    template<class E>
    void set_error(E&& err) && noexcept {
        std::execution::set_error(std::move(__rcvr), static_cast<E&&>(err));
    }

    void set_stopped() && noexcept {
        std::execution::set_stopped(std::move(__rcvr));
    }

    auto get_env() const noexcept(std::is_nothrow_copy_constructible_v<Env>) -> Env {
        return __env;
    }
};

template<class D, class S, class Env>
using __transformed_sender_t = decltype(
    std::declval<D&>().transform_sender(std::declval<S>(), std::declval<Env&>()));

template<class S, class R>
using __transformed_sender_for_t =
    __transformed_sender_t<__receiver_domain_t<R>, S, __receiver_env_t<R>>;

template<class S, class R>
using __transformed_sender_lvalue_for_t =
    std::add_lvalue_reference_t<std::remove_reference_t<__transformed_sender_for_t<S, R>>>;

template<class D, class TS, class Env>
using __transformed_env_t = decltype(
    __transform_env(std::declval<D&>(), std::declval<TS>(), std::declval<Env&>()));

template<class S, class R>
using __transformed_env_for_t =
    __transformed_env_t<__receiver_domain_t<R>,
                        __transformed_sender_lvalue_for_t<S, R>,
                        __receiver_env_t<R>>;

template<class S, class R>
using __domain_receiver_for_t =
    __domain_recv<std::remove_cvref_t<R>, std::decay_t<__transformed_env_for_t<S, R>>>;

template<class S, class R>
concept __domain_connectable =
    receiver<R> &&
    (!__default_receiver_domain<R>) &&
    requires {
        typename __transformed_sender_for_t<S, R>;
        typename __transformed_env_for_t<S, R>;
        typename __domain_receiver_for_t<S, R>;
    } &&
    __direct_connectable<__transformed_sender_for_t<S, R>, __domain_receiver_for_t<S, R>>;

template<class S, class R>
concept __connectable =
    receiver<R> &&
    ((__default_receiver_domain<R> && __direct_connectable<S, R>) ||
     __domain_connectable<S, R>);

template<class S, class R>
consteval bool __connect_noexcept() {
    if constexpr (__default_receiver_domain<R>) {
        return __direct_noexcept<S, R>();
    } else {
        // Non-default domains can perform arbitrary transformations. Keep this
        // conservative rather than declaring noexcept incorrectly.
        return false;
    }
}

template<class S, class R>
decltype(auto) __direct_connect(const connect_t& tag, S&& s, R&& r)
    noexcept(__direct_noexcept<S, R>()) {
    if constexpr (__member_connect<S, R>) {
        return static_cast<S&&>(s).connect(static_cast<R&&>(r));
    } else {
        return __forge_detail::tag_invoke_fn(tag, static_cast<S&&>(s), static_cast<R&&>(r));
    }
}

template<class S, class R>
decltype(auto) __domain_connect(const connect_t& tag, S&& s, R&& r) {
    auto env = std::execution::get_env(r);
    auto domain = std::execution::get_domain(env);
    auto&& ts = domain.transform_sender(static_cast<S&&>(s), env);
    auto transformed_env = __transform_env(domain, ts, env);
    using wrapped_t = __domain_recv<std::remove_cvref_t<R>, decltype(transformed_env)>;
    return __direct_connect(
        tag,
        static_cast<decltype(ts)>(ts),
        wrapped_t{static_cast<R&&>(r), std::move(transformed_env)});
}

} // namespace __forge_domain

struct connect_t {
    template<class S, class R>
        requires __forge_domain::__connectable<S, R>
    auto operator()(S&& s, R&& r) const
        noexcept(__forge_domain::__connect_noexcept<S, R>()) {
        if constexpr (__forge_domain::__default_receiver_domain<R>) {
            return __forge_domain::__direct_connect(*this, static_cast<S&&>(s), static_cast<R&&>(r));
        } else {
            return __forge_domain::__domain_connect(*this, static_cast<S&&>(s), static_cast<R&&>(r));
        }
    }
};
inline constexpr connect_t connect{};

template<class S, class R>
using connect_result_t = decltype(connect_t{}(std::declval<S>(), std::declval<R>()));

template<class S, class R>
concept sender_to =
    sender<S> && receiver<R> && requires(std::remove_cvref_t<S>&& s, std::remove_cvref_t<R>&& r) {
        std::execution::connect(static_cast<std::remove_cvref_t<S>&&>(s), static_cast<std::remove_cvref_t<R>&&>(r));
    };

struct schedule_t;

namespace __forge_cpo_detail {

template<class S>
concept __member_schedule = requires(S&& s) {
    static_cast<S&&>(s).schedule();
};

template<class S>
concept __nothrow_member_schedule = requires(S&& s) {
    { static_cast<S&&>(s).schedule() } noexcept;
};

template<class S>
consteval bool __schedule_noexcept() {
    if constexpr (__member_schedule<S>) {
        return __nothrow_member_schedule<S>;
    } else {
        return __forge_detail::nothrow_tag_invocable<schedule_t, S>;
    }
}

} // namespace __forge_cpo_detail

struct schedule_t {
    template<class S>
        requires (__forge_cpo_detail::__member_schedule<S> ||
                  __forge_detail::tag_invocable<schedule_t, S>)
    auto operator()(S&& s) const
        noexcept(__forge_cpo_detail::__schedule_noexcept<S>()) {
        if constexpr (__forge_cpo_detail::__member_schedule<S>) {
            return static_cast<S&&>(s).schedule();
        } else {
            return __forge_detail::tag_invoke_fn(*this, static_cast<S&&>(s));
        }
    }
};
inline constexpr schedule_t schedule{};

template<class S>
concept scheduler =
    std::copy_constructible<std::remove_cvref_t<S>> &&
    std::equality_comparable<std::remove_cvref_t<S>> &&
    queryable<std::remove_cvref_t<S>> &&
    requires { typename std::remove_cvref_t<S>::scheduler_concept; } &&
    std::derived_from<typename std::remove_cvref_t<S>::scheduler_concept, scheduler_t> &&
    requires(std::remove_cvref_t<S>& s) { { std::execution::schedule(s) } -> sender; };

} // namespace std::execution

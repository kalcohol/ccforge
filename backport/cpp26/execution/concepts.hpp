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
#include "detail/awaitable_helpers.hpp"

#include <variant>

namespace std::execution {

// Completion functions enforce the working draft's void and nothrow Mandates.
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
        requires (!std::is_lvalue_reference_v<R> &&
                  !std::is_const_v<std::remove_reference_t<R>>) &&
                 __forge_cpo_detail::__member_set_value<R, Vs...>
    void operator()(R&& r, Vs&&... vs) const noexcept {
        static_assert(__forge_cpo_detail::__nothrow_member_set_value<R, Vs...>,
                      "set_value(receiver, ...) must be noexcept and return void");
        static_cast<R&&>(r).set_value(static_cast<Vs&&>(vs)...);
    }
};

struct set_error_t {
    template<class R, class E>
        requires (!std::is_lvalue_reference_v<R> &&
                  !std::is_const_v<std::remove_reference_t<R>>) &&
                 __forge_cpo_detail::__member_set_error<R, E>
    void operator()(R&& r, E&& e) const noexcept {
        static_assert(__forge_cpo_detail::__nothrow_member_set_error<R, E>,
                      "set_error(receiver, error) must be noexcept and return void");
        static_cast<R&&>(r).set_error(static_cast<E&&>(e));
    }
};

struct set_stopped_t {
    template<class R>
        requires (!std::is_lvalue_reference_v<R> &&
                  !std::is_const_v<std::remove_reference_t<R>>) &&
                 __forge_cpo_detail::__member_set_stopped<R>
    void operator()(R&& r) const noexcept {
        static_assert(__forge_cpo_detail::__nothrow_member_set_stopped<R>,
                      "set_stopped(receiver) must be noexcept and return void");
        static_cast<R&&>(r).set_stopped();
    }
};

inline constexpr set_value_t set_value{};
inline constexpr set_error_t set_error{};
inline constexpr set_stopped_t set_stopped{};

namespace __forge_cpo_detail {
template<class Sig>
inline constexpr bool __completion_signature = false;
template<class... Vs>
inline constexpr bool __completion_signature<set_value_t(Vs...)> =
    ((std::is_object_v<Vs> || std::is_reference_v<Vs>) && ...);
template<class E>
inline constexpr bool __completion_signature<set_error_t(E)> =
    std::is_object_v<E> || std::is_reference_v<E>;
template<>
inline constexpr bool __completion_signature<set_stopped_t()> = true;
} // namespace __forge_cpo_detail

template<class... Sigs>
    requires (__forge_cpo_detail::__completion_signature<Sigs> && ...)
struct completion_signatures {};

namespace __forge_cpo_detail {
template<class T>
inline constexpr bool __valid_completion_signatures = false;
template<class... Sigs>
inline constexpr bool __valid_completion_signatures<completion_signatures<Sigs...>> = true;
} // namespace __forge_cpo_detail

struct get_completion_signatures_t;

namespace __forge_domain {

template<class S, class Env>
constexpr decltype(auto) __transform_sender_for_completion_signatures(
    S&& sndr,
    const Env& env);

template<class S, class Env>
consteval bool __completion_signatures_use_default_transform();

} // namespace __forge_domain

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
consteval bool __valid_member_completion_signatures() {
    if constexpr (__static_completion_signatures<S, Env>) {
        return __valid_completion_signatures<decltype(std::remove_cvref_t<S>
            ::template get_completion_signatures<S, std::remove_cvref_t<Env>>())>;
    } else if constexpr (__instance_completion_signatures<S, Env>) {
        return __valid_completion_signatures<decltype(std::declval<S>()
            .get_completion_signatures(std::declval<Env>()))>;
    } else {
        return false;
    }
}

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
template<class V>
struct __await_value_signature { using type = set_value_t(V); };
template<> struct __await_value_signature<void> { using type = set_value_t(); };
template<> struct __await_value_signature<const void> : __await_value_signature<void> {};
template<> struct __await_value_signature<volatile void> : __await_value_signature<void> {};
template<> struct __await_value_signature<const volatile void> : __await_value_signature<void> {};

template<class S, class... Env>
    requires __forge_awaitable::__is_awaitable<S, __forge_awaitable::__env_promise<Env>...>
using __await_completion_signatures = completion_signatures<
    typename __await_value_signature<__forge_awaitable::__await_result_t<
        S, __forge_awaitable::__env_promise<Env>...>>::type,
    set_error_t(std::exception_ptr), set_stopped_t()>;
#endif

template<class S, class... Env>
concept __await_signatures_available =
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
    __forge_awaitable::__is_awaitable<S, __forge_awaitable::__env_promise<Env>...>;
#else
    false;
#endif

template<class S, class Env>
concept __member_signatures_selected = __static_completion_signatures<S, Env> ||
    __instance_completion_signatures<S, Env>;

template<class S, class Env>
concept __raw_completion_signatures_available =
    __valid_member_completion_signatures<S, Env>() ||
    (!__member_signatures_selected<S, Env> && __await_signatures_available<S, Env>);

template<class S, class Env>
consteval bool __completion_signatures_noexcept() {
    if constexpr (__static_completion_signatures<S, Env>) {
        return __nothrow_static_completion_signatures<S, Env>;
    } else if constexpr (__instance_completion_signatures<S, Env>) {
        return __nothrow_instance_completion_signatures<S, Env>;
    } else {
        return true;
    }
}

template<class S, class Env>
    requires __raw_completion_signatures_available<S, Env>
decltype(auto) __raw_completion_signatures(S&& s, Env&& env)
    noexcept(__completion_signatures_noexcept<S, Env>());

template<class S, class Env>
using __completion_transformed_sender_t = decltype(
    __forge_domain::__transform_sender_for_completion_signatures(
        std::declval<S>(),
        std::declval<std::remove_reference_t<Env>&>()));

template<class S, class Env, class = void>
struct __has_transformed_completion_signatures : std::false_type {};

template<class S, class Env>
struct __has_transformed_completion_signatures<
    S,
    Env,
    std::void_t<__completion_transformed_sender_t<S, Env>>>
    : std::bool_constant<
          __raw_completion_signatures_available<
              __completion_transformed_sender_t<S, Env>,
              Env>> {};

template<class S, class Env>
concept __transformed_completion_signatures_available =
    __has_transformed_completion_signatures<S, Env>::value;

template<class S, class Env>
decltype(auto) __transformed_completion_signatures(S&& s, Env&& env);

} // namespace __forge_cpo_detail

struct get_completion_signatures_t {
    template<class S, class Env>
        requires (
            (__forge_domain::__completion_signatures_use_default_transform<S, Env>() &&
             __forge_cpo_detail::__raw_completion_signatures_available<S, Env>) ||
            (!__forge_domain::__completion_signatures_use_default_transform<S, Env>() &&
             __forge_cpo_detail::__transformed_completion_signatures_available<S, Env>))
    auto operator()(S&& s, Env&& env) const
        noexcept(__forge_domain::__completion_signatures_use_default_transform<S, Env>() &&
                 __forge_cpo_detail::__completion_signatures_noexcept<S, Env>()) {
        if constexpr (__forge_domain::__completion_signatures_use_default_transform<S, Env>()) {
            return __forge_cpo_detail::__raw_completion_signatures(
                static_cast<S&&>(s), static_cast<Env&&>(env));
        } else {
            return __forge_cpo_detail::__transformed_completion_signatures(
                static_cast<S&&>(s), static_cast<Env&&>(env));
        }
    }

    template<class S>
        requires (__forge_cpo_detail::__valid_member_completion_signatures<S, __empty_env>() ||
                  (!__forge_cpo_detail::__member_signatures_selected<S, __empty_env> &&
                   __forge_cpo_detail::__await_signatures_available<S>))
    auto operator()(S&& s) const
        noexcept(__forge_cpo_detail::__completion_signatures_noexcept<S, __empty_env>()) {
        if constexpr (__forge_cpo_detail::__valid_member_completion_signatures<S, __empty_env>()) {
            return __forge_cpo_detail::__raw_completion_signatures(
                static_cast<S&&>(s), __empty_env{});
        } else {
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
            return __forge_cpo_detail::__await_completion_signatures<S>{};
#endif
        }
    }
};
inline constexpr get_completion_signatures_t get_completion_signatures{};

namespace __forge_cpo_detail {

template<class S, class Env>
    requires __raw_completion_signatures_available<S, Env>
decltype(auto) __raw_completion_signatures(S&& s, Env&& env)
    noexcept(__completion_signatures_noexcept<S, Env>()) {
    if constexpr (__static_completion_signatures<S, Env>) {
        return std::remove_cvref_t<S>::template get_completion_signatures<S, std::remove_cvref_t<Env>>();
    } else if constexpr (__instance_completion_signatures<S, Env>) {
        return static_cast<S&&>(s).get_completion_signatures(static_cast<Env&&>(env));
    } else {
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
        return __await_completion_signatures<S, Env>{};
#endif
    }
}

template<class S, class Env>
decltype(auto) __transformed_completion_signatures(S&& s, Env&& env) {
    auto& env_ref = env;
    decltype(auto) transformed = __forge_domain::__transform_sender_for_completion_signatures(
        static_cast<S&&>(s), env_ref);
    return __raw_completion_signatures(
        static_cast<decltype(transformed)&&>(transformed),
        static_cast<Env&&>(env));
}

} // namespace __forge_cpo_detail

namespace __forge_meta {

template<class... Ts>
struct type_list {};

template<class... Ts>
using __decayed_tuple = std::tuple<std::decay_t<Ts>...>;

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

struct __empty_variant {
    __empty_variant() = delete;
};

template<class... Ts>
struct __variant_or_empty_impl {
    using unique_types =
        list_push_unique_all_t<type_list<>, std::decay_t<Ts>...>;
    using type = list_to_variant_t<std::variant, unique_types>;
};

template<>
struct __variant_or_empty_impl<> {
    using type = __empty_variant;
};

template<class... Ts>
using __variant_or_empty = typename __variant_or_empty_impl<Ts...>::type;

template<
    template<class...> class Tuple,
    template<class...> class Variant,
    class Accumulated,
    class... Sigs>
struct value_types_impl;

template<
    template<class...> class Tuple,
    template<class...> class Variant,
    class... Values>
struct value_types_impl<Tuple, Variant, type_list<Values...>> {
    using type = Variant<Values...>;
};

template<
    template<class...> class Tuple,
    template<class...> class Variant,
    class... Values,
    class... Vs,
    class... Rest>
struct value_types_impl<
    Tuple,
    Variant,
    type_list<Values...>,
    set_value_t(Vs...),
    Rest...>
    : value_types_impl<
          Tuple,
          Variant,
          type_list<Values..., Tuple<Vs...>>,
          Rest...> {
};

template<
    template<class...> class Tuple,
    template<class...> class Variant,
    class... Values,
    class Sig,
    class... Rest>
struct value_types_impl<
    Tuple,
    Variant,
    type_list<Values...>,
    Sig,
    Rest...>
    : value_types_impl<Tuple, Variant, type_list<Values...>, Rest...> {
};

template<
    template<class...> class Variant,
    class Accumulated,
    class... Sigs>
struct error_types_impl;

template<template<class...> class Variant, class... Errors>
struct error_types_impl<Variant, type_list<Errors...>> {
    using type = Variant<Errors...>;
};

template<
    template<class...> class Variant,
    class... Errors,
    class Error,
    class... Rest>
struct error_types_impl<
    Variant,
    type_list<Errors...>,
    set_error_t(Error),
    Rest...>
    : error_types_impl<Variant, type_list<Errors..., Error>, Rest...> {
};

template<
    template<class...> class Variant,
    class... Errors,
    class Sig,
    class... Rest>
struct error_types_impl<
    Variant,
    type_list<Errors...>,
    Sig,
    Rest...>
    : error_types_impl<Variant, type_list<Errors...>, Rest...> {
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
    using type =
        typename value_types_impl<Tuple, Variant, type_list<>, Sigs...>::type;
};

template<class CompletionSignatures, template<class...> class Variant>
struct error_types_from;

template<template<class...> class Variant, class... Sigs>
struct error_types_from<completion_signatures<Sigs...>, Variant> {
    using type =
        typename error_types_impl<Variant, type_list<>, Sigs...>::type;
};

template<class CompletionSignatures>
struct sends_stopped_from;

template<class... Sigs>
struct sends_stopped_from<completion_signatures<Sigs...>> : std::bool_constant<has_stopped_v<Sigs...>> {};

} // namespace __forge_meta

// Tag types for concept markers — [exec.snd], [exec.recv], [exec.opstate], [exec.sched]
struct receiver_tag {};
struct sender_tag {};
struct operation_state_tag {};
struct scheduler_tag {};

using receiver_t = receiver_tag;
using sender_t = sender_tag;
using operation_state_t = operation_state_tag;
using scheduler_t = scheduler_tag;

// enable_sender trait — [exec.snd]
// The private concept short-circuits before probing a marked sender as awaitable.
namespace __forge_concepts {
template<class S>
concept __marked_sender = requires { typename S::sender_concept; } &&
    std::derived_from<typename S::sender_concept, sender_tag>;
template<class S>
concept __enable_sender = __marked_sender<S>
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
    || __forge_awaitable::__is_awaitable<S, __forge_awaitable::__env_promise<env<>>>
#endif
    ;
} // namespace __forge_concepts

template<class S>
inline constexpr bool enable_sender = __forge_concepts::__enable_sender<S>;

template<class S>
concept sender =
    enable_sender<std::remove_cvref_t<S>> &&
    std::move_constructible<std::remove_cvref_t<S>> &&
    std::constructible_from<std::remove_cvref_t<S>, S> &&
    requires(const std::remove_cvref_t<S>& s) {
        { std::execution::get_env(s) } -> __forge_queryable;
    };

// ──────────────────────────────────────────────────────────────────────────
// default_domain / get_domain — [exec.domain.default], [exec.get.domain]
// Defined before connect_t so domain transform_sender participates in connect.
// ──────────────────────────────────────────────────────────────────────────

namespace __forge_domain {

template<class S>
using __tag_of_t = std::remove_cvref_t<
    decltype(std::declval<S>().template get<0>())>;

template<class Tag, class Sender, class Env>
concept __sender_tag_transformable = requires(Sender&& sndr, const Env& env) {
    __tag_of_t<Sender>{}.transform_sender(
        Tag{}, static_cast<Sender&&>(sndr), env);
};

template<class Tag, class Sender, class... Args>
concept __tag_apply_sender = requires(Sender&& sndr, Args&&... args) {
    Tag{}.apply_sender(
        static_cast<Sender&&>(sndr), static_cast<Args&&>(args)...);
};

// Internal dispatch also serves connect's borrowed, non-movable source extension.
template<class Tag, class Sender, class Env>
    requires __sender_tag_transformable<Tag, Sender, Env>
constexpr decltype(auto) __default_transform_sender(Sender&& sndr, const Env& env)
    noexcept(noexcept(__tag_of_t<Sender>{}.transform_sender(
        Tag{}, static_cast<Sender&&>(sndr), env))) {
    return __tag_of_t<Sender>{}.transform_sender(
        Tag{}, static_cast<Sender&&>(sndr), env);
}

template<class Tag, class Sender, class Env>
    requires (!__sender_tag_transformable<Tag, Sender, Env>)
constexpr decltype(auto) __default_transform_sender(Sender&& sndr, const Env&)
    noexcept(noexcept(static_cast<Sender>(static_cast<Sender&&>(sndr)))) {
    return static_cast<Sender>(static_cast<Sender&&>(sndr));
}

} // namespace __forge_domain

struct default_domain {
    template<class Tag, sender Sender, __forge_queryable Env>
    static constexpr decltype(auto) transform_sender(Tag, Sender&& sndr, const Env& env)
        noexcept(noexcept(__forge_domain::__default_transform_sender<Tag>(
            static_cast<Sender&&>(sndr), env))) {
        return __forge_domain::__default_transform_sender<Tag>(
            static_cast<Sender&&>(sndr), env);
    }

    template<class Tag, sender Sender, class... Args>
        requires __forge_domain::__tag_apply_sender<Tag, Sender, Args...>
    static constexpr decltype(auto) apply_sender(
        Tag,
        Sender&& sndr,
        Args&&... args)
        noexcept(noexcept(Tag{}.apply_sender(
            static_cast<Sender&&>(sndr), static_cast<Args&&>(args)...))) {
        return Tag{}.apply_sender(
            static_cast<Sender&&>(sndr), static_cast<Args&&>(args)...);
    }
};

template<class... Domains>
struct indeterminate_domain {
    indeterminate_domain() = default;
    constexpr indeterminate_domain(auto&&) noexcept {}

private:
    template<class D, class Tag, class S, class Env>
    static consteval bool __compatible() {
        if constexpr (requires { D().transform_sender(Tag(), std::declval<S>(), std::declval<const Env&>()); }) {
            return std::same_as<std::decay_t<decltype(D().transform_sender(
                Tag(), std::declval<S>(), std::declval<const Env&>()))>,
                std::decay_t<decltype(default_domain().transform_sender(
                    Tag(), std::declval<S>(), std::declval<const Env&>()))>>;
        } else {
            return true;
        }
    }

public:
    template<class Tag, sender S, __forge_queryable Env>
    static constexpr decltype(auto) transform_sender(Tag tag, S&& sndr, const Env& env)
        noexcept(noexcept(default_domain().transform_sender(tag, static_cast<S&&>(sndr), env))) {
        static_assert((__compatible<Domains, Tag, S, Env>() && ...),
                      "indeterminate domain transformations must agree with default_domain");
        return default_domain().transform_sender(tag, static_cast<S&&>(sndr), env);
    }
};

struct get_domain_t;

namespace __forge_domain {

template<class Query, class Env, class... Args>
struct __queried_domain;

template<class Query, class Env, class... Args>
    requires __forge_env_detail::__member_query<Query, Env, Args...>
struct __queried_domain<Query, Env, Args...> {
    using type = std::remove_cvref_t<decltype(
        std::declval<const std::remove_reference_t<Env>&>().query(
            std::declval<const Query&>(), std::declval<Args>()...))>;
};

template<class Query, class Env, class... Args>
using __queried_domain_t =
    typename __queried_domain<Query, Env, Args...>::type;

template<class Query, class Env, class... Args>
concept __nothrow_default_domain_query =
    __forge_env_detail::__queryable<Query, Env, Args...> &&
    requires { typename __queried_domain<Query, Env, Args...>::type; } &&
    std::is_nothrow_default_constructible_v<
        __queried_domain_t<Query, Env, Args...>>;

} // namespace __forge_domain

template<class CPO = void>
struct get_completion_domain_t;

namespace __forge_domain {

struct __no_completion_query {};

// Adaptors can match scheduler-domain fallback arguments to the environment
// used by their connected receivers. Ordinary attributes keep their arguments.
template<class Attrs, class Env>
struct __completion_domain_env : std::type_identity<Env> {};

template<class Query, class Attrs, class... Envs>
consteval auto __completion_query_type() {
    // Domain selection uses only query types, never the queried values.
    if constexpr (__forge_env_detail::__queryable<Query, Attrs, Envs...>) {
        return std::type_identity<__queried_domain_t<Query, Attrs, Envs...>>{};
    } else if constexpr (sizeof...(Envs) != 0 &&
                         __forge_env_detail::__queryable<Query, Attrs>) {
        return std::type_identity<__queried_domain_t<Query, Attrs>>{};
    } else {
        return std::type_identity<__no_completion_query>{};
    }
}

template<class Attrs>
consteval bool __scheduler_attributes();

} // namespace __forge_domain

template<class CPO>
struct get_completion_domain_t {
private:
    template<class>
    friend struct get_completion_domain_t;

    template<class Attrs, class... Envs>
    static consteval auto __domain_type() {
        using namespace __forge_domain;
        if constexpr (!__forge_env_detail::__completion_query_allowed<
                          std::remove_cvref_t<Attrs>, CPO, Envs...>::value) {
            return std::type_identity<void>{};
        } else {
            using direct_t = typename decltype(__completion_query_type<
                get_completion_domain_t, Attrs, Envs...>())::type;
            if constexpr (!std::same_as<direct_t, __no_completion_query>) {
                return std::type_identity<direct_t>{};
            } else if constexpr (std::same_as<CPO, void>) {
                return get_completion_domain_t<set_value_t>
                    ::template __domain_type<Attrs, Envs...>();
            } else {
                using scheduler_t = typename decltype([] {
                    if constexpr (requires {
                        std::execution::get_completion_scheduler<CPO>(
                            std::declval<Attrs>(), std::declval<Envs>()...);
                    }) {
                        return std::type_identity<decltype(std::execution::get_completion_scheduler<CPO>(
                            std::declval<Attrs>(), std::declval<Envs>()...))>{};
                    } else {
                        return std::type_identity<__no_completion_query>{};
                    }
                }())::type;
                using scheduler_domain_t = typename decltype(__completion_query_type<
                    get_completion_domain_t<set_value_t>, scheduler_t,
                    typename __completion_domain_env<
                        std::remove_cvref_t<Attrs>, Envs>::type...>())::type;
                if constexpr (!std::same_as<scheduler_t, __no_completion_query> &&
                              !std::same_as<scheduler_domain_t, __no_completion_query>) {
                    return std::type_identity<scheduler_domain_t>{};
                } else if constexpr (sizeof...(Envs) != 0 &&
                                     __scheduler_attributes<Attrs>()) {
                    return std::type_identity<default_domain>{};
                } else {
                    return std::type_identity<void>{};
                }
            }
        }
    }

public:
    template<class Attrs, class... Envs>
        requires (std::same_as<CPO, void> || std::same_as<CPO, set_value_t> ||
                  std::same_as<CPO, set_error_t> || std::same_as<CPO, set_stopped_t>) &&
                 std::is_default_constructible_v<
                     typename decltype(__domain_type<Attrs, Envs...>())::type>
    constexpr auto operator()(Attrs&&, Envs&&...) const noexcept
        -> typename decltype(__domain_type<Attrs, Envs...>())::type {
        static_assert(std::is_nothrow_default_constructible_v<
            typename decltype(__domain_type<Attrs, Envs...>())::type>);
        return {};
    }

    constexpr bool query(std::forwarding_query_t) const noexcept {
        return true;
    }
};

template<class CPO = void>
inline constexpr get_completion_domain_t<CPO> get_completion_domain{};

namespace __forge_domain {

template<class Env>
concept __env_domain =
    __forge_env_detail::__queryable<get_domain_t, const Env&>;

template<class Env>
concept __valid_env_domain =
    __nothrow_default_domain_query<get_domain_t, const Env&>;

template<class Env>
using __hide_scheduler = __forge_env_detail::__hide_scheduler<Env>;

template<class Env>
concept __scheduler_completion_domain = requires(const Env& env) {
    std::execution::get_completion_domain<set_value_t>(std::execution::get_scheduler(env), __hide_scheduler<Env>{std::addressof(env)});
};

} // namespace __forge_domain

struct get_domain_t {
    template<class Env>
        requires __forge_domain::__env_domain<Env> &&
                 std::is_default_constructible_v<
                     __forge_domain::__queried_domain_t<get_domain_t, const Env&>>
    constexpr auto operator()(const Env&) const noexcept
        -> __forge_domain::__queried_domain_t<get_domain_t, const Env&> {
        static_assert(__forge_domain::__valid_env_domain<Env>);
        return {};
    }

    template<class Env>
        requires (!__forge_domain::__env_domain<Env> &&
                  __forge_domain::__scheduler_completion_domain<Env>)
    constexpr auto operator()(const Env&) const noexcept
            -> decltype(std::execution::get_completion_domain<set_value_t>(
                std::execution::get_scheduler(std::declval<const Env&>()),
                std::declval<__forge_domain::__hide_scheduler<Env>>())) {
        using domain_t = std::remove_cvref_t<decltype(
            std::execution::get_completion_domain<set_value_t>(
                std::execution::get_scheduler(std::declval<const Env&>()),
                std::declval<__forge_domain::__hide_scheduler<Env>>()))>;
        static_assert(std::is_nothrow_default_constructible_v<domain_t>,
                      "domain construction must be noexcept");
        return domain_t{};
    }

    template<class Env>
        requires (!__forge_domain::__env_domain<Env> &&
                  !__forge_domain::__scheduler_completion_domain<Env>)
    constexpr default_domain operator()(const Env&) const noexcept {
        return {};
    }

    constexpr bool query(std::forwarding_query_t) const noexcept {
        return true;
    }
};

inline constexpr get_domain_t get_domain{};

struct start_t {
    template<class O>
        requires std::is_lvalue_reference_v<O> &&
                 requires(O&& op) { op.start(); }
    void operator()(O&& op) const noexcept {
        static_assert(noexcept(op.start()), "start() must be noexcept");
        static_assert(std::same_as<decltype(op.start()), void>, "start() must return void");
        op.start();
    }
};
inline constexpr start_t start{};

template<class O>
concept operation_state =
    requires { typename O::operation_state_concept; } &&
    std::derived_from<typename O::operation_state_concept, operation_state_tag> &&
    requires(O& op) { { std::execution::start(op) } noexcept; };

template<class R>
concept receiver =
    requires { typename std::remove_cvref_t<R>::receiver_concept; } &&
    std::derived_from<typename std::remove_cvref_t<R>::receiver_concept, receiver_tag> &&
    requires(const std::remove_cvref_t<R>& r) {
        { std::execution::get_env(r) } -> __forge_queryable;
    } &&
    std::move_constructible<std::remove_cvref_t<R>> &&
    std::constructible_from<std::remove_cvref_t<R>, R> &&
    std::is_nothrow_move_constructible_v<std::remove_cvref_t<R>>;

template<class R, class ChildOp>
concept inlinable_receiver = receiver<R> && requires(ChildOp* child) {
    { std::remove_cvref_t<R>::make_receiver_for(child) } noexcept
        -> std::same_as<std::remove_cvref_t<R>>;
};

namespace __forge_concepts {

template<class R, class Sig>
struct receiver_accepts : std::false_type {};

template<class R, class... Vs>
struct receiver_accepts<R, set_value_t(Vs...)>
    : std::bool_constant<
          __forge_cpo_detail::__nothrow_member_set_value<R, Vs...>> {};

template<class R, class E>
struct receiver_accepts<R, set_error_t(E)>
    : std::bool_constant<
          __forge_cpo_detail::__nothrow_member_set_error<R, E>> {};

template<class R>
struct receiver_accepts<R, set_stopped_t()>
    : std::bool_constant<
          __forge_cpo_detail::__nothrow_member_set_stopped<R>> {};

template<class R, class Sig>
inline constexpr bool receiver_accepts_v = receiver_accepts<R, Sig>::value;

template<class R, class Completions>
struct receiver_of_impl : std::false_type {};

template<class R, class... Sigs>
struct receiver_of_impl<R, completion_signatures<Sigs...>>
    : std::bool_constant<receiver<R> && (receiver_accepts_v<std::remove_cvref_t<R>, Sigs> && ...)> {};

} // namespace __forge_concepts

template<class R, class Completions>
concept __forge_receiver_of = __forge_concepts::receiver_of_impl<R, Completions>::value;

template<sender Sndr>
using tag_of_t = __forge_domain::__tag_of_t<Sndr>;

template<class S, class... Env>
concept sender_in =
    sender<S> &&
    (sizeof...(Env) <= 1) &&
    (__forge_queryable<Env> && ...) &&
    requires {
        std::execution::get_completion_signatures(std::declval<S>(), std::declval<Env>()...);
    };

template<class S, class... Env>
    requires sender_in<S, Env...>
using completion_signatures_of_t = decltype(
    std::execution::get_completion_signatures(
        std::declval<S>(),
        std::declval<Env>()...));

template<class S,
         class Env = env<>,
         template<class...> class Tuple = __forge_meta::__decayed_tuple,
         template<class...> class Variant = __forge_meta::__variant_or_empty>
    requires sender_in<S, Env>
using value_types_of_t = typename __forge_meta::value_types_from<
    completion_signatures_of_t<S, Env>, Tuple, Variant>::type;

template<class S,
         class Env = env<>,
         template<class...> class Variant = __forge_meta::__variant_or_empty>
    requires sender_in<S, Env>
using error_types_of_t = typename __forge_meta::error_types_from<
    completion_signatures_of_t<S, Env>, Variant>::type;

template<class S, class Env = env<>>
    requires sender_in<S, Env>
inline constexpr bool sends_stopped =
    __forge_meta::sends_stopped_from<completion_signatures_of_t<S, Env>>::value;

} // namespace std::execution
#include "detail/connect_awaitable.hpp"
namespace std::execution {

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
    __member_connect<S, R>
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
    || __forge_awaitable::__connect_awaitable_available<S, R>
#endif
    ;

template<class S, class R>
consteval bool __direct_noexcept() {
    if constexpr (__member_connect<S, R>) {
        return __nothrow_member_connect<S, R>;
    } else {
        return false; // A coroutine frame uses the global allocator.
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

template<class D, class Tag, class S, class Env>
concept __tagged_transform_sender = requires(D& domain, Tag tag, S&& sndr, const Env& env) {
    { domain.transform_sender(tag, static_cast<S&&>(sndr), env) };
};

template<class D, class Tag, class S, class Env>
consteval bool __transform_sender_once_noexcept() {
    if constexpr (__tagged_transform_sender<D, Tag, S, Env>) {
        return noexcept(std::declval<D&>().transform_sender(
            Tag{}, std::declval<S>(), std::declval<const Env&>()));
    } else {
        return noexcept(__default_transform_sender<Tag>(
            std::declval<S>(), std::declval<const Env&>()));
    }
}

template<class D, class Tag, class S, class Env>
constexpr decltype(auto) __transform_sender_once(
    D& domain,
    Tag tag,
    S&& sndr,
    const Env& env)
    noexcept(__transform_sender_once_noexcept<D, Tag, S, Env>()) {
    if constexpr (__tagged_transform_sender<D, Tag, S, Env>) {
        return domain.transform_sender(tag, static_cast<S&&>(sndr), env);
    } else {
        return __default_transform_sender<Tag>(static_cast<S&&>(sndr), env);
    }
}

template<class S, class Env>
consteval bool __completion_domain_noexcept() {
    if constexpr (requires {
                      std::execution::get_completion_domain<>(
                          std::execution::get_env(std::declval<S&>()),
                          std::declval<const Env&>());
                  }) {
        using domain_t = std::remove_cvref_t<decltype(
            std::execution::get_completion_domain<>(
                std::execution::get_env(std::declval<S&>()),
                std::declval<const Env&>()))>;
        return std::is_nothrow_default_constructible_v<domain_t>;
    } else {
        return true;
    }
}

template<class S, class Env>
constexpr auto __completion_domain_for(S&&, const Env&)
    noexcept(__completion_domain_noexcept<S, Env>()) {
    if constexpr (requires {
                      std::execution::get_completion_domain<>(
                          std::execution::get_env(std::declval<S&>()),
                          std::declval<const Env&>());
                  }) {
        using domain_t = std::remove_cvref_t<decltype(
            std::execution::get_completion_domain<>(
                std::execution::get_env(std::declval<S&>()),
                std::declval<const Env&>()))>;
        static_assert(std::is_nothrow_default_constructible_v<domain_t>,
                      "domain construction must be noexcept");
        return domain_t{};
    } else {
        return default_domain{};
    }
}

template<class Env>
consteval bool __start_domain_noexcept() {
    if constexpr (requires {
                      std::execution::get_domain(std::declval<const Env&>());
                  }) {
        using domain_t = std::remove_cvref_t<decltype(
            std::execution::get_domain(std::declval<const Env&>()))>;
        return std::is_nothrow_default_constructible_v<domain_t>;
    } else {
        return true;
    }
}

template<class Env>
constexpr auto __start_domain_for(const Env&)
    noexcept(__start_domain_noexcept<Env>()) {
    if constexpr (requires {
                      std::execution::get_domain(std::declval<const Env&>());
                  }) {
        using domain_t = std::remove_cvref_t<decltype(
            std::execution::get_domain(std::declval<const Env&>()))>;
        static_assert(std::is_nothrow_default_constructible_v<domain_t>,
                      "domain construction must be noexcept");
        return domain_t{};
    } else {
        return default_domain{};
    }
}

template<class D, class Tag, class S, class Env>
using __transform_once_result_t = decltype(__transform_sender_once(
    std::declval<D&>(),
    std::declval<Tag>(),
    std::declval<S>(),
    std::declval<const Env&>()));

template<class T>
using __forwarded_argument_t = std::conditional_t<
    std::is_lvalue_reference_v<T>, T, std::remove_reference_t<T>>;

template<class S, class Result>
using __stable_transform_result_t = std::conditional_t<
    std::is_lvalue_reference_v<S> && std::is_lvalue_reference_v<Result>,
    Result,
    std::remove_cvref_t<Result>>;

template<class D, class Tag, class S, class Env>
consteval bool __transform_recurse_noexcept() {
    using result_t = __transform_once_result_t<D, Tag, S, Env>;
    constexpr bool once_noexcept =
        __transform_sender_once_noexcept<D, Tag, S, Env>();

    if constexpr (std::same_as<std::remove_cvref_t<result_t>,
                               std::remove_cvref_t<S>>) {
        using stable_t = __stable_transform_result_t<S, result_t>;
        if constexpr (std::is_reference_v<stable_t>) {
            return once_noexcept;
        } else {
            return once_noexcept &&
                   std::is_nothrow_constructible_v<stable_t, result_t&&>;
        }
    } else if constexpr (std::same_as<std::remove_cvref_t<Tag>, start_t>) {
        using next_domain_t = decltype(__start_domain_for(
            std::declval<const Env&>()));
        using next_sender_t = __forwarded_argument_t<result_t>;
        return once_noexcept && __start_domain_noexcept<Env>() &&
               __transform_recurse_noexcept<
                   next_domain_t, Tag, next_sender_t, Env>();
    } else {
        using next_domain_t = decltype(__completion_domain_for(
            std::declval<result_t>(), std::declval<const Env&>()));
        using next_sender_t = __forwarded_argument_t<result_t>;
        return once_noexcept &&
               __completion_domain_noexcept<result_t, Env>() &&
               __transform_recurse_noexcept<
                   next_domain_t, Tag, next_sender_t, Env>();
    }
}

template<class D, class Tag, class S, class Env>
    requires std::same_as<
        std::remove_cvref_t<__transform_once_result_t<D, Tag, S, Env>>,
        std::remove_cvref_t<S>> &&
        (std::is_reference_v<__stable_transform_result_t<
             S, __transform_once_result_t<D, Tag, S, Env>>> ||
         std::constructible_from<
             __stable_transform_result_t<
                 S, __transform_once_result_t<D, Tag, S, Env>>,
             __transform_once_result_t<D, Tag, S, Env>&&>)
constexpr auto __transform_recurse(D& domain, Tag tag, S&& sndr, const Env& env)
    noexcept(__transform_recurse_noexcept<D, Tag, S, Env>())
        -> __stable_transform_result_t<
            S, __transform_once_result_t<D, Tag, S, Env>> {
    decltype(auto) transformed = __transform_sender_once(
        domain, tag, static_cast<S&&>(sndr), env);
    using result_t = decltype(transformed);
    using stable_t = __stable_transform_result_t<S, result_t>;

    if constexpr (std::is_reference_v<stable_t>) {
        return static_cast<result_t&&>(transformed);
    } else {
        return stable_t(static_cast<result_t&&>(transformed));
    }
}

template<class D, class Tag, class S, class Env>
    requires (!std::same_as<
        std::remove_cvref_t<__transform_once_result_t<D, Tag, S, Env>>,
        std::remove_cvref_t<S>>)
constexpr decltype(auto) __transform_recurse(
    D& domain,
    Tag tag,
    S&& sndr,
    const Env& env)
    noexcept(__transform_recurse_noexcept<D, Tag, S, Env>()) {
    decltype(auto) transformed = __transform_sender_once(
        domain, tag, static_cast<S&&>(sndr), env);
    using result_t = decltype(transformed);

    if constexpr (std::same_as<std::remove_cvref_t<Tag>, start_t>) {
        auto next_domain = __start_domain_for(env);
        return __transform_recurse(
            next_domain,
            tag,
            static_cast<result_t&&>(transformed),
            env);
    } else {
        auto next_domain = __completion_domain_for(transformed, env);
        return __transform_recurse(
            next_domain,
            tag,
            static_cast<result_t&&>(transformed),
            env);
    }
}

template<class S, class Env>
consteval bool __transform_sender_for_connect_noexcept() {
    using completion_domain_t = decltype(__completion_domain_for(
        std::declval<S>(), std::declval<const Env&>()));
    using completion_result_t = decltype(__transform_recurse(
        std::declval<completion_domain_t&>(),
        set_value_t{},
        std::declval<S>(),
        std::declval<const Env&>()));
    using start_domain_t = decltype(__start_domain_for(
        std::declval<const Env&>()));
    using start_sender_t = __forwarded_argument_t<completion_result_t>;

    return __completion_domain_noexcept<S, Env>() &&
           __transform_recurse_noexcept<
               completion_domain_t, set_value_t, S, Env>() &&
           __start_domain_noexcept<Env>() &&
           __transform_recurse_noexcept<
               start_domain_t, start_t, start_sender_t, Env>();
}

template<class S, class Env>
constexpr decltype(auto) __transform_sender_for_connect(
    S&& sndr,
    const Env& env)
    noexcept(__transform_sender_for_connect_noexcept<S, Env>()) {
    auto completion_domain = __completion_domain_for(sndr, env);
    decltype(auto) tmp = __transform_recurse(
        completion_domain, set_value_t{}, static_cast<S&&>(sndr), env);
    auto start_domain = __start_domain_for(env);
    return __transform_recurse(
        start_domain,
        start_t{},
        static_cast<decltype(tmp)&&>(tmp),
        env);
}

template<class S, class Env>
constexpr decltype(auto) __transform_sender_for_completion_signatures(
    S&& sndr,
    const Env& env) {
    return __transform_sender_for_connect(static_cast<S&&>(sndr), env);
}

template<class S, class Env>
consteval bool __completion_signatures_use_default_transform() {
    using env_t = std::remove_cvref_t<Env>;
    using completion_domain_t = decltype(__completion_domain_for(
        std::declval<S>(), std::declval<const env_t&>()));
    using start_domain_t = decltype(__start_domain_for(
        std::declval<const env_t&>()));
    return std::same_as<std::remove_cvref_t<completion_domain_t>, default_domain> &&
           std::same_as<std::remove_cvref_t<start_domain_t>, default_domain> &&
           !__sender_tag_transformable<set_value_t, S, env_t> &&
           !__sender_tag_transformable<start_t, S, env_t>;
}

template<class S, class R>
using __sender_completion_domain_for_t =
    decltype(__completion_domain_for(
        std::declval<S>(), std::declval<const __receiver_env_t<R>&>()));

} // namespace __forge_domain

template<class S, class Env>
    requires sender<S> && __forge_queryable<Env> &&
             requires(S&& sndr, const Env& env) {
                 __forge_domain::__transform_sender_for_connect(
                     static_cast<S&&>(sndr), env);
             }
constexpr decltype(auto) transform_sender(S&& sndr, const Env& env)
    noexcept(noexcept(__forge_domain::__transform_sender_for_connect(
        static_cast<S&&>(sndr), env))) {
    return __forge_domain::__transform_sender_for_connect(
        static_cast<S&&>(sndr), env);
}

namespace __forge_domain {

template<class Domain, class Tag, class S, class... Args>
concept __domain_apply_sender = requires(
    Domain& domain,
    S&& sndr,
    Args&&... args) {
    domain.apply_sender(
        Tag{},
        static_cast<S&&>(sndr),
        static_cast<Args&&>(args)...);
};

template<class Domain, class Tag, class S, class... Args>
consteval bool __apply_sender_noexcept() {
    if constexpr (__domain_apply_sender<Domain, Tag, S, Args...>) {
        return noexcept(std::declval<Domain&>().apply_sender(
            Tag{}, std::declval<S>(), std::declval<Args>()...));
    } else {
        return noexcept(default_domain{}.apply_sender(
            Tag{}, std::declval<S>(), std::declval<Args>()...));
    }
}

} // namespace __forge_domain

template<class Domain, class Tag, class S, class... Args>
    requires sender<S> &&
             (__forge_domain::__domain_apply_sender<
                  Domain, Tag, S, Args...> ||
              __forge_domain::__domain_apply_sender<
                  default_domain, Tag, S, Args...>)
constexpr decltype(auto) apply_sender(
    Domain domain,
    Tag,
    S&& sndr,
    Args&&... args)
    noexcept(__forge_domain::__apply_sender_noexcept<
        Domain, Tag, S, Args...>()) {
    if constexpr (__forge_domain::__domain_apply_sender<
                      Domain, Tag, S, Args...>) {
        return domain.apply_sender(
            Tag{},
            static_cast<S&&>(sndr),
            static_cast<Args&&>(args)...);
    } else {
        return default_domain{}.apply_sender(
            Tag{},
            static_cast<S&&>(sndr),
            static_cast<Args&&>(args)...);
    }
}

namespace __forge_domain {

template<class S, class R>
concept __default_connect_domains =
    receiver<R> &&
    __completion_signatures_use_default_transform<S, __receiver_env_t<R>>();

template<class S, class R>
using __transformed_sender_for_t =
    decltype(__transform_sender_for_connect(
        std::declval<S>(), std::declval<__receiver_env_t<R>&>()));

template<class S, class R>
concept __domain_connectable =
    receiver<R> &&
    (!__default_connect_domains<S, R>) &&
    requires {
        typename __transformed_sender_for_t<S, R>;
    } &&
    __direct_connectable<__transformed_sender_for_t<S, R>, R>;

template<class S, class R>
concept __connectable =
    receiver<R> &&
    ((__default_connect_domains<S, R> && __direct_connectable<S, R>) ||
     __domain_connectable<S, R>);

template<class S, class R>
consteval bool __connect_noexcept() {
    if constexpr (__default_connect_domains<S, R>) {
        return __direct_noexcept<S, R>();
    } else {
        return __transform_sender_for_connect_noexcept<S, __receiver_env_t<R>>() &&
               __direct_noexcept<__transformed_sender_for_t<S, R>, R>();
    }
}

template<class S, class R>
decltype(auto) __direct_connect(S&& s, R&& r)
    noexcept(__direct_noexcept<S, R>()) {
    if constexpr (__member_connect<S, R>) {
        return static_cast<S&&>(s).connect(static_cast<R&&>(r));
    } else {
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
        return __forge_awaitable::__connect_awaitable<std::decay_t<S>, std::decay_t<R>>(
            static_cast<S&&>(s), static_cast<R&&>(r));
#endif
    }
}

template<class S, class R>
decltype(auto) __domain_connect(S&& s, R&& r)
    noexcept(__connect_noexcept<S, R>()) {
    decltype(auto) env = std::execution::get_env(r);
    decltype(auto) ts = __transform_sender_for_connect(static_cast<S&&>(s), env);
    return __direct_connect(
        static_cast<decltype(ts)&&>(ts),
        static_cast<R&&>(r));
}

} // namespace __forge_domain

struct connect_t {
    template<class S, class R>
        requires __forge_domain::__connectable<S, R>
    auto operator()(S&& s, R&& r) const
        noexcept(__forge_domain::__connect_noexcept<S, R>()) {
        if constexpr (__forge_domain::__default_connect_domains<S, R>) {
            return __forge_domain::__direct_connect(static_cast<S&&>(s), static_cast<R&&>(r));
        } else {
            return __forge_domain::__domain_connect(static_cast<S&&>(s), static_cast<R&&>(r));
        }
    }
};
inline constexpr connect_t connect{};

template<class S, class R>
using connect_result_t = decltype(connect_t{}(std::declval<S>(), std::declval<R>()));

template<class S, class R>
concept __forge_sender_to =
    receiver<R> &&
    sender_in<S, env_of_t<R>> &&
    __forge_receiver_of<R, completion_signatures_of_t<S, env_of_t<R>>> &&
    requires(S&& s, R&& r) {
        std::execution::connect(
            static_cast<S&&>(s),
            static_cast<R&&>(r));
    };

template<class S, class R>
concept sender_to = __forge_sender_to<S, R>;

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

} // namespace __forge_cpo_detail

struct schedule_t {
    template<class S>
        requires __forge_cpo_detail::__member_schedule<S>
    constexpr auto operator()(S&& s) const
        noexcept(__forge_cpo_detail::__nothrow_member_schedule<S>)
        -> decltype(static_cast<S&&>(s).schedule()) {
        using result_t = decltype(static_cast<S&&>(s).schedule());
        static_assert(sender<result_t>, "schedule must return a sender");
        return static_cast<S&&>(s).schedule();
    }
};
inline constexpr schedule_t schedule{};

namespace __forge_env_detail {

template<class S>
consteval bool __scheduler_shape() {
    return requires { typename std::remove_cvref_t<S>::scheduler_concept; } &&
        requires { requires std::derived_from<
            typename std::remove_cvref_t<S>::scheduler_concept, scheduler_tag>; } &&
        __forge_queryable<S> && std::copyable<std::remove_cvref_t<S>> &&
        std::equality_comparable<std::remove_cvref_t<S>> &&
        requires(S&& s) { { std::execution::schedule(static_cast<S&&>(s)) } -> sender; };
}

} // namespace __forge_env_detail

template<class S>
concept scheduler = __forge_env_detail::__scheduler_shape<S>() && requires(S&& s) {
    { std::execution::get_forward_progress_guarantee(s) } -> std::same_as<forward_progress_guarantee>;
};

namespace __forge_env_detail {
template<class S>
consteval bool __scheduler() { return scheduler<S>; }
} // namespace __forge_env_detail

template<scheduler Sch>
using schedule_result_t = decltype(std::execution::schedule(std::declval<Sch>()));

namespace __forge_domain {

template<class Attrs>
consteval bool __scheduler_attributes() {
    return scheduler<Attrs>;
}

} // namespace __forge_domain

} // namespace std::execution

namespace std {

template<class... Ds, class... Es>
struct common_type<execution::indeterminate_domain<Ds...>, execution::indeterminate_domain<Es...>> {
    using type = execution::__forge_meta::list_to_variant_t<execution::indeterminate_domain,
        execution::__forge_meta::list_push_unique_all_t<execution::__forge_meta::type_list<>, Ds..., Es...>>;
};

template<class... Ds, class D>
struct common_type<execution::indeterminate_domain<Ds...>, D>
    : conditional_t<sizeof...(Ds) == 0, type_identity<D>,
                    common_type<execution::indeterminate_domain<Ds...>, execution::indeterminate_domain<D>>> {};

template<class D, class... Ds>
struct common_type<D, execution::indeterminate_domain<Ds...>>
    : common_type<execution::indeterminate_domain<Ds...>, D> {};

} // namespace std

namespace std::execution::__forge_domain {

template<class... Ds>
consteval auto __common_domain_type() {
    if constexpr (requires { std::common_type_t<Ds...>(); }) {
        return std::type_identity<std::common_type_t<Ds...>>{};
    } else {
        return std::type_identity<__forge_meta::list_to_variant_t<indeterminate_domain,
            __forge_meta::list_push_unique_all_t<__forge_meta::type_list<>, Ds...>>>{};
    }
}
template<class... Ds>
using __common_domain_t = typename decltype(__common_domain_type<Ds...>())::type;

template<class Tag, class S, class... Envs>
    requires (sizeof...(Envs) != 0 || requires {
        std::execution::get_completion_domain<Tag>(std::execution::get_env(std::declval<S>()));
    })
consteval auto __compl_domain_type() {
    if constexpr (requires {
        std::execution::get_completion_domain<Tag>(
            std::execution::get_env(std::declval<S>()), std::declval<Envs>()...);
    }) {
        return std::type_identity<decltype(std::execution::get_completion_domain<Tag>(
            std::execution::get_env(std::declval<S>()), std::declval<Envs>()...))>{};
    } else {
        return std::type_identity<indeterminate_domain<>>{};
    }
}
template<class Tag, class S, class... Envs>
using __compl_domain_t = typename decltype(__compl_domain_type<Tag, S, Envs...>())::type;

} // namespace std::execution::__forge_domain

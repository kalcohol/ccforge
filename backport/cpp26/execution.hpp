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

// NOTE: This is a Forge backport of a small P2300 sender/receiver MVP.
// It is intentionally minimal and correctness-first.

// Language version guard.
#if __cplusplus < 202002L
#error "Forge <execution> backport requires C++20 or newer"
#endif

#if __has_include(<version>)
#include <version>
#endif

#include <atomic>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace std {

// Stop token types are specified in namespace std in P2300R10.
// Guard with a future-facing feature test macro if/when it exists.
#if !defined(__cpp_lib_inplace_stop_token) || (__cpp_lib_inplace_stop_token < 202400L)

class inplace_stop_source;
class inplace_stop_token;
template<class Callback>
class inplace_stop_callback;

namespace __forge_stop_detail {

struct callback_base {
    using fn_t = void(callback_base*) noexcept;
    fn_t*          invoke_fn = nullptr;
    callback_base* next      = nullptr;
    callback_base** prev     = nullptr;
};

} // namespace __forge_stop_detail

class inplace_stop_source {
public:
    inplace_stop_source() noexcept = default;
    // Precondition: no inplace_stop_callback objects referencing this source
    // exist at the point of destruction.  See [stopsource.inplace.general].
    ~inplace_stop_source() noexcept = default;

    inplace_stop_source(const inplace_stop_source&) = delete;
    inplace_stop_source& operator=(const inplace_stop_source&) = delete;

    [[nodiscard]] inplace_stop_token get_token() noexcept;

    bool request_stop() noexcept {
        __forge_stop_detail::callback_base* head = nullptr;
        {
            std::lock_guard lk{mtx_};
            auto st = state_.load(std::memory_order_acquire);
            if ((st & kStopRequested) != 0) {
                return false;
            }
            state_.store(static_cast<std::uint8_t>(st | kStopRequested),
                         std::memory_order_release);

            // Detach the callback list under the lock, but do NOT invoke
            // callbacks here.  Invoking user callbacks while holding mtx_
            // would deadlock if the callback tries to register/remove
            // another callback.  See [stopsource.inplace.mem].
            head = callbacks_;
            callbacks_ = nullptr;
        }

        // Invoke callbacks outside the lock.
        while (head) {
            auto* next = head->next;
            head->next = nullptr;
            head->prev = nullptr;
            if (head->invoke_fn) {
                head->invoke_fn(head);
            }
            head = next;
        }
        return true;
    }

    [[nodiscard]] bool stop_requested() const noexcept {
        return (state_.load(std::memory_order_acquire) & kStopRequested) != 0;
    }

private:
    friend class inplace_stop_token;
    template<class Cb>
    friend class inplace_stop_callback;

    bool try_add_callback(__forge_stop_detail::callback_base* cb) noexcept {
        std::lock_guard lk{mtx_};
        if (stop_requested()) {
            return false;
        }

        cb->next = callbacks_;
        cb->prev = &callbacks_;
        if (callbacks_) {
            callbacks_->prev = &cb->next;
        }
        callbacks_ = cb;
        return true;
    }

    void remove_callback(__forge_stop_detail::callback_base* cb) noexcept {
        std::lock_guard lk{mtx_};
        if (!cb->prev) {
            return; // already removed/detached
        }

        if (cb->next) {
            cb->next->prev = cb->prev;
        }
        *cb->prev = cb->next;
        cb->next = nullptr;
        cb->prev = nullptr;
    }

    static constexpr std::uint8_t kStopRequested = 1;

    std::atomic<std::uint8_t> state_{0};
    std::mutex mtx_{};
    __forge_stop_detail::callback_base* callbacks_ = nullptr;
};

class inplace_stop_token {
public:
    inplace_stop_token() noexcept : source_(nullptr) {}

    [[nodiscard]] bool stop_requested() const noexcept {
        return source_ && source_->stop_requested();
    }
    [[nodiscard]] bool stop_possible() const noexcept { return source_ != nullptr; }

    void swap(inplace_stop_token& other) noexcept { std::swap(source_, other.source_); }
    friend void swap(inplace_stop_token& a, inplace_stop_token& b) noexcept { a.swap(b); }

    bool operator==(const inplace_stop_token&) const noexcept = default;

private:
    friend class inplace_stop_source;
    template<class Cb>
    friend class inplace_stop_callback;

    explicit inplace_stop_token(inplace_stop_source* src) noexcept : source_(src) {}

    inplace_stop_source* source_;
};

inline inplace_stop_token inplace_stop_source::get_token() noexcept {
    return inplace_stop_token{this};
}

template<class Callback>
class inplace_stop_callback : __forge_stop_detail::callback_base {
public:
    using callback_type = Callback;

    template<class Cb>
        requires std::constructible_from<Callback, Cb>
    explicit inplace_stop_callback(inplace_stop_token token, Cb&& cb)
        noexcept(std::is_nothrow_constructible_v<Callback, Cb>)
        : callback_(std::forward<Cb>(cb))
        , source_(token.source_) {
        this->invoke_fn = [](std::__forge_stop_detail::callback_base* base) noexcept {
            auto* self = static_cast<inplace_stop_callback*>(base);
            self->callback_();
        };

        if (source_ && !source_->try_add_callback(this)) {
            // Already stopped: invoke immediately.
            callback_();
            source_ = nullptr;
        }
    }

    ~inplace_stop_callback() {
        if (source_) {
            source_->remove_callback(this);
        }
    }

    inplace_stop_callback(const inplace_stop_callback&) = delete;
    inplace_stop_callback& operator=(const inplace_stop_callback&) = delete;

private:
    [[no_unique_address]] Callback callback_;
    inplace_stop_source* source_;
};

template<class Cb>
inplace_stop_callback(inplace_stop_token, Cb) -> inplace_stop_callback<Cb>;

struct never_stop_token {
    [[nodiscard]] static constexpr bool stop_requested() noexcept { return false; }
    [[nodiscard]] static constexpr bool stop_possible() noexcept { return false; }
    bool operator==(const never_stop_token&) const noexcept = default;
};

#endif // !__cpp_lib_inplace_stop_token

} // namespace std

namespace std::execution {

namespace __forge_detail {

// Non-movable mixin.  Inheriting this makes a type satisfy the
// operation_state requirement !move_constructible<O> while keeping
// the derived type an aggregate (C++17: base classes of aggregates
// need not have user-declared constructors as long as they have no
// virtual functions and no virtual/private/protected bases).
struct __immovable {
    __immovable() = default;
    __immovable(__immovable&&) = delete;
};

// tag_invoke protocol (internal).
namespace __tag_invoke {

void tag_invoke() = delete;

struct fn {
    template<class Tag, class... Args>
        requires requires(Tag&& tag, Args&&... args) {
            tag_invoke(static_cast<Tag&&>(tag), static_cast<Args&&>(args)...);
        }
    constexpr auto operator()(Tag&& tag, Args&&... args) const
        noexcept(noexcept(tag_invoke(static_cast<Tag&&>(tag), static_cast<Args&&>(args)...)))
            -> decltype(tag_invoke(static_cast<Tag&&>(tag), static_cast<Args&&>(args)...)) {
        return tag_invoke(static_cast<Tag&&>(tag), static_cast<Args&&>(args)...);
    }
};

} // namespace __tag_invoke

inline constexpr __tag_invoke::fn tag_invoke_fn{};

template<class Tag, class... Args>
concept tag_invocable =
    requires(Tag&& tag, Args&&... args) {
        std::execution::__forge_detail::tag_invoke_fn(
            static_cast<Tag&&>(tag), static_cast<Args&&>(args)...);
    };

template<class Tag, class... Args>
concept nothrow_tag_invocable =
    tag_invocable<Tag, Args...> &&
    noexcept(std::execution::__forge_detail::tag_invoke_fn(
        std::declval<Tag>(), std::declval<Args>()...));

template<class Tag, class... Args>
    requires tag_invocable<Tag, Args...>
using tag_invoke_result_t =
    decltype(std::execution::__forge_detail::tag_invoke_fn(
        std::declval<Tag>(), std::declval<Args>()...));

} // namespace __forge_detail

// ──────────────────────────────────────────────────────────────────────────
// Environment queries (P2300 MVP)
// ──────────────────────────────────────────────────────────────────────────

struct empty_env {};

struct get_env_t {
    template<class T>
        requires __forge_detail::tag_invocable<get_env_t, const T&>
    auto operator()(const T& obj) const
        noexcept(__forge_detail::nothrow_tag_invocable<get_env_t, const T&>)
            -> __forge_detail::tag_invoke_result_t<get_env_t, const T&> {
        return __forge_detail::tag_invoke_fn(*this, obj);
    }
};
inline constexpr get_env_t get_env{};

template<class T>
using env_of_t = decltype(std::execution::get_env(std::declval<const T&>()));

struct get_scheduler_t {
    template<class Env>
        requires __forge_detail::tag_invocable<get_scheduler_t, Env>
    auto operator()(Env&& env) const
        noexcept(__forge_detail::nothrow_tag_invocable<get_scheduler_t, Env>)
            -> __forge_detail::tag_invoke_result_t<get_scheduler_t, Env> {
        return __forge_detail::tag_invoke_fn(*this, static_cast<Env&&>(env));
    }
};
inline constexpr get_scheduler_t get_scheduler{};

struct get_stop_token_t {
    template<class Env>
        requires __forge_detail::tag_invocable<get_stop_token_t, Env>
    auto operator()(Env&& env) const
        noexcept(__forge_detail::nothrow_tag_invocable<get_stop_token_t, Env>)
            -> __forge_detail::tag_invoke_result_t<get_stop_token_t, Env> {
        return __forge_detail::tag_invoke_fn(*this, static_cast<Env&&>(env));
    }

    // Fallback: return never_stop_token.
    std::never_stop_token operator()(const empty_env&) const noexcept { return {}; }
};
inline constexpr get_stop_token_t get_stop_token{};

struct get_allocator_t {
    template<class Env>
        requires __forge_detail::tag_invocable<get_allocator_t, Env>
    auto operator()(Env&& env) const
        noexcept(__forge_detail::nothrow_tag_invocable<get_allocator_t, Env>)
            -> __forge_detail::tag_invoke_result_t<get_allocator_t, Env> {
        return __forge_detail::tag_invoke_fn(*this, static_cast<Env&&>(env));
    }
};
inline constexpr get_allocator_t get_allocator{};

template<class Tag, class Value>
struct prop {
    [[no_unique_address]] Tag tag_;
    [[no_unique_address]] Value value_;

    friend auto tag_invoke(Tag, const prop& self)
        noexcept(std::is_nothrow_copy_constructible_v<Value>) -> Value {
        return self.value_;
    }
};

template<class Tag, class Value>
prop(Tag, Value) -> prop<Tag, Value>;

template<class Tag, class Value>
[[nodiscard]] auto make_prop(Tag tag, Value&& val) {
    return prop<Tag, std::decay_t<Value>>{tag, std::forward<Value>(val)};
}

template<class... Envs>
struct env : Envs... {};

template<class... Envs>
env(Envs...) -> env<Envs...>;

template<class... Envs>
[[nodiscard]] auto make_env(Envs&&... envs) {
    return env<std::decay_t<Envs>...>{std::forward<Envs>(envs)...};
}

// ──────────────────────────────────────────────────────────────────────────
// Completion signatures and meta utilities
// ──────────────────────────────────────────────────────────────────────────

struct set_value_t {
    template<class R, class... Vs>
        requires __forge_detail::tag_invocable<set_value_t, R, Vs...>
    void operator()(R&& r, Vs&&... vs) const noexcept {
        static_assert(__forge_detail::nothrow_tag_invocable<set_value_t, R, Vs...>,
                      "set_value(receiver, ...) must be noexcept");
        __forge_detail::tag_invoke_fn(*this, static_cast<R&&>(r), static_cast<Vs&&>(vs)...);
    }
};

struct set_error_t {
    template<class R, class E>
        requires __forge_detail::tag_invocable<set_error_t, R, E>
    void operator()(R&& r, E&& e) const noexcept {
        static_assert(__forge_detail::nothrow_tag_invocable<set_error_t, R, E>,
                      "set_error(receiver, error) must be noexcept");
        __forge_detail::tag_invoke_fn(*this, static_cast<R&&>(r), static_cast<E&&>(e));
    }
};

struct set_stopped_t {
    template<class R>
        requires __forge_detail::tag_invocable<set_stopped_t, R>
    void operator()(R&& r) const noexcept {
        static_assert(__forge_detail::nothrow_tag_invocable<set_stopped_t, R>,
                      "set_stopped(receiver) must be noexcept");
        __forge_detail::tag_invoke_fn(*this, static_cast<R&&>(r));
    }
};

inline constexpr set_value_t set_value{};
inline constexpr set_error_t set_error{};
inline constexpr set_stopped_t set_stopped{};

template<class... Sigs>
struct completion_signatures {};

struct get_completion_signatures_t {
    template<class S, class Env>
        requires __forge_detail::tag_invocable<get_completion_signatures_t, S, Env>
    auto operator()(S&& s, Env&& env) const
        noexcept(__forge_detail::nothrow_tag_invocable<get_completion_signatures_t, S, Env>)
            -> __forge_detail::tag_invoke_result_t<get_completion_signatures_t, S, Env> {
        return __forge_detail::tag_invoke_fn(*this, static_cast<S&&>(s), static_cast<Env&&>(env));
    }
};
inline constexpr get_completion_signatures_t get_completion_signatures{};

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

// ──────────────────────────────────────────────────────────────────────────
// Core concepts and CPOs
// ──────────────────────────────────────────────────────────────────────────

struct start_t {
    template<class O>
        requires __forge_detail::tag_invocable<start_t, O&>
    void operator()(O& op) const noexcept {
        static_assert(__forge_detail::nothrow_tag_invocable<start_t, O&>, "start() must be noexcept");
        __forge_detail::tag_invoke_fn(*this, op);
    }
};
inline constexpr start_t start{};

template<class O>
concept operation_state =
    std::destructible<O> && std::is_object_v<O> &&
    !std::move_constructible<O> &&
    requires(O& op) { { std::execution::start(op) } noexcept; };

struct receiver_t {};
struct sender_t {};

template<class R>
concept receiver =
    std::move_constructible<std::remove_cvref_t<R>> &&
    std::constructible_from<std::remove_cvref_t<R>, std::remove_cvref_t<R>> &&
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
    : std::bool_constant<__forge_detail::nothrow_tag_invocable<set_value_t, R, Vs...>> {};

template<class R, class E>
struct receiver_accepts<R, set_error_t(E)>
    : std::bool_constant<__forge_detail::nothrow_tag_invocable<set_error_t, R, E>> {};

template<class R>
struct receiver_accepts<R, set_stopped_t()>
    : std::bool_constant<__forge_detail::nothrow_tag_invocable<set_stopped_t, R>> {};

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

template<class S>
concept sender =
    std::move_constructible<std::remove_cvref_t<S>> &&
    std::constructible_from<std::remove_cvref_t<S>, std::remove_cvref_t<S>> &&
    requires { typename std::remove_cvref_t<S>::sender_concept; } &&
    std::derived_from<typename std::remove_cvref_t<S>::sender_concept, sender_t> &&
    requires(const std::remove_cvref_t<S>& s) {
        { std::execution::get_env(s) } -> queryable;
    };

template<class S, class Env = empty_env>
concept sender_in = sender<S> && requires(std::remove_cvref_t<S>&& s, Env env) {
    std::execution::get_completion_signatures(static_cast<std::remove_cvref_t<S>&&>(s), env);
};

struct connect_t {
    template<class S, class R>
        requires __forge_detail::tag_invocable<connect_t, S, R>
    auto operator()(S&& s, R&& r) const
        noexcept(__forge_detail::nothrow_tag_invocable<connect_t, S, R>)
            -> __forge_detail::tag_invoke_result_t<connect_t, S, R> {
        return __forge_detail::tag_invoke_fn(*this, static_cast<S&&>(s), static_cast<R&&>(r));
    }
};
inline constexpr connect_t connect{};

template<class S, class R>
using connect_result_t = __forge_detail::tag_invoke_result_t<connect_t, S, R>;

template<class S, class R>
concept sender_to =
    sender<S> && receiver<R> && requires(std::remove_cvref_t<S>&& s, std::remove_cvref_t<R>&& r) {
        std::execution::connect(static_cast<std::remove_cvref_t<S>&&>(s), static_cast<std::remove_cvref_t<R>&&>(r));
    };

struct schedule_t {
    template<class S>
        requires __forge_detail::tag_invocable<schedule_t, S>
    auto operator()(S&& s) const
        noexcept(__forge_detail::nothrow_tag_invocable<schedule_t, S>)
            -> __forge_detail::tag_invoke_result_t<schedule_t, S> {
        return __forge_detail::tag_invoke_fn(*this, static_cast<S&&>(s));
    }
};
inline constexpr schedule_t schedule{};

template<class S>
concept scheduler =
    std::copy_constructible<std::remove_cvref_t<S>> &&
    std::equality_comparable<std::remove_cvref_t<S>> &&
    requires(std::remove_cvref_t<S>& s) { { std::execution::schedule(s) } -> sender_in; };

// ──────────────────────────────────────────────────────────────────────────
// Algorithms: just / just_error / just_stopped
// ──────────────────────────────────────────────────────────────────────────

namespace __forge_just {

template<class R, class... Vs>
struct operation : __forge_detail::__immovable {
    [[no_unique_address]] R rcvr_;
    std::tuple<Vs...> values_;

    operation(R rcvr, std::tuple<Vs...> vals)
        : rcvr_(std::move(rcvr)), values_(std::move(vals)) {}

    friend void tag_invoke(start_t, operation& self) noexcept {
        std::apply(
            [&](Vs&&... vs) noexcept {
                std::execution::set_value(std::move(self.rcvr_), static_cast<Vs&&>(vs)...);
            },
            std::move(self.values_));
    }
};

template<class... Vs>
struct sender {
    using sender_concept = sender_t;
    std::tuple<Vs...> values_;

    friend auto tag_invoke(get_completion_signatures_t, const sender&, auto) noexcept
        -> completion_signatures<set_value_t(Vs...)> {
        return {};
    }

    template<receiver R>
    friend auto tag_invoke(connect_t, sender self, R rcvr) -> operation<R, Vs...> {
        return operation<R, Vs...>(std::move(rcvr), std::move(self.values_));
    }

    friend auto tag_invoke(get_env_t, const sender&) noexcept -> empty_env { return {}; }
};

} // namespace __forge_just

template<class... Vs>
    requires (std::move_constructible<std::decay_t<Vs>> && ...)
[[nodiscard]] auto just(Vs&&... vs) {
    return __forge_just::sender<std::decay_t<Vs>...>{
        std::tuple<std::decay_t<Vs>...>{std::forward<Vs>(vs)...}};
}

namespace __forge_just_error {

template<class R, class E>
struct operation : __forge_detail::__immovable {
    [[no_unique_address]] R rcvr_;
    E error_;

    operation(R rcvr, E err)
        : rcvr_(std::move(rcvr)), error_(std::move(err)) {}

    friend void tag_invoke(start_t, operation& self) noexcept {
        std::execution::set_error(std::move(self.rcvr_), std::move(self.error_));
    }
};

template<class E>
struct sender {
    using sender_concept = sender_t;
    E error_;

    friend auto tag_invoke(get_completion_signatures_t, const sender&, auto) noexcept
        -> completion_signatures<set_error_t(E)> {
        return {};
    }

    template<receiver R>
    friend auto tag_invoke(connect_t, sender self, R rcvr) -> operation<R, E> {
        return operation<R, E>(std::move(rcvr), std::move(self.error_));
    }

    friend auto tag_invoke(get_env_t, const sender&) noexcept -> empty_env { return {}; }
};

} // namespace __forge_just_error

template<class E>
    requires std::move_constructible<std::decay_t<E>>
[[nodiscard]] auto just_error(E&& e) {
    return __forge_just_error::sender<std::decay_t<E>>{std::forward<E>(e)};
}

namespace __forge_just_stopped {

template<class R>
struct operation : __forge_detail::__immovable {
    [[no_unique_address]] R rcvr_;

    explicit operation(R rcvr) : rcvr_(std::move(rcvr)) {}

    friend void tag_invoke(start_t, operation& self) noexcept { std::execution::set_stopped(std::move(self.rcvr_)); }
};

struct sender {
    using sender_concept = sender_t;

    friend auto tag_invoke(get_completion_signatures_t, const sender&, auto) noexcept
        -> completion_signatures<set_stopped_t()> {
        return {};
    }

    template<receiver R>
    friend auto tag_invoke(connect_t, sender, R rcvr) -> operation<R> {
        return operation<R>(std::move(rcvr));
    }

    friend auto tag_invoke(get_env_t, const sender&) noexcept -> empty_env { return {}; }
};

} // namespace __forge_just_stopped

[[nodiscard]] inline auto just_stopped() noexcept { return __forge_just_stopped::sender{}; }

// ──────────────────────────────────────────────────────────────────────────
// then
// ──────────────────────────────────────────────────────────────────────────

namespace __forge_then {

template<class... Ts>
struct sig_list {};

template<class List, class T>
struct sig_list_contains;

template<class... Ts, class T>
struct sig_list_contains<sig_list<Ts...>, T> : std::bool_constant<(std::is_same_v<T, Ts> || ...)> {};

template<class List, class T>
inline constexpr bool sig_list_contains_v = sig_list_contains<List, T>::value;

template<class List, class T>
struct sig_list_push_unique;

template<class... Ts, class T>
struct sig_list_push_unique<sig_list<Ts...>, T> {
    using type = std::conditional_t<sig_list_contains_v<sig_list<Ts...>, T>, sig_list<Ts...>, sig_list<Ts..., T>>;
};

template<class List, class T>
using sig_list_push_unique_t = typename sig_list_push_unique<List, T>::type;

template<class List>
struct sig_list_to_completion_signatures;

template<class... Ts>
struct sig_list_to_completion_signatures<sig_list<Ts...>> {
    using type = completion_signatures<Ts...>;
};

template<class List>
using sig_list_to_completion_signatures_t = typename sig_list_to_completion_signatures<List>::type;

template<class R>
struct value_sig_from_result {
    using type = set_value_t(R);
};
template<>
struct value_sig_from_result<void> {
    using type = set_value_t();
};

template<class Fn, class UpSig>
struct transform_sig {
    using type = UpSig;
};

template<class Fn, class... Vs>
struct transform_sig<Fn, set_value_t(Vs...)> {
    using r_t = std::invoke_result_t<Fn, Vs...>;
    using type = typename value_sig_from_result<r_t>::type;
};

template<class Fn, class E>
struct transform_sig<Fn, set_error_t(E)> {
    using type = set_error_t(E);
};

template<class Fn>
struct transform_sig<Fn, set_stopped_t()> {
    using type = set_stopped_t();
};

template<class Fn, class UpCompletionSignatures>
struct transform_completion_signatures;

template<class Fn, class... UpSigs>
struct transform_completion_signatures<Fn, completion_signatures<UpSigs...>> {
private:
    template<class List, class Sig>
    struct add_one {
        using transformed = typename transform_sig<Fn, Sig>::type;
        using type = sig_list_push_unique_t<List, transformed>;
    };

    template<class List, class... Rest>
    struct add_all;

    template<class List>
    struct add_all<List> {
        using type = List;
    };

    template<class List, class Sig, class... Rest>
    struct add_all<List, Sig, Rest...> {
        using next = typename add_one<List, Sig>::type;
        using type = typename add_all<next, Rest...>::type;
    };

    using initial = sig_list<>;
    using mapped = typename add_all<initial, UpSigs...>::type;
    using with_eptr = sig_list_push_unique_t<mapped, set_error_t(std::exception_ptr)>;

public:
    using type = sig_list_to_completion_signatures_t<with_eptr>;
};

template<class Fn, class UpCompletionSignatures>
using transform_completion_signatures_t = typename transform_completion_signatures<Fn, UpCompletionSignatures>::type;

template<class R, class Fn>
struct then_receiver {
    using receiver_concept = receiver_t;

    [[no_unique_address]] R rcvr_;
    [[no_unique_address]] Fn fn_;

    template<class... Vs>
    friend void tag_invoke(set_value_t, then_receiver&& self, Vs&&... vs) noexcept {
        if constexpr (std::is_void_v<std::invoke_result_t<Fn, Vs...>>) {
            try {
                std::invoke(std::move(self.fn_), static_cast<Vs&&>(vs)...);
                std::execution::set_value(std::move(self.rcvr_));
            } catch (...) {
                std::execution::set_error(std::move(self.rcvr_), std::current_exception());
            }
        } else {
            try {
                std::execution::set_value(
                    std::move(self.rcvr_),
                    std::invoke(std::move(self.fn_), static_cast<Vs&&>(vs)...));
            } catch (...) {
                std::execution::set_error(std::move(self.rcvr_), std::current_exception());
            }
        }
    }

    template<class E>
    friend void tag_invoke(set_error_t, then_receiver&& self, E&& e) noexcept {
        std::execution::set_error(std::move(self.rcvr_), static_cast<E&&>(e));
    }

    friend void tag_invoke(set_stopped_t, then_receiver&& self) noexcept {
        std::execution::set_stopped(std::move(self.rcvr_));
    }

    friend auto tag_invoke(get_env_t, const then_receiver& self) noexcept -> env_of_t<R> {
        return std::execution::get_env(self.rcvr_);
    }
};

template<class S, class Fn>
struct then_sender {
    using sender_concept = sender_t;

    [[no_unique_address]] S sndr_;
    [[no_unique_address]] Fn fn_;

    friend auto tag_invoke(get_completion_signatures_t, const then_sender& self, auto env) noexcept {
        using up_cs_t = decltype(std::execution::get_completion_signatures(self.sndr_, env));
        using out_cs_t = transform_completion_signatures_t<Fn, up_cs_t>;
        return out_cs_t{};
    }

    template<std::execution::receiver R>
    friend auto tag_invoke(connect_t, then_sender self, R rcvr) {
        return std::execution::connect(std::move(self.sndr_),
                                       then_receiver<R, Fn>{std::move(rcvr), std::move(self.fn_)});
    }

    friend auto tag_invoke(get_env_t, const then_sender& self) noexcept -> env_of_t<S> {
        return std::execution::get_env(self.sndr_);
    }
};

template<class Fn>
struct then_closure {
    [[no_unique_address]] Fn fn_;

    template<std::execution::sender S>
        requires std::copy_constructible<Fn>
    [[nodiscard]] auto operator()(S&& s) const & {
        return then_sender<std::decay_t<S>, Fn>{std::forward<S>(s), fn_};
    }

    template<std::execution::sender S>
    [[nodiscard]] auto operator()(S&& s) && {
        return then_sender<std::decay_t<S>, Fn>{std::forward<S>(s), std::move(fn_)};
    }

    template<std::execution::sender S>
        requires std::copy_constructible<Fn>
    friend constexpr auto operator|(S&& s, const then_closure& self) {
        return self(std::forward<S>(s));
    }

    template<std::execution::sender S>
    friend constexpr auto operator|(S&& s, then_closure&& self) {
        return std::move(self)(std::forward<S>(s));
    }
};

struct then_t {
    template<std::execution::sender S, class Fn>
    [[nodiscard]] auto operator()(S&& s, Fn&& fn) const {
        return then_sender<std::decay_t<S>, std::decay_t<Fn>>{std::forward<S>(s), std::forward<Fn>(fn)};
    }

    template<class Fn>
    [[nodiscard]] auto operator()(Fn&& fn) const {
        return then_closure<std::decay_t<Fn>>{std::forward<Fn>(fn)};
    }
};

} // namespace __forge_then

inline constexpr __forge_then::then_t then{};

// ──────────────────────────────────────────────────────────────────────────
// sync_wait
// ──────────────────────────────────────────────────────────────────────────

namespace __forge_sync_wait {

struct stopped_t {};

template<class... Vs>
struct shared_state {
    std::mutex mtx_;
    std::condition_variable cv_;
    bool done_ = false;

    using value_t = std::tuple<Vs...>;
    std::variant<std::monostate, value_t, std::exception_ptr, stopped_t> result_;

    std::inplace_stop_source stop_source_;
};

template<class Tuple>
struct state_from_tuple;

template<class... Vs>
struct state_from_tuple<std::tuple<Vs...>> {
    using type = shared_state<Vs...>;
};

template<class CompletionSignatures>
struct value_tuple_for;

template<class... Sigs>
struct value_tuple_for<completion_signatures<Sigs...>> {
private:
    template<class Sig>
    struct is_value_sig : std::false_type {};
    template<class... Vs>
    struct is_value_sig<set_value_t(Vs...)> : std::true_type {};

    static constexpr std::size_t kCount = (static_cast<std::size_t>(is_value_sig<Sigs>::value) + ... + 0u);

    template<class Sig>
    struct value_tuple_of_sig {
        using type = std::tuple<>;
    };
    template<class... Vs>
    struct value_tuple_of_sig<set_value_t(Vs...)> {
        using type = std::tuple<Vs...>;
    };

    template<class... Ts>
    struct first_value_tuple_from_pack {
        using type = std::tuple<>;
    };

    template<class Sig, class... Rest>
    struct first_value_tuple_from_pack<Sig, Rest...> {
        using type = std::conditional_t<is_value_sig<Sig>::value,
                                        typename value_tuple_of_sig<Sig>::type,
                                        typename first_value_tuple_from_pack<Rest...>::type>;
    };

public:
    static_assert(kCount <= 1, "sync_wait MVP supports at most one set_value signature");
    using type = typename first_value_tuple_from_pack<Sigs...>::type;
};

template<class State>
struct receiver {
    using receiver_concept = receiver_t;
    State* state_;

    template<class... Vs>
    friend void tag_invoke(set_value_t, receiver&& self, Vs&&... vs) noexcept {
        {
            std::lock_guard lk{self.state_->mtx_};
            self.state_->result_.template emplace<1>(std::forward<Vs>(vs)...);
            self.state_->done_ = true;
        }
        self.state_->cv_.notify_one();
    }

    template<class E>
    friend void tag_invoke(set_error_t, receiver&& self, E&& e) noexcept {
        {
            std::lock_guard lk{self.state_->mtx_};
            if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>) {
                self.state_->result_.template emplace<2>(std::forward<E>(e));
            } else {
                self.state_->result_.template emplace<2>(std::make_exception_ptr(std::forward<E>(e)));
            }
            self.state_->done_ = true;
        }
        self.state_->cv_.notify_one();
    }

    friend void tag_invoke(set_stopped_t, receiver&& self) noexcept {
        {
            std::lock_guard lk{self.state_->mtx_};
            self.state_->result_.template emplace<3>();
            self.state_->done_ = true;
        }
        self.state_->cv_.notify_one();
    }

    friend auto tag_invoke(get_env_t, const receiver& self) noexcept {
        return std::execution::make_env(
            std::execution::make_prop(get_stop_token_t{}, self.state_->stop_source_.get_token()));
    }
};

} // namespace __forge_sync_wait

template<sender_in S>
auto sync_wait(S&& sndr) {
    using cs_t = decltype(std::execution::get_completion_signatures(std::declval<S>(), empty_env{}));
    using value_tuple_t = typename __forge_sync_wait::value_tuple_for<cs_t>::type;
    using state_t = typename __forge_sync_wait::state_from_tuple<value_tuple_t>::type;
    using recv_t = __forge_sync_wait::receiver<state_t>;

    state_t state;
    auto op = std::execution::connect(std::forward<S>(sndr), recv_t{&state});
    std::execution::start(op);

    std::unique_lock lk{state.mtx_};
    state.cv_.wait(lk, [&] { return state.done_; });

    if (state.result_.index() == 2) {
        std::rethrow_exception(std::get<2>(state.result_));
    }
    if (state.result_.index() == 3) {
        return std::optional<typename state_t::value_t>{std::nullopt};
    }
    return std::optional<typename state_t::value_t>{std::get<1>(state.result_)};
}

// ──────────────────────────────────────────────────────────────────────────
// inline_scheduler
// ──────────────────────────────────────────────────────────────────────────

namespace __forge_inline {

class inline_scheduler;

struct env {
    const inline_scheduler* sched_;
    friend auto tag_invoke(get_scheduler_t, const env& self) noexcept -> inline_scheduler;
};

template<class R>
struct operation : __forge_detail::__immovable {
    [[no_unique_address]] R rcvr_;

    explicit operation(R rcvr) : rcvr_(std::move(rcvr)) {}

    friend void tag_invoke(start_t, operation& self) noexcept { std::execution::set_value(std::move(self.rcvr_)); }
};

struct sender {
    using sender_concept = sender_t;
    const inline_scheduler* sched_ = nullptr;

    friend auto tag_invoke(get_completion_signatures_t, const sender&, auto) noexcept
        -> completion_signatures<set_value_t()> {
        return {};
    }

    template<receiver R>
    friend auto tag_invoke(connect_t, sender s, R rcvr) -> operation<R> {
        return operation<R>(std::move(rcvr));
    }

    friend auto tag_invoke(get_env_t, const sender& self) noexcept -> env { return env{self.sched_}; }
};

} // namespace __forge_inline

class __forge_inline::inline_scheduler {
public:
    inline_scheduler() noexcept = default;

    [[nodiscard]] __forge_inline::sender schedule() const noexcept { return __forge_inline::sender{this}; }

    friend auto tag_invoke(schedule_t, const inline_scheduler& self) noexcept -> __forge_inline::sender {
        return __forge_inline::sender{&self};
    }

    bool operator==(const inline_scheduler&) const noexcept = default;
};

namespace __forge_inline {
inline auto tag_invoke(get_scheduler_t, const env& self) noexcept -> inline_scheduler { return *self.sched_; }
} // namespace __forge_inline

using inline_scheduler = __forge_inline::inline_scheduler;

} // namespace std::execution

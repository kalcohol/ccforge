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
#include "env.hpp"

#include <exception>
#include <functional>
#include <type_traits>

namespace std::execution {

namespace __forge_upon {

template<class R>
struct __value_cs_from_result {
    using type = completion_signatures<set_value_t(R)>;
};

template<>
struct __value_cs_from_result<void> {
    using type = completion_signatures<set_value_t()>;
};

template<bool Enabled>
struct __maybe_eptr_cs {
    using type = completion_signatures<>;
};

template<>
struct __maybe_eptr_cs<true> {
    using type = completion_signatures<set_error_t(std::exception_ptr)>;
};

template<class Fn, class Sig>
struct __accepts_error_sig : std::true_type {};

template<class Fn, class E>
struct __accepts_error_sig<Fn, set_error_t(E)>
    : std::bool_constant<std::is_invocable_v<Fn, E>> {};

template<class Fn, class Sig, bool IsError>
struct __handler_may_throw : std::false_type {};

template<class Fn, class E>
struct __handler_may_throw<Fn, set_error_t(E), true>
    : std::bool_constant<
          std::is_invocable_v<Fn, E> &&
          !std::is_nothrow_invocable_v<Fn, E>> {};

template<class Fn>
struct __handler_may_throw<Fn, set_stopped_t(), false>
    : std::bool_constant<!std::is_nothrow_invocable_v<Fn>> {};

template<class Fn, class Sig, bool IsError>
struct __transform_sig {
    using type = completion_signatures<Sig>;
};

template<class Fn, class E, bool Invocable>
struct __transform_error_sig {
    using type = completion_signatures<set_error_t(E)>;
};

template<class Fn, class E>
struct __transform_error_sig<Fn, E, true> {
    using result_t = std::invoke_result_t<Fn, E>;
    using type = typename __value_cs_from_result<result_t>::type;
};

template<class Fn, class E>
struct __transform_sig<Fn, set_error_t(E), true>
    : __transform_error_sig<Fn, E, std::is_invocable_v<Fn, E>> {};

template<class Fn>
struct __transform_sig<Fn, set_stopped_t(), false> {
    using result_t = std::invoke_result_t<Fn>;
    using type = typename __value_cs_from_result<result_t>::type;
};

template<class Fn, bool IsError, class CS>
struct __completion_sigs;

template<class Fn, bool IsError, class... Sigs>
struct __completion_sigs<Fn, IsError, completion_signatures<Sigs...>> {
    static_assert(
        !IsError || (__accepts_error_sig<Fn, Sigs>::value && ...),
        "upon_error handler must accept every error completion shape");

    static constexpr bool handler_may_throw =
        (__handler_may_throw<Fn, Sigs, IsError>::value || ...);

    using type = __forge_meta::__concat_unique_cs_t<
        typename __transform_sig<Fn, Sigs, IsError>::type...,
        typename __maybe_eptr_cs<handler_may_throw>::type>;
};

template<class R, class Fn>
struct __recv_error {
    using receiver_concept = receiver_t;
    R __rcvr;
    Fn __fn;

    template<class... Vs>
    void set_value(Vs&&... vs) && noexcept {
        std::execution::set_value(std::move(__rcvr), static_cast<Vs&&>(vs)...);
    }

    template<class E>
    void set_error(E&& e) && noexcept {
        if constexpr (!std::invocable<Fn, E>) {
            std::execution::set_error(std::move(__rcvr), static_cast<E&&>(e));
        } else if constexpr (std::is_nothrow_invocable_v<Fn, E>) {
            if constexpr (std::is_void_v<std::invoke_result_t<Fn, E>>) {
                std::invoke(std::move(__fn), static_cast<E&&>(e));
                std::execution::set_value(std::move(__rcvr));
            } else {
                std::execution::set_value(
                    std::move(__rcvr),
                    std::invoke(std::move(__fn), static_cast<E&&>(e)));
            }
        } else if constexpr (std::is_void_v<std::invoke_result_t<Fn, E>>) {
            try {
                std::invoke(std::move(__fn), static_cast<E&&>(e));
                std::execution::set_value(std::move(__rcvr));
            } catch (...) {
                std::execution::set_error(std::move(__rcvr), std::current_exception());
            }
        } else {
            try {
                std::execution::set_value(std::move(__rcvr),
                                          std::invoke(std::move(__fn), static_cast<E&&>(e)));
            } catch (...) {
                std::execution::set_error(std::move(__rcvr), std::current_exception());
            }
        }
    }

    void set_stopped() && noexcept {
        std::execution::set_stopped(std::move(__rcvr));
    }

    auto get_env() const noexcept -> env_of_t<R> {
        return std::execution::get_env(__rcvr);
    }
};

template<class R, class Fn>
struct __recv_stopped {
    using receiver_concept = receiver_t;
    R __rcvr;
    Fn __fn;

    template<class... Vs>
    void set_value(Vs&&... vs) && noexcept {
        std::execution::set_value(std::move(__rcvr), static_cast<Vs&&>(vs)...);
    }

    template<class E>
    void set_error(E&& e) && noexcept {
        std::execution::set_error(std::move(__rcvr), static_cast<E&&>(e));
    }

    void set_stopped() && noexcept {
        if constexpr (std::is_nothrow_invocable_v<Fn>) {
            if constexpr (std::is_void_v<std::invoke_result_t<Fn>>) {
                std::invoke(std::move(__fn));
                std::execution::set_value(std::move(__rcvr));
            } else {
                std::execution::set_value(
                    std::move(__rcvr), std::invoke(std::move(__fn)));
            }
        } else if constexpr (std::is_void_v<std::invoke_result_t<Fn>>) {
            try {
                std::invoke(std::move(__fn));
                std::execution::set_value(std::move(__rcvr));
            } catch (...) {
                std::execution::set_error(std::move(__rcvr), std::current_exception());
            }
        } else {
            try {
                std::execution::set_value(std::move(__rcvr), std::invoke(std::move(__fn)));
            } catch (...) {
                std::execution::set_error(std::move(__rcvr), std::current_exception());
            }
        }
    }

    auto get_env() const noexcept -> env_of_t<R> {
        return std::execution::get_env(__rcvr);
    }
};

template<class S, class Fn, bool IsError>
struct __sender {
    using sender_concept = sender_t;
    using source_t = S;
    using fn_t = Fn;

    S __sndr;
    Fn __fn;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        using self_t = std::remove_cvref_t<Self>;
        using up_cs_t = decltype(std::execution::get_completion_signatures(
            std::declval<typename self_t::source_t>(),
            std::declval<Env>()));
        using out_cs_t = typename __completion_sigs<typename self_t::fn_t, IsError, up_cs_t>::type;
        return out_cs_t{};
    }

    template<receiver R>
    auto connect(R r) && {
        if constexpr (IsError) {
            return std::execution::connect(std::move(__sndr),
                __recv_error<R, Fn>{std::move(r), std::move(__fn)});
        } else {
            return std::execution::connect(std::move(__sndr),
                __recv_stopped<R, Fn>{std::move(r), std::move(__fn)});
        }
    }

    template<receiver R>
        requires std::copy_constructible<S> && std::copy_constructible<Fn>
    auto connect(R r) const& {
        if constexpr (IsError) {
            return std::execution::connect(S(__sndr),
                __recv_error<R, Fn>{std::move(r), Fn(__fn)});
        } else {
            return std::execution::connect(S(__sndr),
                __recv_stopped<R, Fn>{std::move(r), Fn(__fn)});
        }
    }

    auto get_env() const noexcept {
        return std::execution::get_env(__sndr);
    }
};

template<class Fn>
struct __upon_error_closure {
    Fn __fn;
    template<sender S>
    [[nodiscard]] auto operator()(S&& s) const & {
        return __sender<std::decay_t<S>, Fn, true>{
            __forge_detail::__forward_as_given(std::forward<S>(s)), __fn};
    }
    template<sender S>
    [[nodiscard]] auto operator()(S&& s) && {
        return __sender<std::decay_t<S>, Fn, true>{
            __forge_detail::__forward_as_given(std::forward<S>(s)), std::move(__fn)};
    }
    template<sender S>
    friend constexpr auto operator|(S&& s, const __upon_error_closure& self) {
        return self(std::forward<S>(s));
    }
    template<sender S>
    friend constexpr auto operator|(S&& s, __upon_error_closure&& self) {
        return std::move(self)(std::forward<S>(s));
    }
};

template<class Fn>
struct __upon_stopped_closure {
    Fn __fn;
    template<sender S>
    [[nodiscard]] auto operator()(S&& s) const & {
        return __sender<std::decay_t<S>, Fn, false>{
            __forge_detail::__forward_as_given(std::forward<S>(s)), __fn};
    }
    template<sender S>
    [[nodiscard]] auto operator()(S&& s) && {
        return __sender<std::decay_t<S>, Fn, false>{
            __forge_detail::__forward_as_given(std::forward<S>(s)), std::move(__fn)};
    }
    template<sender S>
    friend constexpr auto operator|(S&& s, const __upon_stopped_closure& self) {
        return self(std::forward<S>(s));
    }
    template<sender S>
    friend constexpr auto operator|(S&& s, __upon_stopped_closure&& self) {
        return std::move(self)(std::forward<S>(s));
    }
};

struct __upon_error_t {
    template<sender S, class Fn>
    [[nodiscard]] auto operator()(S&& s, Fn&& fn) const {
        return __sender<std::decay_t<S>, std::decay_t<Fn>, true>{
            __forge_detail::__forward_as_given(std::forward<S>(s)),
            std::forward<Fn>(fn)};
    }
    template<class Fn>
    [[nodiscard]] auto operator()(Fn&& fn) const {
        return __upon_error_closure<std::decay_t<Fn>>{std::forward<Fn>(fn)};
    }
};

struct __upon_stopped_t {
    template<sender S, class Fn>
    [[nodiscard]] auto operator()(S&& s, Fn&& fn) const {
        return __sender<std::decay_t<S>, std::decay_t<Fn>, false>{
            __forge_detail::__forward_as_given(std::forward<S>(s)),
            std::forward<Fn>(fn)};
    }
    template<class Fn>
    [[nodiscard]] auto operator()(Fn&& fn) const {
        return __upon_stopped_closure<std::decay_t<Fn>>{std::forward<Fn>(fn)};
    }
};

} // namespace __forge_upon

inline constexpr __forge_upon::__upon_error_t upon_error{};
inline constexpr __forge_upon::__upon_stopped_t upon_stopped{};

} // namespace std::execution

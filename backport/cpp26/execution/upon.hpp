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

namespace std::execution {

namespace __forge_upon {

template<class R, class Fn>
struct __recv_error {
    using receiver_concept = receiver_t;
    R __rcvr;
    Fn __fn;

    template<class... Vs>
    friend void tag_invoke(set_value_t, __recv_error&& self, Vs&&... vs) noexcept {
        set_value(std::move(self.__rcvr), static_cast<Vs&&>(vs)...);
    }

    template<class E>
    friend void tag_invoke(set_error_t, __recv_error&& self, E&& e) noexcept {
        if constexpr (std::is_void_v<std::invoke_result_t<Fn, E>>) {
            try {
                std::invoke(std::move(self.__fn), static_cast<E&&>(e));
                set_value(std::move(self.__rcvr));
            } catch (...) {
                set_error(std::move(self.__rcvr), std::current_exception());
            }
        } else {
            try {
                set_value(std::move(self.__rcvr),
                          std::invoke(std::move(self.__fn), static_cast<E&&>(e)));
            } catch (...) {
                set_error(std::move(self.__rcvr), std::current_exception());
            }
        }
    }

    friend void tag_invoke(set_stopped_t, __recv_error&& self) noexcept {
        set_stopped(std::move(self.__rcvr));
    }

    friend auto tag_invoke(get_env_t, const __recv_error& self) noexcept -> env_of_t<R> {
        return std::execution::get_env(self.__rcvr);
    }
};

template<class R, class Fn>
struct __recv_stopped {
    using receiver_concept = receiver_t;
    R __rcvr;
    Fn __fn;

    template<class... Vs>
    friend void tag_invoke(set_value_t, __recv_stopped&& self, Vs&&... vs) noexcept {
        set_value(std::move(self.__rcvr), static_cast<Vs&&>(vs)...);
    }

    template<class E>
    friend void tag_invoke(set_error_t, __recv_stopped&& self, E&& e) noexcept {
        set_error(std::move(self.__rcvr), static_cast<E&&>(e));
    }

    friend void tag_invoke(set_stopped_t, __recv_stopped&& self) noexcept {
        if constexpr (std::is_void_v<std::invoke_result_t<Fn>>) {
            try {
                std::invoke(std::move(self.__fn));
                set_value(std::move(self.__rcvr));
            } catch (...) {
                set_error(std::move(self.__rcvr), std::current_exception());
            }
        } else {
            try {
                set_value(std::move(self.__rcvr), std::invoke(std::move(self.__fn)));
            } catch (...) {
                set_error(std::move(self.__rcvr), std::current_exception());
            }
        }
    }

    friend auto tag_invoke(get_env_t, const __recv_stopped& self) noexcept -> env_of_t<R> {
        return std::execution::get_env(self.__rcvr);
    }
};

template<class S, class Fn, bool IsError>
struct __sender {
    using sender_concept = sender_t;
    S __sndr;
    Fn __fn;

    template<class Env>
    friend auto tag_invoke(get_completion_signatures_t,
                           const __sender& self, Env&&) noexcept {
        using up_cs_t = decltype(std::execution::get_completion_signatures(
            self.__sndr, std::declval<Env>()));
        return up_cs_t{};
    }

    template<receiver R>
    friend auto tag_invoke(connect_t, __sender self, R r) {
        if constexpr (IsError) {
            return std::execution::connect(std::move(self.__sndr),
                __recv_error<R, Fn>{std::move(r), std::move(self.__fn)});
        } else {
            return std::execution::connect(std::move(self.__sndr),
                __recv_stopped<R, Fn>{std::move(r), std::move(self.__fn)});
        }
    }

    friend auto tag_invoke(get_env_t, const __sender& self) noexcept {
        return std::execution::get_env(self.__sndr);
    }
};

template<class Fn>
struct __upon_error_closure {
    Fn __fn;
    template<sender S>
    [[nodiscard]] auto operator()(S&& s) const & {
        return __sender<std::decay_t<S>, Fn, true>{std::forward<S>(s), __fn};
    }
    template<sender S>
    [[nodiscard]] auto operator()(S&& s) && {
        return __sender<std::decay_t<S>, Fn, true>{std::forward<S>(s), std::move(__fn)};
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
        return __sender<std::decay_t<S>, Fn, false>{std::forward<S>(s), __fn};
    }
    template<sender S>
    [[nodiscard]] auto operator()(S&& s) && {
        return __sender<std::decay_t<S>, Fn, false>{std::forward<S>(s), std::move(__fn)};
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
            std::forward<S>(s), std::forward<Fn>(fn)};
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
            std::forward<S>(s), std::forward<Fn>(fn)};
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

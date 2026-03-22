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

namespace __forge_let {

struct __value_tag  {};
struct __error_tag  {};
struct __stopped_tag{};

template<class S, class Fn, class R, class Which>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;

    R __outer_recv_;
    Fn __fn_;

    static constexpr std::size_t kBufSize = 1024;
    alignas(std::max_align_t) unsigned char __outer_buf_[kBufSize];
    alignas(std::max_align_t) unsigned char __inner_buf_[kBufSize];

    void (*__outer_start_)(void*) noexcept = nullptr;
    void (*__outer_dtor_)(void*)  noexcept = nullptr;
    void (*__inner_dtor_)(void*)  noexcept = nullptr;

    struct __inner_recv {
        using receiver_concept = receiver_t;
        __op* __self;
        template<class... Vs>
        friend void tag_invoke(set_value_t, __inner_recv&& self, Vs&&... vs) noexcept {
            set_value(std::move(self.__self->__outer_recv_), static_cast<Vs&&>(vs)...);
        }
        template<class E>
        friend void tag_invoke(set_error_t, __inner_recv&& self, E&& e) noexcept {
            set_error(std::move(self.__self->__outer_recv_), static_cast<E&&>(e));
        }
        friend void tag_invoke(set_stopped_t, __inner_recv&& self) noexcept {
            set_stopped(std::move(self.__self->__outer_recv_));
        }
        friend auto tag_invoke(get_env_t, const __inner_recv& self) noexcept
            -> env_of_t<R> {
            return std::execution::get_env(self.__self->__outer_recv_);
        }
    };

    template<class... Vs>
    void __start_inner(Vs&&... vs) noexcept {
        using inner_sender_t = std::decay_t<std::invoke_result_t<Fn, std::decay_t<Vs>...>>;
        using inner_op_t = connect_result_t<inner_sender_t, __inner_recv>;
        static_assert(sizeof(inner_op_t) <= kBufSize,
            "let_value/error/stopped: inner op too large for buffer");
        try {
            auto inner_sndr = std::invoke(std::move(__fn_), static_cast<Vs&&>(vs)...);
            ::new(static_cast<void*>(__inner_buf_))
                inner_op_t(std::execution::connect(
                    std::move(inner_sndr), __inner_recv{this}));
            __inner_dtor_ = [](void* p) noexcept {
                static_cast<inner_op_t*>(p)->~inner_op_t();
            };
            std::execution::start(*static_cast<inner_op_t*>(
                static_cast<void*>(__inner_buf_)));
        } catch (...) {
            set_error(std::move(__outer_recv_), std::current_exception());
        }
    }

    struct __outer_recv {
        using receiver_concept = receiver_t;
        __op* __self;

        template<class... Vs>
        friend void tag_invoke(set_value_t, __outer_recv&& self, Vs&&... vs) noexcept {
            if constexpr (std::is_same_v<Which, __value_tag>) {
                self.__self->__start_inner(static_cast<Vs&&>(vs)...);
            } else {
                set_value(std::move(self.__self->__outer_recv_), static_cast<Vs&&>(vs)...);
            }
        }
        template<class E>
        friend void tag_invoke(set_error_t, __outer_recv&& self, E&& e) noexcept {
            if constexpr (std::is_same_v<Which, __error_tag>) {
                self.__self->__start_inner(static_cast<E&&>(e));
            } else {
                set_error(std::move(self.__self->__outer_recv_), static_cast<E&&>(e));
            }
        }
        friend void tag_invoke(set_stopped_t, __outer_recv&& self) noexcept {
            if constexpr (std::is_same_v<Which, __stopped_tag>) {
                self.__self->__start_inner();
            } else {
                set_stopped(std::move(self.__self->__outer_recv_));
            }
        }
        friend auto tag_invoke(get_env_t, const __outer_recv& self) noexcept
            -> env_of_t<R> {
            return std::execution::get_env(self.__self->__outer_recv_);
        }
    };

    using __outer_op_t = connect_result_t<S, __outer_recv>;
    static_assert(sizeof(__outer_op_t) <= kBufSize,
        "let_value/error/stopped: outer op too large for buffer");

    __op(S sndr, Fn fn, R r)
        : __outer_recv_(std::move(r))
        , __fn_(std::move(fn))
    {
        ::new(static_cast<void*>(__outer_buf_))
            __outer_op_t(std::execution::connect(
                std::move(sndr), __outer_recv{this}));
        __outer_start_ = [](void* p) noexcept {
            std::execution::start(*static_cast<__outer_op_t*>(p));
        };
        __outer_dtor_ = [](void* p) noexcept {
            static_cast<__outer_op_t*>(p)->~__outer_op_t();
        };
    }

    ~__op() {
        if (__inner_dtor_) { __inner_dtor_(static_cast<void*>(__inner_buf_)); }
        if (__outer_dtor_) { __outer_dtor_(static_cast<void*>(__outer_buf_)); }
    }

    friend void tag_invoke(start_t, __op& self) noexcept {
        self.__outer_start_(static_cast<void*>(self.__outer_buf_));
    }
};

template<class S, class Fn, class Which>
struct __sender {
    using sender_concept = sender_t;
    S __sndr;
    Fn __fn;

    friend auto tag_invoke(get_completion_signatures_t,
                           const __sender& self, auto env) noexcept {
        return decltype(std::execution::get_completion_signatures(self.__sndr, env)){};
    }

    template<receiver R>
    friend auto tag_invoke(connect_t, __sender self, R r)
        -> __op<S, Fn, R, Which>
    {
        return __op<S, Fn, R, Which>(
            std::move(self.__sndr), std::move(self.__fn), std::move(r));
    }

    friend auto tag_invoke(get_env_t, const __sender& self) noexcept {
        return std::execution::get_env(self.__sndr);
    }
};

template<class Fn, class Which>
struct __let_closure {
    Fn __fn;
    template<sender S>
    [[nodiscard]] auto operator()(S&& s) const & {
        return __sender<std::decay_t<S>, Fn, Which>{std::forward<S>(s), __fn};
    }
    template<sender S>
    [[nodiscard]] auto operator()(S&& s) && {
        return __sender<std::decay_t<S>, Fn, Which>{std::forward<S>(s), std::move(__fn)};
    }
    template<sender S>
    friend constexpr auto operator|(S&& s, const __let_closure& self) {
        return self(std::forward<S>(s));
    }
    template<sender S>
    friend constexpr auto operator|(S&& s, __let_closure&& self) {
        return std::move(self)(std::forward<S>(s));
    }
};

template<class Which>
struct __let_t {
    template<sender S, class Fn>
    [[nodiscard]] auto operator()(S&& s, Fn&& fn) const {
        return __sender<std::decay_t<S>, std::decay_t<Fn>, Which>{
            std::forward<S>(s), std::forward<Fn>(fn)};
    }
    template<class Fn>
    [[nodiscard]] auto operator()(Fn&& fn) const {
        return __let_closure<std::decay_t<Fn>, Which>{std::forward<Fn>(fn)};
    }
};

} // namespace __forge_let

inline constexpr __forge_let::__let_t<__forge_let::__value_tag>   let_value{};
inline constexpr __forge_let::__let_t<__forge_let::__error_tag>   let_error{};
inline constexpr __forge_let::__let_t<__forge_let::__stopped_tag> let_stopped{};

} // namespace std::execution

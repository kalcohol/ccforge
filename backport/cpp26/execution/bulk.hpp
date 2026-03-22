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

namespace __forge_bulk {

template<class S, class Shape, class Fn, class R>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;

    struct __recv {
        using receiver_concept = receiver_t;
        R* __outer;
        Shape __shape;
        Fn __fn;

        template<class... Vs>
        friend void tag_invoke(set_value_t, __recv&& self, Vs&&... vs) noexcept {
            try {
                for (Shape i = Shape{}; i != self.__shape; ++i) {
                    self.__fn(i, vs...);
                }
                set_value(std::move(*self.__outer), static_cast<Vs&&>(vs)...);
            } catch (...) {
                set_error(std::move(*self.__outer), std::current_exception());
            }
        }
        template<class E>
        friend void tag_invoke(set_error_t, __recv&& self, E&& e) noexcept {
            set_error(std::move(*self.__outer), static_cast<E&&>(e));
        }
        friend void tag_invoke(set_stopped_t, __recv&& self) noexcept {
            set_stopped(std::move(*self.__outer));
        }
        friend auto tag_invoke(get_env_t, const __recv& self) noexcept
            -> env_of_t<R> {
            return std::execution::get_env(*self.__outer);
        }
    };

    using __inner_op_t = connect_result_t<S, __recv>;

    R __outer;
    __inner_op_t __inner;

    __op(S sndr, Shape shape, Fn fn, R recv)
        : __outer(std::move(recv))
        , __inner(std::execution::connect(
            std::move(sndr), __recv{&__outer, std::move(shape), std::move(fn)}))
    {}

    friend void tag_invoke(start_t, __op& self) noexcept {
        std::execution::start(self.__inner);
    }
};

template<class S, class Shape, class Fn>
struct __sender {
    using sender_concept = sender_t;
    S __sndr;
    Shape __shape;
    Fn __fn;

    friend auto tag_invoke(get_completion_signatures_t,
                           const __sender& self, auto env) noexcept {
        using up_cs = decltype(std::execution::get_completion_signatures(self.__sndr, env));
        using with_eptr = __forge_meta::__concat_cs_t<
            up_cs,
            completion_signatures<set_error_t(std::exception_ptr)>>;
        return with_eptr{};
    }

    template<receiver R>
    friend auto tag_invoke(connect_t, __sender self, R r)
        -> __op<S, Shape, Fn, R>
    {
        return __op<S, Shape, Fn, R>(
            std::move(self.__sndr), std::move(self.__shape),
            std::move(self.__fn), std::move(r));
    }

    friend auto tag_invoke(get_env_t, const __sender& self) noexcept {
        return std::execution::get_env(self.__sndr);
    }
};

template<class Fn>
struct __bulk_closure {
    std::decay_t<Fn> __fn_;

    template<class Shape>
    struct __with_shape {
        Shape __shape_;
        std::decay_t<Fn> __fn_;

        template<std::execution::sender S>
        [[nodiscard]] auto operator()(S&& s) && {
            return __sender<std::decay_t<S>, Shape, std::decay_t<Fn>>{
                std::forward<S>(s), std::move(__shape_), std::move(__fn_)};
        }

        template<std::execution::sender S>
        friend constexpr auto operator|(S&& s, __with_shape&& self) {
            return std::move(self)(std::forward<S>(s));
        }
    };
};

struct bulk_t {
    template<std::execution::sender S, class Shape, class Fn>
    [[nodiscard]] auto operator()(S&& s, Shape shape, Fn&& fn) const {
        return __sender<std::decay_t<S>, Shape, std::decay_t<Fn>>{
            std::forward<S>(s), std::move(shape), std::forward<Fn>(fn)};
    }

    template<class Shape, class Fn>
    [[nodiscard]] auto operator()(Shape shape, Fn&& fn) const {
        using closure_t = typename __bulk_closure<std::decay_t<Fn>>::template __with_shape<Shape>;
        return closure_t{std::move(shape), std::forward<Fn>(fn)};
    }
};

} // namespace __forge_bulk

inline constexpr __forge_bulk::bulk_t bulk{};

} // namespace std::execution

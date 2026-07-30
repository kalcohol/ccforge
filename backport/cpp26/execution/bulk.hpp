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

namespace std::execution {

namespace __forge_bulk {

struct __serial_policy {};

template<bool Chunked, class S, class Shape, class Fn, class R>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;

    struct __recv {
        using receiver_concept = receiver_t;
        R* __outer;
        Shape __shape;
        Fn __fn;

        template<class... Vs>
        void set_value(Vs&&... vs) && noexcept {
            try {
                if constexpr (Chunked) {
                    if (Shape{} < __shape) {
                        __fn(Shape{}, __shape, vs...);
                    }
                } else {
                    for (Shape i = Shape{}; i < __shape; ++i) {
                        __fn(i, vs...);
                    }
                }
                std::execution::set_value(std::move(*__outer), static_cast<Vs&&>(vs)...);
            } catch (...) {
                std::execution::set_error(std::move(*__outer), std::current_exception());
            }
        }
        template<class E>
        void set_error(E&& e) && noexcept {
            std::execution::set_error(std::move(*__outer), static_cast<E&&>(e));
        }
        void set_stopped() && noexcept {
            std::execution::set_stopped(std::move(*__outer));
        }
        auto get_env() const noexcept -> env_of_t<R> {
            return std::execution::get_env(*__outer);
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

    void start() & noexcept {
        std::execution::start(__inner);
    }
};

template<bool Chunked, class S, class Policy, class Shape, class Fn>
struct __sender {
    using sender_concept = sender_t;
    using source_t = S;

    S __sndr;
    Policy __policy;
    Shape __shape;
    Fn __fn;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        using self_t = std::remove_cvref_t<Self>;
        using up_cs = decltype(std::execution::get_completion_signatures(
            std::declval<typename self_t::source_t>(),
            std::declval<Env>()));
        using with_eptr = __forge_meta::__concat_cs_t<
            up_cs,
            completion_signatures<set_error_t(std::exception_ptr)>>;
        return with_eptr{};
    }

    template<receiver R>
    auto connect(R r) && -> __op<Chunked, S, Shape, Fn, R>
    {
        return __op<Chunked, S, Shape, Fn, R>(
            std::move(__sndr), std::move(__shape),
            std::move(__fn), std::move(r));
    }

    template<receiver R>
        requires std::copy_constructible<S> && std::copy_constructible<Shape> && std::copy_constructible<Fn>
    auto connect(R r) const& -> __op<Chunked, S, Shape, Fn, R>
    {
        return __op<Chunked, S, Shape, Fn, R>(
            __sndr, __shape, __fn, std::move(r));
    }

    auto get_env() const noexcept {
        return std::execution::get_env(__sndr);
    }
};

template<bool Chunked, class Fn>
struct __bulk_closure {
    std::decay_t<Fn> __fn_;

    template<class Policy, class Shape>
    struct __with_shape {
        std::decay_t<Policy> __policy_;
        Shape __shape_;
        std::decay_t<Fn> __fn_;

        template<std::execution::sender S>
        [[nodiscard]] auto operator()(S&& s) && {
            return __sender<Chunked, std::decay_t<S>, std::decay_t<Policy>, Shape, std::decay_t<Fn>>{
                __forge_detail::__forward_as_given(std::forward<S>(s)),
                std::move(__policy_),
                std::move(__shape_), std::move(__fn_)};
        }

        template<std::execution::sender S>
        friend constexpr auto operator|(S&& s, __with_shape&& self) {
            return std::move(self)(std::forward<S>(s));
        }
    };
};

template<bool Chunked>
struct __bulk_t {
#if defined(FORGE_HAS_NATIVE_EXECUTION_POLICIES)
    template<std::execution::sender S, class Policy, std::integral Shape, class Fn>
        requires std::is_execution_policy_v<std::remove_cvref_t<Policy>> &&
                 std::copy_constructible<std::decay_t<Fn>>
    [[nodiscard]] auto operator()(S&& s, Policy&& policy, Shape shape, Fn&& fn) const {
        return __sender<Chunked, std::decay_t<S>, std::decay_t<Policy>, Shape, std::decay_t<Fn>>{
            __forge_detail::__forward_as_given(std::forward<S>(s)),
            std::forward<Policy>(policy),
            std::move(shape), std::forward<Fn>(fn)};
    }
#endif

    template<std::execution::sender S, class Shape, class Fn>
        requires std::integral<Shape> && std::copy_constructible<std::decay_t<Fn>>
    [[nodiscard]] auto operator()(S&& s, Shape shape, Fn&& fn) const {
        return __sender<Chunked, std::decay_t<S>, __serial_policy, Shape, std::decay_t<Fn>>{
            __forge_detail::__forward_as_given(std::forward<S>(s)),
            __serial_policy{},
            std::move(shape), std::forward<Fn>(fn)};
    }

#if defined(FORGE_HAS_NATIVE_EXECUTION_POLICIES)
    template<class Policy, class Shape, class Fn>
        requires std::is_execution_policy_v<std::remove_cvref_t<Policy>> &&
                 std::integral<Shape> &&
                 std::copy_constructible<std::decay_t<Fn>>
    [[nodiscard]] auto operator()(Policy&& policy, Shape shape, Fn&& fn) const {
        using closure_t = typename __bulk_closure<Chunked, std::decay_t<Fn>>
            ::template __with_shape<std::decay_t<Policy>, Shape>;
        return closure_t{
            std::forward<Policy>(policy),
            std::move(shape),
            std::forward<Fn>(fn)};
    }
#endif

    template<class Shape, class Fn>
        requires std::integral<Shape> && std::copy_constructible<std::decay_t<Fn>>
    [[nodiscard]] auto operator()(Shape shape, Fn&& fn) const {
        using closure_t = typename __bulk_closure<Chunked, std::decay_t<Fn>>
            ::template __with_shape<__serial_policy, Shape>;
        return closure_t{
            __serial_policy{},
            std::move(shape),
            std::forward<Fn>(fn)};
    }
};

struct bulk_t : __bulk_t<false> {};
struct bulk_unchunked_t : __bulk_t<false> {};
struct bulk_chunked_t : __bulk_t<true> {};

} // namespace __forge_bulk

inline constexpr __forge_bulk::bulk_t bulk{};
inline constexpr __forge_bulk::bulk_chunked_t bulk_chunked{};
inline constexpr __forge_bulk::bulk_unchunked_t bulk_unchunked{};

} // namespace std::execution

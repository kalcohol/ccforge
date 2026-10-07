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
#include <memory>

namespace std::execution {

namespace __forge_bulk {

struct __serial_policy {};

template<class Policy>
using __policy_t = std::conditional_t<
    std::copy_constructible<std::remove_cvref_t<Policy>>,
    std::remove_cvref_t<Policy>, const std::remove_cvref_t<Policy>&>;

template<bool Chunked, class Shape, class Fn, class Sig>
struct __value_may_throw : std::false_type {};

template<class Shape, class Fn, class... Vs>
struct __value_may_throw<false, Shape, Fn, set_value_t(Vs...)>
    : std::bool_constant<
          !std::is_nothrow_invocable_v<Fn&, Shape, Vs&...>> {};

template<class Shape, class Fn, class... Vs>
struct __value_may_throw<true, Shape, Fn, set_value_t(Vs...)>
    : std::bool_constant<
          !std::is_nothrow_invocable_v<Fn&, Shape, Shape, Vs&...>> {};

template<bool Chunked, class Shape, class Fn, class CS>
struct __completion_signatures;

template<bool Chunked, class Shape, class Fn, class... Sigs>
struct __completion_signatures<
    Chunked, Shape, Fn, completion_signatures<Sigs...>> {
    static constexpr bool may_throw =
        (__value_may_throw<Chunked, Shape, Fn, Sigs>::value || ...);
    using type = std::conditional_t<
        may_throw,
        __forge_meta::__concat_unique_cs_t<
            completion_signatures<Sigs...>,
            completion_signatures<set_error_t(std::exception_ptr)>>,
        completion_signatures<Sigs...>>;
};

template<bool Chunked, class S, class Shape, class Fn, class R>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;

    struct __recv {
        using receiver_concept = receiver_t;
        R* __outer;
        Shape __shape;
        Fn* __fn;

        template<class... Vs>
        void set_value(Vs&&... vs) && noexcept {
            constexpr bool nothrow = Chunked
                ? std::is_nothrow_invocable_v<Fn&, Shape, Shape, Vs&...>
                : std::is_nothrow_invocable_v<Fn&, Shape, Vs&...>;
            auto run = [&]() noexcept(nothrow) {
                if constexpr (Chunked) {
                    if (Shape{} < __shape) {
                        (*__fn)(Shape{}, Shape(__shape), vs...);
                    }
                } else {
                    for (Shape i = Shape{}; i < __shape; ++i) {
                        (*__fn)(Shape(i), vs...);
                    }
                }
                std::execution::set_value(std::move(*__outer), static_cast<Vs&&>(vs)...);
            };
            if constexpr (nothrow) {
                run();
            } else {
                try {
                    run();
                } catch (...) {
                    std::execution::set_error(
                        std::move(*__outer), std::current_exception());
                }
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
    Fn __fn;
    // Destroy the inner operation before the state borrowed by its receiver.
    __inner_op_t __inner;

    __op(S sndr, Shape shape, Fn fn, R recv)
        : __outer(std::move(recv))
        , __fn(std::move(fn))
        , __inner(std::execution::connect(
            std::move(sndr), __recv{std::addressof(__outer), std::move(shape), std::addressof(__fn)}))
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
        using out_cs = typename __completion_signatures<
            Chunked, Shape, Fn, up_cs>::type;
        return out_cs{};
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
        return __forge_env_detail::__forwarding_attrs<env_of_t<S>>{
            std::execution::get_env(__sndr)};
    }
};

template<bool Chunked, class Fn>
struct __bulk_closure {
    std::decay_t<Fn> __fn_;

    template<class Policy, class Shape>
    struct __with_shape {
        using policy_t = std::decay_t<Policy>;
        using sender_policy_t = __policy_t<policy_t>;
        using fn_t = std::decay_t<Fn>;

        policy_t __policy_;
        Shape __shape_;
        fn_t __fn_;

        template<std::execution::sender S>
            requires std::constructible_from<sender_policy_t, policy_t&> &&
                     std::constructible_from<Shape, Shape&> &&
                     std::constructible_from<fn_t, fn_t&>
        [[nodiscard]] auto operator()(S&& s) & {
            return __sender<Chunked, std::decay_t<S>, sender_policy_t, Shape, fn_t>{
                __forge_detail::__forward_as_given(std::forward<S>(s)),
                sender_policy_t(__policy_), Shape(__shape_), fn_t(__fn_)};
        }

        template<std::execution::sender S>
            requires std::constructible_from<sender_policy_t, const policy_t&> &&
                     std::constructible_from<Shape, const Shape&> &&
                     std::constructible_from<fn_t, const fn_t&>
        [[nodiscard]] auto operator()(S&& s) const & {
            return __sender<Chunked, std::decay_t<S>, sender_policy_t, Shape, fn_t>{
                __forge_detail::__forward_as_given(std::forward<S>(s)),
                sender_policy_t(__policy_), Shape(__shape_), fn_t(__fn_)};
        }

        template<std::execution::sender S>
            requires std::constructible_from<sender_policy_t, policy_t&&> &&
                     std::constructible_from<Shape, Shape&&> &&
                     std::constructible_from<fn_t, fn_t&&>
        [[nodiscard]] auto operator()(S&& s) && {
            return __sender<Chunked, std::decay_t<S>, sender_policy_t, Shape, fn_t>{
                __forge_detail::__forward_as_given(std::forward<S>(s)),
                sender_policy_t(std::move(__policy_)),
                Shape(std::move(__shape_)), fn_t(std::move(__fn_))};
        }

        template<std::execution::sender S>
            requires std::constructible_from<sender_policy_t, const policy_t&&> &&
                     std::constructible_from<Shape, const Shape&&> &&
                     std::constructible_from<fn_t, const fn_t&&>
        [[nodiscard]] auto operator()(S&& s) const && {
            return __sender<Chunked, std::decay_t<S>, sender_policy_t, Shape, fn_t>{
                __forge_detail::__forward_as_given(std::forward<S>(s)),
                sender_policy_t(std::move(__policy_)),
                Shape(std::move(__shape_)), fn_t(std::move(__fn_))};
        }

        // An invalid cvref must not fall back to a different capture category.
        template<class S> void operator()(S&&) & = delete;
        template<class S> void operator()(S&&) const & = delete;
        template<class S> void operator()(S&&) && = delete;
        template<class S> void operator()(S&&) const && = delete;

        template<std::execution::sender S, class Self>
            requires std::same_as<std::remove_cvref_t<Self>, __with_shape> &&
                     requires(Self&& self, S&& s) {
                         static_cast<Self&&>(self)(std::forward<S>(s));
                     }
        friend constexpr auto operator|(S&& s, Self&& self) {
            return static_cast<Self&&>(self)(std::forward<S>(s));
        }
    };
};

template<bool Chunked>
struct __bulk_t {
#if defined(FORGE_HAS_NATIVE_EXECUTION_POLICIES)
    template<std::execution::sender S, class Policy, std::integral Shape, class Fn>
        requires std::is_execution_policy_v<std::remove_cvref_t<Policy>> &&
                 std::copy_constructible<std::decay_t<Fn>> &&
                 std::constructible_from<std::decay_t<Fn>, Fn> &&
                 std::constructible_from<__policy_t<Policy>, Policy>
    [[nodiscard]] auto operator()(S&& s, Policy&& policy, Shape shape, Fn&& fn) const {
        return __sender<Chunked, std::decay_t<S>, __policy_t<Policy>, Shape, std::decay_t<Fn>>{
            std::decay_t<S>(std::forward<S>(s)),
            __policy_t<Policy>(std::forward<Policy>(policy)),
            std::move(shape), std::decay_t<Fn>(std::forward<Fn>(fn))};
    }
#endif

    template<std::execution::sender S, class Shape, class Fn>
        requires std::integral<Shape> && std::copy_constructible<std::decay_t<Fn>> &&
                 std::constructible_from<std::decay_t<Fn>, Fn>
    [[nodiscard]] auto operator()(S&& s, Shape shape, Fn&& fn) const {
        return __sender<Chunked, std::decay_t<S>, __serial_policy, Shape, std::decay_t<Fn>>{
            std::decay_t<S>(std::forward<S>(s)),
            __serial_policy{},
            std::move(shape), std::decay_t<Fn>(std::forward<Fn>(fn))};
    }

#if defined(FORGE_HAS_NATIVE_EXECUTION_POLICIES)
    template<class Policy, class Shape, class Fn>
        requires std::is_execution_policy_v<std::remove_cvref_t<Policy>> &&
                 std::integral<Shape> &&
                 std::copy_constructible<std::decay_t<Fn>> &&
                 std::constructible_from<std::decay_t<Fn>, Fn> &&
                 std::constructible_from<std::decay_t<Policy>, Policy>
    [[nodiscard]] auto operator()(Policy&& policy, Shape shape, Fn&& fn) const {
        using closure_t = typename __bulk_closure<Chunked, std::decay_t<Fn>>
            ::template __with_shape<std::decay_t<Policy>, Shape>;
        return closure_t{
            std::decay_t<Policy>(std::forward<Policy>(policy)),
            std::move(shape),
            std::decay_t<Fn>(std::forward<Fn>(fn))};
    }
#endif

    template<class Shape, class Fn>
        requires std::integral<Shape> && std::copy_constructible<std::decay_t<Fn>> &&
                 std::constructible_from<std::decay_t<Fn>, Fn>
    [[nodiscard]] auto operator()(Shape shape, Fn&& fn) const {
        using closure_t = typename __bulk_closure<Chunked, std::decay_t<Fn>>
            ::template __with_shape<__serial_policy, Shape>;
        return closure_t{
            __serial_policy{},
            std::move(shape),
            std::decay_t<Fn>(std::forward<Fn>(fn))};
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

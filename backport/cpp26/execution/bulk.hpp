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
#include "env.hpp"
#include "detail/adaptor.hpp"

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

template<bool Chunked, class Shape, class Fn, class Sig>
struct __valid_sig : std::true_type {};

template<class Shape, class Fn, class... Vs>
struct __valid_sig<false, Shape, Fn, set_value_t(Vs...)>
    : std::bool_constant<std::is_invocable_v<Fn&, Shape, Vs&...>> {};

template<class Shape, class Fn, class... Vs>
struct __valid_sig<true, Shape, Fn, set_value_t(Vs...)>
    : std::bool_constant<std::is_invocable_v<Fn&, Shape, Shape, Vs&...>> {};

template<bool Chunked, class Shape, class Fn, class CS>
struct __completion_signatures {};

template<bool Chunked, class Shape, class Fn, class... Sigs>
    requires (__valid_sig<Chunked, Shape, Fn, Sigs>::value && ...)
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

template<bool Chunked, class Shape, class Fn, class R>
struct __recv {
    using receiver_concept = receiver_tag;
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
    auto get_env() const noexcept -> __forge_detail::__fwd_env_t<env_of_t<R>> {
        return {std::execution::get_env(*__outer)};
    }
};

template<bool Chunked, class S, class Shape, class Fn, class R>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_tag;

    using __recv = __forge_bulk::__recv<Chunked, Shape, Fn, R>;

    using __inner_op_t = connect_result_t<S, __recv>;

    R __outer;
    Fn __fn;
    // Destroy the inner operation before the state borrowed by its receiver.
    __inner_op_t __inner;

    template<class Data>
    __op(S&& sndr, Shape shape, Data&& fn, R recv)
        noexcept(std::is_nothrow_move_constructible_v<R> && noexcept(Fn(
            __forge_detail::__allocator_aware_forward(static_cast<Data&&>(fn), recv))) &&
            noexcept(std::execution::connect(static_cast<S&&>(sndr),
                __recv{nullptr, Shape{}, nullptr})))
        : __outer(std::move(recv))
        , __fn(__forge_detail::__allocator_aware_forward(static_cast<Data&&>(fn), __outer))
        , __inner(std::execution::connect(
            static_cast<S&&>(sndr), __recv{std::addressof(__outer), std::move(shape), std::addressof(__fn)}))
    {}

    void start() & noexcept {
        std::execution::start(__inner);
    }
};

template<bool Chunked, class S, class Policy, class Shape, class Fn, bool Transformed = false>
struct __sender {
    using sender_concept = sender_tag;
    using source_t = S;

    S __sndr;
    Policy __policy;
    Shape __shape;
    Fn __fn;

    template<class Child, class Env>
    using __cs = typename __completion_signatures<Chunked, Shape, Fn,
        completion_signatures_of_t<Child, __forge_detail::__fwd_env_t<Env>>>::type;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> __cs<__forge_detail::__child_category_t<Self, S>, Env> { return {}; }

    template<receiver R>
        requires requires { typename __cs<S, env_of_t<R>>;
                     typename connect_result_t<S, __recv<Chunked, Shape, Fn, R>>; }
    auto connect(R r) &&
        noexcept(!Transformed && noexcept(
            __op<Chunked, S, Shape, Fn, R>(std::move(__sndr), __shape, std::move(__fn), std::move(r))))
        -> __op<Chunked, S, Shape, Fn, R> {
        return __op<Chunked, S, Shape, Fn, R>(
            std::move(__sndr), __shape, std::move(__fn), std::move(r));
    }

    template<receiver R>
        requires std::copy_constructible<S> && std::copy_constructible<Shape> &&
                 std::copy_constructible<Fn> && requires { typename __cs<const S&, env_of_t<R>>;
                     typename connect_result_t<const S&, __recv<Chunked, Shape, Fn, R>>; }
    auto connect(R r) const&
        noexcept(!Transformed && noexcept(__op<Chunked, const S&, Shape, Fn, R>(
            __sndr, __shape, __fn, std::move(r)))) -> __op<Chunked, const S&, Shape, Fn, R> {
        return __op<Chunked, const S&, Shape, Fn, R>(__sndr, __shape, __fn, std::move(r));
    }

    // An invalid rvalue child connection must not retry the const-lvalue path.
    template<class R> void connect(R) && = delete;

    auto get_env() const noexcept {
        return __forge_env_detail::__forwarding_attrs<env_of_t<S>>{
            std::execution::get_env(__sndr)};
    }
};

template<bool Chunked, class Fn, bool Transformed>
struct __bulk_closure {
    std::decay_t<Fn> __fn_;

    template<class Policy, class Shape>
    struct __with_shape : sender_adaptor_closure<__with_shape<Policy, Shape>> {
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
            return __sender<Chunked, std::decay_t<S>, sender_policy_t, Shape, fn_t, Transformed>{
                __forge_detail::__forward_as_given(std::forward<S>(s)),
                sender_policy_t(__policy_), Shape(__shape_), fn_t(__fn_)};
        }

        template<std::execution::sender S>
            requires std::constructible_from<sender_policy_t, const policy_t&> &&
                     std::constructible_from<Shape, const Shape&> &&
                     std::constructible_from<fn_t, const fn_t&>
        [[nodiscard]] auto operator()(S&& s) const & {
            return __sender<Chunked, std::decay_t<S>, sender_policy_t, Shape, fn_t, Transformed>{
                __forge_detail::__forward_as_given(std::forward<S>(s)),
                sender_policy_t(__policy_), Shape(__shape_), fn_t(__fn_)};
        }

        template<std::execution::sender S>
            requires std::constructible_from<sender_policy_t, policy_t&&> &&
                     std::constructible_from<Shape, Shape&&> &&
                     std::constructible_from<fn_t, fn_t&&>
        [[nodiscard]] auto operator()(S&& s) && {
            return __sender<Chunked, std::decay_t<S>, sender_policy_t, Shape, fn_t, Transformed>{
                __forge_detail::__forward_as_given(std::forward<S>(s)),
                sender_policy_t(std::move(__policy_)),
                Shape(std::move(__shape_)), fn_t(std::move(__fn_))};
        }

        template<std::execution::sender S>
            requires std::constructible_from<sender_policy_t, const policy_t&&> &&
                     std::constructible_from<Shape, const Shape&&> &&
                     std::constructible_from<fn_t, const fn_t&&>
        [[nodiscard]] auto operator()(S&& s) const && {
            return __sender<Chunked, std::decay_t<S>, sender_policy_t, Shape, fn_t, Transformed>{
                __forge_detail::__forward_as_given(std::forward<S>(s)),
                sender_policy_t(std::move(__policy_)),
                Shape(std::move(__shape_)), fn_t(std::move(__fn_))};
        }

        // An invalid cvref must not fall back to a different capture category.
        template<class S> void operator()(S&&) & = delete;
        template<class S> void operator()(S&&) const & = delete;
        template<class S> void operator()(S&&) && = delete;
        template<class S> void operator()(S&&) const && = delete;
    };
};

template<bool Chunked, bool Transformed = false>
struct __serial_bulk_t {
    template<class Shape, class Fn>
    using __closure_t = typename __bulk_closure<Chunked, std::decay_t<Fn>, Transformed>
        ::template __with_shape<__serial_policy, Shape>;

    template<std::execution::sender S, std::integral Shape, class Fn>
        requires std::copy_constructible<std::decay_t<Fn>> &&
                 std::constructible_from<std::decay_t<Fn>, Fn>
    [[nodiscard]] auto operator()(S&& s, Shape shape, Fn&& fn) const
        -> __sender<Chunked, std::decay_t<S>, __serial_policy, Shape, std::decay_t<Fn>, Transformed> {
        return {std::decay_t<S>(std::forward<S>(s)), __serial_policy{},
                std::move(shape), std::decay_t<Fn>(std::forward<Fn>(fn))};
    }

    template<std::integral Shape, class Fn>
        requires std::copy_constructible<std::decay_t<Fn>> &&
                 std::constructible_from<std::decay_t<Fn>, Fn>
    [[nodiscard]] auto operator()(Shape shape, Fn&& fn) const -> __closure_t<Shape, Fn> {
        return {{}, __serial_policy{}, std::move(shape), std::decay_t<Fn>(std::forward<Fn>(fn))};
    }
};

template<bool Chunked, bool Transformed = false>
struct __bulk_t {
#if defined(FORGE_HAS_NATIVE_EXECUTION_POLICIES)
    template<std::execution::sender S, class Policy, std::integral Shape, class Fn>
        requires std::is_execution_policy_v<std::remove_cvref_t<Policy>> &&
                 std::copy_constructible<std::decay_t<Fn>> &&
                 std::constructible_from<std::decay_t<Fn>, Fn> &&
                 std::constructible_from<__policy_t<Policy>, Policy>
    [[nodiscard]] auto operator()(S&& s, Policy&& policy, Shape shape, Fn&& fn) const {
        return __sender<Chunked, std::decay_t<S>, __policy_t<Policy>, Shape, std::decay_t<Fn>, Transformed>{
            std::decay_t<S>(std::forward<S>(s)),
            __policy_t<Policy>(std::forward<Policy>(policy)),
            std::move(shape), std::decay_t<Fn>(std::forward<Fn>(fn))};
    }

    template<class Policy, class Shape, class Fn>
        requires std::is_execution_policy_v<std::remove_cvref_t<Policy>> &&
                 std::integral<Shape> &&
                 std::copy_constructible<std::decay_t<Fn>> &&
                 std::constructible_from<std::decay_t<Fn>, Fn> &&
                 std::constructible_from<std::decay_t<Policy>, Policy>
    [[nodiscard]] auto operator()(Policy&& policy, Shape shape, Fn&& fn) const {
        using closure_t = typename __bulk_closure<Chunked, std::decay_t<Fn>, Transformed>
            ::template __with_shape<std::decay_t<Policy>, Shape>;
        return closure_t{
            {}, std::decay_t<Policy>(std::forward<Policy>(policy)),
            std::move(shape),
            std::decay_t<Fn>(std::forward<Fn>(fn))};
    }
#endif

    template<std::execution::sender S, std::integral Shape, class Fn>
        requires std::copy_constructible<std::decay_t<Fn>> &&
                 std::constructible_from<std::decay_t<Fn>, Fn>
    [[deprecated("use forge::bulk, forge::bulk_chunked or forge::bulk_unchunked, or pass an execution policy")]]
    [[nodiscard]] auto operator()(S&& s, Shape shape, Fn&& fn) const
        -> __sender<Chunked, std::decay_t<S>, __serial_policy, Shape, std::decay_t<Fn>, Transformed> {
        return __serial_bulk_t<Chunked, Transformed>{}.template operator()<S, Shape, Fn>(
            std::forward<S>(s), std::move(shape), std::forward<Fn>(fn));
    }

    template<std::integral Shape, class Fn>
        requires std::copy_constructible<std::decay_t<Fn>> &&
                 std::constructible_from<std::decay_t<Fn>, Fn>
    [[deprecated("use forge::bulk, forge::bulk_chunked or forge::bulk_unchunked, or pass an execution policy")]]
    [[nodiscard]] auto operator()(Shape shape, Fn&& fn) const
        -> typename __serial_bulk_t<Chunked, Transformed>::template __closure_t<Shape, Fn> {
        return __serial_bulk_t<Chunked, Transformed>{}.template operator()<Shape, Fn>(
            std::move(shape), std::forward<Fn>(fn));
    }
};

struct bulk_t : __bulk_t<false, true> {};
struct bulk_unchunked_t : __bulk_t<false> {};
struct bulk_chunked_t : __bulk_t<true> {};

} // namespace __forge_bulk

using bulk_t = __forge_bulk::bulk_t;
inline constexpr bulk_t bulk{};
using bulk_chunked_t = __forge_bulk::bulk_chunked_t;
inline constexpr bulk_chunked_t bulk_chunked{};
using bulk_unchunked_t = __forge_bulk::bulk_unchunked_t;
inline constexpr bulk_unchunked_t bulk_unchunked{};

} // namespace std::execution

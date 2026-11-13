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

#include <concepts>
#include <type_traits>
#include <utility>

namespace std::execution {

namespace __forge_adaptor {
template<class T>
concept __class_type = std::is_class_v<T> && std::same_as<T, std::decay_t<T>>;
} // namespace __forge_adaptor

// D may be incomplete here; recognition is deferred until an operand is used.
template<__forge_adaptor::__class_type D>
struct sender_adaptor_closure {};

namespace __forge_adaptor {

// Deduction considers every specialization, including inaccessible extra bases.
// Multiple specializations make deduction ambiguous rather than opting in.
template<class D>
constexpr std::type_identity<D> __closure_base(const sender_adaptor_closure<D>*) noexcept {
    return {};
}

template<class T>
concept __closure =
    __class_type<std::remove_cvref_t<T>> &&
    // Check completeness before derived_from's is_base_of precondition applies.
    requires { sizeof(std::remove_cvref_t<T>); } &&
    std::derived_from<std::remove_cvref_t<T>, sender_adaptor_closure<std::remove_cvref_t<T>>> &&
    requires(std::remove_cvref_t<T>* object) {
        { __forge_adaptor::__closure_base(object) } ->
            std::same_as<std::type_identity<std::remove_cvref_t<T>>>;
    } && (!sender<std::remove_cvref_t<T>>);

template<class First, class Second, class S>
concept __callable = requires(First&& first, Second&& second, S&& sndr) {
    static_cast<Second&&>(second)(
        static_cast<First&&>(first)(static_cast<S&&>(sndr)));
};

template<class First, class Second>
struct __composed : sender_adaptor_closure<__composed<First, Second>> {
    [[no_unique_address]] First __first;
    [[no_unique_address]] Second __second;

    template<class F, class D>
        requires std::is_constructible_v<First, F> && std::is_constructible_v<Second, D>
    constexpr __composed(F&& first, D&& second)
        noexcept(std::is_nothrow_constructible_v<First, F> &&
                 std::is_nothrow_constructible_v<Second, D>)
        : __first(static_cast<F&&>(first)), __second(static_cast<D&&>(second)) {}

    template<class S>
        requires __callable<First&, Second&, S>
    constexpr decltype(auto) operator()(S&& sndr) &
        noexcept(noexcept(__second(__first(static_cast<S&&>(sndr))))) {
        return __second(__first(static_cast<S&&>(sndr)));
    }

    template<class S>
        requires __callable<const First&, const Second&, S>
    constexpr decltype(auto) operator()(S&& sndr) const&
        noexcept(noexcept(__second(__first(static_cast<S&&>(sndr))))) {
        return __second(__first(static_cast<S&&>(sndr)));
    }

    template<class S>
        requires __callable<First&&, Second&&, S>
    constexpr decltype(auto) operator()(S&& sndr) &&
        noexcept(noexcept(std::move(__second)(std::move(__first)(static_cast<S&&>(sndr))))) {
        return std::move(__second)(std::move(__first)(static_cast<S&&>(sndr)));
    }

    template<class S>
        requires __callable<const First&&, const Second&&, S>
    constexpr decltype(auto) operator()(S&& sndr) const&&
        noexcept(noexcept(std::move(__second)(std::move(__first)(static_cast<S&&>(sndr))))) {
        return std::move(__second)(std::move(__first)(static_cast<S&&>(sndr)));
    }

    template<class S> void operator()(S&&) & = delete;
    template<class S> void operator()(S&&) const& = delete;
    template<class S> void operator()(S&&) && = delete;
    template<class S> void operator()(S&&) const&& = delete;
};

template<class Left, class Right>
concept __sender_pipe = sender<Left> && __closure<Right> &&
    requires(Left&& source, Right&& closure) {
        static_cast<Right&&>(closure)(static_cast<Left&&>(source));
    };

template<class First, class Second>
concept __composable = __closure<First> && __closure<Second> &&
    std::is_constructible_v<std::decay_t<First>, First> &&
    std::is_constructible_v<std::decay_t<Second>, Second>;

template<class Left, class Right>
constexpr bool __pipe_nothrow() {
    if constexpr (__sender_pipe<Left, Right>) {
        return noexcept(std::declval<Right>()(std::declval<Left>()));
    } else {
        return std::is_nothrow_constructible_v<std::decay_t<Left>, Left> &&
               std::is_nothrow_constructible_v<std::decay_t<Right>, Right>;
    }
}

} // namespace __forge_adaptor

// A single namespace-scope template avoids per-specialization hidden friends.
// ADL finds it through the public base even for closures in user namespaces.
template<class Left, class Right>
    requires (__forge_adaptor::__sender_pipe<Left, Right> ||
              __forge_adaptor::__composable<Left, Right>)
[[nodiscard]] constexpr decltype(auto) operator|(Left&& left, Right&& right)
    noexcept(__forge_adaptor::__pipe_nothrow<Left, Right>()) {
    if constexpr (__forge_adaptor::__sender_pipe<Left, Right>) {
        return static_cast<Right&&>(right)(static_cast<Left&&>(left));
    } else {
        return __forge_adaptor::__composed<std::decay_t<Left>, std::decay_t<Right>>{
            static_cast<Left&&>(left), static_cast<Right&&>(right)};
    }
}

} // namespace std::execution

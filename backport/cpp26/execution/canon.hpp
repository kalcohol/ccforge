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

#include "affine.hpp"
#include "associate.hpp"
#include "bulk.hpp"
#include "continues_on.hpp"
#include "into_variant.hpp"
#include "let.hpp"
#include "on.hpp"
#include "stopped_as.hpp"
#include "then.hpp"
#include "unstoppable.hpp"
#include "upon.hpp"
#include "write_env.hpp"

#include <concepts>
#include <type_traits>
#include <utility>

namespace std::execution {
namespace __forge_adaptor {

template<class First, class Second>
struct __composed;

// Only library closures participate; callable shape is not an opt-in marker.
template<class T>
struct __is_closure : std::false_type {};

template<class Fn>
struct __is_closure<__forge_then::then_closure<Fn>> : std::true_type {};
template<class Fn>
struct __is_closure<__forge_upon::__upon_error_closure<Fn>> : std::true_type {};
template<class Fn>
struct __is_closure<__forge_upon::__upon_stopped_closure<Fn>> : std::true_type {};
template<class Fn, class Which>
struct __is_closure<__forge_let::__let_closure<Fn, Which>> : std::true_type {};
template<class Err>
struct __is_closure<__forge_stopped::__error_closure<Err>> : std::true_type {};
template<>
struct __is_closure<__forge_stopped::__optional_closure> : std::true_type {};
template<>
struct __is_closure<__forge_stopped::__optional_t> : std::true_type {};
template<>
struct __is_closure<__forge_into_variant::__into_variant_closure> : std::true_type {};
template<>
struct __is_closure<__forge_into_variant::__into_variant_t> : std::true_type {};
template<>
struct __is_closure<__forge_unstoppable::__closure> : std::true_type {};
template<>
struct __is_closure<__forge_unstoppable::__unstoppable_t> : std::true_type {};
template<>
struct __is_closure<affine_t> : std::true_type {};
template<class Env>
struct __is_closure<__forge_write_env::__closure<Env>> : std::true_type {};
template<class Scheduler>
struct __is_closure<__forge_continues_on::__closure<Scheduler>> : std::true_type {};
template<class Scheduler, class Closure>
struct __is_closure<__forge_on_adaptor::__bound_closure<Scheduler, Closure>> : std::true_type {};
template<class Token>
struct __is_closure<__forge_associate::__closure<Token>> : std::true_type {};
template<class First, class Second>
struct __is_closure<__composed<First, Second>> : std::true_type {};

// The outer bulk template parameters cannot be deduced in a specialization.
template<class T, class = void>
struct __is_bulk_closure : std::false_type {};

template<class T>
struct __is_bulk_closure<T, std::void_t<typename T::policy_t, typename T::fn_t,
    decltype(std::declval<T>().__shape_)>> : std::bool_constant<
        std::same_as<T, typename __forge_bulk::__bulk_closure<false, typename T::fn_t>
            ::template __with_shape<typename T::policy_t, decltype(std::declval<T>().__shape_)>> ||
        std::same_as<T, typename __forge_bulk::__bulk_closure<true, typename T::fn_t>
            ::template __with_shape<typename T::policy_t, decltype(std::declval<T>().__shape_)>>> {};

template<class T>
concept __closure =
    (__is_closure<std::remove_cvref_t<T>>::value ||
     __is_bulk_closure<std::remove_cvref_t<T>>::value) &&
    (!sender<std::remove_cvref_t<T>>);

template<class First, class Second, class S>
concept __callable = sender<S> && requires(First&& first, Second&& second, S&& sndr) {
    { static_cast<Second&&>(second)(
        static_cast<First&&>(first)(static_cast<S&&>(sndr))) } -> sender;
};

template<class First, class Second>
struct __composed {
    [[no_unique_address]] First __first;
    [[no_unique_address]] Second __second;

    template<class F, class D>
        requires std::constructible_from<First, F> && std::constructible_from<Second, D>
    constexpr __composed(F&& first, D&& second)
        noexcept(std::is_nothrow_constructible_v<First, F> &&
                 std::is_nothrow_constructible_v<Second, D>)
        : __first(static_cast<F&&>(first)), __second(static_cast<D&&>(second)) {}

    template<sender S>
        requires __callable<First&, Second&, S>
    constexpr decltype(auto) operator()(S&& sndr) &
        noexcept(noexcept(__second(__first(static_cast<S&&>(sndr))))) {
        return __second(__first(static_cast<S&&>(sndr)));
    }

    template<sender S>
        requires __callable<const First&, const Second&, S>
    constexpr decltype(auto) operator()(S&& sndr) const&
        noexcept(noexcept(__second(__first(static_cast<S&&>(sndr))))) {
        return __second(__first(static_cast<S&&>(sndr)));
    }

    template<sender S>
        requires __callable<First&&, Second&&, S>
    constexpr decltype(auto) operator()(S&& sndr) &&
        noexcept(noexcept(std::move(__second)(std::move(__first)(static_cast<S&&>(sndr))))) {
        return std::move(__second)(std::move(__first)(static_cast<S&&>(sndr)));
    }

    template<sender S>
        requires __callable<const First&&, const Second&&, S>
    constexpr decltype(auto) operator()(S&& sndr) const&&
        noexcept(noexcept(std::move(__second)(std::move(__first)(static_cast<S&&>(sndr))))) {
        return std::move(__second)(std::move(__first)(static_cast<S&&>(sndr)));
    }

    template<class S> void operator()(S&&) & = delete;
    template<class S> void operator()(S&&) const& = delete;
    template<class S> void operator()(S&&) && = delete;
    template<class S> void operator()(S&&) const&& = delete;

    template<sender S, class Self>
        requires std::same_as<std::remove_cvref_t<Self>, __composed> &&
                 requires(Self&& self, S&& sndr) {
                     static_cast<Self&&>(self)(static_cast<S&&>(sndr));
                 }
    friend constexpr decltype(auto) operator|(S&& sndr, Self&& self)
        noexcept(noexcept(static_cast<Self&&>(self)(static_cast<S&&>(sndr)))) {
        return static_cast<Self&&>(self)(static_cast<S&&>(sndr));
    }
};

template<__closure First, __closure Second>
    requires std::constructible_from<std::decay_t<First>, First> &&
             std::constructible_from<std::decay_t<Second>, Second>
[[nodiscard]] constexpr auto operator|(First&& first, Second&& second)
    noexcept(std::is_nothrow_constructible_v<std::decay_t<First>, First> &&
             std::is_nothrow_constructible_v<std::decay_t<Second>, Second>) {
    return __composed<std::decay_t<First>, std::decay_t<Second>>{
        static_cast<First&&>(first), static_cast<Second&&>(second)};
}

} // namespace __forge_adaptor

// Using declarations make this one constrained operator visible to ADL.
using __forge_adaptor::operator|;
namespace __forge_then { using __forge_adaptor::operator|; }
namespace __forge_upon { using __forge_adaptor::operator|; }
namespace __forge_let { using __forge_adaptor::operator|; }
namespace __forge_stopped { using __forge_adaptor::operator|; }
namespace __forge_into_variant { using __forge_adaptor::operator|; }
namespace __forge_unstoppable { using __forge_adaptor::operator|; }
namespace __forge_write_env { using __forge_adaptor::operator|; }
namespace __forge_continues_on { using __forge_adaptor::operator|; }
namespace __forge_on_adaptor { using __forge_adaptor::operator|; }
namespace __forge_bulk { using __forge_adaptor::operator|; }
namespace __forge_associate { using __forge_adaptor::operator|; }

} // namespace std::execution

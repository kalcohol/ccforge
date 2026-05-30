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

#include <type_traits>

namespace std {

#if defined(FORGE_FORCE_CONSTANT_WRAPPER_BACKPORT) || \
    (!defined(FORGE_HAS_NATIVE_CONSTANT_WRAPPER) && !defined(__cpp_lib_constant_wrapper))

template <auto V>
struct constant_wrapper {
    static constexpr auto value = V;
    using type = constant_wrapper;
    using value_type = decltype(V);

    constexpr operator value_type() const noexcept { return value; }

    template <class... Args>
    constexpr decltype(auto) operator()(Args&&... args) const
        noexcept(noexcept(value(static_cast<Args&&>(args)...)))
        requires requires { value(static_cast<Args&&>(args)...); }
    {
        return value(static_cast<Args&&>(args)...);
    }
};

template <auto V>
inline constexpr auto cw = constant_wrapper<V>{};

template <class T>
struct __forge_is_constant_wrapper : false_type {};

template <auto V>
struct __forge_is_constant_wrapper<constant_wrapper<V>> : true_type {};

template <class T>
concept __forge_constant_wrapper =
    __forge_is_constant_wrapper<remove_cvref_t<T>>::value;

template <auto V>
constexpr auto operator+(constant_wrapper<V>) noexcept -> constant_wrapper<(+V)> { return {}; }
template <auto V>
constexpr auto operator-(constant_wrapper<V>) noexcept -> constant_wrapper<(-V)> { return {}; }
template <auto V>
constexpr auto operator~(constant_wrapper<V>) noexcept -> constant_wrapper<(~V)> { return {}; }
template <auto V>
constexpr auto operator!(constant_wrapper<V>) noexcept -> constant_wrapper<(!V)> { return {}; }

template <auto L, auto R>
constexpr auto operator+(constant_wrapper<L>, constant_wrapper<R>) noexcept
    -> constant_wrapper<(L + R)> { return {}; }
template <auto L, auto R>
constexpr auto operator-(constant_wrapper<L>, constant_wrapper<R>) noexcept
    -> constant_wrapper<(L - R)> { return {}; }
template <auto L, auto R>
constexpr auto operator*(constant_wrapper<L>, constant_wrapper<R>) noexcept
    -> constant_wrapper<(L * R)> { return {}; }
template <auto L, auto R>
constexpr auto operator/(constant_wrapper<L>, constant_wrapper<R>) noexcept
    -> constant_wrapper<(L / R)> { return {}; }
template <auto L, auto R>
constexpr auto operator%(constant_wrapper<L>, constant_wrapper<R>) noexcept
    -> constant_wrapper<(L % R)> { return {}; }

template <auto L, auto R>
constexpr auto operator<<(constant_wrapper<L>, constant_wrapper<R>) noexcept
    -> constant_wrapper<(L << R)> { return {}; }
template <auto L, auto R>
constexpr auto operator>>(constant_wrapper<L>, constant_wrapper<R>) noexcept
    -> constant_wrapper<(L >> R)> { return {}; }
template <auto L, auto R>
constexpr auto operator&(constant_wrapper<L>, constant_wrapper<R>) noexcept
    -> constant_wrapper<(L & R)> { return {}; }
template <auto L, auto R>
constexpr auto operator|(constant_wrapper<L>, constant_wrapper<R>) noexcept
    -> constant_wrapper<(L | R)> { return {}; }
template <auto L, auto R>
constexpr auto operator^(constant_wrapper<L>, constant_wrapper<R>) noexcept
    -> constant_wrapper<(L ^ R)> { return {}; }

template <auto L, auto R>
constexpr auto operator<(constant_wrapper<L>, constant_wrapper<R>) noexcept
    -> constant_wrapper<(L < R)> { return {}; }
template <auto L, auto R>
constexpr auto operator<=(constant_wrapper<L>, constant_wrapper<R>) noexcept
    -> constant_wrapper<(L <= R)> { return {}; }
template <auto L, auto R>
constexpr auto operator==(constant_wrapper<L>, constant_wrapper<R>) noexcept
    -> constant_wrapper<(L == R)> { return {}; }
template <auto L, auto R>
constexpr auto operator!=(constant_wrapper<L>, constant_wrapper<R>) noexcept
    -> constant_wrapper<(L != R)> { return {}; }
template <auto L, auto R>
constexpr auto operator>(constant_wrapper<L>, constant_wrapper<R>) noexcept
    -> constant_wrapper<(L > R)> { return {}; }
template <auto L, auto R>
constexpr auto operator>=(constant_wrapper<L>, constant_wrapper<R>) noexcept
    -> constant_wrapper<(L >= R)> { return {}; }

#  if !defined(__cpp_lib_constant_wrapper)
#    define __cpp_lib_constant_wrapper 202603L
#  endif

#endif // forced or no native constant_wrapper

} // namespace std

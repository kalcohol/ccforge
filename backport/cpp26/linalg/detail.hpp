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

#if __has_include(<version>)
#include <version>
#endif

#include <complex>
#include <cstddef>
#if __has_include(<mdspan>)
#include <mdspan>
#endif
#include <type_traits>
#include <utility>
#include <functional>

// Optional SIMD acceleration using CC Forge simd backport
#if __has_include(<simd>)
#include <simd>
#endif

// Macro: 1 if CC Forge simd backport was loaded
#ifdef FORGE_BACKPORT_SIMD_HPP_INCLUDED
#  define __LINALG_HAS_SIMD 1
#else
#  define __LINALG_HAS_SIMD 0
#endif

#if defined(__cpp_lib_mdspan)

namespace std::linalg {

namespace __detail {

template<class T>
struct __is_complex : std::false_type {};

template<class T>
struct __is_complex<std::complex<T>> : std::true_type {};

template<class T>
inline constexpr bool __is_complex_v = __is_complex<std::remove_cvref_t<T>>::value;

template<class T>
T conj(const T&) = delete;

template<class T>
concept __has_adl_conj = requires {
    conj(std::declval<T>());
};

template<class T>
constexpr auto __conj_if_needed(T&& value) {
    if constexpr (
        !std::is_arithmetic_v<std::remove_cvref_t<T>> &&
        requires { conj(std::forward<T>(value)); }) {
        return conj(std::forward<T>(value));
    } else {
        return std::forward<T>(value);
    }
}

template<class Extents1, class Extents2>
consteval bool __compatible_static_extents() {
    if constexpr (Extents1::rank() != Extents2::rank()) {
        return false;
    } else {
        for (std::size_t r = 0; r < Extents1::rank(); ++r) {
            const auto first = Extents1::static_extent(r);
            const auto second = Extents2::static_extent(r);
            if (first != std::dynamic_extent &&
                second != std::dynamic_extent &&
                first != second) {
                return false;
            }
        }
        return true;
    }
}

template<class Extents1, class Extents2>
inline constexpr bool __compatible_static_extents_v =
    __compatible_static_extents<Extents1, Extents2>();

template<class T>
constexpr auto __real_if_needed(const T& value) {
    if constexpr (requires { value.real(); value.imag(); }) {
        return value.real();
    } else {
        return value;
    }
}

template<class Accessor>
using __accessor_value_t =
    std::remove_cv_t<typename Accessor::element_type>;

template<class T>
constexpr auto __abs_if_needed(const T& value) {
    if constexpr (std::is_unsigned_v<std::remove_cvref_t<T>>) {
        return value;
    } else {
        using std::abs;
        return abs(value);
    }
}

template<class T>
constexpr auto __abs_sum_term(const T& value) {
    if constexpr (__is_complex_v<T>) {
        return __abs_if_needed(value.real()) + __abs_if_needed(value.imag());
    } else {
        return __abs_if_needed(value);
    }
}

template<class Real>
constexpr void __update_scaled_sum_of_squares(
    Real magnitude, Real& scale, Real& scaled_sum) {
    if (magnitude == Real{}) {
        return;
    }
    if (scale < magnitude) {
        const Real ratio = scale / magnitude;
        scaled_sum = Real{1} + scaled_sum * ratio * ratio;
        scale = magnitude;
    } else if (scale == magnitude) {
        scaled_sum += Real{1};
    } else {
        const Real ratio = magnitude / scale;
        scaled_sum += ratio * ratio;
    }
}

template<class T>
inline constexpr bool __is_simd_accelerable_v =
    std::is_arithmetic_v<T> && !std::is_same_v<T, bool> &&
    !std::is_same_v<T, long double> &&
    !std::is_same_v<T, char> && !std::is_same_v<T, signed char> &&
    !std::is_same_v<T, unsigned char>;

template<class Layout>
inline constexpr bool __is_contiguous_layout_v =
    std::is_same_v<Layout, std::layout_right> ||
    std::is_same_v<Layout, std::layout_left>;

template<class Accessor>
struct __is_default_accessor : std::false_type {};
template<class T>
struct __is_default_accessor<std::default_accessor<T>> : std::true_type {};

template<class T, class Layout, class Accessor>
inline constexpr bool __can_simd_v =
    (__LINALG_HAS_SIMD == 1) &&
    __is_simd_accelerable_v<std::remove_const_t<T>> &&
    __is_contiguous_layout_v<Layout> &&
    __is_default_accessor<Accessor>::value;

} // namespace __detail

} // namespace std::linalg

#endif // defined(__cpp_lib_mdspan)

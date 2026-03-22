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

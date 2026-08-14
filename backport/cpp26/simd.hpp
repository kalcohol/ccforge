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

#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <cassert>
#include <cmath>
#include <compare>
#include <complex>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#if defined(__has_include)
#  if __has_include(<stdfloat>)
#    include <stdfloat>
#  endif
#endif
#include <tuple>
#include <type_traits>
#include <utility>

// This macro is a test marker for backport injection, not an include guard.
#ifndef FORGE_BACKPORT_SIMD_HPP_INCLUDED
#define FORGE_BACKPORT_SIMD_HPP_INCLUDED 1
#endif

// Feature-test macro: CC Forge simd backport covers the full [simd.syn] API surface.
#if !defined(__cpp_lib_simd)
#define __cpp_lib_simd 202606L
#endif
#if !defined(__cpp_lib_simd_bitops)
#define __cpp_lib_simd_bitops 202607L
#endif
#if !defined(__cpp_lib_simd_complex)
#define __cpp_lib_simd_complex 202502L
#endif
#if !defined(__cpp_lib_simd_permutations)
#define __cpp_lib_simd_permutations 202506L
#endif

namespace std {

#if __cplusplus < 202002L
struct default_sentinel_t {
    explicit constexpr default_sentinel_t() noexcept = default;
};

inline constexpr default_sentinel_t default_sentinel{};
#endif

namespace simd {

#include "simd/base.hpp"
#include "simd/detail/vector_ops.hpp"
#include "simd/types.hpp"
static_assert(true); // Keep implementation fragments lexically independent.
#include "simd/iterator.hpp"
#include "simd/memory.hpp"
#include "simd/operations.hpp"
#include "simd/reductions.hpp"

} // namespace simd

// [simd.syn] makes SIMD overloads visible in the corresponding std overload sets.
using simd::min;
using simd::max;
using simd::minmax;
using simd::clamp;

using simd::acos;
using simd::asin;
using simd::atan;
using simd::atan2;
using simd::cos;
using simd::sin;
using simd::tan;
using simd::acosh;
using simd::asinh;
using simd::atanh;
using simd::cosh;
using simd::sinh;
using simd::tanh;
using simd::exp;
using simd::exp2;
using simd::expm1;
using simd::frexp;
using simd::ilogb;
using simd::ldexp;
using simd::log;
using simd::log10;
using simd::log1p;
using simd::log2;
using simd::logb;
using simd::modf;
using simd::scalbn;
using simd::scalbln;
using simd::cbrt;
using simd::abs;
using simd::fabs;
using simd::hypot;
using simd::pow;
using simd::sqrt;
using simd::erf;
using simd::erfc;
using simd::lgamma;
using simd::tgamma;
using simd::ceil;
using simd::floor;
using simd::nearbyint;
using simd::rint;
using simd::lrint;
using simd::llrint;
using simd::round;
using simd::lround;
using simd::llround;
using simd::trunc;
using simd::fmod;
using simd::remainder;
using simd::remquo;
using simd::copysign;
using simd::nextafter;
using simd::fdim;
using simd::fmax;
using simd::fmin;
using simd::fma;
using simd::lerp;
using simd::fpclassify;
using simd::isfinite;
using simd::isinf;
using simd::isnan;
using simd::isnormal;
using simd::signbit;
using simd::isgreater;
using simd::isgreaterequal;
using simd::isless;
using simd::islessequal;
using simd::islessgreater;
using simd::isunordered;
using simd::assoc_laguerre;
using simd::assoc_legendre;
using simd::beta;
using simd::comp_ellint_1;
using simd::comp_ellint_2;
using simd::comp_ellint_3;
using simd::cyl_bessel_i;
using simd::cyl_bessel_j;
using simd::cyl_bessel_k;
using simd::cyl_neumann;
using simd::ellint_1;
using simd::ellint_2;
using simd::ellint_3;
using simd::expint;
using simd::hermite;
using simd::laguerre;
using simd::legendre;
using simd::riemann_zeta;
using simd::sph_bessel;
using simd::sph_legendre;
using simd::sph_neumann;

using simd::byteswap;
using simd::bit_ceil;
using simd::bit_floor;
using simd::bit_reverse;
using simd::has_single_bit;
using simd::shl;
using simd::shr;
using simd::rotl;
using simd::rotr;
using simd::bit_repeat;
using simd::bit_width;
using simd::countl_zero;
using simd::countl_one;
using simd::countr_zero;
using simd::countr_one;
using simd::popcount;
using simd::bit_compress;
using simd::bit_expand;

using simd::real;
using simd::imag;
using simd::arg;
using simd::norm;
using simd::conj;
using simd::proj;
using simd::polar;

} // namespace std

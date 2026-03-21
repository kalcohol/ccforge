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

namespace std {

#if __cplusplus < 202002L
struct default_sentinel_t {
    explicit constexpr default_sentinel_t() noexcept = default;
};

inline constexpr default_sentinel_t default_sentinel{};
#endif

namespace simd {

#include "simd/base.hpp"
#include "simd/types.hpp"
#include "simd/iterator.hpp"
#include "simd/memory.hpp"
#include "simd/operations.hpp"
#include "simd/reductions.hpp"

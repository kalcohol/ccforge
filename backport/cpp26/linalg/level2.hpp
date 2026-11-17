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

#include "level1.hpp"
#if defined(__cpp_lib_mdspan)

namespace std::linalg {

// ──────────────────────────────────────────────────────────────────────────
// BLAS Level 2 — [linalg.algs.blas2]
// ──────────────────────────────────────────────────────────────────────────

// matrix_vector_product — y = A*x  [linalg.algs.blas2.gemv]
template<class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor>
    requires (__detail::__writable_output_v<YExtents, YLayout, YAccessor> &&
              AExtents::rank() == 2 &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              __detail::__compatible_static_extent_v<AExtents, 1, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 0, YExtents, 0>)
void matrix_vector_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y)
{
    using idx_t = typename AExtents::index_type;
    using ElemT = std::remove_const_t<typename AAccessor::element_type>;
    using XElemT = std::remove_const_t<typename XAccessor::element_type>;
    using YElemT = std::remove_const_t<typename YAccessor::element_type>;
#if __LINALG_HAS_SIMD
    if constexpr (std::is_same_v<ElemT, XElemT> &&
                  std::is_same_v<ElemT, YElemT> &&
                  __detail::__can_simd_v<ElemT, XLayout, XAccessor> &&
                  __detail::__can_simd_v<ElemT, ALayout, AAccessor> &&
                  std::is_same_v<ALayout, std::layout_right>) {
        using abi_t  = typename std::simd::vec<ElemT>::abi_type;
        using simd_t = std::simd::basic_vec<ElemT, abi_t>;
        static constexpr auto kN = std::simd::basic_vec<ElemT, abi_t>::size();
        const auto rows = A.extent(0), cols = A.extent(1);
        const ElemT* px = x.data_handle();
        for (idx_t i = 0; i < rows; ++i) {
            ElemT sum = ElemT{};
            idx_t j = 0;
            // SIMD inner dot product
            for (; j + static_cast<idx_t>(kN) <= cols; j += kN) {
                simd_t va{std::span<const ElemT, kN>{&A[i, 0] + j, kN}};
                simd_t vx{std::span<const ElemT, kN>{px + j, kN}};
                sum += std::simd::reduce(va * vx);
            }
            for (; j < cols; ++j) sum += A[i, j] * px[j];
            y[i] = sum;
        }
        return;
    }
#endif
    for (idx_t i = 0; i < A.extent(0); ++i) {
        __detail::__accessor_value_t<YAccessor> sum{};
        for (idx_t j = 0; j < A.extent(1); ++j)
            sum += A[i, j] * x[j];
        y[i] = sum;
    }
}

// matrix_vector_product (update) — z = y + A*x
template<class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class ZExtents, class ZLayout, class ZAccessor>
    requires (__detail::__writable_output_v<ZExtents, ZLayout, ZAccessor> &&
              AExtents::rank() == 2 &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              ZExtents::rank() == 1 &&
              __detail::__compatible_static_extent_v<AExtents, 1, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 0, YExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 0, ZExtents, 0> &&
              __detail::__compatible_static_extent_v<YExtents, 0, ZExtents, 0>)
void matrix_vector_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename ZAccessor::element_type, ZExtents, ZLayout, ZAccessor> z)
{
    using idx_t = typename AExtents::index_type;
    for (idx_t i = 0; i < A.extent(0); ++i) {
        __detail::__accessor_value_t<ZAccessor> sum{};
        for (idx_t j = 0; j < A.extent(1); ++j)
            sum += A[i, j] * x[j];
        z[i] = y[i] + sum;
    }
}

// triangular_matrix_vector_product — in-place  [linalg.algs.blas2.trmv]
template<class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor>
    requires (__detail::__writable_output_v<XExtents, XLayout, XAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__square_static_extents_v<AExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              XExtents::rank() == 1 &&
              __detail::__compatible_static_extent_v<AExtents, 0, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, XExtents, 0>)
void triangular_matrix_vector_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle, DiagonalStorage,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x)
{
    using idx_t = typename AExtents::index_type;
    const idx_t n = A.extent(0);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    constexpr bool is_unit  = std::is_same_v<DiagonalStorage, implicit_unit_diagonal_t>;
    auto compute_row = [&](idx_t i) {
        __detail::__accessor_value_t<XAccessor> sum{};
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j) {
                if constexpr (is_unit) { if (j == i) { sum += x[j]; continue; } }
                sum += A[i, j] * x[j];
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                if constexpr (is_unit) { if (j == i) { sum += x[j]; continue; } }
                sum += A[i, j] * x[j];
            }
        }
        x[i] = sum;
    };
    if constexpr (is_upper) {
        for (idx_t i = 0; i < n; ++i) compute_row(i);
    } else {
        for (idx_t i = n; i > 0; --i) compute_row(i - 1);
    }
}

// triangular_matrix_vector_product (out-of-place) — y = A*x
template<class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor>
    requires (__detail::__writable_output_v<YExtents, YLayout, YAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__square_static_extents_v<AExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              __detail::__compatible_static_extent_v<AExtents, 0, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 0, YExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, YExtents, 0> &&
              __detail::__compatible_static_extent_v<XExtents, 0, YExtents, 0>)
void triangular_matrix_vector_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t, DiagonalStorage d,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y)
{
    using idx_t = typename AExtents::index_type;
    const idx_t n = A.extent(0);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    constexpr bool is_unit  = std::is_same_v<DiagonalStorage, implicit_unit_diagonal_t>;
    for (idx_t i = 0; i < n; ++i) {
        __detail::__accessor_value_t<YAccessor> sum{};
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j) {
                if constexpr (is_unit) { if (j == i) { sum += x[j]; continue; } }
                sum += A[i, j] * x[j];
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                if constexpr (is_unit) { if (j == i) { sum += x[j]; continue; } }
                sum += A[i, j] * x[j];
            }
        }
        y[i] = sum;
    }
}

// symmetric_matrix_vector_product — y = A*x (symmetric A)  [linalg.algs.blas2.symv]
template<class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor>
    requires (__detail::__writable_output_v<YExtents, YLayout, YAccessor> &&
              __detail::__square_static_extents_v<AExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              __detail::__compatible_static_extent_v<AExtents, 0, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 0, YExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, YExtents, 0> &&
              __detail::__compatible_static_extent_v<XExtents, 0, YExtents, 0>)
void symmetric_matrix_vector_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y)
{
    using idx_t = typename AExtents::index_type;
    const idx_t n = A.extent(0);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        __detail::__accessor_value_t<YAccessor> sum{};
        for (idx_t j = 0; j < n; ++j) {
            if constexpr (is_upper) {
                if (i <= j) sum += A[i, j] * x[j];
                else sum += A[j, i] * x[j];
            } else {
                if (i >= j) sum += A[i, j] * x[j];
                else sum += A[j, i] * x[j];
            }
        }
        y[i] = sum;
    }
}

// symmetric_matrix_vector_product (update) — z = y + A*x
template<class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class ZExtents, class ZLayout, class ZAccessor>
    requires (__detail::__writable_output_v<ZExtents, ZLayout, ZAccessor> &&
              __detail::__square_static_extents_v<AExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              ZExtents::rank() == 1 &&
              __detail::__compatible_static_extent_v<AExtents, 0, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 0, YExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, YExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 0, ZExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, ZExtents, 0> &&
              __detail::__compatible_static_extent_v<XExtents, 0, YExtents, 0> &&
              __detail::__compatible_static_extent_v<XExtents, 0, ZExtents, 0> &&
              __detail::__compatible_static_extent_v<YExtents, 0, ZExtents, 0>)
void symmetric_matrix_vector_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename ZAccessor::element_type, ZExtents, ZLayout, ZAccessor> z)
{
    using idx_t = typename AExtents::index_type;
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    const idx_t n = A.extent(0);
    for (idx_t i = 0; i < n; ++i) {
        __detail::__accessor_value_t<ZAccessor> sum{};
        for (idx_t j = 0; j < n; ++j) {
            if (i == j) {
                sum += A[i, i] * x[j];
            } else if constexpr (is_upper) {
                sum += (i < j ? A[i, j] : A[j, i]) * x[j];
            } else {
                sum += (i < j ? A[j, i] : A[i, j]) * x[j];
            }
        }
        z[i] = y[i] + sum;
    }
}

// hermitian_matrix_vector_product — y = A*x (hermitian A)  [linalg.algs.blas2.hemv]
template<class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor>
    requires (__detail::__writable_output_v<YExtents, YLayout, YAccessor> &&
              __detail::__square_static_extents_v<AExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              __detail::__compatible_static_extent_v<AExtents, 0, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 0, YExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, YExtents, 0> &&
              __detail::__compatible_static_extent_v<XExtents, 0, YExtents, 0>)
void hermitian_matrix_vector_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y)
{
    using idx_t = typename AExtents::index_type;
    const idx_t n = A.extent(0);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        __detail::__accessor_value_t<YAccessor> sum{};
        for (idx_t j = 0; j < n; ++j) {
            if (i == j) {
                sum += __detail::__real_if_needed(__detail::__accessor_value_t<AAccessor>(A[i, i])) * x[j];
            } else if ((is_upper && i < j) || (!is_upper && i > j)) {
                sum += A[i, j] * x[j];
            } else {
                sum += __detail::__conj_if_needed(__detail::__accessor_value_t<AAccessor>(A[j, i])) * x[j];
            }
        }
        y[i] = sum;
    }
}

// hermitian_matrix_vector_product (update) — z = y + A*x
template<class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class ZExtents, class ZLayout, class ZAccessor>
    requires (__detail::__writable_output_v<ZExtents, ZLayout, ZAccessor> &&
              __detail::__square_static_extents_v<AExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              ZExtents::rank() == 1 &&
              __detail::__compatible_static_extent_v<AExtents, 0, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 0, YExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, YExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 0, ZExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, ZExtents, 0> &&
              __detail::__compatible_static_extent_v<XExtents, 0, YExtents, 0> &&
              __detail::__compatible_static_extent_v<XExtents, 0, ZExtents, 0> &&
              __detail::__compatible_static_extent_v<YExtents, 0, ZExtents, 0>)
void hermitian_matrix_vector_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename ZAccessor::element_type, ZExtents, ZLayout, ZAccessor> z)
{
    using idx_t = typename AExtents::index_type;
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    const idx_t n = A.extent(0);
    for (idx_t i = 0; i < n; ++i) {
        __detail::__accessor_value_t<ZAccessor> sum{};
        for (idx_t j = 0; j < n; ++j) {
            if (i == j) {
                sum += __detail::__real_if_needed(__detail::__accessor_value_t<AAccessor>(A[i, i])) * x[j];
            } else if constexpr (is_upper) {
                sum += (i < j
                    ? A[i, j]
                    : __detail::__conj_if_needed(__detail::__accessor_value_t<AAccessor>(A[j, i]))) * x[j];
            } else {
                sum += (i < j
                    ? __detail::__conj_if_needed(__detail::__accessor_value_t<AAccessor>(A[j, i]))
                    : A[i, j]) * x[j];
            }
        }
        z[i] = y[i] + sum;
    }
}

// matrix_rank_1_update — A = x * y^T  [linalg.algs.blas2.rank1]
template<class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class AExtents, class ALayout, class AAccessor>
    requires (__detail::__writable_output_v<AExtents, ALayout, AAccessor> &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              AExtents::rank() == 2 &&
              __detail::__compatible_static_extent_v<XExtents, 0, AExtents, 0> &&
              __detail::__compatible_static_extent_v<YExtents, 0, AExtents, 1>)
void matrix_rank_1_update(
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A)
{
    using idx_t = typename AExtents::index_type;
    for (idx_t i = 0; i < A.extent(0); ++i)
        for (idx_t j = 0; j < A.extent(1); ++j)
            A[i, j] = x[i] * y[j];
}

// matrix_rank_1_update — A = E + x * y^T
template<class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class EExtents, class ELayout, class EAccessor,
         class AExtents, class ALayout, class AAccessor>
    requires (__detail::__writable_output_v<AExtents, ALayout, AAccessor> &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              EExtents::rank() == 2 &&
              AExtents::rank() == 2 &&
              __detail::__compatible_static_extent_v<XExtents, 0, EExtents, 0> &&
              __detail::__compatible_static_extent_v<XExtents, 0, AExtents, 0> &&
              __detail::__compatible_static_extent_v<YExtents, 0, EExtents, 1> &&
              __detail::__compatible_static_extent_v<YExtents, 0, AExtents, 1> &&
              __detail::__compatible_static_extents_v<EExtents, AExtents>)
void matrix_rank_1_update(
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A)
{
    using idx_t = typename AExtents::index_type;
    for (idx_t i = 0; i < A.extent(0); ++i)
        for (idx_t j = 0; j < A.extent(1); ++j)
            A[i, j] = E[i, j] + x[i] * y[j];
}

// matrix_rank_1_update_c — A = x * conj(y^T)  [linalg.algs.blas2.rank1]
template<class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class AExtents, class ALayout, class AAccessor>
    requires (__detail::__writable_output_v<AExtents, ALayout, AAccessor> &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              AExtents::rank() == 2 &&
              __detail::__compatible_static_extent_v<XExtents, 0, AExtents, 0> &&
              __detail::__compatible_static_extent_v<YExtents, 0, AExtents, 1>)
void matrix_rank_1_update_c(
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A)
{
    using idx_t = typename AExtents::index_type;
    for (idx_t i = 0; i < A.extent(0); ++i)
        for (idx_t j = 0; j < A.extent(1); ++j)
            A[i, j] = x[i] * __detail::__conj_if_needed(__detail::__accessor_value_t<YAccessor>(y[j]));
}

// matrix_rank_1_update_c — A = E + x * conj(y^T)
template<class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class EExtents, class ELayout, class EAccessor,
         class AExtents, class ALayout, class AAccessor>
    requires (__detail::__writable_output_v<AExtents, ALayout, AAccessor> &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              EExtents::rank() == 2 &&
              AExtents::rank() == 2 &&
              __detail::__compatible_static_extent_v<XExtents, 0, EExtents, 0> &&
              __detail::__compatible_static_extent_v<XExtents, 0, AExtents, 0> &&
              __detail::__compatible_static_extent_v<YExtents, 0, EExtents, 1> &&
              __detail::__compatible_static_extent_v<YExtents, 0, AExtents, 1> &&
              __detail::__compatible_static_extents_v<EExtents, AExtents>)
void matrix_rank_1_update_c(
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A)
{
    using idx_t = typename AExtents::index_type;
    for (idx_t i = 0; i < A.extent(0); ++i)
        for (idx_t j = 0; j < A.extent(1); ++j)
            A[i, j] = E[i, j] + x[i] * __detail::__conj_if_needed(__detail::__accessor_value_t<YAccessor>(y[j]));
}

// symmetric_matrix_rank_1_update — A = alpha * x * x^T  [linalg.algs.blas2.symherrank1]
template<class ScalingFactor,
         class XExtents, class XLayout, class XAccessor,
         class AExtents, class ALayout, class AAccessor,
         class Triangle>
    requires (__detail::__scalar<ScalingFactor> &&
              __detail::__possibly_packed_output_v<AExtents, ALayout, AAccessor> &&
              __detail::__square_matrix_vector_static_extents_v<AExtents, XExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>)
void symmetric_matrix_rank_1_update(
    ScalingFactor alpha,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle)
{
    using idx_t = typename AExtents::index_type;
    const idx_t n = A.extent(0);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j)
                A[i, j] = alpha * x[i] * x[j];
        } else {
            for (idx_t j = 0; j <= i; ++j)
                A[i, j] = alpha * x[i] * x[j];
        }
    }
}

// symmetric_matrix_rank_1_update — A = E + alpha * x * x^T
template<class ScalingFactor,
         class XExtents, class XLayout, class XAccessor,
         class EExtents, class ELayout, class EAccessor,
         class AExtents, class ALayout, class AAccessor,
         class Triangle>
    requires (__detail::__scalar<ScalingFactor> &&
              __detail::__possibly_packed_output_v<AExtents, ALayout, AAccessor> &&
              __detail::__square_matrix_vector_static_extents_v<EExtents, XExtents> &&
              __detail::__square_matrix_vector_static_extents_v<AExtents, XExtents> &&
              __detail::__compatible_static_extents_v<EExtents, AExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>)
void symmetric_matrix_rank_1_update(
    ScalingFactor alpha,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle)
{
    using idx_t = typename AExtents::index_type;
    const idx_t n = A.extent(0);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j)
                A[i, j] = E[i, j] + alpha * x[i] * x[j];
        } else {
            for (idx_t j = 0; j <= i; ++j)
                A[i, j] = E[i, j] + alpha * x[i] * x[j];
        }
    }
}

template<class ScalingFactor, class Triangle,
         class XExtents, class XLayout, class XAccessor,
         class AExtents, class ALayout, class AAccessor>
    requires (__detail::__scalar<ScalingFactor> &&
              __detail::__possibly_packed_output_v<AExtents, ALayout, AAccessor> &&
              __detail::__square_matrix_vector_static_extents_v<AExtents, XExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>)
void symmetric_matrix_rank_1_update(
    ScalingFactor alpha, Triangle t,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A)
{
    symmetric_matrix_rank_1_update(alpha, x, A, t);
}

// symmetric_matrix_rank_2_update — A = x*y^T + y*x^T  [linalg.algs.blas2.rank2]
template<class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class AExtents, class ALayout, class AAccessor,
         class Triangle>
    requires (__detail::__possibly_packed_output_v<AExtents, ALayout, AAccessor> &&
              __detail::__square_matrix_vectors_static_extents_v<AExtents, XExtents, YExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>)
void symmetric_matrix_rank_2_update(
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle)
{
    using idx_t = typename AExtents::index_type;
    const idx_t n = A.extent(0);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j)
                A[i, j] = x[i] * y[j] + y[i] * x[j];
        } else {
            for (idx_t j = 0; j <= i; ++j)
                A[i, j] = x[i] * y[j] + y[i] * x[j];
        }
    }
}

// symmetric_matrix_rank_2_update — A = E + x*y^T + y*x^T
template<class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class EExtents, class ELayout, class EAccessor,
         class AExtents, class ALayout, class AAccessor,
         class Triangle>
    requires (__detail::__possibly_packed_output_v<AExtents, ALayout, AAccessor> &&
              __detail::__square_matrix_vectors_static_extents_v<EExtents, XExtents, YExtents> &&
              __detail::__square_matrix_vectors_static_extents_v<AExtents, XExtents, YExtents> &&
              __detail::__compatible_static_extents_v<EExtents, AExtents> &&
              __detail::__packed_triangle_matches_v<ELayout, Triangle> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>)
void symmetric_matrix_rank_2_update(
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle)
{
    using idx_t = typename AExtents::index_type;
    const idx_t n = A.extent(0);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j)
                A[i, j] = E[i, j] + x[i] * y[j] + y[i] * x[j];
        } else {
            for (idx_t j = 0; j <= i; ++j)
                A[i, j] = E[i, j] + x[i] * y[j] + y[i] * x[j];
        }
    }
}

template<class ScalingFactor, class Triangle,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class AExtents, class ALayout, class AAccessor>
    requires (__detail::__scalar<ScalingFactor> &&
              __detail::__possibly_packed_output_v<AExtents, ALayout, AAccessor> &&
              __detail::__square_matrix_vectors_static_extents_v<AExtents, XExtents, YExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>)
void symmetric_matrix_rank_2_update(
    ScalingFactor alpha, Triangle,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A)
{
    using idx_t = typename AExtents::index_type;
    const idx_t n = A.extent(0);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j)
                A[i, j] = alpha * (x[i] * y[j] + y[i] * x[j]);
        } else {
            for (idx_t j = 0; j <= i; ++j)
                A[i, j] = alpha * (x[i] * y[j] + y[i] * x[j]);
        }
    }
}

// hermitian_matrix_rank_1_update — A = alpha * x * x^H  [linalg.algs.blas2.symherrank1]
template<class ScalingFactor,
         class XExtents, class XLayout, class XAccessor,
         class AExtents, class ALayout, class AAccessor,
         class Triangle>
    requires (__detail::__scalar<ScalingFactor> &&
              __detail::__possibly_packed_output_v<AExtents, ALayout, AAccessor> &&
              __detail::__square_matrix_vector_static_extents_v<AExtents, XExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>)
void hermitian_matrix_rank_1_update(
    ScalingFactor alpha,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle)
{
    using idx_t = typename AExtents::index_type;
    const idx_t n = A.extent(0);
    const auto scaling = __detail::__real_if_needed(alpha);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j) {
                auto value = scaling * x[i] * __detail::__conj_if_needed(__detail::__accessor_value_t<XAccessor>(x[j]));
                if (i == j) A[i, j] = __detail::__real_if_needed(value);
                else A[i, j] = value;
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                auto value = scaling * x[i] * __detail::__conj_if_needed(__detail::__accessor_value_t<XAccessor>(x[j]));
                if (i == j) A[i, j] = __detail::__real_if_needed(value);
                else A[i, j] = value;
            }
        }
    }
}

// hermitian_matrix_rank_1_update — A = E + alpha * x * x^H
template<class ScalingFactor,
         class XExtents, class XLayout, class XAccessor,
         class EExtents, class ELayout, class EAccessor,
         class AExtents, class ALayout, class AAccessor,
         class Triangle>
    requires (__detail::__scalar<ScalingFactor> &&
              __detail::__possibly_packed_output_v<AExtents, ALayout, AAccessor> &&
              __detail::__square_matrix_vector_static_extents_v<EExtents, XExtents> &&
              __detail::__square_matrix_vector_static_extents_v<AExtents, XExtents> &&
              __detail::__compatible_static_extents_v<EExtents, AExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>)
void hermitian_matrix_rank_1_update(
    ScalingFactor alpha,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle)
{
    using idx_t = typename AExtents::index_type;
    const idx_t n = A.extent(0);
    const auto scaling = __detail::__real_if_needed(alpha);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j) {
                if (i == j) {
                    auto value = __detail::__real_if_needed(__detail::__accessor_value_t<EAccessor>(E[i, j])) + scaling * x[i] * __detail::__conj_if_needed(__detail::__accessor_value_t<XAccessor>(x[j]));
                    A[i, j] = __detail::__real_if_needed(value);
                } else {
                    A[i, j] = E[i, j] + scaling * x[i] * __detail::__conj_if_needed(__detail::__accessor_value_t<XAccessor>(x[j]));
                }
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                if (i == j) {
                    auto value = __detail::__real_if_needed(__detail::__accessor_value_t<EAccessor>(E[i, j])) + scaling * x[i] * __detail::__conj_if_needed(__detail::__accessor_value_t<XAccessor>(x[j]));
                    A[i, j] = __detail::__real_if_needed(value);
                } else {
                    A[i, j] = E[i, j] + scaling * x[i] * __detail::__conj_if_needed(__detail::__accessor_value_t<XAccessor>(x[j]));
                }
            }
        }
    }
}

template<class Triangle,
         class XExtents, class XLayout, class XAccessor,
         class AExtents, class ALayout, class AAccessor>
    requires (__detail::__possibly_packed_output_v<AExtents, ALayout, AAccessor> &&
              __detail::__square_matrix_vector_static_extents_v<AExtents, XExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>)
[[deprecated("use draft parameter order: matrix before Triangle; rank updates put Triangle last")]]
void hermitian_matrix_rank_1_update(
    Triangle t,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A)
{
    hermitian_matrix_rank_1_update(typename AAccessor::element_type{1}, x, A, t);
}

// hermitian_matrix_rank_2_update — A = x*y^H + y*x^H  [linalg.algs.blas2.rank2]
template<class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class AExtents, class ALayout, class AAccessor,
         class Triangle>
    requires (__detail::__possibly_packed_output_v<AExtents, ALayout, AAccessor> &&
              __detail::__square_matrix_vectors_static_extents_v<AExtents, XExtents, YExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>)
void hermitian_matrix_rank_2_update(
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle)
{
    using idx_t = typename AExtents::index_type;
    const idx_t n = A.extent(0);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j) {
                auto value = x[i] * __detail::__conj_if_needed(__detail::__accessor_value_t<YAccessor>(y[j])) +
                             y[i] * __detail::__conj_if_needed(__detail::__accessor_value_t<XAccessor>(x[j]));
                if (i == j) A[i, j] = __detail::__real_if_needed(value);
                else A[i, j] = value;
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                auto value = x[i] * __detail::__conj_if_needed(__detail::__accessor_value_t<YAccessor>(y[j])) +
                             y[i] * __detail::__conj_if_needed(__detail::__accessor_value_t<XAccessor>(x[j]));
                if (i == j) A[i, j] = __detail::__real_if_needed(value);
                else A[i, j] = value;
            }
        }
    }
}

// hermitian_matrix_rank_2_update — A = E + x*y^H + y*x^H
template<class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class EExtents, class ELayout, class EAccessor,
         class AExtents, class ALayout, class AAccessor,
         class Triangle>
    requires (__detail::__possibly_packed_output_v<AExtents, ALayout, AAccessor> &&
              __detail::__square_matrix_vectors_static_extents_v<EExtents, XExtents, YExtents> &&
              __detail::__square_matrix_vectors_static_extents_v<AExtents, XExtents, YExtents> &&
              __detail::__compatible_static_extents_v<EExtents, AExtents> &&
              __detail::__packed_triangle_matches_v<ELayout, Triangle> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>)
void hermitian_matrix_rank_2_update(
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle)
{
    using idx_t = typename AExtents::index_type;
    const idx_t n = A.extent(0);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j) {
                if (i == j) {
                    auto value = __detail::__real_if_needed(__detail::__accessor_value_t<EAccessor>(E[i, j])) + x[i] * __detail::__conj_if_needed(__detail::__accessor_value_t<YAccessor>(y[j])) +
                             y[i] * __detail::__conj_if_needed(__detail::__accessor_value_t<XAccessor>(x[j]));
                    A[i, j] = __detail::__real_if_needed(value);
                } else {
                    A[i, j] = E[i, j] + x[i] * __detail::__conj_if_needed(__detail::__accessor_value_t<YAccessor>(y[j])) +
                             y[i] * __detail::__conj_if_needed(__detail::__accessor_value_t<XAccessor>(x[j]));
                }
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                if (i == j) {
                    auto value = __detail::__real_if_needed(__detail::__accessor_value_t<EAccessor>(E[i, j])) + x[i] * __detail::__conj_if_needed(__detail::__accessor_value_t<YAccessor>(y[j])) +
                             y[i] * __detail::__conj_if_needed(__detail::__accessor_value_t<XAccessor>(x[j]));
                    A[i, j] = __detail::__real_if_needed(value);
                } else {
                    A[i, j] = E[i, j] + x[i] * __detail::__conj_if_needed(__detail::__accessor_value_t<YAccessor>(y[j])) +
                             y[i] * __detail::__conj_if_needed(__detail::__accessor_value_t<XAccessor>(x[j]));
                }
            }
        }
    }
}

template<class Triangle,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class AExtents, class ALayout, class AAccessor>
    requires (__detail::__possibly_packed_output_v<AExtents, ALayout, AAccessor> &&
              __detail::__square_matrix_vectors_static_extents_v<AExtents, XExtents, YExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>)
[[deprecated("use draft parameter order: matrix before Triangle; rank updates put Triangle last")]]
void hermitian_matrix_rank_2_update(
    Triangle t,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A)
{
    hermitian_matrix_rank_2_update(x, y, A, t);
}

namespace __detail {

// Substitution preserves factor order. The divide receives (numerator,
// diagonal); left solves need a left-inverse divide for noncommutative values.
// Only local values use compound assignment; output references only use =.
template<bool Right, class A, class B, class X, class Triangle,
         class DiagonalStorage, class BinaryDivideOp>
void __triangular_solve(A a, B b, X x, Triangle, DiagonalStorage,
                        BinaryDivideOp& divide) {
    using index_type = std::common_type_t<typename A::index_type,
        typename B::index_type, typename X::index_type>;
    using value_type = typename X::value_type;
    constexpr bool upper = std::is_same_v<Triangle, upper_triangle_t>;
    constexpr bool unit = std::is_same_v<DiagonalStorage, implicit_unit_diagonal_t>;
    const index_type n = a.extent(0);
    const index_type nrhs = [&] {
        if constexpr (B::rank() == 1) return index_type{1};
        else return static_cast<index_type>(b.extent(Right ? 0 : 1));
    }();
    for (index_type rhs = 0; rhs < nrhs; ++rhs) {
        auto read_b = [&](index_type i) -> value_type {
            if constexpr (B::rank() == 1) return typename B::value_type(b[i]);
            else if constexpr (Right) return typename B::value_type(b[rhs, i]);
            else return typename B::value_type(b[i, rhs]);
        };
        auto read_x = [&](index_type i) -> value_type {
            if constexpr (X::rank() == 1) return x[i];
            else if constexpr (Right) return x[rhs, i];
            else return x[i, rhs];
        };
        auto store = [&](index_type i, const value_type& value) {
            if constexpr (X::rank() == 1) x[i] = value;
            else if constexpr (Right) x[rhs, i] = value;
            else x[i, rhs] = value;
        };
        for (index_type step = 0; step < n; ++step) {
            constexpr bool reverse = Right ? !upper : upper;
            const index_type i = reverse ? n - 1 - step : step;
            value_type residual(read_b(i));
            const index_type first = reverse ? i + 1 : 0;
            const index_type last = reverse ? n : i;
            for (index_type k = first; k < last; ++k) {
                if constexpr (Right) residual -= read_x(k) * a[k, i];
                else residual -= a[i, k] * read_x(k);
            }
            if constexpr (!unit) {
                residual = divide(residual, typename A::value_type(a[i, i]));
            }
            store(i, residual);
        }
    }
}

} // namespace __detail


// triangular_matrix_vector_solve — [linalg.algs.blas2.trsv]
template<class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class XExtents, class XLayout, class XAccessor,
         class BinaryDivideOp>
    requires (__detail::__writable_output_v<XExtents, XLayout, XAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_matrix_vectors_static_extents_v<AExtents, BExtents, XExtents>)
void triangular_matrix_vector_solve(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t, DiagonalStorage d,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> b,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    BinaryDivideOp divide)
{
    __detail::__triangular_solve<false>(A, b, x, t, d, divide);
}

template<class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class XExtents, class XLayout, class XAccessor>
    requires (__detail::__writable_output_v<XExtents, XLayout, XAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_matrix_vectors_static_extents_v<AExtents, BExtents, XExtents>)
void triangular_matrix_vector_solve(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t, DiagonalStorage d,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> b,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x)
{
    std::linalg::triangular_matrix_vector_solve(A, t, d, b, x, std::divides<void>{});
}

template<class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class BinaryDivideOp>
    requires (__detail::__writable_output_v<BExtents, BLayout, BAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_matrix_vector_static_extents_v<AExtents, BExtents>)
void triangular_matrix_vector_solve(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t, DiagonalStorage d,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> b,
    BinaryDivideOp divide)
{
    __detail::__triangular_solve<false>(A, b, b, t, d, divide);
}

template<class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor>
    requires (__detail::__writable_output_v<BExtents, BLayout, BAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_matrix_vector_static_extents_v<AExtents, BExtents>)
void triangular_matrix_vector_solve(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t, DiagonalStorage d,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> b)
{
    std::linalg::triangular_matrix_vector_solve(A, t, d, b, std::divides<void>{});
}


// Updating triangular product; z may alias y — [linalg.algs.blas2.trmv].
template<class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class ZExtents, class ZLayout, class ZAccessor>
    requires (__detail::__writable_output_v<ZExtents, ZLayout, ZAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_matrix_vectors_static_extents_v<AExtents, XExtents, YExtents> &&
              __detail::__square_matrix_vector_static_extents_v<AExtents, ZExtents> &&
              __detail::__compatible_static_extents_v<YExtents, ZExtents>)
void triangular_matrix_vector_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle, DiagonalStorage,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename ZAccessor::element_type, ZExtents, ZLayout, ZAccessor> z)
{
    using idx_t = typename AExtents::index_type;
    constexpr bool upper = std::is_same_v<Triangle, upper_triangle_t>;
    constexpr bool unit = std::is_same_v<DiagonalStorage, implicit_unit_diagonal_t>;
    for (idx_t i = 0; i < A.extent(0); ++i) {
        __detail::__accessor_value_t<ZAccessor> sum{};
        const idx_t first = upper ? i : idx_t{0};
        const idx_t last = upper ? A.extent(0) : i + 1;
        for (idx_t j = first; j < last; ++j) {
            if constexpr (unit) {
                if (i == j) { sum += x[j]; continue; }
            }
            sum += A[i, j] * x[j];
        }
        z[i] = y[i] + sum;
    }
}

} // namespace std::linalg

#endif // defined(__cpp_lib_mdspan)

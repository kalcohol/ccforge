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

namespace __detail {

template<class T>
auto __conj_if_needed(const T& value)
{
    if constexpr (requires { value.real(); value.imag(); }) {
        return std::conj(value);
    } else {
        return value;
    }
}

template<class T>
auto __real_if_needed(const T& value)
{
    if constexpr (requires { value.real(); value.imag(); }) {
        return value.real();
    } else {
        return value;
    }
}

} // namespace __detail

// ──────────────────────────────────────────────────────────────────────────
// BLAS Level 2 — [linalg.algs.blas2]
// ──────────────────────────────────────────────────────────────────────────

// matrix_vector_product — y = A*x  [linalg.algs.blas2.gemv]
template<class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor>
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
        using abi_t  = std::simd::native_abi<ElemT>;
        using simd_t = std::simd::basic_vec<ElemT, abi_t>;
        static constexpr auto kN = std::simd::simd_size<ElemT, abi_t>::value;
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
        typename YAccessor::element_type sum{};
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
void matrix_vector_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename ZAccessor::element_type, ZExtents, ZLayout, ZAccessor> z)
{
    using idx_t = typename AExtents::index_type;
    for (idx_t i = 0; i < A.extent(0); ++i) {
        typename ZAccessor::element_type sum{};
        for (idx_t j = 0; j < A.extent(1); ++j)
            sum += A[i, j] * x[j];
        z[i] = y[i] + sum;
    }
}

// triangular_matrix_vector_product — in-place  [linalg.algs.blas2.trmv]
template<class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor>
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
        typename XAccessor::element_type sum{};
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
        typename YAccessor::element_type sum{};
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

// triangular_matrix_vector_solve — in-place x = A^{-1} * x  [linalg.algs.blas2.trsv]
template<class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor>
void triangular_matrix_vector_solve(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle, DiagonalStorage,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x)
{
    using idx_t = typename AExtents::index_type;
    const idx_t n = A.extent(0);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    constexpr bool is_unit  = std::is_same_v<DiagonalStorage, implicit_unit_diagonal_t>;
    if constexpr (is_upper) {
        for (idx_t i = n; i-- > 0; ) {
            for (idx_t j = i + 1; j < n; ++j)
                x[i] -= A[i, j] * x[j];
            if constexpr (!is_unit) x[i] /= A[i, i];
        }
    } else {
        for (idx_t i = 0; i < n; ++i) {
            for (idx_t j = 0; j < i; ++j)
                x[i] -= A[i, j] * x[j];
            if constexpr (!is_unit) x[i] /= A[i, i];
        }
    }
}

// symmetric_matrix_vector_product — y = A*x (symmetric A)  [linalg.algs.blas2.symv]
template<class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor>
void symmetric_matrix_vector_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y)
{
    using idx_t = typename AExtents::index_type;
    const idx_t n = A.extent(0);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) y[i] = typename YAccessor::element_type{};
    for (idx_t i = 0; i < n; ++i) {
        y[i] += A[i, i] * x[i];
        for (idx_t j = i + 1; j < n; ++j) {
            auto aij = [&]() -> decltype(auto) {
                if constexpr (is_upper) return A[i, j];
                else return A[j, i];
            }();
            y[i] += aij * x[j];
            y[j] += aij * x[i];
        }
    }
}

// symmetric_matrix_vector_product (update) — z = y + A*x
template<class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class ZExtents, class ZLayout, class ZAccessor>
void symmetric_matrix_vector_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename ZAccessor::element_type, ZExtents, ZLayout, ZAccessor> z)
{
    symmetric_matrix_vector_product(A, t, x, z);
    for (typename ZExtents::index_type i = 0; i < z.extent(0); ++i)
        z[i] += y[i];
}

// hermitian_matrix_vector_product — y = A*x (hermitian A)  [linalg.algs.blas2.hemv]
template<class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor>
void hermitian_matrix_vector_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y)
{
    using idx_t = typename AExtents::index_type;
    const idx_t n = A.extent(0);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) y[i] = typename YAccessor::element_type{};
    for (idx_t i = 0; i < n; ++i) {
        y[i] += __detail::__real_if_needed(A[i, i]) * x[i];
        for (idx_t j = i + 1; j < n; ++j) {
            if constexpr (is_upper) {
                y[i] += A[i, j] * x[j];
                y[j] += __detail::__conj_if_needed(A[i, j]) * x[i];
            } else {
                y[i] += __detail::__conj_if_needed(A[j, i]) * x[j];
                y[j] += A[j, i] * x[i];
            }
        }
    }
}

// hermitian_matrix_vector_product (update) — z = y + A*x
template<class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class ZExtents, class ZLayout, class ZAccessor>
void hermitian_matrix_vector_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename ZAccessor::element_type, ZExtents, ZLayout, ZAccessor> z)
{
    hermitian_matrix_vector_product(A, t, x, z);
    for (typename ZExtents::index_type i = 0; i < z.extent(0); ++i)
        z[i] += y[i];
}

// matrix_rank_1_update — A = x * y^T  [linalg.algs.blas2.rank1]
template<class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class AExtents, class ALayout, class AAccessor>
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
void matrix_rank_1_update_c(
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A)
{
    using idx_t = typename AExtents::index_type;
    for (idx_t i = 0; i < A.extent(0); ++i)
        for (idx_t j = 0; j < A.extent(1); ++j)
            A[i, j] = x[i] * __detail::__conj_if_needed(y[j]);
}

// matrix_rank_1_update_c — A = E + x * conj(y^T)
template<class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class EExtents, class ELayout, class EAccessor,
         class AExtents, class ALayout, class AAccessor>
void matrix_rank_1_update_c(
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A)
{
    using idx_t = typename AExtents::index_type;
    for (idx_t i = 0; i < A.extent(0); ++i)
        for (idx_t j = 0; j < A.extent(1); ++j)
            A[i, j] = E[i, j] + x[i] * __detail::__conj_if_needed(y[j]);
}

// symmetric_matrix_rank_1_update — A = alpha * x * x^T  [linalg.algs.blas2.syr]
template<class ScalingFactor,
         class XExtents, class XLayout, class XAccessor,
         class AExtents, class ALayout, class AAccessor,
         class Triangle>
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
void symmetric_matrix_rank_1_update(
    ScalingFactor alpha, Triangle t,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A)
{
    symmetric_matrix_rank_1_update(alpha, x, A, t);
}

// symmetric_matrix_rank_2_update — A = x*y^T + y*x^T  [linalg.algs.blas2.syr2]
template<class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class AExtents, class ALayout, class AAccessor,
         class Triangle>
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

// hermitian_matrix_rank_1_update — A = alpha * x * x^H  [linalg.algs.blas2.rank1]
template<class ScalingFactor,
         class XExtents, class XLayout, class XAccessor,
         class AExtents, class ALayout, class AAccessor,
         class Triangle>
void hermitian_matrix_rank_1_update(
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
            for (idx_t j = i; j < n; ++j) {
                auto value = alpha * x[i] * __detail::__conj_if_needed(x[j]);
                A[i, j] = (i == j) ? __detail::__real_if_needed(value) : value;
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                auto value = alpha * x[i] * __detail::__conj_if_needed(x[j]);
                A[i, j] = (i == j) ? __detail::__real_if_needed(value) : value;
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
void hermitian_matrix_rank_1_update(
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
            for (idx_t j = i; j < n; ++j) {
                auto value = E[i, j] + alpha * x[i] * __detail::__conj_if_needed(x[j]);
                A[i, j] = (i == j) ? __detail::__real_if_needed(value) : value;
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                auto value = E[i, j] + alpha * x[i] * __detail::__conj_if_needed(x[j]);
                A[i, j] = (i == j) ? __detail::__real_if_needed(value) : value;
            }
        }
    }
}

template<class Triangle,
         class XExtents, class XLayout, class XAccessor,
         class AExtents, class ALayout, class AAccessor>
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
                auto value = x[i] * __detail::__conj_if_needed(y[j]) +
                             y[i] * __detail::__conj_if_needed(x[j]);
                A[i, j] = (i == j) ? __detail::__real_if_needed(value) : value;
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                auto value = x[i] * __detail::__conj_if_needed(y[j]) +
                             y[i] * __detail::__conj_if_needed(x[j]);
                A[i, j] = (i == j) ? __detail::__real_if_needed(value) : value;
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
                auto value = E[i, j] + x[i] * __detail::__conj_if_needed(y[j]) +
                             y[i] * __detail::__conj_if_needed(x[j]);
                A[i, j] = (i == j) ? __detail::__real_if_needed(value) : value;
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                auto value = E[i, j] + x[i] * __detail::__conj_if_needed(y[j]) +
                             y[i] * __detail::__conj_if_needed(x[j]);
                A[i, j] = (i == j) ? __detail::__real_if_needed(value) : value;
            }
        }
    }
}

template<class Triangle,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class AExtents, class ALayout, class AAccessor>
void hermitian_matrix_rank_2_update(
    Triangle t,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A)
{
    hermitian_matrix_rank_2_update(x, y, A, t);
}

} // namespace std::linalg

#endif // defined(__cpp_lib_mdspan)

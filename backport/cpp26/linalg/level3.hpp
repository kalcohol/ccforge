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

#include "level2.hpp"
#include <array>
#if defined(__cpp_lib_mdspan)

namespace std::linalg {

// ──────────────────────────────────────────────────────────────────────────
// BLAS Level 3 — [linalg.algs.blas3]
// ──────────────────────────────────────────────────────────────────────────

namespace __detail {

// Zero denotes an unknown layout: never infer contiguity from runtime strides.
template<class Layout> inline constexpr int __product_order = 0;
template<> inline constexpr int __product_order<layout_right> = 1;
template<> inline constexpr int __product_order<layout_left> = 2;
template<size_t P> inline constexpr int __product_order<layout_right_padded<P>> = 1;
template<size_t P> inline constexpr int __product_order<layout_left_padded<P>> = 2;

// Keep arbitrary user accessors on the scalar traversal, including when nested
// in a view. All paths access elements through mdspan, without pointer arithmetic.
template<class Acc> inline constexpr bool __product_plain_accessor = false;
template<class T> inline constexpr bool __product_plain_accessor<default_accessor<T>> = true;
template<class S, class Acc>
inline constexpr bool __product_plain_accessor<scaled_accessor<S, Acc>> =
    __product_plain_accessor<Acc>;
template<class Acc>
inline constexpr bool __product_plain_accessor<conjugated_accessor<Acc>> =
    __product_plain_accessor<Acc>;

// A short strip of independent sums exposes a contiguous inner dimension without
// a matrix-sized scratch buffer or reassociation of any individual reduction.
// Each output is assigned once, after its ascending-k reduction is complete.
template<class Value, class AL, class BL, bool Plain, class Index, class K,
         class Accumulate, class Store>
void __product_sums(Index m, Index n, K first, K last,
                    Accumulate accumulate, Store store)
{
    constexpr Index width = 8;
    if constexpr (Plain && __product_order<AL> != 0 && __product_order<BL> == 1) {
        for (Index i = 0; i < m; ++i)
            for (Index j = 0; j < n; ) {
                const Index count = n - j < width ? n - j : width;
                std::array<Value, 8> sums{Value{}, Value{}, Value{}, Value{},
                                          Value{}, Value{}, Value{}, Value{}};
                for (K k = first; k < last; ++k)
                    for (Index t = 0; t < count; ++t)
                        accumulate(sums[t], i, Index(j + t), k);
                for (Index t = 0; t < count; ++t)
                    store(i, Index(j + t), sums[t]);
                j += count;
            }
    } else if constexpr (Plain && __product_order<AL> == 2 && __product_order<BL> != 0) {
        for (Index j = 0; j < n; ++j)
            for (Index i = 0; i < m; ) {
                const Index count = m - i < width ? m - i : width;
                std::array<Value, 8> sums{Value{}, Value{}, Value{}, Value{},
                                          Value{}, Value{}, Value{}, Value{}};
                for (K k = first; k < last; ++k)
                    for (Index t = 0; t < count; ++t)
                        accumulate(sums[t], Index(i + t), j, k);
                for (Index t = 0; t < count; ++t)
                    store(Index(i + t), j, sums[t]);
                i += count;
            }
    } else {
        for (Index i = 0; i < m; ++i)
            for (Index j = 0; j < n; ++j) {
                Value sum{};
                for (K k = first; k < last; ++k)
                    accumulate(sum, i, j, k);
                store(i, j, sum);
            }
    }
}

} // namespace __detail

// matrix_product — C = A * B  [linalg.algs.blas3.gemm]
template<class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (__detail::__always_unique_mapping_v<CExtents, CLayout> &&
              __detail::__matrix_product_static_extents_v<AExtents, BExtents, CExtents>)
void matrix_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    using idx_t = typename CExtents::index_type;
    const idx_t m = C.extent(0), n = C.extent(1);
    const auto k = A.extent(1);
    __detail::__product_sums<__detail::__accessor_value_t<CAccessor>, ALayout, BLayout,
        __detail::__product_plain_accessor<AAccessor> &&
        __detail::__product_plain_accessor<BAccessor> &&
        __detail::__product_plain_accessor<CAccessor> &&
        (__detail::__product_order<CLayout> != 0)>(m, n, typename AExtents::index_type{0}, k,
        [&](auto& sum, idx_t i, idx_t j, auto l) { sum += A[i, l] * B[l, j]; },
        [&](idx_t i, idx_t j, const auto& sum) { C[i, j] = sum; });
}

// matrix_product (update) — C = E + A * B
template<class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class EExtents, class ELayout, class EAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (__detail::__always_unique_mapping_v<CExtents, CLayout> &&
              __detail::__matrix_product_static_extents_v<AExtents, BExtents, EExtents> &&
              __detail::__matrix_product_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__compatible_static_extents_v<EExtents, CExtents>)
void matrix_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    using idx_t = typename CExtents::index_type;
    const idx_t m = C.extent(0), n = C.extent(1);
    const auto k = A.extent(1);
    __detail::__product_sums<__detail::__accessor_value_t<CAccessor>, ALayout, BLayout,
        __detail::__product_plain_accessor<AAccessor> &&
        __detail::__product_plain_accessor<BAccessor> &&
        __detail::__product_plain_accessor<CAccessor> &&
        __detail::__product_plain_accessor<EAccessor> &&
        (__detail::__product_order<CLayout> != 0) &&
        (__detail::__product_order<ELayout> != 0)>(m, n, typename AExtents::index_type{0}, k,
        [&](auto& sum, idx_t i, idx_t j, auto l) { sum += A[i, l] * B[l, j]; },
        [&](idx_t i, idx_t j, const auto& sum) { C[i, j] = E[i, j] + sum; });
}

// triangular_matrix_left_product — C = A * C, in place [linalg.algs.blas3.trmm]
template<class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (__detail::__always_unique_mapping_v<CExtents, CLayout> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__square_matrix_left_operand_static_extents_v<AExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>)
void triangular_matrix_left_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle, DiagonalStorage,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    using idx_t = typename CExtents::index_type;
    using value_t = std::remove_cv_t<typename CAccessor::element_type>;
    const idx_t m = C.extent(0), n = C.extent(1);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    constexpr bool is_unit = std::is_same_v<DiagonalStorage, implicit_unit_diagonal_t>;
    if constexpr (__detail::__product_order<ALayout> == 2 &&
                  __detail::__product_order<CLayout> == 2 &&
                  std::is_same_v<AAccessor, default_accessor<typename AAccessor::element_type>> &&
                  std::is_same_v<CAccessor, default_accessor<typename CAccessor::element_type>>) {
        // Transform each column independently. Consume source k before any
        // update can reach it: upper ascends, lower descends. Both inner
        // accesses follow columns, including for padded layouts.
        for (idx_t j = 0; j < n; ++j)
            for (idx_t step = 0; step < m; ++step) {
                const idx_t k = is_upper ? step : idx_t(m - 1 - step);
                const value_t source(C[k, j]);
                if constexpr (!is_unit) {
                    value_t sum{};
                    sum += A[k, k] * source;
                    C[k, j] = sum;
                }
                const idx_t first = is_upper ? idx_t{0} : idx_t(k + 1);
                const idx_t last = is_upper ? k : m;
                for (idx_t i = first; i < last; ++i) {
                    value_t sum(C[i, j]);
                    sum += A[i, k] * source;
                    C[i, j] = sum;
                }
            }
        return;
    }
    // Upper reads rows at or above i; lower reads rows at or below i.
    for (idx_t step = 0; step < m; ++step) {
        const idx_t i = is_upper ? step : idx_t(m - 1 - step);
        const idx_t first = is_upper ? i : idx_t{0};
        const idx_t last = is_upper ? m : idx_t(i + 1);
        __detail::__product_sums<value_t, ALayout, CLayout,
            __detail::__product_plain_accessor<AAccessor> &&
            __detail::__product_plain_accessor<CAccessor> &&
            (__detail::__product_order<CLayout> != 0)>(
            idx_t{1}, n, first, last,
            [&](auto& sum, idx_t, idx_t j, idx_t k) {
                if constexpr (is_unit)
                    sum += (k == i ? value_t(C[k, j]) : A[i, k] * value_t(C[k, j]));
                else
                    sum += A[i, k] * value_t(C[k, j]);
            },
            [&](idx_t, idx_t j, const auto& sum) { C[i, j] = sum; });
    }
}

// triangular_matrix_right_product — C = C * A, in place [linalg.algs.blas3.trmm]
template<class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (__detail::__always_unique_mapping_v<CExtents, CLayout> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__square_matrix_right_operand_static_extents_v<AExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>)
void triangular_matrix_right_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle, DiagonalStorage,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    using idx_t = typename CExtents::index_type;
    using value_t = std::remove_cv_t<typename CAccessor::element_type>;
    const idx_t m = C.extent(0), n = C.extent(1);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    constexpr bool is_unit = std::is_same_v<DiagonalStorage, implicit_unit_diagonal_t>;
    if constexpr (__detail::__product_order<ALayout> == 1 &&
                  __detail::__product_order<CLayout> == 1 &&
                  std::is_same_v<AAccessor, default_accessor<typename AAccessor::element_type>> &&
                  std::is_same_v<CAccessor, default_accessor<typename CAccessor::element_type>>) {
        // Transform each row independently. Lower consumes sources ascending,
        // upper descending; destinations have already supplied their original
        // values. The inner loop follows contiguous rows of both A and C.
        for (idx_t i = 0; i < m; ++i)
            for (idx_t step = 0; step < n; ++step) {
                const idx_t k = is_upper ? idx_t(n - 1 - step) : step;
                const value_t source(C[i, k]);
                if constexpr (!is_unit) {
                    value_t sum{};
                    sum += source * A[k, k];
                    C[i, k] = sum;
                }
                const idx_t first = is_upper ? idx_t(k + 1) : idx_t{0};
                const idx_t last = is_upper ? n : k;
                for (idx_t j = first; j < last; ++j) {
                    value_t sum(C[i, j]);
                    sum += source * A[k, j];
                    C[i, j] = sum;
                }
            }
        return;
    }
    // Upper reads columns at or below j; lower reads columns at or above j.
    for (idx_t step = 0; step < n; ++step) {
        const idx_t j = is_upper ? idx_t(n - 1 - step) : step;
        const idx_t first = is_upper ? idx_t{0} : j;
        const idx_t last = is_upper ? idx_t(j + 1) : n;
        __detail::__product_sums<value_t, CLayout,
            std::conditional_t<(__detail::__product_order<ALayout> != 0), layout_left, ALayout>,
            __detail::__product_plain_accessor<AAccessor> &&
            __detail::__product_plain_accessor<CAccessor> &&
            (__detail::__product_order<CLayout> != 0)>(
            m, idx_t{1}, first, last,
            [&](auto& sum, idx_t i, idx_t, idx_t k) {
                if constexpr (is_unit)
                    sum += (k == j ? value_t(C[i, k]) : value_t(C[i, k]) * A[k, j]);
                else
                    sum += value_t(C[i, k]) * A[k, j];
            },
            [&](idx_t i, idx_t, const auto& sum) { C[i, j] = sum; });
    }
}

// triangular_matrix_matrix_left_solve — B = A^{-1} * B  [linalg.algs.blas3.trsm]
template<class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor>
    requires (__detail::__always_unique_mapping_v<BExtents, BLayout> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__square_matrix_left_operand_static_extents_v<AExtents, BExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>)
void triangular_matrix_matrix_left_solve(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t, DiagonalStorage d,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B)
{
    using idx_t = typename BExtents::index_type;
    const idx_t n = A.extent(0), nrhs = B.extent(1);
    for (idx_t j = 0; j < nrhs; ++j) {
        auto Bcol = [&](idx_t i) -> decltype(auto) { return B[i, j]; };
        constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
        constexpr bool is_unit  = std::is_same_v<DiagonalStorage, implicit_unit_diagonal_t>;
        if constexpr (is_upper) {
            for (idx_t i = n; i-- > 0; ) {
                for (idx_t k = i + 1; k < n; ++k)
                    Bcol(i) -= A[i, k] * Bcol(k);
                if constexpr (!is_unit) Bcol(i) /= A[i, i];
            }
        } else {
            for (idx_t i = 0; i < n; ++i) {
                for (idx_t k = 0; k < i; ++k)
                    Bcol(i) -= A[i, k] * Bcol(k);
                if constexpr (!is_unit) Bcol(i) /= A[i, i];
            }
        }
    }
}

// symmetric_matrix_product — C = A * B where A is symmetric  [linalg.algs.blas3.symm]
template<class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (__detail::__always_unique_mapping_v<CExtents, CLayout> &&
              __detail::__square_matrix_product_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>)
void symmetric_matrix_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    using idx_t = typename CExtents::index_type;
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    __detail::__product_sums<__detail::__accessor_value_t<CAccessor>, ALayout, BLayout,
        __detail::__product_plain_accessor<AAccessor> &&
        __detail::__product_plain_accessor<BAccessor> &&
        __detail::__product_plain_accessor<CAccessor> &&
        (__detail::__product_order<CLayout> != 0)>(
        C.extent(0), C.extent(1), idx_t{0}, static_cast<idx_t>(A.extent(1)),
        [&](auto& sum, idx_t i, idx_t j, idx_t l) {
            auto ail = [&]() -> decltype(auto) {
                if (i == l) return A[i, i];
                if constexpr (is_upper) {
                    return i < l ? A[i, l] : A[l, i];
                } else {
                    return i > l ? A[i, l] : A[l, i];
                }
            }();
            sum += ail * B[l, j];
        },
        [&](idx_t i, idx_t j, const auto& sum) { C[i, j] = sum; });
}

// symmetric_matrix_rank_k_update — C = alpha * A * A^T  [linalg.algs.blas3.syrk]
template<class ScalingFactor,
         class AExtents, class ALayout, class AAccessor,
         class CExtents, class CLayout, class CAccessor,
         class Triangle>
    requires (__detail::__possibly_packed_output_mapping_v<CExtents, CLayout> &&
              __detail::__matrix_rank_k_static_extents_v<AExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<CLayout, Triangle>)
void symmetric_matrix_rank_k_update(
    ScalingFactor alpha,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C,
    Triangle)
{
    using idx_t = typename CExtents::index_type;
    const idx_t n = C.extent(0);
    const auto k = A.extent(1);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j) {
                __detail::__accessor_value_t<CAccessor> sum{};
                for (typename AExtents::index_type l = 0; l < k; ++l)
                    sum += A[i, l] * A[j, l];
                C[i, j] = alpha * sum;
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                __detail::__accessor_value_t<CAccessor> sum{};
                for (typename AExtents::index_type l = 0; l < k; ++l)
                    sum += A[i, l] * A[j, l];
                C[i, j] = alpha * sum;
            }
        }
    }
}

// symmetric_matrix_rank_k_update — C = E + alpha * A * A^T
template<class ScalingFactor,
         class AExtents, class ALayout, class AAccessor,
         class EExtents, class ELayout, class EAccessor,
         class CExtents, class CLayout, class CAccessor,
         class Triangle>
    requires (__detail::__possibly_packed_output_mapping_v<CExtents, CLayout> &&
              __detail::__matrix_rank_k_static_extents_v<AExtents, EExtents> &&
              __detail::__matrix_rank_k_static_extents_v<AExtents, CExtents> &&
              __detail::__compatible_static_extents_v<EExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<ELayout, Triangle> &&
              __detail::__packed_triangle_matches_v<CLayout, Triangle>)
void symmetric_matrix_rank_k_update(
    ScalingFactor alpha,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C,
    Triangle)
{
    using idx_t = typename CExtents::index_type;
    const idx_t n = C.extent(0);
    const auto k = A.extent(1);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j) {
                __detail::__accessor_value_t<CAccessor> sum{};
                for (typename AExtents::index_type l = 0; l < k; ++l)
                    sum += A[i, l] * A[j, l];
                C[i, j] = E[i, j] + alpha * sum;
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                __detail::__accessor_value_t<CAccessor> sum{};
                for (typename AExtents::index_type l = 0; l < k; ++l)
                    sum += A[i, l] * A[j, l];
                C[i, j] = E[i, j] + alpha * sum;
            }
        }
    }
}

template<class Triangle, class ScalingFactor,
         class AExtents, class ALayout, class AAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (__detail::__possibly_packed_output_mapping_v<CExtents, CLayout> &&
              __detail::__matrix_rank_k_static_extents_v<AExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<CLayout, Triangle>)
void symmetric_matrix_rank_k_update(
    ScalingFactor alpha, Triangle t,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    symmetric_matrix_rank_k_update(alpha, A, C, t);
}

// symmetric_matrix_rank_2k_update — C = A*B^T + B*A^T  [linalg.algs.blas3.syr2k]
template<class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class CExtents, class CLayout, class CAccessor,
         class Triangle>
    requires (__detail::__possibly_packed_output_mapping_v<CExtents, CLayout> &&
              __detail::__matrix_rank_2k_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<CLayout, Triangle>)
void symmetric_matrix_rank_2k_update(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C,
    Triangle)
{
    using idx_t = typename CExtents::index_type;
    const idx_t n = C.extent(0);
    const auto k = A.extent(1);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j) {
                __detail::__accessor_value_t<CAccessor> sum{};
                for (typename AExtents::index_type l = 0; l < k; ++l)
                    sum += A[i, l] * B[j, l] + B[i, l] * A[j, l];
                C[i, j] = sum;
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                __detail::__accessor_value_t<CAccessor> sum{};
                for (typename AExtents::index_type l = 0; l < k; ++l)
                    sum += A[i, l] * B[j, l] + B[i, l] * A[j, l];
                C[i, j] = sum;
            }
        }
    }
}

// symmetric_matrix_rank_2k_update — C = E + A*B^T + B*A^T
template<class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class EExtents, class ELayout, class EAccessor,
         class CExtents, class CLayout, class CAccessor,
         class Triangle>
    requires (__detail::__possibly_packed_output_mapping_v<CExtents, CLayout> &&
              __detail::__matrix_rank_2k_static_extents_v<AExtents, BExtents, EExtents> &&
              __detail::__matrix_rank_2k_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__compatible_static_extents_v<EExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<ELayout, Triangle> &&
              __detail::__packed_triangle_matches_v<CLayout, Triangle>)
void symmetric_matrix_rank_2k_update(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C,
    Triangle)
{
    using idx_t = typename CExtents::index_type;
    const idx_t n = C.extent(0);
    const auto k = A.extent(1);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j) {
                __detail::__accessor_value_t<CAccessor> sum{};
                for (typename AExtents::index_type l = 0; l < k; ++l)
                    sum += A[i, l] * B[j, l] + B[i, l] * A[j, l];
                C[i, j] = E[i, j] + sum;
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                __detail::__accessor_value_t<CAccessor> sum{};
                for (typename AExtents::index_type l = 0; l < k; ++l)
                    sum += A[i, l] * B[j, l] + B[i, l] * A[j, l];
                C[i, j] = E[i, j] + sum;
            }
        }
    }
}

template<class Triangle, class ScalingFactor,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (__detail::__possibly_packed_output_mapping_v<CExtents, CLayout> &&
              __detail::__matrix_rank_2k_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<CLayout, Triangle>)
void symmetric_matrix_rank_2k_update(
    ScalingFactor alpha, Triangle,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    using idx_t = typename CExtents::index_type;
    const idx_t n = C.extent(0);
    const auto k = A.extent(1);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j) {
                __detail::__accessor_value_t<CAccessor> sum{};
                for (typename AExtents::index_type l = 0; l < k; ++l)
                    sum += A[i, l] * B[j, l] + B[i, l] * A[j, l];
                C[i, j] = alpha * sum;
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                __detail::__accessor_value_t<CAccessor> sum{};
                for (typename AExtents::index_type l = 0; l < k; ++l)
                    sum += A[i, l] * B[j, l] + B[i, l] * A[j, l];
                C[i, j] = alpha * sum;
            }
        }
    }
}

// hermitian_matrix_product — [linalg.algs.blas3.hemm]
template<class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (__detail::__always_unique_mapping_v<CExtents, CLayout> &&
              __detail::__square_matrix_product_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>)
void hermitian_matrix_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    using idx_t = typename CExtents::index_type;
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    __detail::__product_sums<__detail::__accessor_value_t<CAccessor>, ALayout, BLayout,
        __detail::__product_plain_accessor<AAccessor> &&
        __detail::__product_plain_accessor<BAccessor> &&
        __detail::__product_plain_accessor<CAccessor> &&
        (__detail::__product_order<CLayout> != 0)>(
        C.extent(0), C.extent(1), idx_t{0}, static_cast<idx_t>(A.extent(1)),
        [&](auto& sum, idx_t i, idx_t j, idx_t l) {
            auto ail = [&]() -> typename CAccessor::element_type {
                if (i == l) return __detail::__real_if_needed(A[i, i]);
                if constexpr (is_upper) {
                    return i < l ? A[i, l] : __detail::__conj_if_needed(A[l, i]);
                } else {
                    return i > l ? A[i, l] : __detail::__conj_if_needed(A[l, i]);
                }
            }();
            sum += ail * B[l, j];
        },
        [&](idx_t i, idx_t j, const auto& sum) { C[i, j] = sum; });
}

template<class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (__detail::__always_unique_mapping_v<CExtents, CLayout> &&
              __detail::__square_matrix_product_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>)
[[deprecated("use draft parameter order: matrix before Triangle; rank updates put Triangle last")]]
void hermitian_matrix_product(
    Triangle t,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    hermitian_matrix_product(A, t, B, C);
}

// hermitian_matrix_rank_k_update — C = alpha * A * A^H  [linalg.algs.blas3.herk]
template<class ScalingFactor,
         class AExtents, class ALayout, class AAccessor,
         class CExtents, class CLayout, class CAccessor,
         class Triangle>
    requires (__detail::__possibly_packed_output_mapping_v<CExtents, CLayout> &&
              __detail::__matrix_rank_k_static_extents_v<AExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<CLayout, Triangle>)
void hermitian_matrix_rank_k_update(
    ScalingFactor alpha,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C,
    Triangle)
{
    using idx_t = typename CExtents::index_type;
    const idx_t n = C.extent(0);
    const auto k = A.extent(1);
    const auto scaling = __detail::__real_if_needed(alpha);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j) {
                __detail::__accessor_value_t<CAccessor> sum{};
                for (typename AExtents::index_type l = 0; l < k; ++l)
                    sum += A[i, l] * __detail::__conj_if_needed(A[j, l]);
                auto value = scaling * sum;
                C[i, j] = (i == j) ? __detail::__real_if_needed(value) : value;
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                __detail::__accessor_value_t<CAccessor> sum{};
                for (typename AExtents::index_type l = 0; l < k; ++l)
                    sum += A[i, l] * __detail::__conj_if_needed(A[j, l]);
                auto value = scaling * sum;
                C[i, j] = (i == j) ? __detail::__real_if_needed(value) : value;
            }
        }
    }
}

// hermitian_matrix_rank_k_update — C = E + alpha * A * A^H
template<class ScalingFactor,
         class AExtents, class ALayout, class AAccessor,
         class EExtents, class ELayout, class EAccessor,
         class CExtents, class CLayout, class CAccessor,
         class Triangle>
    requires (__detail::__possibly_packed_output_mapping_v<CExtents, CLayout> &&
              __detail::__matrix_rank_k_static_extents_v<AExtents, EExtents> &&
              __detail::__matrix_rank_k_static_extents_v<AExtents, CExtents> &&
              __detail::__compatible_static_extents_v<EExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<ELayout, Triangle> &&
              __detail::__packed_triangle_matches_v<CLayout, Triangle>)
void hermitian_matrix_rank_k_update(
    ScalingFactor alpha,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C,
    Triangle)
{
    using idx_t = typename CExtents::index_type;
    const idx_t n = C.extent(0);
    const auto k = A.extent(1);
    const auto scaling = __detail::__real_if_needed(alpha);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j) {
                __detail::__accessor_value_t<CAccessor> sum{};
                for (typename AExtents::index_type l = 0; l < k; ++l)
                    sum += A[i, l] * __detail::__conj_if_needed(A[j, l]);
                auto value = E[i, j] + scaling * sum;
                C[i, j] = (i == j) ? __detail::__real_if_needed(value) : value;
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                __detail::__accessor_value_t<CAccessor> sum{};
                for (typename AExtents::index_type l = 0; l < k; ++l)
                    sum += A[i, l] * __detail::__conj_if_needed(A[j, l]);
                auto value = E[i, j] + scaling * sum;
                C[i, j] = (i == j) ? __detail::__real_if_needed(value) : value;
            }
        }
    }
}

template<class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (__detail::__possibly_packed_output_mapping_v<CExtents, CLayout> &&
              __detail::__matrix_rank_k_static_extents_v<AExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<CLayout, Triangle>)
[[deprecated("use draft parameter order: matrix before Triangle; rank updates put Triangle last")]]
void hermitian_matrix_rank_k_update(
    Triangle t,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    hermitian_matrix_rank_k_update(typename CAccessor::element_type{1}, A, C, t);
}

} // namespace std::linalg

#endif // defined(__cpp_lib_mdspan)

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
#if defined(__cpp_lib_mdspan)

namespace std::linalg {

// ──────────────────────────────────────────────────────────────────────────
// BLAS Level 3 — [linalg.algs.blas3]
// ──────────────────────────────────────────────────────────────────────────

// matrix_product — C = A * B  [linalg.algs.blas3.gemm]
template<class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class CExtents, class CLayout, class CAccessor>
void matrix_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    using idx_t = typename CExtents::index_type;
    const idx_t m = C.extent(0), n = C.extent(1), k = A.extent(1);
    for (idx_t i = 0; i < m; ++i)
        for (idx_t j = 0; j < n; ++j) {
            typename CAccessor::element_type sum{};
            for (idx_t l = 0; l < k; ++l)
                sum += A[i, l] * B[l, j];
            C[i, j] = sum;
        }
}

// matrix_product (update) — C = E + A * B
template<class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class EExtents, class ELayout, class EAccessor,
         class CExtents, class CLayout, class CAccessor>
void matrix_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    using idx_t = typename CExtents::index_type;
    const idx_t m = C.extent(0), n = C.extent(1), k = A.extent(1);
    for (idx_t i = 0; i < m; ++i)
        for (idx_t j = 0; j < n; ++j) {
            typename CAccessor::element_type sum{};
            for (idx_t l = 0; l < k; ++l)
                sum += A[i, l] * B[l, j];
            C[i, j] = E[i, j] + sum;
        }
}

// triangular_matrix_product — C = A * B where A is triangular  [linalg.algs.blas3.trmm]
template<class Triangle, class DiagonalStorage, class Side,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class CExtents, class CLayout, class CAccessor>
void triangular_matrix_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle, DiagonalStorage, Side,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    using idx_t = typename CExtents::index_type;
    const idx_t m = C.extent(0), n = C.extent(1);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    constexpr bool is_unit  = std::is_same_v<DiagonalStorage, implicit_unit_diagonal_t>;
    for (idx_t i = 0; i < m; ++i)
        for (idx_t j = 0; j < n; ++j) {
            typename CAccessor::element_type sum{};
            if constexpr (is_upper) {
                for (idx_t k = i; k < m; ++k) {
                    if constexpr (is_unit) {
                        sum += (k == i ? B[k, j] : A[i, k] * B[k, j]);
                    } else {
                        sum += A[i, k] * B[k, j];
                    }
                }
            } else {
                for (idx_t k = 0; k <= i; ++k) {
                    if constexpr (is_unit) {
                        sum += (k == i ? B[k, j] : A[i, k] * B[k, j]);
                    } else {
                        sum += A[i, k] * B[k, j];
                    }
                }
            }
            C[i, j] = sum;
        }
}

// triangular_matrix_matrix_left_solve — B = A^{-1} * B  [linalg.algs.blas3.trsm]
template<class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor>
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
void symmetric_matrix_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    using idx_t = typename CExtents::index_type;
    const idx_t m = C.extent(0), n = C.extent(1), k = A.extent(0);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < m; ++i)
        for (idx_t j = 0; j < n; ++j) {
            typename CAccessor::element_type sum{};
            for (idx_t l = 0; l < k; ++l) {
                auto ail = [&]() -> decltype(auto) {
                    if (i == l) return A[i, i];
                    if constexpr (is_upper) {
                        return i < l ? A[i, l] : A[l, i];
                    } else {
                        return i > l ? A[i, l] : A[l, i];
                    }
                }();
                sum += ail * B[l, j];
            }
            C[i, j] = sum;
        }
}

// symmetric_matrix_rank_k_update — C = alpha * A * A^T  [linalg.algs.blas3.syrk]
template<class ScalingFactor,
         class AExtents, class ALayout, class AAccessor,
         class CExtents, class CLayout, class CAccessor,
         class Triangle>
void symmetric_matrix_rank_k_update(
    ScalingFactor alpha,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C,
    Triangle)
{
    using idx_t = typename CExtents::index_type;
    const idx_t n = C.extent(0), k = A.extent(1);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j) {
                typename CAccessor::element_type sum{};
                for (idx_t l = 0; l < k; ++l)
                    sum += A[i, l] * A[j, l];
                C[i, j] = alpha * sum;
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                typename CAccessor::element_type sum{};
                for (idx_t l = 0; l < k; ++l)
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
void symmetric_matrix_rank_k_update(
    ScalingFactor alpha,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C,
    Triangle)
{
    using idx_t = typename CExtents::index_type;
    const idx_t n = C.extent(0), k = A.extent(1);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j) {
                typename CAccessor::element_type sum{};
                for (idx_t l = 0; l < k; ++l)
                    sum += A[i, l] * A[j, l];
                C[i, j] = E[i, j] + alpha * sum;
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                typename CAccessor::element_type sum{};
                for (idx_t l = 0; l < k; ++l)
                    sum += A[i, l] * A[j, l];
                C[i, j] = E[i, j] + alpha * sum;
            }
        }
    }
}

template<class Triangle, class ScalingFactor,
         class AExtents, class ALayout, class AAccessor,
         class CExtents, class CLayout, class CAccessor>
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
void symmetric_matrix_rank_2k_update(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C,
    Triangle)
{
    using idx_t = typename CExtents::index_type;
    const idx_t n = C.extent(0), k = A.extent(1);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j) {
                typename CAccessor::element_type sum{};
                for (idx_t l = 0; l < k; ++l)
                    sum += A[i, l] * B[j, l] + B[i, l] * A[j, l];
                C[i, j] = sum;
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                typename CAccessor::element_type sum{};
                for (idx_t l = 0; l < k; ++l)
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
void symmetric_matrix_rank_2k_update(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C,
    Triangle)
{
    using idx_t = typename CExtents::index_type;
    const idx_t n = C.extent(0), k = A.extent(1);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j) {
                typename CAccessor::element_type sum{};
                for (idx_t l = 0; l < k; ++l)
                    sum += A[i, l] * B[j, l] + B[i, l] * A[j, l];
                C[i, j] = E[i, j] + sum;
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                typename CAccessor::element_type sum{};
                for (idx_t l = 0; l < k; ++l)
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
void symmetric_matrix_rank_2k_update(
    ScalingFactor alpha, Triangle,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    using idx_t = typename CExtents::index_type;
    const idx_t n = C.extent(0), k = A.extent(1);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j) {
                typename CAccessor::element_type sum{};
                for (idx_t l = 0; l < k; ++l)
                    sum += A[i, l] * B[j, l] + B[i, l] * A[j, l];
                C[i, j] = alpha * sum;
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                typename CAccessor::element_type sum{};
                for (idx_t l = 0; l < k; ++l)
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
void hermitian_matrix_product(
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    using idx_t = typename CExtents::index_type;
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < C.extent(0); ++i) {
        for (idx_t j = 0; j < C.extent(1); ++j) {
            typename CAccessor::element_type sum{};
            for (idx_t l = 0; l < A.extent(1); ++l) {
                auto ail = [&]() -> typename CAccessor::element_type {
                    if (i == l) return __detail::__real_if_needed(A[i, i]);
                    if constexpr (is_upper) {
                        return i < l ? A[i, l] : __detail::__conj_if_needed(A[l, i]);
                    } else {
                        return i > l ? A[i, l] : __detail::__conj_if_needed(A[l, i]);
                    }
                }();
                sum += ail * B[l, j];
            }
            C[i, j] = sum;
        }
    }
}

template<class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class CExtents, class CLayout, class CAccessor>
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
void hermitian_matrix_rank_k_update(
    ScalingFactor alpha,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C,
    Triangle)
{
    using idx_t = typename CExtents::index_type;
    const idx_t n = C.extent(0);
    const idx_t k = A.extent(1);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j) {
                typename CAccessor::element_type sum{};
                for (idx_t l = 0; l < k; ++l)
                    sum += A[i, l] * __detail::__conj_if_needed(A[j, l]);
                auto value = alpha * sum;
                C[i, j] = (i == j) ? __detail::__real_if_needed(value) : value;
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                typename CAccessor::element_type sum{};
                for (idx_t l = 0; l < k; ++l)
                    sum += A[i, l] * __detail::__conj_if_needed(A[j, l]);
                auto value = alpha * sum;
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
void hermitian_matrix_rank_k_update(
    ScalingFactor alpha,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C,
    Triangle)
{
    using idx_t = typename CExtents::index_type;
    const idx_t n = C.extent(0);
    const idx_t k = A.extent(1);
    constexpr bool is_upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (idx_t i = 0; i < n; ++i) {
        if constexpr (is_upper) {
            for (idx_t j = i; j < n; ++j) {
                typename CAccessor::element_type sum{};
                for (idx_t l = 0; l < k; ++l)
                    sum += A[i, l] * __detail::__conj_if_needed(A[j, l]);
                auto value = E[i, j] + alpha * sum;
                C[i, j] = (i == j) ? __detail::__real_if_needed(value) : value;
            }
        } else {
            for (idx_t j = 0; j <= i; ++j) {
                typename CAccessor::element_type sum{};
                for (idx_t l = 0; l < k; ++l)
                    sum += A[i, l] * __detail::__conj_if_needed(A[j, l]);
                auto value = E[i, j] + alpha * sum;
                C[i, j] = (i == j) ? __detail::__real_if_needed(value) : value;
            }
        }
    }
}

template<class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class CExtents, class CLayout, class CAccessor>
void hermitian_matrix_rank_k_update(
    Triangle t,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    hermitian_matrix_rank_k_update(typename CAccessor::element_type{1}, A, C, t);
}

} // namespace std::linalg

#endif // defined(__cpp_lib_mdspan)

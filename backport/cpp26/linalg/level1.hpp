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

#include "accessor.hpp"
#if defined(__cpp_lib_mdspan)

#include <limits>

namespace std::linalg {

// ──────────────────────────────────────────────────────────────────────────
// BLAS Level 1 — [linalg.algs.blas1]
// ──────────────────────────────────────────────────────────────────────────

// copy — [linalg.algs.blas1.copy]
template<class InExtents, class InLayout, class InAccessor,
         class OutExtents, class OutLayout, class OutAccessor>
    requires (std::mdspan<typename InAccessor::element_type, InExtents, InLayout, InAccessor>::rank() ==
              std::mdspan<typename OutAccessor::element_type, OutExtents, OutLayout, OutAccessor>::rank())
void copy(
    std::mdspan<typename InAccessor::element_type, InExtents, InLayout, InAccessor> from,
    std::mdspan<typename OutAccessor::element_type, OutExtents, OutLayout, OutAccessor> to)
{
    if constexpr (InExtents::rank() == 1) {
        using ElemT = std::remove_const_t<typename InAccessor::element_type>;
        using OutElemT = std::remove_const_t<typename OutAccessor::element_type>;
#if __LINALG_HAS_SIMD
        if constexpr (std::is_same_v<ElemT, OutElemT> &&
                      __detail::__can_simd_v<ElemT, InLayout, InAccessor> &&
                      __detail::__can_simd_v<ElemT, OutLayout, OutAccessor>) {
            using abi_t  = std::simd::native_abi<ElemT>;
            using simd_t = std::simd::basic_vec<ElemT, abi_t>;
            static constexpr auto kN = std::simd::simd_size<ElemT, abi_t>::value;
            const auto n = from.extent(0);
            typename InExtents::index_type i = 0;
            const ElemT* src = from.data_handle();
            ElemT* dst = to.data_handle();
            for (; i + static_cast<decltype(i)>(kN) <= n; i += kN) {
                simd_t v{std::span<const ElemT, kN>{src + i, kN}};
                for (std::ptrdiff_t j = 0; j < kN; ++j) dst[i+j] = v[j];
            }
            for (; i < n; ++i) dst[i] = src[i];
            return;
        }
#endif
        for (typename InExtents::index_type i = 0; i < from.extent(0); ++i)
            to[i] = from[i];
    } else {
        for (typename InExtents::index_type i = 0; i < from.extent(0); ++i)
            for (typename InExtents::index_type j = 0; j < from.extent(1); ++j)
                to[i, j] = from[i, j];
    }
}

// scale — [linalg.algs.blas1.scale]
template<class ScalingFactor,
         class Extents, class Layout, class Accessor>
void scale(
    ScalingFactor alpha,
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x)
{
    if constexpr (Extents::rank() == 1) {
        using ElemT = std::remove_const_t<typename Accessor::element_type>;
#if __LINALG_HAS_SIMD
        if constexpr (__detail::__can_simd_v<ElemT, Layout, Accessor> &&
                      std::is_same_v<ScalingFactor, ElemT>) {
            using abi_t  = std::simd::native_abi<ElemT>;
            using simd_t = std::simd::basic_vec<ElemT, abi_t>;
            static constexpr auto kN = std::simd::simd_size<ElemT, abi_t>::value;
            const auto n = x.extent(0);
            typename Extents::index_type i = 0;
            ElemT* px = x.data_handle();
            for (; i + static_cast<decltype(i)>(kN) <= n; i += kN) {
                simd_t v{std::span<const ElemT, kN>{px + i, kN}};
                v *= simd_t{alpha};
                for (std::ptrdiff_t j = 0; j < kN; ++j) px[i+j] = v[j];
            }
            for (; i < n; ++i) px[i] *= alpha;
            return;
        }
#endif
        for (typename Extents::index_type i = 0; i < x.extent(0); ++i)
            x[i] *= alpha;
    } else {
        for (typename Extents::index_type i = 0; i < x.extent(0); ++i)
            for (typename Extents::index_type j = 0; j < x.extent(1); ++j)
                x[i, j] *= alpha;
    }
}

// swap_elements — [linalg.algs.blas1.swap]
template<class Extents, class Layout1, class Accessor1,
                        class Layout2, class Accessor2>
void swap_elements(
    std::mdspan<typename Accessor1::element_type, Extents, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents, Layout2, Accessor2> y)
{
    using std::swap;
    if constexpr (Extents::rank() == 1) {
        for (typename Extents::index_type i = 0; i < x.extent(0); ++i)
            swap(x[i], y[i]);
    } else {
        for (typename Extents::index_type i = 0; i < x.extent(0); ++i)
            for (typename Extents::index_type j = 0; j < x.extent(1); ++j)
                swap(x[i, j], y[i, j]);
    }
}

// add — [linalg.algs.blas1.add]
template<class InExtents, class InLayout1, class InAccessor1,
                          class InLayout2, class InAccessor2,
         class OutExtents, class OutLayout, class OutAccessor>
void add(
    std::mdspan<typename InAccessor1::element_type, InExtents, InLayout1, InAccessor1> x,
    std::mdspan<typename InAccessor2::element_type, InExtents, InLayout2, InAccessor2> y,
    std::mdspan<typename OutAccessor::element_type, OutExtents, OutLayout, OutAccessor> z)
{
    if constexpr (InExtents::rank() == 1) {
        for (typename InExtents::index_type i = 0; i < x.extent(0); ++i)
            z[i] = x[i] + y[i];
    } else {
        for (typename InExtents::index_type i = 0; i < x.extent(0); ++i)
            for (typename InExtents::index_type j = 0; j < x.extent(1); ++j)
                z[i, j] = x[i, j] + y[i, j];
    }
}

// dot — [linalg.algs.blas1.dot]
template<class Extents, class Layout1, class Accessor1,
                        class Layout2, class Accessor2,
         class T>
T dot(
    std::mdspan<typename Accessor1::element_type, Extents, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents, Layout2, Accessor2> y,
    T init)
{
    using ElemT = std::remove_const_t<typename Accessor1::element_type>;
    using ElemT2 = std::remove_const_t<typename Accessor2::element_type>;
#if __LINALG_HAS_SIMD
    if constexpr (std::is_same_v<ElemT, ElemT2> &&
                  std::is_same_v<T, ElemT> &&
                  __detail::__can_simd_v<ElemT, Layout1, Accessor1> &&
                  __detail::__can_simd_v<ElemT, Layout2, Accessor2>) {
        using abi_t  = std::simd::native_abi<ElemT>;
        using simd_t = std::simd::basic_vec<ElemT, abi_t>;
        static constexpr auto kN = std::simd::simd_size<ElemT, abi_t>::value;
        const auto n = x.extent(0);
        typename Extents::index_type i = 0;
        const ElemT* px = x.data_handle();
        const ElemT* py = y.data_handle();
        ElemT acc = ElemT{};
        for (; i + static_cast<decltype(i)>(kN) <= n; i += kN) {
            simd_t vx{std::span<const ElemT, kN>{px + i, kN}};
            simd_t vy{std::span<const ElemT, kN>{py + i, kN}};
            acc += std::simd::reduce(vx * vy);
        }
        for (; i < n; ++i) acc += px[i] * py[i];
        return init + acc;
    }
#endif
    for (typename Extents::index_type i = 0; i < x.extent(0); ++i)
        init += static_cast<T>(x[i]) * static_cast<T>(y[i]);
    return init;
}

template<class Extents, class Layout1, class Accessor1,
                        class Layout2, class Accessor2>
auto dot(
    std::mdspan<typename Accessor1::element_type, Extents, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents, Layout2, Accessor2> y)
{
    using T = decltype(x[0] * y[0]);
    return dot(x, y, T{});
}

// dotc — [linalg.algs.blas1.dotc] (conjugate dot)
template<class Extents, class Layout1, class Accessor1,
                        class Layout2, class Accessor2,
         class T>
T dotc(
    std::mdspan<typename Accessor1::element_type, Extents, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents, Layout2, Accessor2> y,
    T init)
{
    for (typename Extents::index_type i = 0; i < x.extent(0); ++i) {
        auto xi = x[i];
        if constexpr (requires { xi.real(); xi.imag(); }) {
            init += static_cast<T>(std::conj(xi)) * static_cast<T>(y[i]);
        } else {
            init += static_cast<T>(xi) * static_cast<T>(y[i]);
        }
    }
    return init;
}

template<class Extents, class Layout1, class Accessor1,
                        class Layout2, class Accessor2>
auto dotc(
    std::mdspan<typename Accessor1::element_type, Extents, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents, Layout2, Accessor2> y)
{
    using T = decltype(x[0] * y[0]);
    return dotc(x, y, T{});
}

// vector_two_norm — [linalg.algs.blas1.nrm2]
template<class Extents, class Layout, class Accessor, class T>
T vector_two_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x,
    T init)
{
    T sum_sq = __detail::__norm_square_term_as<T>(init);
    for (typename Extents::index_type i = 0; i < x.extent(0); ++i) {
        sum_sq += __detail::__norm_square_term_as<T>(x[i]);
    }
    using std::sqrt;
    return sqrt(sum_sq);
}

template<class Extents, class Layout, class Accessor>
auto vector_two_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x)
{
    using std::abs;
    using std::sqrt;
    using T = decltype(abs(x[0]));
    using ElemT = std::remove_const_t<typename Accessor::element_type>;
#if __LINALG_HAS_SIMD
    if constexpr (!__detail::__is_complex_v<ElemT> &&
                  std::is_same_v<T, ElemT> &&
                  __detail::__can_simd_v<ElemT, Layout, Accessor>) {
        using abi_t  = std::simd::native_abi<ElemT>;
        using simd_t = std::simd::basic_vec<ElemT, abi_t>;
        static constexpr auto kN = std::simd::simd_size<ElemT, abi_t>::value;
        const auto n = x.extent(0);
        typename Extents::index_type i = 0;
        const ElemT* px = x.data_handle();
        ElemT sum_sq = ElemT{};
        for (; i + static_cast<decltype(i)>(kN) <= n; i += kN) {
            simd_t v{std::span<const ElemT, kN>{px + i, kN}};
            sum_sq += std::simd::reduce(v * v);
        }
        for (; i < n; ++i) { ElemT vi = px[i]; sum_sq += vi * vi; }
        return sqrt(static_cast<T>(sum_sq));
    }
#endif
    T sum_sq = T{};
    for (typename Extents::index_type i = 0; i < x.extent(0); ++i) {
        auto v = abs(x[i]);
        const T vt = static_cast<T>(v);
        sum_sq += vt * vt;
    }
    return sqrt(sum_sq);
}

// vector_abs_sum — [linalg.algs.blas1.asum]
template<class Extents, class Layout, class Accessor, class T>
T vector_abs_sum(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x,
    T init)
{
    for (typename Extents::index_type i = 0; i < x.extent(0); ++i)
        init += static_cast<T>(__detail::__abs_sum_term(x[i]));
    return init;
}

template<class Extents, class Layout, class Accessor>
auto vector_abs_sum(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x)
{
    using T = decltype(__detail::__abs_sum_term(x[0]));
    using ElemT = std::remove_const_t<typename Accessor::element_type>;
#if __LINALG_HAS_SIMD
    if constexpr (!__detail::__is_complex_v<ElemT> &&
                  __detail::__can_simd_v<ElemT, Layout, Accessor>) {
        using abi_t  = std::simd::native_abi<ElemT>;
        using simd_t = std::simd::basic_vec<ElemT, abi_t>;
        static constexpr auto kN = std::simd::simd_size<ElemT, abi_t>::value;
        const auto n = x.extent(0);
        typename Extents::index_type i = 0;
        const ElemT* px = x.data_handle();
        T acc = T{};
        for (; i + static_cast<decltype(i)>(kN) <= n; i += kN) {
            simd_t v{std::span<const ElemT, kN>{px + i, kN}};
            acc += static_cast<T>(std::simd::reduce(
                std::simd::basic_vec<ElemT, abi_t>{[&v](auto j) { return v[j] < ElemT{} ? -v[j] : v[j]; }}
            ));
        }
        for (; i < n; ++i) acc += abs(px[i]);
        return acc;
    }
#endif
    return vector_abs_sum(x, T{});
}

// vector_idx_abs_max — [linalg.algs.blas1.iamax]
template<class Extents, class Layout, class Accessor>
typename Extents::size_type vector_idx_abs_max(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x)
{
    using idx_t = typename Extents::index_type;
    using size_type = typename Extents::size_type;
    if (x.extent(0) == 0) return std::numeric_limits<size_type>::max();
    idx_t best = 0;
    auto best_v = __detail::__abs_sum_term(x[0]);
    for (idx_t i = 1; i < x.extent(0); ++i) {
        auto v = __detail::__abs_sum_term(x[i]);
        if (v > best_v) { best_v = v; best = i; }
    }
    return static_cast<size_type>(best);
}

// sum_of_squares helper struct — [linalg.algs.blas1.ssq]
template<class T>
struct sum_of_squares_result {
    T scaling_factor;
    T scaled_sum_of_squares;
};

template<class Extents, class Layout, class Accessor, class T>
sum_of_squares_result<T> vector_sum_of_squares(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x,
    sum_of_squares_result<T> init)
{
    using std::abs;
    T scale = init.scaling_factor;
    T ssq   = init.scaled_sum_of_squares;
    for (typename Extents::index_type i = 0; i < x.extent(0); ++i) {
        auto absxi = abs(x[i]);
        if (absxi != T{}) {
            if (scale < absxi) {
                ssq = T{1} + ssq * (scale / absxi) * (scale / absxi);
                scale = absxi;
            } else {
                ssq += (absxi / scale) * (absxi / scale);
            }
        }
    }
    return {scale, ssq};
}

// setup_givens_rotation — [linalg.algs.blas1.givens]
template<class T>
struct setup_givens_rotation_result {
    T c;
    T s;
    T r;
};

template<class T>
    requires (!__detail::__is_complex_v<T>)
setup_givens_rotation_result<T> setup_givens_rotation(T a, T b) {
    using std::abs;
    using std::sqrt;
    if (b == T{}) return {T{1}, T{0}, a};
    if (a == T{}) return {T{0}, T{1}, b};
    T r = (abs(a) > abs(b))
        ? abs(a) * sqrt(T{1} + (b/a)*(b/a))
        : abs(b) * sqrt(T{1} + (a/b)*(a/b));
    T c = a / r;
    T s = b / r;
    return {c, s, r};
}

template<class Real>
struct setup_givens_rotation_result<std::complex<Real>> {
    Real c;
    std::complex<Real> s;
    std::complex<Real> r;
};

template<class Real>
setup_givens_rotation_result<std::complex<Real>>
setup_givens_rotation(std::complex<Real> a, std::complex<Real> b) {
    using complex = std::complex<Real>;
    using std::abs;
    using std::conj;
    using std::norm;
    using std::sqrt;

    if (b == complex{}) return {Real{1}, complex{}, a};
    if (a == complex{}) return {Real{0}, complex{1}, b};

    const Real scale = abs(a) + abs(b);
    const complex scaled_a = a / scale;
    const complex scaled_b = b / scale;
    const Real rho = scale * sqrt(norm(scaled_a) + norm(scaled_b));
    const complex alpha = a / abs(a);
    const Real c = abs(a) / rho;
    const complex s = alpha * conj(b) / rho;
    const complex r = alpha * rho;
    return {c, s, r};
}

// apply_givens_rotation — [linalg.algs.blas1.givens]
template<class Extents, class Layout1, class Accessor1,
                        class Layout2, class Accessor2, class T>
void apply_givens_rotation(
    std::mdspan<typename Accessor1::element_type, Extents, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents, Layout2, Accessor2> y,
    T c, T s)
{
    for (typename Extents::index_type i = 0; i < x.extent(0); ++i) {
        auto xi = x[i];
        auto yi = y[i];
        x[i] = c * xi + s * yi;
        y[i] = -s * xi + c * yi;
    }
}

template<class Extents, class Layout1, class Accessor1,
                        class Layout2, class Accessor2, class Real>
void apply_givens_rotation(
    std::mdspan<std::complex<Real>, Extents, Layout1, Accessor1> x,
    std::mdspan<std::complex<Real>, Extents, Layout2, Accessor2> y,
    Real c, std::complex<Real> s)
{
    using std::conj;
    for (typename Extents::index_type i = 0; i < x.extent(0); ++i) {
        auto xi = x[i];
        auto yi = y[i];
        x[i] = c * xi + s * yi;
        y[i] = -conj(s) * xi + c * yi;
    }
}

// matrix_frob_norm — [linalg.algs.blas1.matfrobnorm]
template<class Extents, class Layout, class Accessor, class T>
T matrix_frob_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> A,
    T init)
{
    T sum = __detail::__norm_square_term_as<T>(init);
    for (typename Extents::index_type i = 0; i < A.extent(0); ++i) {
        for (typename Extents::index_type j = 0; j < A.extent(1); ++j) {
            sum += __detail::__norm_square_term_as<T>(A[i, j]);
        }
    }
    return std::sqrt(sum);
}

template<class Extents, class Layout, class Accessor>
auto matrix_frob_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> A)
{
    using std::abs;
    using T = decltype(abs(A[0, 0]));
    return matrix_frob_norm(A, T{});
}

// matrix_one_norm — [linalg.algs.blas1.matonenorm]
template<class Extents, class Layout, class Accessor, class T>
T matrix_one_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> A,
    T init)
{
    using std::abs;
    if (A.extent(1) == 0) {
        return init;
    }
    T max_col_sum{};
    for (typename Extents::index_type j = 0; j < A.extent(1); ++j) {
        T col_sum = T{};
        for (typename Extents::index_type i = 0; i < A.extent(0); ++i) {
            col_sum += static_cast<T>(abs(A[i, j]));
        }
        if (col_sum > max_col_sum) max_col_sum = col_sum;
    }
    return init + max_col_sum;
}

template<class Extents, class Layout, class Accessor>
auto matrix_one_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> A)
{
    using std::abs;
    using T = decltype(abs(A[0, 0]));
    return matrix_one_norm(A, T{});
}

// matrix_inf_norm — [linalg.algs.blas1.matinfnorm]
template<class Extents, class Layout, class Accessor, class T>
T matrix_inf_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> A,
    T init)
{
    using std::abs;
    if (A.extent(0) == 0) {
        return init;
    }
    T max_row_sum{};
    for (typename Extents::index_type i = 0; i < A.extent(0); ++i) {
        T row_sum = T{};
        for (typename Extents::index_type j = 0; j < A.extent(1); ++j) {
            row_sum += static_cast<T>(abs(A[i, j]));
        }
        if (row_sum > max_row_sum) max_row_sum = row_sum;
    }
    return init + max_row_sum;
}

template<class Extents, class Layout, class Accessor>
auto matrix_inf_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> A)
{
    using std::abs;
    using T = decltype(abs(A[0, 0]));
    return matrix_inf_norm(A, T{});
}

} // namespace std::linalg

#endif // defined(__cpp_lib_mdspan)

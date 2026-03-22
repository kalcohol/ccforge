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
#if __LINALG_HAS_SIMD
        if constexpr (__detail::__can_simd_v<ElemT, InLayout, InAccessor> &&
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
#if __LINALG_HAS_SIMD
    if constexpr (__detail::__can_simd_v<ElemT, Layout1, Accessor1> &&
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
        init += x[i] * y[i];
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
        using xi_t = std::remove_cvref_t<decltype(xi)>;
        if constexpr (requires { xi.real(); xi.imag(); }) {
            init += std::conj(xi) * y[i];
        } else {
            init += xi * y[i];
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
    using std::abs;
    T sum_sq = T{};
    for (typename Extents::index_type i = 0; i < x.extent(0); ++i) {
        auto v = abs(x[i]);
        sum_sq += v * v;
    }
    using std::sqrt;
    return init + sqrt(sum_sq);
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
    if constexpr (__detail::__can_simd_v<ElemT, Layout, Accessor>) {
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
        sum_sq += v * v;
    }
    return sqrt(sum_sq);
}

// vector_abs_sum — [linalg.algs.blas1.asum]
template<class Extents, class Layout, class Accessor, class T>
T vector_abs_sum(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x,
    T init)
{
    using std::abs;
    for (typename Extents::index_type i = 0; i < x.extent(0); ++i)
        init += abs(x[i]);
    return init;
}

template<class Extents, class Layout, class Accessor>
auto vector_abs_sum(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x)
{
    using std::abs;
    using T = decltype(abs(x[0]));
    using ElemT = std::remove_const_t<typename Accessor::element_type>;
#if __LINALG_HAS_SIMD
    if constexpr (__detail::__can_simd_v<ElemT, Layout, Accessor>) {
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
typename Extents::index_type vector_idx_abs_max(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x)
{
    using std::abs;
    using idx_t = typename Extents::index_type;
    if (x.extent(0) == 0) return 0;
    idx_t best = 0;
    auto best_v = abs(x[0]);
    for (idx_t i = 1; i < x.extent(0); ++i) {
        auto v = abs(x[i]);
        if (v > best_v) { best_v = v; best = i; }
    }
    return best;
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

// givens_rotation_setup — [linalg.algs.blas1.givens]
template<class T>
struct givens_rotation_result {
    T c;
    T s;
    T r;
};

template<class T>
givens_rotation_result<T> givens_rotation_setup(T a, T b) {
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

// givens_rotation_apply — [linalg.algs.blas1.givens]
template<class Extents, class Layout1, class Accessor1,
                        class Layout2, class Accessor2, class T>
void givens_rotation_apply(
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




} // namespace std::linalg

#endif // defined(__cpp_lib_mdspan)

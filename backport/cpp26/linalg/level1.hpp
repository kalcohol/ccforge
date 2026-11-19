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
#define FORGE_LINALG_BACKPORT_ACTIVE 1

#include <algorithm>
#include <limits>

namespace std::linalg {

// ──────────────────────────────────────────────────────────────────────────
// BLAS Level 1 — [linalg.algs.blas1]
// ──────────────────────────────────────────────────────────────────────────

// copy — [linalg.algs.blas1.copy]
template<class InExtents, class InLayout, class InAccessor,
         class OutExtents, class OutLayout, class OutAccessor>
    requires ((OutExtents::rank() == 1 || OutExtents::rank() == 2) &&
              __detail::__writable_output_v<OutExtents, OutLayout, OutAccessor> &&
              __detail::__compatible_static_extents_v<InExtents, OutExtents>)
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
            using abi_t  = typename std::simd::vec<ElemT>::abi_type;
            using simd_t = std::simd::basic_vec<ElemT, abi_t>;
            static constexpr auto kN = std::simd::basic_vec<ElemT, abi_t>::size();
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

// scale — [linalg.algs.blas1.scal]
template<class ScalingFactor,
         class Extents, class Layout, class Accessor>
    requires (__detail::__scalar<ScalingFactor> &&
              (Extents::rank() == 1 || Extents::rank() == 2) &&
              __detail::__writable_output_v<Extents, Layout, Accessor>)
void scale(
    ScalingFactor alpha,
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x)
{
    if constexpr (Extents::rank() == 1) {
        using ElemT = std::remove_const_t<typename Accessor::element_type>;
#if __LINALG_HAS_SIMD
        if constexpr (__detail::__can_simd_v<ElemT, Layout, Accessor> &&
                      std::is_same_v<ScalingFactor, ElemT>) {
            using abi_t  = typename std::simd::vec<ElemT>::abi_type;
            using simd_t = std::simd::basic_vec<ElemT, abi_t>;
            static constexpr auto kN = std::simd::basic_vec<ElemT, abi_t>::size();
            const auto n = x.extent(0);
            typename Extents::index_type i = 0;
            ElemT* px = x.data_handle();
            for (; i + static_cast<decltype(i)>(kN) <= n; i += kN) {
                simd_t v{std::span<const ElemT, kN>{px + i, kN}};
                v = simd_t{alpha} * v;
                for (std::ptrdiff_t j = 0; j < kN; ++j) px[i+j] = v[j];
            }
            for (; i < n; ++i) px[i] = alpha * px[i];
            return;
        }
#endif
        for (typename Extents::index_type i = 0; i < x.extent(0); ++i) {
            auto&& element = x[i];
            std::forward<decltype(element)>(element) =
                alpha * std::forward<decltype(element)>(element);
        }
    } else {
        for (typename Extents::index_type i = 0; i < x.extent(0); ++i)
            for (typename Extents::index_type j = 0; j < x.extent(1); ++j) {
                auto&& element = x[i, j];
                std::forward<decltype(element)>(element) =
                    alpha * std::forward<decltype(element)>(element);
            }
    }
}

// swap_elements — [linalg.algs.blas1.swap]
template<class Extents1, class Layout1, class Accessor1,
         class Extents2, class Layout2, class Accessor2>
    requires ((Extents1::rank() == 1 || Extents1::rank() == 2) &&
              __detail::__writable_output_v<Extents1, Layout1, Accessor1> &&
              __detail::__writable_output_v<Extents2, Layout2, Accessor2> &&
              __detail::__compatible_static_extents_v<Extents1, Extents2>)
void swap_elements(
    std::mdspan<typename Accessor1::element_type, Extents1, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents2, Layout2, Accessor2> y)
{
    if constexpr (Extents1::rank() == 1) {
        for (typename Extents1::index_type i = 0; i < x.extent(0); ++i) {
            __detail::__accessor_value_t<Accessor1> xi(std::move(x[i]));
            __detail::__accessor_value_t<Accessor2> yi(std::move(y[i]));
            x[i] = std::move(yi);
            y[i] = std::move(xi);
        }
    } else {
        for (typename Extents1::index_type i = 0; i < x.extent(0); ++i)
            for (typename Extents1::index_type j = 0; j < x.extent(1); ++j) {
                __detail::__accessor_value_t<Accessor1> xi(std::move(x[i, j]));
                __detail::__accessor_value_t<Accessor2> yi(std::move(y[i, j]));
                x[i, j] = std::move(yi);
                y[i, j] = std::move(xi);
            }
    }
}

// add — [linalg.algs.blas1.add]
template<class InExtents1, class InLayout1, class InAccessor1,
         class InExtents2, class InLayout2, class InAccessor2,
         class OutExtents, class OutLayout, class OutAccessor>
    requires ((OutExtents::rank() == 1 || OutExtents::rank() == 2) &&
              __detail::__writable_output_v<OutExtents, OutLayout, OutAccessor> &&
              __detail::__compatible_static_extents_v<InExtents1, InExtents2> &&
             __detail::__compatible_static_extents_v<InExtents1, OutExtents> &&
             __detail::__compatible_static_extents_v<InExtents2, OutExtents>)
void add(
    std::mdspan<typename InAccessor1::element_type, InExtents1, InLayout1, InAccessor1> x,
    std::mdspan<typename InAccessor2::element_type, InExtents2, InLayout2, InAccessor2> y,
    std::mdspan<typename OutAccessor::element_type, OutExtents, OutLayout, OutAccessor> z)
{
    if constexpr (InExtents1::rank() == 1) {
        for (typename InExtents1::index_type i = 0; i < x.extent(0); ++i)
            z[i] = x[i] + y[i];
    } else {
        for (typename InExtents1::index_type i = 0; i < x.extent(0); ++i)
            for (typename InExtents1::index_type j = 0; j < x.extent(1); ++j)
                z[i, j] = x[i, j] + y[i, j];
    }
}

// dot — [linalg.algs.blas1.dot]
template<class Extents1, class Layout1, class Accessor1,
         class Extents2, class Layout2, class Accessor2,
         class T>
    requires (__detail::__scalar<T> && Extents1::rank() == 1 && Extents2::rank() == 1 &&
              __detail::__compatible_static_extents_v<Extents1, Extents2>)
T dot(
    std::mdspan<typename Accessor1::element_type, Extents1, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents2, Layout2, Accessor2> y,
    T init)
{
    using ElemT = std::remove_const_t<typename Accessor1::element_type>;
    using ElemT2 = std::remove_const_t<typename Accessor2::element_type>;
    const auto n = x.extent(0);
    if (n == 0) return init;
#if __LINALG_HAS_SIMD
    if constexpr (std::is_same_v<ElemT, ElemT2> &&
                  std::is_same_v<T, ElemT> &&
                  requires {
                      requires std::is_same_v<T, decltype(
                          std::declval<ElemT>() + std::declval<ElemT2>())>;
                  } &&
                  __detail::__can_simd_v<ElemT, Layout1, Accessor1> &&
                  __detail::__can_simd_v<ElemT, Layout2, Accessor2>) {
        using abi_t  = typename std::simd::vec<ElemT>::abi_type;
        using simd_t = std::simd::basic_vec<ElemT, abi_t>;
        static constexpr auto kN = std::simd::basic_vec<ElemT, abi_t>::size();
        typename Extents1::index_type i = 0;
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
    using index_type = typename Extents1::index_type;
    auto with_product = [&](index_type i, auto&& consume) -> decltype(auto) {
        return consume(__detail::__dot_product<T,
            typename decltype(x)::value_type, typename decltype(y)::value_type>(x[i], y[i]));
    };
    using product_type = typename decltype(with_product(
        std::declval<index_type>(), __detail::__dot_product_type{}))::type;
    if constexpr (__detail::__use_numeric_dot_sum_v<T, product_type>) {
        return __detail::__dot_numeric_sum(init, n, with_product);
    } else {
        for (index_type i = 0; i < n; ++i) {
            with_product(i, [&](auto&& product) {
                init += std::forward<decltype(product)>(product);
            });
        }
        return init;
    }
}

template<class Extents1, class Layout1, class Accessor1,
         class Extents2, class Layout2, class Accessor2>
    requires (Extents1::rank() == 1 && Extents2::rank() == 1 &&
              __detail::__compatible_static_extents_v<Extents1, Extents2>)
auto dot(
    std::mdspan<typename Accessor1::element_type, Extents1, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents2, Layout2, Accessor2> y)
{
    using T = decltype(std::declval<typename decltype(x)::value_type>() *
                       std::declval<typename decltype(y)::value_type>());
    return dot(x, y, T{});
}

// dotc — [linalg.algs.blas1.dot] (conjugate dot)
template<class Extents1, class Layout1, class Accessor1,
         class Extents2, class Layout2, class Accessor2,
         class T>
    requires (__detail::__scalar<T> && Extents1::rank() == 1 && Extents2::rank() == 1 &&
              __detail::__compatible_static_extents_v<Extents1, Extents2>)
T dotc(
    std::mdspan<typename Accessor1::element_type, Extents1, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents2, Layout2, Accessor2> y,
    T init)
{
    return std::linalg::dot(std::linalg::conjugated(x), y, init);
}

template<class Extents1, class Layout1, class Accessor1,
         class Extents2, class Layout2, class Accessor2>
    requires (Extents1::rank() == 1 && Extents2::rank() == 1 &&
              __detail::__compatible_static_extents_v<Extents1, Extents2>)
auto dotc(
    std::mdspan<typename Accessor1::element_type, Extents1, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents2, Layout2, Accessor2> y)
{
    using T = decltype(__detail::__conj_if_needed(
                           std::declval<typename decltype(x)::value_type>()) *
                       std::declval<typename decltype(y)::value_type>());
    return dotc(x, y, T{});
}

namespace __detail {

// Retain the arithmetic used by Forge's program-defined magnitude types.
template<class Real>
class __linalg_norm_sum {
public:
    void __add(Real magnitude) {
        __update_scaled_sum_of_squares(magnitude, __scale, __sum);
    }

    auto __norm() const {
        using std::sqrt;
        return __scale * sqrt(__sum);
    }

private:
    Real __scale{};
    Real __sum{1};
};

template<std::floating_point Real>
class __linalg_norm_sum<Real> {
public:
    void __add(Real value) {
        if (value == Real{} || std::isnan(__sum)) return;
        if (!std::isfinite(value)) {
            __sum = std::abs(value);
            return;
        }
        if (std::isinf(__sum)) return;

        const int exponent = std::ilogb(value);
        if (__sum == Real{}) {
            __exponent = exponent;
        } else if (exponent > __exponent) {
            const int shift = __exponent - exponent;
            if (shift < __min_shift) {
                __sum = Real{};
                __error = Real{};
            } else {
                __sum = std::scalbn(__sum, 2 * shift);
                __error = std::scalbn(__error, 2 * shift);
            }
            __exponent = exponent;
        }

        const int shift = exponent - __exponent;
        if (shift < __min_shift) return;
        const Real scaled = std::scalbn(value, -__exponent);
        const Real square = scaled * scaled;
        const Real high = std::max(__sum, square);
        const Real low = std::min(__sum, square);
        const Real sum = high + low;
        const Real addition_error = (high - sum) + low;
        const Real product_error = std::fma(scaled, scaled, -square);
        const Real correction = (__error + addition_error) + product_error;
        // The correction is smaller than sum. Renormalize with FastTwoSum
        // so the low part stays bounded by the new sum's rounding error.
        __sum = sum + correction;
        __error = (sum - __sum) + correction;
    }

    int __scale_exponent() const { return __exponent; }

    Real __scaled_norm() const {
        if (__sum == Real{} || !std::isfinite(__sum)) return __sum;
        const Real root = std::sqrt(__sum);
        // Fold the low part into the root without rounding the sum again.
        return root + (std::fma(-root, root, __sum) + __error) / (Real{2} * root);
    }

    Real __norm() const { return std::scalbn(__scaled_norm(), __exponent); }

private:
    // Leave enough exponent range for the rounding error of a scaled square.
    // Terms below this scale are negligible even in an mdspan-sized sum.
    static constexpr int __min_shift =
        std::numeric_limits<Real>::min_exponent / 2 + std::numeric_limits<Real>::digits;
    int __exponent{};
    Real __sum{};
    Real __error{};
};

template<class T>
using __norm_precision_t = std::conditional_t<
    std::is_floating_point_v<__dot_component_t<T>>, __dot_component_t<T>, double>;

template<class Value, class T>
decltype(auto) __norm_read(T&& value) {
    // A numeric accessor result can carry more precision than value_type.
    if constexpr (std::is_arithmetic_v<std::remove_cvref_t<T>> || __is_complex_v<T>) {
        return std::forward<T>(value);
    } else if constexpr (!requires { __abs_if_needed(std::forward<T>(value)); }) {
        return static_cast<Value>(std::forward<T>(value));
    } else if constexpr (__is_complex_v<Value> &&
                         std::is_floating_point_v<__dot_component_t<Value>> &&
                         std::is_constructible_v<Value, T>) {
        using component = __dot_component_t<Value>;
        using magnitude = decltype(__abs_if_needed(std::forward<T>(value)));
        // Prefer a wider ADL magnitude to rounding the proxy's components.
        if constexpr (std::is_floating_point_v<magnitude> &&
                      (std::numeric_limits<magnitude>::digits >
                       std::numeric_limits<component>::digits ||
                       std::numeric_limits<magnitude>::max_exponent >
                       std::numeric_limits<component>::max_exponent)) {
            return __abs_if_needed(std::forward<T>(value));
        } else {
            return static_cast<Value>(std::forward<T>(value));
        }
    } else {
        return std::forward<T>(value);
    }
}

// A proxy's selected magnitude can be wider than its element type.
template<class Init, class Value, class Ref>
using __norm_accum_t = std::conditional_t<
    std::is_arithmetic_v<Init>,
    std::common_type_t<__norm_precision_t<Init>, __norm_precision_t<Value>,
                       __norm_precision_t<decltype(__abs_if_needed(
                           __norm_read<Value>(std::declval<Ref>())))>, double>,
    Init>;

template<class Value, class Real, class T>
void __add_norm_term(__linalg_norm_sum<Real>& sum, T&& value) {
    decltype(auto) read = __norm_read<Value>(std::forward<T>(value));
    if constexpr (std::is_floating_point_v<Real> && __is_complex_v<decltype(read)> &&
                  std::is_floating_point_v<__dot_component_t<decltype(read)>>) {
        const Real re = static_cast<Real>(read.real());
        const Real im = static_cast<Real>(read.imag());
        // A complex magnitude uses hypot's convention for infinity and NaN.
        if (!std::isfinite(re) || !std::isfinite(im)) {
            sum.__add(std::hypot(re, im));
        } else {
            sum.__add(re);
            sum.__add(im);
        }
    } else {
        sum.__add(__abs_if_needed_as<Real>(std::forward<decltype(read)>(read)));
    }
}

template<bool UseValueType, class Accessor>
auto __norm_default_init() {
    using value_type = __accessor_value_t<Accessor>;
    using reference = typename Accessor::reference;
    // The draft uses value_type. Forge also supports proxy-defined magnitudes.
    if constexpr (!UseValueType && requires { __abs_if_needed(std::declval<reference>()); }) {
        using magnitude = decltype(__abs_if_needed(std::declval<reference>()));
        return decltype(std::declval<magnitude>() * std::declval<magnitude>()){};
    } else {
        using magnitude = decltype(__abs_if_needed(std::declval<value_type>()));
        return decltype(std::declval<magnitude>() * std::declval<magnitude>()){};
    }
}

template<std::floating_point T, std::floating_point Real>
T __linalg_norm_cast(Real value) {
    if constexpr (std::numeric_limits<T>::max_exponent < std::numeric_limits<Real>::max_exponent ||
                  std::numeric_limits<T>::digits < std::numeric_limits<Real>::digits) {
        // Handle overflow before narrowing the result to its output format.
        if (std::abs(value) > std::numeric_limits<T>::max()) {
            constexpr T infinity = std::numeric_limits<T>::infinity();
            return std::signbit(value) ? -infinity : infinity;
        }
    }
    return static_cast<T>(value);
}

template<class T, class Real>
T __norm_result(const __linalg_norm_sum<Real>& sum) {
    if constexpr (std::is_integral_v<T>) {
        const Real norm = sum.__norm();
        // Saturate the nonnegative norm in its working precision before casting.
        if (std::isnan(norm)) return T{};
        constexpr T maximum = std::numeric_limits<T>::max();
        if (norm >= static_cast<Real>(maximum)) return maximum;
        return static_cast<T>(norm);
    } else if constexpr (std::is_floating_point_v<Real> &&
                         std::is_floating_point_v<__dot_component_t<T>>) {
        return static_cast<T>(__linalg_norm_cast<__dot_component_t<T>>(sum.__norm()));
    } else {
        return static_cast<T>(sum.__norm());
    }
}

} // namespace __detail

// vector_two_norm — [linalg.algs.blas1.nrm2]
// Borrow the public function parameters without copying their accessors or Scalar.
template<class Extents, class Layout, class Accessor, class T>
T __vector_two_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor>& x,
    T& init)
{
    using value_type = __detail::__accessor_value_t<Accessor>;
    if constexpr (
        std::is_integral_v<value_type> &&
        std::is_integral_v<std::remove_cvref_t<typename Accessor::reference>> &&
        std::is_integral_v<T>) {
        __detail::__integral_norm_accumulator accumulator;
        accumulator.__add(init);
        for (typename Extents::index_type i = 0; i < x.extent(0); ++i) {
            accumulator.__add(x[i]);
        }
        return __detail::__saturate_cast<T>(accumulator.__norm());
    } else {
        using magnitude_type = std::remove_cvref_t<decltype(__detail::__abs_if_needed(init))>;
        using accum_type = __detail::__norm_accum_t<magnitude_type, value_type, typename Accessor::reference>;
        __detail::__linalg_norm_sum<accum_type> sum;
        __detail::__add_norm_term<T>(sum, init);
        for (typename Extents::index_type i = 0; i < x.extent(0); ++i) {
            __detail::__add_norm_term<value_type>(sum, x[i]);
        }
        return __detail::__norm_result<T>(sum);
    }
}

template<bool UseValueType = false, class Extents, class Layout, class Accessor>
auto __vector_two_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x)
{
    auto init = __detail::__norm_default_init<UseValueType, Accessor>();
    return __vector_two_norm(x, init);
}

template<class Extents, class Layout, class Accessor, class T>
    requires (Extents::rank() == 1 &&
              __detail::__norm_arguments<__detail::__accessor_value_t<Accessor>, T>)
T vector_two_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x,
    T init)
{
    return std::linalg::__vector_two_norm(x, init);
}

template<class Extents, class Layout, class Accessor>
    requires (Extents::rank() == 1 && __detail::__norm_scalar<__detail::__accessor_value_t<Accessor>>)
auto vector_two_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x)
{
    return std::linalg::__vector_two_norm<true>(x);
}

// vector_abs_sum — [linalg.algs.blas1.asum]
template<class Extents, class Layout, class Accessor, class T>
    requires (Extents::rank() == 1 && __detail::__scalar<T>)
T vector_abs_sum(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x,
    T init)
{
    using value_type = typename decltype(x)::value_type;
    using sum_type = std::remove_cvref_t<
        decltype(__detail::__real_if_needed(init))>;
    using term_type = std::remove_cvref_t<
        decltype(__detail::__abs_sum_term<value_type>(x[0]))>;
    if constexpr (std::is_integral_v<T> && std::is_integral_v<term_type>) {
        std::uintmax_t magnitude{};
        for (typename Extents::index_type i = 0; i < x.extent(0); ++i) {
            __detail::__saturating_accumulate_magnitude(
                magnitude, __detail::__abs_sum_term<value_type>(x[i]));
        }
        return __detail::__saturating_add_magnitude(init, magnitude);
    } else if constexpr (std::is_integral_v<T>) {
        long double result = static_cast<long double>(init);
        for (typename Extents::index_type i = 0; i < x.extent(0); ++i) {
            result += __detail::__abs_sum_term_as<long double, value_type>(x[i]);
        }
        return __detail::__saturate_cast<T>(static_cast<double>(result));
    } else {
        for (typename Extents::index_type i = 0; i < x.extent(0); ++i) {
            init += static_cast<T>(
                __detail::__abs_sum_term_as<sum_type, value_type>(x[i]));
        }
        return init;
    }
}

template<class Extents, class Layout, class Accessor>
    requires (Extents::rank() == 1)
auto vector_abs_sum(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x)
{
    using T = typename decltype(x)::value_type;
    using ElemT = T;
#if __LINALG_HAS_SIMD
    // Integral reductions use the scalar widening-and-saturation policy;
    // reducing in an integral SIMD lane could wrap before widening.
    if constexpr (std::is_floating_point_v<ElemT> &&
                  __detail::__can_simd_v<ElemT, Layout, Accessor>) {
        using abi_t  = typename std::simd::vec<ElemT>::abi_type;
        using simd_t = std::simd::basic_vec<ElemT, abi_t>;
        static constexpr auto kN = std::simd::basic_vec<ElemT, abi_t>::size();
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
        for (; i < n; ++i) acc += __detail::__abs_if_needed(px[i]);
        return acc;
    }
#endif
    return vector_abs_sum(x, T{});
}

// vector_idx_abs_max — [linalg.algs.blas1.iamax]
template<class Extents, class Layout, class Accessor>
    requires (Extents::rank() == 1)
typename Extents::size_type vector_idx_abs_max(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x)
{
    using idx_t = typename Extents::index_type;
    using size_type = typename Extents::size_type;
    using value_type = typename decltype(x)::value_type;
    if (x.extent(0) == 0) return std::numeric_limits<size_type>::max();
    idx_t best = 0;
    auto best_v = __detail::__abs_sum_term<value_type>(x[0]);
    for (idx_t i = 1; i < x.extent(0); ++i) {
        auto v = __detail::__abs_sum_term<value_type>(x[i]);
        if (v > best_v) { best_v = v; best = i; }
    }
    return static_cast<size_type>(best);
}

// sum_of_squares helper struct — historical Forge compatibility
template<class T>
struct __sum_of_squares_result {
    T scaling_factor;
    T scaled_sum_of_squares;
};

template<class Extents, class Layout, class Accessor, class T>
__sum_of_squares_result<T> __vector_sum_of_squares(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor>& x,
    __sum_of_squares_result<T>& init)
{
    if constexpr (std::is_integral_v<T>) {
        // The scaled recurrence divides magnitudes, which truncates to zero
        // in an integral T ({3, 4} used to produce {4, 1}, reconstructing a
        // norm of 4 instead of 5). Integral inputs accumulate the raw sum
        // of squares in double against a unit scaling factor, preserving
        // the scaling_factor^2 * scaled_sum_of_squares contract exactly.
        const double init_scale = static_cast<double>(init.scaling_factor);
        double total = init_scale * init_scale *
            static_cast<double>(init.scaled_sum_of_squares);
        for (typename Extents::index_type i = 0; i < x.extent(0); ++i) {
            const double magnitude =
                static_cast<double>(__detail::__abs_if_needed(x[i]));
            total += magnitude * magnitude;
        }
        return {T{1}, __detail::__saturate_cast<T>(total)};
    } else {
        T scale = init.scaling_factor;
        T ssq   = init.scaled_sum_of_squares;
        for (typename Extents::index_type i = 0; i < x.extent(0); ++i) {
            auto absxi = __detail::__abs_if_needed_as<T>(x[i]);
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
}

// setup_givens_rotation — [linalg.algs.blas1.givens]
template<class T>
struct setup_givens_rotation_result {
    T c;
    T s;
    T r;
};

namespace __detail {

template<std::floating_point Real>
Real __rescale_givens_component(Real value, Real factor, int shift) {
    if (value == Real{}) return value;
    // A component can be much smaller than the scale of the rotation.
    const int exponent = std::ilogb(value);
    return std::scalbn(std::scalbn(value, -exponent) * factor, exponent + shift);
}

} // namespace __detail

template<class T>
    requires (!__detail::__is_complex_v<T>)
setup_givens_rotation_result<T> __setup_givens_rotation(T& a, T& b) noexcept {
    using std::abs;
    using std::sqrt;
    if (b == T{}) {
        const T r = abs(a);
        return {a < T{} ? T{-1} : T{1}, T{0}, r};
    }
    if (a == T{}) {
        const T r = abs(b);
        return {T{0}, b < T{} ? T{-1} : T{1}, r};
    }
    if constexpr (std::is_floating_point_v<T>) {
        const T nan = std::numeric_limits<T>::quiet_NaN();
        if (std::isnan(a) || std::isnan(b) || (std::isinf(a) && std::isinf(b))) {
            return {nan, nan, nan};
        }
        const T inf = std::numeric_limits<T>::infinity();
        // Keep the coefficients from dividing by the positive infinite norm.
        if (std::isinf(a)) return {nan, std::copysign(T{}, b), inf};
        if (std::isinf(b)) return {std::copysign(T{}, a), nan, inf};

        using work_type = std::common_type_t<T, double>;
        __detail::__linalg_norm_sum<work_type> sum;
        sum.__add(a);
        sum.__add(b);
        const work_type norm = sum.__scaled_norm();
        const int exponent = sum.__scale_exponent();
        const work_type inv_norm = work_type{1} / norm;
        return {
            static_cast<T>(__detail::__rescale_givens_component(work_type(a), inv_norm, -exponent)),
            static_cast<T>(__detail::__rescale_givens_component(work_type(b), inv_norm, -exponent)),
            __detail::__linalg_norm_cast<T>(std::scalbn(norm, exponent))
        };
    } else {
        T r = (abs(a) > abs(b))
            ? abs(a) * sqrt(T{1} + (b/a)*(b/a))
            : abs(b) * sqrt(T{1} + (a/b)*(a/b));
        T c = a / r;
        T s = b / r;
        return {c, s, r};
    }
}

template<class T>
    requires __detail::__givens_real<T>
setup_givens_rotation_result<T> setup_givens_rotation(T a, T b) noexcept {
    return std::linalg::__setup_givens_rotation(a, b);
}

template<class Real>
struct setup_givens_rotation_result<std::complex<Real>> {
    Real c;
    std::complex<Real> s;
    std::complex<Real> r;
};

namespace __detail {

template<std::floating_point Real>
setup_givens_rotation_result<std::complex<Real>>
__floating_complex_givens(const std::complex<Real>& a, const std::complex<Real>& b) {
    using complex = std::complex<Real>;
    using work_type = std::common_type_t<Real, double>;
    using work_complex = std::complex<work_type>;
    if (b == complex{}) return {Real{1}, complex{}, a};

    const Real nan = std::numeric_limits<Real>::quiet_NaN();
    if (std::isnan(a.real()) || std::isnan(a.imag()) ||
        std::isnan(b.real()) || std::isnan(b.imag())) {
        return {nan, {nan, nan}, {nan, nan}};
    }

    const bool a_infinite = std::isinf(a.real()) || std::isinf(a.imag());
    const bool b_infinite = std::isinf(b.real()) || std::isinf(b.imag());
    if (a_infinite) {
        if (b_infinite) return {nan, {nan, nan}, {nan, nan}};
        return {Real{1}, complex{}, a};
    }

    const bool a_zero = a == complex{};
    __linalg_norm_sum<work_type> a_norm;
    a_norm.__add(a.real());
    a_norm.__add(a.imag());
    const work_type abs_a = a_norm.__scaled_norm();
    const int a_exponent = a_norm.__scale_exponent();
    const work_complex phase = a_zero ? work_complex{1} : work_complex{
        std::scalbn(work_type(a.real()), -a_exponent) / abs_a,
        std::scalbn(work_type(a.imag()), -a_exponent) / abs_a
    };

    if (b_infinite) {
        const auto direction = [](Real x) {
            return std::copysign(std::isinf(x) ? work_type{1} : work_type{}, work_type(x));
        };
        const work_type re = direction(b.real());
        const work_type im = direction(b.imag());
        const work_complex conjugate_direction{re, -im};
        const work_complex s = (a_zero ? conjugate_direction : phase * conjugate_direction)
                               / std::hypot(re, im);
        const auto infinite_component = [](Real x) {
            return x == Real{} ? x : std::copysign(std::numeric_limits<Real>::infinity(), x);
        };
        const complex r = a_zero
            ? complex{std::numeric_limits<Real>::infinity()}
            : complex{infinite_component(a.real()), infinite_component(a.imag())};
        return {Real{}, {static_cast<Real>(s.real()), static_cast<Real>(s.imag())}, r};
    }

    auto sum = a_norm;
    sum.__add(b.real());
    sum.__add(b.imag());
    const work_type norm = sum.__scaled_norm();
    const int exponent = sum.__scale_exponent();

    const int b_exponent = std::ilogb(std::max(std::abs(b.real()), std::abs(b.imag())));
    const work_complex scaled_b{
        std::scalbn(work_type(b.real()), -b_exponent),
        std::scalbn(work_type(b.imag()), -b_exponent)
    };
    const work_complex conjugate_b = std::conj(scaled_b);
    const work_complex rotated_b = a_zero ? conjugate_b : phase * conjugate_b;
    const complex s{
        static_cast<Real>(std::scalbn(rotated_b.real() / norm, b_exponent - exponent)),
        static_cast<Real>(std::scalbn(rotated_b.imag() / norm, b_exponent - exponent))
    };
    if (a_zero) return {Real{}, s, {__linalg_norm_cast<Real>(std::scalbn(norm, exponent)), Real{}}};

    const work_type c = std::scalbn(abs_a / norm, a_exponent - exponent);
    // Rescale the two result components independently. The length itself
    // can overflow even when both components of r are representable.
    const work_type factor = norm / abs_a;
    const int shift = exponent - a_exponent;
    const complex r{
        __linalg_norm_cast<Real>(__rescale_givens_component(work_type(a.real()), factor, shift)),
        __linalg_norm_cast<Real>(__rescale_givens_component(work_type(a.imag()), factor, shift))
    };
    return {static_cast<Real>(c), s, r};
}

} // namespace __detail

template<class Real>
setup_givens_rotation_result<std::complex<Real>>
__setup_givens_rotation_complex(std::complex<Real> a, std::complex<Real> b) noexcept {
    if constexpr (std::is_floating_point_v<Real>) {
        return __detail::__floating_complex_givens(a, b);
    } else {
        using complex = std::complex<Real>;
        using std::abs;
        using std::conj;
        using std::norm;
        using std::sqrt;

        if (b == complex{}) return {Real{1}, complex{}, a};
        if (a == complex{}) {
            const Real scale = std::max(abs(b.real()), abs(b.imag()));
            const complex scaled_b{b.real() / scale, b.imag() / scale};
            const Real scaled_norm = sqrt(norm(scaled_b));
            // Normalize the phase independently of the rounded norm for subnormal b.
            const complex s = conj(scaled_b) / scaled_norm;
            return {Real{0}, s, complex{std::hypot(b.real(), b.imag()), Real{0}}};
        }

        const Real abs_a = abs(a);
        const Real abs_b = abs(b);
        const Real scale = std::max(abs_a, abs_b);
        const complex scaled_a = a / scale;
        const complex scaled_b = b / scale;
        const Real rho = scale * sqrt(norm(scaled_a) + norm(scaled_b));
        const complex alpha = a / abs_a;
        const Real c = abs_a / rho;
        const complex s = alpha * conj(b) / rho;
        const complex r = alpha * rho;
        return {c, s, r};
    }
}

template<class Real>
    requires __detail::__givens_real<Real>
setup_givens_rotation_result<std::complex<Real>>
setup_givens_rotation(std::complex<Real> a, std::complex<Real> b) noexcept {
    return std::linalg::__setup_givens_rotation_complex(a, b);
}

// apply_givens_rotation — [linalg.algs.blas1.givens]
template<class Extents1, class Layout1, class Accessor1,
         class Extents2, class Layout2, class Accessor2, class T>
    requires (__detail::__writable_output_v<Extents1, Layout1, Accessor1> &&
              __detail::__writable_output_v<Extents2, Layout2, Accessor2> &&
              __detail::__compatible_static_extents_v<Extents1, Extents2>)
void __apply_givens_rotation(
    std::mdspan<typename Accessor1::element_type, Extents1, Layout1, Accessor1>& x,
    std::mdspan<typename Accessor2::element_type, Extents2, Layout2, Accessor2>& y,
    T& c, T& s)
{
    for (typename Extents1::index_type i = 0; i < x.extent(0); ++i) {
        __detail::__accessor_value_t<Accessor1> xi = x[i];
        __detail::__accessor_value_t<Accessor2> yi = y[i];
        x[i] = c * xi + s * yi;
        y[i] = -s * xi + c * yi;
    }
}

template<class Extents1, class Layout1, class Accessor1,
         class Extents2, class Layout2, class Accessor2, class T>
    requires (__detail::__givens_real<T> && Extents1::rank() == 1 &&
              Extents2::rank() == 1 && __detail::__writable_output_v<Extents1, Layout1, Accessor1> &&
              __detail::__writable_output_v<Extents2, Layout2, Accessor2> &&
              __detail::__compatible_static_extents_v<Extents1, Extents2>)
void apply_givens_rotation(
    std::mdspan<typename Accessor1::element_type, Extents1, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents2, Layout2, Accessor2> y,
    T c, T s)
{
    std::linalg::__apply_givens_rotation(x, y, c, s);
}

template<class Extents1, class Layout1, class Accessor1,
         class Extents2, class Layout2, class Accessor2, class Real>
    requires (__detail::__givens_real<Real> && Extents1::rank() == 1 &&
              Extents2::rank() == 1 && __detail::__writable_output_v<Extents1, Layout1, Accessor1> &&
              __detail::__writable_output_v<Extents2, Layout2, Accessor2> &&
              __detail::__compatible_static_extents_v<Extents1, Extents2>)
void apply_givens_rotation(
    std::mdspan<typename Accessor1::element_type, Extents1, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents2, Layout2, Accessor2> y,
    Real c, std::complex<Real> s)
{
    using std::conj;
    for (typename Extents1::index_type i = 0; i < x.extent(0); ++i) {
        __detail::__accessor_value_t<Accessor1> xi = x[i];
        __detail::__accessor_value_t<Accessor2> yi = y[i];
        x[i] = c * xi + s * yi;
        y[i] = -conj(s) * xi + c * yi;
    }
}

// matrix_frob_norm — [linalg.algs.blas1.matfrobnorm]
template<class Extents, class Layout, class Accessor, class T>
T __matrix_frob_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor>& A,
    T& init)
{
    using value_type = __detail::__accessor_value_t<Accessor>;
    if constexpr (
        std::is_integral_v<value_type> &&
        std::is_integral_v<std::remove_cvref_t<typename Accessor::reference>> &&
        std::is_integral_v<T>) {
        __detail::__integral_norm_accumulator accumulator;
        accumulator.__add(init);
        for (typename Extents::index_type i = 0; i < A.extent(0); ++i) {
            for (typename Extents::index_type j = 0; j < A.extent(1); ++j) {
                accumulator.__add(A[i, j]);
            }
        }
        return __detail::__saturate_cast<T>(accumulator.__norm());
    } else {
        using magnitude_type = std::remove_cvref_t<decltype(__detail::__abs_if_needed(init))>;
        using accum_type = __detail::__norm_accum_t<magnitude_type, value_type, typename Accessor::reference>;
        __detail::__linalg_norm_sum<accum_type> sum;
        __detail::__add_norm_term<T>(sum, init);
        for (typename Extents::index_type i = 0; i < A.extent(0); ++i) {
            for (typename Extents::index_type j = 0; j < A.extent(1); ++j) {
                __detail::__add_norm_term<value_type>(sum, A[i, j]);
            }
        }
        return __detail::__norm_result<T>(sum);
    }
}

template<bool UseValueType = false, class Extents, class Layout, class Accessor>
auto __matrix_frob_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> A)
{
    auto init = __detail::__norm_default_init<UseValueType, Accessor>();
    return __matrix_frob_norm(A, init);
}

// Public wrappers borrow their parameters in the shared implementation.
template<class Extents, class Layout, class Accessor, class T>
    requires (Extents::rank() == 2 &&
              __detail::__norm_arguments<__detail::__accessor_value_t<Accessor>, T>)
T matrix_frob_norm(std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x, T init) {
    return std::linalg::__matrix_frob_norm(x, init);
}

template<class Extents, class Layout, class Accessor>
    requires (Extents::rank() == 2 && __detail::__norm_scalar<__detail::__accessor_value_t<Accessor>>)
auto matrix_frob_norm(std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x) {
    return std::linalg::__matrix_frob_norm<true>(x);
}

// matrix_one_norm — [linalg.algs.blas1.matonenorm]
template<class Extents, class Layout, class Accessor, class T>
    requires (Extents::rank() == 2 && __detail::__scalar<T>)
T matrix_one_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> A,
    T init)
{
    if (A.extent(1) == 0) {
        return init;
    }
    using sum_type = std::remove_cvref_t<
        decltype(__detail::__real_if_needed(init))>;
    using magnitude_type = std::remove_cvref_t<
        decltype(__detail::__abs_if_needed(A[0, 0]))>;
    if constexpr (std::is_integral_v<T> &&
                  std::is_integral_v<magnitude_type>) {
        std::uintmax_t max_col_sum{};
        for (typename Extents::index_type j = 0; j < A.extent(1); ++j) {
            std::uintmax_t col_sum{};
            for (typename Extents::index_type i = 0; i < A.extent(0); ++i) {
                __detail::__saturating_accumulate_magnitude(
                    col_sum, __detail::__abs_if_needed(A[i, j]));
            }
            if (col_sum > max_col_sum) max_col_sum = col_sum;
        }
        return __detail::__saturating_add_magnitude(init, max_col_sum);
    }
    using accum_type = std::conditional_t<
        std::is_integral_v<sum_type> || std::is_integral_v<magnitude_type>,
        double,
        sum_type>;
    accum_type max_col_sum{};
    for (typename Extents::index_type j = 0; j < A.extent(1); ++j) {
        accum_type col_sum{};
        for (typename Extents::index_type i = 0; i < A.extent(0); ++i) {
            col_sum += __detail::__abs_if_needed_as<accum_type>(A[i, j]);
        }
        if (col_sum > max_col_sum) max_col_sum = col_sum;
    }
    if constexpr (std::is_integral_v<T>) {
        return __detail::__saturate_cast<T>(
            static_cast<double>(init) + static_cast<double>(max_col_sum));
    } else {
        return init + static_cast<T>(max_col_sum);
    }
}

template<class Extents, class Layout, class Accessor>
    requires (Extents::rank() == 2)
auto matrix_one_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> A)
{
    using T = decltype(__detail::__abs_if_needed(A[0, 0]));
    return matrix_one_norm(A, T{});
}

// matrix_inf_norm — [linalg.algs.blas1.matinfnorm]
template<class Extents, class Layout, class Accessor, class T>
    requires (Extents::rank() == 2 && __detail::__scalar<T>)
T matrix_inf_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> A,
    T init)
{
    if (A.extent(0) == 0) {
        return init;
    }
    using sum_type = std::remove_cvref_t<
        decltype(__detail::__real_if_needed(init))>;
    using magnitude_type = std::remove_cvref_t<
        decltype(__detail::__abs_if_needed(A[0, 0]))>;
    if constexpr (std::is_integral_v<T> &&
                  std::is_integral_v<magnitude_type>) {
        std::uintmax_t max_row_sum{};
        for (typename Extents::index_type i = 0; i < A.extent(0); ++i) {
            std::uintmax_t row_sum{};
            for (typename Extents::index_type j = 0; j < A.extent(1); ++j) {
                __detail::__saturating_accumulate_magnitude(
                    row_sum, __detail::__abs_if_needed(A[i, j]));
            }
            if (row_sum > max_row_sum) max_row_sum = row_sum;
        }
        return __detail::__saturating_add_magnitude(init, max_row_sum);
    }
    using accum_type = std::conditional_t<
        std::is_integral_v<sum_type> || std::is_integral_v<magnitude_type>,
        double,
        sum_type>;
    accum_type max_row_sum{};
    for (typename Extents::index_type i = 0; i < A.extent(0); ++i) {
        accum_type row_sum{};
        for (typename Extents::index_type j = 0; j < A.extent(1); ++j) {
            row_sum += __detail::__abs_if_needed_as<accum_type>(A[i, j]);
        }
        if (row_sum > max_row_sum) max_row_sum = row_sum;
    }
    if constexpr (std::is_integral_v<T>) {
        return __detail::__saturate_cast<T>(
            static_cast<double>(init) + static_cast<double>(max_row_sum));
    } else {
        return init + static_cast<T>(max_row_sum);
    }
}

template<class Extents, class Layout, class Accessor>
    requires (Extents::rank() == 2)
auto matrix_inf_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> A)
{
    using T = decltype(__detail::__abs_if_needed(A[0, 0]));
    return matrix_inf_norm(A, T{});
}

} // namespace std::linalg

#endif // defined(__cpp_lib_mdspan)

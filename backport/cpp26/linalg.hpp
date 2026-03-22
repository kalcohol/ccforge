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

#if __cplusplus < 202002L
#error "Forge <linalg> backport requires C++20 or newer"
#endif

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


// Optional SIMD acceleration using Forge simd backport
#if __has_include(<simd>)
#include <simd>
#endif

// Macro: 1 if Forge simd backport was loaded
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

// ──────────────────────────────────────────────────────────────────────────
// Tag types — [linalg.tags]
// ──────────────────────────────────────────────────────────────────────────

struct column_major_t { explicit column_major_t() = default; };
struct row_major_t    { explicit row_major_t()    = default; };

inline constexpr column_major_t column_major{};
inline constexpr row_major_t    row_major{};

struct upper_triangle_t { explicit upper_triangle_t() = default; };
struct lower_triangle_t { explicit lower_triangle_t() = default; };

inline constexpr upper_triangle_t upper_triangle{};
inline constexpr lower_triangle_t lower_triangle{};

struct implicit_unit_diagonal_t { explicit implicit_unit_diagonal_t() = default; };
struct explicit_diagonal_t      { explicit explicit_diagonal_t()      = default; };

inline constexpr implicit_unit_diagonal_t implicit_unit_diagonal{};
inline constexpr explicit_diagonal_t      explicit_diagonal{};

// ──────────────────────────────────────────────────────────────────────────
// layout_blas_packed — [linalg.layout.packed]
// Packed storage layout for triangular/symmetric/hermitian matrices.
// ──────────────────────────────────────────────────────────────────────────

template<class Triangle, class StorageOrder>
struct layout_blas_packed {
    template<class Extents>
    struct mapping {
        using extents_type = Extents;
        using index_type   = typename Extents::index_type;
        using size_type    = typename Extents::size_type;
        using rank_type    = typename Extents::rank_type;
        using layout_type  = layout_blas_packed;

        static_assert(Extents::rank() == 2,
            "layout_blas_packed requires 2D extents");

        mapping() noexcept = default;

        explicit mapping(const Extents& e) noexcept : extents_(e) {}

        [[nodiscard]] constexpr const Extents& extents() const noexcept {
            return extents_;
        }

        [[nodiscard]] constexpr size_type required_span_size() const noexcept {
            const auto n = extents_.extent(0);
            return n * (n + 1) / 2;
        }

        [[nodiscard]] constexpr index_type operator()(index_type i, index_type j) const noexcept {
            if constexpr (std::is_same_v<StorageOrder, column_major_t>) {
                if constexpr (std::is_same_v<Triangle, upper_triangle_t>) {
                    if (j < i) std::swap(i, j);
                    return j * (j + 1) / 2 + i;
                } else {
                    const auto n = extents_.extent(0);
                    if (i < j) std::swap(i, j);
                    return i + n * j - j * (j + 1) / 2;
                }
            } else {
                if constexpr (std::is_same_v<Triangle, upper_triangle_t>) {
                    if (j < i) std::swap(i, j);
                    const auto n = extents_.extent(0);
                    return i * n - i * (i - 1) / 2 + j - i;
                } else {
                    if (i < j) std::swap(i, j);
                    return i * (i + 1) / 2 + j;
                }
            }
        }

        [[nodiscard]] constexpr bool is_unique()            const noexcept { return false; }
        [[nodiscard]] static constexpr bool is_always_unique()            noexcept { return false; }
        [[nodiscard]] static constexpr bool is_always_exhaustive()        noexcept { return true;  }
        [[nodiscard]] static constexpr bool is_always_strided()           noexcept { return false; }
        [[nodiscard]] constexpr bool is_exhaustive()        const noexcept { return true;  }
        [[nodiscard]] constexpr bool is_strided()           const noexcept { return false; }

        friend bool operator==(const mapping&, const mapping&) noexcept = default;

    private:
        [[no_unique_address]] Extents extents_{};
    };
};

// ──────────────────────────────────────────────────────────────────────────
// scaled_accessor — [linalg.scaled.accessor]
// ──────────────────────────────────────────────────────────────────────────

template<class ScalingFactor, class NestedAccessor>
class scaled_accessor {
public:
    using element_type     = std::add_const_t<
        decltype(std::declval<ScalingFactor>() *
                 std::declval<typename NestedAccessor::element_type>())>;
    using reference        = element_type;
    using data_handle_type = typename NestedAccessor::data_handle_type;
    using offset_policy    = scaled_accessor<ScalingFactor,
                                 typename NestedAccessor::offset_policy>;

    scaled_accessor() noexcept = default;

    scaled_accessor(ScalingFactor s, NestedAccessor a)
        : scaling_factor_(std::move(s)), nested_(std::move(a)) {}

    [[nodiscard]] reference access(data_handle_type p, std::ptrdiff_t i) const {
        return scaling_factor_ * nested_.access(p, i);
    }

    [[nodiscard]] typename offset_policy::data_handle_type
    offset(data_handle_type p, std::ptrdiff_t i) const {
        return nested_.offset(p, i);
    }

    [[nodiscard]] const NestedAccessor& nested_accessor() const noexcept {
        return nested_;
    }
    [[nodiscard]] const ScalingFactor& scaling_factor() const noexcept {
        return scaling_factor_;
    }

private:
    ScalingFactor scaling_factor_;
    NestedAccessor nested_;
};

// ──────────────────────────────────────────────────────────────────────────
// conjugated_accessor — [linalg.conj.accessor]
// ──────────────────────────────────────────────────────────────────────────

namespace __detail {
template<class T>
struct __conj_element {
    using type = T;
    static T apply(T v) noexcept { return v; }
};
template<class T>
struct __conj_element<std::complex<T>> {
    using type = std::complex<T>;
    static std::complex<T> apply(std::complex<T> v) noexcept {
        return std::conj(v);
    }
};
} // namespace __detail

template<class NestedAccessor>
class conjugated_accessor {
public:
    using element_type     = std::add_const_t<
        typename __detail::__conj_element<
            std::remove_const_t<typename NestedAccessor::element_type>>::type>;
    using reference        = element_type;
    using data_handle_type = typename NestedAccessor::data_handle_type;
    using offset_policy    = conjugated_accessor<typename NestedAccessor::offset_policy>;

    conjugated_accessor() noexcept = default;
    explicit conjugated_accessor(NestedAccessor a) : nested_(std::move(a)) {}

    [[nodiscard]] reference access(data_handle_type p, std::ptrdiff_t i) const {
        return __detail::__conj_element<
            std::remove_const_t<typename NestedAccessor::element_type>
        >::apply(nested_.access(p, i));
    }

    [[nodiscard]] typename offset_policy::data_handle_type
    offset(data_handle_type p, std::ptrdiff_t i) const {
        return nested_.offset(p, i);
    }

    [[nodiscard]] const NestedAccessor& nested_accessor() const noexcept {
        return nested_;
    }

private:
    NestedAccessor nested_;
};

// ──────────────────────────────────────────────────────────────────────────
// layout_transpose — [linalg.layout.transpose]
// ──────────────────────────────────────────────────────────────────────────

template<class Layout>
class layout_transpose {
public:
    template<class Extents>
    struct mapping {
        static_assert(Extents::rank() == 2, "layout_transpose requires 2D extents");

        using extents_type = Extents;
        using index_type   = typename Extents::index_type;
        using size_type    = typename Extents::size_type;
        using rank_type    = typename Extents::rank_type;
        using layout_type  = layout_transpose;

        using __transposed_extents_type = std::extents<
            index_type, Extents::static_extent(1), Extents::static_extent(0)>;
        using __nested_mapping_type = typename Layout::template mapping<__transposed_extents_type>;

        mapping() noexcept = default;

        template<class OtherExtents>
        explicit mapping(const typename Layout::template mapping<OtherExtents>& m)
            : nested_(__transposed_extents_type(m.extents().extent(1), m.extents().extent(0)))
            , extents_(m.extents().extent(1), m.extents().extent(0))
        {}

        [[nodiscard]] constexpr const Extents& extents() const noexcept {
            return extents_;
        }

        [[nodiscard]] constexpr size_type required_span_size() const noexcept {
            return nested_.required_span_size();
        }

        [[nodiscard]] constexpr index_type operator()(index_type i, index_type j) const noexcept {
            return nested_(j, i);
        }

        [[nodiscard]] constexpr bool is_unique()     const noexcept { return nested_.is_unique();     }
        [[nodiscard]] constexpr bool is_exhaustive() const noexcept { return nested_.is_exhaustive(); }
        [[nodiscard]] constexpr bool is_strided()    const noexcept { return nested_.is_strided();    }
        [[nodiscard]] static constexpr bool is_always_unique()     noexcept { return __nested_mapping_type::is_always_unique();     }
        [[nodiscard]] static constexpr bool is_always_exhaustive() noexcept { return __nested_mapping_type::is_always_exhaustive(); }
        [[nodiscard]] static constexpr bool is_always_strided()    noexcept { return __nested_mapping_type::is_always_strided();    }

        [[nodiscard]] constexpr index_type stride(rank_type r) const {
            return nested_.stride(r == 0 ? 1 : 0);
        }

        friend bool operator==(const mapping&, const mapping&) noexcept = default;

    private:
        __nested_mapping_type nested_{};
        Extents extents_{};
    };
};

// ──────────────────────────────────────────────────────────────────────────
// Utility view functions — [linalg.helpers]
// ──────────────────────────────────────────────────────────────────────────

template<class ScalingFactor,
         class ElementType, class Extents, class Layout, class Accessor>
[[nodiscard]] auto scaled(
    ScalingFactor alpha,
    std::mdspan<ElementType, Extents, Layout, Accessor> x)
{
    using acc_t = scaled_accessor<ScalingFactor, Accessor>;
    return std::mdspan<
        typename acc_t::element_type, Extents, Layout, acc_t>(
            x.data_handle(), x.mapping(),
            acc_t(std::move(alpha), x.accessor()));
}

template<class ElementType, class Extents, class Layout, class Accessor>
[[nodiscard]] auto conjugated(
    std::mdspan<ElementType, Extents, Layout, Accessor> x)
{
    using acc_t = conjugated_accessor<Accessor>;
    return std::mdspan<
        typename acc_t::element_type, Extents, Layout, acc_t>(
            x.data_handle(), x.mapping(), acc_t(x.accessor()));
}

template<class ElementType, class Extents, class Layout, class Accessor>
[[nodiscard]] auto transposed(
    std::mdspan<ElementType, Extents, Layout, Accessor> x)
{
    static_assert(Extents::rank() == 2, "transposed requires a 2D mdspan");
    using layout_t = layout_transpose<Layout>;
    using index_t = typename Extents::index_type;
    using new_extents_t = std::extents<
        index_t, Extents::static_extent(1), Extents::static_extent(0)>;
    typename layout_t::template mapping<new_extents_t> m(x.mapping());
    return std::mdspan<ElementType, new_extents_t, layout_t, Accessor>(
        x.data_handle(), m, x.accessor());
}

template<class ElementType, class Extents, class Layout, class Accessor>
[[nodiscard]] auto conjugate_transposed(
    std::mdspan<ElementType, Extents, Layout, Accessor> x)
{
    return conjugated(transposed(x));
}


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
    for (idx_t i = 0; i < n; ++i) {
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
    for (idx_t i = 0; i < n; ++i) y[i] = typename YAccessor::element_type{};
    for (idx_t i = 0; i < n; ++i) {
        y[i] += A[i, i] * x[i];
        for (idx_t j = i + 1; j < n; ++j) {
            y[i] += A[i, j] * x[j];
            y[j] += A[i, j] * x[i];
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
    for (idx_t i = 0; i < n; ++i) y[i] = typename YAccessor::element_type{};
    for (idx_t i = 0; i < n; ++i) {
        y[i] += A[i, i] * x[i];
        for (idx_t j = i + 1; j < n; ++j) {
            using std::conj;
            y[i] += A[i, j] * x[j];
            y[j] += conj(A[i, j]) * x[i];
        }
    }
}

// matrix_rank_1_update — A += x * y^T  [linalg.algs.blas2.rank1]
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
            A[i, j] += x[i] * y[j];
}

// matrix_rank_1_update_c — A += x * conj(y^T)  [linalg.algs.blas2.rank1]
template<class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class AExtents, class ALayout, class AAccessor>
void matrix_rank_1_update_c(
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A)
{
    using std::conj;
    using idx_t = typename AExtents::index_type;
    for (idx_t i = 0; i < A.extent(0); ++i)
        for (idx_t j = 0; j < A.extent(1); ++j)
            A[i, j] += x[i] * conj(y[j]);
}

// symmetric_matrix_rank_1_update — A += alpha * x * x^T  [linalg.algs.blas2.syr]
template<class ScalingFactor, class Triangle,
         class XExtents, class XLayout, class XAccessor,
         class AExtents, class ALayout, class AAccessor>
void symmetric_matrix_rank_1_update(
    ScalingFactor alpha, Triangle,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A)
{
    using idx_t = typename AExtents::index_type;
    const idx_t n = A.extent(0);
    for (idx_t i = 0; i < n; ++i)
        for (idx_t j = i; j < n; ++j)
            A[i, j] += alpha * x[i] * x[j];
}

// symmetric_matrix_rank_2_update — A += alpha*(x*y^T + y*x^T)  [linalg.algs.blas2.syr2]
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
    for (idx_t i = 0; i < n; ++i)
        for (idx_t j = i; j < n; ++j)
            A[i, j] += alpha * (x[i] * y[j] + y[i] * x[j]);
}

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

// symmetric_matrix_product — C += alpha * A * B + alpha * B * A^T (symmetric)  [linalg.algs.blas3.symm]
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
    for (idx_t i = 0; i < m; ++i)
        for (idx_t j = 0; j < n; ++j) {
            typename CAccessor::element_type sum{};
            for (idx_t l = 0; l < k; ++l)
                sum += A[i, l] * B[l, j];
            C[i, j] = sum;
        }
}

// matrix_rank_k_update — C += alpha * A * A^T  [linalg.algs.blas3.syrk]
template<class Triangle, class ScalingFactor,
         class AExtents, class ALayout, class AAccessor,
         class CExtents, class CLayout, class CAccessor>
void symmetric_matrix_rank_k_update(
    ScalingFactor alpha, Triangle,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    using idx_t = typename CExtents::index_type;
    const idx_t n = C.extent(0), k = A.extent(1);
    for (idx_t i = 0; i < n; ++i)
        for (idx_t j = i; j < n; ++j) {
            typename CAccessor::element_type sum{};
            for (idx_t l = 0; l < k; ++l)
                sum += A[i, l] * A[j, l];
            C[i, j] += alpha * sum;
            if (i != j) C[j, i] += alpha * sum;
        }
}

// matrix_rank_2k_update — C += alpha * A * B^T + alpha * B * A^T  [linalg.algs.blas3.syr2k]
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
    for (idx_t i = 0; i < n; ++i)
        for (idx_t j = i; j < n; ++j) {
            typename CAccessor::element_type sum{};
            for (idx_t l = 0; l < k; ++l)
                sum += A[i, l] * B[j, l] + B[i, l] * A[j, l];
            C[i, j] += alpha * sum;
            if (i != j) C[j, i] += alpha * sum;
        }
}


} // namespace std::linalg

#endif // defined(__cpp_lib_mdspan)

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

#if defined(__cpp_lib_mdspan)

namespace std::linalg {

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

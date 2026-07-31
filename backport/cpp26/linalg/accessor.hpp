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

#include "tags_layout.hpp"
#include <array>
#if defined(__cpp_lib_mdspan)

namespace std::linalg {

template<class NestedAccessor>
class conjugated_accessor;

template<class Layout>
class layout_transpose;

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

template<class T>
struct __is_conjugated_accessor : std::false_type {};

template<class NestedAccessor>
struct __is_conjugated_accessor<conjugated_accessor<NestedAccessor>>
    : std::true_type {
    using nested_accessor_type = NestedAccessor;
};

enum class __transpose_mapping_kind {
    wrapped,
    direct,
    left_padded,
    right_padded,
    strided,
    nested
};

template<class Layout>
struct __transpose_layout {
    using type = layout_transpose<Layout>;
    static constexpr auto kind = __transpose_mapping_kind::wrapped;
};

template<>
struct __transpose_layout<layout_left> {
    using type = layout_right;
    static constexpr auto kind = __transpose_mapping_kind::direct;
};

template<>
struct __transpose_layout<layout_right> {
    using type = layout_left;
    static constexpr auto kind = __transpose_mapping_kind::direct;
};

template<size_t PaddingValue>
struct __transpose_layout<layout_left_padded<PaddingValue>> {
    using type = layout_right_padded<PaddingValue>;
    static constexpr auto kind = __transpose_mapping_kind::left_padded;
};

template<size_t PaddingValue>
struct __transpose_layout<layout_right_padded<PaddingValue>> {
    using type = layout_left_padded<PaddingValue>;
    static constexpr auto kind = __transpose_mapping_kind::right_padded;
};

template<>
struct __transpose_layout<layout_stride> {
    using type = layout_stride;
    static constexpr auto kind = __transpose_mapping_kind::strided;
};

template<class Triangle, class StorageOrder>
struct __transpose_layout<layout_blas_packed<Triangle, StorageOrder>> {
    using opposite_triangle = std::conditional_t<
        std::is_same_v<Triangle, upper_triangle_t>,
        lower_triangle_t,
        upper_triangle_t>;
    using opposite_storage_order = std::conditional_t<
        std::is_same_v<StorageOrder, column_major_t>,
        row_major_t,
        column_major_t>;
    using type =
        layout_blas_packed<opposite_triangle, opposite_storage_order>;
    static constexpr auto kind = __transpose_mapping_kind::direct;
};

template<class NestedLayout>
struct __transpose_layout<layout_transpose<NestedLayout>> {
    using type = NestedLayout;
    static constexpr auto kind = __transpose_mapping_kind::nested;
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
    using nested_layout_type = Layout;

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
        constexpr explicit mapping(
            const typename Layout::template mapping<OtherExtents>& m)
            : nested_(m)
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

        [[nodiscard]] constexpr const __nested_mapping_type&
        nested_mapping() const noexcept {
            return nested_;
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
    if constexpr (__detail::__is_conjugated_accessor<Accessor>::value) {
        using acc_t = typename __detail::__is_conjugated_accessor<
            Accessor>::nested_accessor_type;
        return std::mdspan<
            typename acc_t::element_type, Extents, Layout, acc_t>(
                x.data_handle(), x.mapping(),
                x.accessor().nested_accessor());
    } else if constexpr (std::is_arithmetic_v<std::remove_cvref_t<ElementType>>) {
        return x;
    } else {
        using acc_t = conjugated_accessor<Accessor>;
        return std::mdspan<
            typename acc_t::element_type, Extents, Layout, acc_t>(
                x.data_handle(), x.mapping(), acc_t(x.accessor()));
    }
}

template<class ElementType, class Extents, class Layout, class Accessor>
[[nodiscard]] auto transposed(
    std::mdspan<ElementType, Extents, Layout, Accessor> x)
{
    static_assert(Extents::rank() == 2, "transposed requires a 2D mdspan");
    using traits_t = __detail::__transpose_layout<Layout>;
    using layout_t = typename traits_t::type;
    using index_t = typename Extents::index_type;
    using new_extents_t = std::extents<
        index_t, Extents::static_extent(1), Extents::static_extent(0)>;
    using mapping_t = typename layout_t::template mapping<new_extents_t>;
    using result_t =
        std::mdspan<ElementType, new_extents_t, layout_t, Accessor>;
    const new_extents_t transposed_extents(
        x.extent(1), x.extent(0));

    if constexpr (
        traits_t::kind == __detail::__transpose_mapping_kind::direct) {
        return result_t(
            x.data_handle(), mapping_t(transposed_extents), x.accessor());
    } else if constexpr (
        traits_t::kind == __detail::__transpose_mapping_kind::left_padded) {
        return result_t(
            x.data_handle(),
            mapping_t(transposed_extents, x.mapping().stride(1)),
            x.accessor());
    } else if constexpr (
        traits_t::kind == __detail::__transpose_mapping_kind::right_padded) {
        return result_t(
            x.data_handle(),
            mapping_t(transposed_extents, x.mapping().stride(0)),
            x.accessor());
    } else if constexpr (
        traits_t::kind == __detail::__transpose_mapping_kind::strided) {
        return result_t(
            x.data_handle(),
            mapping_t(
                transposed_extents,
                std::array<index_t, 2>{
                    x.mapping().stride(1), x.mapping().stride(0)}),
            x.accessor());
    } else if constexpr (
        traits_t::kind == __detail::__transpose_mapping_kind::nested) {
        return result_t(
            x.data_handle(), x.mapping().nested_mapping(), x.accessor());
    } else {
        return result_t(
            x.data_handle(), mapping_t(x.mapping()), x.accessor());
    }
}

template<class ElementType, class Extents, class Layout, class Accessor>
[[nodiscard]] auto conjugate_transposed(
    std::mdspan<ElementType, Extents, Layout, Accessor> x)
{
    return conjugated(transposed(x));
}



} // namespace std::linalg

#endif // defined(__cpp_lib_mdspan)

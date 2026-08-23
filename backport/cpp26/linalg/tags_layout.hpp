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

#include "detail.hpp"
#if defined(__cpp_lib_mdspan)

namespace std::linalg {

namespace __detail {

template<class T>
struct __is_extents_specialization : std::false_type {};

template<class IndexType, std::size_t... Extents>
struct __is_extents_specialization<std::extents<IndexType, Extents...>>
    : std::true_type {};

template<class T>
inline constexpr bool __is_extents_specialization_v =
    __is_extents_specialization<T>::value;

template<class Extents>
consteval bool __packed_static_product_representable() {
    if constexpr (!__is_extents_specialization_v<Extents> ||
                  Extents::rank() != 2) {
        return false;
    } else if constexpr (Extents::rank_dynamic() != 0) {
        return true;
    } else {
        using index_type = typename Extents::index_type;
        constexpr auto n = static_cast<std::uintmax_t>(
            Extents::static_extent(0));
        constexpr auto maximum = static_cast<std::uintmax_t>(
            std::numeric_limits<index_type>::max());
        return n != std::numeric_limits<std::uintmax_t>::max() &&
            n <= maximum / (n + 1);
    }
}

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
    using triangle_type = Triangle;
    using storage_order_type = StorageOrder;

    static_assert(
        std::is_same_v<Triangle, upper_triangle_t> ||
        std::is_same_v<Triangle, lower_triangle_t>);
    static_assert(
        std::is_same_v<StorageOrder, column_major_t> ||
        std::is_same_v<StorageOrder, row_major_t>);

    template<class Extents>
    struct mapping {
        using extents_type = Extents;
        using index_type   = typename Extents::index_type;
        using size_type    = typename Extents::size_type;
        using rank_type    = typename Extents::rank_type;
        using layout_type  = layout_blas_packed;

        static_assert(__detail::__is_extents_specialization_v<Extents>);
        static_assert(Extents::rank() == 2);
        static_assert(__detail::__square_static_extents_v<Extents>);
        static_assert(
            __detail::__packed_static_product_representable<Extents>());

        constexpr mapping() noexcept = default;

        constexpr mapping(const Extents& e) noexcept : extents_(e) {}

        template<class OtherExtents>
            requires std::is_constructible_v<Extents, OtherExtents>
        constexpr explicit(!std::is_convertible_v<OtherExtents, Extents>)
        mapping(
            const typename layout_blas_packed::template mapping<OtherExtents>&
                other) noexcept
            : extents_(other.extents()) {}

        [[nodiscard]] constexpr const Extents& extents() const noexcept {
            return extents_;
        }

        [[nodiscard]] constexpr index_type required_span_size() const noexcept {
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

        [[nodiscard]] constexpr bool is_unique()            const noexcept {
            return extents_.extent(0) < 2;
        }
        [[nodiscard]] static constexpr bool is_always_unique()            noexcept {
            constexpr auto n0 = Extents::static_extent(0);
            constexpr auto n1 = Extents::static_extent(1);
            return (n0 != std::dynamic_extent && n0 <= 1) ||
                (n1 != std::dynamic_extent && n1 <= 1);
        }
        [[nodiscard]] static constexpr bool is_always_exhaustive()        noexcept { return true;  }
        [[nodiscard]] static constexpr bool is_always_strided()           noexcept {
            return is_always_unique();
        }
        [[nodiscard]] constexpr bool is_exhaustive()        const noexcept { return true;  }
        [[nodiscard]] constexpr bool is_strided()           const noexcept {
            return extents_.extent(0) < 2;
        }
        [[nodiscard]] constexpr index_type stride(rank_type) const noexcept { return 1; }

        template<class OtherExtents>
        friend constexpr bool operator==(
            const mapping& x,
            const typename layout_blas_packed::template mapping<OtherExtents>&
                y) noexcept {
            return x.extents() == y.extents();
        }

    private:
        [[no_unique_address]] Extents extents_{};
    };
};

namespace __detail {

template<class Layout, class Triangle>
struct __packed_triangle_matches : std::true_type {};

template<class PackedTriangle, class StorageOrder, class Triangle>
struct __packed_triangle_matches<
    layout_blas_packed<PackedTriangle, StorageOrder>, Triangle>
    : std::is_same<PackedTriangle, Triangle> {};

template<class Layout, class Triangle>
inline constexpr bool __packed_triangle_matches_v =
    __packed_triangle_matches<Layout, Triangle>::value;

} // namespace __detail

} // namespace std::linalg

#endif // defined(__cpp_lib_mdspan)

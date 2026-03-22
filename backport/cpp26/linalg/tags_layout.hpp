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

} // namespace std::linalg

#endif // defined(__cpp_lib_mdspan)

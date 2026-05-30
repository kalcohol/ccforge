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

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

// <mdspan> is included by the backport/mdspan wrapper before this file.

namespace std {

#if defined(FORGE_FORCE_MDSPAN_PADDED_LAYOUTS_BACKPORT) || \
    !defined(FORGE_HAS_NATIVE_MDSPAN_PADDED_LAYOUTS)

namespace __forge_mdspan_padded_detail {

template <class Extents>
constexpr bool empty_extents(const Extents& e) noexcept {
    for (typename Extents::rank_type r = 0; r < Extents::rank(); ++r) {
        if (e.extent(r) == typename Extents::index_type(0)) {
            return true;
        }
    }
    return false;
}

template <class I>
constexpr I least_multiple_at_least(I alignment, I value) noexcept {
    if (value == I(0)) {
        return I(0);
    }
    const I remainder = value % alignment;
    return remainder == I(0) ? value : static_cast<I>(value + (alignment - remainder));
}

} // namespace __forge_mdspan_padded_detail

template <size_t PaddingValue = dynamic_extent>
struct layout_left_padded {
    template <class Extents>
    class mapping {
    public:
        static constexpr size_t padding_value = PaddingValue;

        using extents_type = Extents;
        using index_type = typename extents_type::index_type;
        using size_type = typename extents_type::size_type;
        using rank_type = typename extents_type::rank_type;
        using layout_type = layout_left_padded<PaddingValue>;

    private:
        static constexpr size_t rank_ = extents_type::rank();

        static consteval size_t first_static_extent_() {
            if constexpr (rank_ == 0) {
                return dynamic_extent;
            } else {
                return extents_type::static_extent(0);
            }
        }

        static constexpr size_t first_static_extent = first_static_extent_();
        static constexpr size_t static_padding_stride =
            rank_ <= 1 ? size_t(1) :
            PaddingValue != dynamic_extent ? PaddingValue :
            first_static_extent != dynamic_extent ? first_static_extent :
            dynamic_extent;

        static constexpr index_type make_stride(index_type extent, index_type padding) noexcept {
            return __forge_mdspan_padded_detail::least_multiple_at_least(padding, extent);
        }

    public:
        constexpr mapping() noexcept : mapping(extents_type{}) {}
        constexpr mapping(const mapping&) noexcept = default;

        constexpr mapping(const extents_type& ext)
            : stride_1_(rank_ > 1
                  ? (PaddingValue == dynamic_extent
                        ? static_cast<index_type>(ext.extent(0))
                        : make_stride(static_cast<index_type>(ext.extent(0)),
                                      static_cast<index_type>(PaddingValue)))
                  : index_type(1)),
              extents_(ext) {}

        template <class OtherIndexType>
            requires is_convertible_v<OtherIndexType, index_type>
        constexpr mapping(const extents_type& ext, OtherIndexType padding)
            : stride_1_(rank_ > 1
                  ? make_stride(static_cast<index_type>(ext.extent(0)),
                                static_cast<index_type>(padding))
                  : index_type(1)),
              extents_(ext) {}

        template <class OtherExtents>
            requires is_constructible_v<extents_type, OtherExtents>
        constexpr explicit(!is_convertible_v<OtherExtents, extents_type>)
        mapping(const layout_left::mapping<OtherExtents>& other)
            : mapping(extents_type(other.extents())) {}

        template <class OtherExtents>
            requires is_constructible_v<extents_type, OtherExtents>
        constexpr explicit(!(rank_ == 0 && is_convertible_v<OtherExtents, extents_type>))
        mapping(const layout_stride::mapping<OtherExtents>& other)
            : stride_1_(rank_ > 1 ? static_cast<index_type>(other.stride(1)) : index_type(1)),
              extents_(other.extents()) {}

        template <class OtherMapping>
            requires requires(const OtherMapping& other) {
                OtherMapping::padding_value;
                typename OtherMapping::layout_type;
                typename OtherMapping::extents_type;
                other.extents();
                other.stride(rank_type{});
            }
        constexpr explicit(!is_convertible_v<typename OtherMapping::extents_type, extents_type>)
        mapping(const OtherMapping& other)
            : stride_1_(rank_ > 1 ? static_cast<index_type>(other.stride(1)) : index_type(1)),
              extents_(other.extents()) {}

        constexpr mapping& operator=(const mapping&) noexcept = default;

        constexpr const extents_type& extents() const noexcept { return extents_; }

        constexpr array<index_type, rank_> strides() const noexcept {
            array<index_type, rank_> result{};
            for (rank_type r = 0; r < rank_; ++r) {
                result[r] = stride(r);
            }
            return result;
        }

        constexpr index_type required_span_size() const noexcept {
            if (__forge_mdspan_padded_detail::empty_extents(extents_)) {
                return index_type(0);
            }
            index_type result = 0;
            for (rank_type r = 0; r < rank_; ++r) {
                result += static_cast<index_type>(extents_.extent(r) - index_type(1)) * stride(r);
            }
            return static_cast<index_type>(result + index_type(1));
        }

        template <class... Indices>
            requires (sizeof...(Indices) == rank_) &&
                     (is_convertible_v<Indices, index_type> && ...)
        constexpr index_type operator()(Indices... idxs) const noexcept {
            index_type result = 0;
            if constexpr (sizeof...(Indices) > 0) {
                rank_type r = 0;
                ((result += static_cast<index_type>(idxs) * stride(r++)), ...);
            }
            return result;
        }

        static constexpr bool is_always_unique() noexcept { return true; }
        static constexpr bool is_always_strided() noexcept { return true; }
        static constexpr bool is_unique() noexcept { return true; }
        static constexpr bool is_strided() noexcept { return true; }

        static constexpr bool is_always_exhaustive() noexcept {
            if constexpr (rank_ <= 1) {
                return true;
            } else if constexpr (static_padding_stride != dynamic_extent &&
                                 first_static_extent != dynamic_extent) {
                return static_padding_stride == first_static_extent;
            } else {
                return false;
            }
        }

        constexpr bool is_exhaustive() const noexcept {
            if constexpr (rank_ <= 1) {
                return true;
            } else {
                return static_cast<index_type>(extents_.extent(0)) == stride(1);
            }
        }

        constexpr index_type stride(rank_type r) const noexcept {
            if constexpr (rank_ == 0) {
                (void)r;
                return index_type(1);
            } else {
                if (r == 0) {
                    return index_type(1);
                }
                if (r == 1) {
                    return stride_1_;
                }
                index_type result = stride_1_;
                for (rank_type k = 1; k < r; ++k) {
                    result = static_cast<index_type>(result * static_cast<index_type>(extents_.extent(k)));
                }
                return result;
            }
        }

        template <class OtherMapping>
        friend constexpr bool operator==(const mapping& x, const OtherMapping& y) noexcept
            requires requires {
                OtherMapping::padding_value;
                typename OtherMapping::layout_type;
                typename OtherMapping::extents_type;
            }
        {
            if constexpr (OtherMapping::extents_type::rank() != rank_) {
                return false;
            } else if constexpr (rank_ <= 1) {
                return x.extents() == y.extents();
            } else {
                return x.extents() == y.extents() && x.stride(1) == y.stride(1);
            }
        }

    private:
        index_type stride_1_ = static_padding_stride == dynamic_extent
            ? index_type(0)
            : static_cast<index_type>(static_padding_stride);
        extents_type extents_{};
    };
};

template <size_t PaddingValue = dynamic_extent>
struct layout_right_padded {
    template <class Extents>
    class mapping {
    public:
        static constexpr size_t padding_value = PaddingValue;

        using extents_type = Extents;
        using index_type = typename extents_type::index_type;
        using size_type = typename extents_type::size_type;
        using rank_type = typename extents_type::rank_type;
        using layout_type = layout_right_padded<PaddingValue>;

    private:
        static constexpr size_t rank_ = extents_type::rank();

        static consteval size_t last_static_extent_() {
            if constexpr (rank_ == 0) {
                return dynamic_extent;
            } else {
                return extents_type::static_extent(rank_ - 1);
            }
        }

        static constexpr size_t last_static_extent = last_static_extent_();
        static constexpr size_t static_padding_stride =
            rank_ <= 1 ? size_t(1) :
            PaddingValue != dynamic_extent ? PaddingValue :
            last_static_extent != dynamic_extent ? last_static_extent :
            dynamic_extent;

        static constexpr index_type make_stride(index_type extent, index_type padding) noexcept {
            return __forge_mdspan_padded_detail::least_multiple_at_least(padding, extent);
        }

    public:
        constexpr mapping() noexcept : mapping(extents_type{}) {}
        constexpr mapping(const mapping&) noexcept = default;

        constexpr mapping(const extents_type& ext)
            : stride_rm2_(rank_ > 1
                  ? (PaddingValue == dynamic_extent
                        ? static_cast<index_type>(ext.extent(rank_ - 1))
                        : make_stride(static_cast<index_type>(ext.extent(rank_ - 1)),
                                      static_cast<index_type>(PaddingValue)))
                  : index_type(1)),
              extents_(ext) {}

        template <class OtherIndexType>
            requires is_convertible_v<OtherIndexType, index_type>
        constexpr mapping(const extents_type& ext, OtherIndexType padding)
            : stride_rm2_(rank_ > 1
                  ? make_stride(static_cast<index_type>(ext.extent(rank_ - 1)),
                                static_cast<index_type>(padding))
                  : index_type(1)),
              extents_(ext) {}

        template <class OtherExtents>
            requires is_constructible_v<extents_type, OtherExtents>
        constexpr explicit(!is_convertible_v<OtherExtents, extents_type>)
        mapping(const layout_right::mapping<OtherExtents>& other)
            : mapping(extents_type(other.extents())) {}

        template <class OtherExtents>
            requires is_constructible_v<extents_type, OtherExtents>
        constexpr explicit(!(rank_ == 0 && is_convertible_v<OtherExtents, extents_type>))
        mapping(const layout_stride::mapping<OtherExtents>& other)
            : stride_rm2_(rank_ > 1 ? static_cast<index_type>(other.stride(rank_ - 2)) : index_type(1)),
              extents_(other.extents()) {}

        template <class OtherMapping>
            requires requires(const OtherMapping& other) {
                OtherMapping::padding_value;
                typename OtherMapping::layout_type;
                typename OtherMapping::extents_type;
                other.extents();
                other.stride(rank_type{});
            }
        constexpr explicit(!is_convertible_v<typename OtherMapping::extents_type, extents_type>)
        mapping(const OtherMapping& other)
            : stride_rm2_(rank_ > 1 ? static_cast<index_type>(other.stride(rank_ - 2)) : index_type(1)),
              extents_(other.extents()) {}

        constexpr mapping& operator=(const mapping&) noexcept = default;

        constexpr const extents_type& extents() const noexcept { return extents_; }

        constexpr array<index_type, rank_> strides() const noexcept {
            array<index_type, rank_> result{};
            for (rank_type r = 0; r < rank_; ++r) {
                result[r] = stride(r);
            }
            return result;
        }

        constexpr index_type required_span_size() const noexcept {
            if (__forge_mdspan_padded_detail::empty_extents(extents_)) {
                return index_type(0);
            }
            index_type result = 0;
            for (rank_type r = 0; r < rank_; ++r) {
                result += static_cast<index_type>(extents_.extent(r) - index_type(1)) * stride(r);
            }
            return static_cast<index_type>(result + index_type(1));
        }

        template <class... Indices>
            requires (sizeof...(Indices) == rank_) &&
                     (is_convertible_v<Indices, index_type> && ...)
        constexpr index_type operator()(Indices... idxs) const noexcept {
            index_type result = 0;
            if constexpr (sizeof...(Indices) > 0) {
                rank_type r = 0;
                ((result += static_cast<index_type>(idxs) * stride(r++)), ...);
            }
            return result;
        }

        static constexpr bool is_always_unique() noexcept { return true; }
        static constexpr bool is_always_strided() noexcept { return true; }
        static constexpr bool is_unique() noexcept { return true; }
        static constexpr bool is_strided() noexcept { return true; }

        static constexpr bool is_always_exhaustive() noexcept {
            if constexpr (rank_ <= 1) {
                return true;
            } else if constexpr (static_padding_stride != dynamic_extent &&
                                 last_static_extent != dynamic_extent) {
                return static_padding_stride == last_static_extent;
            } else {
                return false;
            }
        }

        constexpr bool is_exhaustive() const noexcept {
            if constexpr (rank_ <= 1) {
                return true;
            } else {
                return static_cast<index_type>(extents_.extent(rank_ - 1)) == stride(rank_ - 2);
            }
        }

        constexpr index_type stride(rank_type r) const noexcept {
            if constexpr (rank_ == 0) {
                (void)r;
                return index_type(1);
            } else {
                if (r == rank_ - 1) {
                    return index_type(1);
                }
                if (r == rank_ - 2) {
                    return stride_rm2_;
                }
                index_type result = stride_rm2_;
                for (rank_type k = r + 1; k < rank_ - 1; ++k) {
                    result = static_cast<index_type>(result * static_cast<index_type>(extents_.extent(k)));
                }
                return result;
            }
        }

        template <class OtherMapping>
        friend constexpr bool operator==(const mapping& x, const OtherMapping& y) noexcept
            requires requires {
                OtherMapping::padding_value;
                typename OtherMapping::layout_type;
                typename OtherMapping::extents_type;
            }
        {
            if constexpr (OtherMapping::extents_type::rank() != rank_) {
                return false;
            } else if constexpr (rank_ <= 1) {
                return x.extents() == y.extents();
            } else {
                return x.extents() == y.extents() && x.stride(rank_ - 2) == y.stride(rank_ - 2);
            }
        }

    private:
        index_type stride_rm2_ = static_padding_stride == dynamic_extent
            ? index_type(0)
            : static_cast<index_type>(static_padding_stride);
        extents_type extents_{};
    };
};

#endif // forced or no native mdspan padded layouts

} // namespace std

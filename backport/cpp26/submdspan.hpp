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

// std::submdspan backport (P2630 + P3663/P3982 working draft wording)
//
// Normative baseline: C++26 working draft [mdspan.sub], May 2026
// https://eel.is/c++draft/mdspan.sub
//
// Scope:
//   - extent_slice, range_slice, submdspan_mapping_result
//   - canonical_slices, subextents
//   - submdspan_mapping for layout_left, layout_right, layout_stride, and the
//     Forge padded-layout backport
//   - submdspan() function template
//   - Also defines full_extent_t / full_extent when missing (libc++ 21 C++23 mode
//     has __cpp_lib_mdspan=202207 which predates full_extent_t in libc++)
//
// Guard: this file is included only when native __cpp_lib_submdspan is absent
// or older than the current 202603L draft value, except for explicit diagnostic
// force-injection builds.

#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

#include "mdspan_padded.hpp"

// <mdspan> is included by the backport/mdspan wrapper before this file.

namespace std {

// ---------------------------------------------------------------------------
// full_extent_t / full_extent — [mdspan.syn]
// These are introduced by submdspan (P2630), NOT by base mdspan (P0009): a
// toolchain may ship <mdspan> (any __cpp_lib_mdspan value, e.g. libc++ bumped it
// to 202406 for P2389 dims) yet still lack submdspan and full_extent. So the
// correct discriminator is __cpp_lib_submdspan, not __cpp_lib_mdspan. Define
// full_extent exactly when the native submdspan facility is absent.
// ---------------------------------------------------------------------------
#if !defined(__cpp_lib_submdspan)
struct full_extent_t { explicit full_extent_t() = default; };
inline constexpr full_extent_t full_extent{};
#endif

// ---------------------------------------------------------------------------
// [mdspan.sub.range.slices] extent_slice / range_slice
// ---------------------------------------------------------------------------

template <class OffsetType, class ExtentType, class StrideType>
struct extent_slice {
    using offset_type = OffsetType;
    using extent_type = ExtentType;
    using stride_type = StrideType;

    [[no_unique_address]] offset_type offset{};
    [[no_unique_address]] extent_type extent{};
    [[no_unique_address]] stride_type stride{};

    // Mandates: each type is integral or integral-constant-like [mdspan.sub.strided.slice p3]
    static_assert(is_integral_v<remove_cvref_t<OffsetType>> ||
                  requires { OffsetType::value; },
                  "extent_slice: OffsetType must be integral or integral-constant-like");
    static_assert(is_integral_v<remove_cvref_t<ExtentType>> ||
                  requires { ExtentType::value; },
                  "extent_slice: ExtentType must be integral or integral-constant-like");
    static_assert(is_integral_v<remove_cvref_t<StrideType>> ||
                  requires { StrideType::value; },
                  "extent_slice: StrideType must be integral or integral-constant-like");
};

template <class O, class E, class S>
extent_slice(O, E, S) -> extent_slice<O, E, S>;

template <class FirstType, class LastType, class StrideType = constant_wrapper<1zu>>
struct range_slice {
    [[no_unique_address]] FirstType first{};
    [[no_unique_address]] LastType last{};
    [[no_unique_address]] StrideType stride{};

    static_assert(is_integral_v<remove_cvref_t<FirstType>> ||
                  requires { FirstType::value; },
                  "range_slice: FirstType must be integral or integral-constant-like");
    static_assert(is_integral_v<remove_cvref_t<LastType>> ||
                  requires { LastType::value; },
                  "range_slice: LastType must be integral or integral-constant-like");
    static_assert(is_integral_v<remove_cvref_t<StrideType>> ||
                  requires { StrideType::value; },
                  "range_slice: StrideType must be integral or integral-constant-like");
};

template <class F, class L>
range_slice(F, L) -> range_slice<F, L>;
template <class F, class L, class S>
range_slice(F, L, S) -> range_slice<F, L, S>;

// ---------------------------------------------------------------------------
// [mdspan.sub.map.result] submdspan_mapping_result
// ---------------------------------------------------------------------------

template <class LayoutMapping>
struct submdspan_mapping_result {
    [[no_unique_address]] LayoutMapping mapping = LayoutMapping();
    size_t offset{};
};

// ---------------------------------------------------------------------------
// Exposition-only helpers — namespace std::__forge_submdspan_detail
// ---------------------------------------------------------------------------

namespace __forge_submdspan_detail {

// --- integral_constant_like (exposition-only) ---------------------------
// [span.syn] concept integral-constant-like
// We implement as a type trait (concepts not required here).

template <class T, class = void>
struct icl_impl : false_type {};

template <class T>
struct icl_impl<T, void_t<
    decltype(T::value),
    decltype(static_cast<remove_cvref_t<decltype(T::value)>>(declval<T>()))>>
    : bool_constant<
        is_integral_v<remove_cvref_t<decltype(T::value)>> &&
        !is_same_v<bool, remove_cvref_t<decltype(T::value)>> &&
        is_convertible_v<T, remove_cvref_t<decltype(T::value)>> &&
        bool_constant<T{} == T::value>::value &&
        bool_constant<
            static_cast<remove_cvref_t<decltype(T::value)>>(T{}) == T::value>::value> {};

template <class T>
inline constexpr bool is_icl_v = icl_impl<T>::value;

// --- slice type recognition --------------------------------------------

template <class T>
struct is_extent_slice_s : false_type {};
template <class O, class E, class S>
struct is_extent_slice_s<extent_slice<O, E, S>> : true_type {};
template <class T>
inline constexpr bool is_extent_slice_v = is_extent_slice_s<remove_cvref_t<T>>::value;

template <class T>
struct is_range_slice_s : false_type {};
template <class F, class L, class S>
struct is_range_slice_s<range_slice<F, L, S>> : true_type {};
template <class T>
inline constexpr bool is_range_slice_type_v = is_range_slice_s<remove_cvref_t<T>>::value;

template <class T>
struct is_layout_left_padded_policy : false_type {};
template <size_t PaddingValue>
struct is_layout_left_padded_policy<layout_left_padded<PaddingValue>> : true_type {};

template <class T>
struct is_layout_right_padded_policy : false_type {};
template <size_t PaddingValue>
struct is_layout_right_padded_policy<layout_right_padded<PaddingValue>> : true_type {};

template <class Mapping, class = void>
struct is_layout_left_padded_mapping : false_type {};
template <class Mapping>
struct is_layout_left_padded_mapping<Mapping, void_t<typename Mapping::layout_type>>
    : is_layout_left_padded_policy<typename Mapping::layout_type> {};
template <class Mapping>
inline constexpr bool is_layout_left_padded_mapping_v =
    is_layout_left_padded_mapping<remove_cvref_t<Mapping>>::value;

template <class Mapping, class = void>
struct is_layout_right_padded_mapping : false_type {};
template <class Mapping>
struct is_layout_right_padded_mapping<Mapping, void_t<typename Mapping::layout_type>>
    : is_layout_right_padded_policy<typename Mapping::layout_type> {};
template <class Mapping>
inline constexpr bool is_layout_right_padded_mapping_v =
    is_layout_right_padded_mapping<remove_cvref_t<Mapping>>::value;

// --- index_pair_like (pair/tuple/array<T,2>/complex<T>) -----------------

template <class T, class I, class = void>
struct ipl_s : false_type {};
template <class A, class B, class I>
struct ipl_s<pair<A,B>,I> : bool_constant<is_convertible_v<A,I>&&is_convertible_v<B,I>> {};
template <class A, class B, class I>
struct ipl_s<tuple<A,B>,I> : bool_constant<is_convertible_v<A,I>&&is_convertible_v<B,I>> {};
template <class A, class I>
struct ipl_s<array<A,2>,I> : bool_constant<is_convertible_v<A,I>> {};
template <class A, class I>
struct ipl_s<complex<A>,I> : bool_constant<is_convertible_v<A,I>> {};
template <class T, class I>
inline constexpr bool is_ipl_v = ipl_s<remove_cvref_t<T>,I>::value;

// --- slice classification -----------------------------------------------

template <class S, class I>
inline constexpr bool is_index_slice_v =
    is_convertible_v<S,I> &&
    !is_extent_slice_v<S> &&
    !is_range_slice_type_v<S>;

template <class S, class I>
inline constexpr bool is_range_slice_v =
    is_same_v<remove_cvref_t<S>, full_extent_t> || is_ipl_v<S,I> || is_range_slice_type_v<S>;

template <class T>
struct is_constant_wrapper_s : false_type {};
template <auto V, class T>
struct is_constant_wrapper_s<constant_wrapper<V, T>> : true_type {};
template <class T>
inline constexpr bool is_constant_wrapper_v =
    is_constant_wrapper_s<remove_cvref_t<T>>::value;

template <class S, class IndexType>
struct is_canonical_index_slice_s
    : bool_constant<is_same_v<remove_cvref_t<S>, IndexType>> {};
template <auto V, class T, class IndexType>
struct is_canonical_index_slice_s<constant_wrapper<V, T>, IndexType>
    : bool_constant<
          is_same_v<decltype(V), IndexType> && V >= IndexType(0)> {};
template <class S, class IndexType>
inline constexpr bool is_canonical_index_slice_v =
    is_canonical_index_slice_s<remove_cvref_t<S>, IndexType>::value;

template <class ExtentType, class StrideType, class IndexType>
consteval bool has_valid_static_stride() {
    if constexpr (is_constant_wrapper_v<ExtentType> &&
                  is_constant_wrapper_v<StrideType>) {
        return StrideType::value > IndexType(0);
    } else {
        return true;
    }
}

template <class S, class IndexType>
struct is_canonical_slice_s
    : bool_constant<
          is_same_v<remove_cvref_t<S>, full_extent_t> ||
          is_canonical_index_slice_v<S, IndexType>> {};
template <class O, class E, class S, class IndexType>
struct is_canonical_slice_s<extent_slice<O, E, S>, IndexType>
    : bool_constant<
          is_canonical_index_slice_v<O, IndexType> &&
          is_canonical_index_slice_v<E, IndexType> &&
          is_canonical_index_slice_v<S, IndexType> &&
          has_valid_static_stride<E, S, IndexType>()> {};
template <class S, class IndexType>
inline constexpr bool is_canonical_slice_v =
    is_canonical_slice_s<remove_cvref_t<S>, IndexType>::value;

template <class T>
consteval uintmax_t static_index_or(uintmax_t fallback) {
    if constexpr (is_constant_wrapper_v<T>) {
        return static_cast<uintmax_t>(T::value);
    } else {
        return fallback;
    }
}

template <class Extents, size_t K, class Slice>
consteval bool is_valid_static_slice_type() {
    constexpr size_t source_extent = Extents::static_extent(K);
    if constexpr (source_extent == dynamic_extent) {
        return true;
    } else {
        using slice_t = remove_cvref_t<Slice>;
        constexpr uintmax_t x = source_extent;
        if constexpr (is_same_v<slice_t, full_extent_t>) {
            return true;
        } else if constexpr (is_extent_slice_v<slice_t>) {
            constexpr uintmax_t offset =
                static_index_or<typename slice_t::offset_type>(0);
            constexpr uintmax_t extent =
                static_index_or<typename slice_t::extent_type>(0);
            constexpr uintmax_t stride =
                static_index_or<typename slice_t::stride_type>(1);

            if constexpr (offset > x || extent > x) {
                return false;
            } else if constexpr (extent == 0) {
                return true;
            } else if constexpr (offset >= x) {
                return false;
            } else if constexpr (extent == 1) {
                return true;
            } else if constexpr (stride == 0) {
                return false;
            } else {
                return extent - 1 <= (x - offset - 1) / stride;
            }
        } else if constexpr (is_constant_wrapper_v<slice_t>) {
            return static_cast<uintmax_t>(slice_t::value) < x;
        } else {
            return true;
        }
    }
}

template <class Extents, class SliceTuple, size_t... Is>
consteval bool has_valid_static_slice_types(index_sequence<Is...>) {
    return (is_valid_static_slice_type<
                Extents, Is, tuple_element_t<Is, SliceTuple>>() && ...);
}

template <class Extents, class... SliceSpecifiers>
consteval bool has_canonical_mapping_slices() {
    if constexpr (sizeof...(SliceSpecifiers) != Extents::rank()) {
        return false;
    } else {
        using slices_t = tuple<remove_cvref_t<SliceSpecifiers>...>;
        return (is_canonical_slice_v<
                    SliceSpecifiers, typename Extents::index_type> && ...) &&
            has_valid_static_slice_types<Extents, slices_t>(
                make_index_sequence<Extents::rank()>{});
    }
}

template <class Extents, class... SliceSpecifiers>
inline constexpr bool has_canonical_mapping_slices_v =
    has_canonical_mapping_slices<Extents, SliceSpecifiers...>();

template <class Mapping, class... SliceSpecifiers>
consteval bool mapping_has_canonical_slices() {
    if constexpr (requires { typename Mapping::extents_type; }) {
        return has_canonical_mapping_slices_v<
            typename Mapping::extents_type,
            SliceSpecifiers...>;
    } else {
        return false;
    }
}

template <class Mapping, class... SliceSpecifiers>
inline constexpr bool mapping_has_canonical_slices_v =
    mapping_has_canonical_slices<Mapping, SliceSpecifiers...>();

// --- unit-stride slice: full_extent_t OR extent-like slice with ct stride==1 --
// [mdspan.sub.overview] p6

template <class S, class = void>
struct uss_s : bool_constant<is_same_v<remove_cvref_t<S>, full_extent_t>> {};
template <class O, class E, auto V>
struct uss_s<extent_slice<O, E, constant_wrapper<V>>> : bool_constant<V == decltype(V)(1)> {};
template <class S>
inline constexpr bool is_unit_stride_slice_v = uss_s<S>::value;

// --- first_of -----------------------------------------------------------

// integral (non-bool)
template <class T>
constexpr auto first_of(const T& i)
    -> enable_if_t<is_integral_v<T> && !is_same_v<T,bool>, T>
{ return i; }

// integral_constant_like
template <class T>
constexpr auto first_of(const T&)
    -> enable_if_t<is_icl_v<T>, integral_constant<remove_cvref_t<decltype(T::value)>, T::value>>
{ return {}; }

// full_extent_t → 0
inline constexpr integral_constant<size_t,0> first_of(full_extent_t) { return {}; }

// pair
template <class A, class B>
constexpr auto first_of(const pair<A,B>& p) { return p.first; }
// tuple<A,B>
template <class A, class B>
constexpr auto first_of(const tuple<A,B>& t) { return get<0>(t); }
// array<A,2>
template <class A>
constexpr auto first_of(const array<A,2>& a) { return a[0]; }
// complex<A>
template <class A>
constexpr auto first_of(const complex<A>& c) { return c.real(); }
// extent_slice
template <class O, class E, class S>
constexpr auto first_of(const extent_slice<O,E,S>& r) { return r.offset; }
// range_slice
template <class F, class L, class S>
constexpr auto first_of(const range_slice<F,L,S>& r) { return r.first; }

// --- last_of ------------------------------------------------------------

// integer index → same value
template <size_t K, class Ext, class T>
constexpr auto last_of(integral_constant<size_t,K>, const Ext&, const T& i)
    -> enable_if_t<is_integral_v<T> && !is_same_v<T,bool>, T>
{ return i; }

template <size_t K, class Ext, class T>
constexpr auto last_of(integral_constant<size_t,K>, const Ext&, const T&)
    -> enable_if_t<is_icl_v<T>, integral_constant<remove_cvref_t<decltype(T::value)>, T::value>>
{ return {}; }

// full_extent_t → extent(K)
template <size_t K, class Ext>
constexpr auto last_of(integral_constant<size_t,K>, const Ext& e, full_extent_t) {
    if constexpr (Ext::static_extent(K) == dynamic_extent)
        return e.extent(K);
    else
        return integral_constant<size_t, Ext::static_extent(K)>{};
}

// pair
template <size_t K, class Ext, class A, class B>
constexpr auto last_of(integral_constant<size_t,K>, const Ext&, const pair<A,B>& p)
{ return p.second; }
// tuple<A,B>
template <size_t K, class Ext, class A, class B>
constexpr auto last_of(integral_constant<size_t,K>, const Ext&, const tuple<A,B>& t)
{ return get<1>(t); }
// array<A,2>
template <size_t K, class Ext, class A>
constexpr auto last_of(integral_constant<size_t,K>, const Ext&, const array<A,2>& a)
{ return a[1]; }
// complex<A>
template <size_t K, class Ext, class A>
constexpr auto last_of(integral_constant<size_t,K>, const Ext&, const complex<A>& c)
{ return c.imag(); }
// extent_slice → returns the output element count
template <size_t K, class Ext, class O, class E, class S>
constexpr auto last_of(integral_constant<size_t,K>, const Ext&,
                        const extent_slice<O,E,S>& r)
{ return r.extent; }
// range_slice → returns half-open last
template <size_t K, class Ext, class F, class L, class S>
constexpr auto last_of(integral_constant<size_t,K>, const Ext&,
                        const range_slice<F,L,S>& r)
{ return r.last; }

// --- stride_of ----------------------------------------------------------

template <class T>
constexpr integral_constant<size_t,1> stride_of(const T&) { return {}; }
template <class O, class E, class S>
constexpr auto stride_of(const extent_slice<O,E,S>& r) { return r.stride; }
template <class F, class L, class S>
constexpr auto stride_of(const range_slice<F,L,S>& r) { return r.stride; }

// --- Static extent arithmetic -------------------------------------------

// Range [first, last)
template <class First, class Last>
struct SERangeT { static constexpr size_t value = dynamic_extent; };
template <class I0, I0 v0, class I1, I1 v1>
struct SERangeT<integral_constant<I0,v0>, integral_constant<I1,v1>> {
    static constexpr size_t value = (v1 >= v0)
        ? static_cast<size_t>(v1) - static_cast<size_t>(v0)
        : dynamic_extent;
};

// Strided: 0 if extent==0, else 1 + (extent-1)/stride
template <class E, class S>
struct SEStridedT { static constexpr size_t value = dynamic_extent; };
template <class I0, I0 v0, class I1, I1 v1>
struct SEStridedT<integral_constant<I0,v0>, integral_constant<I1,v1>> {
    static constexpr size_t value =
        (v0 == I0(0)) ? size_t(0)
                      : size_t(1) + (static_cast<size_t>(v0) - 1u) / static_cast<size_t>(v1);
};
template <auto v0, auto v1>
struct SEStridedT<constant_wrapper<v0>, constant_wrapper<v1>> {
    static constexpr size_t value =
        (v0 == decltype(v0)(0)) ? size_t(0)
                                : size_t(1) + (static_cast<size_t>(v0) - 1u) / static_cast<size_t>(v1);
};

template <class E>
struct SEExtentT { static constexpr size_t value = dynamic_extent; };
template <class I0, I0 v0>
struct SEExtentT<integral_constant<I0,v0>> {
    static constexpr size_t value = static_cast<size_t>(v0);
};
template <auto v0>
struct SEExtentT<constant_wrapper<v0>> {
    static constexpr size_t value = static_cast<size_t>(v0);
};

// --- Out-of-bounds guard [mdspan.sub.map.common p8 / LWG 4060] ----------

template <class IndexType, class Slice>
constexpr bool one_oob(const IndexType& ext, const Slice& sl) {
    using C = common_type_t<decay_t<decltype(first_of(sl))>, IndexType>;
    return static_cast<C>(first_of(sl)) == static_cast<C>(ext);
}
template <size_t... Ks, class I, size_t... Es, class... Ss>
constexpr bool any_oob_impl(index_sequence<Ks...>,
                              const extents<I,Es...>& e,
                              const Ss&... ss) {
    return (one_oob(e.extent(Ks), ss) || ...);
}
template <class I, size_t... Es, class... Ss>
constexpr bool any_oob(const extents<I,Es...>& e, const Ss&... ss) {
    return any_oob_impl(make_index_sequence<sizeof...(Ss)>{}, e, ss...);
}

// --- inv_map_rank -------------------------------------------------------
// Builds index_sequence of source-rank indices for non-collapsing slices.

template <size_t C, size_t... Ms>
constexpr auto inv_map_rank(integral_constant<size_t,C>, index_sequence<Ms...>)
{ return index_sequence<Ms...>{}; }

template <size_t C, class Slice, class... Rest, size_t... Ms>
constexpr auto inv_map_rank(integral_constant<size_t,C>, index_sequence<Ms...>,
                             Slice, Rest... rest) {
    using next_seq = conditional_t<
        is_convertible_v<Slice, size_t>,
        index_sequence<Ms...>,
        index_sequence<Ms..., C>>;
    return inv_map_rank(integral_constant<size_t,C+1>{}, next_seq{}, rest...);
}

// --- make_stride_factors ------------------------------------------------
// sf[k] = stride_of(slices[k]) for all k in 0..Rank-1

template <size_t Rank, size_t... Ks, class... Ss>
constexpr array<size_t, Rank>
make_sf_impl(index_sequence<Ks...>, const Ss&... ss) {
    const auto stride_factor = []<class Slice>(const Slice& slice) {
        if constexpr (is_extent_slice_v<Slice>) {
            return static_cast<size_t>(slice.extent) > size_t(1)
                ? static_cast<size_t>(slice.stride)
                : size_t(1);
        } else {
            return static_cast<size_t>(stride_of(slice));
        }
    };
    array<size_t, Rank> sf{};
    ((sf[Ks] = stride_factor(ss)), ...);
    return sf;
}
template <size_t Rank, class... Ss>
constexpr array<size_t, Rank> make_stride_factors(const Ss&... ss) {
    static_assert(sizeof...(Ss) == Rank);
    return make_sf_impl<Rank>(make_index_sequence<Rank>{}, ss...);
}

// --- construct_sub_strides ----------------------------------------------
// For sub-rank dim i mapped back to src-rank InvMapIdxs[i]:
//   out[i] = src.stride(InvMapIdxs[i]) * sf[InvMapIdxs[i]]

template <class Src, size_t... Ms, size_t N>
constexpr auto build_sub_strides(const Src& src,
                                   index_sequence<Ms...>,
                                   const array<size_t,N>& sf) {
    using idx_t = typename Src::index_type;
    return array<idx_t, sizeof...(Ms)>{
        {static_cast<idx_t>(src.stride(Ms)) * static_cast<idx_t>(sf[Ms])...}};
}

// ---------------------------------------------------------------------------
// extents_builder — recursive compile-time extents computation
// [mdspan.sub.extents] p5-6
// ---------------------------------------------------------------------------

// Each overload is disambiguated by a tag type to avoid ambiguity between
// index-convertible slices and range slices that might both match the same
// enable_if. We use three explicit dispatch tags.

struct tag_index          {};  // slice is index (rank-collapsing)
struct tag_range          {};  // slice is range-like (full_extent / pair-like)
struct tag_extent         {};  // slice is current extent_slice

template <class S, class IndexType>
using slice_tag_t = conditional_t<
    is_extent_slice_v<S>, tag_extent,
    conditional_t<
        is_index_slice_v<S, IndexType>, tag_index,
        tag_range>>;

template <size_t K, class SrcExt, size_t... NS>
struct EB {  // extents builder
    static constexpr size_t src_k = SrcExt::rank() - K;
    using idx_t = typename SrcExt::index_type;

    // --- range-like overload
    template <class Slice, class... Rest>
    static constexpr auto apply_tagged(tag_range,
                                        const SrcExt& src, const Slice& sl,
                                        Rest... rest) {
        using first_t = decay_t<decltype(first_of(sl))>;
        using last_t  = decay_t<decltype(
            last_of(integral_constant<size_t,src_k>{}, src, sl))>;
        constexpr size_t ns = SERangeT<first_t, last_t>::value;
        idx_t dyn = idx_t(last_of(integral_constant<size_t,src_k>{}, src, sl))
                  - idx_t(first_of(sl));
        return EB<K-1, SrcExt, NS..., ns>::apply(src, rest..., dyn);
    }

    // --- index (collapsing) overload
    template <class Slice, class... Rest>
    static constexpr auto apply_tagged(tag_index,
                                        const SrcExt& src, const Slice&,
                                        Rest... rest) {
        return EB<K-1, SrcExt, NS...>::apply(src, rest...);
    }

    // --- current extent_slice overload: extent is the output element count
    template <class O, class E, class S, class... Rest>
    static constexpr auto apply_tagged(tag_extent,
                                        const SrcExt& src,
                                        const extent_slice<O,E,S>& sl,
                                        Rest... rest) {
        using st = SEExtentT<E>;
        if constexpr (st::value != dynamic_extent) {
            constexpr size_t ns = st::value;
            return EB<K-1, SrcExt, NS..., ns>::apply(src, rest..., idx_t(ns));
        } else {
            return EB<K-1, SrcExt, NS..., dynamic_extent>::apply(
                src, rest..., static_cast<idx_t>(sl.extent));
        }
    }

    // Dispatcher
    template <class Slice, class... Rest>
    static constexpr auto apply(const SrcExt& src, const Slice& sl, Rest... rest) {
        return apply_tagged(slice_tag_t<Slice, idx_t>{}, src, sl, rest...);
    }
};

// Base case: construct result extents
template <class SrcExt, size_t... NS>
struct EB<0, SrcExt, NS...> {
    using idx_t = typename SrcExt::index_type;
    template <class... Dyn>
    static constexpr auto apply(const SrcExt&, Dyn... dyn) {
        return extents<idx_t, NS...>(idx_t(dyn)...);
    }
};

// ---------------------------------------------------------------------------
// Layout-preservation logic
// [mdspan.sub.map.left] p1.3  [mdspan.sub.map.right] p1.3
// ---------------------------------------------------------------------------

// layout_left_preserving: true iff
//   slices[0..SubRank-2] are full_extent_t AND
//   slices[SubRank-1]    is a unit-stride slice type AND
//   slices[SubRank..]    are index slices
template <size_t SubRank, size_t SrcRank, class... Ss>
constexpr bool ll_preserving() {
    static_assert(sizeof...(Ss) == SrcRank);
    if constexpr (SubRank == 0) return true;
    constexpr bool is_idx[]  = { is_index_slice_v<Ss, size_t>... };
    constexpr bool is_full[] = { is_same_v<remove_cvref_t<Ss>, full_extent_t>... };
    constexpr bool is_us[]   = { is_unit_stride_slice_v<Ss>... };

    if constexpr (SubRank == 1) {
        // Find first non-index slice
        size_t first_rng = SrcRank;
        for (size_t k = 0; k < SrcRank; ++k)
            if (!is_idx[k]) { first_rng = k; break; }
        if (first_rng == SrcRank) return false;
        if (!is_us[first_rng]) return false;
        for (size_t k = 0; k < first_rng; ++k) if (!is_full[k]) return false;
        for (size_t k = first_rng+1; k < SrcRank; ++k) if (!is_idx[k]) return false;
        return true;
    } else {
        for (size_t k = 0; k < SubRank-1; ++k) if (!is_full[k]) return false;
        if (!is_us[SubRank-1]) return false;
        for (size_t k = SubRank; k < SrcRank; ++k) if (!is_idx[k]) return false;
        return true;
    }
}

// layout_right_preserving: mirror of layout_left (operates from the right)
template <size_t SubRank, size_t SrcRank, class... Ss>
constexpr bool lr_preserving() {
    static_assert(sizeof...(Ss) == SrcRank);
    if constexpr (SubRank == 0) return true;
    constexpr bool is_idx[]  = { is_index_slice_v<Ss, size_t>... };
    constexpr bool is_full[] = { is_same_v<remove_cvref_t<Ss>, full_extent_t>... };
    constexpr bool is_us[]   = { is_unit_stride_slice_v<Ss>... };

    if constexpr (SubRank == 1) {
        size_t last_rng = SrcRank;
        for (size_t k = SrcRank; k > 0; --k)
            if (!is_idx[k-1]) { last_rng = k-1; break; }
        if (last_rng == SrcRank) return false;
        if (!is_us[last_rng]) return false;
        for (size_t k = last_rng+1; k < SrcRank; ++k) if (!is_full[k]) return false;
        for (size_t k = 0; k < last_rng; ++k) if (!is_idx[k]) return false;
        return true;
    } else {
        for (size_t k = SrcRank-SubRank+1; k < SrcRank; ++k) if (!is_full[k]) return false;
        if (!is_us[SrcRank-SubRank]) return false;
        for (size_t k = 0; k < SrcRank-SubRank; ++k) if (!is_idx[k]) return false;
        return true;
    }
}

// [mdspan.sub.map.left] and [mdspan.sub.map.leftpad] p1.4.
// Returns the source stride index u + 1, or SrcRank when p1.4 does not apply.
template <size_t SubRank, size_t SrcRank, class... Ss>
consteval size_t left_padded_stride_index() {
    static_assert(sizeof...(Ss) == SrcRank);
    if constexpr (SubRank < 2 || SrcRank < 2) {
        return SrcRank;
    } else {
        constexpr array<bool, SrcRank> is_full{
            is_same_v<remove_cvref_t<Ss>, full_extent_t>...};
        constexpr array<bool, SrcRank> is_unit{
            is_unit_stride_slice_v<Ss>...};

        if (!is_unit[0]) {
            return SrcRank;
        }

        size_t stride_index = SrcRank;
        for (size_t k = 1; k < SrcRank; ++k) {
            if (is_unit[k]) {
                stride_index = k;
                break;
            }
        }
        if (stride_index == SrcRank ||
            SubRank - 2 > SrcRank - 1 - stride_index) {
            return SrcRank;
        }

        const size_t final_unit = stride_index + SubRank - 2;
        for (size_t k = stride_index; k < final_unit; ++k) {
            if (!is_full[k]) {
                return SrcRank;
            }
        }
        return is_unit[final_unit] ? stride_index : SrcRank;
    }
}

// [mdspan.sub.map.right] and [mdspan.sub.map.rightpad] p1.4.
// Returns the source stride index rank_ - u - 2, or SrcRank on no match.
template <size_t SubRank, size_t SrcRank, class... Ss>
consteval size_t right_padded_stride_index() {
    static_assert(sizeof...(Ss) == SrcRank);
    if constexpr (SubRank < 2 || SrcRank < 2) {
        return SrcRank;
    } else {
        constexpr array<bool, SrcRank> is_full{
            is_same_v<remove_cvref_t<Ss>, full_extent_t>...};
        constexpr array<bool, SrcRank> is_unit{
            is_unit_stride_slice_v<Ss>...};

        if (!is_unit[SrcRank - 1]) {
            return SrcRank;
        }

        size_t stride_index = SrcRank;
        for (size_t k = SrcRank - 1; k > 0; --k) {
            if (is_unit[k - 1]) {
                stride_index = k - 1;
                break;
            }
        }
        if (stride_index == SrcRank || SubRank - 2 > stride_index) {
            return SrcRank;
        }

        const size_t final_unit = stride_index - (SubRank - 2);
        for (size_t k = final_unit + 1; k <= stride_index; ++k) {
            if (!is_full[k]) {
                return SrcRank;
            }
        }
        return is_unit[final_unit] ? stride_index : SrcRank;
    }
}

template <class Extents>
consteval size_t static_extent_product(
    size_t first, size_t last, size_t initial = size_t(1)) {
    if (initial == dynamic_extent) {
        return dynamic_extent;
    }

    size_t result = initial;
    for (size_t k = first; k < last; ++k) {
        const size_t extent = Extents::static_extent(k);
        if (extent == dynamic_extent) {
            return dynamic_extent;
        }
        result *= extent;
    }
    return result;
}

constexpr size_t least_multiple_at_least(
    size_t alignment, size_t value) noexcept {
    if (alignment == 0 || value == 0) {
        return value;
    }
    const size_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

template <class Mapping>
consteval size_t left_static_padding_stride() {
    using extents_t = typename Mapping::extents_type;
    if constexpr (extents_t::rank() <= 1) {
        return size_t(1);
    } else if constexpr (
        Mapping::padding_value == dynamic_extent ||
        extents_t::static_extent(0) == dynamic_extent) {
        return dynamic_extent;
    } else {
        return least_multiple_at_least(
            Mapping::padding_value, extents_t::static_extent(0));
    }
}

template <class Mapping>
consteval size_t right_static_padding_stride() {
    using extents_t = typename Mapping::extents_type;
    if constexpr (extents_t::rank() <= 1) {
        return size_t(1);
    } else if constexpr (
        Mapping::padding_value == dynamic_extent ||
        extents_t::static_extent(extents_t::rank() - 1) == dynamic_extent) {
        return dynamic_extent;
    } else {
        return least_multiple_at_least(
            Mapping::padding_value,
            extents_t::static_extent(extents_t::rank() - 1));
    }
}

// ---------------------------------------------------------------------------
// Canonicalization helpers
// ---------------------------------------------------------------------------

template <class IndexType, class T>
constexpr auto canonical_index(T value) {
    using V = remove_cvref_t<T>;
    if constexpr (is_icl_v<V>) {
        return constant_wrapper<static_cast<IndexType>(V::value)>{};
    } else {
        return static_cast<IndexType>(value);
    }
}

template <class IndexType, class OffsetType, class SpanType, class... StrideTypes>
constexpr auto canonical_range_slice(OffsetType offset, SpanType span, StrideTypes... strides) {
    static_assert(sizeof...(StrideTypes) <= 1);
    auto c_offset = canonical_index<IndexType>(offset);
    auto c_span = canonical_index<IndexType>(span);
    auto c_stride = [&] {
        if constexpr (sizeof...(StrideTypes) == 0) {
            return constant_wrapper<IndexType(1)>{};
        } else {
            return canonical_index<IndexType>((strides, ...));
        }
    }();

    using span_t = remove_cvref_t<decltype(c_span)>;
    using stride_t = remove_cvref_t<decltype(c_stride)>;

    if constexpr (is_icl_v<span_t> && is_icl_v<stride_t>) {
        constexpr IndexType span_v = static_cast<IndexType>(span_t::value);
        constexpr IndexType stride_v = static_cast<IndexType>(stride_t::value);
        static_assert(stride_v > IndexType(0));
        constexpr IndexType extent_v =
            span_v == IndexType(0) ? IndexType(0)
                                   : static_cast<IndexType>(IndexType(1) + (span_v - IndexType(1)) / stride_v);
        return extent_slice{c_offset, constant_wrapper<extent_v>{}, c_stride};
    } else {
        const IndexType span_v = static_cast<IndexType>(c_span);
        const IndexType stride_v = span_v == IndexType(0)
            ? IndexType(1)
            : static_cast<IndexType>(c_stride);
        const IndexType extent_v = span_v == IndexType(0)
            ? IndexType(0)
            : static_cast<IndexType>(IndexType(1) + (span_v - IndexType(1)) / stride_v);
        return extent_slice{c_offset, extent_v, c_stride};
    }
}

template <class IndexType, class S>
constexpr auto canonical_slice(S s) {
    using slice_t = remove_cvref_t<S>;
    if constexpr (is_same_v<slice_t, full_extent_t>) {
        return full_extent;
    } else if constexpr (is_index_slice_v<S, IndexType>) {
        return canonical_index<IndexType>(s);
    } else if constexpr (is_extent_slice_v<S>) {
        return extent_slice{
            canonical_index<IndexType>(s.offset),
            canonical_index<IndexType>(s.extent),
            canonical_index<IndexType>(s.stride)};
    } else if constexpr (is_range_slice_type_v<S>) {
        auto c_first = canonical_index<IndexType>(s.first);
        auto c_last = canonical_index<IndexType>(s.last);
        auto c_stride = canonical_index<IndexType>(s.stride);
        return canonical_range_slice<IndexType>(c_first, c_last - c_first, c_stride);
    } else if constexpr (is_ipl_v<S, IndexType>) {
        auto c_first = canonical_index<IndexType>(first_of(s));
        auto c_last = canonical_index<IndexType>(
            last_of(integral_constant<size_t,0>{}, extents<IndexType>{}, s));
        return canonical_range_slice<IndexType>(c_first, c_last - c_first);
    } else {
        auto [s_first, s_last] = std::move(s);
        auto c_first = canonical_index<IndexType>(std::move(s_first));
        auto c_last = canonical_index<IndexType>(std::move(s_last));
        return canonical_range_slice<IndexType>(c_first, c_last - c_first);
    }
}

template <class Extents, class... RawSlices>
consteval bool has_valid_raw_slice_types() {
    if constexpr (sizeof...(RawSlices) != Extents::rank()) {
        return false;
    } else if constexpr (requires {
                             typename tuple<remove_cvref_t<decltype(
                                 canonical_slice<typename Extents::index_type>(
                                     declval<RawSlices>()))>...>;
                         }) {
        using slices_t = tuple<remove_cvref_t<decltype(
            canonical_slice<typename Extents::index_type>(
                declval<RawSlices>()))>...>;
        return []<size_t... Is>(index_sequence<Is...>) {
            return has_canonical_mapping_slices_v<
                Extents, tuple_element_t<Is, slices_t>...>;
        }(make_index_sequence<Extents::rank()>{});
    } else {
        return false;
    }
}

template <class Extents, class... RawSlices>
inline constexpr bool has_valid_raw_slice_types_v =
    has_valid_raw_slice_types<Extents, RawSlices...>();

template <class SrcExt, class Tuple, size_t... Is>
constexpr auto subextents_from_tuple(const SrcExt& src, Tuple&& slices, index_sequence<Is...>) {
    return EB<SrcExt::rank(), SrcExt>::apply(
        src, get<Is>(static_cast<Tuple&&>(slices))...);
}

} // namespace __forge_submdspan_detail

// ---------------------------------------------------------------------------
// [mdspan.sub.canonical] canonical_slices
// ---------------------------------------------------------------------------

template <class IndexType, size_t... Exts, class... SliceSpecifiers>
    requires __forge_submdspan_detail::has_valid_raw_slice_types_v<
        extents<IndexType, Exts...>, SliceSpecifiers...>
constexpr auto canonical_slices(const extents<IndexType, Exts...>& src,
                                  SliceSpecifiers... slices) {
    static_assert(sizeof...(SliceSpecifiers) == sizeof...(Exts),
                  "canonical_slices: number of slices must equal rank");
    (void)src;
    return make_tuple(__forge_submdspan_detail::canonical_slice<IndexType>(slices)...);
}

// ---------------------------------------------------------------------------
// [mdspan.sub.extents] subextents
// ---------------------------------------------------------------------------

template <class IndexType, size_t... Exts, class... SliceSpecifiers>
    requires __forge_submdspan_detail::has_valid_raw_slice_types_v<
        extents<IndexType, Exts...>, SliceSpecifiers...>
constexpr auto subextents(const extents<IndexType, Exts...>& src,
                            SliceSpecifiers... raw_slices) {
    static_assert(sizeof...(SliceSpecifiers) == sizeof...(Exts),
                  "subextents: number of slices must equal rank");
    using src_t = extents<IndexType, Exts...>;
    auto slices = canonical_slices(src, raw_slices...);
    return __forge_submdspan_detail::subextents_from_tuple(
        src, slices, make_index_sequence<src_t::rank()>{});
}

// ---------------------------------------------------------------------------
// [mdspan.sub.map.left] submdspan_mapping — layout_left
// ---------------------------------------------------------------------------

template <class Extents, class... SliceSpecifiers>
    requires __forge_submdspan_detail::has_canonical_mapping_slices_v<
        Extents, SliceSpecifiers...>
constexpr auto submdspan_mapping(const layout_left::mapping<Extents>& src,
                                   SliceSpecifiers... slices) {
    static_assert(sizeof...(SliceSpecifiers) == Extents::rank());
    namespace D = __forge_submdspan_detail;

    auto sub_ext = subextents(src.extents(), slices...);
    using sub_t = decltype(sub_ext);
    constexpr size_t sr = Extents::rank();
    constexpr size_t dr = sub_t::rank();

    // Rank-0 source
    if constexpr (sr == 0)
        return submdspan_mapping_result<layout_left::mapping<sub_t>>{
            layout_left::mapping<sub_t>{sub_ext}, size_t(0)};

    const bool oob = D::any_oob(src.extents(), slices...);
    const size_t off = oob ? src.required_span_size()
        : static_cast<size_t>(
              src(static_cast<typename Extents::index_type>(D::first_of(slices))...));

    // Rank-0 result → layout_left [mdspan.sub.map.left p1.2]
    if constexpr (dr == 0) {
        return submdspan_mapping_result<layout_left::mapping<sub_t>>{
            layout_left::mapping<sub_t>{sub_ext}, off};
    // Layout preservation [mdspan.sub.map.left p1.3]
    } else if constexpr (D::ll_preserving<dr, sr, SliceSpecifiers...>()) {
        return submdspan_mapping_result<layout_left::mapping<sub_t>>{
            layout_left::mapping<sub_t>{sub_ext}, off};
    // Padded preservation [mdspan.sub.map.left p1.4]
    } else if constexpr (
        D::left_padded_stride_index<dr, sr, SliceSpecifiers...>() != sr) {
        constexpr size_t stride_index =
            D::left_padded_stride_index<dr, sr, SliceSpecifiers...>();
        constexpr size_t static_padding =
            D::static_extent_product<Extents>(0, stride_index);
        using dst_t =
            typename layout_left_padded<static_padding>::template mapping<sub_t>;
        return submdspan_mapping_result<dst_t>{
            dst_t{sub_ext, src.stride(stride_index)}, off};
    // General: layout_stride [mdspan.sub.map.left p1.5]
    } else {
        auto inv = D::inv_map_rank(integral_constant<size_t,0>{}, index_sequence<>{}, slices...);
        auto sf  = D::make_stride_factors<sr>(slices...);
        auto sts = D::build_sub_strides(src, inv, sf);
        using dst_t = layout_stride::mapping<sub_t>;
        return submdspan_mapping_result<dst_t>{dst_t{sub_ext, sts}, off};
    }
}

// ---------------------------------------------------------------------------
// [mdspan.sub.map.right] submdspan_mapping — layout_right
// ---------------------------------------------------------------------------

template <class Extents, class... SliceSpecifiers>
    requires __forge_submdspan_detail::has_canonical_mapping_slices_v<
        Extents, SliceSpecifiers...>
constexpr auto submdspan_mapping(const layout_right::mapping<Extents>& src,
                                   SliceSpecifiers... slices) {
    static_assert(sizeof...(SliceSpecifiers) == Extents::rank());
    namespace D = __forge_submdspan_detail;

    auto sub_ext = subextents(src.extents(), slices...);
    using sub_t = decltype(sub_ext);
    constexpr size_t sr = Extents::rank();
    constexpr size_t dr = sub_t::rank();

    if constexpr (sr == 0)
        return submdspan_mapping_result<layout_right::mapping<sub_t>>{
            layout_right::mapping<sub_t>{sub_ext}, size_t(0)};

    const bool oob = D::any_oob(src.extents(), slices...);
    const size_t off = oob ? src.required_span_size()
        : static_cast<size_t>(
              src(static_cast<typename Extents::index_type>(D::first_of(slices))...));

    if constexpr (dr == 0) {
        return submdspan_mapping_result<layout_right::mapping<sub_t>>{
            layout_right::mapping<sub_t>{sub_ext}, off};
    // Layout preservation [mdspan.sub.map.right p1.3]
    } else if constexpr (D::lr_preserving<dr, sr, SliceSpecifiers...>()) {
        return submdspan_mapping_result<layout_right::mapping<sub_t>>{
            layout_right::mapping<sub_t>{sub_ext}, off};
    // Padded preservation [mdspan.sub.map.right p1.4]
    } else if constexpr (
        D::right_padded_stride_index<dr, sr, SliceSpecifiers...>() != sr) {
        constexpr size_t stride_index =
            D::right_padded_stride_index<dr, sr, SliceSpecifiers...>();
        constexpr size_t static_padding =
            D::static_extent_product<Extents>(stride_index + 1, sr);
        using dst_t =
            typename layout_right_padded<static_padding>::template mapping<sub_t>;
        return submdspan_mapping_result<dst_t>{
            dst_t{sub_ext, src.stride(stride_index)}, off};
    // General: layout_stride [mdspan.sub.map.right p1.5]
    } else {
        auto inv = D::inv_map_rank(integral_constant<size_t,0>{}, index_sequence<>{}, slices...);
        auto sf  = D::make_stride_factors<sr>(slices...);
        auto sts = D::build_sub_strides(src, inv, sf);
        using dst_t = layout_stride::mapping<sub_t>;
        return submdspan_mapping_result<dst_t>{dst_t{sub_ext, sts}, off};
    }
}

// ---------------------------------------------------------------------------
// [mdspan.sub.map.stride] submdspan_mapping — layout_stride
// ---------------------------------------------------------------------------

template <class Extents, class... SliceSpecifiers>
    requires __forge_submdspan_detail::has_canonical_mapping_slices_v<
        Extents, SliceSpecifiers...>
constexpr auto submdspan_mapping(const layout_stride::mapping<Extents>& src,
                                   SliceSpecifiers... slices) {
    static_assert(sizeof...(SliceSpecifiers) == Extents::rank());
    namespace D = __forge_submdspan_detail;

    auto sub_ext = subextents(src.extents(), slices...);
    using sub_t = decltype(sub_ext);
    constexpr size_t sr = Extents::rank();

    // Rank-0 source [mdspan.sub.map.stride p1.1]
    if constexpr (sr == 0) {
        array<typename Extents::index_type, 0> z{};
        return submdspan_mapping_result<layout_stride::mapping<sub_t>>{
            layout_stride::mapping<sub_t>{sub_ext, z}, size_t(0)};
    }

    const bool oob = D::any_oob(src.extents(), slices...);
    const size_t off = oob ? src.required_span_size()
        : static_cast<size_t>(
              src(static_cast<typename Extents::index_type>(D::first_of(slices))...));

    auto inv = D::inv_map_rank(integral_constant<size_t,0>{}, index_sequence<>{}, slices...);
    auto sf   = D::make_stride_factors<sr>(slices...);
    auto sts  = D::build_sub_strides(src, inv, sf);
    using dst_t = layout_stride::mapping<sub_t>;
    return submdspan_mapping_result<dst_t>{dst_t{sub_ext, sts}, off};
}

// ---------------------------------------------------------------------------
// [mdspan.sub.map.leftpad] submdspan_mapping — layout_left_padded
// ---------------------------------------------------------------------------

template <class Mapping, class... SliceSpecifiers>
    requires (__forge_submdspan_detail::is_layout_left_padded_mapping_v<Mapping> &&
              __forge_submdspan_detail::mapping_has_canonical_slices_v<
                  Mapping, SliceSpecifiers...>)
constexpr auto submdspan_mapping(const Mapping& src, SliceSpecifiers... slices) {
    namespace D = __forge_submdspan_detail;

    using extents_t = typename Mapping::extents_type;
    static_assert(sizeof...(SliceSpecifiers) == extents_t::rank());
    auto sub_ext = subextents(src.extents(), slices...);
    using sub_t = decltype(sub_ext);
    constexpr size_t sr = extents_t::rank();
    constexpr size_t dr = sub_t::rank();

    if constexpr (sr == 0) {
        return submdspan_mapping_result<Mapping>{src, size_t(0)};
    } else {
        const bool oob = D::any_oob(src.extents(), slices...);
        const size_t off = oob ? src.required_span_size()
            : static_cast<size_t>(
                  src(static_cast<typename extents_t::index_type>(D::first_of(slices))...));

        if constexpr (dr == 0) {
            return submdspan_mapping_result<layout_left::mapping<sub_t>>{
                layout_left::mapping<sub_t>{sub_ext}, off};
        } else if constexpr (dr == 1 &&
                             D::is_unit_stride_slice_v<tuple_element_t<0, tuple<SliceSpecifiers...>>>) {
            return submdspan_mapping_result<layout_left::mapping<sub_t>>{
                layout_left::mapping<sub_t>{sub_ext}, off};
        } else if constexpr (
            D::left_padded_stride_index<dr, sr, SliceSpecifiers...>() != sr) {
            constexpr size_t stride_index =
                D::left_padded_stride_index<dr, sr, SliceSpecifiers...>();
            constexpr size_t static_padding =
                D::static_extent_product<extents_t>(
                    1, stride_index, D::left_static_padding_stride<Mapping>());
            using dst_t =
                typename layout_left_padded<static_padding>::template mapping<sub_t>;
            return submdspan_mapping_result<dst_t>{
                dst_t{sub_ext, src.stride(stride_index)}, off};
        } else {
            auto inv = D::inv_map_rank(integral_constant<size_t,0>{}, index_sequence<>{}, slices...);
            auto sf  = D::make_stride_factors<sr>(slices...);
            auto sts = D::build_sub_strides(src, inv, sf);
            using dst_t = layout_stride::mapping<sub_t>;
            return submdspan_mapping_result<dst_t>{dst_t{sub_ext, sts}, off};
        }
    }
}

// ---------------------------------------------------------------------------
// [mdspan.sub.map.rightpad] submdspan_mapping — layout_right_padded
// ---------------------------------------------------------------------------

template <class Mapping, class... SliceSpecifiers>
    requires (__forge_submdspan_detail::is_layout_right_padded_mapping_v<Mapping> &&
              __forge_submdspan_detail::mapping_has_canonical_slices_v<
                  Mapping, SliceSpecifiers...>)
constexpr auto submdspan_mapping(const Mapping& src, SliceSpecifiers... slices) {
    namespace D = __forge_submdspan_detail;

    using extents_t = typename Mapping::extents_type;
    static_assert(sizeof...(SliceSpecifiers) == extents_t::rank());
    auto sub_ext = subextents(src.extents(), slices...);
    using sub_t = decltype(sub_ext);
    constexpr size_t sr = extents_t::rank();
    constexpr size_t dr = sub_t::rank();

    if constexpr (sr == 0) {
        return submdspan_mapping_result<Mapping>{src, size_t(0)};
    } else {
        const bool oob = D::any_oob(src.extents(), slices...);
        const size_t off = oob ? src.required_span_size()
            : static_cast<size_t>(
                  src(static_cast<typename extents_t::index_type>(D::first_of(slices))...));

        if constexpr (dr == 0) {
            return submdspan_mapping_result<layout_right::mapping<sub_t>>{
                layout_right::mapping<sub_t>{sub_ext}, off};
        } else if constexpr (dr == 1 &&
                             D::is_unit_stride_slice_v<tuple_element_t<sr - 1, tuple<SliceSpecifiers...>>>) {
            return submdspan_mapping_result<layout_right::mapping<sub_t>>{
                layout_right::mapping<sub_t>{sub_ext}, off};
        } else if constexpr (
            D::right_padded_stride_index<dr, sr, SliceSpecifiers...>() != sr) {
            constexpr size_t stride_index =
                D::right_padded_stride_index<dr, sr, SliceSpecifiers...>();
            constexpr size_t static_padding =
                D::static_extent_product<extents_t>(
                    stride_index + 1, sr - 1,
                    D::right_static_padding_stride<Mapping>());
            using dst_t =
                typename layout_right_padded<static_padding>::template mapping<sub_t>;
            return submdspan_mapping_result<dst_t>{
                dst_t{sub_ext, src.stride(stride_index)}, off};
        } else {
            auto inv = D::inv_map_rank(integral_constant<size_t,0>{}, index_sequence<>{}, slices...);
            auto sf  = D::make_stride_factors<sr>(slices...);
            auto sts = D::build_sub_strides(src, inv, sf);
            using dst_t = layout_stride::mapping<sub_t>;
            return submdspan_mapping_result<dst_t>{dst_t{sub_ext, sts}, off};
        }
    }
}

// ---------------------------------------------------------------------------
// [mdspan.sub.sub] submdspan
// ---------------------------------------------------------------------------

template <class ElementType, class Extents, class LayoutPolicy,
          class AccessorPolicy, class... SliceSpecifiers>
    requires __forge_submdspan_detail::has_valid_raw_slice_types_v<
        Extents, SliceSpecifiers...>
constexpr auto submdspan(
    const mdspan<ElementType, Extents, LayoutPolicy, AccessorPolicy>& src,
    SliceSpecifiers... raw_slices)
{
    static_assert(sizeof...(SliceSpecifiers) == Extents::rank(),
                  "submdspan: slice count must match rank");

    auto slices = canonical_slices(src.extents(), raw_slices...);
    auto result = apply(
        [&](auto... canonical) {
            return submdspan_mapping(src.mapping(), canonical...);
        },
        slices);

    using sub_map_t  = remove_cvref_t<decltype(result.mapping)>;
    using sub_ext_t  = typename sub_map_t::extents_type;
    using sub_lay_t  = typename sub_map_t::layout_type;
    using sub_acc_t  = typename AccessorPolicy::offset_policy;

    return mdspan<ElementType, sub_ext_t, sub_lay_t, sub_acc_t>(
        src.accessor().offset(src.data_handle(), result.offset),
        result.mapping,
        sub_acc_t(src.accessor()));
}

// Feature test macro [mdspan.sub]
#ifndef __cpp_lib_submdspan
#  define __cpp_lib_submdspan 202603L
#endif

} // namespace std

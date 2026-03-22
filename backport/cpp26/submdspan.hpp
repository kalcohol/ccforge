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

// std::submdspan backport (P2630R4 + P3355)
//
// Normative baseline: C++26 working draft [mdspan.sub], March 2026
// https://eel.is/c++draft/mdspan.sub
//
// Phase 1 scope:
//   - strided_slice, submdspan_mapping_result, submdspan_extents
//   - submdspan_mapping for layout_left, layout_right, layout_stride
//   - submdspan() function template
//   - Also defines full_extent_t / full_extent when missing (libc++ 21 C++23 mode
//     has __cpp_lib_mdspan=202207 which predates full_extent_t in libc++)
//
// Phase 2 (deferred):
//   - layout_left_padded / layout_right_padded submdspan_mapping (P2642)
//
// Open known issues:
//   - P3982R0: may redefine strided_slice::extent semantics before C++26 ships.
//     Current impl follows the WD: .extent is the OUTPUT element count.
//   - P3663R3: slice canonicalization - not yet in WD, not implemented.
//
// constant_wrapper (P2781) is C++26 only. We use std::integral_constant<T,v>
// as the compile-time value carrier; this provides the same semantics.
//
// Guard: this file is included only when __cpp_lib_submdspan is not defined.

#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

// <mdspan> is included by the backport/mdspan wrapper before this file.

namespace std {

// ---------------------------------------------------------------------------
// full_extent_t / full_extent — [mdspan.syn]
// Present in C++26 (202406) libc++; absent in C++23 libc++21 (202207).
// ---------------------------------------------------------------------------
#if !defined(__cpp_lib_mdspan) || __cpp_lib_mdspan < 202406L
struct full_extent_t { explicit full_extent_t() = default; };
inline constexpr full_extent_t full_extent{};
#endif

// ---------------------------------------------------------------------------
// [mdspan.sub.strided.slice] strided_slice
// strided_slice{.offset=o, .extent=e, .stride=s} selects e elements:
//   o, o+s, o+2s, ..., o+(e-1)*s
// .extent is the OUTPUT element count (not the input span length).
// ---------------------------------------------------------------------------

template <class OffsetType, class ExtentType, class StrideType>
struct strided_slice {
    using offset_type = OffsetType;
    using extent_type = ExtentType;
    using stride_type = StrideType;

    [[no_unique_address]] offset_type offset{};
    [[no_unique_address]] extent_type extent{};
    [[no_unique_address]] stride_type stride{};

    // Mandates: each type is integral or integral-constant-like [mdspan.sub.strided.slice p3]
    static_assert(is_integral_v<remove_cvref_t<OffsetType>> ||
                  requires { OffsetType::value; },
                  "strided_slice: OffsetType must be integral or integral-constant-like");
    static_assert(is_integral_v<remove_cvref_t<ExtentType>> ||
                  requires { ExtentType::value; },
                  "strided_slice: ExtentType must be integral or integral-constant-like");
    static_assert(is_integral_v<remove_cvref_t<StrideType>> ||
                  requires { StrideType::value; },
                  "strided_slice: StrideType must be integral or integral-constant-like");
};

template <class O, class E, class S>
strided_slice(O, E, S) -> strided_slice<O, E, S>;

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

// --- is_strided_slice ---------------------------------------------------

template <class T>
struct is_strided_slice_s : false_type {};
template <class O, class E, class S>
struct is_strided_slice_s<strided_slice<O, E, S>> : true_type {};
template <class T>
inline constexpr bool is_strided_slice_v = is_strided_slice_s<remove_cvref_t<T>>::value;

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
inline constexpr bool is_index_slice_v = is_convertible_v<S,I> && !is_strided_slice_v<S>;

template <class S, class I>
inline constexpr bool is_range_slice_v =
    is_same_v<remove_cvref_t<S>, full_extent_t> || is_ipl_v<S,I>;

// --- unit-stride slice: full_extent_t OR strided_slice with ct stride==1 --
// [mdspan.sub.overview] p6

template <class S, class = void>
struct uss_s : bool_constant<is_same_v<remove_cvref_t<S>, full_extent_t>> {};
template <class O, class E, class I, I V>
struct uss_s<strided_slice<O, E, integral_constant<I,V>>> : bool_constant<V == I(1)> {};
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
// strided_slice
template <class O, class E, class S>
constexpr auto first_of(const strided_slice<O,E,S>& r) { return r.offset; }

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
// strided_slice → returns the extent field (output element count)
template <size_t K, class Ext, class O, class E, class S>
constexpr auto last_of(integral_constant<size_t,K>, const Ext&,
                        const strided_slice<O,E,S>& r)
{ return r.extent; }

// --- stride_of ----------------------------------------------------------

template <class T>
constexpr integral_constant<size_t,1> stride_of(const T&) { return {}; }
template <class O, class E, class S>
constexpr auto stride_of(const strided_slice<O,E,S>& r) { return r.stride; }

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
    array<size_t, Rank> sf{};
    ((sf[Ks] = static_cast<size_t>(stride_of(ss))), ...);
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

struct tag_index   {};  // slice is index (rank-collapsing)
struct tag_range   {};  // slice is range-like (full_extent / pair-like)
struct tag_strided {};  // slice is strided_slice

template <class S, class IndexType>
using slice_tag_t = conditional_t<
    is_strided_slice_v<S>,    tag_strided,
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

    // --- strided_slice overload
    template <class O, class E, class S, class... Rest>
    static constexpr auto apply_tagged(tag_strided,
                                        const SrcExt& src,
                                        const strided_slice<O,E,S>& sl,
                                        Rest... rest) {
        using st = SEStridedT<E,S>;
        if constexpr (st::value != dynamic_extent) {
            constexpr size_t ns = st::value;
            return EB<K-1, SrcExt, NS..., ns>::apply(src, rest..., idx_t(ns));
        } else {
            idx_t cnt = (sl.extent == E{})
                ? idx_t(0)
                : idx_t(1) + (idx_t(sl.extent) - idx_t(1)) / idx_t(sl.stride);
            return EB<K-1, SrcExt, NS..., dynamic_extent>::apply(src, rest..., cnt);
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

} // namespace __forge_submdspan_detail

// ---------------------------------------------------------------------------
// [mdspan.sub.extents] submdspan_extents
// ---------------------------------------------------------------------------

template <class IndexType, size_t... Exts, class... SliceSpecifiers>
constexpr auto submdspan_extents(const extents<IndexType, Exts...>& src,
                                   SliceSpecifiers... slices) {
    static_assert(sizeof...(SliceSpecifiers) == sizeof...(Exts),
                  "submdspan_extents: number of slices must equal rank");
    using src_t = extents<IndexType, Exts...>;
    return __forge_submdspan_detail::EB<src_t::rank(), src_t>::apply(src, slices...);
}

// ---------------------------------------------------------------------------
// [mdspan.sub.map.left] submdspan_mapping — layout_left
// ---------------------------------------------------------------------------

template <class Extents, class... SliceSpecifiers>
constexpr auto submdspan_mapping(const layout_left::mapping<Extents>& src,
                                   SliceSpecifiers... slices) {
    static_assert(sizeof...(SliceSpecifiers) == Extents::rank());
    namespace D = __forge_submdspan_detail;

    auto sub_ext = submdspan_extents(src.extents(), slices...);
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
constexpr auto submdspan_mapping(const layout_right::mapping<Extents>& src,
                                   SliceSpecifiers... slices) {
    static_assert(sizeof...(SliceSpecifiers) == Extents::rank());
    namespace D = __forge_submdspan_detail;

    auto sub_ext = submdspan_extents(src.extents(), slices...);
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
constexpr auto submdspan_mapping(const layout_stride::mapping<Extents>& src,
                                   SliceSpecifiers... slices) {
    static_assert(sizeof...(SliceSpecifiers) == Extents::rank());
    namespace D = __forge_submdspan_detail;

    auto sub_ext = submdspan_extents(src.extents(), slices...);
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
// [mdspan.sub.sub] submdspan
// ---------------------------------------------------------------------------

template <class ElementType, class Extents, class LayoutPolicy,
          class AccessorPolicy, class... SliceSpecifiers>
constexpr auto submdspan(
    const mdspan<ElementType, Extents, LayoutPolicy, AccessorPolicy>& src,
    SliceSpecifiers... slices)
{
    static_assert(sizeof...(SliceSpecifiers) == Extents::rank(),
                  "submdspan: slice count must match rank");

    auto result = submdspan_mapping(src.mapping(), slices...);

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
#  define __cpp_lib_submdspan 202411L
#endif

} // namespace std

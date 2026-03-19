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

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#ifndef FORGE_BACKPORT_SIMD_HPP_INCLUDED
#define FORGE_BACKPORT_SIMD_HPP_INCLUDED 1
#endif

namespace std {

#if __cplusplus < 202002L
struct default_sentinel_t {
    explicit constexpr default_sentinel_t() noexcept = default;
};

inline constexpr default_sentinel_t default_sentinel{};
#endif

namespace simd {

using simd_size_type = ptrdiff_t;

template<class... Flags>
struct flags;

struct convert_flag;
struct aligned_flag;
template<size_t N>
struct overaligned_flag;

namespace detail {

template<class T>
using remove_cvref_t = typename remove_cv<typename remove_reference<T>::type>::type;

template<class T>
struct is_supported_value
    : integral_constant<bool,
        is_arithmetic<remove_cvref_t<T>>::value &&
        !is_same<remove_cvref_t<T>, bool>::value> {};

template<class T>
struct is_data_parallel_type : false_type {};

template<size_t Bytes>
struct integer_from_size;

template<>
struct integer_from_size<1> {
    using type = signed char;
};

template<>
struct integer_from_size<2> {
    using type = short;
};

template<>
struct integer_from_size<4> {
    using type = int;
};

template<>
struct integer_from_size<8> {
    using type = long long;
};

template<class T>
struct native_lane_count
    : integral_constant<simd_size_type,
#if defined(__AVX512F__)
        ((64 / sizeof(T)) > 0 ? (64 / sizeof(T)) : 1)
#elif defined(__AVX2__) || defined(__AVX__)
        ((32 / sizeof(T)) > 0 ? (32 / sizeof(T)) : 1)
#elif defined(__SSE2__) || defined(__ARM_NEON) || defined(__aarch64__)
        ((16 / sizeof(T)) > 0 ? (16 / sizeof(T)) : 1)
#else
        1
#endif
    > {};

template<class Flag, class FlagsPack>
struct has_flag : false_type {};

template<class Flag, class... Flags>
struct has_flag<Flag, simd::flags<Flags...>>
    : disjunction<is_same<Flag, Flags>...> {};

template<class Flag>
struct is_alignment_flag : false_type {};

template<>
struct is_alignment_flag<simd::aligned_flag> : true_type {};

template<size_t N>
struct is_alignment_flag<simd::overaligned_flag<N>> : true_type {};

template<class FlagsPack>
struct has_alignment_flag : false_type {};

template<class... Flags>
struct has_alignment_flag<simd::flags<Flags...>> : disjunction<is_alignment_flag<Flags>...> {};

template<class I, class FlagsPack>
constexpr void require_iterator_compatible_flags() noexcept {
    if constexpr (!is_pointer<remove_cvref_t<I>>::value) {
        static_assert(!has_alignment_flag<FlagsPack>::value,
            "alignment flags require pointer-based load/store in this backport");
    }
}

template<size_t N>
struct is_power_of_two
    : integral_constant<bool, N != 0 && (N & (N - 1)) == 0> {};

template<class T, class BinaryOperation>
struct reduction_identity {
    static constexpr T value() noexcept {
        return T{};
    }
};

template<class T>
struct reduction_identity<T, plus<>> {
    static constexpr T value() noexcept {
        return T{};
    }
};

template<class T>
struct reduction_identity<T, plus<T>> {
    static constexpr T value() noexcept {
        return T{};
    }
};

template<class T>
struct reduction_identity<T, multiplies<>> {
    static constexpr T value() noexcept {
        return T{1};
    }
};

template<class T>
struct reduction_identity<T, multiplies<T>> {
    static constexpr T value() noexcept {
        return T{1};
    }
};

template<class From, class To>
struct is_value_preserving_conversion
    : integral_constant<bool,
        is_same<remove_cvref_t<From>, remove_cvref_t<To>>::value ||
        (
            is_integral<remove_cvref_t<From>>::value &&
            is_integral<remove_cvref_t<To>>::value &&
            (
                (is_signed<remove_cvref_t<From>>::value == is_signed<remove_cvref_t<To>>::value &&
                 numeric_limits<remove_cvref_t<From>>::digits <= numeric_limits<remove_cvref_t<To>>::digits) ||
                (!is_signed<remove_cvref_t<From>>::value && is_signed<remove_cvref_t<To>>::value &&
                 numeric_limits<remove_cvref_t<From>>::digits < numeric_limits<remove_cvref_t<To>>::digits)
            )
        ) ||
        (
            is_floating_point<remove_cvref_t<From>>::value &&
            is_floating_point<remove_cvref_t<To>>::value &&
            numeric_limits<remove_cvref_t<From>>::digits <= numeric_limits<remove_cvref_t<To>>::digits &&
            numeric_limits<remove_cvref_t<From>>::max_exponent <= numeric_limits<remove_cvref_t<To>>::max_exponent &&
            numeric_limits<remove_cvref_t<From>>::min_exponent >= numeric_limits<remove_cvref_t<To>>::min_exponent
        ) ||
        (
            is_integral<remove_cvref_t<From>>::value &&
            is_floating_point<remove_cvref_t<To>>::value &&
            numeric_limits<remove_cvref_t<To>>::radix == 2 &&
            numeric_limits<remove_cvref_t<From>>::digits <= numeric_limits<remove_cvref_t<To>>::digits
        )> {};

template<class To, class From, class FlagsPack>
constexpr To convert_or_copy(const From& value, FlagsPack) noexcept {
    using source_type = remove_cvref_t<From>;

    static_assert(is_supported_value<To>::value, "std::simd value type must be a supported non-bool arithmetic type");
    static_assert(is_supported_value<source_type>::value, "unchecked_load/unchecked_store source type must be a supported non-bool arithmetic type");
    static_assert(
        is_value_preserving_conversion<source_type, To>::value || has_flag<simd::convert_flag, FlagsPack>::value,
        "type-changing load/store requires std::simd::flag_convert unless the conversion is value-preserving");

    return static_cast<To>(value);
}

} // namespace detail

template<simd_size_type N>
struct fixed_size_abi {
    static_assert(N > 0, "std::simd lane count must be positive");
};

template<class T>
struct native_abi {
    static_assert(detail::is_supported_value<T>::value, "std::simd only supports arithmetic non-bool value types");
};

template<class V>
class simd_iterator;

template<class T, class Abi>
struct simd_size;

template<class T, simd_size_type N>
struct simd_size<T, fixed_size_abi<N>> : integral_constant<simd_size_type, N> {};

template<class T>
struct simd_size<T, native_abi<T>> : detail::native_lane_count<T> {};

template<class Abi>
struct abi_lane_count;

template<simd_size_type N>
struct abi_lane_count<fixed_size_abi<N>> : integral_constant<simd_size_type, N> {};

template<class T>
struct abi_lane_count<native_abi<T>> : detail::native_lane_count<T> {};

template<class T, simd_size_type N = simd_size<T, native_abi<T>>::value>
using deduce_abi_t = typename conditional<
    N == simd_size<T, native_abi<T>>::value,
    native_abi<T>,
    fixed_size_abi<N>>::type;

template<class... Flags>
struct flags {};

struct convert_flag {};
struct aligned_flag {};

template<size_t N>
struct overaligned_flag {
    static_assert(detail::is_power_of_two<N>::value, "std::simd::flag_overaligned requires a non-zero power-of-two alignment");
};

inline constexpr flags<> flag_default{};
inline constexpr flags<convert_flag> flag_convert{};
inline constexpr flags<aligned_flag> flag_aligned{};

template<size_t N>
inline constexpr flags<overaligned_flag<N>> flag_overaligned{};

template<class... Left, class... Right>
constexpr flags<Left..., Right...> operator|(flags<Left...>, flags<Right...>) noexcept {
    return {};
}

template<class T, class Abi = native_abi<T>>
class basic_vec;

template<size_t Bytes, class Abi = native_abi<typename detail::integer_from_size<Bytes>::type>>
class basic_mask;

template<class T, simd_size_type N = simd_size<T, native_abi<T>>::value>
using vec = basic_vec<T, deduce_abi_t<T, N>>;

template<class T, simd_size_type N = simd_size<T, native_abi<T>>::value>
using mask = basic_mask<sizeof(T), deduce_abi_t<T, N>>;

template<class T, class U = typename T::value_type>
struct alignment;

template<class T, class U = typename T::value_type>
inline constexpr size_t alignment_v = alignment<T, U>::value;

template<class T, class V>
struct rebind;

template<class T, class V>
using rebind_t = typename rebind<T, V>::type;

template<simd_size_type N, class V>
struct resize;

template<simd_size_type N, class V>
using resize_t = typename resize<N, V>::type;

inline constexpr simd_size_type zero_element = -1;
inline constexpr simd_size_type uninit_element = -2;

namespace detail {

template<size_t Bytes, class Abi>
constexpr bool& lane_ref(basic_mask<Bytes, Abi>& value, simd_size_type i) noexcept;

template<class T, class Abi>
constexpr T& lane_ref(basic_vec<T, Abi>& value, simd_size_type i) noexcept;

template<class V, class U>
constexpr void set_lane(V& value, simd_size_type i, U&& lane) noexcept;

template<class...>
using void_t = void;

template<class I, class = void>
struct has_simd_value_type : false_type {};

template<class I>
struct has_simd_value_type<I, void_t<typename remove_cvref_t<I>::value_type, decltype(remove_cvref_t<I>::size)>> : true_type {};

template<class I, class = void>
struct has_lane_subscript : false_type {};

template<class I>
struct has_lane_subscript<I, void_t<decltype(declval<const remove_cvref_t<I>&>()[simd_size_type{}])>> : true_type {};

template<class I, class = void>
struct is_simd_index_vector : false_type {};

template<class I>
struct is_simd_index_vector<I,
    typename enable_if<
        is_data_parallel_type<remove_cvref_t<I>>::value &&
        has_simd_value_type<I>::value &&
        has_lane_subscript<I>::value &&
        is_integral<typename remove_cvref_t<I>::value_type>::value &&
        !is_same<typename remove_cvref_t<I>::value_type, bool>::value>::type>
    : true_type {};

template<class V, class Indices>
using permute_result_t = resize_t<remove_cvref_t<Indices>::size, V>;

template<class V>
struct lane_mapped_value {
    using type = typename V::value_type;
};

template<size_t Bytes, class Abi>
struct lane_mapped_value<basic_mask<Bytes, Abi>> {
    using type = bool;
};

template<class V>
using lane_mapped_value_t = typename lane_mapped_value<V>::type;

template<class T, class Abi>
struct is_data_parallel_type<basic_vec<T, Abi>> : true_type {};

template<size_t Bytes, class Abi>
struct is_data_parallel_type<basic_mask<Bytes, Abi>> : true_type {};

template<class V>
constexpr lane_mapped_value_t<V> permute_lane(const V& value, simd_size_type index) noexcept {
    return index == zero_element || index == uninit_element ? lane_mapped_value_t<V>{} : value[index];
}

template<class V>
constexpr lane_mapped_value_t<V> dynamic_permute_lane(const V& value, simd_size_type index) noexcept {
    assert(index >= 0);
    assert(index < static_cast<simd_size_type>(V::size));
    return value[index];
}

template<simd_size_type Size, class IndexMap, class Lane>
constexpr simd_size_type invoke_index_map(IndexMap&& index_map, Lane lane) {
    if constexpr (is_invocable<IndexMap, Lane, integral_constant<simd_size_type, Size>>::value) {
        return static_cast<simd_size_type>(std::forward<IndexMap>(index_map)(lane, integral_constant<simd_size_type, Size>{}));
    } else {
        return static_cast<simd_size_type>(std::forward<IndexMap>(index_map)(lane));
    }
}

template<simd_size_type Size, class IndexMap, class Seq>
struct is_static_permute_index_map_sequence;

template<simd_size_type Size, class IndexMap, size_t... I>
struct is_static_permute_index_map_sequence<Size, IndexMap, index_sequence<I...>>
    : conjunction<integral_constant<bool,
        is_invocable<IndexMap&, integral_constant<simd_size_type, static_cast<simd_size_type>(I)>>::value ||
        is_invocable<IndexMap&,
            integral_constant<simd_size_type, static_cast<simd_size_type>(I)>,
            integral_constant<simd_size_type, Size>>::value>...> {};

template<simd_size_type Size, class IndexMap>
struct is_static_permute_index_map
    : is_static_permute_index_map_sequence<Size, remove_cvref_t<IndexMap>, make_index_sequence<static_cast<size_t>(Size)>> {};

template<class V, class Indices>
constexpr permute_result_t<V, Indices> permute_from_indices(const V& value, const Indices& indices) noexcept {
    permute_result_t<V, Indices> result;
    for (simd_size_type i = 0; i < decltype(result)::size; ++i) {
        detail::set_lane(result, i, dynamic_permute_lane(value, static_cast<simd_size_type>(indices[i])));
    }
    return result;
}

template<class V, class IndexMap, simd_size_type... I>
constexpr resize_t<sizeof...(I), V> permute_from_map_impl(const V& value, IndexMap&& index_map, integer_sequence<simd_size_type, I...>) {
    return resize_t<sizeof...(I), V>([&](auto lane) {
        return permute_lane(value, invoke_index_map<static_cast<simd_size_type>(V::size)>(std::forward<IndexMap>(index_map), lane));
    });
}

template<class Chunk, class V, size_t... I>
constexpr array<Chunk, sizeof...(I)> chunk_array_impl(const V& value, index_sequence<I...>) {
    return {{Chunk([&](auto lane) {
        return value[static_cast<simd_size_type>(I * static_cast<size_t>(Chunk::size) + static_cast<size_t>(decltype(lane)::value))];
    })...}};
}

template<class Chunk, class V, size_t... I>
constexpr auto chunk_tuple_impl(const V& value, index_sequence<I...>) {
    return make_tuple(Chunk([&](auto lane) {
        return value[static_cast<simd_size_type>(I * static_cast<size_t>(Chunk::size) + static_cast<size_t>(decltype(lane)::value))];
    })...);
}

template<class Chunk, class V>
constexpr Chunk make_chunk(const V& value, simd_size_type offset) {
    return Chunk([&](auto lane) {
        return value[offset + decltype(lane)::value];
    });
}

template<class Tail, class V>
constexpr Tail tail_chunk(const V& value, simd_size_type offset) {
    return make_chunk<Tail>(value, offset);
}

template<class T, class First, class... Rest>
constexpr vec<T, First::size + (Rest::size + ... + 0)> cat_impl(const First& first, const Rest&... rest) {
    vec<T, First::size + (Rest::size + ... + 0)> result;
    simd_size_type offset = 0;
    const auto append = [&](const auto& current) {
        for (simd_size_type i = 0; i < decay_t<decltype(current)>::size; ++i) {
            detail::set_lane(result, offset + i, current[i]);
        }
        offset += decay_t<decltype(current)>::size;
    };
    append(first);
    (append(rest), ...);
    return result;
}

template<class I, class S, class = void>
struct is_sized_sentinel_for : false_type {};

template<class I, class S>
struct is_sized_sentinel_for<I, S, void_t<decltype(declval<S>() - declval<I>())>> : true_type {};

template<class I>
using iterator_reference_t = decltype(*declval<I&>());

template<class I, class = void>
struct is_random_access_load_store_iterator : false_type {};

template<class I>
struct is_random_access_load_store_iterator<I, void_t<decltype(++declval<I&>()), iterator_reference_t<I>, typename iterator_traits<I>::iterator_category>>
    : integral_constant<bool,
        is_base_of<random_access_iterator_tag, typename iterator_traits<I>::iterator_category>::value &&
        is_lvalue_reference<iterator_reference_t<I>>::value &&
        is_supported_value<typename remove_reference<iterator_reference_t<I>>::type>::value> {};

template<class I, class = void>
struct is_writable_load_store_iterator : false_type {};

template<class I>
struct is_writable_load_store_iterator<I,
    void_t<decltype(++declval<I&>()),
           iterator_reference_t<I>,
           typename iterator_traits<I>::iterator_category,
           decltype(*declval<I&>() = *declval<I&>())>>
    : integral_constant<bool,
        is_random_access_load_store_iterator<I>::value &&
        !is_const<typename remove_reference<iterator_reference_t<I>>::type>::value> {};

template<class I, class S>
constexpr simd_size_type iterator_distance(I first, S last) noexcept {
    return static_cast<simd_size_type>(last - first);
}

template<class V>
constexpr void require_unchecked_extent(simd_size_type count) noexcept {
    assert(count >= static_cast<simd_size_type>(V::size));
    (void)count;
}

template<class Count>
constexpr void require_nonnegative_extent(Count count) noexcept {
    assert(count >= 0);
    (void)count;
}


template<size_t I>
using generator_index_constant = integral_constant<simd_size_type, static_cast<simd_size_type>(I)>;

template<class G, class R, class Seq>
struct is_simd_generator_sequence;

template<class G, class R, size_t... I>
struct is_simd_generator_sequence<G, R, index_sequence<I...>>
    : conjunction<is_invocable_r<R, G&, generator_index_constant<I>>...> {};

template<class G, class R, size_t N>
struct is_simd_generator : is_simd_generator_sequence<G, R, make_index_sequence<N>> {};

template<class G, class R, class Seq>
struct is_nothrow_simd_generator_sequence;

template<class G, class R, size_t... I>
struct is_nothrow_simd_generator_sequence<G, R, index_sequence<I...>>
    : conjunction<integral_constant<bool, noexcept(static_cast<R>(declval<G&>()(generator_index_constant<I>{})))>...> {};

template<class G, class R, size_t N>
struct is_nothrow_simd_generator : is_nothrow_simd_generator_sequence<G, R, make_index_sequence<N>> {};

template<class T, class G, size_t... I>
constexpr array<T, sizeof...(I)> generate_array_impl(G& gen, index_sequence<I...>)
    noexcept(is_nothrow_simd_generator<G, T, sizeof...(I)>::value) {
    return {{static_cast<T>(gen(generator_index_constant<I>{}))...}};
}

template<class T, size_t N, class G>
constexpr array<T, N> generate_array(G& gen)
    noexcept(is_nothrow_simd_generator<G, T, N>::value) {
    return generate_array_impl<T>(gen, make_index_sequence<N>{});
}

} // namespace detail

template<class V, class Indices,
         typename enable_if<detail::is_simd_index_vector<Indices>::value, int>::type = 0>
constexpr detail::permute_result_t<V, Indices> permute(const V& value, const Indices& indices);

template<simd_size_type N = 0, class V, class IndexMap,
         typename enable_if<!detail::is_simd_index_vector<detail::remove_cvref_t<IndexMap>>::value &&
             detail::is_static_permute_index_map<(N == 0 ? static_cast<simd_size_type>(V::size) : N), IndexMap>::value, int>::type = 0>
constexpr resize_t<(N == 0 ? static_cast<simd_size_type>(V::size) : N), V> permute(const V& value, IndexMap&& index_map);

template<class Chunk,
         class V,
         typename enable_if<detail::is_data_parallel_type<detail::remove_cvref_t<Chunk>>::value &&
             is_same<detail::lane_mapped_value_t<detail::remove_cvref_t<Chunk>>, detail::lane_mapped_value_t<V>>::value,
             int>::type = 0>
constexpr auto chunk(const V& value);

template<simd_size_type N, class V>
constexpr auto chunk(const V& value);

template<class First, class... Rest>
constexpr auto cat(const First& first, const Rest&... rest);

	template<size_t Bytes, class Abi>
	class basic_mask {
    friend constexpr bool& detail::lane_ref<Bytes, Abi>(basic_mask& value, simd_size_type i) noexcept;

public:
    using value_type = bool;
    using abi_type = Abi;
    using iterator = simd_iterator<basic_mask>;
    using const_iterator = simd_iterator<const basic_mask>;
    inline static constexpr integral_constant<simd_size_type, abi_lane_count<Abi>::value> size{};

    constexpr basic_mask() noexcept : data_{} {}

    constexpr explicit basic_mask(bool value) noexcept : data_{} {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] = value;
        }
    }

    template<size_t OtherBytes,
             class OtherAbi,
             typename enable_if<
                 static_cast<simd_size_type>(basic_mask<OtherBytes, OtherAbi>::size) == static_cast<simd_size_type>(size),
                 int>::type = 0>
    constexpr explicit basic_mask(const basic_mask<OtherBytes, OtherAbi>& other) noexcept : data_{} {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] = other[i];
        }
    }

    template<class U,
             typename enable_if<
                 is_unsigned<detail::remove_cvref_t<U>>::value &&
                 !is_same<detail::remove_cvref_t<U>, bool>::value,
                 int>::type = 0>
    constexpr explicit basic_mask(U value) noexcept : data_{} {
        using unsigned_type = detail::remove_cvref_t<U>;
        unsigned_type bits = static_cast<unsigned_type>(value);
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] = (bits & unsigned_type{1}) != 0;
            bits >>= 1;
        }
    }

    template<size_t N,
             typename enable_if<N == abi_lane_count<Abi>::value, int>::type = 0>
    basic_mask(const bitset<N>& bits) noexcept : data_{} {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] = bits[static_cast<size_t>(i)];
        }
    }

	    template<class G,
	             typename enable_if<detail::is_simd_generator<G, bool, abi_lane_count<Abi>::value>::value &&
	                 !is_same<detail::remove_cvref_t<G>, basic_mask>::value, int>::type = 0>
	    constexpr explicit basic_mask(G&& gen)
	        noexcept(detail::is_nothrow_simd_generator<G, bool, abi_lane_count<Abi>::value>::value)
	        : data_(detail::generate_array<bool, abi_lane_count<Abi>::value>(gen)) {}

	    constexpr bool operator[](simd_size_type i) const noexcept {
	        return data_[i];
	    }

    template<class Indices,
             typename enable_if<detail::is_simd_index_vector<Indices>::value, int>::type = 0>
    constexpr detail::permute_result_t<basic_mask, Indices> operator[](const Indices& indices) const {
        return simd::permute(*this, indices);
    }


    constexpr iterator begin() noexcept {
        return iterator(*this, 0);
    }

    constexpr const_iterator begin() const noexcept {
        return const_iterator(*this, 0);
    }

    constexpr const_iterator cbegin() const noexcept {
        return const_iterator(*this, 0);
    }

    constexpr default_sentinel_t end() const noexcept {
        return default_sentinel;
    }

    constexpr default_sentinel_t cend() const noexcept {
        return default_sentinel;
    }

	#if defined(__cpp_lib_constexpr_bitset) && __cpp_lib_constexpr_bitset >= 202207L
	    constexpr bitset<abi_lane_count<Abi>::value> to_bitset() const noexcept {
	        bitset<abi_lane_count<Abi>::value> result;
	        for (simd_size_type i = 0; i < size; ++i) {
	            result.set(static_cast<size_t>(i), data_[i]);
	        }
	        return result;
	    }
	#else
	    bitset<abi_lane_count<Abi>::value> to_bitset() const noexcept {
	        bitset<abi_lane_count<Abi>::value> result;
	        for (simd_size_type i = 0; i < size; ++i) {
	            result.set(static_cast<size_t>(i), data_[i]);
	        }
	        return result;
	    }
	#endif

    constexpr unsigned long long to_ullong() const noexcept {
        constexpr simd_size_type digit_count = static_cast<simd_size_type>(numeric_limits<unsigned long long>::digits);

        unsigned long long result = 0;
        const simd_size_type active_lanes = size < digit_count ? size : digit_count;
        for (simd_size_type i = 0; i < active_lanes; ++i) {
            if (data_[i]) {
                result |= (1ull << static_cast<unsigned>(i));
            }
        }
        return result;
    }

    template<class U, class A,
             typename enable_if<
                 static_cast<simd_size_type>(basic_vec<U, A>::size) == static_cast<simd_size_type>(size) &&
                 sizeof(U) == Bytes,
                 int>::type = 0>
    constexpr operator basic_vec<U, A>() const noexcept {
        return basic_vec<U, A>([&](auto lane) {
            return data_[static_cast<size_t>(decltype(lane)::value)] ? U{1} : U{0};
        });
    }

    template<class U, class A,
             typename enable_if<
                 static_cast<simd_size_type>(basic_vec<U, A>::size) == static_cast<simd_size_type>(size) &&
                 sizeof(U) != Bytes,
                 int>::type = 0>
    constexpr explicit operator basic_vec<U, A>() const noexcept {
        return basic_vec<U, A>([&](auto lane) {
            return data_[static_cast<size_t>(decltype(lane)::value)] ? U{1} : U{0};
        });
    }

    constexpr basic_mask& operator&=(const basic_mask& other) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] = data_[i] && other[i];
        }
        return *this;
    }

    constexpr basic_mask& operator|=(const basic_mask& other) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] = data_[i] || other[i];
        }
        return *this;
    }

    constexpr basic_mask& operator^=(const basic_mask& other) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] = data_[i] != other[i];
        }
        return *this;
    }

    friend constexpr basic_mask operator!(const basic_mask& value) noexcept {
        basic_mask result;
        for (simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = !value[i];
        }
        return result;
    }

    friend constexpr basic_mask operator&&(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] && right[i];
        }
        return result;
    }

    friend constexpr basic_mask operator||(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] || right[i];
        }
        return result;
    }

    friend constexpr basic_mask operator&(basic_mask left, const basic_mask& right) noexcept {
        left &= right;
        return left;
    }

    friend constexpr basic_mask operator|(basic_mask left, const basic_mask& right) noexcept {
        left |= right;
        return left;
    }

    friend constexpr basic_mask operator^(basic_mask left, const basic_mask& right) noexcept {
        left ^= right;
        return left;
    }

    friend constexpr basic_vec<typename detail::integer_from_size<Bytes>::type, Abi> operator+(const basic_mask& value) noexcept {
        using result_type = basic_vec<typename detail::integer_from_size<Bytes>::type, Abi>;
        result_type result;
        for (simd_size_type i = 0; i < size; ++i) {
            detail::set_lane(result, i, value[i] ? typename result_type::value_type{1} : typename result_type::value_type{0});
        }
        return result;
    }

    friend constexpr basic_vec<typename detail::integer_from_size<Bytes>::type, Abi> operator-(const basic_mask& value) noexcept {
        using result_type = basic_vec<typename detail::integer_from_size<Bytes>::type, Abi>;
        result_type result;
        for (simd_size_type i = 0; i < size; ++i) {
            detail::set_lane(result, i, value[i] ? typename result_type::value_type{-1} : typename result_type::value_type{0});
        }
        return result;
    }

    friend constexpr basic_vec<typename detail::integer_from_size<Bytes>::type, Abi> operator~(const basic_mask& value) noexcept {
        using result_type = basic_vec<typename detail::integer_from_size<Bytes>::type, Abi>;
        result_type result;
        for (simd_size_type i = 0; i < size; ++i) {
            using lane_type = typename result_type::value_type;
            detail::set_lane(result, i, value[i] ? static_cast<lane_type>(~lane_type{1}) : static_cast<lane_type>(~lane_type{0}));
        }
        return result;
    }

    friend constexpr basic_mask operator==(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] == right[i];
        }
        return result;
    }

    friend constexpr basic_mask operator!=(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] != right[i];
        }
        return result;
    }

    friend constexpr basic_mask operator<(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] < right[i];
        }
        return result;
    }

    friend constexpr basic_mask operator<=(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] <= right[i];
        }
        return result;
    }

    friend constexpr basic_mask operator>(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] > right[i];
        }
        return result;
    }

    friend constexpr basic_mask operator>=(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] >= right[i];
        }
        return result;
    }

private:
    array<bool, abi_lane_count<Abi>::value> data_;
};

	template<class T, class Abi>
	class basic_vec {
    friend constexpr T& detail::lane_ref<T, Abi>(basic_vec& value, simd_size_type i) noexcept;

public:
    static_assert(detail::is_supported_value<T>::value, "std::simd::basic_vec only supports arithmetic non-bool value types");

    using value_type = T;
    using mask_type = basic_mask<sizeof(T), Abi>;
    using abi_type = Abi;
    using iterator = simd_iterator<basic_vec>;
    using const_iterator = simd_iterator<const basic_vec>;
	    inline static constexpr integral_constant<simd_size_type, simd_size<T, Abi>::value> size{};

	    constexpr basic_vec() noexcept : data_{} {}

	    constexpr basic_vec(T value) noexcept : data_{} {
	        for (simd_size_type i = 0; i < size; ++i) {
	            data_[i] = value;
	        }
	    }

    template<class G,
             typename enable_if<detail::is_simd_generator<G, T, simd_size<T, Abi>::value>::value &&
                 !is_same<detail::remove_cvref_t<G>, basic_vec>::value, int>::type = 0>
	    constexpr explicit basic_vec(G&& gen)
	        noexcept(detail::is_nothrow_simd_generator<G, T, simd_size<T, Abi>::value>::value)
	        : data_(detail::generate_array<T, simd_size<T, Abi>::value>(gen)) {}

	    template<class U, class OtherAbi>
	    constexpr explicit basic_vec(const basic_vec<U, OtherAbi>& other, flags<convert_flag> = {}) noexcept : data_{} {
	        static_assert(basic_vec<U, OtherAbi>::size == size, "std::simd converting constructor requires matching lane count");

        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] = static_cast<T>(other[i]);
        }
    }

    constexpr T operator[](simd_size_type i) const noexcept {
        return data_[i];
    }

    template<class Indices,
             typename enable_if<detail::is_simd_index_vector<Indices>::value, int>::type = 0>
    constexpr detail::permute_result_t<basic_vec, Indices> operator[](const Indices& indices) const {
        return simd::permute(*this, indices);
    }


    constexpr iterator begin() noexcept {
        return iterator(*this, 0);
    }

    constexpr const_iterator begin() const noexcept {
        return const_iterator(*this, 0);
    }

    constexpr const_iterator cbegin() const noexcept {
        return const_iterator(*this, 0);
    }

    constexpr default_sentinel_t end() const noexcept {
        return default_sentinel;
    }

    constexpr default_sentinel_t cend() const noexcept {
        return default_sentinel;
    }

    constexpr basic_vec operator+() const noexcept {
        return *this;
    }

	    constexpr basic_vec operator-() const noexcept {
	        basic_vec result;
	        for (simd_size_type i = 0; i < size; ++i) {
	            detail::set_lane(result, i, -data_[i]);
	        }
	        return result;
	    }

	    friend constexpr mask_type operator!(const basic_vec& value) noexcept {
	        mask_type result;
	        for (simd_size_type i = 0; i < size; ++i) {
	            detail::set_lane(result, i, !value[i]);
	        }
	        return result;
	    }

	    constexpr basic_vec& operator++() noexcept {
	        for (simd_size_type i = 0; i < size; ++i) {
	            ++data_[i];
	        }
	        return *this;
	    }

	    constexpr basic_vec operator++(int) noexcept {
	        basic_vec previous = *this;
	        ++(*this);
	        return previous;
	    }

	    constexpr basic_vec& operator--() noexcept {
	        for (simd_size_type i = 0; i < size; ++i) {
	            --data_[i];
	        }
	        return *this;
	    }

	    constexpr basic_vec operator--(int) noexcept {
	        basic_vec previous = *this;
	        --(*this);
	        return previous;
	    }

	    constexpr basic_vec& operator+=(const basic_vec& other) noexcept {
	        for (simd_size_type i = 0; i < size; ++i) {
	            data_[i] += other[i];
	        }
        return *this;
    }

    constexpr basic_vec& operator-=(const basic_vec& other) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] -= other[i];
        }
        return *this;
    }

    constexpr basic_vec& operator*=(const basic_vec& other) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] *= other[i];
        }
        return *this;
    }

    constexpr basic_vec& operator/=(const basic_vec& other) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] /= other[i];
        }
        return *this;
    }


    template<class U = T>
    constexpr typename enable_if<is_integral<U>::value, basic_vec&>::type operator%=(const basic_vec& other) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] %= other[i];
        }
        return *this;
    }

    template<class U = T>
    constexpr typename enable_if<is_integral<U>::value, basic_vec&>::type operator&=(const basic_vec& other) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] &= other[i];
        }
        return *this;
    }

    template<class U = T>
    constexpr typename enable_if<is_integral<U>::value, basic_vec&>::type operator|=(const basic_vec& other) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] |= other[i];
        }
        return *this;
    }

    template<class U = T>
    constexpr typename enable_if<is_integral<U>::value, basic_vec&>::type operator^=(const basic_vec& other) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] ^= other[i];
        }
        return *this;
    }

    template<class Shift, class U = T>
    constexpr typename enable_if<is_integral<U>::value && is_integral<Shift>::value, basic_vec&>::type operator<<=(Shift shift) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] <<= shift;
        }
        return *this;
    }

    template<class Shift, class U = T>
    constexpr typename enable_if<is_integral<U>::value && is_integral<Shift>::value, basic_vec&>::type operator>>=(Shift shift) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] >>= shift;
        }
        return *this;
    }

    friend constexpr basic_vec operator+(basic_vec left, const basic_vec& right) noexcept {
        left += right;
        return left;
    }

    friend constexpr basic_vec operator-(basic_vec left, const basic_vec& right) noexcept {
        left -= right;
        return left;
    }

    friend constexpr basic_vec operator*(basic_vec left, const basic_vec& right) noexcept {
        left *= right;
        return left;
    }

    friend constexpr basic_vec operator/(basic_vec left, const basic_vec& right) noexcept {
        left /= right;
        return left;
    }


    template<class U = T>
    friend constexpr typename enable_if<is_integral<U>::value, basic_vec>::type operator%(basic_vec left, const basic_vec& right) noexcept {
        left %= right;
        return left;
    }

    template<class U = T>
    friend constexpr typename enable_if<is_integral<U>::value, basic_vec>::type operator&(basic_vec left, const basic_vec& right) noexcept {
        left &= right;
        return left;
    }

    template<class U = T>
    friend constexpr typename enable_if<is_integral<U>::value, basic_vec>::type operator|(basic_vec left, const basic_vec& right) noexcept {
        left |= right;
        return left;
    }

    template<class U = T>
    friend constexpr typename enable_if<is_integral<U>::value, basic_vec>::type operator^(basic_vec left, const basic_vec& right) noexcept {
        left ^= right;
        return left;
    }

    template<class U = T>
    constexpr typename enable_if<is_integral<U>::value, basic_vec>::type operator~() const noexcept {
        basic_vec result;
        for (simd_size_type i = 0; i < size; ++i) {
            detail::set_lane(result, i, ~data_[i]);
        }
        return result;
    }

    template<class Shift, class U = T>
    friend constexpr typename enable_if<is_integral<U>::value && is_integral<Shift>::value, basic_vec>::type operator<<(basic_vec left, Shift shift) noexcept {
        left <<= shift;
        return left;
    }

    template<class Shift, class U = T>
    friend constexpr typename enable_if<is_integral<U>::value && is_integral<Shift>::value, basic_vec>::type operator>>(basic_vec left, Shift shift) noexcept {
        left >>= shift;
        return left;
    }

    friend constexpr mask_type operator==(const basic_vec& left, const basic_vec& right) noexcept {
        mask_type result;
        for (simd_size_type i = 0; i < size; ++i) {
            detail::set_lane(result, i, left[i] == right[i]);
        }
        return result;
    }

    friend constexpr mask_type operator!=(const basic_vec& left, const basic_vec& right) noexcept {
        mask_type result;
        for (simd_size_type i = 0; i < size; ++i) {
            detail::set_lane(result, i, left[i] != right[i]);
        }
        return result;
    }

    friend constexpr mask_type operator<(const basic_vec& left, const basic_vec& right) noexcept {
        mask_type result;
        for (simd_size_type i = 0; i < size; ++i) {
            detail::set_lane(result, i, left[i] < right[i]);
        }
        return result;
    }

    friend constexpr mask_type operator<=(const basic_vec& left, const basic_vec& right) noexcept {
        mask_type result;
        for (simd_size_type i = 0; i < size; ++i) {
            detail::set_lane(result, i, left[i] <= right[i]);
        }
        return result;
    }

    friend constexpr mask_type operator>(const basic_vec& left, const basic_vec& right) noexcept {
        mask_type result;
        for (simd_size_type i = 0; i < size; ++i) {
            detail::set_lane(result, i, left[i] > right[i]);
        }
        return result;
    }

    friend constexpr mask_type operator>=(const basic_vec& left, const basic_vec& right) noexcept {
        mask_type result;
        for (simd_size_type i = 0; i < size; ++i) {
            detail::set_lane(result, i, left[i] >= right[i]);
        }
        return result;
    }

private:
    array<T, simd_size<T, Abi>::value> data_;
};

template<class T, class U>
struct alignment<basic_vec<T, U>, T> : integral_constant<size_t, alignof(T) * abi_lane_count<U>::value> {};

template<size_t Bytes, class Abi>
struct alignment<basic_mask<Bytes, Abi>, bool> : integral_constant<size_t, alignof(bool) * abi_lane_count<Abi>::value> {};

template<class T, class U, class Abi>
struct rebind<T, basic_vec<U, Abi>> {
    using type = basic_vec<T, deduce_abi_t<T, abi_lane_count<Abi>::value>>;
};

template<class T, size_t Bytes, class Abi>
struct rebind<T, basic_mask<Bytes, Abi>> {
    using type = basic_mask<sizeof(T), deduce_abi_t<T, abi_lane_count<Abi>::value>>;
};

template<simd_size_type N, class T, class Abi>
struct resize<N, basic_vec<T, Abi>> {
    using type = basic_vec<T, deduce_abi_t<T, N>>;
};

template<simd_size_type N, size_t Bytes, class Abi>
struct resize<N, basic_mask<Bytes, Abi>> {
    using type = basic_mask<Bytes, deduce_abi_t<typename detail::integer_from_size<Bytes>::type, N>>;
};

template<class V>
class simd_iterator {
    template<class>
    friend class simd_iterator;

    template<size_t, class>
    friend class basic_mask;

    template<class, class>
    friend class basic_vec;

private:
    using simd_type = typename remove_const<V>::type;

    constexpr simd_iterator(V& value, simd_size_type index) noexcept
        : value_(addressof(value)), index_(index) {}

public:
    using value_type = typename simd_type::value_type;
    using difference_type = simd_size_type;
    using pointer = void;
    using reference = value_type;
    using iterator_category = input_iterator_tag;
    using iterator_concept = random_access_iterator_tag;

    constexpr simd_iterator() noexcept : value_(nullptr), index_(0) {}

    template<class U = V,
             typename enable_if<is_const<U>::value, int>::type = 0>
    constexpr simd_iterator(const simd_iterator<simd_type>& other) noexcept
        : value_(other.value_), index_(other.index_) {}

    constexpr value_type operator*() const noexcept {
        return (*value_)[index_];
    }

    constexpr value_type operator[](difference_type offset) const noexcept {
        return (*value_)[index_ + offset];
    }

    constexpr simd_iterator& operator++() noexcept {
        ++index_;
        return *this;
    }

    constexpr simd_iterator operator++(int) noexcept {
        simd_iterator tmp(*this);
        ++(*this);
        return tmp;
    }

    constexpr simd_iterator& operator--() noexcept {
        --index_;
        return *this;
    }

    constexpr simd_iterator operator--(int) noexcept {
        simd_iterator tmp(*this);
        --(*this);
        return tmp;
    }

    constexpr simd_iterator& operator+=(difference_type offset) noexcept {
        index_ += offset;
        return *this;
    }

    constexpr simd_iterator& operator-=(difference_type offset) noexcept {
        index_ -= offset;
        return *this;
    }

    friend constexpr simd_iterator operator+(simd_iterator it, difference_type offset) noexcept {
        it += offset;
        return it;
    }

    friend constexpr simd_iterator operator+(difference_type offset, simd_iterator it) noexcept {
        it += offset;
        return it;
    }

    friend constexpr simd_iterator operator-(simd_iterator it, difference_type offset) noexcept {
        it -= offset;
        return it;
    }

    friend constexpr difference_type operator-(const simd_iterator& left, const simd_iterator& right) noexcept {
        return left.index_ - right.index_;
    }

    friend constexpr bool operator==(const simd_iterator& left, const simd_iterator& right) noexcept {
        return left.value_ == right.value_ && left.index_ == right.index_;
    }

    friend constexpr bool operator!=(const simd_iterator& left, const simd_iterator& right) noexcept {
        return !(left == right);
    }

    friend constexpr bool operator<(const simd_iterator& left, const simd_iterator& right) noexcept {
        return left.index_ < right.index_;
    }

    friend constexpr bool operator<=(const simd_iterator& left, const simd_iterator& right) noexcept {
        return left.index_ <= right.index_;
    }

    friend constexpr bool operator>(const simd_iterator& left, const simd_iterator& right) noexcept {
        return left.index_ > right.index_;
    }

    friend constexpr bool operator>=(const simd_iterator& left, const simd_iterator& right) noexcept {
        return left.index_ >= right.index_;
    }

    friend constexpr bool operator==(const simd_iterator& it, default_sentinel_t) noexcept {
        return it.index_ == simd_type::size;
    }

    friend constexpr bool operator==(default_sentinel_t, const simd_iterator& it) noexcept {
        return it == default_sentinel;
    }

    friend constexpr bool operator!=(const simd_iterator& it, default_sentinel_t) noexcept {
        return !(it == default_sentinel);
    }

    friend constexpr bool operator!=(default_sentinel_t, const simd_iterator& it) noexcept {
        return !(it == default_sentinel);
    }

    friend constexpr difference_type operator-(default_sentinel_t, const simd_iterator& it) noexcept {
        return simd_type::size - it.index_;
    }

    friend constexpr difference_type operator-(const simd_iterator& it, default_sentinel_t) noexcept {
        return it.index_ - simd_type::size;
    }

private:
    V* value_;
    simd_size_type index_;
};

namespace detail {

template<size_t Bytes, class Abi>
constexpr bool& lane_ref(basic_mask<Bytes, Abi>& value, simd_size_type i) noexcept {
    return value.data_[i];
}

template<class T, class Abi>
constexpr T& lane_ref(basic_vec<T, Abi>& value, simd_size_type i) noexcept {
    return value.data_[i];
}

template<class V, class U>
constexpr void set_lane(V& value, simd_size_type i, U&& lane) noexcept {
    lane_ref(value, i) = std::forward<U>(lane);
}

template<class V, class U, class... Flags>
constexpr V load_impl(const U* first, simd_size_type count, flags<Flags...> f);

template<class V, class U, class... Flags>
constexpr V load_impl(const U* first, simd_size_type count, const typename V::mask_type& mask_value, flags<Flags...> f);

template<class V,
         class I,
         class S,
         class... Flags,
         typename enable_if<is_sized_sentinel_for<I, S>::value, int>::type = 0>
constexpr V load_impl(I first, S last, flags<Flags...> f);

template<class V,
         class I,
         class S,
         class... Flags,
         typename enable_if<is_sized_sentinel_for<I, S>::value, int>::type = 0>
constexpr V load_impl(I first, S last, const typename V::mask_type& mask_value, flags<Flags...> f);

template<class T, class Abi, class U, class... Flags>
constexpr void store_impl(const basic_vec<T, Abi>& value, U* first, simd_size_type count, flags<Flags...> f);

template<class T, class Abi, class U, class... Flags>
constexpr void store_impl(const basic_vec<T, Abi>& value, U* first, simd_size_type count, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f);

template<class T,
         class Abi,
         class I,
         class S,
         class... Flags,
         typename enable_if<is_sized_sentinel_for<I, S>::value, int>::type = 0>
constexpr void store_impl(const basic_vec<T, Abi>& value, I first, S last, flags<Flags...> f);

template<class T,
         class Abi,
         class I,
         class S,
         class... Flags,
         typename enable_if<is_sized_sentinel_for<I, S>::value, int>::type = 0>
constexpr void store_impl(const basic_vec<T, Abi>& value, I first, S last, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f);

template<class V, class I, class... Flags>
constexpr V load_n_impl(I first, simd_size_type count, flags<Flags...> f) {
    require_nonnegative_extent(count);
    V result;
    for (simd_size_type i = 0; i < V::size; ++i) {
        if (i < count) {
            detail::set_lane(result, i, convert_or_copy<typename V::value_type>(*first, f));
            ++first;
        } else {
            detail::set_lane(result, i, typename V::value_type{});
        }
    }
    return result;
}

template<class V, class I, class... Flags>
constexpr V load_n_impl(I first, simd_size_type count, const typename V::mask_type& mask_value, flags<Flags...> f) {
    require_nonnegative_extent(count);
    V result;
    for (simd_size_type i = 0; i < V::size; ++i) {
        if (i < count) {
            if (mask_value[i]) {
                detail::set_lane(result, i, convert_or_copy<typename V::value_type>(*first, f));
            } else {
                detail::set_lane(result, i, typename V::value_type{});
            }
            ++first;
        } else {
            detail::set_lane(result, i, typename V::value_type{});
        }
    }
    return result;
}

template<class T, class Abi, class I, class... Flags>
constexpr void store_n_impl(const basic_vec<T, Abi>& value, I first, simd_size_type count, flags<Flags...> f) {
    require_nonnegative_extent(count);
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size && i < count; ++i, ++first) {
        *first = convert_or_copy<typename remove_reference<iterator_reference_t<I>>::type>(value[i], f);
    }
}

template<class T, class Abi, class I, class... Flags>
constexpr void store_n_impl(const basic_vec<T, Abi>& value, I first, simd_size_type count, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f) {
    require_nonnegative_extent(count);
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size && i < count; ++i, ++first) {
        if (mask_value[i]) {
            *first = convert_or_copy<typename remove_reference<iterator_reference_t<I>>::type>(value[i], f);
        }
    }
}

template<class V, class U, class... Flags>
constexpr V load_impl(const U* first, flags<Flags...> f = {}) {
    return load_n_impl<V>(first, V::size, f);
}

template<class V, class U>
constexpr V load_impl(const U* first, simd_size_type count) {
    return load_n_impl<V>(first, count, flag_default);
}

template<class V, class U, class... Flags>
constexpr V load_impl(const U* first, const typename V::mask_type& mask_value, flags<Flags...> f = {}) {
    return load_n_impl<V>(first, V::size, mask_value, f);
}

template<class V, class U>
constexpr V load_impl(const U* first, simd_size_type count, const typename V::mask_type& mask_value) {
    return load_n_impl<V>(first, count, mask_value, flag_default);
}

template<class V, class U, class... Flags>
constexpr V load_impl(const U* first, simd_size_type count, flags<Flags...> f) {
    return load_n_impl<V>(first, count, f);
}

template<class V,
         class I,
         class S,
         class... Flags,
         typename enable_if<is_sized_sentinel_for<I, S>::value, int>::type>
constexpr V load_impl(I first, S last, flags<Flags...> f) {
    return load_n_impl<V>(first, iterator_distance(first, last), f);
}

template<class V, class U, class... Flags>
constexpr V load_impl(const U* first, simd_size_type count, const typename V::mask_type& mask_value, flags<Flags...> f) {
    return load_n_impl<V>(first, count, mask_value, f);
}

template<class V,
         class I,
         class S,
         class... Flags,
         typename enable_if<is_sized_sentinel_for<I, S>::value, int>::type>
constexpr V load_impl(I first, S last, const typename V::mask_type& mask_value, flags<Flags...> f) {
    return load_n_impl<V>(first, iterator_distance(first, last), mask_value, f);
}

template<class T, class Abi, class U, class... Flags>
constexpr void store_impl(const basic_vec<T, Abi>& value, U* first, flags<Flags...> f = {}) {
    store_n_impl(value, first, basic_vec<T, Abi>::size, f);
}

template<class T, class Abi, class U>
constexpr void store_impl(const basic_vec<T, Abi>& value, U* first, simd_size_type count) {
    store_n_impl(value, first, count, flag_default);
}

template<class T, class Abi, class U, class... Flags>
constexpr void store_impl(const basic_vec<T, Abi>& value, U* first, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f = {}) {
    store_n_impl(value, first, basic_vec<T, Abi>::size, mask_value, f);
}

template<class T, class Abi, class U>
constexpr void store_impl(const basic_vec<T, Abi>& value,
                          U* first,
                          simd_size_type count,
                          const typename basic_vec<T, Abi>::mask_type& mask_value) {
    store_n_impl(value, first, count, mask_value, flag_default);
}

template<class T, class Abi, class U, class... Flags>
constexpr void store_impl(const basic_vec<T, Abi>& value, U* first, simd_size_type count, flags<Flags...> f) {
    store_n_impl(value, first, count, f);
}

template<class T,
         class Abi,
         class I,
         class S,
         class... Flags,
         typename enable_if<is_sized_sentinel_for<I, S>::value, int>::type>
constexpr void store_impl(const basic_vec<T, Abi>& value, I first, S last, flags<Flags...> f) {
    store_n_impl(value, first, iterator_distance(first, last), f);
}

template<class T, class Abi, class U, class... Flags>
constexpr void store_impl(const basic_vec<T, Abi>& value, U* first, simd_size_type count, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f) {
    store_n_impl(value, first, count, mask_value, f);
}

template<class T,
         class Abi,
         class I,
         class S,
         class... Flags,
         typename enable_if<is_sized_sentinel_for<I, S>::value, int>::type>
constexpr void store_impl(const basic_vec<T, Abi>& value, I first, S last, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f) {
    store_n_impl(value, first, iterator_distance(first, last), mask_value, f);
}

} // namespace detail

template<class V,
         class I,
         class... Flags,
         typename enable_if<!is_pointer<typename detail::remove_cvref_t<I>>::value && detail::is_random_access_load_store_iterator<I>::value, int>::type = 0>
constexpr V partial_load(I first, simd_size_type count, flags<Flags...> f = {}) {
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    return detail::load_n_impl<V>(first, count, f);
}

template<class V,
         class I,
         class... Flags,
         typename enable_if<!is_pointer<typename detail::remove_cvref_t<I>>::value && detail::is_random_access_load_store_iterator<I>::value, int>::type = 0>
constexpr V partial_load(I first, simd_size_type count, const typename V::mask_type& mask_value, flags<Flags...> f = {}) {
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    return detail::load_n_impl<V>(first, count, mask_value, f);
}

template<class V, class U, class... Flags>
constexpr V partial_load(const U* first, simd_size_type count, flags<Flags...> f = {}) {
    return detail::load_impl<V>(first, count, f);
}

template<class V, class U, class... Flags>
constexpr V partial_load(const U* first, simd_size_type count, const typename V::mask_type& mask_value, flags<Flags...> f = {}) {
    return detail::load_impl<V>(first, count, mask_value, f);
}

template<class V,
         class I,
         class S,
         class... Flags,
         typename enable_if<detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_random_access_load_store_iterator<I>::value), int>::type = 0>
constexpr V partial_load(I first, S last, flags<Flags...> f = {}) {
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    return detail::load_impl<V>(first, last, f);
}

template<class V,
         class I,
         class S,
         class... Flags,
         typename enable_if<detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_random_access_load_store_iterator<I>::value), int>::type = 0>
constexpr V partial_load(I first, S last, const typename V::mask_type& mask_value, flags<Flags...> f = {}) {
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    return detail::load_impl<V>(first, last, mask_value, f);
}

template<class T,
         class Abi,
         class I,
         class... Flags,
         typename enable_if<!is_pointer<typename detail::remove_cvref_t<I>>::value && detail::is_writable_load_store_iterator<I>::value, int>::type = 0>
constexpr void partial_store(const basic_vec<T, Abi>& value, I first, simd_size_type count, flags<Flags...> f = {}) {
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    detail::store_n_impl(value, first, count, f);
}

template<class T,
         class Abi,
         class I,
         class... Flags,
         typename enable_if<!is_pointer<typename detail::remove_cvref_t<I>>::value && detail::is_writable_load_store_iterator<I>::value, int>::type = 0>
constexpr void partial_store(const basic_vec<T, Abi>& value, I first, simd_size_type count, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f = {}) {
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    detail::store_n_impl(value, first, count, mask_value, f);
}

template<class T, class Abi, class U, class... Flags>
constexpr void partial_store(const basic_vec<T, Abi>& value, U* first, simd_size_type count, flags<Flags...> f = {}) {
    detail::store_impl(value, first, count, f);
}

template<class T, class Abi, class U, class... Flags>
constexpr void partial_store(const basic_vec<T, Abi>& value,
                             U* first,
                             simd_size_type count,
                             const typename basic_vec<T, Abi>::mask_type& mask_value,
                             flags<Flags...> f = {}) {
    detail::store_impl(value, first, count, mask_value, f);
}

template<class T,
         class Abi,
         class I,
         class S,
         class... Flags,
         typename enable_if<detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_writable_load_store_iterator<I>::value), int>::type = 0>
constexpr void partial_store(const basic_vec<T, Abi>& value, I first, S last, flags<Flags...> f = {}) {
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    detail::store_impl(value, first, last, f);
}

template<class T,
         class Abi,
         class I,
         class S,
         class... Flags,
         typename enable_if<detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_writable_load_store_iterator<I>::value), int>::type = 0>
constexpr void partial_store(const basic_vec<T, Abi>& value,
                             I first,
                             S last,
                             const typename basic_vec<T, Abi>::mask_type& mask_value,
                             flags<Flags...> f = {}) {
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    detail::store_impl(value, first, last, mask_value, f);
}

template<class V, class U, class... Flags>
constexpr V unchecked_load(const U* first, simd_size_type count, flags<Flags...> f);

template<class V, class U, class... Flags>
constexpr V unchecked_load(const U* first, simd_size_type count, const typename V::mask_type& mask_value, flags<Flags...> f);

template<class V,
         class I,
         class S,
         class... Flags,
         typename enable_if<detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_random_access_load_store_iterator<I>::value), int>::type = 0>
constexpr V unchecked_load(I first, S last, flags<Flags...> f);

template<class V,
         class I,
         class S,
         class... Flags,
         typename enable_if<detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_random_access_load_store_iterator<I>::value), int>::type = 0>
constexpr V unchecked_load(I first, S last, const typename V::mask_type& mask_value, flags<Flags...> f);

template<class T, class Abi, class U, class... Flags>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, U* first, simd_size_type count, flags<Flags...> f);

template<class T, class Abi, class U, class... Flags>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, U* first, simd_size_type count, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f);

template<class T,
         class Abi,
         class I,
         class S,
         class... Flags,
         typename enable_if<detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_writable_load_store_iterator<I>::value), int>::type = 0>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, I first, S last, flags<Flags...> f);

template<class T,
         class Abi,
         class I,
         class S,
         class... Flags,
         typename enable_if<detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_writable_load_store_iterator<I>::value), int>::type = 0>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, I first, S last, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f);

template<class V,
         class I,
         class... Flags,
         typename enable_if<!is_pointer<typename detail::remove_cvref_t<I>>::value && detail::is_random_access_load_store_iterator<I>::value, int>::type = 0>
constexpr V unchecked_load(I first, simd_size_type count, flags<Flags...> f = {}) {
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    detail::require_unchecked_extent<V>(count);
    return detail::load_n_impl<V>(first, count, f);
}

template<class V,
         class I,
         class... Flags,
         typename enable_if<!is_pointer<typename detail::remove_cvref_t<I>>::value && detail::is_random_access_load_store_iterator<I>::value, int>::type = 0>
constexpr V unchecked_load(I first, simd_size_type count, const typename V::mask_type& mask_value, flags<Flags...> f = {}) {
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    detail::require_unchecked_extent<V>(count);
    return detail::load_n_impl<V>(first, count, mask_value, f);
}

template<class V, class U, class... Flags>
constexpr V unchecked_load(const U* first, flags<Flags...> f = {}) {
    return detail::load_impl<V>(first, f);
}

template<class V, class U>
constexpr V unchecked_load(const U* first, simd_size_type count) {
    detail::require_unchecked_extent<V>(count);
    return detail::load_impl<V>(first, count);
}

template<class V, class U, class... Flags>
constexpr V unchecked_load(const U* first, const typename V::mask_type& mask_value, flags<Flags...> f = {}) {
    return detail::load_impl<V>(first, mask_value, f);
}

template<class V, class U>
constexpr V unchecked_load(const U* first, simd_size_type count, const typename V::mask_type& mask_value) {
    detail::require_unchecked_extent<V>(count);
    return detail::load_impl<V>(first, count, mask_value);
}

template<class V, class U, class... Flags>
constexpr V unchecked_load(const U* first, simd_size_type count, flags<Flags...> f) {
    detail::require_unchecked_extent<V>(count);
    return detail::load_impl<V>(first, count, f);
}

template<class V, class U, class... Flags>
constexpr V unchecked_load(const U* first, simd_size_type count, const typename V::mask_type& mask_value, flags<Flags...> f) {
    detail::require_unchecked_extent<V>(count);
    return detail::load_impl<V>(first, count, mask_value, f);
}

template<class V,
         class I,
         class S,
         class... Flags,
         typename enable_if<detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_random_access_load_store_iterator<I>::value), int>::type>
constexpr V unchecked_load(I first, S last, flags<Flags...> f) {
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    detail::require_unchecked_extent<V>(detail::iterator_distance(first, last));
    return detail::load_impl<V>(first, last, f);
}

template<class V,
         class I,
         class S,
         class... Flags,
         typename enable_if<detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_random_access_load_store_iterator<I>::value), int>::type>
constexpr V unchecked_load(I first, S last, const typename V::mask_type& mask_value, flags<Flags...> f) {
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    detail::require_unchecked_extent<V>(detail::iterator_distance(first, last));
    return detail::load_impl<V>(first, last, mask_value, f);
}

template<class T,
         class Abi,
         class I,
         class... Flags,
         typename enable_if<!is_pointer<typename detail::remove_cvref_t<I>>::value && detail::is_writable_load_store_iterator<I>::value, int>::type = 0>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, I first, simd_size_type count, flags<Flags...> f = {}) {
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    detail::require_unchecked_extent<basic_vec<T, Abi>>(count);
    detail::store_n_impl(value, first, count, f);
}

template<class T,
         class Abi,
         class I,
         class... Flags,
         typename enable_if<!is_pointer<typename detail::remove_cvref_t<I>>::value && detail::is_writable_load_store_iterator<I>::value, int>::type = 0>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, I first, simd_size_type count, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f = {}) {
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    detail::require_unchecked_extent<basic_vec<T, Abi>>(count);
    detail::store_n_impl(value, first, count, mask_value, f);
}

template<class T, class Abi, class U, class... Flags>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, U* first, flags<Flags...> f = {}) {
    detail::store_impl(value, first, f);
}

template<class T, class Abi, class U>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, U* first, simd_size_type count) {
    detail::require_unchecked_extent<basic_vec<T, Abi>>(count);
    detail::store_impl(value, first, count);
}

template<class T, class Abi, class U, class... Flags>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, U* first, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f = {}) {
    detail::store_impl(value, first, mask_value, f);
}

template<class T, class Abi, class U>
constexpr void unchecked_store(const basic_vec<T, Abi>& value,
                               U* first,
                               simd_size_type count,
                               const typename basic_vec<T, Abi>::mask_type& mask_value) {
    detail::require_unchecked_extent<basic_vec<T, Abi>>(count);
    detail::store_impl(value, first, count, mask_value);
}

template<class T, class Abi, class U, class... Flags>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, U* first, simd_size_type count, flags<Flags...> f) {
    detail::require_unchecked_extent<basic_vec<T, Abi>>(count);
    detail::store_impl(value, first, count, f);
}

	template<class T, class Abi, class U, class... Flags>
	constexpr void unchecked_store(const basic_vec<T, Abi>& value, U* first, simd_size_type count, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f) {
	    detail::require_unchecked_extent<basic_vec<T, Abi>>(count);
	    detail::store_impl(value, first, count, mask_value, f);
	}

	template<class V,
	         class I,
	         class Indices,
	         class... Flags,
	         typename enable_if<
	             detail::is_simd_index_vector<Indices>::value &&
	                 (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_random_access_load_store_iterator<I>::value),
	             int>::type = 0>
		constexpr V partial_gather_from(I first, simd_size_type count, const Indices& indices, flags<Flags...> f = {}) {
		    detail::require_nonnegative_extent(count);
		    detail::require_iterator_compatible_flags<I, flags<Flags...>>();

		    V result;
		    for (simd_size_type i = 0; i < static_cast<simd_size_type>(V::size); ++i) {
		        const simd_size_type offset = static_cast<simd_size_type>(indices[i]);
		        if (offset >= 0 && offset < count) {
		            detail::set_lane(result, i, detail::convert_or_copy<typename V::value_type>(*(first + offset), f));
		        } else {
		            detail::set_lane(result, i, typename V::value_type{});
		        }
		    }
		    return result;
		}

	template<class V,
	         class I,
	         class Indices,
	         class... Flags,
	         typename enable_if<
	             detail::is_simd_index_vector<Indices>::value &&
	                 (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_random_access_load_store_iterator<I>::value),
	             int>::type = 0>
		constexpr V partial_gather_from(I first,
		                                simd_size_type count,
		                                const Indices& indices,
		                                const typename V::mask_type& mask_value,
		                                flags<Flags...> f = {}) {
		    detail::require_nonnegative_extent(count);
		    detail::require_iterator_compatible_flags<I, flags<Flags...>>();

		    V result;
		    for (simd_size_type i = 0; i < static_cast<simd_size_type>(V::size); ++i) {
		        const simd_size_type offset = static_cast<simd_size_type>(indices[i]);
		        if (mask_value[i] && offset >= 0 && offset < count) {
		            detail::set_lane(result, i, detail::convert_or_copy<typename V::value_type>(*(first + offset), f));
		        } else {
		            detail::set_lane(result, i, typename V::value_type{});
		        }
		    }
		    return result;
		}

	template<class V, class I, class Indices, class... Flags,
	         typename enable_if<
	             detail::is_simd_index_vector<Indices>::value &&
	                 (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_random_access_load_store_iterator<I>::value),
	             int>::type = 0>
		constexpr V unchecked_gather_from(I first, const Indices& indices, flags<Flags...> f = {}) {
		    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
		    V result;
		    for (simd_size_type i = 0; i < static_cast<simd_size_type>(V::size); ++i) {
		        const simd_size_type offset = static_cast<simd_size_type>(indices[i]);
		        assert(offset >= 0);
		        detail::set_lane(result, i, detail::convert_or_copy<typename V::value_type>(*(first + offset), f));
		    }
		    return result;
		}

	template<class V, class I, class Indices, class... Flags,
	         typename enable_if<
	             detail::is_simd_index_vector<Indices>::value &&
	                 (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_random_access_load_store_iterator<I>::value),
	             int>::type = 0>
		constexpr V unchecked_gather_from(I first, const Indices& indices, const typename V::mask_type& mask_value, flags<Flags...> f = {}) {
		    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
		    V result;
		    for (simd_size_type i = 0; i < static_cast<simd_size_type>(V::size); ++i) {
		        const simd_size_type offset = static_cast<simd_size_type>(indices[i]);
		        assert(offset >= 0);
		        if (mask_value[i]) {
		            detail::set_lane(result, i, detail::convert_or_copy<typename V::value_type>(*(first + offset), f));
		        } else {
		            detail::set_lane(result, i, typename V::value_type{});
		        }
		    }
		    return result;
		}

	template<class T,
	         class Abi,
	         class I,
	         class Indices,
	         class... Flags,
	         typename enable_if<
	             detail::is_simd_index_vector<Indices>::value &&
	                 (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_writable_load_store_iterator<I>::value),
	             int>::type = 0>
		constexpr void partial_scatter_to(const basic_vec<T, Abi>& value, I first, simd_size_type count, const Indices& indices, flags<Flags...> f = {}) {
		    detail::require_nonnegative_extent(count);
		    detail::require_iterator_compatible_flags<I, flags<Flags...>>();

		    for (simd_size_type i = 0; i < static_cast<simd_size_type>(basic_vec<T, Abi>::size); ++i) {
		        const simd_size_type offset = static_cast<simd_size_type>(indices[i]);
		        if (offset >= 0 && offset < count) {
		            *(first + offset) = detail::convert_or_copy<typename iterator_traits<I>::value_type>(value[i], f);
		        }
		    }
		}

	template<class T,
	         class Abi,
	         class I,
	         class Indices,
	         class... Flags,
	         typename enable_if<
	             detail::is_simd_index_vector<Indices>::value &&
	                 (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_writable_load_store_iterator<I>::value),
	             int>::type = 0>
		constexpr void partial_scatter_to(const basic_vec<T, Abi>& value,
		                                 I first,
		                                 simd_size_type count,
		                                 const Indices& indices,
		                                 const typename basic_vec<T, Abi>::mask_type& mask_value,
		                                 flags<Flags...> f = {}) {
		    detail::require_nonnegative_extent(count);
		    detail::require_iterator_compatible_flags<I, flags<Flags...>>();

		    for (simd_size_type i = 0; i < static_cast<simd_size_type>(basic_vec<T, Abi>::size); ++i) {
		        const simd_size_type offset = static_cast<simd_size_type>(indices[i]);
		        if (mask_value[i] && offset >= 0 && offset < count) {
		            *(first + offset) = detail::convert_or_copy<typename iterator_traits<I>::value_type>(value[i], f);
		        }
		    }
		}

	template<class T, class Abi, class I, class Indices, class... Flags,
	         typename enable_if<
	             detail::is_simd_index_vector<Indices>::value &&
	                 (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_writable_load_store_iterator<I>::value),
	             int>::type = 0>
		constexpr void unchecked_scatter_to(const basic_vec<T, Abi>& value, I first, const Indices& indices, flags<Flags...> f = {}) {
		    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
		    for (simd_size_type i = 0; i < static_cast<simd_size_type>(basic_vec<T, Abi>::size); ++i) {
		        const simd_size_type offset = static_cast<simd_size_type>(indices[i]);
		        assert(offset >= 0);
		        *(first + offset) = detail::convert_or_copy<typename iterator_traits<I>::value_type>(value[i], f);
		    }
		}

	template<class T, class Abi, class I, class Indices, class... Flags,
	         typename enable_if<
	             detail::is_simd_index_vector<Indices>::value &&
	                 (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_writable_load_store_iterator<I>::value),
	             int>::type = 0>
		constexpr void unchecked_scatter_to(const basic_vec<T, Abi>& value,
		                                    I first,
		                                    const Indices& indices,
		                                    const typename basic_vec<T, Abi>::mask_type& mask_value,
		                                    flags<Flags...> f = {}) {
		    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
		    for (simd_size_type i = 0; i < static_cast<simd_size_type>(basic_vec<T, Abi>::size); ++i) {
		        const simd_size_type offset = static_cast<simd_size_type>(indices[i]);
		        assert(offset >= 0);
		        if (mask_value[i]) {
		            *(first + offset) = detail::convert_or_copy<typename iterator_traits<I>::value_type>(value[i], f);
		        }
		    }
		}

template<class T,
         class Abi,
         class I,
         class S,
         class... Flags,
         typename enable_if<detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_writable_load_store_iterator<I>::value), int>::type>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, I first, S last, flags<Flags...> f) {
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    detail::require_unchecked_extent<basic_vec<T, Abi>>(detail::iterator_distance(first, last));
    detail::store_impl(value, first, last, f);
}

template<class T,
         class Abi,
         class I,
         class S,
         class... Flags,
         typename enable_if<detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_writable_load_store_iterator<I>::value), int>::type>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, I first, S last, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f) {
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    detail::require_unchecked_extent<basic_vec<T, Abi>>(detail::iterator_distance(first, last));
    detail::store_impl(value, first, last, mask_value, f);
}

	template<class V, class Indices,
	         typename enable_if<detail::is_simd_index_vector<Indices>::value, int>::type>
	constexpr detail::permute_result_t<V, Indices> permute(const V& value, const Indices& indices) {
	    return detail::permute_from_indices(value, indices);
	}

	template<class T, class Abi>
	constexpr basic_vec<T, Abi> compress(const basic_vec<T, Abi>& value, const typename basic_vec<T, Abi>::mask_type& mask_value) noexcept {
	    basic_vec<T, Abi> result;
	    simd_size_type out = 0;
	    for (simd_size_type i = 0; i < static_cast<simd_size_type>(basic_vec<T, Abi>::size); ++i) {
	        if (mask_value[i]) {
	            detail::set_lane(result, out, value[i]);
	            ++out;
	        }
	    }
	    for (; out < static_cast<simd_size_type>(basic_vec<T, Abi>::size); ++out) {
	        detail::set_lane(result, out, T{});
	    }
	    return result;
	}

	template<class T, class Abi>
	constexpr basic_vec<T, Abi> expand(const basic_vec<T, Abi>& value, const typename basic_vec<T, Abi>::mask_type& mask_value) noexcept {
	    basic_vec<T, Abi> result;
	    simd_size_type in = 0;
	    for (simd_size_type i = 0; i < static_cast<simd_size_type>(basic_vec<T, Abi>::size); ++i) {
	        if (mask_value[i]) {
	            detail::set_lane(result, i, value[in]);
	            ++in;
	        } else {
	            detail::set_lane(result, i, T{});
	        }
	    }
	    return result;
	}

template<simd_size_type N, class V, class IndexMap,
         typename enable_if<!detail::is_simd_index_vector<detail::remove_cvref_t<IndexMap>>::value &&
             detail::is_static_permute_index_map<(N == 0 ? static_cast<simd_size_type>(V::size) : N), IndexMap>::value, int>::type>
constexpr resize_t<(N == 0 ? static_cast<simd_size_type>(V::size) : N), V> permute(const V& value, IndexMap&& index_map) {
    constexpr simd_size_type lane_count = N == 0 ? static_cast<simd_size_type>(V::size) : N;
    static_assert(lane_count > 0, "std::simd::permute requires a positive lane count");

    return detail::permute_from_map_impl(
        value,
        std::forward<IndexMap>(index_map),
        make_integer_sequence<simd_size_type, lane_count>{});
}

template<class Chunk,
         class V,
         typename enable_if<detail::is_data_parallel_type<detail::remove_cvref_t<Chunk>>::value &&
             is_same<detail::lane_mapped_value_t<detail::remove_cvref_t<Chunk>>, detail::lane_mapped_value_t<V>>::value,
             int>::type>
constexpr auto chunk(const V& value) {
    using chunk_type = detail::remove_cvref_t<Chunk>;
    constexpr simd_size_type chunk_size = static_cast<simd_size_type>(chunk_type::size);
    constexpr simd_size_type full_chunk_count = static_cast<simd_size_type>(V::size) / chunk_size;
    constexpr simd_size_type tail_size = static_cast<simd_size_type>(V::size) % chunk_size;

    static_assert(chunk_size > 0, "std::simd::chunk requires a positive chunk width");

    if constexpr (tail_size == 0) {
        return detail::chunk_array_impl<chunk_type>(value, make_index_sequence<static_cast<size_t>(full_chunk_count)>{});
    } else {
        return make_tuple(
            detail::chunk_array_impl<chunk_type>(value, make_index_sequence<static_cast<size_t>(full_chunk_count)>{}),
            detail::tail_chunk<resize_t<tail_size, chunk_type>>(value, full_chunk_count * chunk_size));
    }
}

template<simd_size_type N, class V>
constexpr auto chunk(const V& value) {
    static_assert(N > 0, "std::simd::chunk requires a positive chunk width");
    return simd::chunk<resize_t<N, V>>(value);
}

	template<class First, class... Rest>
	constexpr auto cat(const First& first, const Rest&... rest) {
    static_assert(conjunction<is_same<detail::lane_mapped_value_t<First>, detail::lane_mapped_value_t<Rest>>...>::value,
        "std::simd::cat requires matching lane value types");

    constexpr simd_size_type total_size = static_cast<simd_size_type>(First::size) + (static_cast<simd_size_type>(Rest::size) + ... + 0);
    using result_type = resize_t<total_size, First>;

    result_type result;
    simd_size_type offset = 0;
    const auto append = [&](const auto& current) {
        using current_type = decay_t<decltype(current)>;
        for (simd_size_type i = 0; i < static_cast<simd_size_type>(current_type::size); ++i) {
            detail::set_lane(result, offset + i, current[i]);
        }
        offset += static_cast<simd_size_type>(current_type::size);
    };

	    append(first);
		    (append(rest), ...);
		    return result;
		}

	namespace detail {

	template<class V, simd_size_type Offset, simd_size_type N>
	constexpr resize_t<N, V> split_piece(const V& value) {
	    static_assert(N > 0, "std::simd::split requires positive chunk sizes");
	    return resize_t<N, V>([&](auto lane) {
	        return value[Offset + decltype(lane)::value];
	    });
	}

	template<class V, simd_size_type Offset, simd_size_type First>
	constexpr auto split_sizes_impl(const V& value) {
	    return std::make_tuple(split_piece<V, Offset, First>(value));
	}

	template<class V, simd_size_type Offset, simd_size_type First, simd_size_type Second, simd_size_type... Rest>
	constexpr auto split_sizes_impl(const V& value) {
	    return std::tuple_cat(
	        std::make_tuple(split_piece<V, Offset, First>(value)),
	        split_sizes_impl<V, Offset + First, Second, Rest...>(value));
	}

	} // namespace detail

	template<class Chunk, class V>
	constexpr auto split(const V& value) {
	    return simd::chunk<Chunk>(value);
	}

	template<simd_size_type... Sizes, class V>
	constexpr auto split(const V& value) {
	    static_assert(sizeof...(Sizes) > 0, "std::simd::split requires at least one chunk size");
	    static_assert(((Sizes > 0) && ...), "std::simd::split requires positive chunk sizes");
	    static_assert((Sizes + ...) == static_cast<simd_size_type>(V::size),
	        "std::simd::split<Sizes...>(value) requires sum(Sizes...) == value.size()");
	    return detail::split_sizes_impl<V, 0, Sizes...>(value);
	}

		template<class V>
		constexpr V iota(typename V::value_type start = {}) noexcept {
		    using value_type = typename V::value_type;
		    return V([&](auto lane) {
		        return static_cast<value_type>(start + static_cast<value_type>(decltype(lane)::value));
	    });
	}

	template<class T, class Abi>
	constexpr basic_vec<T, Abi> abs(const basic_vec<T, Abi>& value) noexcept(noexcept(std::abs(std::declval<T>()))) {
	    basic_vec<T, Abi> result;
	    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
	        detail::set_lane(result, i, static_cast<T>(std::abs(value[i])));
	    }
	    return result;
	}

template<class T, class Abi>
constexpr basic_vec<T, Abi> min(const basic_vec<T, Abi>& left, const basic_vec<T, Abi>& right) noexcept {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(result, i, left[i] < right[i] ? left[i] : right[i]);
    }
    return result;
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> max(const basic_vec<T, Abi>& left, const basic_vec<T, Abi>& right) noexcept {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(result, i, left[i] < right[i] ? right[i] : left[i]);
    }
    return result;
}

template<class T, class Abi>
constexpr pair<basic_vec<T, Abi>, basic_vec<T, Abi>> minmax(const basic_vec<T, Abi>& left,
                                                            const basic_vec<T, Abi>& right) noexcept {
    return {min(left, right), max(left, right)};
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> clamp(const basic_vec<T, Abi>& value,
                                  const basic_vec<T, Abi>& low,
                                  const basic_vec<T, Abi>& high) noexcept {
    return min(max(value, low), high);
}

	template<class T, class Abi>
	constexpr basic_vec<T, Abi> sqrt(const basic_vec<T, Abi>& value) noexcept(noexcept(std::sqrt(std::declval<T>()))) {
	    basic_vec<T, Abi> result;
	    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
	        detail::set_lane(result, i, static_cast<T>(std::sqrt(value[i])));
	    }
	    return result;
	}

	template<class T, class Abi>
	constexpr basic_vec<T, Abi> floor(const basic_vec<T, Abi>& value) noexcept(noexcept(std::floor(std::declval<T>()))) {
	    basic_vec<T, Abi> result;
	    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
	        detail::set_lane(result, i, static_cast<T>(std::floor(value[i])));
	    }
	    return result;
	}

	template<class T, class Abi>
	constexpr basic_vec<T, Abi> ceil(const basic_vec<T, Abi>& value) noexcept(noexcept(std::ceil(std::declval<T>()))) {
	    basic_vec<T, Abi> result;
	    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
	        detail::set_lane(result, i, static_cast<T>(std::ceil(value[i])));
	    }
	    return result;
	}

	template<class T, class Abi>
	constexpr basic_vec<T, Abi> round(const basic_vec<T, Abi>& value) noexcept(noexcept(std::round(std::declval<T>()))) {
	    basic_vec<T, Abi> result;
	    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
	        detail::set_lane(result, i, static_cast<T>(std::round(value[i])));
	    }
	    return result;
	}

	template<class T, class Abi>
	constexpr basic_vec<T, Abi> trunc(const basic_vec<T, Abi>& value) noexcept(noexcept(std::trunc(std::declval<T>()))) {
	    basic_vec<T, Abi> result;
	    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
	        detail::set_lane(result, i, static_cast<T>(std::trunc(value[i])));
	    }
	    return result;
	}

	template<class T, class Abi>
	constexpr basic_vec<T, Abi> sin(const basic_vec<T, Abi>& value) noexcept(noexcept(std::sin(std::declval<T>()))) {
	    basic_vec<T, Abi> result;
	    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
	        detail::set_lane(result, i, static_cast<T>(std::sin(value[i])));
	    }
	    return result;
	}

	template<class T, class Abi>
	constexpr basic_vec<T, Abi> cos(const basic_vec<T, Abi>& value) noexcept(noexcept(std::cos(std::declval<T>()))) {
	    basic_vec<T, Abi> result;
	    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
	        detail::set_lane(result, i, static_cast<T>(std::cos(value[i])));
	    }
	    return result;
	}

	template<class T, class Abi>
	constexpr basic_vec<T, Abi> exp(const basic_vec<T, Abi>& value) noexcept(noexcept(std::exp(std::declval<T>()))) {
	    basic_vec<T, Abi> result;
	    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
	        detail::set_lane(result, i, static_cast<T>(std::exp(value[i])));
	    }
	    return result;
	}

	template<class T, class Abi>
	constexpr basic_vec<T, Abi> log(const basic_vec<T, Abi>& value) noexcept(noexcept(std::log(std::declval<T>()))) {
	    basic_vec<T, Abi> result;
	    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
	        detail::set_lane(result, i, static_cast<T>(std::log(value[i])));
	    }
	    return result;
	}

	template<class T, class Abi>
	constexpr basic_vec<T, Abi> pow(const basic_vec<T, Abi>& base, const basic_vec<T, Abi>& exponent) noexcept(noexcept(std::pow(std::declval<T>(), std::declval<T>()))) {
	    basic_vec<T, Abi> result;
	    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
	        detail::set_lane(result, i, static_cast<T>(std::pow(base[i], exponent[i])));
	    }
	    return result;
	}

template<class T, class Abi>
constexpr basic_vec<T, Abi> select(const typename basic_vec<T, Abi>::mask_type& mask_value,
                                   const basic_vec<T, Abi>& true_value,
                                   const basic_vec<T, Abi>& false_value) noexcept {
	basic_vec<T, Abi> result;
	for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
		detail::set_lane(result, i, mask_value[i] ? true_value[i] : false_value[i]);
	}
	return result;
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> select(const typename basic_vec<T, Abi>::mask_type& mask_value,
                                   const basic_vec<T, Abi>& true_value,
                                   const T& false_value) noexcept {
	return select(mask_value, true_value, basic_vec<T, Abi>(false_value));
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> select(const typename basic_vec<T, Abi>::mask_type& mask_value,
                                   const T& true_value,
                                   const basic_vec<T, Abi>& false_value) noexcept {
	return select(mask_value, basic_vec<T, Abi>(true_value), false_value);
}

template<size_t Bytes, class Abi>
constexpr basic_mask<Bytes, Abi> select(const basic_mask<Bytes, Abi>& mask_value,
                                        const basic_mask<Bytes, Abi>& true_value,
                                        const basic_mask<Bytes, Abi>& false_value) noexcept {
	basic_mask<Bytes, Abi> result;
	for (simd_size_type i = 0; i < static_cast<simd_size_type>(basic_mask<Bytes, Abi>::size); ++i) {
		detail::lane_ref(result, i) = mask_value[i] ? true_value[i] : false_value[i];
	}
	return result;
}

template<class T, class U,
         typename enable_if<
             !detail::is_data_parallel_type<detail::remove_cvref_t<T>>::value &&
                 !detail::is_data_parallel_type<detail::remove_cvref_t<U>>::value,
             int>::type = 0>
constexpr common_type_t<T, U> select(bool cond, const T& true_value, const U& false_value) noexcept(
	noexcept(static_cast<common_type_t<T, U>>(true_value)) && noexcept(static_cast<common_type_t<T, U>>(false_value))) {
	return cond ? static_cast<common_type_t<T, U>>(true_value) : static_cast<common_type_t<T, U>>(false_value);
}

namespace detail {

template<class>
struct is_basic_vec_type : false_type {};

template<class T, class Abi>
struct is_basic_vec_type<basic_vec<T, Abi>> : true_type {};

template<class>
struct is_basic_mask_type : false_type {};

template<size_t Bytes, class Abi>
struct is_basic_mask_type<basic_mask<Bytes, Abi>> : true_type {};

} // namespace detail

template<class To, class T, class Abi,
         typename enable_if<detail::is_basic_vec_type<detail::remove_cvref_t<To>>::value, int>::type = 0>
constexpr detail::remove_cvref_t<To> simd_cast(const basic_vec<T, Abi>& value) noexcept(
	noexcept(static_cast<typename detail::remove_cvref_t<To>::value_type>(std::declval<T>()))) {
	using to_type = detail::remove_cvref_t<To>;
	using to_value = typename to_type::value_type;
	static_assert(static_cast<simd_size_type>(to_type::size) == static_cast<simd_size_type>(basic_vec<T, Abi>::size),
		"std::simd::simd_cast requires matching lane count");
	static_assert(detail::is_value_preserving_conversion<T, to_value>::value,
		"std::simd::simd_cast requires a value-preserving conversion");

	to_type result;
	for (simd_size_type i = 0; i < static_cast<simd_size_type>(to_type::size); ++i) {
		detail::set_lane(result, i, static_cast<to_value>(value[i]));
	}
	return result;
}

template<class To, class T, class Abi,
         typename enable_if<detail::is_basic_vec_type<detail::remove_cvref_t<To>>::value, int>::type = 0>
constexpr detail::remove_cvref_t<To> static_simd_cast(const basic_vec<T, Abi>& value) noexcept(
	noexcept(static_cast<typename detail::remove_cvref_t<To>::value_type>(std::declval<T>()))) {
	using to_type = detail::remove_cvref_t<To>;
	using to_value = typename to_type::value_type;
	static_assert(static_cast<simd_size_type>(to_type::size) == static_cast<simd_size_type>(basic_vec<T, Abi>::size),
		"std::simd::static_simd_cast requires matching lane count");

	to_type result;
	for (simd_size_type i = 0; i < static_cast<simd_size_type>(to_type::size); ++i) {
		detail::set_lane(result, i, static_cast<to_value>(value[i]));
	}
	return result;
}

template<class To, size_t Bytes, class Abi,
         typename enable_if<detail::is_basic_mask_type<detail::remove_cvref_t<To>>::value, int>::type = 0>
constexpr detail::remove_cvref_t<To> simd_cast(const basic_mask<Bytes, Abi>& value) noexcept {
	using to_type = detail::remove_cvref_t<To>;
	static_assert(static_cast<simd_size_type>(to_type::size) == static_cast<simd_size_type>(basic_mask<Bytes, Abi>::size),
		"std::simd::simd_cast(mask) requires matching lane count");

	to_type result;
	for (simd_size_type i = 0; i < static_cast<simd_size_type>(to_type::size); ++i) {
		detail::lane_ref(result, i) = value[i];
	}
	return result;
}

template<class To, size_t Bytes, class Abi,
         typename enable_if<detail::is_basic_mask_type<detail::remove_cvref_t<To>>::value, int>::type = 0>
constexpr detail::remove_cvref_t<To> static_simd_cast(const basic_mask<Bytes, Abi>& value) noexcept {
	return simd_cast<To>(value);
}

template<class Mask, class V>
class where_expression;

template<class Mask, class V>
class const_where_expression;

template<size_t Bytes, class Abi, class T>
class where_expression<basic_mask<Bytes, Abi>, basic_vec<T, Abi>> {
public:
	using mask_type = basic_mask<Bytes, Abi>;
	using value_type = basic_vec<T, Abi>;

	constexpr where_expression(const mask_type& mask_value, value_type& value) noexcept
		: mask_(mask_value), value_(&value) {
		static_assert(Bytes == sizeof(T), "std::simd::where requires mask bytes to match vector value type");
		static_assert(static_cast<simd_size_type>(mask_type::size) == static_cast<simd_size_type>(value_type::size),
			"std::simd::where requires matching lane count");
	}

	constexpr where_expression& operator=(const value_type& other) noexcept {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) = other[i];
			}
		}
		return *this;
	}

	constexpr where_expression& operator=(const T& scalar) noexcept {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) = scalar;
			}
		}
		return *this;
	}

	constexpr where_expression& operator+=(const value_type& other) noexcept(noexcept(std::declval<T&>() += std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) += other[i];
			}
		}
		return *this;
	}

	constexpr where_expression& operator-=(const value_type& other) noexcept(noexcept(std::declval<T&>() -= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) -= other[i];
			}
		}
		return *this;
	}

	constexpr where_expression& operator*=(const value_type& other) noexcept(noexcept(std::declval<T&>() *= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) *= other[i];
			}
		}
		return *this;
	}

	constexpr where_expression& operator/=(const value_type& other) noexcept(noexcept(std::declval<T&>() /= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) /= other[i];
			}
		}
		return *this;
	}

	constexpr where_expression& operator+=(const T& scalar) noexcept(noexcept(std::declval<T&>() += std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) += scalar;
			}
		}
		return *this;
	}

	constexpr where_expression& operator-=(const T& scalar) noexcept(noexcept(std::declval<T&>() -= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) -= scalar;
			}
		}
		return *this;
	}

	constexpr where_expression& operator*=(const T& scalar) noexcept(noexcept(std::declval<T&>() *= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) *= scalar;
			}
		}
		return *this;
	}

	constexpr where_expression& operator/=(const T& scalar) noexcept(noexcept(std::declval<T&>() /= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) /= scalar;
			}
		}
		return *this;
	}

	template<class U = T, typename enable_if<is_integral<U>::value, int>::type = 0>
	constexpr where_expression& operator%=(const value_type& other) noexcept(noexcept(std::declval<T&>() %= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) %= other[i];
			}
		}
		return *this;
	}

	template<class U = T, typename enable_if<is_integral<U>::value, int>::type = 0>
	constexpr where_expression& operator&=(const value_type& other) noexcept(noexcept(std::declval<T&>() &= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) &= other[i];
			}
		}
		return *this;
	}

	template<class U = T, typename enable_if<is_integral<U>::value, int>::type = 0>
	constexpr where_expression& operator|=(const value_type& other) noexcept(noexcept(std::declval<T&>() |= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) |= other[i];
			}
		}
		return *this;
	}

	template<class U = T, typename enable_if<is_integral<U>::value, int>::type = 0>
	constexpr where_expression& operator^=(const value_type& other) noexcept(noexcept(std::declval<T&>() ^= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) ^= other[i];
			}
		}
		return *this;
	}

	template<class U = T, typename enable_if<is_integral<U>::value, int>::type = 0>
	constexpr where_expression& operator%=(const T& scalar) noexcept(noexcept(std::declval<T&>() %= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) %= scalar;
			}
		}
		return *this;
	}

	template<class U = T, typename enable_if<is_integral<U>::value, int>::type = 0>
	constexpr where_expression& operator&=(const T& scalar) noexcept(noexcept(std::declval<T&>() &= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) &= scalar;
			}
		}
		return *this;
	}

	template<class U = T, typename enable_if<is_integral<U>::value, int>::type = 0>
	constexpr where_expression& operator|=(const T& scalar) noexcept(noexcept(std::declval<T&>() |= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) |= scalar;
			}
		}
		return *this;
	}

	template<class U = T, typename enable_if<is_integral<U>::value, int>::type = 0>
	constexpr where_expression& operator^=(const T& scalar) noexcept(noexcept(std::declval<T&>() ^= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) ^= scalar;
			}
		}
		return *this;
	}

	template<class Shift, class U = T,
	         typename enable_if<is_integral<U>::value && is_integral<Shift>::value, int>::type = 0>
	constexpr where_expression& operator<<=(Shift shift) noexcept(noexcept(std::declval<T&>() <<= shift)) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) <<= shift;
			}
		}
		return *this;
	}

	template<class Shift, class U = T,
	         typename enable_if<is_integral<U>::value && is_integral<Shift>::value, int>::type = 0>
	constexpr where_expression& operator>>=(Shift shift) noexcept(noexcept(std::declval<T&>() >>= shift)) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) >>= shift;
			}
		}
		return *this;
	}

	template<class U, class OtherAbi>
	constexpr where_expression& operator=(const basic_vec<U, OtherAbi>& other) noexcept {
		static_assert(static_cast<simd_size_type>(basic_vec<U, OtherAbi>::size) == static_cast<simd_size_type>(value_type::size),
			"std::simd::where assignment requires matching lane count");
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) = static_cast<T>(other[i]);
			}
		}
		return *this;
	}

	template<class U, class... Flags>
	constexpr void copy_from(const U* first, flags<Flags...> f = {}) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) = detail::convert_or_copy<T>(first[static_cast<size_t>(i)], f);
			}
		}
	}

	template<class U, class... Flags>
	constexpr void copy_to(U* first, flags<Flags...> f = {}) const {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				first[static_cast<size_t>(i)] = detail::convert_or_copy<U>((*value_)[i], f);
			}
		}
	}

private:
	mask_type mask_;
	value_type* value_;
};

template<size_t Bytes, class Abi>
class where_expression<basic_mask<Bytes, Abi>, basic_mask<Bytes, Abi>> {
public:
	using mask_type = basic_mask<Bytes, Abi>;
	using value_type = basic_mask<Bytes, Abi>;

	constexpr where_expression(const mask_type& mask_value, value_type& value) noexcept
		: mask_(mask_value), value_(&value) {}

	constexpr where_expression& operator=(const value_type& other) noexcept {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) = other[i];
			}
		}
		return *this;
	}

	constexpr where_expression& operator=(bool scalar) noexcept {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) = scalar;
			}
		}
		return *this;
	}

private:
	mask_type mask_;
	value_type* value_;
};

template<size_t Bytes, class Abi, class T>
class const_where_expression<basic_mask<Bytes, Abi>, basic_vec<T, Abi>> {
public:
	using mask_type = basic_mask<Bytes, Abi>;
	using value_type = basic_vec<T, Abi>;

	constexpr const_where_expression(const mask_type& mask_value, const value_type& value) noexcept
		: mask_(mask_value), value_(&value) {
		static_assert(Bytes == sizeof(T), "std::simd::where requires mask bytes to match vector value type");
		static_assert(static_cast<simd_size_type>(mask_type::size) == static_cast<simd_size_type>(value_type::size),
			"std::simd::where requires matching lane count");
	}

	template<class U, class... Flags>
	constexpr void copy_to(U* first, flags<Flags...> f = {}) const {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				first[static_cast<size_t>(i)] = detail::convert_or_copy<U>((*value_)[i], f);
			}
		}
	}

private:
	mask_type mask_;
	const value_type* value_;
};

template<size_t Bytes, class Abi>
class const_where_expression<basic_mask<Bytes, Abi>, basic_mask<Bytes, Abi>> {
public:
	using mask_type = basic_mask<Bytes, Abi>;
	using value_type = basic_mask<Bytes, Abi>;

	constexpr const_where_expression(const mask_type& mask_value, const value_type& value) noexcept
		: mask_(mask_value), value_(&value) {}

private:
	mask_type mask_;
	const value_type* value_;
};

template<size_t Bytes, class Abi, class T>
constexpr where_expression<basic_mask<Bytes, Abi>, basic_vec<T, Abi>> where(const basic_mask<Bytes, Abi>& mask_value,
                                                                            basic_vec<T, Abi>& value) noexcept {
	return where_expression<basic_mask<Bytes, Abi>, basic_vec<T, Abi>>(mask_value, value);
}

template<size_t Bytes, class Abi, class T>
constexpr const_where_expression<basic_mask<Bytes, Abi>, basic_vec<T, Abi>> where(const basic_mask<Bytes, Abi>& mask_value,
                                                                                  const basic_vec<T, Abi>& value) noexcept {
	return const_where_expression<basic_mask<Bytes, Abi>, basic_vec<T, Abi>>(mask_value, value);
}

template<size_t Bytes, class Abi>
constexpr where_expression<basic_mask<Bytes, Abi>, basic_mask<Bytes, Abi>> where(const basic_mask<Bytes, Abi>& mask_value,
                                                                                 basic_mask<Bytes, Abi>& value) noexcept {
	return where_expression<basic_mask<Bytes, Abi>, basic_mask<Bytes, Abi>>(mask_value, value);
}

template<size_t Bytes, class Abi>
constexpr const_where_expression<basic_mask<Bytes, Abi>, basic_mask<Bytes, Abi>> where(const basic_mask<Bytes, Abi>& mask_value,
                                                                                       const basic_mask<Bytes, Abi>& value) noexcept {
	return const_where_expression<basic_mask<Bytes, Abi>, basic_mask<Bytes, Abi>>(mask_value, value);
}

template<class T, class Abi>
constexpr where_expression<typename basic_vec<T, Abi>::mask_type, basic_vec<T, Abi>> where(bool cond, basic_vec<T, Abi>& value) noexcept {
	typename basic_vec<T, Abi>::mask_type mask_value(cond);
	return where(mask_value, value);
}

template<class T, class Abi>
constexpr const_where_expression<typename basic_vec<T, Abi>::mask_type, basic_vec<T, Abi>> where(bool cond, const basic_vec<T, Abi>& value) noexcept {
	typename basic_vec<T, Abi>::mask_type mask_value(cond);
	return where(mask_value, value);
}

template<class T,
         class Abi,
         class BinaryOperation = plus<>,
         typename enable_if<!is_same<detail::remove_cvref_t<BinaryOperation>, typename basic_vec<T, Abi>::mask_type>::value, int>::type = 0>
constexpr T reduce(const basic_vec<T, Abi>& value, BinaryOperation binary_op = {}) noexcept(
	noexcept(std::declval<BinaryOperation&>()(std::declval<T>(), std::declval<T>()))) {
	T result = value[0];
	for (simd_size_type i = 1; i < basic_vec<T, Abi>::size; ++i) {
		result = binary_op(result, value[i]);
	}
	return result;
}

template<class T, class Abi, class BinaryOperation = plus<>>
constexpr T reduce(const basic_vec<T, Abi>& value,
                   const typename basic_vec<T, Abi>::mask_type& mask_value,
                   BinaryOperation binary_op = {},
                   T identity_element = detail::reduction_identity<T, BinaryOperation>::value()) noexcept(
	noexcept(std::declval<BinaryOperation&>()(std::declval<T>(), std::declval<T>()))) {
	T result = identity_element;
	for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
		if (mask_value[i]) {
			result = binary_op(result, value[i]);
		}
	}
	return result;
}

template<class T, class Abi>
constexpr T reduce_min(const basic_vec<T, Abi>& value) noexcept {
	T result = value[0];
	for (simd_size_type i = 1; i < basic_vec<T, Abi>::size; ++i) {
		if (value[i] < result) {
			result = value[i];
		}
	}
	return result;
}

template<class T, class Abi>
constexpr T reduce_min(const basic_vec<T, Abi>& value, const typename basic_vec<T, Abi>::mask_type& mask_value) noexcept {
	simd_size_type first = 0;
	while (first < basic_vec<T, Abi>::size && !mask_value[first]) {
		++first;
	}

	if (first == basic_vec<T, Abi>::size) {
		return numeric_limits<T>::max();
	}

	T result = value[first];
	for (simd_size_type i = first + 1; i < basic_vec<T, Abi>::size; ++i) {
		if (mask_value[i] && value[i] < result) {
			result = value[i];
		}
	}
	return result;
}

template<class T, class Abi>
constexpr T reduce_max(const basic_vec<T, Abi>& value) noexcept {
	T result = value[0];
	for (simd_size_type i = 1; i < basic_vec<T, Abi>::size; ++i) {
		if (value[i] > result) {
			result = value[i];
		}
	}
	return result;
}

template<class T, class Abi>
constexpr T reduce_max(const basic_vec<T, Abi>& value, const typename basic_vec<T, Abi>::mask_type& mask_value) noexcept {
	simd_size_type first = 0;
	while (first < basic_vec<T, Abi>::size && !mask_value[first]) {
		++first;
	}

	if (first == basic_vec<T, Abi>::size) {
		return numeric_limits<T>::lowest();
	}

	T result = value[first];
	for (simd_size_type i = first + 1; i < basic_vec<T, Abi>::size; ++i) {
		if (mask_value[i] && value[i] > result) {
			result = value[i];
		}
	}
	return result;
}

template<size_t Bytes, class Abi>
constexpr bool all_of(const basic_mask<Bytes, Abi>& value) noexcept {
	for (simd_size_type i = 0; i < basic_mask<Bytes, Abi>::size; ++i) {
		if (!value[i]) {
			return false;
		}
	}
	return true;
}

template<size_t Bytes, class Abi>
constexpr bool any_of(const basic_mask<Bytes, Abi>& value) noexcept {
	for (simd_size_type i = 0; i < basic_mask<Bytes, Abi>::size; ++i) {
		if (value[i]) {
			return true;
		}
	}
	return false;
}

template<size_t Bytes, class Abi>
constexpr bool none_of(const basic_mask<Bytes, Abi>& value) noexcept {
	return !any_of(value);
}

template<size_t Bytes, class Abi>
constexpr simd_size_type reduce_count(const basic_mask<Bytes, Abi>& value) noexcept {
	simd_size_type result = 0;
	for (simd_size_type i = 0; i < basic_mask<Bytes, Abi>::size; ++i) {
		if (value[i]) {
			++result;
		}
	}
	return result;
}

template<size_t Bytes, class Abi>
constexpr simd_size_type reduce_min_index(const basic_mask<Bytes, Abi>& value) noexcept {
	for (simd_size_type i = 0; i < basic_mask<Bytes, Abi>::size; ++i) {
		if (value[i]) {
			return i;
		}
	}
	return 0;
}

template<size_t Bytes, class Abi>
constexpr simd_size_type reduce_max_index(const basic_mask<Bytes, Abi>& value) noexcept {
	for (simd_size_type i = basic_mask<Bytes, Abi>::size; i > 0; --i) {
		if (value[i - 1]) {
			return i - 1;
		}
	}
	return 0;
}

constexpr bool all_of(bool value) noexcept {
	return value;
}

constexpr bool any_of(bool value) noexcept {
	return value;
}

constexpr bool none_of(bool value) noexcept {
	return !value;
}

constexpr simd_size_type reduce_count(bool value) noexcept {
	return value ? 1 : 0;
}

constexpr simd_size_type reduce_min_index(bool) noexcept {
	return 0;
}

constexpr simd_size_type reduce_max_index(bool) noexcept {
	return 0;
}

} // namespace simd
} // namespace std

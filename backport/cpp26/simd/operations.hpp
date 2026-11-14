#pragma once

template<class V, class Indices,
         typename enable_if<
             __detail::is_simd_index_vector<Indices>::value,
             int>::type>
constexpr __detail::permute_result_t<V, Indices> permute(
    const V& value,
    const Indices& indices) {
    return __detail::permute_from_indices(value, indices);
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> compress(
    const basic_vec<T, Abi>& value,
    const typename basic_vec<T, Abi>::mask_type& mask_value,
    const type_identity_t<T>& fill_value) noexcept {
    basic_vec<T, Abi> result;
    __simd_size_type out = 0;
    for (__simd_size_type i = 0;
         i < static_cast<__simd_size_type>(basic_vec<T, Abi>::size);
         ++i) {
        if (__detail::mask_lane(mask_value, i)) {
            __detail::set_lane(result, out, value[i]);
            ++out;
        }
    }
    for (; out < static_cast<__simd_size_type>(basic_vec<T, Abi>::size); ++out) {
        __detail::set_lane(result, out, fill_value);
    }
    return result;
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> compress(
    const basic_vec<T, Abi>& value,
    const typename basic_vec<T, Abi>::mask_type& mask_value) noexcept {
    return simd::compress(value, mask_value, T{});
}

template<size_t Bytes, class Abi>
constexpr basic_mask<Bytes, Abi> compress(
    const basic_mask<Bytes, Abi>& value,
    const type_identity_t<basic_mask<Bytes, Abi>>& mask_value,
    bool fill_value = false) noexcept {
    basic_mask<Bytes, Abi> result(fill_value);
    __simd_size_type out = 0;
    for (__simd_size_type i = 0;
         i < static_cast<__simd_size_type>(basic_mask<Bytes, Abi>::size);
         ++i) {
        if (__detail::mask_lane(mask_value, i)) {
            __detail::copy_lane(result, out, value, i);
            ++out;
        }
    }
    for (; out < static_cast<__simd_size_type>(basic_mask<Bytes, Abi>::size); ++out) {
        __detail::set_lane(result, out, fill_value);
    }
    return result;
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> expand(
    const basic_vec<T, Abi>& value,
    const typename basic_vec<T, Abi>::mask_type& mask_value,
    const basic_vec<T, Abi>& original) noexcept {
    basic_vec<T, Abi> result = original;
    __simd_size_type in = 0;
    for (__simd_size_type i = 0;
         i < static_cast<__simd_size_type>(basic_vec<T, Abi>::size);
         ++i) {
        if (__detail::mask_lane(mask_value, i)) {
            __detail::set_lane(result, i, value[in]);
            ++in;
        }
    }
    return result;
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> expand(
    const basic_vec<T, Abi>& value,
    const typename basic_vec<T, Abi>::mask_type& mask_value) noexcept {
    return simd::expand(value, mask_value, basic_vec<T, Abi>{});
}

template<size_t Bytes, class Abi>
constexpr basic_mask<Bytes, Abi> expand(
    const basic_mask<Bytes, Abi>& value,
    const type_identity_t<basic_mask<Bytes, Abi>>& mask_value,
    const basic_mask<Bytes, Abi>& original = basic_mask<Bytes, Abi>{}) noexcept {
    basic_mask<Bytes, Abi> result = original;
    __simd_size_type in = 0;
    for (__simd_size_type i = 0;
         i < static_cast<__simd_size_type>(basic_mask<Bytes, Abi>::size);
         ++i) {
        if (__detail::mask_lane(mask_value, i)) {
            __detail::copy_lane(result, i, value, in);
            ++in;
        }
    }
    return result;
}

template<
    __simd_size_type N,
    class V,
    class IndexMap,
    typename enable_if<
        !__detail::is_simd_index_vector<__detail::remove_cvref_t<IndexMap>>::value &&
            __detail::is_static_permute_index_map<IndexMap>::value,
        int>::type>
constexpr resize_t<(N == 0 ? static_cast<__simd_size_type>(V::size) : N), V>
permute(const V& value, IndexMap&& index_map) {
    constexpr __simd_size_type lane_count = N == 0 ? static_cast<__simd_size_type>(V::size) : N;
    static_assert(lane_count > 0, "std::simd::permute requires a positive lane count");

    return __detail::permute_from_map_impl(
        value,
        index_map,
        make_integer_sequence<__simd_size_type, lane_count>{});
}

namespace __detail {

template<class Chunk>
struct chunk_type_traits {
    static constexpr bool enabled = false;
};

template<class T, class Abi>
struct chunk_type_traits<basic_vec<T, Abi>> {
    using value_type = T;
    static constexpr bool enabled = is_enabled_basic_vec<T, Abi>::value;
};

template<size_t Bytes, class Abi>
struct chunk_type_traits<basic_mask<Bytes, Abi>> {
    static constexpr size_t mask_bytes = Bytes;
    static constexpr bool enabled = is_enabled_basic_mask<Bytes, Abi>::value;
};

template<class Chunk, class V>
concept chunk_has_valid_tail = V::size % Chunk::size == 0 || requires {
    typename resize_t<V::size % Chunk::size, Chunk>;
};

template<class Chunk, class V>
constexpr auto chunk_impl(const V& value) noexcept {
    using chunk_type = Chunk;
    constexpr __simd_size_type chunk_size = static_cast<__simd_size_type>(chunk_type::size);
    constexpr __simd_size_type full_chunk_count = static_cast<__simd_size_type>(V::size) / chunk_size;
    constexpr __simd_size_type tail_size = static_cast<__simd_size_type>(V::size) % chunk_size;

    static_assert(chunk_size > 0, "std::simd::chunk requires a positive chunk width");

    if constexpr (tail_size == 0) {
        return __detail::chunk_array_impl<chunk_type>(
            value,
            make_index_sequence<static_cast<size_t>(full_chunk_count)>{});
    } else {
        return tuple_cat(
            __detail::chunk_tuple_impl<chunk_type>(
                value,
                make_index_sequence<static_cast<size_t>(full_chunk_count)>{}),
            make_tuple(__detail::tail_chunk<resize_t<tail_size, chunk_type>>(
                value,
                full_chunk_count * chunk_size)));
    }
}

template<class First, class... Rest>
using cat_result_t = resize_t<
    static_cast<__simd_size_type>(First::size) +
        (static_cast<__simd_size_type>(Rest::size) + ... + 0),
    First>;

template<class Result, class Current>
constexpr void cat_append(Result& result, __simd_size_type& offset, const Current& current) {
    using current_type = decay_t<Current>;
    for (__simd_size_type i = 0; i < static_cast<__simd_size_type>(current_type::size); ++i) {
        __detail::copy_lane(result, offset + i, current, i);
    }
    offset += static_cast<__simd_size_type>(current_type::size);
}

template<class Result, class First, class... Rest>
constexpr Result cat_impl(const First& first, const Rest&... rest) noexcept {
    Result result;
    __simd_size_type offset = 0;
    __detail::cat_append(result, offset, first);
    (__detail::cat_append(result, offset, rest), ...);
    return result;
}

} // namespace __detail

template<class Chunk, class Abi>
    requires __detail::chunk_type_traits<Chunk>::enabled &&
             __detail::chunk_has_valid_tail<Chunk,
                 basic_vec<typename __detail::chunk_type_traits<Chunk>::value_type, Abi>>
constexpr auto chunk(
    const basic_vec<typename __detail::chunk_type_traits<Chunk>::value_type, Abi>& value) noexcept {
    return __detail::chunk_impl<Chunk>(value);
}

template<class Chunk, class Abi>
    requires __detail::chunk_type_traits<Chunk>::enabled &&
             __detail::chunk_has_valid_tail<Chunk,
                 basic_mask<__detail::chunk_type_traits<Chunk>::mask_bytes, Abi>>
constexpr auto chunk(
    const basic_mask<__detail::chunk_type_traits<Chunk>::mask_bytes, Abi>& value) noexcept {
    return __detail::chunk_impl<Chunk>(value);
}

template<__simd_size_type N, class T, class Abi>
    requires requires(const basic_vec<T, Abi>& value) {
        simd::chunk<resize_t<N, basic_vec<T, Abi>>>(value);
    }
constexpr auto chunk(const basic_vec<T, Abi>& value) noexcept {
    return simd::chunk<resize_t<N, basic_vec<T, Abi>>>(value);
}

template<__simd_size_type N, size_t Bytes, class Abi>
    requires requires(const basic_mask<Bytes, Abi>& value) {
        simd::chunk<resize_t<N, basic_mask<Bytes, Abi>>>(value);
    }
constexpr auto chunk(const basic_mask<Bytes, Abi>& value) noexcept {
    return simd::chunk<resize_t<N, basic_mask<Bytes, Abi>>>(value);
}

template<class T, class FirstAbi, class... Abis>
constexpr __detail::cat_result_t<basic_vec<T, FirstAbi>, basic_vec<T, Abis>...>
cat(const basic_vec<T, FirstAbi>& first, const basic_vec<T, Abis>&... rest) noexcept {
    using result_type = __detail::cat_result_t<basic_vec<T, FirstAbi>, basic_vec<T, Abis>...>;
    return __detail::cat_impl<result_type>(first, rest...);
}

template<size_t Bytes, class FirstAbi, class... Abis>
constexpr __detail::cat_result_t<basic_mask<Bytes, FirstAbi>, basic_mask<Bytes, Abis>...>
cat(const basic_mask<Bytes, FirstAbi>& first, const basic_mask<Bytes, Abis>&... rest) noexcept {
    using result_type = __detail::cat_result_t<basic_mask<Bytes, FirstAbi>, basic_mask<Bytes, Abis>...>;
    return __detail::cat_impl<result_type>(first, rest...);
}

namespace __detail {

template<class T>
struct iota_vec_traits {
    static constexpr bool enabled = false;
};

template<class T, class Abi>
struct iota_vec_traits<basic_vec<T, Abi>> {
    static constexpr bool enabled =
        is_enabled_basic_vec<T, Abi>::value &&
        is_arithmetic<T>::value &&
        static_cast<long double>(basic_vec<T, Abi>::size - 1) <=
            static_cast<long double>(numeric_limits<T>::max());
};

template<class T>
struct iota_lane_generator {
    constexpr T operator()(T index) const { return index; }
};

template<class T>
constexpr T make_iota() {
    static_assert(
        (is_supported_scalar_value<T>::value && is_arithmetic<T>::value) ||
            iota_vec_traits<T>::enabled,
        "std::simd::iota requires an arithmetic vectorizable type or an enabled "
        "arithmetic basic_vec");

    if constexpr (is_arithmetic<T>::value) {
        return T{};
    } else {
        return T(iota_lane_generator<typename T::value_type>{});
    }
}

} // namespace __detail

template<class T>
constexpr T iota = __detail::make_iota<T>();

template<class T, class Abi>
constexpr basic_vec<T, Abi> min(
    const basic_vec<T, Abi>& left,
    const basic_vec<T, Abi>& right) noexcept
    requires requires(const T& left_value, const T& right_value) {
        static_cast<bool>(left_value < right_value);
    }
{
    basic_vec<T, Abi> result;
    for (__simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        __detail::set_lane(result, i, right[i] < left[i] ? right[i] : left[i]);
    }
    return result;
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> max(
    const basic_vec<T, Abi>& left,
    const basic_vec<T, Abi>& right) noexcept
    requires requires(const T& left_value, const T& right_value) {
        static_cast<bool>(left_value < right_value);
    }
{
    basic_vec<T, Abi> result;
    for (__simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        __detail::set_lane(result, i, left[i] < right[i] ? right[i] : left[i]);
    }
    return result;
}

template<class T, class Abi>
constexpr pair<basic_vec<T, Abi>, basic_vec<T, Abi>> minmax(const basic_vec<T, Abi>& left,
                                                            const basic_vec<T, Abi>& right) noexcept
    requires requires(const T& left_value, const T& right_value) {
        static_cast<bool>(left_value < right_value);
    }
{
    return {min(left, right), max(left, right)};
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> clamp(const basic_vec<T, Abi>& value,
                                  const basic_vec<T, Abi>& low,
                                  const basic_vec<T, Abi>& high)
    requires requires(const T& left_value, const T& right_value) {
        static_cast<bool>(left_value < right_value);
    }
{
    basic_vec<T, Abi> result;
    for (__simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        __detail::set_lane(
            result,
            i,
            value[i] < low[i] ? low[i]
                              : (high[i] < value[i] ? high[i] : value[i]));
    }
    return result;
}

template<
    size_t Bytes,
    class Abi,
    class T,
    class U,
    class Result = decltype(__simd_select_impl(
        declval<const basic_mask<Bytes, Abi>&>(),
        declval<const T&>(),
        declval<const U&>()))>
constexpr Result select(const basic_mask<Bytes, Abi>& mask_value,
                        const T& true_value,
                        const U& false_value) noexcept(
    noexcept(__simd_select_impl(mask_value, true_value, false_value))) {
    return __simd_select_impl(mask_value, true_value, false_value);
}

template<class T, class U>
constexpr auto select(bool cond, const T& true_value, const U& false_value)
    -> remove_cvref_t<decltype(cond ? true_value : false_value)> {
    return cond ? true_value : false_value;
}

#include "operations_bit.hpp"
#include "operations_math.hpp"
#include "operations_complex.hpp"

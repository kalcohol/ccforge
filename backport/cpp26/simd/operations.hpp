#pragma once

template<class V, class Indices,
         typename enable_if<
             detail::is_simd_index_vector<Indices>::value,
             int>::type>
constexpr detail::permute_result_t<V, Indices> permute(
    const V& value,
    const Indices& indices) {
    return detail::permute_from_indices(value, indices);
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> compress(
    const basic_vec<T, Abi>& value,
    const typename basic_vec<T, Abi>::mask_type& mask_value,
    T fill_value) noexcept {
    basic_vec<T, Abi> result;
    simd_size_type out = 0;
    for (simd_size_type i = 0;
         i < static_cast<simd_size_type>(basic_vec<T, Abi>::size);
         ++i) {
        if (mask_value[i]) {
            detail::set_lane(result, out, value[i]);
            ++out;
        }
    }
    for (; out < static_cast<simd_size_type>(basic_vec<T, Abi>::size); ++out) {
        detail::set_lane(result, out, fill_value);
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
    const basic_mask<Bytes, Abi>& mask_value,
    bool fill_value = false) noexcept {
    basic_mask<Bytes, Abi> result(fill_value);
    simd_size_type out = 0;
    for (simd_size_type i = 0;
         i < static_cast<simd_size_type>(basic_mask<Bytes, Abi>::size);
         ++i) {
        if (mask_value[i]) {
            detail::lane_ref(result, out) = value[i];
            ++out;
        }
    }
    for (; out < static_cast<simd_size_type>(basic_mask<Bytes, Abi>::size); ++out) {
        detail::lane_ref(result, out) = fill_value;
    }
    return result;
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> expand(
    const basic_vec<T, Abi>& value,
    const typename basic_vec<T, Abi>::mask_type& mask_value,
    const basic_vec<T, Abi>& original) noexcept {
    basic_vec<T, Abi> result = original;
    simd_size_type in = 0;
    for (simd_size_type i = 0;
         i < static_cast<simd_size_type>(basic_vec<T, Abi>::size);
         ++i) {
        if (mask_value[i]) {
            detail::set_lane(result, i, value[in]);
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
    const basic_mask<Bytes, Abi>& mask_value,
    const basic_mask<Bytes, Abi>& original = basic_mask<Bytes, Abi>{}) noexcept {
    basic_mask<Bytes, Abi> result = original;
    simd_size_type in = 0;
    for (simd_size_type i = 0;
         i < static_cast<simd_size_type>(basic_mask<Bytes, Abi>::size);
         ++i) {
        if (mask_value[i]) {
            detail::lane_ref(result, i) = value[in];
            ++in;
        }
    }
    return result;
}

template<
    simd_size_type N,
    class V,
    class IndexMap,
    typename enable_if<
        !detail::is_simd_index_vector<detail::remove_cvref_t<IndexMap>>::value &&
            detail::is_static_permute_index_map<
                (N == 0 ? static_cast<simd_size_type>(V::size) : N),
                IndexMap>::value,
        int>::type>
constexpr resize_t<(N == 0 ? static_cast<simd_size_type>(V::size) : N), V>
permute(const V& value, IndexMap&& index_map) {
    constexpr simd_size_type lane_count = N == 0 ? static_cast<simd_size_type>(V::size) : N;
    static_assert(lane_count > 0, "std::simd::permute requires a positive lane count");

    return detail::permute_from_map_impl(
        value,
        std::forward<IndexMap>(index_map),
        make_integer_sequence<simd_size_type, lane_count>{});
}

template<class Chunk,
         class V,
         typename enable_if<
             detail::is_data_parallel_type<detail::remove_cvref_t<Chunk>>::value &&
                 is_same<
                     detail::lane_mapped_value_t<detail::remove_cvref_t<Chunk>>,
                     detail::lane_mapped_value_t<V>>::value,
             int>::type>
constexpr auto chunk(const V& value) {
    using chunk_type = detail::remove_cvref_t<Chunk>;
    constexpr simd_size_type chunk_size = static_cast<simd_size_type>(chunk_type::size);
    constexpr simd_size_type full_chunk_count = static_cast<simd_size_type>(V::size) / chunk_size;
    constexpr simd_size_type tail_size = static_cast<simd_size_type>(V::size) % chunk_size;

    static_assert(chunk_size > 0, "std::simd::chunk requires a positive chunk width");

    if constexpr (tail_size == 0) {
        return detail::chunk_array_impl<chunk_type>(
            value,
            make_index_sequence<static_cast<size_t>(full_chunk_count)>{});
    } else {
        return tuple_cat(
            detail::chunk_tuple_impl<chunk_type>(
                value,
                make_index_sequence<static_cast<size_t>(full_chunk_count)>{}),
            make_tuple(detail::tail_chunk<resize_t<tail_size, chunk_type>>(
                value,
                full_chunk_count * chunk_size)));
    }
}

template<simd_size_type N, class V>
constexpr auto chunk(const V& value) {
    static_assert(N > 0, "std::simd::chunk requires a positive chunk width");
    return simd::chunk<resize_t<N, V>>(value);
}

template<class First, class... Rest>
constexpr auto cat(const First& first, const Rest&... rest) {
    static_assert(
        conjunction<
            is_same<
                detail::lane_mapped_value_t<First>,
                detail::lane_mapped_value_t<Rest>>...>::value,
        "std::simd::cat requires matching lane value types");

    constexpr simd_size_type total_size =
        static_cast<simd_size_type>(First::size) +
        (static_cast<simd_size_type>(Rest::size) + ... + 0);
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
constexpr T make_iota() {
    static_assert(
        (is_supported_scalar_value<T>::value && is_arithmetic<T>::value) ||
            iota_vec_traits<T>::enabled,
        "std::simd::iota requires an arithmetic vectorizable type or an enabled "
        "arithmetic basic_vec");

    if constexpr (is_arithmetic<T>::value) {
        return T{};
    } else {
        return T([](typename T::value_type index) { return index; });
    }
}

} // namespace detail

template<class T>
constexpr T iota = detail::make_iota<T>();

template<class T, class Abi>
constexpr basic_vec<T, Abi> min(
    const basic_vec<T, Abi>& left,
    const basic_vec<T, Abi>& right) noexcept
    requires requires(const T& left_value, const T& right_value) {
        static_cast<bool>(left_value < right_value);
    }
{
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(result, i, right[i] < left[i] ? right[i] : left[i]);
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
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(result, i, left[i] < right[i] ? right[i] : left[i]);
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
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(
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
    class Result = decltype(simd_select_impl(
        declval<const basic_mask<Bytes, Abi>&>(),
        declval<const T&>(),
        declval<const U&>()))>
constexpr Result select(const basic_mask<Bytes, Abi>& mask_value,
                        const T& true_value,
                        const U& false_value) noexcept(
    noexcept(simd_select_impl(mask_value, true_value, false_value))) {
    return simd_select_impl(mask_value, true_value, false_value);
}

template<class T, class U>
constexpr auto select(bool cond, const T& true_value, const U& false_value)
    -> remove_cvref_t<decltype(cond ? true_value : false_value)> {
    return cond ? true_value : false_value;
}

#include "operations_bit.hpp"
#include "operations_math.hpp"
#include "operations_complex.hpp"

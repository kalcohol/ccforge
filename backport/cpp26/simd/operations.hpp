	template<class V, class Indices,
	         typename enable_if<detail::is_simd_index_vector<Indices>::value, int>::type>
	constexpr detail::permute_result_t<V, Indices> permute(const V& value, const Indices& indices) {
	    return detail::permute_from_indices(value, indices);
	}

		template<class T, class Abi>
		constexpr basic_vec<T, Abi> compress(const basic_vec<T, Abi>& value,
		                                     const typename basic_vec<T, Abi>::mask_type& mask_value,
		                                     T fill_value) noexcept {
		    basic_vec<T, Abi> result;
		    simd_size_type out = 0;
		    for (simd_size_type i = 0; i < static_cast<simd_size_type>(basic_vec<T, Abi>::size); ++i) {
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
		constexpr basic_vec<T, Abi> compress(const basic_vec<T, Abi>& value, const typename basic_vec<T, Abi>::mask_type& mask_value) noexcept {
		    return simd::compress(value, mask_value, T{});
		}

		template<size_t Bytes, class Abi>
		constexpr basic_mask<Bytes, Abi> compress(const basic_mask<Bytes, Abi>& value,
		                                          const basic_mask<Bytes, Abi>& mask_value,
		                                          bool fill_value = false) noexcept {
		    basic_mask<Bytes, Abi> result(fill_value);
		    simd_size_type out = 0;
		    for (simd_size_type i = 0; i < static_cast<simd_size_type>(basic_mask<Bytes, Abi>::size); ++i) {
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
		constexpr basic_vec<T, Abi> expand(const basic_vec<T, Abi>& value,
		                                   const typename basic_vec<T, Abi>::mask_type& mask_value,
		                                   const basic_vec<T, Abi>& original) noexcept {
		    basic_vec<T, Abi> result = original;
		    simd_size_type in = 0;
		    for (simd_size_type i = 0; i < static_cast<simd_size_type>(basic_vec<T, Abi>::size); ++i) {
		        if (mask_value[i]) {
		            detail::set_lane(result, i, value[in]);
		            ++in;
		        }
		    }
		    return result;
		}

		template<class T, class Abi>
		constexpr basic_vec<T, Abi> expand(const basic_vec<T, Abi>& value, const typename basic_vec<T, Abi>::mask_type& mask_value) noexcept {
		    return simd::expand(value, mask_value, basic_vec<T, Abi>{});
		}

		template<size_t Bytes, class Abi>
		constexpr basic_mask<Bytes, Abi> expand(const basic_mask<Bytes, Abi>& value,
		                                        const basic_mask<Bytes, Abi>& mask_value,
		                                        const basic_mask<Bytes, Abi>& original = basic_mask<Bytes, Abi>{}) noexcept {
		    basic_mask<Bytes, Abi> result = original;
		    simd_size_type in = 0;
		    for (simd_size_type i = 0; i < static_cast<simd_size_type>(basic_mask<Bytes, Abi>::size); ++i) {
		        if (mask_value[i]) {
		            detail::lane_ref(result, i) = value[in];
		            ++in;
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
constexpr basic_vec<T, Abi> min(const basic_vec<T, Abi>& left, const basic_vec<T, Abi>& right) noexcept
    requires requires(const T& left_value, const T& right_value) { static_cast<bool>(left_value < right_value); } {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(result, i, left[i] < right[i] ? left[i] : right[i]);
    }
    return result;
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> max(const basic_vec<T, Abi>& left, const basic_vec<T, Abi>& right) noexcept
    requires requires(const T& left_value, const T& right_value) { static_cast<bool>(left_value < right_value); } {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(result, i, left[i] < right[i] ? right[i] : left[i]);
    }
    return result;
}

template<class T, class Abi>
constexpr pair<basic_vec<T, Abi>, basic_vec<T, Abi>> minmax(const basic_vec<T, Abi>& left,
                                                            const basic_vec<T, Abi>& right) noexcept
    requires requires(const T& left_value, const T& right_value) { static_cast<bool>(left_value < right_value); } {
    return {min(left, right), max(left, right)};
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> clamp(const basic_vec<T, Abi>& value,
                                  const basic_vec<T, Abi>& low,
                                  const basic_vec<T, Abi>& high) noexcept
    requires requires(const T& left_value, const T& right_value) { static_cast<bool>(left_value < right_value); } {
    return min(max(value, low), high);
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

template<size_t Bytes, class Abi, class T, class U,
         class Result = decltype(simd_select_impl(declval<const basic_mask<Bytes, Abi>&>(), declval<const T&>(), declval<const U&>()))>
constexpr Result select(const basic_mask<Bytes, Abi>& mask_value,
                        const T& true_value,
                        const U& false_value) noexcept(noexcept(simd_select_impl(mask_value, true_value, false_value))) {
    return simd_select_impl(mask_value, true_value, false_value);
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

#include "operations_bit.hpp"
#include "operations_math.hpp"
#include "operations_complex.hpp"

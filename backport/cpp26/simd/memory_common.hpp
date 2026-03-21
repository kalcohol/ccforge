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

template<class I, class = void>
struct default_load_vector {};

template<class I>
struct default_load_vector<I, void_t<typename iterator_traits<remove_cvref_t<I>>::value_type>> {
    using type = basic_vec<typename iterator_traits<remove_cvref_t<I>>::value_type>;
};

template<class I>
using default_load_vector_t = typename default_load_vector<I>::type;

template<class T, class = void>
struct default_pointer_load_vector {};

template<class T>
struct default_pointer_load_vector<T, enable_if_t<is_supported_value<remove_cvref_t<T>>::value>> {
    using type = basic_vec<remove_cvref_t<T>>;
};

template<class T>
using default_pointer_load_vector_t = typename default_pointer_load_vector<T>::type;

template<class R, class = void>
struct default_range_load_vector {};

template<class R>
struct default_range_load_vector<R, void_t<ranges::range_value_t<R>>> {
    using type = basic_vec<ranges::range_value_t<R>>;
};

template<class R>
using default_range_load_vector_t = typename default_range_load_vector<R>::type;

template<class R, class Indices, class = void>
struct default_gather_vector {};

template<class R, class Indices>
struct default_gather_vector<R, Indices, void_t<ranges::range_value_t<R>, decltype(Indices::size)>> {
    using type = vec<ranges::range_value_t<R>, Indices::size>;
};

template<class R, class Indices>
using default_gather_vector_t = typename default_gather_vector<R, Indices>::type;

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

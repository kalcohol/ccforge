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

template<class V, class R, class... Flags,
         typename enable_if<detail::is_contiguous_load_store_range<R>::value, int>::type = 0>
constexpr V partial_load(R&& r, flags<Flags...> f = {}) {
    return simd::partial_load<V>(ranges::data(r), detail::range_size(r), f);
}

template<class V, class R, class... Flags,
         typename enable_if<detail::is_contiguous_load_store_range<R>::value, int>::type = 0>
constexpr V partial_load(R&& r, const typename V::mask_type& mask_value, flags<Flags...> f = {}) {
    return simd::partial_load<V>(ranges::data(r), detail::range_size(r), mask_value, f);
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

template<class T, class Abi, class R, class... Flags,
         typename enable_if<detail::is_writable_load_store_range<R>::value, int>::type = 0>
constexpr void partial_store(const basic_vec<T, Abi>& value, R&& r, flags<Flags...> f = {}) {
    simd::partial_store(value, ranges::data(r), detail::range_size(r), f);
}

template<class T, class Abi, class R, class... Flags,
         typename enable_if<detail::is_writable_load_store_range<R>::value, int>::type = 0>
constexpr void partial_store(const basic_vec<T, Abi>& value, R&& r, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f = {}) {
    simd::partial_store(value, ranges::data(r), detail::range_size(r), mask_value, f);
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

template<class V, class R, class... Flags,
         typename enable_if<detail::is_contiguous_load_store_range<R>::value, int>::type = 0>
constexpr V unchecked_load(R&& r, flags<Flags...> f = {}) {
    return simd::unchecked_load<V>(ranges::data(r), detail::range_size(r), f);
}

template<class V, class R, class... Flags,
         typename enable_if<detail::is_contiguous_load_store_range<R>::value, int>::type = 0>
constexpr V unchecked_load(R&& r, const typename V::mask_type& mask_value, flags<Flags...> f = {}) {
    return simd::unchecked_load<V>(ranges::data(r), detail::range_size(r), mask_value, f);
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
		                                const typename Indices::mask_type& mask_value,
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

        template<class V, class R, class Indices, class... Flags,
                 typename enable_if<detail::is_contiguous_load_store_range<R>::value &&
                     detail::is_simd_index_vector<Indices>::value, int>::type = 0>
        constexpr V partial_gather_from(R&& r, const Indices& indices, flags<Flags...> f = {}) {
            return simd::partial_gather_from<V>(ranges::data(r), detail::range_size(r), indices, f);
        }

        template<class V, class R, class Indices, class... Flags,
                 typename enable_if<detail::is_contiguous_load_store_range<R>::value &&
                     detail::is_simd_index_vector<Indices>::value, int>::type = 0>
        constexpr V partial_gather_from(R&& r, const typename Indices::mask_type& mask_value, const Indices& indices, flags<Flags...> f = {}) {
            return simd::partial_gather_from<V>(ranges::data(r), detail::range_size(r), indices, mask_value, f);
        }

	template<class V, class I, class Indices, class... Flags,
	         typename enable_if<
	             detail::is_simd_index_vector<Indices>::value &&
	                 (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_random_access_load_store_iterator<I>::value),
	             int>::type = 0>
		constexpr V unchecked_gather_from(I first, const Indices& indices, const typename Indices::mask_type& mask_value, flags<Flags...> f = {}) {
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
		                                 const typename Indices::mask_type& mask_value,
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
		                                    const typename Indices::mask_type& mask_value,
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

        template<class V, class R, class Indices, class... Flags,
                 typename enable_if<detail::is_contiguous_load_store_range<R>::value &&
                     detail::is_simd_index_vector<Indices>::value, int>::type = 0>
        constexpr V unchecked_gather_from(R&& r, const Indices& indices, flags<Flags...> f = {}) {
            return simd::unchecked_gather_from<V>(ranges::data(r), indices, f);
        }

        template<class V, class R, class Indices, class... Flags,
                 typename enable_if<detail::is_contiguous_load_store_range<R>::value &&
                     detail::is_simd_index_vector<Indices>::value, int>::type = 0>
        constexpr V unchecked_gather_from(R&& r, const typename Indices::mask_type& mask_value, const Indices& indices, flags<Flags...> f = {}) {
            return simd::unchecked_gather_from<V>(ranges::data(r), indices, mask_value, f);
        }

        template<class T, class Abi, class R, class Indices, class... Flags,
                 typename enable_if<detail::is_writable_load_store_range<R>::value &&
                     detail::is_simd_index_vector<Indices>::value, int>::type = 0>
        constexpr void partial_scatter_to(const basic_vec<T, Abi>& value, R&& r, const Indices& indices, flags<Flags...> f = {}) {
            simd::partial_scatter_to(value, ranges::data(r), detail::range_size(r), indices, f);
        }

        template<class T, class Abi, class R, class Indices, class... Flags,
                 typename enable_if<detail::is_writable_load_store_range<R>::value &&
                     detail::is_simd_index_vector<Indices>::value, int>::type = 0>
        constexpr void partial_scatter_to(const basic_vec<T, Abi>& value,
                                          R&& r,
                                          const typename Indices::mask_type& mask_value,
                                          const Indices& indices,
                                          flags<Flags...> f = {}) {
            simd::partial_scatter_to(value, ranges::data(r), detail::range_size(r), indices, mask_value, f);
        }

        template<class T, class Abi, class R, class Indices, class... Flags,
                 typename enable_if<detail::is_writable_load_store_range<R>::value &&
                     detail::is_simd_index_vector<Indices>::value, int>::type = 0>
        constexpr void unchecked_scatter_to(const basic_vec<T, Abi>& value, R&& r, const Indices& indices, flags<Flags...> f = {}) {
            simd::unchecked_scatter_to(value, ranges::data(r), indices, f);
        }

        template<class T, class Abi, class R, class Indices, class... Flags,
                 typename enable_if<detail::is_writable_load_store_range<R>::value &&
                     detail::is_simd_index_vector<Indices>::value, int>::type = 0>
        constexpr void unchecked_scatter_to(const basic_vec<T, Abi>& value,
                                            R&& r,
                                            const typename Indices::mask_type& mask_value,
                                            const Indices& indices,
                                            flags<Flags...> f = {}) {
            simd::unchecked_scatter_to(value, ranges::data(r), indices, mask_value, f);
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

template<class T, class Abi, class R, class... Flags,
         typename enable_if<detail::is_writable_load_store_range<R>::value, int>::type = 0>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, R&& r, flags<Flags...> f = {}) {
    simd::unchecked_store(value, ranges::data(r), detail::range_size(r), f);
}

template<class T, class Abi, class R, class... Flags,
         typename enable_if<detail::is_writable_load_store_range<R>::value, int>::type = 0>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, R&& r, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f = {}) {
    simd::unchecked_store(value, ranges::data(r), detail::range_size(r), mask_value, f);
}


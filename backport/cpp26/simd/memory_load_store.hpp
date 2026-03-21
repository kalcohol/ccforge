template<class I,
         class... Flags,
         typename enable_if<!is_pointer<typename detail::remove_cvref_t<I>>::value && detail::is_random_access_load_store_iterator<I>::value, int>::type = 0>
constexpr detail::default_load_vector_t<I> partial_load(I first, simd_size_type count, flags<Flags...> f = {}) {
    using V = detail::default_load_vector_t<I>;
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    return detail::load_n_impl<V>(first, count, f);
}

template<class I,
         class... Flags,
         typename enable_if<!is_pointer<typename detail::remove_cvref_t<I>>::value && detail::is_random_access_load_store_iterator<I>::value, int>::type = 0>
constexpr detail::default_load_vector_t<I>
partial_load(I first, simd_size_type count, const typename detail::default_load_vector_t<I>::mask_type& mask_value, flags<Flags...> f = {}) {
    using V = detail::default_load_vector_t<I>;
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    return detail::load_n_impl<V>(first, count, mask_value, f);
}

template<class U, class... Flags>
constexpr detail::default_pointer_load_vector_t<U> partial_load(const U* first, simd_size_type count, flags<Flags...> f = {}) {
    return detail::load_impl<detail::default_pointer_load_vector_t<U>>(first, count, f);
}

template<class U, class... Flags>
constexpr detail::default_pointer_load_vector_t<U> partial_load(const U* first,
                                                                simd_size_type count,
                                                                const typename detail::default_pointer_load_vector_t<U>::mask_type& mask_value,
                                                                flags<Flags...> f = {}) {
    return detail::load_impl<detail::default_pointer_load_vector_t<U>>(first, count, mask_value, f);
}

template<class I,
         class S,
         class... Flags,
         typename enable_if<detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_random_access_load_store_iterator<I>::value), int>::type = 0>
constexpr detail::default_load_vector_t<I> partial_load(I first, S last, flags<Flags...> f = {}) {
    using V = detail::default_load_vector_t<I>;
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    return detail::load_impl<V>(first, last, f);
}

template<class I,
         class S,
         class... Flags,
         typename enable_if<detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_random_access_load_store_iterator<I>::value), int>::type = 0>
constexpr detail::default_load_vector_t<I>
partial_load(I first, S last, const typename detail::default_load_vector_t<I>::mask_type& mask_value, flags<Flags...> f = {}) {
    using V = detail::default_load_vector_t<I>;
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    return detail::load_impl<V>(first, last, mask_value, f);
}

template<class R, class... Flags,
         typename enable_if<detail::is_contiguous_load_store_range<R>::value, int>::type = 0>
constexpr detail::default_range_load_vector_t<R> partial_load(R&& r, flags<Flags...> f = {}) {
    return detail::load_impl<detail::default_range_load_vector_t<R>>(ranges::data(r), detail::range_size(r), f);
}

template<class R, class... Flags,
         typename enable_if<detail::is_contiguous_load_store_range<R>::value, int>::type = 0>
constexpr detail::default_range_load_vector_t<R>
partial_load(R&& r, const typename detail::default_range_load_vector_t<R>::mask_type& mask_value, flags<Flags...> f = {}) {
    return detail::load_impl<detail::default_range_load_vector_t<R>>(ranges::data(r), detail::range_size(r), mask_value, f);
}

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

template<class I,
         class... Flags,
         typename enable_if<!is_pointer<typename detail::remove_cvref_t<I>>::value && detail::is_random_access_load_store_iterator<I>::value, int>::type = 0>
constexpr detail::default_load_vector_t<I> unchecked_load(I first, simd_size_type count, flags<Flags...> f = {}) {
    using V = detail::default_load_vector_t<I>;
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    detail::require_unchecked_extent<V>(count);
    return detail::load_n_impl<V>(first, count, f);
}

template<class I,
         class... Flags,
         typename enable_if<!is_pointer<typename detail::remove_cvref_t<I>>::value && detail::is_random_access_load_store_iterator<I>::value, int>::type = 0>
constexpr detail::default_load_vector_t<I>
unchecked_load(I first, simd_size_type count, const typename detail::default_load_vector_t<I>::mask_type& mask_value, flags<Flags...> f = {}) {
    using V = detail::default_load_vector_t<I>;
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    detail::require_unchecked_extent<V>(count);
    return detail::load_n_impl<V>(first, count, mask_value, f);
}

template<class U, class... Flags>
constexpr detail::default_pointer_load_vector_t<U> unchecked_load(const U* first, flags<Flags...> f = {}) {
    return detail::load_impl<detail::default_pointer_load_vector_t<U>>(first, f);
}

template<class U, class... Flags>
constexpr detail::default_pointer_load_vector_t<U> unchecked_load(const U* first,
                                                                  const typename detail::default_pointer_load_vector_t<U>::mask_type& mask_value,
                                                                  flags<Flags...> f = {}) {
    return detail::load_impl<detail::default_pointer_load_vector_t<U>>(first, mask_value, f);
}

template<class U, class... Flags>
constexpr detail::default_pointer_load_vector_t<U> unchecked_load(const U* first, simd_size_type count, flags<Flags...> f = {}) {
    detail::require_unchecked_extent<detail::default_pointer_load_vector_t<U>>(count);
    return detail::load_impl<detail::default_pointer_load_vector_t<U>>(first, count, f);
}

template<class U, class... Flags>
constexpr detail::default_pointer_load_vector_t<U> unchecked_load(const U* first,
                                                                  simd_size_type count,
                                                                  const typename detail::default_pointer_load_vector_t<U>::mask_type& mask_value,
                                                                  flags<Flags...> f = {}) {
    detail::require_unchecked_extent<detail::default_pointer_load_vector_t<U>>(count);
    return detail::load_impl<detail::default_pointer_load_vector_t<U>>(first, count, mask_value, f);
}

template<class I,
         class S,
         class... Flags,
         typename enable_if<detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_random_access_load_store_iterator<I>::value), int>::type = 0>
constexpr detail::default_load_vector_t<I> unchecked_load(I first, S last, flags<Flags...> f = {}) {
    using V = detail::default_load_vector_t<I>;
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    detail::require_unchecked_extent<V>(detail::iterator_distance(first, last));
    return detail::load_impl<V>(first, last, f);
}

template<class I,
         class S,
         class... Flags,
         typename enable_if<detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_random_access_load_store_iterator<I>::value), int>::type = 0>
constexpr detail::default_load_vector_t<I>
unchecked_load(I first, S last, const typename detail::default_load_vector_t<I>::mask_type& mask_value, flags<Flags...> f = {}) {
    using V = detail::default_load_vector_t<I>;
    detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    detail::require_unchecked_extent<V>(detail::iterator_distance(first, last));
    return detail::load_impl<V>(first, last, mask_value, f);
}

template<class R, class... Flags,
         typename enable_if<detail::is_contiguous_load_store_range<R>::value, int>::type = 0>
constexpr detail::default_range_load_vector_t<R> unchecked_load(R&& r, flags<Flags...> f = {}) {
    using V = detail::default_range_load_vector_t<R>;
    detail::require_unchecked_extent<V>(detail::range_size(r));
    return detail::load_impl<V>(ranges::data(r), detail::range_size(r), f);
}

template<class R, class... Flags,
         typename enable_if<detail::is_contiguous_load_store_range<R>::value, int>::type = 0>
constexpr detail::default_range_load_vector_t<R>
unchecked_load(R&& r, const typename detail::default_range_load_vector_t<R>::mask_type& mask_value, flags<Flags...> f = {}) {
    using V = detail::default_range_load_vector_t<R>;
    detail::require_unchecked_extent<V>(detail::range_size(r));
    return detail::load_impl<V>(ranges::data(r), detail::range_size(r), mask_value, f);
}

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
constexpr void unchecked_store(const basic_vec<T, Abi>& value,
                               U* first,
                               simd_size_type count,
                               const typename basic_vec<T, Abi>::mask_type& mask_value,
                               flags<Flags...> f) {
    detail::require_unchecked_extent<basic_vec<T, Abi>>(count);
    detail::store_impl(value, first, count, mask_value, f);
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

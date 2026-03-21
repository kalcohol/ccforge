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
                                const typename Indices::mask_type& mask_value,
                                const Indices& indices,
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

template<class R, class Indices, class... Flags,
         typename enable_if<detail::is_contiguous_load_store_range<R>::value &&
             detail::is_simd_index_vector<Indices>::value, int>::type = 0>
constexpr detail::default_gather_vector_t<R, Indices> partial_gather_from(R&& r, const Indices& indices, flags<Flags...> f = {}) {
    using V = detail::default_gather_vector_t<R, Indices>;
    return simd::partial_gather_from<V>(ranges::data(r), detail::range_size(r), indices, f);
}

template<class R, class Indices, class... Flags,
         typename enable_if<detail::is_contiguous_load_store_range<R>::value &&
             detail::is_simd_index_vector<Indices>::value, int>::type = 0>
constexpr detail::default_gather_vector_t<R, Indices>
partial_gather_from(R&& r, const typename Indices::mask_type& mask_value, const Indices& indices, flags<Flags...> f = {}) {
    using V = detail::default_gather_vector_t<R, Indices>;
    return simd::partial_gather_from<V>(ranges::data(r), detail::range_size(r), mask_value, indices, f);
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
    return simd::partial_gather_from<V>(ranges::data(r), detail::range_size(r), mask_value, indices, f);
}

template<class V, class I, class Indices, class... Flags,
         typename enable_if<
             detail::is_simd_index_vector<Indices>::value &&
                 (is_pointer<typename detail::remove_cvref_t<I>>::value || detail::is_random_access_load_store_iterator<I>::value),
             int>::type = 0>
constexpr V unchecked_gather_from(I first, const typename Indices::mask_type& mask_value, const Indices& indices, flags<Flags...> f = {}) {
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

template<class R, class Indices, class... Flags,
         typename enable_if<detail::is_contiguous_load_store_range<R>::value &&
             detail::is_simd_index_vector<Indices>::value, int>::type = 0>
constexpr detail::default_gather_vector_t<R, Indices> unchecked_gather_from(R&& r, const Indices& indices, flags<Flags...> f = {}) {
    using V = detail::default_gather_vector_t<R, Indices>;
    return simd::unchecked_gather_from<V>(ranges::data(r), indices, f);
}

template<class R, class Indices, class... Flags,
         typename enable_if<detail::is_contiguous_load_store_range<R>::value &&
             detail::is_simd_index_vector<Indices>::value, int>::type = 0>
constexpr detail::default_gather_vector_t<R, Indices>
unchecked_gather_from(R&& r, const typename Indices::mask_type& mask_value, const Indices& indices, flags<Flags...> f = {}) {
    using V = detail::default_gather_vector_t<R, Indices>;
    return simd::unchecked_gather_from<V>(ranges::data(r), mask_value, indices, f);
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
                                  const typename Indices::mask_type& mask_value,
                                  const Indices& indices,
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
                                    const typename Indices::mask_type& mask_value,
                                    const Indices& indices,
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
    return simd::unchecked_gather_from<V>(ranges::data(r), mask_value, indices, f);
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
    simd::partial_scatter_to(value, ranges::data(r), detail::range_size(r), mask_value, indices, f);
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
    simd::unchecked_scatter_to(value, ranges::data(r), mask_value, indices, f);
}

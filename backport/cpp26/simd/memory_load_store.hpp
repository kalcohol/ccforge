#pragma once

namespace __detail {

// Inspect an object-independent size without constructing or copying the range.
// Match constant_range_size's MSVC workaround for state-dependent sizes.
#if (defined(__GNUC__) || defined(_MSC_VER)) && !defined(__clang__)
template<class R>
concept has_unchecked_range_size_constant = requires(R& r) {
    typename integral_constant<decltype(ranges::size(r)), ranges::size(r)>;
};

template<class R>
    requires has_unchecked_range_size_constant<R>
auto unchecked_range_size_constant(R& r) {
    constexpr auto value = ranges::size(r);
    return integral_constant<decltype(ranges::size(r)), value>{};
}

template<class Result, class R>
auto unchecked_range_size_result(Result (*)(R&)) -> type_identity<Result>;
#else
template<class R>
auto unchecked_range_size_constant(R r)
    -> integral_constant<decltype(ranges::size(r)), ranges::size(r)>;

template<class Result, class R>
auto unchecked_range_size_result(Result (*)(R)) -> type_identity<Result>;
#endif

template<class R>
using unchecked_range_size_t = typename decltype(__detail::unchecked_range_size_result(
    &__detail::unchecked_range_size_constant<remove_reference_t<R>>))::type;

template<class V, class R>
constexpr void require_unchecked_static_extent() noexcept {
    constexpr auto fixed = fixed_range_size<remove_reference_t<R>>::value;
    if constexpr (fixed >= 0) {
        static_assert(fixed >= static_cast<__simd_size_type>(V::size),
            "simd unchecked load/store requires a range at least as large as the vector");
    } else if constexpr (requires { typename __detail::unchecked_range_size_t<remove_reference_t<R>>; }) {
        static_assert(__detail::unchecked_range_size_t<remove_reference_t<R>>::value >= V::size,
            "simd unchecked load/store requires a range at least as large as the vector");
    }
}

} // namespace __detail

template<class I,
         class... Flags,
         typename enable_if<!is_pointer<typename __detail::remove_cvref_t<I>>::value && __detail::is_random_access_load_store_iterator<I>::value, int>::type = 0>
constexpr __detail::default_load_vector_t<I> partial_load(I first, __simd_size_type count, flags<Flags...> f = {}) {
    using V = __detail::default_load_vector_t<I>;
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    return __detail::load_n_impl<V>(first, count, f);
}

template<class I,
         class... Flags,
         typename enable_if<!is_pointer<typename __detail::remove_cvref_t<I>>::value && __detail::is_random_access_load_store_iterator<I>::value, int>::type = 0>
constexpr __detail::default_load_vector_t<I>
partial_load(I first, __simd_size_type count, const typename __detail::default_load_vector_t<I>::mask_type& mask_value, flags<Flags...> f = {}) {
    using V = __detail::default_load_vector_t<I>;
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    return __detail::load_n_impl<V>(first, count, mask_value, f);
}

template<class U, class... Flags>
constexpr __detail::default_pointer_load_vector_t<U> partial_load(const U* first, __simd_size_type count, flags<Flags...> f = {}) {
    return __detail::load_impl<__detail::default_pointer_load_vector_t<U>>(first, count, f);
}

template<class U, class... Flags>
constexpr __detail::default_pointer_load_vector_t<U> partial_load(const U* first,
                                                                __simd_size_type count,
                                                                const typename __detail::default_pointer_load_vector_t<U>::mask_type& mask_value,
                                                                flags<Flags...> f = {}) {
    return __detail::load_impl<__detail::default_pointer_load_vector_t<U>>(first, count, mask_value, f);
}

template<class I,
         class S,
         class... Flags,
         typename enable_if<__detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename __detail::remove_cvref_t<I>>::value || __detail::is_random_access_load_store_iterator<I>::value), int>::type = 0>
constexpr __detail::default_load_vector_t<I> partial_load(I first, S last, flags<Flags...> f = {}) {
    using V = __detail::default_load_vector_t<I>;
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    return __detail::load_impl<V>(first, last, f);
}

template<class I,
         class S,
         class... Flags,
         typename enable_if<__detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename __detail::remove_cvref_t<I>>::value || __detail::is_random_access_load_store_iterator<I>::value), int>::type = 0>
constexpr __detail::default_load_vector_t<I>
partial_load(I first, S last, const typename __detail::default_load_vector_t<I>::mask_type& mask_value, flags<Flags...> f = {}) {
    using V = __detail::default_load_vector_t<I>;
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    return __detail::load_impl<V>(first, last, mask_value, f);
}

template<class R, class... Flags,
         typename enable_if<__detail::is_contiguous_load_store_range<R>::value, int>::type = 0>
constexpr __detail::default_range_load_vector_t<R> partial_load(R&& r, flags<Flags...> f = {}) {
    return __detail::load_impl<__detail::default_range_load_vector_t<R>>(ranges::data(r), __detail::range_size(r), f);
}

template<class R, class... Flags,
         typename enable_if<__detail::is_contiguous_load_store_range<R>::value, int>::type = 0>
constexpr __detail::default_range_load_vector_t<R>
partial_load(R&& r, const typename __detail::default_range_load_vector_t<R>::mask_type& mask_value, flags<Flags...> f = {}) {
    return __detail::load_impl<__detail::default_range_load_vector_t<R>>(ranges::data(r), __detail::range_size(r), mask_value, f);
}

template<class V,
         class I,
         class... Flags,
         typename enable_if<!is_pointer<typename __detail::remove_cvref_t<I>>::value && __detail::is_random_access_load_store_iterator<I>::value, int>::type = 0>
constexpr V partial_load(I first, __simd_size_type count, flags<Flags...> f = {}) {
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    return __detail::load_n_impl<V>(first, count, f);
}

template<class V,
         class I,
         class... Flags,
         typename enable_if<!is_pointer<typename __detail::remove_cvref_t<I>>::value && __detail::is_random_access_load_store_iterator<I>::value, int>::type = 0>
constexpr V partial_load(I first, __simd_size_type count, const typename V::mask_type& mask_value, flags<Flags...> f = {}) {
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    return __detail::load_n_impl<V>(first, count, mask_value, f);
}

template<class V, class U, class... Flags>
constexpr V partial_load(const U* first, __simd_size_type count, flags<Flags...> f = {}) {
    return __detail::load_impl<V>(first, count, f);
}

template<class V, class U, class... Flags>
constexpr V partial_load(const U* first, __simd_size_type count, const typename V::mask_type& mask_value, flags<Flags...> f = {}) {
    return __detail::load_impl<V>(first, count, mask_value, f);
}

template<class V,
         class I,
         class S,
         class... Flags,
         typename enable_if<__detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename __detail::remove_cvref_t<I>>::value || __detail::is_random_access_load_store_iterator<I>::value), int>::type = 0>
constexpr V partial_load(I first, S last, flags<Flags...> f = {}) {
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    return __detail::load_impl<V>(first, last, f);
}

template<class V,
         class I,
         class S,
         class... Flags,
         typename enable_if<__detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename __detail::remove_cvref_t<I>>::value || __detail::is_random_access_load_store_iterator<I>::value), int>::type = 0>
constexpr V partial_load(I first, S last, const typename V::mask_type& mask_value, flags<Flags...> f = {}) {
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    return __detail::load_impl<V>(first, last, mask_value, f);
}

template<class V, class R, class... Flags,
         typename enable_if<__detail::is_contiguous_load_store_range<R>::value, int>::type = 0>
constexpr V partial_load(R&& r, flags<Flags...> f = {}) {
    return simd::partial_load<V>(ranges::data(r), __detail::range_size(r), f);
}

template<class V, class R, class... Flags,
         typename enable_if<__detail::is_contiguous_load_store_range<R>::value, int>::type = 0>
constexpr V partial_load(R&& r, const typename V::mask_type& mask_value, flags<Flags...> f = {}) {
    return simd::partial_load<V>(ranges::data(r), __detail::range_size(r), mask_value, f);
}

template<class T,
         class Abi,
         class I,
         class... Flags,
         typename enable_if<!is_pointer<typename __detail::remove_cvref_t<I>>::value && __detail::is_writable_load_store_iterator<I>::value, int>::type = 0>
constexpr void partial_store(const basic_vec<T, Abi>& value, I first, __simd_size_type count, flags<Flags...> f = {})
    requires __detail::is_explicitly_simd_convertible<T, iter_value_t<__detail::remove_cvref_t<I>>>::value &&
             (indirectly_writable<I, iter_value_t<I>>){
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    __detail::store_n_impl(value, first, count, f);
}

template<class T,
         class Abi,
         class I,
         class... Flags,
         typename enable_if<!is_pointer<typename __detail::remove_cvref_t<I>>::value && __detail::is_writable_load_store_iterator<I>::value, int>::type = 0>
constexpr void partial_store(const basic_vec<T, Abi>& value, I first, __simd_size_type count, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f = {})
    requires __detail::is_explicitly_simd_convertible<T, iter_value_t<__detail::remove_cvref_t<I>>>::value &&
             (indirectly_writable<I, iter_value_t<I>>){
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    __detail::store_n_impl(value, first, count, mask_value, f);
}

template<class T, class Abi, class U, class... Flags>
constexpr void partial_store(const basic_vec<T, Abi>& value, U* first, __simd_size_type count, flags<Flags...> f = {})
    requires __detail::is_explicitly_simd_convertible<T, U>::value &&
             (indirectly_writable<U*, iter_value_t<U*>>){
    __detail::store_impl(value, first, count, f);
}

template<class T, class Abi, class U, class... Flags>
constexpr void partial_store(const basic_vec<T, Abi>& value,
                             U* first,
                             __simd_size_type count,
                             const typename basic_vec<T, Abi>::mask_type& mask_value,
                             flags<Flags...> f = {})
    requires __detail::is_explicitly_simd_convertible<T, U>::value &&
             (indirectly_writable<U*, iter_value_t<U*>>){
    __detail::store_impl(value, first, count, mask_value, f);
}

template<class T,
         class Abi,
         class I,
         class S,
         class... Flags,
         typename enable_if<__detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename __detail::remove_cvref_t<I>>::value || __detail::is_writable_load_store_iterator<I>::value), int>::type = 0>
constexpr void partial_store(const basic_vec<T, Abi>& value, I first, S last, flags<Flags...> f = {})
    requires __detail::is_explicitly_simd_convertible<T, iter_value_t<__detail::remove_cvref_t<I>>>::value &&
             (indirectly_writable<I, iter_value_t<I>>){
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    __detail::store_impl(value, first, last, f);
}

template<class T,
         class Abi,
         class I,
         class S,
         class... Flags,
         typename enable_if<__detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename __detail::remove_cvref_t<I>>::value || __detail::is_writable_load_store_iterator<I>::value), int>::type = 0>
constexpr void partial_store(const basic_vec<T, Abi>& value,
                             I first,
                             S last,
                             const typename basic_vec<T, Abi>::mask_type& mask_value,
                             flags<Flags...> f = {})
    requires __detail::is_explicitly_simd_convertible<T, iter_value_t<__detail::remove_cvref_t<I>>>::value &&
             (indirectly_writable<I, iter_value_t<I>>){
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    __detail::store_impl(value, first, last, mask_value, f);
}

template<class T, class Abi, class R, class... Flags,
         typename enable_if<__detail::is_writable_load_store_range<R>::value, int>::type = 0>
constexpr void partial_store(const basic_vec<T, Abi>& value, R&& r, flags<Flags...> f = {})
    requires __detail::is_explicitly_simd_convertible<T, ranges::range_value_t<R>>::value &&
             (indirectly_writable<ranges::iterator_t<R>, ranges::range_value_t<R>>){
    simd::partial_store(value, ranges::data(r), __detail::range_size(r), f);
}

template<class T, class Abi, class R, class... Flags,
         typename enable_if<__detail::is_writable_load_store_range<R>::value, int>::type = 0>
constexpr void partial_store(const basic_vec<T, Abi>& value, R&& r, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f = {})
    requires __detail::is_explicitly_simd_convertible<T, ranges::range_value_t<R>>::value &&
             (indirectly_writable<ranges::iterator_t<R>, ranges::range_value_t<R>>){
    simd::partial_store(value, ranges::data(r), __detail::range_size(r), mask_value, f);
}

template<class V, class U, class... Flags>
constexpr V unchecked_load(const U* first, __simd_size_type count, flags<Flags...> f);

template<class V, class U, class... Flags>
constexpr V unchecked_load(const U* first, __simd_size_type count, const typename V::mask_type& mask_value, flags<Flags...> f);

template<class V,
         class I,
         class S,
         class... Flags,
         typename enable_if<__detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename __detail::remove_cvref_t<I>>::value || __detail::is_random_access_load_store_iterator<I>::value), int>::type = 0>
constexpr V unchecked_load(I first, S last, flags<Flags...> f = {});

template<class V,
         class I,
         class S,
         class... Flags,
         typename enable_if<__detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename __detail::remove_cvref_t<I>>::value || __detail::is_random_access_load_store_iterator<I>::value), int>::type = 0>
constexpr V unchecked_load(I first, S last, const typename V::mask_type& mask_value, flags<Flags...> f = {});

template<class T, class Abi, class U, class... Flags>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, U* first, __simd_size_type count, flags<Flags...> f)
    requires __detail::is_explicitly_simd_convertible<T, U>::value &&
             (indirectly_writable<U*, iter_value_t<U*>>);

template<class T, class Abi, class U, class... Flags>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, U* first, __simd_size_type count, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f)
    requires __detail::is_explicitly_simd_convertible<T, U>::value &&
             (indirectly_writable<U*, iter_value_t<U*>>);

template<class T,
         class Abi,
         class I,
         class S,
         class... Flags,
         typename enable_if<__detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename __detail::remove_cvref_t<I>>::value || __detail::is_writable_load_store_iterator<I>::value), int>::type = 0>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, I first, S last, flags<Flags...> f = {})
    requires __detail::is_explicitly_simd_convertible<T, iter_value_t<__detail::remove_cvref_t<I>>>::value &&
             (indirectly_writable<I, iter_value_t<I>>);

template<class T,
         class Abi,
         class I,
         class S,
         class... Flags,
         typename enable_if<__detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename __detail::remove_cvref_t<I>>::value || __detail::is_writable_load_store_iterator<I>::value), int>::type = 0>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, I first, S last, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f = {})
    requires __detail::is_explicitly_simd_convertible<T, iter_value_t<__detail::remove_cvref_t<I>>>::value &&
             (indirectly_writable<I, iter_value_t<I>>);

template<class I,
         class... Flags,
         typename enable_if<!is_pointer<typename __detail::remove_cvref_t<I>>::value && __detail::is_random_access_load_store_iterator<I>::value, int>::type = 0>
constexpr __detail::default_load_vector_t<I> unchecked_load(I first, __simd_size_type count, flags<Flags...> f = {}) {
    using V = __detail::default_load_vector_t<I>;
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    __detail::require_unchecked_extent<V>(count);
    return __detail::load_n_impl<V>(first, count, f);
}

template<class I,
         class... Flags,
         typename enable_if<!is_pointer<typename __detail::remove_cvref_t<I>>::value && __detail::is_random_access_load_store_iterator<I>::value, int>::type = 0>
constexpr __detail::default_load_vector_t<I>
unchecked_load(I first, __simd_size_type count, const typename __detail::default_load_vector_t<I>::mask_type& mask_value, flags<Flags...> f = {}) {
    using V = __detail::default_load_vector_t<I>;
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    __detail::require_unchecked_extent<V>(count);
    return __detail::load_n_impl<V>(first, count, mask_value, f);
}

template<class I, class... Flags,
         typename enable_if<!__detail::is_contiguous_load_store_range<I>::value &&
             is_pointer<decay_t<I>>::value, int>::type = 0>
constexpr __detail::default_pointer_load_vector_t<remove_pointer_t<decay_t<I>>>
unchecked_load(I&& input, flags<Flags...> f = {}) {
    decay_t<I> first(std::forward<I>(input));
    return __detail::load_impl<__detail::default_pointer_load_vector_t<remove_pointer_t<decay_t<I>>>>(first, f);
}

template<class I, class... Flags,
         typename enable_if<!__detail::is_contiguous_load_store_range<I>::value &&
             is_pointer<decay_t<I>>::value, int>::type = 0>
constexpr __detail::default_pointer_load_vector_t<remove_pointer_t<decay_t<I>>>
unchecked_load(I&& input,
               const typename __detail::default_pointer_load_vector_t<remove_pointer_t<decay_t<I>>>::mask_type& mask_value,
               flags<Flags...> f = {}) {
    decay_t<I> first(std::forward<I>(input));
    return __detail::load_impl<__detail::default_pointer_load_vector_t<remove_pointer_t<decay_t<I>>>>(first, mask_value, f);
}

template<class U, class... Flags>
constexpr __detail::default_pointer_load_vector_t<U> unchecked_load(const U* first, __simd_size_type count, flags<Flags...> f = {}) {
    __detail::require_unchecked_extent<__detail::default_pointer_load_vector_t<U>>(count);
    return __detail::load_impl<__detail::default_pointer_load_vector_t<U>>(first, count, f);
}

template<class U, class... Flags>
constexpr __detail::default_pointer_load_vector_t<U> unchecked_load(const U* first,
                                                                  __simd_size_type count,
                                                                  const typename __detail::default_pointer_load_vector_t<U>::mask_type& mask_value,
                                                                  flags<Flags...> f = {}) {
    __detail::require_unchecked_extent<__detail::default_pointer_load_vector_t<U>>(count);
    return __detail::load_impl<__detail::default_pointer_load_vector_t<U>>(first, count, mask_value, f);
}

template<class I,
         class S,
         class... Flags,
         typename enable_if<__detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename __detail::remove_cvref_t<I>>::value || __detail::is_random_access_load_store_iterator<I>::value), int>::type = 0>
constexpr __detail::default_load_vector_t<I> unchecked_load(I first, S last, flags<Flags...> f = {}) {
    using V = __detail::default_load_vector_t<I>;
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    __detail::require_unchecked_extent<V>(__detail::iterator_distance(first, last));
    return __detail::load_impl<V>(first, last, f);
}

template<class I,
         class S,
         class... Flags,
         typename enable_if<__detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename __detail::remove_cvref_t<I>>::value || __detail::is_random_access_load_store_iterator<I>::value), int>::type = 0>
constexpr __detail::default_load_vector_t<I>
unchecked_load(I first, S last, const typename __detail::default_load_vector_t<I>::mask_type& mask_value, flags<Flags...> f = {}) {
    using V = __detail::default_load_vector_t<I>;
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    __detail::require_unchecked_extent<V>(__detail::iterator_distance(first, last));
    return __detail::load_impl<V>(first, last, mask_value, f);
}

template<class R, class... Flags,
         typename enable_if<__detail::is_contiguous_load_store_range<R>::value, int>::type = 0>
constexpr __detail::default_range_load_vector_t<R> unchecked_load(R&& r, flags<Flags...> f = {}) {
    using V = __detail::default_range_load_vector_t<R>;
    __detail::require_unchecked_static_extent<V, R>();
    __detail::require_unchecked_extent<V>(__detail::range_size(r));
    return __detail::load_impl<V>(ranges::data(r), __detail::range_size(r), f);
}

template<class R, class... Flags,
         typename enable_if<__detail::is_contiguous_load_store_range<R>::value, int>::type = 0>
constexpr __detail::default_range_load_vector_t<R>
unchecked_load(R&& r, const typename __detail::default_range_load_vector_t<R>::mask_type& mask_value, flags<Flags...> f = {}) {
    using V = __detail::default_range_load_vector_t<R>;
    __detail::require_unchecked_static_extent<V, R>();
    __detail::require_unchecked_extent<V>(__detail::range_size(r));
    return __detail::load_impl<V>(ranges::data(r), __detail::range_size(r), mask_value, f);
}

template<class V,
         class I,
         class... Flags,
         typename enable_if<!is_pointer<typename __detail::remove_cvref_t<I>>::value && __detail::is_random_access_load_store_iterator<I>::value, int>::type = 0>
constexpr V unchecked_load(I first, __simd_size_type count, flags<Flags...> f = {}) {
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    __detail::require_unchecked_extent<V>(count);
    return __detail::load_n_impl<V>(first, count, f);
}

template<class V,
         class I,
         class... Flags,
         typename enable_if<!is_pointer<typename __detail::remove_cvref_t<I>>::value && __detail::is_random_access_load_store_iterator<I>::value, int>::type = 0>
constexpr V unchecked_load(I first, __simd_size_type count, const typename V::mask_type& mask_value, flags<Flags...> f = {}) {
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    __detail::require_unchecked_extent<V>(count);
    return __detail::load_n_impl<V>(first, count, mask_value, f);
}

template<class V, class I, class... Flags,
         typename enable_if<!__detail::is_contiguous_load_store_range<I>::value &&
             is_pointer<decay_t<I>>::value, int>::type = 0>
constexpr V unchecked_load(I&& input, flags<Flags...> f = {}) {
    decay_t<I> first(std::forward<I>(input));
    return __detail::load_impl<V>(first, f);
}

template<class V, class U>
constexpr V unchecked_load(const U* first, __simd_size_type count) {
    __detail::require_unchecked_extent<V>(count);
    return __detail::load_impl<V>(first, count);
}

template<class V, class I, class... Flags,
         typename enable_if<!__detail::is_contiguous_load_store_range<I>::value &&
             is_pointer<decay_t<I>>::value, int>::type = 0>
constexpr V unchecked_load(I&& input, const typename V::mask_type& mask_value, flags<Flags...> f = {}) {
    decay_t<I> first(std::forward<I>(input));
    return __detail::load_impl<V>(first, mask_value, f);
}

template<class V, class U>
constexpr V unchecked_load(const U* first, __simd_size_type count, const typename V::mask_type& mask_value) {
    __detail::require_unchecked_extent<V>(count);
    return __detail::load_impl<V>(first, count, mask_value);
}

template<class V, class U, class... Flags>
constexpr V unchecked_load(const U* first, __simd_size_type count, flags<Flags...> f) {
    __detail::require_unchecked_extent<V>(count);
    return __detail::load_impl<V>(first, count, f);
}

template<class V, class U, class... Flags>
constexpr V unchecked_load(const U* first, __simd_size_type count, const typename V::mask_type& mask_value, flags<Flags...> f) {
    __detail::require_unchecked_extent<V>(count);
    return __detail::load_impl<V>(first, count, mask_value, f);
}

template<class V,
         class I,
         class S,
         class... Flags,
         typename enable_if<__detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename __detail::remove_cvref_t<I>>::value || __detail::is_random_access_load_store_iterator<I>::value), int>::type>
constexpr V unchecked_load(I first, S last, flags<Flags...> f) {
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    __detail::require_unchecked_extent<V>(__detail::iterator_distance(first, last));
    return __detail::load_impl<V>(first, last, f);
}

template<class V,
         class I,
         class S,
         class... Flags,
         typename enable_if<__detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename __detail::remove_cvref_t<I>>::value || __detail::is_random_access_load_store_iterator<I>::value), int>::type>
constexpr V unchecked_load(I first, S last, const typename V::mask_type& mask_value, flags<Flags...> f) {
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    __detail::require_unchecked_extent<V>(__detail::iterator_distance(first, last));
    return __detail::load_impl<V>(first, last, mask_value, f);
}

template<class V, class R, class... Flags,
         typename enable_if<__detail::is_contiguous_load_store_range<R>::value, int>::type = 0>
constexpr V unchecked_load(R&& r, flags<Flags...> f = {}) {
    __detail::require_unchecked_static_extent<V, R>();
    return simd::unchecked_load<V>(ranges::data(r), __detail::range_size(r), f);
}

template<class V, class R, class... Flags,
         typename enable_if<__detail::is_contiguous_load_store_range<R>::value, int>::type = 0>
constexpr V unchecked_load(R&& r, const typename V::mask_type& mask_value, flags<Flags...> f = {}) {
    __detail::require_unchecked_static_extent<V, R>();
    return simd::unchecked_load<V>(ranges::data(r), __detail::range_size(r), mask_value, f);
}

template<class T,
         class Abi,
         class I,
         class... Flags,
         typename enable_if<!is_pointer<typename __detail::remove_cvref_t<I>>::value && __detail::is_writable_load_store_iterator<I>::value, int>::type = 0>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, I first, __simd_size_type count, flags<Flags...> f = {})
    requires __detail::is_explicitly_simd_convertible<T, iter_value_t<__detail::remove_cvref_t<I>>>::value &&
             (indirectly_writable<I, iter_value_t<I>>){
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    __detail::require_unchecked_extent<basic_vec<T, Abi>>(count);
    __detail::store_n_impl(value, first, count, f);
}

template<class T,
         class Abi,
         class I,
         class... Flags,
         typename enable_if<!is_pointer<typename __detail::remove_cvref_t<I>>::value && __detail::is_writable_load_store_iterator<I>::value, int>::type = 0>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, I first, __simd_size_type count, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f = {})
    requires __detail::is_explicitly_simd_convertible<T, iter_value_t<__detail::remove_cvref_t<I>>>::value &&
             (indirectly_writable<I, iter_value_t<I>>){
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    __detail::require_unchecked_extent<basic_vec<T, Abi>>(count);
    __detail::store_n_impl(value, first, count, mask_value, f);
}

template<class T, class Abi, class I, class... Flags,
         typename enable_if<!__detail::is_contiguous_load_store_range<I>::value &&
             is_pointer<decay_t<I>>::value, int>::type = 0>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, I&& input, flags<Flags...> f = {})
    requires __detail::is_explicitly_simd_convertible<T, iter_value_t<__detail::remove_cvref_t<I>>>::value &&
             (indirectly_writable<decay_t<I>, iter_value_t<decay_t<I>>>){
    decay_t<I> first(std::forward<I>(input));
    __detail::store_impl(value, first, f);
}

template<class T, class Abi, class U>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, U* first, __simd_size_type count)
    requires __detail::is_explicitly_simd_convertible<T, U>::value &&
             (indirectly_writable<U*, iter_value_t<U*>>){
    __detail::require_unchecked_extent<basic_vec<T, Abi>>(count);
    __detail::store_impl(value, first, count);
}

template<class T, class Abi, class I, class... Flags,
         typename enable_if<!__detail::is_contiguous_load_store_range<I>::value &&
             is_pointer<decay_t<I>>::value, int>::type = 0>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, I&& input, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f = {})
    requires __detail::is_explicitly_simd_convertible<T, iter_value_t<__detail::remove_cvref_t<I>>>::value &&
             (indirectly_writable<decay_t<I>, iter_value_t<decay_t<I>>>){
    decay_t<I> first(std::forward<I>(input));
    __detail::store_impl(value, first, mask_value, f);
}

template<class T, class Abi, class U>
constexpr void unchecked_store(const basic_vec<T, Abi>& value,
                               U* first,
                               __simd_size_type count,
                               const typename basic_vec<T, Abi>::mask_type& mask_value)
    requires __detail::is_explicitly_simd_convertible<T, U>::value &&
             (indirectly_writable<U*, iter_value_t<U*>>){
    __detail::require_unchecked_extent<basic_vec<T, Abi>>(count);
    __detail::store_impl(value, first, count, mask_value);
}

template<class T, class Abi, class U, class... Flags>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, U* first, __simd_size_type count, flags<Flags...> f)
    requires __detail::is_explicitly_simd_convertible<T, U>::value &&
             (indirectly_writable<U*, iter_value_t<U*>>){
    __detail::require_unchecked_extent<basic_vec<T, Abi>>(count);
    __detail::store_impl(value, first, count, f);
}

template<class T, class Abi, class U, class... Flags>
constexpr void unchecked_store(const basic_vec<T, Abi>& value,
                               U* first,
                               __simd_size_type count,
                               const typename basic_vec<T, Abi>::mask_type& mask_value,
                               flags<Flags...> f)
    requires __detail::is_explicitly_simd_convertible<T, U>::value &&
             (indirectly_writable<U*, iter_value_t<U*>>){
    __detail::require_unchecked_extent<basic_vec<T, Abi>>(count);
    __detail::store_impl(value, first, count, mask_value, f);
}

template<class T,
         class Abi,
         class I,
         class S,
         class... Flags,
         typename enable_if<__detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename __detail::remove_cvref_t<I>>::value || __detail::is_writable_load_store_iterator<I>::value), int>::type>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, I first, S last, flags<Flags...> f)
    requires __detail::is_explicitly_simd_convertible<T, iter_value_t<__detail::remove_cvref_t<I>>>::value &&
             (indirectly_writable<I, iter_value_t<I>>){
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    __detail::require_unchecked_extent<basic_vec<T, Abi>>(__detail::iterator_distance(first, last));
    __detail::store_impl(value, first, last, f);
}

template<class T,
         class Abi,
         class I,
         class S,
         class... Flags,
         typename enable_if<__detail::is_sized_sentinel_for<I, S>::value &&
             (is_pointer<typename __detail::remove_cvref_t<I>>::value || __detail::is_writable_load_store_iterator<I>::value), int>::type>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, I first, S last, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f)
    requires __detail::is_explicitly_simd_convertible<T, iter_value_t<__detail::remove_cvref_t<I>>>::value &&
             (indirectly_writable<I, iter_value_t<I>>){
    __detail::require_iterator_compatible_flags<I, flags<Flags...>>();
    __detail::require_unchecked_extent<basic_vec<T, Abi>>(__detail::iterator_distance(first, last));
    __detail::store_impl(value, first, last, mask_value, f);
}

template<class T, class Abi, class R, class... Flags,
         typename enable_if<__detail::is_writable_load_store_range<R>::value, int>::type = 0>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, R&& r, flags<Flags...> f = {})
    requires __detail::is_explicitly_simd_convertible<T, ranges::range_value_t<R>>::value &&
             (indirectly_writable<ranges::iterator_t<R>, ranges::range_value_t<R>>){
    __detail::require_unchecked_static_extent<basic_vec<T, Abi>, R>();
    simd::unchecked_store(value, ranges::data(r), __detail::range_size(r), f);
}

template<class T, class Abi, class R, class... Flags,
         typename enable_if<__detail::is_writable_load_store_range<R>::value, int>::type = 0>
constexpr void unchecked_store(const basic_vec<T, Abi>& value, R&& r, const typename basic_vec<T, Abi>::mask_type& mask_value, flags<Flags...> f = {})
    requires __detail::is_explicitly_simd_convertible<T, ranges::range_value_t<R>>::value &&
             (indirectly_writable<ranges::iterator_t<R>, ranges::range_value_t<R>>){
    __detail::require_unchecked_static_extent<basic_vec<T, Abi>, R>();
    simd::unchecked_store(value, ranges::data(r), __detail::range_size(r), mask_value, f);
}

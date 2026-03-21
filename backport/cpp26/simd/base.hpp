using simd_size_type = ptrdiff_t;

template<class... Flags>
struct flags;

struct convert_flag;
struct aligned_flag;
template<size_t N>
struct overaligned_flag;

namespace detail {

template<class...>
using void_t = void;

template<class T>
using remove_cvref_t = typename remove_cv<typename remove_reference<T>::type>::type;

template<class T>
struct is_extended_integer : false_type {};

#if defined(__SIZEOF_INT128__)
template<>
struct is_extended_integer<__int128> : true_type {};

template<>
struct is_extended_integer<unsigned __int128> : true_type {};
#endif

template<class T>
struct is_complex_value : false_type {};

template<class T>
struct is_complex_value<complex<T>> : true_type {};

template<class T, class = void>
struct complex_value;

template<class T>
struct complex_value<complex<T>, void> {
    using type = T;
};

template<class T>
using complex_value_t = typename complex_value<remove_cvref_t<T>>::type;

template<class T>
struct is_extended_floating_point : false_type {};

#ifdef __STDCPP_FLOAT16_T__
template<>
struct is_extended_floating_point<float16_t> : true_type {};
#endif

#ifdef __STDCPP_FLOAT32_T__
template<>
struct is_extended_floating_point<float32_t> : true_type {};
#endif

#ifdef __STDCPP_FLOAT64_T__
template<>
struct is_extended_floating_point<float64_t> : true_type {};
#endif

template<class T>
struct is_vectorizable_floating_point
    : integral_constant<bool,
        is_same<remove_cvref_t<T>, float>::value ||
        is_same<remove_cvref_t<T>, double>::value ||
        is_extended_floating_point<remove_cvref_t<T>>::value> {};

template<class T>
struct is_vectorizable_integral
    : integral_constant<bool,
        is_integral<remove_cvref_t<T>>::value &&
        !is_same<remove_cvref_t<T>, bool>::value &&
        !is_extended_integer<remove_cvref_t<T>>::value> {};

template<class T>
struct is_supported_scalar_value
    : integral_constant<bool,
        is_vectorizable_integral<T>::value ||
        is_vectorizable_floating_point<T>::value> {};

template<class T, bool = is_complex_value<remove_cvref_t<T>>::value>
struct is_supported_complex_value : false_type {};

template<class T>
struct is_supported_complex_value<T, true> : is_vectorizable_floating_point<complex_value_t<T>> {};

template<class T>
struct is_supported_value
    : integral_constant<bool,
        is_supported_scalar_value<T>::value ||
        is_supported_complex_value<T>::value> {};

struct unavailable_real_type {};

template<class From, class To>
struct is_value_preserving_conversion;

template<class From, class To, bool = is_supported_complex_value<To>::value>
struct is_value_preserving_complex_target : false_type {};

template<class From, class To>
struct is_value_preserving_complex_target<From, To, true>
    : is_value_preserving_conversion<remove_cvref_t<From>, complex_value_t<To>> {};

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

#if defined(__SIZEOF_INT128__)
template<>
struct integer_from_size<16> {
    using type = __int128;
};
#endif

template<size_t Bytes, class = void>
struct has_integer_from_size : false_type {};

template<size_t Bytes>
struct has_integer_from_size<Bytes, void_t<typename integer_from_size<Bytes>::type>> : true_type {};

template<size_t Bytes, class = void>
struct mask_representative_value;

template<>
struct mask_representative_value<1, void> {
    using type = signed char;
};

template<>
struct mask_representative_value<2, void> {
    using type = short;
};

template<>
struct mask_representative_value<4, void> {
    using type = int;
};

template<>
struct mask_representative_value<8, void> {
    using type = long long;
};

template<>
struct mask_representative_value<16, void> {
    using type = complex<double>;
};

template<size_t Bytes, class = void>
struct has_mask_representative_value : false_type {};

template<size_t Bytes>
struct has_mask_representative_value<Bytes, void_t<typename mask_representative_value<Bytes>::type>> : true_type {};

template<size_t Bytes>
using mask_representative_value_t = typename mask_representative_value<Bytes>::type;

template<size_t Bytes>
struct has_vectorizable_signed_integer_of_size
    : integral_constant<bool,
        has_integer_from_size<Bytes>::value &&
        is_supported_value<typename integer_from_size<Bytes>::type>::value &&
        is_signed<typename integer_from_size<Bytes>::type>::value> {};

inline constexpr simd_size_type max_supported_fixed_size = 64;

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
struct reduction_identity;

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

template<class T>
struct reduction_identity<T, bit_and<>> {
    static constexpr T value() noexcept {
        return static_cast<T>(~T{});
    }
};

template<class T>
struct reduction_identity<T, bit_and<T>> {
    static constexpr T value() noexcept {
        return static_cast<T>(~T{});
    }
};

template<class T>
struct reduction_identity<T, bit_or<>> {
    static constexpr T value() noexcept {
        return T{};
    }
};

template<class T>
struct reduction_identity<T, bit_or<T>> {
    static constexpr T value() noexcept {
        return T{};
    }
};

template<class T>
struct reduction_identity<T, bit_xor<>> {
    static constexpr T value() noexcept {
        return T{};
    }
};

template<class T>
struct reduction_identity<T, bit_xor<T>> {
    static constexpr T value() noexcept {
        return T{};
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
        ) ||
        (
            is_arithmetic<remove_cvref_t<From>>::value &&
            is_supported_complex_value<To>::value &&
            is_value_preserving_complex_target<From, To>::value
        )> {};

template<class From, class To>
struct is_implicit_simd_conversion : is_value_preserving_conversion<From, To> {};

template<class From>
struct is_implicit_simd_conversion<From, bool> : is_same<detail::remove_cvref_t<From>, bool> {};

template<class To, class From, class FlagsPack>
constexpr To convert_or_copy(const From& value, FlagsPack) noexcept {
    using source_type = remove_cvref_t<From>;

    static_assert(is_supported_value<To>::value, "std::simd value type must be a supported std::simd value type");
    static_assert(is_supported_value<source_type>::value, "unchecked_load/unchecked_store source type must be a supported std::simd value type");
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
struct native_abi {};

namespace detail {

template<class T, simd_size_type N>
struct is_deduce_abi_available
    : integral_constant<bool,
        is_supported_value<T>::value &&
        (N > 0) &&
        (N <= max_supported_fixed_size)> {};

template<class T, class Abi>
struct is_enabled_basic_vec : false_type {};

template<class T, simd_size_type N>
struct is_enabled_basic_vec<T, fixed_size_abi<N>> : is_deduce_abi_available<T, N> {};

template<class T, class U>
struct is_enabled_basic_vec<T, native_abi<U>>
    : integral_constant<bool,
        is_supported_value<T>::value &&
        is_supported_value<U>::value &&
        sizeof(remove_cvref_t<T>) == sizeof(remove_cvref_t<U>)> {};

template<size_t Bytes, class Abi>
struct is_enabled_basic_mask : false_type {};

template<size_t Bytes, simd_size_type N>
struct is_enabled_basic_mask<Bytes, fixed_size_abi<N>>
    : integral_constant<bool,
        has_mask_representative_value<Bytes>::value &&
        (N > 0) &&
        (N <= max_supported_fixed_size)> {};

template<size_t Bytes, class U>
struct is_enabled_basic_mask<Bytes, native_abi<U>>
    : integral_constant<bool,
        has_mask_representative_value<Bytes>::value &&
        is_supported_value<U>::value &&
        sizeof(remove_cvref_t<U>) == Bytes> {};

template<class Flag>
struct is_valid_flag : false_type {};

template<>
struct is_valid_flag<simd::convert_flag> : true_type {};

template<>
struct is_valid_flag<simd::aligned_flag> : true_type {};

template<size_t N>
struct is_valid_flag<simd::overaligned_flag<N>> : true_type {};

template<class T, simd_size_type N, class = void>
struct deduce_abi {};

template<class T, simd_size_type N>
struct deduce_abi<T, N, enable_if_t<is_deduce_abi_available<T, N>::value>> {
    using type = typename conditional<
        N == native_lane_count<remove_cvref_t<T>>::value,
        native_abi<remove_cvref_t<T>>,
        fixed_size_abi<N>>::type;
};

} // namespace detail

template<class V>
class simd_iterator;

template<class T, class Abi>
struct simd_size;

template<class T, simd_size_type N>
struct simd_size<T, fixed_size_abi<N>>
    : integral_constant<simd_size_type, detail::is_enabled_basic_vec<T, fixed_size_abi<N>>::value ? N : 0> {};

template<class T, class U>
struct simd_size<T, native_abi<U>>
    : integral_constant<simd_size_type,
        detail::is_enabled_basic_vec<T, native_abi<U>>::value ? detail::native_lane_count<detail::remove_cvref_t<U>>::value : 0> {};

template<class Abi>
struct abi_lane_count;

template<simd_size_type N>
struct abi_lane_count<fixed_size_abi<N>> : integral_constant<simd_size_type, N> {};

template<class T>
struct abi_lane_count<native_abi<T>> : detail::native_lane_count<T> {};

template<class T, simd_size_type N = simd_size<T, native_abi<T>>::value>
using deduce_abi_t = typename detail::deduce_abi<T, N>::type;

template<class... Flags>
struct flags {
    static_assert((detail::is_valid_flag<Flags>::value && ...),
        "std::simd::flags only accepts std::simd::convert_flag, std::simd::aligned_flag, and std::simd::overaligned_flag<N>");
};

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
consteval flags<Left..., Right...> operator|(flags<Left...>, flags<Right...>) noexcept {
    return {};
}

template<class T, class Abi = native_abi<T>>
class basic_vec;

template<size_t Bytes, class Abi>
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

template<class T, class V, bool = detail::is_complex_value<detail::remove_cvref_t<T>>::value>
struct real_simd_type {
    using type = detail::unavailable_real_type;
};

template<class T, class V>
struct real_simd_type<T, V, true> {
    using type = rebind_t<detail::complex_value_t<T>, V>;
};

template<class T, class V>
using real_simd_t = typename real_simd_type<T, V>::type;

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

template<class From, class To, class = void>
struct is_explicitly_simd_convertible : false_type {};

template<class From, class To>
struct is_explicitly_simd_convertible<From,
                                      To,
                                      void_t<decltype(static_cast<To>(declval<const remove_cvref_t<From>&>()))>>
    : true_type {};

template<class I, class = void>
struct has_simd_value_type : false_type {};

template<class I>
struct has_simd_value_type<I, void_t<typename remove_cvref_t<I>::value_type, decltype(remove_cvref_t<I>::size)>> : true_type {};

template<class I, class = void>
struct has_lane_subscript : false_type {};

template<class I>
struct has_lane_subscript<I, void_t<decltype(declval<const remove_cvref_t<I>&>()[simd_size_type{}])>> : true_type {};

template<class T, class = void>
struct is_constexpr_wrapper_like : false_type {};

template<class T>
struct is_constexpr_wrapper_like<T, void_t<
    decltype(T::value),
    typename enable_if<is_convertible<T, remove_cvref_t<decltype(T::value)>>::value>::type,
    decltype(bool_constant<(T{} == T::value)>{}),
    decltype(bool_constant<(static_cast<remove_cvref_t<decltype(T::value)>>(T{}) == T::value)>{})
>> : true_type {};

template<class Wrapper, class To, class = void>
struct is_constexpr_wrapper_value_representable : false_type {};

template<class Wrapper, class To>
struct is_constexpr_wrapper_value_representable<
    Wrapper,
    To,
    typename enable_if<
        is_constexpr_wrapper_like<Wrapper>::value &&
        is_arithmetic<remove_cvref_t<decltype(Wrapper::value)>>::value &&
        is_arithmetic<To>::value>::type>
    : integral_constant<bool,
        is_value_preserving_conversion<remove_cvref_t<decltype(Wrapper::value)>, To>::value ||
        static_cast<remove_cvref_t<decltype(Wrapper::value)>>(static_cast<To>(Wrapper::value)) == Wrapper::value> {};

template<class From, class To, bool IsArithmetic = is_arithmetic<remove_cvref_t<From>>::value,
         bool IsWrapper = is_constexpr_wrapper_like<remove_cvref_t<From>>::value>
struct is_implicit_simd_broadcast_condition : false_type {};

template<class From, class To>
struct is_implicit_simd_broadcast_condition<From, To, false, false> : true_type {};

template<class From, class To>
struct is_implicit_simd_broadcast_condition<From, To, true, false>
    : is_value_preserving_conversion<From, To> {};

template<class From, class To>
struct is_implicit_simd_broadcast_condition<From, To, false, true>
    : integral_constant<bool,
        is_arithmetic<remove_cvref_t<decltype(remove_cvref_t<From>::value)>>::value &&
        is_constexpr_wrapper_value_representable<remove_cvref_t<From>, To>::value> {};

template<class From, class To>
struct is_implicit_simd_broadcast
    : integral_constant<bool,
        is_convertible<From, To>::value &&
        is_implicit_simd_broadcast_condition<From, To>::value> {};

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
struct is_data_parallel_type<basic_vec<T, Abi>> : is_enabled_basic_vec<T, Abi> {};

template<size_t Bytes, class Abi>
struct is_data_parallel_type<basic_mask<Bytes, Abi>> : is_enabled_basic_mask<Bytes, Abi> {};

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

template<class I, class S, class = void>
struct is_sized_sentinel_for : false_type {};

template<class I, class S>
struct is_sized_sentinel_for<I, S, void_t<decltype(declval<S>() - declval<I>())>> : true_type {};

template<class I>
using iterator_reference_t = decltype(*declval<I&>());

template<class I, class = void>
struct is_random_access_load_store_iterator : false_type {};

template<class I>
struct is_random_access_load_store_iterator<I, void_t<decltype(++declval<I&>()), iterator_reference_t<I>>>
    : integral_constant<bool,
        contiguous_iterator<I> &&
        is_lvalue_reference<iterator_reference_t<I>>::value &&
        is_supported_value<typename remove_reference<iterator_reference_t<I>>::type>::value> {};

template<class I, class = void>
struct is_writable_load_store_iterator : false_type {};

template<class I>
struct is_writable_load_store_iterator<I,
    void_t<decltype(++declval<I&>()), iterator_reference_t<I>, decltype(*declval<I&>() = *declval<I&>())>>
    : integral_constant<bool,
        is_random_access_load_store_iterator<I>::value &&
        !is_const<typename remove_reference<iterator_reference_t<I>>::type>::value> {};

template<class R, class = void>
struct is_contiguous_load_store_range : false_type {};

template<class R>
struct is_contiguous_load_store_range<R,
    void_t<decltype(ranges::data(declval<R&>())),
           decltype(ranges::size(declval<R&>())),
           typename ranges::range_value_t<remove_cvref_t<R>>>>
    : integral_constant<bool,
        ranges::contiguous_range<remove_cvref_t<R>> &&
        ranges::sized_range<remove_cvref_t<R>> &&
        is_supported_value<typename ranges::range_value_t<remove_cvref_t<R>>>::value> {};

template<class R, class = void>
struct is_writable_load_store_range : false_type {};

template<class R>
struct is_writable_load_store_range<R,
    void_t<decltype(ranges::data(declval<R&>())),
           decltype(ranges::size(declval<R&>())),
           typename ranges::range_reference_t<remove_cvref_t<R>>>>
    : integral_constant<bool,
        is_contiguous_load_store_range<R>::value &&
        !is_const<typename remove_reference<typename ranges::range_reference_t<remove_cvref_t<R>>>::type>::value> {};

template<class R, class = void>
struct is_contiguous_mask_range : false_type {};

template<class R>
struct is_contiguous_mask_range<R,
    void_t<decltype(ranges::data(declval<R&>())),
           decltype(ranges::size(declval<R&>())),
           typename ranges::range_reference_t<remove_cvref_t<R>>>>
    : integral_constant<bool,
        ranges::contiguous_range<remove_cvref_t<R>> &&
        ranges::sized_range<remove_cvref_t<R>> &&
        is_constructible<bool, typename ranges::range_reference_t<remove_cvref_t<R>>>::value> {};

template<class R, class = void>
struct fixed_range_size : integral_constant<simd_size_type, -1> {};

template<class T, size_t N>
struct fixed_range_size<T[N], void> : integral_constant<simd_size_type, static_cast<simd_size_type>(N)> {};

template<class T, size_t N>
struct fixed_range_size<array<T, N>, void> : integral_constant<simd_size_type, static_cast<simd_size_type>(N)> {};

template<class T, size_t N>
struct fixed_range_size<span<T, N>, typename enable_if<N != dynamic_extent>::type>
    : integral_constant<simd_size_type, static_cast<simd_size_type>(N)> {};

template<class R, simd_size_type N>
struct has_matching_fixed_range_size
    : integral_constant<bool, fixed_range_size<remove_cvref_t<R>>::value == N> {};

template<class I, class S>
constexpr simd_size_type iterator_distance(I first, S last) noexcept {
    return static_cast<simd_size_type>(last - first);
}

template<class R>
constexpr simd_size_type range_size(R&& r) noexcept(noexcept(ranges::size(r))) {
    return static_cast<simd_size_type>(ranges::size(r));
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

template<class G, class R, size_t I, class = void>
struct is_simd_generator_lane : false_type {};

template<class G, class R, size_t I>
struct is_simd_generator_lane<G, R, I, void_t<decltype(declval<G&>()(generator_index_constant<I>{}))>>
    : integral_constant<bool,
        is_implicit_simd_conversion<decltype(declval<G&>()(generator_index_constant<I>{})), R>::value> {};

template<class G, class R, size_t... I>
struct is_simd_generator_sequence<G, R, index_sequence<I...>>
    : conjunction<is_simd_generator_lane<G, R, I>...> {};

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

template<class T, class BinaryOperation, class = void>
struct is_reduction_binary_operation : false_type {};

template<class T, class BinaryOperation>
struct is_reduction_binary_operation<
    T,
    BinaryOperation,
    void_t<decltype(declval<const BinaryOperation&>()(declval<const vec<T, 1>&>(), declval<const vec<T, 1>&>()))>>
    : is_same<remove_cvref_t<decltype(declval<const BinaryOperation&>()(declval<const vec<T, 1>&>(),
                                                                        declval<const vec<T, 1>&>()))>,
              vec<T, 1>> {};

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

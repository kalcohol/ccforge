#pragma once

using __simd_size_type = ptrdiff_t;

template<class... Flags>
struct flags;

struct __convert_flag;
struct __aligned_flag;
template<size_t N>
struct __overaligned_flag;

namespace __detail {

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

template<size_t Bytes, class = void>
struct has_vectorizable_signed_integer_of_size
    : false_type {};

template<size_t Bytes>
struct has_vectorizable_signed_integer_of_size<Bytes, void_t<typename integer_from_size<Bytes>::type>>
    : integral_constant<bool,
        has_integer_from_size<Bytes>::value &&
        is_supported_value<typename integer_from_size<Bytes>::type>::value &&
        is_signed<typename integer_from_size<Bytes>::type>::value> {};

inline constexpr __simd_size_type max_supported_fixed_size = 64;

template<class T, size_t NativeBytes>
struct native_lane_count
    : integral_constant<__simd_size_type,
        ((NativeBytes / sizeof(T)) > 0 ? (NativeBytes / sizeof(T)) : 1)> {};

template<class Flag, class FlagsPack>
struct has_flag : false_type {};

template<class Flag, class... Flags>
struct has_flag<Flag, simd::flags<Flags...>>
    : disjunction<is_same<Flag, Flags>...> {};

template<class Flag>
struct is_alignment_flag : false_type {};

template<>
struct is_alignment_flag<simd::__aligned_flag> : true_type {};

template<size_t N>
struct is_alignment_flag<simd::__overaligned_flag<N>> : true_type {};

template<class FlagsPack>
struct has_alignment_flag : false_type {};

template<class... Flags>
struct has_alignment_flag<simd::flags<Flags...>> : disjunction<is_alignment_flag<Flags>...> {};

template<class I, class FlagsPack>
constexpr void require_iterator_compatible_flags() noexcept {
    if constexpr (!contiguous_iterator<remove_cvref_t<I>>) {
        static_assert(!has_alignment_flag<FlagsPack>::value,
            "alignment flags require contiguous iterators");
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
struct is_implicit_simd_conversion<From, bool> : is_same<__detail::remove_cvref_t<From>, bool> {};

template<class To, class From, class FlagsPack>
constexpr To convert_or_copy(const From& value, FlagsPack) noexcept {
    using source_type = remove_cvref_t<From>;

    static_assert(is_supported_value<To>::value, "std::simd value type must be a supported std::simd value type");
    static_assert(is_supported_value<source_type>::value, "unchecked_load/unchecked_store source type must be a supported std::simd value type");
    static_assert(
        is_value_preserving_conversion<source_type, To>::value || has_flag<simd::__convert_flag, FlagsPack>::value,
        "type-changing load/store requires std::simd::flag_convert unless the conversion is value-preserving");

    return static_cast<To>(value);
}

} // namespace __detail

template<__simd_size_type N>
struct __fixed_size_abi {
    static_assert(N > 0, "std::simd lane count must be positive");
};

namespace __detail {

// Width is part of the tag identity, including across differently targeted TUs.
template<class T, __simd_size_type N>
struct __native_abi_tag {
    static constexpr __simd_size_type width = N;
};

} // namespace __detail

#if defined(__AVX512F__)
template<class T>
using __native_abi = __detail::__native_abi_tag<T, __detail::native_lane_count<T, 64>::value>;
#elif defined(__AVX2__) || defined(__AVX__)
template<class T>
using __native_abi = __detail::__native_abi_tag<T, __detail::native_lane_count<T, 32>::value>;
#elif defined(__SSE2__) || defined(__ARM_NEON) || defined(__aarch64__) || \
    (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64)))
// MSVC x64 has baseline SSE2; ARM64 has baseline NEON. Keep ISA feature
// macros above these target defaults, including when compiling with clang-cl.
template<class T>
using __native_abi = __detail::__native_abi_tag<T, __detail::native_lane_count<T, 16>::value>;
#else
template<class T>
using __native_abi = __detail::__native_abi_tag<T, 1>;
#endif

namespace __detail {

template<class T, __simd_size_type N>
struct is_deduce_abi_available
    : integral_constant<bool,
        is_supported_value<T>::value &&
        (N > 0) &&
        (N <= max_supported_fixed_size)> {};

template<class T, class Abi>
struct is_enabled_basic_vec : false_type {};

template<class T, __simd_size_type N>
struct is_enabled_basic_vec<T, __fixed_size_abi<N>> : is_deduce_abi_available<T, N> {};

template<class T, class U, __simd_size_type N>
struct is_enabled_basic_vec<T, __native_abi_tag<U, N>>
    : integral_constant<bool,
        is_deduce_abi_available<T, N>::value &&
        is_supported_value<U>::value &&
        sizeof(remove_cvref_t<T>) == sizeof(remove_cvref_t<U>)> {};

template<size_t Bytes, class Abi>
struct is_enabled_basic_mask : false_type {};

template<size_t Bytes, __simd_size_type N>
struct is_enabled_basic_mask<Bytes, __fixed_size_abi<N>>
    : integral_constant<bool,
        has_mask_representative_value<Bytes>::value &&
        (N > 0) &&
        (N <= max_supported_fixed_size)> {};

template<size_t Bytes, class U, __simd_size_type N>
struct is_enabled_basic_mask<Bytes, __native_abi_tag<U, N>>
    : integral_constant<bool,
        has_mask_representative_value<Bytes>::value &&
        is_deduce_abi_available<U, N>::value &&
        sizeof(remove_cvref_t<U>) == Bytes> {};

template<class Flag>
struct is_valid_flag : false_type {};

template<>
struct is_valid_flag<simd::__convert_flag> : true_type {};

template<>
struct is_valid_flag<simd::__aligned_flag> : true_type {};

template<size_t N>
struct is_valid_flag<simd::__overaligned_flag<N>> : true_type {};

// Invalid deductions still name a type, but not an enabled ABI tag.
struct unavailable_abi {};

template<class T, __simd_size_type N, class = void>
struct deduce_abi {
    using type = unavailable_abi;
};

template<class T, __simd_size_type N>
struct deduce_abi<T, N, enable_if_t<is_deduce_abi_available<T, N>::value>> {
    using type = typename conditional<
        N == __native_abi<remove_cvref_t<T>>::width,
        __native_abi<remove_cvref_t<T>>,
        __fixed_size_abi<N>>::type;
};

} // namespace __detail

template<class V>
class __simd_iterator;

template<class T, class Abi>
struct __simd_size;

template<class T>
struct __simd_size<T, __detail::unavailable_abi> : integral_constant<__simd_size_type, 0> {};

template<class T, __simd_size_type N>
struct __simd_size<T, __fixed_size_abi<N>>
    : integral_constant<__simd_size_type, __detail::is_enabled_basic_vec<T, __fixed_size_abi<N>>::value ? N : 0> {};

template<class T, class U, __simd_size_type N>
struct __simd_size<T, __detail::__native_abi_tag<U, N>>
    : integral_constant<__simd_size_type,
        __detail::is_enabled_basic_vec<T, __detail::__native_abi_tag<U, N>>::value ? N : 0> {};

template<class Abi>
struct __abi_lane_count;

template<__simd_size_type N>
struct __abi_lane_count<__fixed_size_abi<N>> : integral_constant<__simd_size_type, N> {};

template<class T, __simd_size_type N>
struct __abi_lane_count<__detail::__native_abi_tag<T, N>> : integral_constant<__simd_size_type, N> {};

template<class T, __simd_size_type N = __simd_size<T, __native_abi<T>>::value>
using __deduce_abi_t = typename __detail::deduce_abi<T, N>::type;

template<class... Flags>
struct flags {
    static_assert((__detail::is_valid_flag<Flags>::value && ...),
        "std::simd::flags only accepts std::simd::__convert_flag, std::simd::__aligned_flag, and std::simd::__overaligned_flag<N>");
};

struct __convert_flag {};
struct __aligned_flag {};

template<size_t N>
struct __overaligned_flag {
    static_assert(__detail::is_power_of_two<N>::value, "std::simd::flag_overaligned requires a non-zero power-of-two alignment");
};

inline constexpr flags<> flag_default{};
inline constexpr flags<__convert_flag> flag_convert{};
inline constexpr flags<__aligned_flag> flag_aligned{};

template<size_t N>
    requires(__detail::is_power_of_two<N>::value)
inline constexpr flags<__overaligned_flag<N>> flag_overaligned{};

template<class... Left, class... Right>
consteval flags<Left..., Right...> operator|(flags<Left...>, flags<Right...>) noexcept {
    return {};
}

template<class T, class Abi = __native_abi<T>>
class basic_vec;

template<size_t Bytes, class Abi>
class basic_mask;

template<class T, __simd_size_type N = __simd_size<T, __native_abi<T>>::value>
using vec = basic_vec<T, __deduce_abi_t<T, N>>;

template<class T, __simd_size_type N = __simd_size<T, __native_abi<T>>::value>
using mask = basic_mask<sizeof(T), __deduce_abi_t<T, N>>;

template<class T, class U = typename T::value_type>
struct alignment;

template<class T, class U>
    requires(__detail::is_supported_value<U>::value)
struct alignment<basic_vec<T, __detail::unavailable_abi>, U>
    : integral_constant<size_t, alignof(U)> {};

template<class T, class U = typename T::value_type>
inline constexpr size_t alignment_v = alignment<T, U>::value;

template<class T, class V>
struct rebind {};

template<class T, class V>
using rebind_t = typename rebind<T, V>::type;

template<class T, class V, bool = __detail::is_complex_value<__detail::remove_cvref_t<T>>::value>
struct __real_simd_type {
    using type = __detail::unavailable_real_type;
};

template<class T, class V>
struct __real_simd_type<T, V, true> {
    using type = rebind_t<__detail::complex_value_t<T>, V>;
};

template<class T, class V>
using __real_simd_t = typename __real_simd_type<T, V>::type;

template<__simd_size_type N, class V>
struct resize {};

template<__simd_size_type N, class V>
using resize_t = typename resize<N, V>::type;

inline constexpr __simd_size_type zero_element = -1;
inline constexpr __simd_size_type uninit_element = -2;

namespace __detail {

// Bytes > 8 uses the widest portable lane, including complex<double> masks.
template<size_t Bytes>
using mask_lane_t = conditional_t<Bytes == 1, uint8_t,
    conditional_t<Bytes == 2, uint16_t,
    conditional_t<Bytes == 4, uint32_t, uint64_t>>>;

template<size_t Bytes>
constexpr mask_lane_t<Bytes> mask_bits(bool value) noexcept {
    return static_cast<mask_lane_t<Bytes>>(mask_lane_t<Bytes>{0} -
        static_cast<mask_lane_t<Bytes>>(value));
}

struct mask_access {
    template<size_t Bytes, class Abi>
    static constexpr mask_lane_t<Bytes> get(const basic_mask<Bytes, Abi>& value,
                                           __simd_size_type i) noexcept {
        return value.data_[i];
    }

    template<size_t Bytes, class Abi>
    static constexpr void set(basic_mask<Bytes, Abi>& value, __simd_size_type i, bool lane) noexcept {
        value.data_[i] = mask_bits<Bytes>(lane);
    }

    // Both source and destination lanes use the zero/all-ones invariant.
    template<size_t Bytes, class Abi, size_t SourceBytes, class SourceAbi>
    static constexpr void copy(basic_mask<Bytes, Abi>& destination, __simd_size_type to,
                               const basic_mask<SourceBytes, SourceAbi>& source,
                               __simd_size_type from) noexcept {
        if constexpr (sizeof(mask_lane_t<Bytes>) == sizeof(mask_lane_t<SourceBytes>)) {
            destination.data_[to] = source.data_[from];
        } else {
            // Sign extension preserves all ones; narrowing preserves the low bits.
            using source_lane = mask_lane_t<SourceBytes>;
            destination.data_[to] = static_cast<mask_lane_t<Bytes>>(
                bit_cast<make_signed_t<source_lane>>(source.data_[from]));
        }
    }
};

template<size_t Bytes, class Abi>
constexpr mask_lane_t<Bytes> mask_lane(const basic_mask<Bytes, Abi>& value, __simd_size_type i) noexcept {
    return mask_access::get(value, i);
}

template<class V>
constexpr auto lane_value(const V& value, __simd_size_type i) noexcept {
    if constexpr (is_same_v<typename V::value_type, bool>) {
        return mask_lane(value, i);
    } else {
        return value[i];
    }
}

// A complete 128-bit block can use the blend loop even at the baseline ISA.
// Scalar tails stay conditional, including vec<T, 1> reduction accumulators.
template<size_t Bytes>
inline constexpr __simd_size_type mask_blend_block = 16 / sizeof(mask_lane_t<Bytes>);

template<size_t Bytes, __simd_size_type Lanes>
inline constexpr __simd_size_type mask_blend_prefix =
    Lanes / mask_blend_block<Bytes> * mask_blend_block<Bytes>;

template<size_t Bytes, __simd_size_type Lanes, class T>
inline constexpr __simd_size_type select_blend_prefix =
#if defined(__clang__)
    // Four-lane 32-bit conditionals are cheaper in Clang's measured kernels.
    (Lanes == 4 && sizeof(T) == 4 && is_arithmetic_v<T>) ? 0 :
#endif
    mask_blend_prefix<Bytes, Lanes>;

template<size_t Bytes, bool Blend, class T>
constexpr T select_lane(mask_lane_t<Bytes> bits, const T& when_true, const T& when_false) noexcept {
    if constexpr (Blend && sizeof(T) == sizeof(mask_lane_t<Bytes>) && is_arithmetic_v<T>) {
        if (!is_constant_evaluated()) {
            using lane_type = mask_lane_t<Bytes>;
            return bit_cast<T>(static_cast<lane_type>(
                (bits & bit_cast<lane_type>(when_true)) |
                (static_cast<lane_type>(~bits) & bit_cast<lane_type>(when_false))));
        }
    }
    return bits ? when_true : when_false;
}

template<class T, class Abi>
constexpr T& lane_ref(basic_vec<T, Abi>& value, __simd_size_type i) noexcept;

template<class V, class U>
constexpr void set_lane(V& value, __simd_size_type i, U&& lane) noexcept;

template<class Destination, class Source>
constexpr void copy_lane(Destination& destination, __simd_size_type to,
                         const Source& source, __simd_size_type from) noexcept {
    if constexpr (is_same_v<typename Destination::value_type, bool> &&
                  is_same_v<typename Source::value_type, bool>) {
        mask_access::copy(destination, to, source, from);
    } else {
        set_lane(destination, to, source[from]);
    }
}

template<class From, class To, class = void>
struct is_explicitly_simd_convertible : false_type {};

template<class From, class To>
struct is_explicitly_simd_convertible<From,
                                      To,
                                      void_t<decltype(static_cast<To>(declval<const remove_cvref_t<From>&>()))>>
    : true_type {};

template<class T>
consteval int integer_conversion_rank() {
    using value_type = remove_cvref_t<T>;

    if constexpr (is_same<value_type, bool>::value) {
        return 0;
    } else if constexpr (is_same<value_type, char>::value ||
                         is_same<value_type, signed char>::value ||
                         is_same<value_type, unsigned char>::value ||
                         is_same<value_type, char8_t>::value) {
        return 1;
    } else if constexpr (is_same<value_type, short>::value ||
                         is_same<value_type, unsigned short>::value) {
        return 2;
    } else if constexpr (is_same<value_type, int>::value ||
                         is_same<value_type, unsigned int>::value) {
        return 3;
    } else if constexpr (is_same<value_type, long>::value ||
                         is_same<value_type, unsigned long>::value) {
        return 4;
    } else if constexpr (is_same<value_type, long long>::value ||
                         is_same<value_type, unsigned long long>::value) {
        return 5;
    } else if constexpr (numeric_limits<value_type>::digits <= numeric_limits<unsigned char>::digits) {
        return 1;
    } else if constexpr (numeric_limits<value_type>::digits <= numeric_limits<unsigned short>::digits) {
        return 2;
    } else if constexpr (numeric_limits<value_type>::digits <= numeric_limits<unsigned int>::digits) {
        return 3;
    } else if constexpr (numeric_limits<value_type>::digits <= numeric_limits<unsigned long>::digits) {
        return 4;
    } else {
        return 5;
    }
}

template<class From, class To>
consteval bool has_greater_floating_conversion_rank() {
    using source_type = remove_cvref_t<From>;
    using target_type = remove_cvref_t<To>;

    constexpr bool source_is_superset =
        numeric_limits<source_type>::digits >= numeric_limits<target_type>::digits &&
        numeric_limits<source_type>::max_exponent >= numeric_limits<target_type>::max_exponent &&
        numeric_limits<source_type>::min_exponent <= numeric_limits<target_type>::min_exponent;
    constexpr bool sets_differ =
        numeric_limits<source_type>::digits != numeric_limits<target_type>::digits ||
        numeric_limits<source_type>::max_exponent != numeric_limits<target_type>::max_exponent ||
        numeric_limits<source_type>::min_exponent != numeric_limits<target_type>::min_exponent;

    if constexpr (source_is_superset && sets_differ) {
        return true;
    } else if constexpr (!sets_differ &&
                         is_same<source_type, double>::value &&
                         is_same<target_type, float>::value) {
        return true;
    } else {
        return false;
    }
}

template<class From, class To,
         bool = is_integral<remove_cvref_t<From>>::value &&
                is_integral<remove_cvref_t<To>>::value>
struct has_greater_integer_conversion_rank : false_type {};

template<class From, class To>
struct has_greater_integer_conversion_rank<From, To, true>
    : integral_constant<bool, (integer_conversion_rank<From>() > integer_conversion_rank<To>())> {};

template<class From, class To,
         bool = is_floating_point<remove_cvref_t<From>>::value &&
                is_floating_point<remove_cvref_t<To>>::value>
struct has_greater_floating_rank : false_type {};

template<class From, class To>
struct has_greater_floating_rank<From, To, true>
    : integral_constant<bool, has_greater_floating_conversion_rank<From, To>()> {};

template<class From, class To>
struct is_explicit_simd_vector_conversion
    : integral_constant<bool,
        !is_value_preserving_conversion<From, To>::value ||
        has_greater_integer_conversion_rank<From, To>::value ||
        has_greater_floating_rank<From, To>::value> {};

template<class I, class = void>
struct has_simd_value_type : false_type {};

template<class I>
struct has_simd_value_type<I, void_t<typename remove_cvref_t<I>::value_type, decltype(remove_cvref_t<I>::size)>> : true_type {};

template<class I, class = void>
struct has_lane_subscript : false_type {};

template<class I>
struct has_lane_subscript<I, void_t<decltype(declval<const remove_cvref_t<I>&>()[__simd_size_type{}])>> : true_type {};

template<class T, class = void>
struct is_constexpr_wrapper_like : false_type {};

template<class T>
struct is_constexpr_wrapper_like<T, void_t<
    decltype(T::value),
    // Reject object-dependent glvalues without reading or copying static values.
    typename enable_if<
        !is_reference<decltype((T::value))>::value ||
        requires { typename bool_constant<(std::addressof(T::value), true)>; }
    >::type,
    // Check the value type alone before mixed comparisons or conversions.
    typename enable_if<equality_comparable<decltype(T::value)>>::type,
    typename enable_if<is_convertible<T, decltype(T::value)>::value>::type,
    typename enable_if<equality_comparable_with<T, decltype(T::value)>>::type,
    typename enable_if<bool_constant<(T{} == T::value)>::value>::type,
    typename enable_if<
        bool_constant<(static_cast<decltype(T::value)>(T{}) == T::value)>::value>::type
>> : true_type {};

template<class To, class From>
consteval bool is_arithmetic_value_representable(From value) {
    using target_type = remove_cvref_t<To>;
    using source_type = remove_cvref_t<From>;

    if constexpr (is_supported_complex_value<target_type>::value) {
        return is_arithmetic_value_representable<complex_value_t<target_type>>(value);
    } else if constexpr (is_integral<source_type>::value && is_integral<target_type>::value) {
        // std::in_range mandates away bool and the character types on
        // either side, and those rejections escape the immediate context
        // as hard errors; range-check through the widest integers of the
        // matching signedness instead.
        if constexpr (is_same<source_type, bool>::value) {
            return true;
        } else if constexpr (is_same<target_type, bool>::value) {
            return value == source_type{} ||
                value == static_cast<source_type>(1);
        } else {
            // Compare entirely in the source's own domain: when the source
            // is at least as wide as the target, converting the target's
            // bounds into the source type is exact; when it is narrower,
            // representability follows from the signedness relation alone.
            // No cast ever narrows the value, so extended integers such as
            // __int128 (which std::in_range rejects outright, and which a
            // long long detour would silently truncate) stay exact.
            constexpr auto target_min = numeric_limits<target_type>::min();
            constexpr auto target_max = numeric_limits<target_type>::max();
            if constexpr (is_signed<source_type>::value ==
                          is_signed<target_type>::value) {
                if constexpr (sizeof(source_type) >= sizeof(target_type)) {
                    return value >= static_cast<source_type>(target_min) &&
                        value <= static_cast<source_type>(target_max);
                } else {
                    return true;
                }
            } else if constexpr (is_signed<source_type>::value) {
                if (value < source_type{}) {
                    return false;
                }
                if constexpr (sizeof(source_type) > sizeof(target_type)) {
                    return value <= static_cast<source_type>(target_max);
                } else {
                    // A non-negative signed value always fits an unsigned
                    // target of at least the source's width.
                    return true;
                }
            } else {
                if constexpr (sizeof(source_type) >= sizeof(target_type)) {
                    return value <= static_cast<source_type>(target_max);
                } else {
                    // A narrower unsigned value always fits a wider signed
                    // target.
                    return true;
                }
            }
        }
    } else if constexpr (is_integral<source_type>::value && is_floating_point<target_type>::value) {
        if constexpr (is_same<source_type, bool>::value) {
            return true;
        } else {
            using unsigned_source = typename make_unsigned<source_type>::type;
            auto magnitude = static_cast<unsigned_source>(value);
            if constexpr (is_signed<source_type>::value) {
                if (value < 0) {
                    magnitude = unsigned_source{} - magnitude;
                }
            }

            int bit_count = 0;
            for (auto remaining = magnitude; remaining != 0; remaining >>= 1) {
                ++bit_count;
            }
            if (bit_count > numeric_limits<target_type>::max_exponent) {
                return false;
            }
            if (bit_count <= numeric_limits<target_type>::digits) {
                return true;
            }

            const int discarded_bits = bit_count - numeric_limits<target_type>::digits;
            for (int i = 0; i < discarded_bits; ++i) {
                if ((magnitude & (unsigned_source{1} << i)) != 0) {
                    return false;
                }
            }
            return true;
        }
    } else if constexpr (is_floating_point<source_type>::value && is_integral<target_type>::value) {
        if (value != value) {
            return false;
        }

        source_type upper_bound = source_type{1};
        for (int i = 0; i < numeric_limits<target_type>::digits; ++i) {
            upper_bound *= source_type{2};
        }
        if constexpr (is_signed<target_type>::value) {
            if (!(value >= -upper_bound && value < upper_bound)) {
                return false;
            }
        } else if (!(value >= source_type{} && value < upper_bound)) {
            return false;
        }

        const auto converted = static_cast<target_type>(value);
        return static_cast<source_type>(converted) == value;
    } else if constexpr (is_floating_point<source_type>::value && is_floating_point<target_type>::value) {
        if (value != value) {
            return false;
        }
        if (value == numeric_limits<source_type>::infinity() ||
            value == -numeric_limits<source_type>::infinity()) {
            return numeric_limits<target_type>::has_infinity;
        }

        const auto target_max = static_cast<source_type>(numeric_limits<target_type>::max());
        if (value > target_max || value < -target_max) {
            return false;
        }
        const auto converted = static_cast<target_type>(value);
        return static_cast<source_type>(converted) == value;
    } else {
        return false;
    }
}

template<class Wrapper, class To, class = void>
struct is_constexpr_wrapper_value_representable : false_type {};

template<class Wrapper, class To>
struct is_constexpr_wrapper_value_representable<
    Wrapper,
    To,
    typename enable_if<
        is_constexpr_wrapper_like<Wrapper>::value &&
        is_arithmetic<remove_cvref_t<decltype(Wrapper::value)>>::value &&
        (is_arithmetic<To>::value || is_supported_complex_value<To>::value)>::type>
    : integral_constant<bool, is_arithmetic_value_representable<To>(Wrapper::value)> {};

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

template<class From, class To, bool = is_convertible<From, To>::value>
struct is_implicit_simd_broadcast : false_type {};

template<class From, class To>
struct is_implicit_simd_broadcast<From, To, true>
    : is_implicit_simd_broadcast_condition<From, To> {};

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

template<class V, class Indices, bool = is_simd_index_vector<Indices>::value>
struct has_matching_index_width : false_type {};

template<class V, class Indices>
struct has_matching_index_width<V, Indices, true>
    : integral_constant<bool,
        static_cast<__simd_size_type>(V::size) ==
        static_cast<__simd_size_type>(remove_cvref_t<Indices>::size)> {};

template<class V, class Indices>
constexpr void require_matching_index_width() noexcept {
    static_assert(has_matching_index_width<V, Indices>::value,
        "std::simd gather requires the result and index vectors to have matching widths");
}

template<class V, class Indices>
using permute_result_t = resize_t<remove_cvref_t<Indices>::size, V>;

template<class V>
struct lane_mapped_value {
    using type = typename V::value_type;
};

template<size_t Bytes, class Abi>
struct lane_mapped_value<basic_mask<Bytes, Abi>> {
    using type = mask_lane_t<Bytes>;
};

template<class V>
using lane_mapped_value_t = typename lane_mapped_value<V>::type;

template<class T, class Abi>
struct is_data_parallel_type<basic_vec<T, Abi>> : is_enabled_basic_vec<T, Abi> {};

template<size_t Bytes, class Abi>
struct is_data_parallel_type<basic_mask<Bytes, Abi>> : is_enabled_basic_mask<Bytes, Abi> {};

template<class V>
constexpr lane_mapped_value_t<V> permute_lane(const V& value, __simd_size_type index) noexcept {
    return index == zero_element || index == uninit_element ? lane_mapped_value_t<V>{} : static_cast<lane_mapped_value_t<V>>(lane_value(value, index));
}

template<__simd_size_type Size, class IndexMap>
constexpr decltype(auto) invoke_index_map(IndexMap& index_map, __simd_size_type lane) {
    if constexpr (is_invocable<IndexMap&, __simd_size_type, __simd_size_type>::value) {
        return index_map(static_cast<__simd_size_type>(lane), static_cast<__simd_size_type>(Size));
    } else {
        return index_map(static_cast<__simd_size_type>(lane));
    }
}

template<__simd_size_type Size, class Index>
constexpr bool is_valid_static_permute_index(const Index& index) {
    return index == zero_element || index == uninit_element ||
        (index >= 0 && index < Size);
}

template<__simd_size_type Index, bool Valid>
struct checked_static_permute_index : integral_constant<__simd_size_type, Index> {
    static_assert(Valid,
        "std::simd static permute index maps must produce a sentinel or a valid source index");
};

// Only return types are queried; neither form constructs or copies the map.
// GNU needs body deduction here for portability; other compilers retain the prototype.
#if defined(__GNUC__) && !defined(__clang__)
template<__simd_size_type Size, __simd_size_type Lane, class IndexMap>
    requires requires(IndexMap& index_map) {
        typename bool_constant<__detail::is_valid_static_permute_index<Size>(
            __detail::invoke_index_map<Size>(index_map, Lane))>;
    }
auto static_permute_index_constant(IndexMap& index_map) {
    constexpr bool valid_index = __detail::is_valid_static_permute_index<Size>(
        __detail::invoke_index_map<Size>(index_map, Lane));
    constexpr __simd_size_type index = valid_index
        ? static_cast<__simd_size_type>(__detail::invoke_index_map<Size>(index_map, Lane)) : 0;
    return checked_static_permute_index<index, valid_index>{};
}
#else
template<__simd_size_type Size, __simd_size_type Lane, class IndexMap>
auto static_permute_index_constant(IndexMap index_map)
    -> checked_static_permute_index<
        __detail::is_valid_static_permute_index<Size>(__detail::invoke_index_map<Size>(index_map, Lane))
            ? static_cast<__simd_size_type>(__detail::invoke_index_map<Size>(index_map, Lane)) : 0,
        __detail::is_valid_static_permute_index<Size>(__detail::invoke_index_map<Size>(index_map, Lane))>;
#endif

template<class Result, class IndexMap>
auto static_permute_index_result(Result (*)(IndexMap)) -> type_identity<Result>;

template<__simd_size_type Size, __simd_size_type Lane, class IndexMap>
using static_permute_index_t = typename decltype(__detail::static_permute_index_result(
    &__detail::static_permute_index_constant<Size, Lane, IndexMap>))::type;

template<class IndexMap, class... Args>
concept integral_index_map_result = requires {
    typename invoke_result_t<IndexMap&, Args...>;
    requires is_integral<invoke_result_t<IndexMap&, Args...>>::value;
};

template<class IndexMap>
struct is_static_permute_index_map
    : bool_constant<
        integral_index_map_result<IndexMap, __simd_size_type> ||
        integral_index_map_result<IndexMap, __simd_size_type, __simd_size_type>> {};

template<class V, class Indices>
constexpr permute_result_t<V, Indices> permute_from_indices(const V& value, const Indices& indices) noexcept {
    permute_result_t<V, Indices> result;
    for (__simd_size_type i = 0; i < decltype(result)::size; ++i) {
        const auto index = static_cast<__simd_size_type>(indices[i]);
        assert(index >= 0);
        assert(index < static_cast<__simd_size_type>(V::size));
        __detail::copy_lane(result, i, value, index);
    }
    return result;
}

// Namespace-scope generators avoid local closure identities in templates used
// for both constant evaluation and runtime construction.
template<class V, class IndexMap>
struct permute_lane_generator {
    const V& value;

    template<class Lane>
    constexpr auto operator()(Lane) const {
        using source_index = static_permute_index_t<static_cast<__simd_size_type>(V::size),
            static_cast<__simd_size_type>(Lane::value), IndexMap>;
        return permute_lane(value, source_index::value);
    }
};

template<class V>
struct chunk_lane_generator {
    const V& value;
    __simd_size_type offset;

    template<class Lane>
    constexpr auto operator()(Lane) const {
        return lane_value(value, offset + Lane::value);
    }
};

template<class IndexMap, __simd_size_type Lane, class Result, class V>
constexpr void copy_permuted_lane(Result& result, const V& value) {
    using source_index = static_permute_index_t<static_cast<__simd_size_type>(V::size), Lane, IndexMap>;
    if constexpr (source_index::value != zero_element && source_index::value != uninit_element) {
        __detail::copy_lane(result, Lane, value, source_index::value);
    }
}

template<class V, class IndexMap, __simd_size_type... I>
constexpr resize_t<sizeof...(I), V> permute_from_map_impl(const V& value, IndexMap&, integer_sequence<__simd_size_type, I...>) {
    if constexpr (is_same_v<typename V::value_type, bool>) {
        resize_t<sizeof...(I), V> result;
        (__detail::copy_permuted_lane<IndexMap, I>(result, value), ...);
        return result;
    } else {
        return resize_t<sizeof...(I), V>(permute_lane_generator<V, IndexMap>{value});
    }
}

template<class Chunk, class V>
constexpr Chunk make_chunk(const V& value, __simd_size_type offset) {
    if constexpr (is_same_v<typename Chunk::value_type, bool>) {
        Chunk result;
        for (__simd_size_type i = 0; i < Chunk::size; ++i) {
            __detail::copy_lane(result, i, value, offset + i);
        }
        return result;
    } else {
        return Chunk(chunk_lane_generator<V>{value, offset});
    }
}

template<class Chunk, class V, size_t... I>
constexpr array<Chunk, sizeof...(I)> chunk_array_impl(const V& value, index_sequence<I...>) {
    return {{make_chunk<Chunk>(value,
        static_cast<__simd_size_type>(I * static_cast<size_t>(Chunk::size)))...}};
}

template<class Chunk, class V, size_t... I>
constexpr auto chunk_tuple_impl(const V& value, index_sequence<I...>) {
    return make_tuple(make_chunk<Chunk>(value,
        static_cast<__simd_size_type>(I * static_cast<size_t>(Chunk::size)))...);
}

template<class Tail, class V>
constexpr Tail tail_chunk(const V& value, __simd_size_type offset) {
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
           typename ranges::range_value_t<R>>>
    : integral_constant<bool,
        ranges::contiguous_range<R> &&
        ranges::sized_range<R> &&
        is_supported_value<typename ranges::range_value_t<R>>::value> {};

template<class R, class = void>
struct is_writable_load_store_range : false_type {};

template<class R>
struct is_writable_load_store_range<R,
    void_t<decltype(ranges::data(declval<R&>())),
           decltype(ranges::size(declval<R&>())),
           typename ranges::range_reference_t<R>>>
    : integral_constant<bool,
        is_contiguous_load_store_range<R>::value &&
        !is_const<typename remove_reference<typename ranges::range_reference_t<R>>::type>::value> {};

template<class R, class = void>
struct is_contiguous_mask_range : false_type {};

template<class R>
struct is_contiguous_mask_range<R,
    void_t<decltype(ranges::data(declval<R&>())),
           decltype(ranges::size(declval<R&>())),
           typename ranges::range_reference_t<R>>>
    : integral_constant<bool,
        ranges::contiguous_range<R> &&
        ranges::sized_range<R> &&
        is_constructible<bool, typename ranges::range_reference_t<R>>::value> {};

// GNU and MSVC need body-based extraction. MSVC can misclassify a
// state-dependent size in the declaration-only return type. Check the
// constant expression on a reference before deducing the result in a body.
// Neither path constructs, copies, or calls the range.
#if (defined(__GNUC__) || defined(_MSC_VER)) && !defined(__clang__)
template<class R>
concept has_constant_range_size = requires(R& range) {
    typename integral_constant<__simd_size_type, ranges::size(range)>;
};

template<class R>
    requires has_constant_range_size<R>
auto constant_range_size(R& range) {
    constexpr __simd_size_type value = ranges::size(range);
    return integral_constant<__simd_size_type, value>{};
}

template<class Result, class R>
auto constant_range_size_result(Result (*)(R&)) -> type_identity<Result>;
#else
template<class R>
auto constant_range_size(R range)
    -> integral_constant<__simd_size_type, ranges::size(range)>;

template<class Result, class R>
auto constant_range_size_result(Result (*)(R)) -> type_identity<Result>;
#endif

template<class R, class = void>
struct constant_range_size_type : integral_constant<__simd_size_type, -1> {};

template<class R>
struct constant_range_size_type<R, void_t<typename decltype(
    __detail::constant_range_size_result(
        &__detail::constant_range_size<remove_reference_t<R>>))::type>>
    : decltype(__detail::constant_range_size_result(
        &__detail::constant_range_size<remove_reference_t<R>>))::type {};

template<class R, class = void>
struct fixed_range_size : constant_range_size_type<R> {};

template<class T, size_t N>
struct fixed_range_size<T[N], void> : integral_constant<__simd_size_type, static_cast<__simd_size_type>(N)> {};

template<class T, size_t N>
struct fixed_range_size<array<T, N>, void> : integral_constant<__simd_size_type, static_cast<__simd_size_type>(N)> {};

template<class T, size_t N>
struct fixed_range_size<span<T, N>, typename enable_if<N != dynamic_extent>::type>
    : integral_constant<__simd_size_type, static_cast<__simd_size_type>(N)> {};

template<class R, __simd_size_type N>
struct has_matching_fixed_range_size
    : integral_constant<bool, fixed_range_size<remove_reference_t<R>>::value == N> {};

template<class I, class S>
constexpr __simd_size_type iterator_distance(I first, S last) noexcept {
    return static_cast<__simd_size_type>(last - first);
}

template<class R>
constexpr __simd_size_type range_size(R&& r) noexcept(noexcept(ranges::size(r))) {
    return static_cast<__simd_size_type>(ranges::size(r));
}

template<class V>
constexpr void require_unchecked_extent(__simd_size_type count) noexcept {
    assert(count >= static_cast<__simd_size_type>(V::size));
    (void)count;
}

template<class Count>
constexpr void require_nonnegative_extent(Count count) noexcept {
    assert(count >= 0);
    (void)count;
}


template<size_t I>
using generator_index_constant = integral_constant<__simd_size_type, static_cast<__simd_size_type>(I)>;

template<class G, class R, class Seq>
struct is_simd_generator_sequence;

template<class G, class R, size_t I, class = void>
struct is_simd_generator_lane : false_type {};

template<class G, class R, size_t I>
struct is_simd_generator_lane<G, R, I, void_t<decltype(declval<G&>()(generator_index_constant<I>{}))>>
    : integral_constant<bool,
        is_convertible<decltype(declval<G&>()(generator_index_constant<I>{})), R>::value &&
        (!is_arithmetic<decltype(declval<G&>()(generator_index_constant<I>{}))>::value ||
         is_value_preserving_conversion<
             decltype(declval<G&>()(generator_index_constant<I>{})), R>::value)> {};

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

} // namespace __detail

template<class V, class Indices,
         typename enable_if<__detail::is_simd_index_vector<Indices>::value, int>::type = 0>
constexpr __detail::permute_result_t<V, Indices> permute(const V& value, const Indices& indices);

template<__simd_size_type N = 0, class V, class IndexMap,
         typename enable_if<!__detail::is_simd_index_vector<__detail::remove_cvref_t<IndexMap>>::value &&
             __detail::is_static_permute_index_map<IndexMap>::value, int>::type = 0>
constexpr resize_t<(N == 0 ? static_cast<__simd_size_type>(V::size) : N), V> permute(const V& value, IndexMap&& index_map);

// Compatibility spellings retained for at least one deprecated release.
// Former class templates are aliases: naming/instantiation preserves identity,
// but specialization and template-template deduction follow alias rules.
// See the migration table for the source compatibility limitations.
using simd_size_type [[deprecated("use decltype(std::simd::vec<int>::size())")]] = __simd_size_type;
template<__simd_size_type N>
using fixed_size_abi [[deprecated("use forge::simd_fixed_size_abi<N> in <forge/simd.hpp> (backport only)")]] = __fixed_size_abi<N>;
template<class T>
using native_abi [[deprecated("use forge::simd_native_abi<T> in <forge/simd.hpp>")]] = __native_abi<T>;
template<class T, class Abi>
using simd_size [[deprecated("use std::simd::basic_vec<T, Abi>::size() for enabled vectors")]] = __simd_size<T, Abi>;
template<class Abi>
using abi_lane_count [[deprecated("use the vector or mask size() member")]] = __abi_lane_count<Abi>;
template<class T, __simd_size_type N = __simd_size<T, __native_abi<T>>::value>
using deduce_abi_t [[deprecated("use forge::simd_abi<T, N> in <forge/simd.hpp>")]] = __deduce_abi_t<T, N>;
template<class V>
using simd_iterator [[deprecated("use decltype(std::declval<V&>().begin())")]] = __simd_iterator<V>;
using convert_flag [[deprecated("use decltype(std::simd::flag_convert)")]] = __convert_flag;
using aligned_flag [[deprecated("use decltype(std::simd::flag_aligned)")]] = __aligned_flag;
template<size_t N>
using overaligned_flag [[deprecated("use decltype(std::simd::flag_overaligned<N>)")]] = __overaligned_flag<N>;
template<class T, class V, bool IsComplex = __detail::is_complex_value<__detail::remove_cvref_t<T>>::value>
using real_simd_type [[deprecated("use std::simd::rebind<typename T::value_type, V> for complex T")]] = __real_simd_type<T, V, IsComplex>;
template<class T, class V>
using real_simd_t [[deprecated("use std::simd::rebind_t<typename T::value_type, V> for complex T")]] = __real_simd_t<T, V>;
namespace __detail {
template<class T, __simd_size_type N>
using native_abi_tag [[deprecated("use forge::simd_native_abi_tag<T, N> in <forge/simd.hpp> (backport only)")]] = __native_abi_tag<T, N>;
}
// Namespace aliases cannot carry a standard deprecated attribute.
// Retained as a documentation-deprecated compatibility bridge only.
namespace detail = __detail;

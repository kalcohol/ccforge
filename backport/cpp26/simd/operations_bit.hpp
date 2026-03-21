namespace detail {

template<class T>
using bit_unsigned_t = make_unsigned_t<remove_cvref_t<T>>;

template<class V>
using bit_signed_rebind_t = rebind_t<make_signed_t<typename remove_cvref_t<V>::value_type>, remove_cvref_t<V>>;

template<class T>
constexpr bit_unsigned_t<T> to_unsigned_bits(T value) noexcept {
    return static_cast<bit_unsigned_t<T>>(value);
}

template<class T, class = void>
struct has_byteswap : false_type {};

template<class T>
struct has_byteswap<T, void_t<decltype(std::byteswap(to_unsigned_bits(T{})))>> : true_type {};

template<class T, class = void>
struct has_popcount : false_type {};

template<class T>
struct has_popcount<T, void_t<decltype(std::popcount(to_unsigned_bits(T{})))>> : true_type {};

template<class T, class = void>
struct has_countl_zero : false_type {};

template<class T>
struct has_countl_zero<T, void_t<decltype(std::countl_zero(to_unsigned_bits(T{})))>> : true_type {};

template<class T, class = void>
struct has_countl_one : false_type {};

template<class T>
struct has_countl_one<T, void_t<decltype(std::countl_one(to_unsigned_bits(T{})))>> : true_type {};

template<class T, class = void>
struct has_countr_zero : false_type {};

template<class T>
struct has_countr_zero<T, void_t<decltype(std::countr_zero(to_unsigned_bits(T{})))>> : true_type {};

template<class T, class = void>
struct has_countr_one : false_type {};

template<class T>
struct has_countr_one<T, void_t<decltype(std::countr_one(to_unsigned_bits(T{})))>> : true_type {};

template<class T, class = void>
struct has_bit_width : false_type {};

template<class T>
struct has_bit_width<T, void_t<decltype(std::bit_width(to_unsigned_bits(T{})))>> : true_type {};

template<class T, class = void>
struct has_has_single_bit : false_type {};

template<class T>
struct has_has_single_bit<T, void_t<decltype(std::has_single_bit(to_unsigned_bits(T{})))>> : true_type {};

template<class T, class = void>
struct has_bit_floor : false_type {};

template<class T>
struct has_bit_floor<T, void_t<decltype(std::bit_floor(to_unsigned_bits(T{})))>> : true_type {};

template<class T, class = void>
struct has_bit_ceil : false_type {};

template<class T>
struct has_bit_ceil<T, void_t<decltype(std::bit_ceil(to_unsigned_bits(T{})))>> : true_type {};

template<class T, class = void>
struct has_rotl : false_type {};

template<class T>
struct has_rotl<T, void_t<decltype(std::rotl(to_unsigned_bits(T{}), int{}))>> : true_type {};

template<class T, class = void>
struct has_rotr : false_type {};

template<class T>
struct has_rotr<T, void_t<decltype(std::rotr(to_unsigned_bits(T{}), int{}))>> : true_type {};

} // namespace detail

template<class T, class Abi>
constexpr basic_vec<T, Abi> byteswap(const basic_vec<T, Abi>& value) noexcept
    requires(is_integral<T>::value && detail::has_byteswap<T>::value) {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(result, i, static_cast<T>(std::byteswap(detail::to_unsigned_bits(value[i]))));
    }
    return result;
}

template<class T, class Abi>
constexpr detail::bit_signed_rebind_t<basic_vec<T, Abi>> popcount(const basic_vec<T, Abi>& value) noexcept
    requires(is_unsigned<T>::value && detail::has_popcount<T>::value) {
    using result_type = detail::bit_signed_rebind_t<basic_vec<T, Abi>>;
    result_type result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(result, i, static_cast<typename result_type::value_type>(std::popcount(detail::to_unsigned_bits(value[i]))));
    }
    return result;
}

template<class T, class Abi>
constexpr detail::bit_signed_rebind_t<basic_vec<T, Abi>> countl_zero(const basic_vec<T, Abi>& value) noexcept
    requires(is_unsigned<T>::value && detail::has_countl_zero<T>::value) {
    using result_type = detail::bit_signed_rebind_t<basic_vec<T, Abi>>;
    result_type result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(result, i, static_cast<typename result_type::value_type>(std::countl_zero(detail::to_unsigned_bits(value[i]))));
    }
    return result;
}

template<class T, class Abi>
constexpr detail::bit_signed_rebind_t<basic_vec<T, Abi>> countl_one(const basic_vec<T, Abi>& value) noexcept
    requires(is_unsigned<T>::value && detail::has_countl_one<T>::value) {
    using result_type = detail::bit_signed_rebind_t<basic_vec<T, Abi>>;
    result_type result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(result, i, static_cast<typename result_type::value_type>(std::countl_one(detail::to_unsigned_bits(value[i]))));
    }
    return result;
}

template<class T, class Abi>
constexpr detail::bit_signed_rebind_t<basic_vec<T, Abi>> countr_zero(const basic_vec<T, Abi>& value) noexcept
    requires(is_unsigned<T>::value && detail::has_countr_zero<T>::value) {
    using result_type = detail::bit_signed_rebind_t<basic_vec<T, Abi>>;
    result_type result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(result, i, static_cast<typename result_type::value_type>(std::countr_zero(detail::to_unsigned_bits(value[i]))));
    }
    return result;
}

template<class T, class Abi>
constexpr detail::bit_signed_rebind_t<basic_vec<T, Abi>> countr_one(const basic_vec<T, Abi>& value) noexcept
    requires(is_unsigned<T>::value && detail::has_countr_one<T>::value) {
    using result_type = detail::bit_signed_rebind_t<basic_vec<T, Abi>>;
    result_type result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(result, i, static_cast<typename result_type::value_type>(std::countr_one(detail::to_unsigned_bits(value[i]))));
    }
    return result;
}

template<class T, class Abi>
constexpr detail::bit_signed_rebind_t<basic_vec<T, Abi>> bit_width(const basic_vec<T, Abi>& value) noexcept
    requires(is_unsigned<T>::value && detail::has_bit_width<T>::value) {
    using result_type = detail::bit_signed_rebind_t<basic_vec<T, Abi>>;
    result_type result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(result, i, static_cast<typename result_type::value_type>(std::bit_width(detail::to_unsigned_bits(value[i]))));
    }
    return result;
}

template<class T, class Abi>
constexpr typename basic_vec<T, Abi>::mask_type has_single_bit(const basic_vec<T, Abi>& value) noexcept
    requires(is_unsigned<T>::value && detail::has_has_single_bit<T>::value) {
    typename basic_vec<T, Abi>::mask_type result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(result, i, std::has_single_bit(detail::to_unsigned_bits(value[i])));
    }
    return result;
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> bit_floor(const basic_vec<T, Abi>& value) noexcept
    requires(is_unsigned<T>::value && detail::has_bit_floor<T>::value) {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(result, i, static_cast<T>(std::bit_floor(detail::to_unsigned_bits(value[i]))));
    }
    return result;
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> bit_ceil(const basic_vec<T, Abi>& value)
    requires(is_unsigned<T>::value && detail::has_bit_ceil<T>::value) {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(result, i, static_cast<T>(std::bit_ceil(detail::to_unsigned_bits(value[i]))));
    }
    return result;
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> rotl(const basic_vec<T, Abi>& value, int shift) noexcept
    requires(is_unsigned<T>::value && detail::has_rotl<T>::value) {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(result, i, static_cast<T>(std::rotl(detail::to_unsigned_bits(value[i]), shift)));
    }
    return result;
}

template<class T, class Abi, class Shift, class ShiftAbi>
constexpr basic_vec<T, Abi> rotl(const basic_vec<T, Abi>& value, const basic_vec<Shift, ShiftAbi>& shift) noexcept
    requires(
        is_unsigned<T>::value &&
        is_integral<Shift>::value &&
        sizeof(T) == sizeof(Shift) &&
        basic_vec<T, Abi>::size == basic_vec<Shift, ShiftAbi>::size &&
        detail::has_rotl<T>::value) {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(result, i, static_cast<T>(std::rotl(detail::to_unsigned_bits(value[i]), static_cast<int>(shift[i]))));
    }
    return result;
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> rotr(const basic_vec<T, Abi>& value, int shift) noexcept
    requires(is_unsigned<T>::value && detail::has_rotr<T>::value) {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(result, i, static_cast<T>(std::rotr(detail::to_unsigned_bits(value[i]), shift)));
    }
    return result;
}

template<class T, class Abi, class Shift, class ShiftAbi>
constexpr basic_vec<T, Abi> rotr(const basic_vec<T, Abi>& value, const basic_vec<Shift, ShiftAbi>& shift) noexcept
    requires(
        is_unsigned<T>::value &&
        is_integral<Shift>::value &&
        sizeof(T) == sizeof(Shift) &&
        basic_vec<T, Abi>::size == basic_vec<Shift, ShiftAbi>::size &&
        detail::has_rotr<T>::value) {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(result, i, static_cast<T>(std::rotr(detail::to_unsigned_bits(value[i]), static_cast<int>(shift[i]))));
    }
    return result;
}

#pragma once

namespace detail {

template<class T>
using bit_unsigned_t = make_unsigned_t<remove_cvref_t<T>>;

template<class V>
using bit_signed_rebind_t = rebind_t<make_signed_t<typename remove_cvref_t<V>::value_type>, remove_cvref_t<V>>;

template<class T>
constexpr bit_unsigned_t<T> to_unsigned_bits(T value) noexcept {
    return static_cast<bit_unsigned_t<T>>(value);
}

template<class T>
constexpr T bit_reverse_scalar(T value) noexcept {
    constexpr unsigned digits =
        static_cast<unsigned>(numeric_limits<T>::digits);
    T result{};
    for (unsigned source = 0; source < digits; ++source) {
        result |= static_cast<T>(
            ((value >> source) & T{1}) << (digits - source - 1u));
    }
    return result;
}

template<class S>
constexpr auto shift_magnitude(S shift) noexcept -> make_unsigned_t<S> {
    using U = make_unsigned_t<S>;
    if constexpr (is_signed<S>::value) {
        if (shift < S{}) {
            U magnitude = static_cast<U>(-(shift + S{1}));
            return static_cast<U>(magnitude + U{1});
        }
    }
    return static_cast<U>(shift);
}

template<class T, class S>
constexpr T shift_left_scalar(T value, S shift) noexcept {
    using U = make_unsigned_t<T>;
    constexpr unsigned digits =
        static_cast<unsigned>(numeric_limits<U>::digits);
    const auto magnitude = shift_magnitude(shift);
    if (magnitude >= static_cast<decltype(magnitude)>(digits)) {
        if constexpr (is_signed<S>::value) {
            if (shift < S{}) {
                if constexpr (is_signed<T>::value) {
                    return value < T{} ? T{-1} : T{};
                }
                return T{};
            }
        }
        return T{};
    }

    const auto amount = static_cast<unsigned>(magnitude);
    if constexpr (is_signed<S>::value) {
        if (shift < S{}) {
            return static_cast<T>(value >> amount);
        }
    }
    return static_cast<T>(static_cast<U>(value) << amount);
}

template<class T, class S>
constexpr T shift_right_scalar(T value, S shift) noexcept {
    using U = make_unsigned_t<T>;
    constexpr unsigned digits =
        static_cast<unsigned>(numeric_limits<U>::digits);
    const auto magnitude = shift_magnitude(shift);
    if (magnitude >= static_cast<decltype(magnitude)>(digits)) {
        if constexpr (is_signed<S>::value) {
            if (shift < S{}) {
                return T{};
            }
        }
        if constexpr (is_signed<T>::value) {
            return value < T{} ? T{-1} : T{};
        }
        return T{};
    }

    const auto amount = static_cast<unsigned>(magnitude);
    if constexpr (is_signed<S>::value) {
        if (shift < S{}) {
            return static_cast<T>(static_cast<U>(value) << amount);
        }
    }
    return static_cast<T>(value >> amount);
}

template<class T>
constexpr T bit_repeat_scalar(T value, int length) {
    if (length <= 0) {
        if consteval {
            throw "std::simd::bit_repeat requires a positive repeat length";
        } else {
            assert(length > 0);
            return T{};
        }
    }

    constexpr unsigned digits =
        static_cast<unsigned>(numeric_limits<T>::digits);
    const auto pattern_length = static_cast<unsigned>(length);
    T result{};
    for (unsigned destination = 0; destination < digits; ++destination) {
        const unsigned source = destination % pattern_length;
        result |= static_cast<T>(
            ((value >> source) & T{1}) << destination);
    }
    return result;
}

template<class T>
constexpr T bit_compress_scalar(T value, T mask) noexcept {
    constexpr unsigned digits =
        static_cast<unsigned>(numeric_limits<T>::digits);
    T result{};
    unsigned destination = 0;
    for (unsigned source = 0; source < digits; ++source) {
        if (((mask >> source) & T{1}) != T{}) {
            result |= static_cast<T>(
                ((value >> source) & T{1}) << destination);
            ++destination;
        }
    }
    return result;
}

template<class T>
constexpr T bit_expand_scalar(T value, T mask) noexcept {
    constexpr unsigned digits =
        static_cast<unsigned>(numeric_limits<T>::digits);
    T result{};
    unsigned source = 0;
    for (unsigned destination = 0; destination < digits; ++destination) {
        if (((mask >> destination) & T{1}) != T{}) {
            result |= static_cast<T>(
                ((value >> source) & T{1}) << destination);
            ++source;
        }
    }
    return result;
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
constexpr basic_vec<T, Abi> bit_reverse(
    const basic_vec<T, Abi>& value) noexcept
    requires(is_unsigned<T>::value) {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(
            result,
            i,
            detail::bit_reverse_scalar(value[i]));
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

template<class T, class Abi, class Shift, class ShiftAbi>
constexpr basic_vec<T, Abi> shl(
    const basic_vec<T, Abi>& value,
    const basic_vec<Shift, ShiftAbi>& shift) noexcept
    requires(
        detail::is_vectorizable_integral<T>::value &&
        detail::is_vectorizable_integral<Shift>::value &&
        sizeof(T) == sizeof(Shift) &&
        basic_vec<T, Abi>::size == basic_vec<Shift, ShiftAbi>::size) {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(
            result,
            i,
            detail::shift_left_scalar(value[i], shift[i]));
    }
    return result;
}

template<class T, class Abi, class Shift>
constexpr basic_vec<T, Abi> shl(
    const basic_vec<T, Abi>& value,
    Shift shift) noexcept
    requires(
        detail::is_vectorizable_integral<T>::value &&
        detail::is_vectorizable_integral<Shift>::value) {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(
            result,
            i,
            detail::shift_left_scalar(value[i], shift));
    }
    return result;
}

template<class T, class Abi, class Shift, class ShiftAbi>
constexpr basic_vec<T, Abi> shr(
    const basic_vec<T, Abi>& value,
    const basic_vec<Shift, ShiftAbi>& shift) noexcept
    requires(
        detail::is_vectorizable_integral<T>::value &&
        detail::is_vectorizable_integral<Shift>::value &&
        sizeof(T) == sizeof(Shift) &&
        basic_vec<T, Abi>::size == basic_vec<Shift, ShiftAbi>::size) {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(
            result,
            i,
            detail::shift_right_scalar(value[i], shift[i]));
    }
    return result;
}

template<class T, class Abi, class Shift>
constexpr basic_vec<T, Abi> shr(
    const basic_vec<T, Abi>& value,
    Shift shift) noexcept
    requires(
        detail::is_vectorizable_integral<T>::value &&
        detail::is_vectorizable_integral<Shift>::value) {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(
            result,
            i,
            detail::shift_right_scalar(value[i], shift));
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

template<class T, class Abi, class Length, class LengthAbi>
constexpr basic_vec<T, Abi> bit_repeat(
    const basic_vec<T, Abi>& value,
    const basic_vec<Length, LengthAbi>& length)
    requires(
        is_unsigned<T>::value &&
        detail::is_vectorizable_integral<Length>::value &&
        sizeof(T) == sizeof(Length) &&
        basic_vec<T, Abi>::size == basic_vec<Length, LengthAbi>::size) {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(
            result,
            i,
            detail::bit_repeat_scalar(
                value[i],
                static_cast<int>(length[i])));
    }
    return result;
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> bit_repeat(
    const basic_vec<T, Abi>& value,
    int length)
    requires(is_unsigned<T>::value) {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(
            result,
            i,
            detail::bit_repeat_scalar(value[i], length));
    }
    return result;
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> bit_compress(
    const basic_vec<T, Abi>& value,
    const basic_vec<T, Abi>& mask) noexcept
    requires(is_unsigned<T>::value) {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(
            result,
            i,
            detail::bit_compress_scalar(value[i], mask[i]));
    }
    return result;
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> bit_compress(
    const basic_vec<T, Abi>& value,
    type_identity_t<T> mask) noexcept
    requires(is_unsigned<T>::value) {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(
            result,
            i,
            detail::bit_compress_scalar(value[i], mask));
    }
    return result;
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> bit_expand(
    const basic_vec<T, Abi>& value,
    const basic_vec<T, Abi>& mask) noexcept
    requires(is_unsigned<T>::value) {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(
            result,
            i,
            detail::bit_expand_scalar(value[i], mask[i]));
    }
    return result;
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> bit_expand(
    const basic_vec<T, Abi>& value,
    type_identity_t<T> mask) noexcept
    requires(is_unsigned<T>::value) {
    basic_vec<T, Abi> result;
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        detail::set_lane(
            result,
            i,
            detail::bit_expand_scalar(value[i], mask));
    }
    return result;
}

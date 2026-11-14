#pragma once

template<class T,
         class Abi,
         class BinaryOperation = plus<>,
         typename enable_if<
             !is_same<
                 __detail::remove_cvref_t<BinaryOperation>,
                 typename basic_vec<T, Abi>::mask_type>::value &&
             __detail::is_reduction_binary_operation<T, BinaryOperation>::value,
             int>::type = 0>
constexpr T reduce(const basic_vec<T, Abi>& value, BinaryOperation binary_op = {}) noexcept(
    noexcept(std::declval<BinaryOperation&>()(
        std::declval<vec<T, 1>&>(),
        std::declval<vec<T, 1>>()))) {
    vec<T, 1> result(value[0]);
    for (__simd_size_type i = 1; i < basic_vec<T, Abi>::size; ++i) {
        result = binary_op(result, vec<T, 1>(value[i]));
    }
    return result[0];
}

template<class T,
         class Abi,
         class BinaryOperation = plus<>,
         typename enable_if<
             __detail::is_reduction_binary_operation<T, BinaryOperation>::value,
             int>::type = 0>
constexpr T reduce(const basic_vec<T, Abi>& value,
                   const typename basic_vec<T, Abi>::mask_type& mask_value,
                   BinaryOperation binary_op = {},
                   typename type_identity<T>::type identity_element =
                       __detail::reduction_identity<T, BinaryOperation>::value()) noexcept(
    noexcept(std::declval<BinaryOperation&>()(
        std::declval<vec<T, 1>&>(),
        std::declval<vec<T, 1>>()))) {
    __simd_size_type first = 0;
    while (first < basic_vec<T, Abi>::size && !__detail::mask_lane(mask_value, first)) {
        ++first;
    }

    if (first == basic_vec<T, Abi>::size) {
        return identity_element;
    }

    vec<T, 1> result(value[first]);
    for (__simd_size_type i = first + 1; i < basic_vec<T, Abi>::size; ++i) {
        if (__detail::mask_lane(mask_value, i)) {
            result = binary_op(result, vec<T, 1>(value[i]));
        }
    }
    return result[0];
}

template<class T, class Abi>
    requires totally_ordered<T>
constexpr T reduce_min(const basic_vec<T, Abi>& value) noexcept {
    T result = value[0];
    for (__simd_size_type i = 1; i < basic_vec<T, Abi>::size; ++i) {
        if (value[i] < result) {
            result = value[i];
        }
    }
    return result;
}

template<class T, class Abi>
    requires totally_ordered<T>
constexpr T reduce_min(
    const basic_vec<T, Abi>& value,
    const typename basic_vec<T, Abi>::mask_type& mask_value) noexcept {
    __simd_size_type first = 0;
    while (first < basic_vec<T, Abi>::size && !__detail::mask_lane(mask_value, first)) {
        ++first;
    }

    if (first == basic_vec<T, Abi>::size) {
        return numeric_limits<T>::max();
    }

    T result = value[first];
    for (__simd_size_type i = first + 1; i < basic_vec<T, Abi>::size; ++i) {
        if (__detail::mask_lane(mask_value, i) && value[i] < result) {
            result = value[i];
        }
    }
    return result;
}

template<class T, class Abi>
    requires totally_ordered<T>
constexpr T reduce_max(const basic_vec<T, Abi>& value) noexcept {
    T result = value[0];
    for (__simd_size_type i = 1; i < basic_vec<T, Abi>::size; ++i) {
        if (value[i] > result) {
            result = value[i];
        }
    }
    return result;
}

template<class T, class Abi>
    requires totally_ordered<T>
constexpr T reduce_max(
    const basic_vec<T, Abi>& value,
    const typename basic_vec<T, Abi>::mask_type& mask_value) noexcept {
    __simd_size_type first = 0;
    while (first < basic_vec<T, Abi>::size && !__detail::mask_lane(mask_value, first)) {
        ++first;
    }

    if (first == basic_vec<T, Abi>::size) {
        return numeric_limits<T>::lowest();
    }

    T result = value[first];
    for (__simd_size_type i = first + 1; i < basic_vec<T, Abi>::size; ++i) {
        if (__detail::mask_lane(mask_value, i) && value[i] > result) {
            result = value[i];
        }
    }
    return result;
}

template<class T,
         class BinaryOperation = plus<>,
         typename enable_if<
             __detail::is_supported_value<T>::value &&
                 __detail::is_reduction_binary_operation<T, BinaryOperation>::value,
             int>::type = 0>
constexpr T reduce(const T& value, BinaryOperation = {}) {
    return value;
}

template<class T,
         class Mask,
         class BinaryOperation = plus<>,
         typename enable_if<
             __detail::is_supported_value<T>::value &&
                 is_same<__detail::remove_cvref_t<Mask>, bool>::value &&
                 __detail::is_reduction_binary_operation<T, BinaryOperation>::value,
             int>::type = 0>
constexpr T reduce(
    const T& value,
    Mask mask,
    BinaryOperation = {},
    type_identity_t<T> identity_element =
        __detail::reduction_identity<T, BinaryOperation>::value()) {
    return mask ? value : identity_element;
}

template<class T>
    requires(__detail::is_supported_scalar_value<T>::value && totally_ordered<T>)
constexpr T reduce_min(const T& value) noexcept {
    return value;
}

template<class T, class Mask>
    requires(
        __detail::is_supported_scalar_value<T>::value &&
        totally_ordered<T> &&
        same_as<Mask, bool>)
constexpr T reduce_min(const T& value, Mask mask) noexcept {
    return mask ? value : numeric_limits<T>::max();
}

template<class T>
    requires(__detail::is_supported_scalar_value<T>::value && totally_ordered<T>)
constexpr T reduce_max(const T& value) noexcept {
    return value;
}

template<class T, class Mask>
    requires(
        __detail::is_supported_scalar_value<T>::value &&
        totally_ordered<T> &&
        same_as<Mask, bool>)
constexpr T reduce_max(const T& value, Mask mask) noexcept {
    return mask ? value : numeric_limits<T>::lowest();
}

namespace __detail {

template<bool All, size_t Bytes, class Abi>
constexpr bool mask_test(const basic_mask<Bytes, Abi>& value) noexcept {
    // Both early-decided and full-scan inputs were measured: GCC keeps a
    // predictable early exit, the same algorithm as the bool representation.
    // Other compilers get a branch-free reduction mapping to compare/pack/
    // movemask sequences or AND/OR trees.
#if defined(__GNUC__) && !defined(__clang__)
    for (__simd_size_type i = 0; i < basic_mask<Bytes, Abi>::size; ++i) {
        if ((mask_lane(value, i) != 0) != All) return !All;
    }
    return All;
#else
    if constexpr (All) {
        auto acc = static_cast<mask_lane_t<Bytes>>(~mask_lane_t<Bytes>{0});
        for (__simd_size_type i = 0; i < basic_mask<Bytes, Abi>::size; ++i) acc &= mask_lane(value, i);
        return acc != 0;
    } else {
        mask_lane_t<Bytes> acc{0};
        for (__simd_size_type i = 0; i < basic_mask<Bytes, Abi>::size; ++i) acc |= mask_lane(value, i);
        return acc != 0;
    }
#endif
}

} // namespace __detail

template<size_t Bytes, class Abi>
constexpr bool all_of(const basic_mask<Bytes, Abi>& value) noexcept {
    return __detail::mask_test<true>(value);
}

template<size_t Bytes, class Abi>
constexpr bool any_of(const basic_mask<Bytes, Abi>& value) noexcept {
    return __detail::mask_test<false>(value);
}

template<size_t Bytes, class Abi>
constexpr bool none_of(const basic_mask<Bytes, Abi>& value) noexcept {
    return !std::simd::any_of(value);
}

template<size_t Bytes, class Abi>
constexpr __simd_size_type reduce_count(const basic_mask<Bytes, Abi>& value) noexcept {
    // Measured lane summation avoids the byte-view cost at every width and
    // works unchanged in constant evaluation.
    __simd_size_type result = 0;
    for (__simd_size_type i = 0; i < basic_mask<Bytes, Abi>::size; ++i) {
        result += static_cast<__simd_size_type>(__detail::mask_lane(value, i) & 1u);
    }
    return result;
}

template<size_t Bytes, class Abi>
constexpr __simd_size_type reduce_min_index(const basic_mask<Bytes, Abi>& value) noexcept {
    for (__simd_size_type i = 0; i < basic_mask<Bytes, Abi>::size; ++i) {
        if (__detail::mask_lane(value, i)) {
            return i;
        }
    }
    return 0;
}

template<size_t Bytes, class Abi>
constexpr __simd_size_type reduce_max_index(const basic_mask<Bytes, Abi>& value) noexcept {
    for (__simd_size_type i = basic_mask<Bytes, Abi>::size; i > 0; --i) {
        if (__detail::mask_lane(value, i - 1)) {
            return i - 1;
        }
    }
    return 0;
}

constexpr bool all_of(same_as<bool> auto value) noexcept {
    return value;
}

constexpr bool any_of(same_as<bool> auto value) noexcept {
    return value;
}

constexpr bool none_of(same_as<bool> auto value) noexcept {
    return !value;
}

constexpr __simd_size_type reduce_count(same_as<bool> auto value) noexcept {
    return value ? 1 : 0;
}

constexpr __simd_size_type reduce_min_index(same_as<bool> auto) noexcept {
    return 0;
}

constexpr __simd_size_type reduce_max_index(same_as<bool> auto) noexcept {
    return 0;
}

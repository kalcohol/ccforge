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

namespace __detail {

// Integer plus<> wraps in the same-width unsigned mask lane type. Adding
// every lane ANDed with its mask lane loads all lanes; it was measured
// faster than the predictable branches from three lanes on Clang and for
// complete 128-bit blocks on GCC, which keeps other widths scalar.
// Other compilers keep the conditional loop.
template<class T, class BinaryOperation, __simd_size_type Lanes>
inline constexpr bool is_masked_integer_plus =
    is_vectorizable_integral<T>::value &&
    is_same_v<remove_cvref_t<BinaryOperation>, plus<>> &&
    sizeof(mask_lane_t<sizeof(T)>) == sizeof(T) &&
#if defined(__clang__)
    Lanes >= 3;
#elif defined(__GNUC__)
    Lanes % mask_blend_block<sizeof(T)> == 0;
#else
    false;
#endif

// Clang visits the later lanes of a masked reduce at constant indices once the
// loop is unrolled. The loop that starts after the first lane reads the mask at
// a run-time index: Clang 19 on x86-64 then kept a stack copy of a fused
// comparison's mask (scalarizing the float x4 compare), and stored masks were
// slower as well at the tested widths (3 to 16 lanes). GCC 16.2 measured slower
// stored-mask kernels with the constant-index form; GCC and other compilers
// keep the loop that starts after the first lane.
inline constexpr bool masked_reduce_constant_index =
#if defined(__clang__)
    true;
#else
    false;
#endif

} // namespace __detail

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
    if constexpr (__detail::is_masked_integer_plus<T, BinaryOperation, basic_vec<T, Abi>::size>) {
        // Clang vectorized the conditional loop below with masked loads and
        // GCC with spilled mask lanes and a scalar tail; the sum of every lane
        // ANDed with its mask lane maps to AND/ADD trees instead. Unsigned
        // lanes add no signed overflow.
        using lane_type = __detail::mask_lane_t<sizeof(T)>;
        lane_type sum{0};
        lane_type any{0};
        for (__simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
            const lane_type bits = __detail::mask_lane(mask_value, i);
            sum = static_cast<lane_type>(sum + (static_cast<lane_type>(value[i]) & bits));
            any = static_cast<lane_type>(any | bits);
        }
        // An empty mask leaves sum zero and keeps identity_element; this
        // term folds away for the default identity.
        return static_cast<T>(static_cast<lane_type>(
            sum + (static_cast<lane_type>(identity_element) & static_cast<lane_type>(~any))));
    }
    __simd_size_type first = 0;
    while (first < basic_vec<T, Abi>::size && !__detail::mask_lane(mask_value, first)) {
        ++first;
    }

    if (first == basic_vec<T, Abi>::size) {
        return identity_element;
    }

    vec<T, 1> result(value[first]);
    if constexpr (__detail::masked_reduce_constant_index) {
        // Same lanes and order as below: every selected lane after the first.
        for (__simd_size_type i = 1; i < basic_vec<T, Abi>::size; ++i) {
            if (i > first && __detail::mask_lane(mask_value, i)) {
                result = binary_op(result, vec<T, 1>(value[i]));
            }
        }
    } else {
        for (__simd_size_type i = first + 1; i < basic_vec<T, Abi>::size; ++i) {
            if (__detail::mask_lane(mask_value, i)) {
                result = binary_op(result, vec<T, 1>(value[i]));
            }
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
    // Every loop works unchanged in constant evaluation.
    using lane_type = __detail::mask_lane_t<Bytes>;
    constexpr __simd_size_type size = basic_mask<Bytes, Abi>::size;
#if defined(__clang__) && defined(__AVX2__) && defined(__POPCNT__)
    // One 16/32-byte compare with all ones, movemask and popcnt: measured
    // faster than the add tree for Clang AVX2 at these widths only.
    constexpr bool compare_count = (sizeof(lane_type) == 4 && (size == 4 || size == 8)) ||
                                   (sizeof(lane_type) == 8 && size == 4);
#else
    constexpr bool compare_count = false;
#endif
#if defined(__clang__)
    constexpr __simd_size_type wrap_min = 2;
#else
    // GCC kept the low-bit sum scalar and faster below four lanes.
    constexpr __simd_size_type wrap_min = 4;
#endif
    constexpr bool wrap_sum = size >= wrap_min &&
        static_cast<unsigned long long>(size) <=
            static_cast<unsigned long long>(numeric_limits<lane_type>::max());
    if constexpr (compare_count) {
        __simd_size_type result = 0;
        for (__simd_size_type i = 0; i < size; ++i) {
            result += static_cast<__simd_size_type>(
                __detail::mask_lane(value, i) == static_cast<lane_type>(~lane_type{0}));
        }
        return result;
    } else if constexpr (wrap_sum) {
        // A set lane is all ones, -1 modulo the lane width, so the wrap-around
        // sum in the lane type is the count. It stays one add tree in the lane
        // width; the low-bit sum was split into vector and scalar parts
        // (Clang) or widened to 64 bits (GCC).
        lane_type acc{0};
        for (__simd_size_type i = 0; i < size; ++i) {
            acc = static_cast<lane_type>(acc - __detail::mask_lane(value, i));
        }
        return static_cast<__simd_size_type>(acc);
    } else {
        __simd_size_type result = 0;
        for (__simd_size_type i = 0; i < size; ++i) {
            result += static_cast<__simd_size_type>(__detail::mask_lane(value, i) & 1u);
        }
        return result;
    }
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
    if constexpr (basic_mask<Bytes, Abi>::size > 2) {
        for (__simd_size_type i = basic_mask<Bytes, Abi>::size - 1; i > 1; --i) {
            if (__detail::mask_lane(value, i)) {
                return i;
            }
        }
        // With no higher lane set the result is 1 or 0: the low bit of lane 1.
        // Both compilers emitted a compare and setcc for this last test.
        return static_cast<__simd_size_type>(__detail::mask_lane(value, 1) & 1u);
    } else {
        // Two-lane masks keep the plain loop: with the low-bit form there, other
        // two-lane kernels built by GCC measured slower in the benchmark.
        for (__simd_size_type i = basic_mask<Bytes, Abi>::size; i > 0; --i) {
            if (__detail::mask_lane(value, i - 1)) {
                return i - 1;
            }
        }
        return 0;
    }
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

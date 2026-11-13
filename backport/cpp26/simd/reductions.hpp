#pragma once

namespace detail {

// A balanced tree exposes independent subexpressions even without FP
// reassociation flags. No padding lanes or identity operands are evaluated.
template<simd_size_type First, simd_size_type Count, class Get, class Op>
constexpr auto reduction_tree(Get& get, Op& op) {
    if constexpr (Count == 1) {
        return get(First);
    } else {
        auto left = detail::reduction_tree<First, Count / 2>(get, op);
        auto right = detail::reduction_tree<First + Count / 2, Count - Count / 2>(get, op);
        return op(left, std::move(right));
    }
}

template<class T, class Abi, class Op>
constexpr T masked_reduction_tree(const basic_vec<T, Abi>& value,
    const typename basic_vec<T, Abi>::mask_type& mask, Op op, T identity) {
    array<T, basic_vec<T, Abi>::size()> lanes{};
    simd_size_type count = 0;
    for (simd_size_type i = 0; i < value.size(); ++i) {
        if (mask[i]) lanes[count++] = value[i];
    }
    if (count == 0) return identity;
    // Carry odd tails forward; never inject an identity into a nonempty sum.
    while (count > 1) {
        simd_size_type out = 0;
        simd_size_type i = 0;
        for (; i + 1 < count; i += 2) lanes[out++] = op(lanes[i], lanes[i + 1]);
        if (i < count) lanes[out++] = lanes[i];
        count = out;
    }
    return lanes[0];
}

// Ignore later unordered values just as the former left fold did. Keeping
// the first selected NaN requires a separate check at the public entry point.
struct reduction_minimum {
    template<class T> constexpr T operator()(T a, T b) const {
        return a != a || b < a ? b : a;
    }
};
struct reduction_maximum {
    template<class T> constexpr T operator()(T a, T b) const {
        return a != a || a < b ? b : a;
    }
};

} // namespace detail

template<class T,
         class Abi,
         class BinaryOperation = plus<>,
         typename enable_if<
             !is_same<
                 detail::remove_cvref_t<BinaryOperation>,
                 typename basic_vec<T, Abi>::mask_type>::value &&
             detail::is_reduction_binary_operation<T, BinaryOperation>::value,
             int>::type = 0>
constexpr T reduce(const basic_vec<T, Abi>& value, BinaryOperation binary_op = {}) noexcept(
    noexcept(std::declval<BinaryOperation&>()(
        std::declval<vec<T, 1>&>(),
        std::declval<vec<T, 1>>()))) {
    auto get = [&](simd_size_type i) { return vec<T, 1>(value[i]); };
    return detail::reduction_tree<0, basic_vec<T, Abi>::size()>(get, binary_op)[0];
}

template<class T,
         class Abi,
         class BinaryOperation = plus<>,
         typename enable_if<
             detail::is_reduction_binary_operation<T, BinaryOperation>::value,
             int>::type = 0>
constexpr T reduce(const basic_vec<T, Abi>& value,
                   const typename basic_vec<T, Abi>::mask_type& mask_value,
                   BinaryOperation binary_op = {},
                   typename type_identity<T>::type identity_element =
                       detail::reduction_identity<T, BinaryOperation>::value()) noexcept(
    noexcept(std::declval<BinaryOperation&>()(
        std::declval<vec<T, 1>&>(),
        std::declval<vec<T, 1>>()))) {
    auto combine = [&](T a, T b) {
        vec<T, 1> left(a);
        return binary_op(left, vec<T, 1>(b))[0];
    };
    return detail::masked_reduction_tree(value, mask_value, combine, identity_element);
}

template<class T, class Abi>
    requires totally_ordered<T>
constexpr T reduce_min(const basic_vec<T, Abi>& value) noexcept {
    if (value[0] != value[0]) return value[0];
    auto get = [&](simd_size_type i) { return T(value[i]); };
    detail::reduction_minimum op;
    return detail::reduction_tree<0, basic_vec<T, Abi>::size()>(get, op);
}

template<class T, class Abi>
    requires totally_ordered<T>
constexpr T reduce_min(
    const basic_vec<T, Abi>& value,
    const typename basic_vec<T, Abi>::mask_type& mask_value) noexcept {
    for (simd_size_type i = 0; i < value.size(); ++i) {
        if (mask_value[i]) {
            if (value[i] != value[i]) return value[i];
            break;
        }
    }
    return detail::masked_reduction_tree(value, mask_value,
        detail::reduction_minimum{}, numeric_limits<T>::max());
}

template<class T, class Abi>
    requires totally_ordered<T>
constexpr T reduce_max(const basic_vec<T, Abi>& value) noexcept {
    if (value[0] != value[0]) return value[0];
    auto get = [&](simd_size_type i) { return T(value[i]); };
    detail::reduction_maximum op;
    return detail::reduction_tree<0, basic_vec<T, Abi>::size()>(get, op);
}

template<class T, class Abi>
    requires totally_ordered<T>
constexpr T reduce_max(
    const basic_vec<T, Abi>& value,
    const typename basic_vec<T, Abi>::mask_type& mask_value) noexcept {
    for (simd_size_type i = 0; i < value.size(); ++i) {
        if (mask_value[i]) {
            if (value[i] != value[i]) return value[i];
            break;
        }
    }
    return detail::masked_reduction_tree(value, mask_value,
        detail::reduction_maximum{}, numeric_limits<T>::lowest());
}

template<class T,
         class BinaryOperation = plus<>,
         typename enable_if<
             detail::is_supported_value<T>::value &&
                 detail::is_reduction_binary_operation<T, BinaryOperation>::value,
             int>::type = 0>
constexpr T reduce(const T& value, BinaryOperation = {}) {
    return value;
}

template<class T,
         class Mask,
         class BinaryOperation = plus<>,
         typename enable_if<
             detail::is_supported_value<T>::value &&
                 is_same<detail::remove_cvref_t<Mask>, bool>::value &&
                 detail::is_reduction_binary_operation<T, BinaryOperation>::value,
             int>::type = 0>
constexpr T reduce(
    const T& value,
    Mask mask,
    BinaryOperation = {},
    type_identity_t<T> identity_element =
        detail::reduction_identity<T, BinaryOperation>::value()) {
    return mask ? value : identity_element;
}

template<class T>
    requires(detail::is_supported_scalar_value<T>::value && totally_ordered<T>)
constexpr T reduce_min(const T& value) noexcept {
    return value;
}

template<class T, class Mask>
    requires(
        detail::is_supported_scalar_value<T>::value &&
        totally_ordered<T> &&
        same_as<Mask, bool>)
constexpr T reduce_min(const T& value, Mask mask) noexcept {
    return mask ? value : numeric_limits<T>::max();
}

template<class T>
    requires(detail::is_supported_scalar_value<T>::value && totally_ordered<T>)
constexpr T reduce_max(const T& value) noexcept {
    return value;
}

template<class T, class Mask>
    requires(
        detail::is_supported_scalar_value<T>::value &&
        totally_ordered<T> &&
        same_as<Mask, bool>)
constexpr T reduce_max(const T& value, Mask mask) noexcept {
    return mask ? value : numeric_limits<T>::lowest();
}

template<size_t Bytes, class Abi>
constexpr bool all_of(const basic_mask<Bytes, Abi>& value) noexcept {
    for (simd_size_type i = 0; i < basic_mask<Bytes, Abi>::size; ++i) {
        if (!value[i]) {
            return false;
        }
    }
    return true;
}

template<size_t Bytes, class Abi>
constexpr bool any_of(const basic_mask<Bytes, Abi>& value) noexcept {
    for (simd_size_type i = 0; i < basic_mask<Bytes, Abi>::size; ++i) {
        if (value[i]) {
            return true;
        }
    }
    return false;
}

template<size_t Bytes, class Abi>
constexpr bool none_of(const basic_mask<Bytes, Abi>& value) noexcept {
    return !std::simd::any_of(value);
}

template<size_t Bytes, class Abi>
constexpr simd_size_type reduce_count(const basic_mask<Bytes, Abi>& value) noexcept {
    auto get = [&](simd_size_type i) { return simd_size_type(value[i] ? 1 : 0); };
    plus<> op;
    return detail::reduction_tree<0, basic_mask<Bytes, Abi>::size()>(get, op);
}

template<size_t Bytes, class Abi>
constexpr simd_size_type reduce_min_index(const basic_mask<Bytes, Abi>& value) noexcept {
    for (simd_size_type i = 0; i < basic_mask<Bytes, Abi>::size; ++i) {
        if (value[i]) {
            return i;
        }
    }
    return 0;
}

template<size_t Bytes, class Abi>
constexpr simd_size_type reduce_max_index(const basic_mask<Bytes, Abi>& value) noexcept {
    for (simd_size_type i = basic_mask<Bytes, Abi>::size; i > 0; --i) {
        if (value[i - 1]) {
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

constexpr simd_size_type reduce_count(same_as<bool> auto value) noexcept {
    return value ? 1 : 0;
}

constexpr simd_size_type reduce_min_index(same_as<bool> auto) noexcept {
    return 0;
}

constexpr simd_size_type reduce_max_index(same_as<bool> auto) noexcept {
    return 0;
}

#pragma once

template<class V>
constexpr remove_cvref_t<V> abs(const V& value)
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    return __detail::unary_math_transform<result_type>(value, __detail::math_fabs_fn{});
}

template<class V>
constexpr remove_cvref_t<V> abs(const V& value)
    requires(__detail::is_simd_signed_integral_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    return __detail::unary_math_transform<result_type>(value, __detail::math_signed_abs_fn{});
}

template<class V>
constexpr remove_cvref_t<V> fabs(const V& value)
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    return __detail::unary_math_transform<result_type>(value, __detail::math_fabs_fn{});
}

#define FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(name) \
template<class V> \
constexpr remove_cvref_t<V> name(const V& value) \
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) { \
    using result_type = remove_cvref_t<V>; \
    return __detail::unary_math_transform<result_type>(value, __detail::math_##name##_fn{}); \
}

#define FORGE_SIMD_UNARY_FLOAT_MATH_RUNTIME(name) \
template<class V> \
remove_cvref_t<V> name(const V& value) \
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) { \
    using result_type = remove_cvref_t<V>; \
    return __detail::unary_math_transform<result_type>(value, __detail::math_##name##_fn{}); \
}

#define FORGE_SIMD_UNARY_FLOAT_INT_MATH_CONSTEXPR(name, int_type) \
template<class V> \
constexpr rebind_t<int_type, remove_cvref_t<V>> name(const V& value) \
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) { \
    using result_type = rebind_t<int_type, remove_cvref_t<V>>; \
    return __detail::unary_math_transform<result_type>(value, __detail::math_##name##_fn{}); \
}

#define FORGE_SIMD_UNARY_FLOAT_INT_MATH_RUNTIME(name, int_type) \
template<class V> \
rebind_t<int_type, remove_cvref_t<V>> name(const V& value) \
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) { \
    using result_type = rebind_t<int_type, remove_cvref_t<V>>; \
    return __detail::unary_math_transform<result_type>(value, __detail::math_##name##_fn{}); \
}

// The unused defaulted argument gives each deduction pattern a distinct
// specialization identity even when its substituted function type is identical.
#define FORGE_SIMD_BINARY_FLOAT_MATH_OVERLOAD(overload_id, name, left_type, right_type) \
template<class V, int = overload_id> \
constexpr __detail::deduced_math_vector_t<V> name(const left_type& left, const right_type& right) \
    requires(__detail::math_floating_point<V>) { \
    using result_type = __detail::deduced_math_vector_t<V>; \
    return __detail::binary_math_transform<result_type>(left, right, __detail::math_##name##_fn{}); \
}

#define FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(name) \
FORGE_SIMD_BINARY_FLOAT_MATH_OVERLOAD(1, name, V, V) \
FORGE_SIMD_BINARY_FLOAT_MATH_OVERLOAD(2, name, __detail::deduced_math_vector_t<V>, V) \
FORGE_SIMD_BINARY_FLOAT_MATH_OVERLOAD(3, name, V, __detail::deduced_math_vector_t<V>)

#define FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(overload_id, name, x_type, y_type, z_type) \
template<class V, int = overload_id> \
constexpr __detail::deduced_math_vector_t<V> name(const x_type& x, const y_type& y, const z_type& z) \
    requires(__detail::math_floating_point<V>) { \
    using result_type = __detail::deduced_math_vector_t<V>; \
    return __detail::ternary_math_transform<result_type>(x, y, z, __detail::math_##name##_fn{}); \
}

#define FORGE_SIMD_TERNARY_FLOAT_MATH_CONSTEXPR(name) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(1, name, V, V, V) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(2, name, __detail::deduced_math_vector_t<V>, V, V) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(3, name, V, __detail::deduced_math_vector_t<V>, V) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(4, name, V, V, __detail::deduced_math_vector_t<V>) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(5, name, __detail::deduced_math_vector_t<V>, __detail::deduced_math_vector_t<V>, V) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(6, name, __detail::deduced_math_vector_t<V>, V, __detail::deduced_math_vector_t<V>) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(7, name, V, __detail::deduced_math_vector_t<V>, __detail::deduced_math_vector_t<V>)

#define FORGE_SIMD_BINARY_FLOAT_MASK_OVERLOAD(overload_id, name, left_type, right_type) \
template<class V, int = overload_id> \
constexpr typename __detail::deduced_math_vector_t<V>::mask_type name(const left_type& left, const right_type& right) \
    requires(__detail::math_floating_point<V>) { \
    using result_type = __detail::deduced_math_vector_t<V>; \
    return __detail::binary_math_mask_transform<result_type>(left, right, __detail::math_##name##_fn{}); \
}

#define FORGE_SIMD_BINARY_FLOAT_MASK_CONSTEXPR(name) \
FORGE_SIMD_BINARY_FLOAT_MASK_OVERLOAD(1, name, V, V) \
FORGE_SIMD_BINARY_FLOAT_MASK_OVERLOAD(2, name, __detail::deduced_math_vector_t<V>, V) \
FORGE_SIMD_BINARY_FLOAT_MASK_OVERLOAD(3, name, V, __detail::deduced_math_vector_t<V>)

template<class V>
constexpr remove_cvref_t<V> ceil(const V& value)
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    return __detail::unary_math_transform<result_type>(value, __detail::math_ceil_fn{});
}

template<class V>
constexpr remove_cvref_t<V> floor(const V& value)
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    return __detail::unary_math_transform<result_type>(value, __detail::math_floor_fn{});
}

FORGE_SIMD_UNARY_FLOAT_MATH_RUNTIME(nearbyint)
FORGE_SIMD_UNARY_FLOAT_MATH_RUNTIME(rint)
FORGE_SIMD_UNARY_FLOAT_INT_MATH_CONSTEXPR(ilogb, int)
FORGE_SIMD_UNARY_FLOAT_INT_MATH_RUNTIME(lrint, long int)
FORGE_SIMD_UNARY_FLOAT_INT_MATH_RUNTIME(llrint, long long int)
template<class V>
constexpr remove_cvref_t<V> round(const V& value)
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    return __detail::unary_math_transform<result_type>(value, __detail::math_round_fn{});
}

template<class V>
constexpr rebind_t<long int, remove_cvref_t<V>> lround(const V& value)
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = rebind_t<long int, remove_cvref_t<V>>;
    return __detail::unary_math_transform<result_type>(value, __detail::math_round_to_integer_fn<long int>{});
}

template<class V>
constexpr rebind_t<long long int, remove_cvref_t<V>> llround(const V& value)
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = rebind_t<long long int, remove_cvref_t<V>>;
    return __detail::unary_math_transform<result_type>(value, __detail::math_round_to_integer_fn<long long int>{});
}

template<class V>
constexpr remove_cvref_t<V> trunc(const V& value)
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    return __detail::unary_math_transform<result_type>(value, __detail::math_trunc_fn{});
}

FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(fmod)
FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(remainder)
FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(copysign)
FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(nextafter)
FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(fdim)
FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(fmax)
FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(fmin)
FORGE_SIMD_TERNARY_FLOAT_MATH_CONSTEXPR(fma)

template<class V>
constexpr rebind_t<int, remove_cvref_t<V>> fpclassify(const V& value)
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = rebind_t<int, remove_cvref_t<V>>;
    return __detail::unary_math_transform<result_type>(value, __detail::math_fpclassify_fn{});
}

template<class V>
constexpr typename remove_cvref_t<V>::mask_type isfinite(const V& value)
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    return __detail::unary_math_mask_transform(value, __detail::math_isfinite_fn{});
}

template<class V>
constexpr typename remove_cvref_t<V>::mask_type isinf(const V& value)
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    return __detail::unary_math_mask_transform(value, __detail::math_isinf_fn{});
}

template<class V>
constexpr typename remove_cvref_t<V>::mask_type isnan(const V& value)
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    return __detail::unary_math_mask_transform(value, __detail::math_isnan_fn{});
}

template<class V>
constexpr typename remove_cvref_t<V>::mask_type isnormal(const V& value)
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    return __detail::unary_math_mask_transform(value, __detail::math_isnormal_fn{});
}

template<class V>
constexpr typename remove_cvref_t<V>::mask_type signbit(const V& value)
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    return __detail::unary_math_mask_transform(value, __detail::math_signbit_fn{});
}

FORGE_SIMD_BINARY_FLOAT_MASK_CONSTEXPR(isgreater)
FORGE_SIMD_BINARY_FLOAT_MASK_CONSTEXPR(isgreaterequal)
FORGE_SIMD_BINARY_FLOAT_MASK_CONSTEXPR(isless)
FORGE_SIMD_BINARY_FLOAT_MASK_CONSTEXPR(islessequal)
FORGE_SIMD_BINARY_FLOAT_MASK_CONSTEXPR(islessgreater)
FORGE_SIMD_BINARY_FLOAT_MASK_CONSTEXPR(isunordered)

template<class V>
constexpr remove_cvref_t<V> frexp(const V& value, rebind_t<int, remove_cvref_t<V>>* exp)
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using vec_type = remove_cvref_t<V>;
    using exp_type = rebind_t<int, vec_type>;

    vec_type fraction;
    exp_type exponent;
    for (__simd_size_type i = 0; i < vec_type::size; ++i) {
        int lane_exponent = 0;
        __detail::set_lane(fraction, i, static_cast<typename vec_type::value_type>(std::frexp(value[i], &lane_exponent)));
        __detail::set_lane(exponent, i, lane_exponent);
    }
    if (exp != nullptr) {
        *exp = exponent;
    }
    return fraction;
}

template<class T, class Abi>
constexpr basic_vec<T, Abi> modf(const type_identity_t<basic_vec<T, Abi>>& value, basic_vec<T, Abi>* iptr)
    requires(is_floating_point<T>::value) {
    basic_vec<T, Abi> fractional;
    basic_vec<T, Abi> integral;
    for (__simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        T integral_lane{};
        __detail::set_lane(fractional, i, static_cast<T>(std::modf(value[i], &integral_lane)));
        __detail::set_lane(integral, i, integral_lane);
    }
    if (iptr != nullptr) {
        *iptr = integral;
    }
    return fractional;
}

#define FORGE_SIMD_REMQUO_OVERLOAD(overload_id, left_type, right_type) \
template<class V, int = overload_id> \
constexpr __detail::deduced_math_vector_t<V> \
remquo(const left_type& left, const right_type& right, rebind_t<int, __detail::deduced_math_vector_t<V>>* quo) \
    requires(__detail::math_floating_point<V>) { \
    using result_type = __detail::deduced_math_vector_t<V>; \
    using quo_type = rebind_t<int, result_type>; \
    const result_type lhs = __detail::to_math_vector<result_type>(left); \
    const result_type rhs = __detail::to_math_vector<result_type>(right); \
    result_type remainder_value; \
    quo_type quotient_bits; \
    for (__simd_size_type i = 0; i < result_type::size; ++i) { \
        int quotient = 0; \
        __detail::set_lane(remainder_value, i, static_cast<typename result_type::value_type>(std::remquo(lhs[i], rhs[i], &quotient))); \
        __detail::set_lane(quotient_bits, i, quotient); \
    } \
    if (quo != nullptr) { \
        *quo = quotient_bits; \
    } \
    return remainder_value; \
}

FORGE_SIMD_REMQUO_OVERLOAD(1, V, V)
FORGE_SIMD_REMQUO_OVERLOAD(2, __detail::deduced_math_vector_t<V>, V)
FORGE_SIMD_REMQUO_OVERLOAD(3, V, __detail::deduced_math_vector_t<V>)

#undef FORGE_SIMD_REMQUO_OVERLOAD

template<class V>
constexpr remove_cvref_t<V> ldexp(const V& value, const rebind_t<int, remove_cvref_t<V>>& exp)
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    result_type result;
    for (__simd_size_type i = 0; i < result_type::size; ++i) {
        __detail::set_lane(result, i, static_cast<typename result_type::value_type>(std::ldexp(value[i], exp[i])));
    }
    return result;
}

template<class V>
constexpr remove_cvref_t<V> scalbn(const V& value, const rebind_t<int, remove_cvref_t<V>>& exp)
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    result_type result;
    for (__simd_size_type i = 0; i < result_type::size; ++i) {
        __detail::set_lane(result, i, static_cast<typename result_type::value_type>(std::scalbn(value[i], exp[i])));
    }
    return result;
}

template<class V>
constexpr remove_cvref_t<V> scalbln(const V& value, const rebind_t<long int, remove_cvref_t<V>>& exp)
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    result_type result;
    for (__simd_size_type i = 0; i < result_type::size; ++i) {
        __detail::set_lane(result, i, static_cast<typename result_type::value_type>(std::scalbln(value[i], exp[i])));
    }
    return result;
}

#undef FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR
#undef FORGE_SIMD_UNARY_FLOAT_MATH_RUNTIME
#undef FORGE_SIMD_UNARY_FLOAT_INT_MATH_CONSTEXPR
#undef FORGE_SIMD_UNARY_FLOAT_INT_MATH_RUNTIME
#undef FORGE_SIMD_BINARY_FLOAT_MATH_OVERLOAD
#undef FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR
#undef FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD
#undef FORGE_SIMD_TERNARY_FLOAT_MATH_CONSTEXPR
#undef FORGE_SIMD_BINARY_FLOAT_MASK_OVERLOAD
#undef FORGE_SIMD_BINARY_FLOAT_MASK_CONSTEXPR

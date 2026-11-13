#pragma once

#define FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(name) \
template<class V> \
constexpr remove_cvref_t<V> name(const V& value) \
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) { \
    using result_type = remove_cvref_t<V>; \
    return detail::unary_math_transform<result_type>(value, detail::math_##name##_fn{}); \
}

// The unused defaulted argument gives each deduction pattern a distinct
// specialization identity even when its substituted function type is identical.
#define FORGE_SIMD_BINARY_FLOAT_MATH_OVERLOAD(overload_id, name, left_type, right_type) \
template<class V, int = overload_id> \
constexpr detail::deduced_math_vector_t<V> name(const left_type& left, const right_type& right) \
    requires(detail::math_floating_point<V>) { \
    using result_type = detail::deduced_math_vector_t<V>; \
    return detail::binary_math_transform<result_type>(left, right, detail::math_##name##_fn{}); \
}

#define FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(name) \
FORGE_SIMD_BINARY_FLOAT_MATH_OVERLOAD(1, name, V, V) \
FORGE_SIMD_BINARY_FLOAT_MATH_OVERLOAD(2, name, detail::deduced_math_vector_t<V>, V) \
FORGE_SIMD_BINARY_FLOAT_MATH_OVERLOAD(3, name, V, detail::deduced_math_vector_t<V>)

#define FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(overload_id, name, x_type, y_type, z_type, noexcept_spec) \
template<class V, int = overload_id> \
constexpr detail::deduced_math_vector_t<V> name(const x_type& x, const y_type& y, const z_type& z) noexcept_spec \
    requires(detail::math_floating_point<V>) { \
    using result_type = detail::deduced_math_vector_t<V>; \
    return detail::ternary_math_transform<result_type>(x, y, z, detail::math_##name##_fn{}); \
}

#define FORGE_SIMD_TERNARY_FLOAT_MATH_CONSTEXPR(name, noexcept_spec) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(1, name, V, V, V, noexcept_spec) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(2, name, detail::deduced_math_vector_t<V>, V, V, ) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(3, name, V, detail::deduced_math_vector_t<V>, V, ) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(4, name, V, V, detail::deduced_math_vector_t<V>, ) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(5, name, detail::deduced_math_vector_t<V>, detail::deduced_math_vector_t<V>, V, ) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(6, name, detail::deduced_math_vector_t<V>, V, detail::deduced_math_vector_t<V>, ) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(7, name, V, detail::deduced_math_vector_t<V>, detail::deduced_math_vector_t<V>, )

// SIMD math leaves errno access unspecified. Limit the GCC option to this
// function: never change the including translation unit's floating-point mode.
template<class V>
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("no-math-errno")))
#endif
constexpr remove_cvref_t<V> sqrt(const V& value)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    using T = typename result_type::value_type;
    if consteval {
        return detail::unary_math_transform<result_type>(value, detail::math_sqrt_fn{});
    } else {
        result_type result;
        for (simd_size_type i = 0; i < result_type::size(); ++i) {
#if defined(__clang__) && defined(__has_builtin)
#  if __has_builtin(__builtin_elementwise_sqrt)
            detail::set_lane(result, i, __builtin_elementwise_sqrt(static_cast<T>(value[i])));
#  else
            detail::set_lane(result, i, std::sqrt(value[i]));
#  endif
#elif defined(__GNUC__)
            if constexpr (is_same_v<T, float>) detail::set_lane(result, i, __builtin_sqrtf(value[i]));
            else if constexpr (is_same_v<T, double>) detail::set_lane(result, i, __builtin_sqrt(value[i]));
            else detail::set_lane(result, i, std::sqrt(value[i]));
#else
            detail::set_lane(result, i, std::sqrt(value[i]));
#endif
        }
        return result;
    }
}
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(sin)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(cos)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(tan)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(asin)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(acos)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(atan)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(sinh)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(cosh)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(tanh)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(asinh)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(acosh)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(atanh)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(exp)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(exp2)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(expm1)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(log)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(log10)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(log1p)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(log2)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(logb)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(cbrt)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(erf)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(erfc)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(lgamma)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(tgamma)

FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(atan2)
FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(pow)
FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(hypot)

FORGE_SIMD_TERNARY_FLOAT_MATH_CONSTEXPR(hypot, )
FORGE_SIMD_TERNARY_FLOAT_MATH_CONSTEXPR(lerp, noexcept)

#undef FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR
#undef FORGE_SIMD_BINARY_FLOAT_MATH_OVERLOAD
#undef FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR
#undef FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD
#undef FORGE_SIMD_TERNARY_FLOAT_MATH_CONSTEXPR

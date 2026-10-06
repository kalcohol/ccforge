#pragma once

#define FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(name) \
template<class V> \
constexpr remove_cvref_t<V> name(const V& value) \
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) { \
    using result_type = remove_cvref_t<V>; \
    return detail::unary_math_transform<result_type>(value, [](auto lane) { return std::name(lane); }); \
}

#define FORGE_SIMD_BINARY_FLOAT_MATH_OVERLOAD(name, left_type, right_type) \
template<class V> \
constexpr detail::deduced_math_vector_t<V> name(const left_type& left, const right_type& right) \
    requires(detail::math_floating_point<V>) { \
    using result_type = detail::deduced_math_vector_t<V>; \
    return detail::binary_math_transform<result_type>(left, right, [](auto lhs, auto rhs) { return std::name(lhs, rhs); }); \
}

#define FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(name) \
FORGE_SIMD_BINARY_FLOAT_MATH_OVERLOAD(name, V, V) \
FORGE_SIMD_BINARY_FLOAT_MATH_OVERLOAD(name, detail::deduced_math_vector_t<V>, V) \
FORGE_SIMD_BINARY_FLOAT_MATH_OVERLOAD(name, V, detail::deduced_math_vector_t<V>)

#define FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(name, x_type, y_type, z_type, noexcept_spec) \
template<class V> \
constexpr detail::deduced_math_vector_t<V> name(const x_type& x, const y_type& y, const z_type& z) noexcept_spec \
    requires(detail::math_floating_point<V>) { \
    using result_type = detail::deduced_math_vector_t<V>; \
    return detail::ternary_math_transform<result_type>(x, y, z, [](auto vx, auto vy, auto vz) { return std::name(vx, vy, vz); }); \
}

#define FORGE_SIMD_TERNARY_FLOAT_MATH_CONSTEXPR(name, noexcept_spec) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(name, V, V, V, noexcept_spec) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(name, detail::deduced_math_vector_t<V>, V, V, ) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(name, V, detail::deduced_math_vector_t<V>, V, ) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(name, V, V, detail::deduced_math_vector_t<V>, ) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(name, detail::deduced_math_vector_t<V>, detail::deduced_math_vector_t<V>, V, ) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(name, detail::deduced_math_vector_t<V>, V, detail::deduced_math_vector_t<V>, ) \
FORGE_SIMD_TERNARY_FLOAT_MATH_OVERLOAD(name, V, detail::deduced_math_vector_t<V>, detail::deduced_math_vector_t<V>, )

FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(sqrt)
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

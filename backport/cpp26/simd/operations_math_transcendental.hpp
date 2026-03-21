#define FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(name) \
template<class V> \
constexpr remove_cvref_t<V> name(const V& value) \
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) { \
    using result_type = remove_cvref_t<V>; \
    return detail::unary_math_transform<result_type>(value, [](auto lane) { return std::name(lane); }); \
}

#define FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(name) \
template<class A, class B> \
constexpr detail::binary_math_result_t<A, B> name(const A& left, const B& right) \
    requires(detail::is_binary_math_floating<A, B>::value) { \
    using result_type = detail::binary_math_result_t<A, B>; \
    return detail::binary_math_transform<result_type>(left, right, [](auto lhs, auto rhs) { return std::name(lhs, rhs); }); \
}

#define FORGE_SIMD_TERNARY_FLOAT_MATH_CONSTEXPR(name) \
template<class A, class B, class C> \
constexpr detail::ternary_math_result_t<A, B, C> name(const A& x, const B& y, const C& z) \
    requires(detail::is_ternary_math_floating<A, B, C>::value) { \
    using result_type = detail::ternary_math_result_t<A, B, C>; \
    return detail::ternary_math_transform<result_type>(x, y, z, [](auto vx, auto vy, auto vz) { return std::name(vx, vy, vz); }); \
}

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

template<class A, class B, class C>
constexpr detail::ternary_math_result_t<A, B, C> hypot(const A& x, const B& y, const C& z)
    requires(detail::is_ternary_math_floating<A, B, C>::value) {
    using result_type = detail::ternary_math_result_t<A, B, C>;
    return detail::ternary_math_transform<result_type>(x, y, z, [](auto vx, auto vy, auto vz) {
        return std::hypot(vx, vy, vz);
    });
}

template<class A, class B, class C>
constexpr detail::ternary_math_result_t<A, B, C> lerp(const A& a, const B& b, const C& t) noexcept
    requires(detail::is_ternary_math_floating<A, B, C>::value) {
    using result_type = detail::ternary_math_result_t<A, B, C>;
    return detail::ternary_math_transform<result_type>(a, b, t, [](auto va, auto vb, auto vt) {
        return std::lerp(va, vb, vt);
    });
}

#undef FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR
#undef FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR
#undef FORGE_SIMD_TERNARY_FLOAT_MATH_CONSTEXPR

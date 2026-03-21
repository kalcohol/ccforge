#include "operations_math_special_scalar.hpp"

#define FORGE_SIMD_UNARY_FLOAT_MATH_RUNTIME(name) \
template<class V> \
remove_cvref_t<V> name(const V& value) \
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) { \
    using result_type = remove_cvref_t<V>; \
    return detail::unary_math_transform<result_type>(value, [](auto lane) { return detail::special_math::name(lane); }); \
}

#define FORGE_SIMD_BINARY_FLOAT_MATH_RUNTIME(name) \
template<class A, class B> \
detail::binary_math_result_t<A, B> name(const A& left, const B& right) \
    requires(detail::is_binary_math_floating<A, B>::value) { \
    using result_type = detail::binary_math_result_t<A, B>; \
    return detail::binary_math_transform<result_type>(left, right, [](auto lhs, auto rhs) { return detail::special_math::name(lhs, rhs); }); \
}

#define FORGE_SIMD_TERNARY_FLOAT_MATH_RUNTIME(name) \
template<class A, class B, class C> \
detail::ternary_math_result_t<A, B, C> name(const A& x, const B& y, const C& z) \
    requires(detail::is_ternary_math_floating<A, B, C>::value) { \
    using result_type = detail::ternary_math_result_t<A, B, C>; \
    return detail::ternary_math_transform<result_type>(x, y, z, [](auto vx, auto vy, auto vz) { return detail::special_math::name(vx, vy, vz); }); \
}

FORGE_SIMD_UNARY_FLOAT_MATH_RUNTIME(comp_ellint_1)
FORGE_SIMD_UNARY_FLOAT_MATH_RUNTIME(comp_ellint_2)
FORGE_SIMD_UNARY_FLOAT_MATH_RUNTIME(expint)
FORGE_SIMD_UNARY_FLOAT_MATH_RUNTIME(riemann_zeta)

FORGE_SIMD_BINARY_FLOAT_MATH_RUNTIME(beta)
FORGE_SIMD_BINARY_FLOAT_MATH_RUNTIME(comp_ellint_3)
FORGE_SIMD_BINARY_FLOAT_MATH_RUNTIME(cyl_bessel_i)
FORGE_SIMD_BINARY_FLOAT_MATH_RUNTIME(cyl_bessel_j)
FORGE_SIMD_BINARY_FLOAT_MATH_RUNTIME(cyl_bessel_k)
FORGE_SIMD_BINARY_FLOAT_MATH_RUNTIME(cyl_neumann)
FORGE_SIMD_BINARY_FLOAT_MATH_RUNTIME(ellint_1)
FORGE_SIMD_BINARY_FLOAT_MATH_RUNTIME(ellint_2)

FORGE_SIMD_TERNARY_FLOAT_MATH_RUNTIME(ellint_3)

template<class V>
remove_cvref_t<V> hermite(const rebind_t<unsigned, remove_cvref_t<V>>& n, const V& x)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    result_type result;
    for (simd_size_type i = 0; i < result_type::size; ++i) {
        detail::set_lane(result, i, static_cast<typename result_type::value_type>(detail::special_math::hermite(n[i], x[i])));
    }
    return result;
}

template<class V>
remove_cvref_t<V> laguerre(const rebind_t<unsigned, remove_cvref_t<V>>& n, const V& x)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    result_type result;
    for (simd_size_type i = 0; i < result_type::size; ++i) {
        detail::set_lane(result, i, static_cast<typename result_type::value_type>(detail::special_math::laguerre(n[i], x[i])));
    }
    return result;
}

template<class V>
remove_cvref_t<V> legendre(const rebind_t<unsigned, remove_cvref_t<V>>& n, const V& x)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    result_type result;
    for (simd_size_type i = 0; i < result_type::size; ++i) {
        detail::set_lane(result, i, static_cast<typename result_type::value_type>(detail::special_math::legendre(n[i], x[i])));
    }
    return result;
}

template<class V>
remove_cvref_t<V> sph_bessel(const rebind_t<unsigned, remove_cvref_t<V>>& n, const V& x)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    result_type result;
    for (simd_size_type i = 0; i < result_type::size; ++i) {
        detail::set_lane(result, i, static_cast<typename result_type::value_type>(detail::special_math::sph_bessel(n[i], x[i])));
    }
    return result;
}

template<class V>
remove_cvref_t<V> sph_neumann(const rebind_t<unsigned, remove_cvref_t<V>>& n, const V& x)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    result_type result;
    for (simd_size_type i = 0; i < result_type::size; ++i) {
        detail::set_lane(result, i, static_cast<typename result_type::value_type>(detail::special_math::sph_neumann(n[i], x[i])));
    }
    return result;
}

template<class V>
remove_cvref_t<V> assoc_laguerre(const rebind_t<unsigned, remove_cvref_t<V>>& n,
                                 const rebind_t<unsigned, remove_cvref_t<V>>& m,
                                 const V& x)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    result_type result;
    for (simd_size_type i = 0; i < result_type::size; ++i) {
        detail::set_lane(result, i, static_cast<typename result_type::value_type>(detail::special_math::assoc_laguerre(n[i], m[i], x[i])));
    }
    return result;
}

template<class V>
remove_cvref_t<V> assoc_legendre(const rebind_t<unsigned, remove_cvref_t<V>>& l,
                                 const rebind_t<unsigned, remove_cvref_t<V>>& m,
                                 const V& x)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    result_type result;
    for (simd_size_type i = 0; i < result_type::size; ++i) {
        detail::set_lane(result, i, static_cast<typename result_type::value_type>(detail::special_math::assoc_legendre(l[i], m[i], x[i])));
    }
    return result;
}

template<class V>
remove_cvref_t<V> sph_legendre(const rebind_t<unsigned, remove_cvref_t<V>>& l,
                               const rebind_t<unsigned, remove_cvref_t<V>>& m,
                               const V& theta)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    result_type result;
    for (simd_size_type i = 0; i < result_type::size; ++i) {
        detail::set_lane(result, i, static_cast<typename result_type::value_type>(detail::special_math::sph_legendre(l[i], m[i], theta[i])));
    }
    return result;
}

#undef FORGE_SIMD_UNARY_FLOAT_MATH_RUNTIME
#undef FORGE_SIMD_BINARY_FLOAT_MATH_RUNTIME
#undef FORGE_SIMD_TERNARY_FLOAT_MATH_RUNTIME

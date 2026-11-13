#pragma once

namespace __detail {

// Preserve the const-reference scalar arguments used by complex transforms.
#define FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(name) \
struct complex_math_##name##_fn { \
    template<class... T> \
    constexpr auto operator()(const T&... args) const { return std::name(args...); } \
};

FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(abs)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(arg)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(norm)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(pow)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(polar)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(conj)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(proj)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(exp)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(log)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(log10)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(sqrt)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(sin)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(cos)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(tan)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(asin)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(acos)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(atan)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(sinh)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(cosh)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(tanh)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(asinh)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(acosh)
FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION(atanh)

#undef FORGE_SIMD_DETAIL_COMPLEX_MATH_FUNCTION

template<class T>
struct is_simd_complex_value : false_type {};

template<class T, class Abi>
struct is_simd_complex_value<basic_vec<complex<T>, Abi>> : true_type {};

template<class Result, class V, class Fun>
constexpr Result unary_complex_transform(const V& value, Fun fun) {
    Result result;
    for (__simd_size_type i = 0; i < remove_cvref_t<V>::size; ++i) {
        __detail::set_lane(result, i, static_cast<typename Result::value_type>(fun(value[i])));
    }
    return result;
}

template<class V, class Fun>
constexpr typename remove_cvref_t<V>::__real_type unary_complex_real_transform(const V& value, Fun fun) {
    using result_type = typename remove_cvref_t<V>::__real_type;
    result_type result;
    for (__simd_size_type i = 0; i < remove_cvref_t<V>::size; ++i) {
        __detail::set_lane(result, i, static_cast<typename result_type::value_type>(fun(value[i])));
    }
    return result;
}

template<class Result, class A, class B, class Fun>
constexpr Result binary_complex_transform(const A& left, const B& right, Fun fun) {
    const Result lhs(left, flag_convert);
    const Result rhs(right, flag_convert);
    Result result;
    for (__simd_size_type i = 0; i < Result::size; ++i) {
        __detail::set_lane(result, i, static_cast<typename Result::value_type>(fun(lhs[i], rhs[i])));
    }
    return result;
}

} // namespace __detail

template<class V>
constexpr typename remove_cvref_t<V>::__real_type real(const V& value) noexcept
    requires(__detail::is_simd_complex_value<remove_cvref_t<V>>::value) {
    return value.real();
}

template<class V>
constexpr typename remove_cvref_t<V>::__real_type imag(const V& value) noexcept
    requires(__detail::is_simd_complex_value<remove_cvref_t<V>>::value) {
    return value.imag();
}

template<class V>
constexpr typename remove_cvref_t<V>::__real_type abs(const V& value)
    requires(__detail::is_simd_complex_value<remove_cvref_t<V>>::value) {
    return __detail::unary_complex_real_transform(value, __detail::complex_math_abs_fn{});
}

template<class V>
constexpr typename remove_cvref_t<V>::__real_type arg(const V& value)
    requires(__detail::is_simd_complex_value<remove_cvref_t<V>>::value) {
    return __detail::unary_complex_real_transform(value, __detail::complex_math_arg_fn{});
}

template<class V>
constexpr typename remove_cvref_t<V>::__real_type norm(const V& value)
    requires(__detail::is_simd_complex_value<remove_cvref_t<V>>::value) {
    return __detail::unary_complex_real_transform(value, __detail::complex_math_norm_fn{});
}

#define FORGE_SIMD_UNARY_COMPLEX_MATH(name) \
template<class V> \
constexpr remove_cvref_t<V> name(const V& value) \
    requires(__detail::is_simd_complex_value<remove_cvref_t<V>>::value) { \
    using result_type = remove_cvref_t<V>; \
    return __detail::unary_complex_transform<result_type>(value, __detail::complex_math_##name##_fn{}); \
}

FORGE_SIMD_UNARY_COMPLEX_MATH(conj)
FORGE_SIMD_UNARY_COMPLEX_MATH(proj)
FORGE_SIMD_UNARY_COMPLEX_MATH(exp)
FORGE_SIMD_UNARY_COMPLEX_MATH(log)
FORGE_SIMD_UNARY_COMPLEX_MATH(log10)
FORGE_SIMD_UNARY_COMPLEX_MATH(sqrt)
FORGE_SIMD_UNARY_COMPLEX_MATH(sin)
FORGE_SIMD_UNARY_COMPLEX_MATH(cos)
FORGE_SIMD_UNARY_COMPLEX_MATH(tan)
FORGE_SIMD_UNARY_COMPLEX_MATH(asin)
FORGE_SIMD_UNARY_COMPLEX_MATH(acos)
FORGE_SIMD_UNARY_COMPLEX_MATH(atan)
FORGE_SIMD_UNARY_COMPLEX_MATH(sinh)
FORGE_SIMD_UNARY_COMPLEX_MATH(cosh)
FORGE_SIMD_UNARY_COMPLEX_MATH(tanh)
FORGE_SIMD_UNARY_COMPLEX_MATH(asinh)
FORGE_SIMD_UNARY_COMPLEX_MATH(acosh)
FORGE_SIMD_UNARY_COMPLEX_MATH(atanh)

#undef FORGE_SIMD_UNARY_COMPLEX_MATH

template<class V>
constexpr remove_cvref_t<V> pow(const V& left, const V& right)
    requires(__detail::is_simd_complex_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    return __detail::binary_complex_transform<result_type>(left, right, __detail::complex_math_pow_fn{});
}

template<class V>
constexpr rebind_t<complex<typename remove_cvref_t<V>::value_type>, remove_cvref_t<V>>
polar(const V& rho)
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = rebind_t<complex<typename remove_cvref_t<V>::value_type>, remove_cvref_t<V>>;
    return __detail::unary_complex_transform<result_type>(rho, __detail::complex_math_polar_fn{});
}

template<class V>
constexpr rebind_t<complex<typename remove_cvref_t<V>::value_type>, remove_cvref_t<V>>
polar(const V& rho, const V& theta)
    requires(__detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = rebind_t<complex<typename remove_cvref_t<V>::value_type>, remove_cvref_t<V>>;
    result_type result;
    for (__simd_size_type i = 0; i < remove_cvref_t<V>::size; ++i) {
        __detail::set_lane(result, i, std::polar(rho[i], theta[i]));
    }
    return result;
}

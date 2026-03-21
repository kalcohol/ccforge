namespace detail {

template<class T>
struct is_simd_complex_value : false_type {};

template<class T, class Abi>
struct is_simd_complex_value<basic_vec<complex<T>, Abi>> : true_type {};

template<class A, class B>
struct has_matching_complex_extent
    : bool_constant<remove_cvref_t<A>::size == remove_cvref_t<B>::size> {};

template<class A, class B>
struct complex_binary_result {
    using prototype = remove_cvref_t<A>;
    using scalar_type = common_type_t<typename remove_cvref_t<A>::value_type, typename remove_cvref_t<B>::value_type>;
    using type = rebind_t<scalar_type, prototype>;
};

template<class A, class B>
using complex_binary_result_t = typename complex_binary_result<A, B>::type;

template<class Result, class V, class Fun>
constexpr Result unary_complex_transform(const V& value, Fun fun) {
    Result result;
    for (simd_size_type i = 0; i < remove_cvref_t<V>::size; ++i) {
        detail::set_lane(result, i, static_cast<typename Result::value_type>(fun(value[i])));
    }
    return result;
}

template<class V, class Fun>
constexpr typename remove_cvref_t<V>::real_type unary_complex_real_transform(const V& value, Fun fun) {
    using result_type = typename remove_cvref_t<V>::real_type;
    result_type result;
    for (simd_size_type i = 0; i < remove_cvref_t<V>::size; ++i) {
        detail::set_lane(result, i, static_cast<typename result_type::value_type>(fun(value[i])));
    }
    return result;
}

template<class Result, class A, class B, class Fun>
constexpr Result binary_complex_transform(const A& left, const B& right, Fun fun) {
    const Result lhs(left, flag_convert);
    const Result rhs(right, flag_convert);
    Result result;
    for (simd_size_type i = 0; i < Result::size; ++i) {
        detail::set_lane(result, i, static_cast<typename Result::value_type>(fun(lhs[i], rhs[i])));
    }
    return result;
}

} // namespace detail

template<class V>
constexpr typename remove_cvref_t<V>::real_type real(const V& value) noexcept
    requires(detail::is_simd_complex_value<remove_cvref_t<V>>::value) {
    return value.real();
}

template<class V>
constexpr typename remove_cvref_t<V>::real_type imag(const V& value) noexcept
    requires(detail::is_simd_complex_value<remove_cvref_t<V>>::value) {
    return value.imag();
}

template<class V>
constexpr typename remove_cvref_t<V>::real_type abs(const V& value) noexcept
    requires(detail::is_simd_complex_value<remove_cvref_t<V>>::value) {
    return detail::unary_complex_real_transform(value, [](const auto& lane) { return std::abs(lane); });
}

template<class V>
constexpr typename remove_cvref_t<V>::real_type arg(const V& value) noexcept
    requires(detail::is_simd_complex_value<remove_cvref_t<V>>::value) {
    return detail::unary_complex_real_transform(value, [](const auto& lane) { return std::arg(lane); });
}

template<class V>
constexpr typename remove_cvref_t<V>::real_type norm(const V& value) noexcept
    requires(detail::is_simd_complex_value<remove_cvref_t<V>>::value) {
    return detail::unary_complex_real_transform(value, [](const auto& lane) { return std::norm(lane); });
}

#define FORGE_SIMD_UNARY_COMPLEX_MATH(name) \
template<class V> \
constexpr remove_cvref_t<V> name(const V& value) noexcept \
    requires(detail::is_simd_complex_value<remove_cvref_t<V>>::value) { \
    using result_type = remove_cvref_t<V>; \
    return detail::unary_complex_transform<result_type>(value, [](const auto& lane) { return std::name(lane); }); \
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

template<class A, class B>
constexpr detail::complex_binary_result_t<A, B> pow(const A& left, const B& right) noexcept
    requires(
        detail::is_simd_complex_value<remove_cvref_t<A>>::value &&
        detail::is_simd_complex_value<remove_cvref_t<B>>::value &&
        detail::has_matching_complex_extent<A, B>::value) {
    using result_type = detail::complex_binary_result_t<A, B>;
    return detail::binary_complex_transform<result_type>(left, right, [](const auto& lhs, const auto& rhs) {
        return std::pow(lhs, rhs);
    });
}

template<class V>
constexpr rebind_t<complex<typename remove_cvref_t<V>::value_type>, remove_cvref_t<V>>
polar(const V& rho) noexcept
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = rebind_t<complex<typename remove_cvref_t<V>::value_type>, remove_cvref_t<V>>;
    return detail::unary_complex_transform<result_type>(rho, [](const auto& lane) { return std::polar(lane); });
}

template<class V>
constexpr rebind_t<complex<typename remove_cvref_t<V>::value_type>, remove_cvref_t<V>>
polar(const V& rho, const V& theta) noexcept
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = rebind_t<complex<typename remove_cvref_t<V>::value_type>, remove_cvref_t<V>>;
    result_type result;
    for (simd_size_type i = 0; i < remove_cvref_t<V>::size; ++i) {
        detail::set_lane(result, i, std::polar(rho[i], theta[i]));
    }
    return result;
}

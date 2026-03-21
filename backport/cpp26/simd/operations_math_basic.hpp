template<class V>
constexpr remove_cvref_t<V> abs(const V& value)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    return detail::unary_math_transform<result_type>(value, [](auto lane) { return std::abs(lane); });
}

template<class V>
constexpr remove_cvref_t<V> abs(const V& value)
    requires(detail::is_simd_signed_integral_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    return detail::unary_math_transform<result_type>(value, [](auto lane) { return std::abs(lane); });
}

template<class V>
constexpr remove_cvref_t<V> fabs(const V& value)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    return detail::unary_math_transform<result_type>(value, [](auto lane) { return std::fabs(lane); });
}

#define FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(name) \
template<class V> \
constexpr remove_cvref_t<V> name(const V& value) \
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) { \
    using result_type = remove_cvref_t<V>; \
    return detail::unary_math_transform<result_type>(value, [](auto lane) { return std::name(lane); }); \
}

#define FORGE_SIMD_UNARY_FLOAT_MATH_RUNTIME(name) \
template<class V> \
remove_cvref_t<V> name(const V& value) \
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) { \
    using result_type = remove_cvref_t<V>; \
    return detail::unary_math_transform<result_type>(value, [](auto lane) { return std::name(lane); }); \
}

#define FORGE_SIMD_UNARY_FLOAT_INT_MATH_CONSTEXPR(name, int_type) \
template<class V> \
constexpr rebind_t<int_type, remove_cvref_t<V>> name(const V& value) \
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) { \
    using result_type = rebind_t<int_type, remove_cvref_t<V>>; \
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

#define FORGE_SIMD_BINARY_FLOAT_MASK_CONSTEXPR(name) \
template<class A, class B> \
constexpr typename detail::binary_math_result_t<A, B>::mask_type name(const A& left, const B& right) \
    requires(detail::is_binary_math_floating<A, B>::value) { \
    using result_type = detail::binary_math_result_t<A, B>; \
    return detail::binary_math_mask_transform<result_type>(left, right, [](auto lhs, auto rhs) { return std::name(lhs, rhs); }); \
}

FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(ceil)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(floor)
FORGE_SIMD_UNARY_FLOAT_MATH_RUNTIME(nearbyint)
FORGE_SIMD_UNARY_FLOAT_MATH_RUNTIME(rint)
FORGE_SIMD_UNARY_FLOAT_INT_MATH_CONSTEXPR(ilogb, int)
FORGE_SIMD_UNARY_FLOAT_INT_MATH_CONSTEXPR(lrint, long int)
FORGE_SIMD_UNARY_FLOAT_INT_MATH_CONSTEXPR(llrint, long long int)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(round)
FORGE_SIMD_UNARY_FLOAT_INT_MATH_CONSTEXPR(lround, long int)
FORGE_SIMD_UNARY_FLOAT_INT_MATH_CONSTEXPR(llround, long long int)
FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR(trunc)

FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(fmod)
FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(remainder)
FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(copysign)
FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(nextafter)
FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(fdim)
FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(fmax)
FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR(fmin)

template<class A, class B, class C>
constexpr detail::ternary_math_result_t<A, B, C> fma(const A& x, const B& y, const C& z)
    requires(detail::is_ternary_math_floating<A, B, C>::value) {
    using result_type = detail::ternary_math_result_t<A, B, C>;
    return detail::ternary_math_transform<result_type>(x, y, z, [](auto vx, auto vy, auto vz) {
        return std::fma(vx, vy, vz);
    });
}

template<class V>
constexpr rebind_t<int, remove_cvref_t<V>> fpclassify(const V& value)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = rebind_t<int, remove_cvref_t<V>>;
    return detail::unary_math_transform<result_type>(value, [](auto lane) { return std::fpclassify(lane); });
}

template<class V>
constexpr typename remove_cvref_t<V>::mask_type isfinite(const V& value)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    return detail::unary_math_mask_transform(value, [](auto lane) { return std::isfinite(lane); });
}

template<class V>
constexpr typename remove_cvref_t<V>::mask_type isinf(const V& value)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    return detail::unary_math_mask_transform(value, [](auto lane) { return std::isinf(lane); });
}

template<class V>
constexpr typename remove_cvref_t<V>::mask_type isnan(const V& value)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    return detail::unary_math_mask_transform(value, [](auto lane) { return std::isnan(lane); });
}

template<class V>
constexpr typename remove_cvref_t<V>::mask_type isnormal(const V& value)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    return detail::unary_math_mask_transform(value, [](auto lane) { return std::isnormal(lane); });
}

template<class V>
constexpr typename remove_cvref_t<V>::mask_type signbit(const V& value)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    return detail::unary_math_mask_transform(value, [](auto lane) { return std::signbit(lane); });
}

FORGE_SIMD_BINARY_FLOAT_MASK_CONSTEXPR(isgreater)
FORGE_SIMD_BINARY_FLOAT_MASK_CONSTEXPR(isgreaterequal)
FORGE_SIMD_BINARY_FLOAT_MASK_CONSTEXPR(isless)
FORGE_SIMD_BINARY_FLOAT_MASK_CONSTEXPR(islessequal)
FORGE_SIMD_BINARY_FLOAT_MASK_CONSTEXPR(islessgreater)
FORGE_SIMD_BINARY_FLOAT_MASK_CONSTEXPR(isunordered)

template<class V>
constexpr remove_cvref_t<V> frexp(const V& value, rebind_t<int, remove_cvref_t<V>>* exp)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using vec_type = remove_cvref_t<V>;
    using exp_type = rebind_t<int, vec_type>;

    vec_type fraction;
    exp_type exponent;
    for (simd_size_type i = 0; i < vec_type::size; ++i) {
        int lane_exponent = 0;
        detail::set_lane(fraction, i, static_cast<typename vec_type::value_type>(std::frexp(value[i], &lane_exponent)));
        detail::set_lane(exponent, i, lane_exponent);
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
    for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
        T integral_lane{};
        detail::set_lane(fractional, i, static_cast<T>(std::modf(value[i], &integral_lane)));
        detail::set_lane(integral, i, integral_lane);
    }
    if (iptr != nullptr) {
        *iptr = integral;
    }
    return fractional;
}

template<class A, class B>
constexpr detail::binary_math_result_t<A, B>
remquo(const A& left, const B& right, rebind_t<int, detail::binary_math_result_t<A, B>>* quo)
    requires(detail::is_binary_math_floating<A, B>::value) {
    using result_type = detail::binary_math_result_t<A, B>;
    using quo_type = rebind_t<int, result_type>;

    const result_type lhs = detail::to_math_vector<result_type>(left);
    const result_type rhs = detail::to_math_vector<result_type>(right);
    result_type remainder_value;
    quo_type quotient_bits;

    for (simd_size_type i = 0; i < result_type::size; ++i) {
        int quotient = 0;
        detail::set_lane(remainder_value, i, static_cast<typename result_type::value_type>(std::remquo(lhs[i], rhs[i], &quotient)));
        detail::set_lane(quotient_bits, i, quotient);
    }

    if (quo != nullptr) {
        *quo = quotient_bits;
    }
    return remainder_value;
}

template<class V>
constexpr remove_cvref_t<V> ldexp(const V& value, const rebind_t<int, remove_cvref_t<V>>& exp)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    result_type result;
    for (simd_size_type i = 0; i < result_type::size; ++i) {
        detail::set_lane(result, i, static_cast<typename result_type::value_type>(std::ldexp(value[i], exp[i])));
    }
    return result;
}

template<class V>
constexpr remove_cvref_t<V> scalbn(const V& value, const rebind_t<int, remove_cvref_t<V>>& exp)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    result_type result;
    for (simd_size_type i = 0; i < result_type::size; ++i) {
        detail::set_lane(result, i, static_cast<typename result_type::value_type>(std::scalbn(value[i], exp[i])));
    }
    return result;
}

template<class V>
constexpr remove_cvref_t<V> scalbln(const V& value, const rebind_t<long int, remove_cvref_t<V>>& exp)
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    result_type result;
    for (simd_size_type i = 0; i < result_type::size; ++i) {
        detail::set_lane(result, i, static_cast<typename result_type::value_type>(std::scalbln(value[i], exp[i])));
    }
    return result;
}

#undef FORGE_SIMD_UNARY_FLOAT_MATH_CONSTEXPR
#undef FORGE_SIMD_UNARY_FLOAT_MATH_RUNTIME
#undef FORGE_SIMD_UNARY_FLOAT_INT_MATH_CONSTEXPR
#undef FORGE_SIMD_BINARY_FLOAT_MATH_CONSTEXPR
#undef FORGE_SIMD_TERNARY_FLOAT_MATH_CONSTEXPR
#undef FORGE_SIMD_BINARY_FLOAT_MASK_CONSTEXPR

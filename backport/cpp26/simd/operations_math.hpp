namespace detail {

template<class T, bool = is_data_parallel_type<remove_cvref_t<T>>::value>
struct math_scalar_value {
    using type = remove_cvref_t<T>;
};

template<class T>
struct math_scalar_value<T, true> {
    using type = typename remove_cvref_t<T>::value_type;
};

template<class T>
using math_scalar_value_t = typename math_scalar_value<T>::type;

template<class T>
struct is_simd_floating_value : false_type {};

template<class T, class Abi>
struct is_simd_floating_value<basic_vec<T, Abi>> : is_floating_point<T> {};

template<class T>
struct is_simd_signed_integral_value : false_type {};

template<class T, class Abi>
struct is_simd_signed_integral_value<basic_vec<T, Abi>>
    : bool_constant<is_integral<T>::value && is_signed<T>::value> {};

template<class A, class B, bool = is_data_parallel_type<remove_cvref_t<A>>::value && is_data_parallel_type<remove_cvref_t<B>>::value>
struct has_matching_math_extent : true_type {};

template<class A, class B>
struct has_matching_math_extent<A, B, true>
    : bool_constant<remove_cvref_t<A>::size == remove_cvref_t<B>::size> {};

template<class A, class B, class C>
struct has_matching_math_extent3
    : bool_constant<
        has_matching_math_extent<A, B>::value &&
        has_matching_math_extent<A, C>::value &&
        has_matching_math_extent<B, C>::value> {};

template<class A, class B>
struct binary_math_prototype {
    using type = conditional_t<
        is_data_parallel_type<remove_cvref_t<A>>::value,
        remove_cvref_t<A>,
        remove_cvref_t<B>>;
};

template<class A, class B>
using binary_math_prototype_t = typename binary_math_prototype<A, B>::type;

template<class A, class B, class C>
struct ternary_math_prototype {
    using type = conditional_t<
        is_data_parallel_type<remove_cvref_t<A>>::value,
        remove_cvref_t<A>,
        conditional_t<
            is_data_parallel_type<remove_cvref_t<B>>::value,
            remove_cvref_t<B>,
            remove_cvref_t<C>>>;
};

template<class A, class B, class C>
using ternary_math_prototype_t = typename ternary_math_prototype<A, B, C>::type;

template<class A, class B>
struct is_binary_math_floating
    : bool_constant<
        has_matching_math_extent<A, B>::value &&
        (is_data_parallel_type<remove_cvref_t<A>>::value || is_data_parallel_type<remove_cvref_t<B>>::value) &&
        is_floating_point<math_scalar_value_t<A>>::value &&
        is_floating_point<math_scalar_value_t<B>>::value> {};

template<class A, class B, class C>
struct is_ternary_math_floating
    : bool_constant<
        has_matching_math_extent3<A, B, C>::value &&
        (is_data_parallel_type<remove_cvref_t<A>>::value ||
         is_data_parallel_type<remove_cvref_t<B>>::value ||
         is_data_parallel_type<remove_cvref_t<C>>::value) &&
        is_floating_point<math_scalar_value_t<A>>::value &&
        is_floating_point<math_scalar_value_t<B>>::value &&
        is_floating_point<math_scalar_value_t<C>>::value> {};

template<class A, class B>
using binary_math_result_t =
    rebind_t<common_type_t<math_scalar_value_t<A>, math_scalar_value_t<B>>, binary_math_prototype_t<A, B>>;

template<class A, class B, class C>
using ternary_math_result_t =
    rebind_t<common_type_t<math_scalar_value_t<A>, math_scalar_value_t<B>, math_scalar_value_t<C>>,
             ternary_math_prototype_t<A, B, C>>;

template<class Result, class Arg>
constexpr Result to_math_vector(const Arg& arg) noexcept {
    if constexpr (is_data_parallel_type<remove_cvref_t<Arg>>::value) {
        if constexpr (is_same<remove_cvref_t<Arg>, Result>::value) {
            return arg;
        } else {
            return Result(arg, flag_convert);
        }
    } else {
        return Result(arg);
    }
}

template<class Result, class V, class Fun>
constexpr Result unary_math_transform(const V& value, Fun fun) {
    Result result;
    for (simd_size_type i = 0; i < remove_cvref_t<V>::size; ++i) {
        detail::set_lane(result, i, static_cast<typename Result::value_type>(fun(value[i])));
    }
    return result;
}

template<class V, class Fun>
constexpr typename remove_cvref_t<V>::mask_type unary_math_mask_transform(const V& value, Fun fun) {
    typename remove_cvref_t<V>::mask_type result;
    for (simd_size_type i = 0; i < remove_cvref_t<V>::size; ++i) {
        detail::set_lane(result, i, static_cast<bool>(fun(value[i])));
    }
    return result;
}

template<class Result, class A, class B, class Fun>
constexpr Result binary_math_transform(const A& left, const B& right, Fun fun) {
    const Result lhs = to_math_vector<Result>(left);
    const Result rhs = to_math_vector<Result>(right);
    Result result;
    for (simd_size_type i = 0; i < Result::size; ++i) {
        detail::set_lane(result, i, static_cast<typename Result::value_type>(fun(lhs[i], rhs[i])));
    }
    return result;
}

template<class Result, class A, class B, class C, class Fun>
constexpr Result ternary_math_transform(const A& x, const B& y, const C& z, Fun fun) {
    const Result vx = to_math_vector<Result>(x);
    const Result vy = to_math_vector<Result>(y);
    const Result vz = to_math_vector<Result>(z);
    Result result;
    for (simd_size_type i = 0; i < Result::size; ++i) {
        detail::set_lane(result, i, static_cast<typename Result::value_type>(fun(vx[i], vy[i], vz[i])));
    }
    return result;
}

} // namespace detail

template<class V>
constexpr remove_cvref_t<V> abs(const V& value) noexcept
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    return detail::unary_math_transform<result_type>(value, [](auto lane) { return std::abs(lane); });
}

template<class V>
constexpr remove_cvref_t<V> abs(const V& value) noexcept
    requires(detail::is_simd_signed_integral_value<remove_cvref_t<V>>::value) {
    using result_type = remove_cvref_t<V>;
    return detail::unary_math_transform<result_type>(value, [](auto lane) { return std::abs(lane); });
}

#define FORGE_SIMD_UNARY_FLOAT_MATH(name) \
template<class V> \
constexpr remove_cvref_t<V> name(const V& value) noexcept \
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) { \
    using result_type = remove_cvref_t<V>; \
    return detail::unary_math_transform<result_type>(value, [](auto lane) { return std::name(lane); }); \
}

FORGE_SIMD_UNARY_FLOAT_MATH(sqrt)
FORGE_SIMD_UNARY_FLOAT_MATH(floor)
FORGE_SIMD_UNARY_FLOAT_MATH(ceil)
FORGE_SIMD_UNARY_FLOAT_MATH(round)
FORGE_SIMD_UNARY_FLOAT_MATH(trunc)
FORGE_SIMD_UNARY_FLOAT_MATH(sin)
FORGE_SIMD_UNARY_FLOAT_MATH(cos)
FORGE_SIMD_UNARY_FLOAT_MATH(exp)
FORGE_SIMD_UNARY_FLOAT_MATH(log)
FORGE_SIMD_UNARY_FLOAT_MATH(log10)
FORGE_SIMD_UNARY_FLOAT_MATH(log1p)
FORGE_SIMD_UNARY_FLOAT_MATH(log2)
FORGE_SIMD_UNARY_FLOAT_MATH(logb)
FORGE_SIMD_UNARY_FLOAT_MATH(cbrt)
FORGE_SIMD_UNARY_FLOAT_MATH(asin)
FORGE_SIMD_UNARY_FLOAT_MATH(acos)
FORGE_SIMD_UNARY_FLOAT_MATH(atan)
FORGE_SIMD_UNARY_FLOAT_MATH(sinh)
FORGE_SIMD_UNARY_FLOAT_MATH(cosh)
FORGE_SIMD_UNARY_FLOAT_MATH(tanh)
FORGE_SIMD_UNARY_FLOAT_MATH(asinh)
FORGE_SIMD_UNARY_FLOAT_MATH(acosh)
FORGE_SIMD_UNARY_FLOAT_MATH(atanh)

#undef FORGE_SIMD_UNARY_FLOAT_MATH

#define FORGE_SIMD_BINARY_FLOAT_MATH(name) \
template<class A, class B> \
constexpr detail::binary_math_result_t<A, B> name(const A& left, const B& right) noexcept \
    requires(detail::is_binary_math_floating<A, B>::value) { \
    using result_type = detail::binary_math_result_t<A, B>; \
    return detail::binary_math_transform<result_type>(left, right, [](auto lhs, auto rhs) { return std::name(lhs, rhs); }); \
}

FORGE_SIMD_BINARY_FLOAT_MATH(pow)
FORGE_SIMD_BINARY_FLOAT_MATH(hypot)
FORGE_SIMD_BINARY_FLOAT_MATH(fmod)
FORGE_SIMD_BINARY_FLOAT_MATH(remainder)
FORGE_SIMD_BINARY_FLOAT_MATH(copysign)
FORGE_SIMD_BINARY_FLOAT_MATH(nextafter)
FORGE_SIMD_BINARY_FLOAT_MATH(fdim)
FORGE_SIMD_BINARY_FLOAT_MATH(fmin)
FORGE_SIMD_BINARY_FLOAT_MATH(fmax)

#undef FORGE_SIMD_BINARY_FLOAT_MATH

template<class A, class B, class C>
constexpr detail::ternary_math_result_t<A, B, C> fma(const A& x, const B& y, const C& z) noexcept
    requires(detail::is_ternary_math_floating<A, B, C>::value) {
    using result_type = detail::ternary_math_result_t<A, B, C>;
    return detail::ternary_math_transform<result_type>(x, y, z, [](auto vx, auto vy, auto vz) {
        return std::fma(vx, vy, vz);
    });
}

template<class V>
constexpr typename remove_cvref_t<V>::mask_type isfinite(const V& value) noexcept
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    return detail::unary_math_mask_transform(value, [](auto lane) { return std::isfinite(lane); });
}

template<class V>
constexpr typename remove_cvref_t<V>::mask_type isinf(const V& value) noexcept
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    return detail::unary_math_mask_transform(value, [](auto lane) { return std::isinf(lane); });
}

template<class V>
constexpr typename remove_cvref_t<V>::mask_type isnan(const V& value) noexcept
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    return detail::unary_math_mask_transform(value, [](auto lane) { return std::isnan(lane); });
}

template<class V>
constexpr typename remove_cvref_t<V>::mask_type isnormal(const V& value) noexcept
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    return detail::unary_math_mask_transform(value, [](auto lane) { return std::isnormal(lane); });
}

template<class V>
constexpr typename remove_cvref_t<V>::mask_type signbit(const V& value) noexcept
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    return detail::unary_math_mask_transform(value, [](auto lane) { return std::signbit(lane); });
}

template<class V>
constexpr pair<remove_cvref_t<V>, rebind_t<int, remove_cvref_t<V>>> frexp(const V& value) noexcept
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
    return {fraction, exponent};
}

template<class V>
constexpr pair<remove_cvref_t<V>, remove_cvref_t<V>> modf(const V& value) noexcept
    requires(detail::is_simd_floating_value<remove_cvref_t<V>>::value) {
    using vec_type = remove_cvref_t<V>;

    vec_type fractional;
    vec_type integral;
    for (simd_size_type i = 0; i < vec_type::size; ++i) {
        typename vec_type::value_type integral_lane{};
        detail::set_lane(fractional, i, static_cast<typename vec_type::value_type>(std::modf(value[i], &integral_lane)));
        detail::set_lane(integral, i, integral_lane);
    }
    return {fractional, integral};
}

template<class A, class B>
constexpr pair<detail::binary_math_result_t<A, B>, rebind_t<int, detail::binary_math_result_t<A, B>>>
remquo(const A& left, const B& right) noexcept
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
    return {remainder_value, quotient_bits};
}

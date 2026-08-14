#pragma once

namespace detail {

template<class T>
constexpr T constexpr_math_integral_bound() noexcept {
    T result{1};
    for (int i = 0; i < numeric_limits<T>::digits; ++i) {
        result *= T{2};
    }
    return result;
}

template<class T>
constexpr T constexpr_math_trunc(T value) noexcept {
    if (value != value || value == T{}) {
        return value;
    }

    constexpr T integral_bound = constexpr_math_integral_bound<T>();
    if (value >= integral_bound || value <= -integral_bound) {
        return value;
    }
    const T integral = static_cast<T>(static_cast<long long>(value));
    return integral == T{} && value < T{} ? -T{} : integral;
}

template<class T>
using floating_bits_t = make_unsigned_t<typename integer_from_size<sizeof(T)>::type>;

template<class T>
constexpr floating_bits_t<T> floating_sign_mask() noexcept {
    return floating_bits_t<T>{1}
        << (sizeof(T) * numeric_limits<unsigned char>::digits - 1);
}

template<class T>
constexpr bool constexpr_math_signbit(T value) noexcept {
    static_assert(numeric_limits<T>::is_iec559);
    return (bit_cast<floating_bits_t<T>>(value) & floating_sign_mask<T>()) != 0;
}

template<class T>
constexpr T constexpr_math_copysign(T magnitude, T sign) noexcept {
    static_assert(numeric_limits<T>::is_iec559);
    auto bits = bit_cast<floating_bits_t<T>>(magnitude);
    bits &= ~floating_sign_mask<T>();
    if (constexpr_math_signbit(sign)) {
        bits |= floating_sign_mask<T>();
    }
    return bit_cast<T>(bits);
}

template<class T>
constexpr T math_fabs(T value) {
    if consteval {
        return constexpr_math_copysign(value, T{1});
    } else {
        return std::fabs(value);
    }
}

template<class T>
constexpr T math_ceil(T value) {
    if consteval {
        const T integral = constexpr_math_trunc(value);
        return integral < value ? integral + T{1} : integral;
    } else {
        return std::ceil(value);
    }
}

template<class T>
constexpr T math_floor(T value) {
    if consteval {
        const T integral = constexpr_math_trunc(value);
        return integral > value ? integral - T{1} : integral;
    } else {
        return std::floor(value);
    }
}

template<class T>
constexpr T math_round(T value) {
    if consteval {
        const T integral = constexpr_math_trunc(value);
        const T fraction = value - integral;
        if (fraction >= T{0.5}) {
            return integral + T{1};
        }
        if (fraction <= T{-0.5}) {
            return integral - T{1};
        }
        return integral;
    } else {
        return std::round(value);
    }
}

template<class Int, class T>
constexpr Int math_round_to_integer(T value) {
    if consteval {
        const T rounded = math_round(value);
        if (rounded < static_cast<T>(numeric_limits<Int>::lowest()) ||
            rounded > static_cast<T>(numeric_limits<Int>::max())) {
            throw "SIMD rounding result is not representable in the destination integer type";
        }
        return static_cast<Int>(rounded);
    } else {
        if constexpr (is_same<Int, long int>::value) {
            return std::lround(value);
        } else {
            return std::llround(value);
        }
    }
}

template<class T>
constexpr T math_trunc(T value) {
    if consteval {
        return constexpr_math_trunc(value);
    } else {
        return std::trunc(value);
    }
}

template<class T>
constexpr T math_copysign(T magnitude, T sign) {
    if consteval {
        return constexpr_math_copysign(magnitude, sign);
    } else {
        return std::copysign(magnitude, sign);
    }
}

template<class T>
constexpr T math_fmax(T left, T right) {
    if consteval {
        if (left != left) {
            return right;
        }
        if (right != right) {
            return left;
        }
        if (left == right) {
            return constexpr_math_signbit(left) ? right : left;
        }
        return left < right ? right : left;
    } else {
        return std::fmax(left, right);
    }
}

template<class T>
constexpr T math_fmin(T left, T right) {
    if consteval {
        if (left != left) {
            return right;
        }
        if (right != right) {
            return left;
        }
        if (left == right) {
            return constexpr_math_signbit(left) ? left : right;
        }
        return left < right ? left : right;
    } else {
        return std::fmin(left, right);
    }
}

template<class T>
constexpr int math_fpclassify(T value) {
    if consteval {
        if (value != value) {
            return FP_NAN;
        }
        const T magnitude = math_fabs(value);
        if (magnitude == numeric_limits<T>::infinity()) {
            return FP_INFINITE;
        }
        if (magnitude == T{}) {
            return FP_ZERO;
        }
        return magnitude < numeric_limits<T>::min() ? FP_SUBNORMAL : FP_NORMAL;
    } else {
        return std::fpclassify(value);
    }
}

template<class T>
constexpr bool math_signbit(T value) {
    if consteval {
        return constexpr_math_signbit(value);
    } else {
        return std::signbit(value);
    }
}

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
constexpr Result to_math_vector(const Arg& arg) {
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

template<class Result, class A, class B, class Fun>
constexpr typename Result::mask_type binary_math_mask_transform(const A& left, const B& right, Fun fun) {
    const Result lhs = to_math_vector<Result>(left);
    const Result rhs = to_math_vector<Result>(right);
    typename Result::mask_type result;
    for (simd_size_type i = 0; i < Result::size; ++i) {
        detail::set_lane(result, i, static_cast<bool>(fun(lhs[i], rhs[i])));
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

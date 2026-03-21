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

#pragma once

namespace detail::special_math {

template<class T>
constexpr T euler_gamma_v = static_cast<T>(0.57721566490153286060651209008240243104L);

template<class T>
constexpr T pi_v = static_cast<T>(3.141592653589793238462643383279502884L);

template<class T>
T quiet_nan() noexcept {
    if constexpr (numeric_limits<T>::has_quiet_NaN) {
        return numeric_limits<T>::quiet_NaN();
    } else {
        return T{};
    }
}

template<class T>
T infinity() noexcept {
    if constexpr (numeric_limits<T>::has_infinity) {
        return numeric_limits<T>::infinity();
    } else {
        return numeric_limits<T>::max();
    }
}

template<class T>
bool almost_equal(T left, T right, T tolerance = static_cast<T>(1e-12L)) noexcept {
    return std::abs(left - right) <= tolerance;
}

template<class T>
bool almost_integer(T value, T tolerance = static_cast<T>(1e-12L)) noexcept {
    return almost_equal(value, std::round(value), tolerance);
}

template<class Fun>
long double simpson_integral(Fun&& fun, long double a, long double b) {
    const long double mid = (a + b) / 2.0L;
    return (b - a) * (fun(a) + 4.0L * fun(mid) + fun(b)) / 6.0L;
}

template<class Fun>
long double adaptive_simpson_integral(Fun&& fun,
                                      long double a,
                                      long double b,
                                      long double epsilon,
                                      long double whole,
                                      int depth) {
    const long double mid = (a + b) / 2.0L;
    const long double left = simpson_integral(fun, a, mid);
    const long double right = simpson_integral(fun, mid, b);
    const long double delta = left + right - whole;

    if (depth <= 0 || std::abs(delta) <= 15.0L * epsilon) {
        return left + right + delta / 15.0L;
    }

    return adaptive_simpson_integral(fun, a, mid, epsilon / 2.0L, left, depth - 1) +
        adaptive_simpson_integral(fun, mid, b, epsilon / 2.0L, right, depth - 1);
}

template<class Fun>
long double adaptive_simpson_integral(Fun&& fun, long double a, long double b) {
    if (a == b) {
        return 0.0L;
    }

    const long double whole = simpson_integral(fun, a, b);
    const long double scale = std::max(1.0L, std::abs(whole));
    const long double tolerance = std::max(
        1e-12L * scale,
        64.0L * std::numeric_limits<long double>::epsilon() * scale);
    return adaptive_simpson_integral(
        fun, a, b, tolerance, whole, 18);
}

struct checked_integral_result {
    long double value;
    long double error;
    bool converged;
};

template<class Fun>
auto checked_simpson_integral(
    Fun&& fun,
    long double a,
    long double b,
    long double tolerance,
    long double whole,
    unsigned depth) -> checked_integral_result {
    const long double mid = (a + b) / 2.0L;
    const long double left = simpson_integral(fun, a, mid);
    const long double right = simpson_integral(fun, mid, b);
    const long double delta = left + right - whole;
    const long double error = std::abs(delta) / 15.0L;
    const long double corrected = left + right + delta / 15.0L;
    if (error <= tolerance) {
        return {corrected, error, true};
    }
    if (depth == 0u) {
        return {corrected, error, false};
    }

    const auto left_result = checked_simpson_integral(
        fun, a, mid, tolerance / 2.0L, left, depth - 1u);
    const auto right_result = checked_simpson_integral(
        fun, mid, b, tolerance / 2.0L, right, depth - 1u);
    return {
        left_result.value + right_result.value,
        left_result.error + right_result.error,
        left_result.converged && right_result.converged};
}

template<class Fun>
auto checked_simpson_integral(
    Fun&& fun,
    long double a,
    long double b,
    long double tolerance,
    unsigned depth = 18u) -> checked_integral_result {
    if (a == b) {
        return {0.0L, 0.0L, true};
    }
    const long double whole = simpson_integral(fun, a, b);
    return checked_simpson_integral(
        fun, a, b, tolerance, whole, depth);
}

template<class Fun>
auto segmented_checked_simpson_integral(
    Fun&& fun,
    long double a,
    long double b,
    unsigned segments,
    long double tolerance) -> checked_integral_result {
    checked_integral_result result{0.0L, 0.0L, true};
    for (unsigned i = 0u; i < segments; ++i) {
        const long double left =
            a + (b - a) * static_cast<long double>(i) /
                static_cast<long double>(segments);
        const long double right =
            a + (b - a) * static_cast<long double>(i + 1u) /
                static_cast<long double>(segments);
        const auto part = checked_simpson_integral(
            fun,
            left,
            right,
            tolerance / static_cast<long double>(segments));
        result.value += part.value;
        result.error += part.error;
        result.converged = result.converged && part.converged;
    }
    return result;
}

template<class T>
T cyl_bessel_j_fallback(T nu, T x);

} // namespace detail::special_math

#pragma once

namespace detail::special_math {

struct polynomial_scaled_value {
    long double fraction;
    int exponent;
};

inline auto polynomial_scaled(long double value, int exponent = 0)
    -> polynomial_scaled_value {
    if (value == 0.0L || !std::isfinite(value)) {
        return {value, 0};
    }
    int shift = 0;
    const long double fraction = std::frexp(value, &shift);
    return {fraction, exponent + shift};
}

inline auto polynomial_scaled_product(
    polynomial_scaled_value left, polynomial_scaled_value right)
    -> polynomial_scaled_value {
    return polynomial_scaled(
        left.fraction * right.fraction, left.exponent + right.exponent);
}

inline auto polynomial_scaled_difference(
    polynomial_scaled_value left, polynomial_scaled_value right)
    -> polynomial_scaled_value {
    if (left.fraction == 0.0L) {
        return polynomial_scaled(-right.fraction, right.exponent);
    }
    if (right.fraction == 0.0L) {
        return left;
    }
    const int exponent = std::max(left.exponent, right.exponent);
    const long double a = std::ldexp(left.fraction, left.exponent - exponent);
    const long double b = std::ldexp(right.fraction, right.exponent - exponent);
    return polynomial_scaled(a - b, exponent);
}

// Keep each recurrence term scaled, including coefficients and seeds. This
// does not depend on long double having a wider exponent range than double.
inline auto polynomial_scaled_next(
    polynomial_scaled_value previous, polynomial_scaled_value current,
    polynomial_scaled_value first, long double second, long double divisor = 1.0L)
    -> polynomial_scaled_value {
    const auto difference = polynomial_scaled_difference(
        polynomial_scaled_product(first, current),
        polynomial_scaled_product(polynomial_scaled(second), previous));
    return polynomial_scaled(difference.fraction / divisor, difference.exponent);
}

template<class T>
T polynomial_unscale(polynomial_scaled_value value) {
    if (!std::isfinite(value.fraction)) {
        return static_cast<T>(value.fraction);
    }
    int max_exponent = 0;
    const long double max_fraction = std::frexp(
        static_cast<long double>(std::numeric_limits<T>::max()), &max_exponent);
    if (value.exponent > max_exponent ||
        (value.exponent == max_exponent && std::abs(value.fraction) > max_fraction)) {
        return std::copysign(infinity<T>(), static_cast<T>(value.fraction));
    }
    return static_cast<T>(std::ldexp(value.fraction, value.exponent));
}

inline auto polynomial_laguerre_scaled(unsigned n, unsigned m, long double x)
    -> polynomial_scaled_value {
    auto previous = polynomial_scaled(1.0L);
    auto current = polynomial_scaled(static_cast<long double>(m + 1u) - x);
    for (unsigned i = 1; i < n; ++i) {
        const auto next = polynomial_scaled_next(
            previous, current,
            polynomial_scaled(static_cast<long double>(2u * i + m + 1u) - x),
            static_cast<long double>(i + m), static_cast<long double>(i + 1u));
        previous = current;
        current = next;
    }
    return current;
}

template<class T>
T hermite_fallback(unsigned n, T x) {
    if (std::isnan(x)) {
        return x;
    }
    if (n >= recurrence_order_limit) {
        return quiet_nan<T>();
    }
    if (n == 0u) {
        return T{1};
    }
    if (std::isinf(x)) {
        return (n & 1u) != 0u ? x : infinity<T>();
    }
    if (n == 1u) {
        return static_cast<T>(2) * x;
    }

    const auto twice_x = polynomial_scaled_product(
        polynomial_scaled(2.0L), polynomial_scaled(static_cast<long double>(x)));
    auto hm2 = polynomial_scaled(1.0L);
    auto hm1 = twice_x;
    for (unsigned i = 1; i < n; ++i) {
        const auto next = polynomial_scaled_next(
            hm2, hm1, twice_x, static_cast<long double>(2u * i));
        hm2 = hm1;
        hm1 = next;
    }
    return polynomial_unscale<T>(hm1);
}

template<class T>
T laguerre_fallback(unsigned n, T x) {
    if (std::isnan(x)) {
        return x;
    }
    if (n >= recurrence_order_limit) {
        return quiet_nan<T>();
    }
    if (n == 0u) {
        return T{1};
    }
    if (std::isinf(x) && x > T{}) {
        return (n & 1u) != 0u ? -x : x;
    }
    if (n == 1u) {
        return T{1} - x;
    }

    return polynomial_unscale<T>(polynomial_laguerre_scaled(n, 0u, static_cast<long double>(x)));
}

template<class T>
T legendre_fallback(unsigned n, T x) {
    if (std::isnan(x)) {
        return x;
    }
    if (n >= recurrence_order_limit) {
        return quiet_nan<T>();
    }
    if (std::abs(x) > T{1}) {
        return quiet_nan<T>();
    }
    if (n == 0u) {
        return T{1};
    }
    if (n == 1u) {
        return x;
    }

    T pm2 = T{1};
    T pm1 = x;
    for (unsigned i = 1; i < n; ++i) {
        const T next = (static_cast<T>(2 * i + 1) * x * pm1 - static_cast<T>(i) * pm2) / static_cast<T>(i + 1);
        pm2 = pm1;
        pm1 = next;
    }
    return pm1;
}

template<class T>
T assoc_laguerre_fallback(unsigned n, unsigned m, T x) {
    if (std::isnan(x)) {
        return x;
    }
    if (n >= recurrence_order_limit || m >= recurrence_order_limit) {
        return quiet_nan<T>();
    }
    if (n == 0u) {
        return T{1};
    }
    if (std::isinf(x) && x > T{}) {
        return (n & 1u) != 0u ? -x : x;
    }
    if (n == 1u) {
        return static_cast<T>(m + 1) - x;
    }

    return polynomial_unscale<T>(polynomial_laguerre_scaled(n, m, static_cast<long double>(x)));
}

template<class T>
T assoc_legendre_fallback(unsigned l, unsigned m, T x) {
    if (std::isnan(x)) {
        return x;
    }
    if (std::abs(x) > T{1}) {
        return quiet_nan<T>();
    }
    if (m > l) {
        return T{};
    }
    if (l >= recurrence_order_limit) {
        return quiet_nan<T>();
    }

    auto pmm = polynomial_scaled(1.0L);
    if (m > 0u) {
        const long double argument = static_cast<long double>(x);
        const long double root = std::sqrt(std::max(0.0L, 1.0L - argument * argument));
        const auto scaled_root = polynomial_scaled(root);
        for (unsigned i = 1; i <= m; ++i) {
            pmm = polynomial_scaled_product(pmm, polynomial_scaled_product(
                polynomial_scaled(static_cast<long double>(2u * i - 1u)), scaled_root));
        }
    }

    if (l == m) {
        return polynomial_unscale<T>(pmm);
    }

    const auto scaled_x = polynomial_scaled(static_cast<long double>(x));
    auto pmmp1 = polynomial_scaled_product(pmm, polynomial_scaled_product(
        polynomial_scaled(static_cast<long double>(2u * m + 1u)), scaled_x));
    if (l == m + 1u) {
        return polynomial_unscale<T>(pmmp1);
    }

    for (unsigned ll = m + 2u; ll <= l; ++ll) {
        const auto pll = polynomial_scaled_next(
            pmm, pmmp1,
            polynomial_scaled_product(polynomial_scaled(static_cast<long double>(2u * ll - 1u)), scaled_x),
            static_cast<long double>(ll + m - 1u), static_cast<long double>(ll - m));
        pmm = pmmp1;
        pmmp1 = pll;
    }
    return polynomial_unscale<T>(pmmp1);
}

template<class T>
T sph_legendre_fallback(unsigned l, unsigned m, T theta) {
    if (std::isnan(theta)) {
        return theta;
    }
    if (m > l) {
        return quiet_nan<T>();
    }
    if (l >= recurrence_order_limit) {
        return quiet_nan<T>();
    }

    using wide_t = conditional_t<(sizeof(T) < sizeof(double)), double, long double>;
    const wide_t angle = static_cast<wide_t>(theta);
    const wide_t x = std::cos(angle);
    const wide_t sine = std::abs(std::sin(angle));

    wide_t current = wide_t{1} /
        std::sqrt(wide_t{4} * pi_v<wide_t>);
    for (unsigned order = 1u; order <= m; ++order) {
        const wide_t order_value = static_cast<wide_t>(order);
        current *= -std::sqrt(
            (wide_t{2} * order_value + wide_t{1}) /
            (wide_t{2} * order_value)) * sine;
    }
    if (l == m) {
        return static_cast<T>(current);
    }

    wide_t previous = current;
    current = std::sqrt(static_cast<wide_t>(2u * m + 3u)) * x * current;
    if (l == m + 1u) {
        return static_cast<T>(current);
    }

    const wide_t order = static_cast<wide_t>(m);
    for (unsigned degree = m + 2u; degree <= l; ++degree) {
        const wide_t degree_value = static_cast<wide_t>(degree);
        const wide_t denominator =
            degree_value * degree_value - order * order;
        const wide_t first = std::sqrt(
            (wide_t{4} * degree_value * degree_value - wide_t{1}) /
            denominator);
        const wide_t second = std::sqrt(
            ((wide_t{2} * degree_value + wide_t{1}) *
             ((degree_value - wide_t{1}) * (degree_value - wide_t{1}) -
              order * order)) /
            ((wide_t{2} * degree_value - wide_t{3}) * denominator));
        const wide_t next = first * x * current - second * previous;
        previous = current;
        current = next;
    }
    return static_cast<T>(current);
}

} // namespace detail::special_math

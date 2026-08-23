#pragma once

namespace detail::special_math {

template<class T>
T hermite_fallback(unsigned n, T x) {
    if (std::isnan(x)) {
        return x;
    }
    if (n == 0u) {
        return T{1};
    }
    if (n == 1u) {
        return static_cast<T>(2) * x;
    }

    T hm2 = T{1};
    T hm1 = static_cast<T>(2) * x;
    for (unsigned i = 1; i < n; ++i) {
        const T next = static_cast<T>(2) * x * hm1 - static_cast<T>(2 * i) * hm2;
        hm2 = hm1;
        hm1 = next;
    }
    return hm1;
}

template<class T>
T laguerre_fallback(unsigned n, T x) {
    if (std::isnan(x)) {
        return x;
    }
    if (n == 0u) {
        return T{1};
    }
    if (n == 1u) {
        return T{1} - x;
    }

    T lm2 = T{1};
    T lm1 = T{1} - x;
    for (unsigned i = 1; i < n; ++i) {
        const T next = ((static_cast<T>(2 * i + 1) - x) * lm1 - static_cast<T>(i) * lm2) / static_cast<T>(i + 1);
        lm2 = lm1;
        lm1 = next;
    }
    return lm1;
}

template<class T>
T legendre_fallback(unsigned n, T x) {
    if (std::isnan(x)) {
        return x;
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
    if (n == 0u) {
        return T{1};
    }
    if (n == 1u) {
        return static_cast<T>(m + 1) - x;
    }

    T lm2 = T{1};
    T lm1 = static_cast<T>(m + 1) - x;
    for (unsigned i = 1; i < n; ++i) {
        const T next =
            ((static_cast<T>(2 * i + m + 1) - x) * lm1 - static_cast<T>(i + m) * lm2) / static_cast<T>(i + 1);
        lm2 = lm1;
        lm1 = next;
    }
    return lm1;
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

    T pmm = T{1};
    if (m > 0u) {
        const T one_minus_x2 = std::max(T{}, T{1} - x * x);
        const T root = std::sqrt(one_minus_x2);
        T factor = T{1};
        for (unsigned i = 1; i <= m; ++i) {
            pmm *= factor * root;
            factor += T{2};
        }
    }

    if (l == m) {
        return pmm;
    }

    T pmmp1 = static_cast<T>(2 * m + 1) * x * pmm;
    if (l == m + 1u) {
        return pmmp1;
    }

    T pll = T{};
    for (unsigned ll = m + 2u; ll <= l; ++ll) {
        pll = (static_cast<T>(2 * ll - 1) * x * pmmp1 - static_cast<T>(ll + m - 1) * pmm) /
            static_cast<T>(ll - m);
        pmm = pmmp1;
        pmmp1 = pll;
    }
    return pll;
}

template<class T>
T sph_legendre_fallback(unsigned l, unsigned m, T theta) {
    if (std::isnan(theta)) {
        return theta;
    }
    if (m > l) {
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

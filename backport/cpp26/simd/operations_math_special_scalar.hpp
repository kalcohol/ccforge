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

template<class T>
T beta_fallback(T x, T y) {
    if (!(x > T{}) || !(y > T{})) {
        return quiet_nan<T>();
    }
    if (x == T{1}) {
        return T{1} / y;
    }
    if (y == T{1}) {
        return T{1} / x;
    }

    using wide_t = conditional_t<(sizeof(T) < sizeof(double)), double, long double>;
    const wide_t a = std::max(
        static_cast<wide_t>(x), static_cast<wide_t>(y));
    const wide_t b = std::min(
        static_cast<wide_t>(x), static_cast<wide_t>(y));

    wide_t log_value;
    if (a <= static_cast<wide_t>(1.0e6L)) {
        log_value = std::lgamma(a) + std::lgamma(b) - std::lgamma(a + b);
    } else {
        const wide_t ratio = b / a;
        const wide_t log_ratio_sum = std::log1p(ratio);
        const wide_t log_a = std::log(a);
        const wide_t log_sum = log_a + log_ratio_sum;
        const auto correction = [](wide_t inverse) {
            const wide_t inverse2 = inverse * inverse;
            return inverse *
                (wide_t{1} / wide_t{12} -
                 inverse2 / wide_t{360} +
                 inverse2 * inverse2 / wide_t{1260});
        };
        const wide_t inverse_a = wide_t{1} / a;
        const wide_t inverse_sum = inverse_a / (wide_t{1} + ratio);
        const wide_t scaled_log_ratio_sum =
            ratio == wide_t{} ? b : a * log_ratio_sum;

        if (b <= static_cast<wide_t>(1.0e6L)) {
            log_value =
                std::lgamma(b) - scaled_log_ratio_sum +
                wide_t{0.5} * log_ratio_sum - b * log_sum + b +
                correction(inverse_a) - correction(inverse_sum);
        } else {
            const wide_t inverse_b = wide_t{1} / b;
            log_value =
                -scaled_log_ratio_sum +
                b * (std::log(ratio) - log_ratio_sum) +
                wide_t{0.5} *
                    (log_sum - log_a - std::log(b) +
                     std::log(wide_t{2} * pi_v<wide_t>)) +
                correction(inverse_a) + correction(inverse_b) -
                correction(inverse_sum);
        }
    }

    const wide_t max_log = std::log(
        static_cast<wide_t>(std::numeric_limits<T>::max()));
    const wide_t min_log = std::log(
        static_cast<wide_t>(std::numeric_limits<T>::denorm_min()));
    if (log_value > max_log) {
        return infinity<T>();
    }
    if (log_value < min_log) {
        return T{};
    }
    return static_cast<T>(std::exp(log_value));
}

template<class T>
T hermite_fallback(unsigned n, T x) {
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
    if (m > l) {
        return quiet_nan<T>();
    }

    using wide_t = conditional_t<(sizeof(T) < sizeof(double)), double, long double>;
    const wide_t x = std::cos(static_cast<wide_t>(theta));
    const wide_t sine = std::sqrt(std::max(wide_t{}, wide_t{1} - x * x));

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

template<class T>
T sph_bessel_fallback(unsigned n, T x) {
    if (x == T{}) {
        return n == 0u ? T{1} : T{};
    }

    const long double argument =
        std::abs(static_cast<long double>(x));
    const long double cylindrical = cyl_bessel_j_fallback(
        static_cast<long double>(n) + 0.5L,
        argument);
    long double result =
        std::sqrt(pi_v<long double> / (2.0L * argument)) *
        cylindrical;
    if (x < T{} && (n & 1u) != 0u) {
        result = -result;
    }
    return static_cast<T>(result);
}

template<class T>
T sph_neumann_fallback(unsigned n, T x) {
    if (x == T{}) {
        return -infinity<T>();
    }

    const T y0 = -std::cos(x) / x;
    if (n == 0u) {
        return y0;
    }

    T ym2 = y0;
    T ym1 = -std::cos(x) / (x * x) - std::sin(x) / x;
    if (n == 1u) {
        return ym1;
    }

    for (unsigned i = 1; i < n; ++i) {
        const T next = (static_cast<T>(2 * i + 1) / x) * ym1 - ym2;
        ym2 = ym1;
        ym1 = next;
    }
    return ym1;
}

template<class T>
T expint_fallback(T x) {
    if (std::isnan(x)) {
        return quiet_nan<T>();
    }
    if (std::isinf(x)) {
        return x > T{} ? infinity<T>() : std::copysign(T{}, T{-1});
    }
    if (x == T{}) {
        return -infinity<T>();
    }

    constexpr T negative_series_cutoff = static_cast<T>(6);
    constexpr T positive_series_cutoff = static_cast<T>(40);
    constexpr T epsilon = static_cast<T>(1e-15L);

    if (x < -negative_series_cutoff) {
        const T positive_x = -x;
        T b = positive_x + T{1};
        T c = T{1} / std::numeric_limits<T>::min();
        T d = T{1} / b;
        T fraction = d;
        for (unsigned k = 1; k < 256u; ++k) {
            const T kt = static_cast<T>(k);
            const T a = -(kt * kt);
            b += T{2};
            d = T{1} / (a * d + b);
            c = b + a / c;
            const T delta = c * d;
            fraction *= delta;
            if (std::abs(delta - T{1}) <= epsilon) {
                break;
            }
        }
        return -std::exp(-positive_x) * fraction;
    }

    if (x <= positive_series_cutoff) {
        using wide_t = conditional_t<(sizeof(T) < sizeof(double)), double, long double>;
        const wide_t wide_x = static_cast<wide_t>(x);
        constexpr wide_t series_epsilon = static_cast<wide_t>(1e-18L);
        wide_t sum = wide_t{};
        wide_t term = wide_x;
        for (unsigned k = 1; k < 512u; ++k) {
            const wide_t add = term / static_cast<wide_t>(k);
            sum += add;
            term *= wide_x / static_cast<wide_t>(k + 1u);
            if (std::abs(add) <=
                series_epsilon * std::max(wide_t{1}, std::abs(sum))) {
                break;
            }
        }
        return static_cast<T>(
            euler_gamma_v<wide_t> + std::log(std::abs(wide_x)) + sum);
    }

    using wide_t = conditional_t<(sizeof(T) < sizeof(double)), double, long double>;
    const wide_t wide_x = static_cast<wide_t>(x);
    wide_t sum = wide_t{1};
    wide_t term = wide_t{1};
    wide_t previous_magnitude = infinity<wide_t>();
    for (unsigned k = 1; k < 256u; ++k) {
        term *= static_cast<wide_t>(k) / wide_x;
        const wide_t magnitude = std::abs(term);
        if (magnitude >= previous_magnitude) {
            break;
        }
        sum += term;
        if (magnitude <=
            static_cast<wide_t>(epsilon) *
                std::max(wide_t{1}, std::abs(sum))) {
            break;
        }
        previous_magnitude = magnitude;
    }

    const wide_t log_value =
        wide_x + std::log(sum / wide_x);
    const wide_t max_log = std::log(
        static_cast<wide_t>(std::numeric_limits<T>::max()));
    if (log_value > max_log) {
        return infinity<T>();
    }
    return static_cast<T>(std::exp(log_value));
}

template<class T>
T riemann_zeta_hasse(T s) {
    T sum = T{};
    for (unsigned n = 0; n < 128u; ++n) {
        T inner = T{};
        T binom = T{1};
        for (unsigned k = 0; k <= n; ++k) {
            if (k > 0u) {
                binom *= static_cast<T>(n - k + 1u) / static_cast<T>(k);
            }
            const T term = binom / std::pow(static_cast<T>(k + 1u), s);
            inner += (k % 2u == 0u) ? term : -term;
        }

        const T outer = inner / std::ldexp(T{1}, static_cast<int>(n + 1u));
        sum += outer;
        if (std::abs(outer) <= static_cast<T>(1e-15L) * std::max(T{1}, std::abs(sum))) {
            break;
        }
    }

    return sum / (T{1} - std::pow(T{2}, T{1} - s));
}

template<class T>
T riemann_zeta_fallback(T s) {
    if (std::isnan(s)) {
        return quiet_nan<T>();
    }
    if (std::isinf(s)) {
        return s > T{} ? T{1} : quiet_nan<T>();
    }
    if (s == T{1}) {
        return infinity<T>();
    }
    if (s == T{0}) {
        return static_cast<T>(-0.5L);
    }
    if (s < T{}) {
        const T rounded = std::round(s);
        const T half = rounded / T{2};
        if (s == rounded && std::trunc(half) == half) {
            return T{};
        }

        using wide_t = conditional_t<(sizeof(T) < sizeof(double)), double, long double>;
        const wide_t wide_s = static_cast<wide_t>(s);
        const wide_t sine =
            std::sin(pi_v<wide_t> * wide_s / wide_t{2});
        if (sine == wide_t{}) {
            return T{};
        }
        const wide_t reflected = riemann_zeta_hasse(wide_t{1} - wide_s);
        const wide_t log_magnitude =
            wide_s * std::log(wide_t{2}) +
            (wide_s - wide_t{1}) * std::log(pi_v<wide_t>) +
            std::lgamma(wide_t{1} - wide_s) +
            std::log(std::abs(sine)) + std::log(reflected);
        const wide_t max_log = std::log(
            static_cast<wide_t>(std::numeric_limits<T>::max()));
        const wide_t min_log = std::log(
            static_cast<wide_t>(std::numeric_limits<T>::denorm_min()));
        if (log_magnitude > max_log) {
            return std::copysign(infinity<T>(), static_cast<T>(sine));
        }
        if (log_magnitude < min_log) {
            return std::copysign(T{}, static_cast<T>(sine));
        }
        return std::copysign(
            static_cast<T>(std::exp(log_magnitude)),
            static_cast<T>(sine));
    }

    return riemann_zeta_hasse(s);
}

template<class T, class Fun>
T elliptic_integral(T upper, Fun&& integrand) {
    if (upper == T{}) {
        return T{};
    }

    const T sign = upper < T{} ? T{-1} : T{1};
    const long double bound = std::abs(static_cast<long double>(upper));
    constexpr long double period = pi_v<long double>;
    const long double full_periods = std::floor(bound / period);
    const long double remainder = std::fmod(bound, period);
    const auto wide_integrand = [&](long double theta) {
            return static_cast<long double>(integrand(static_cast<T>(theta)));
        };

    long double integral = 0.0L;
    if (full_periods > 0.0L) {
        integral += full_periods * adaptive_simpson_integral(
            wide_integrand, 0.0L, period);
    }
    if (remainder > 0.0L) {
        integral += adaptive_simpson_integral(
            wide_integrand, 0.0L, remainder);
    }
    return sign * static_cast<T>(integral);
}

template<class T>
T complete_ellint_1_agm(T k) {
    if (std::isnan(k)) {
        return quiet_nan<T>();
    }

    using wide_t = conditional_t<(sizeof(T) < sizeof(double)), double, long double>;
    const wide_t modulus = std::abs(static_cast<wide_t>(k));
    if (modulus > wide_t{1}) {
        return quiet_nan<T>();
    }
    if (modulus == wide_t{1}) {
        return infinity<T>();
    }

    wide_t arithmetic = wide_t{1};
    wide_t geometric = std::sqrt((wide_t{1} - modulus) * (wide_t{1} + modulus));
    for (unsigned iteration = 0; iteration < 128u; ++iteration) {
        const wide_t difference = arithmetic - geometric;
        if (std::abs(difference) <=
            8 * std::numeric_limits<wide_t>::epsilon() * arithmetic) {
            break;
        }

        const wide_t next_arithmetic = (arithmetic + geometric) / wide_t{2};
        geometric = std::sqrt(arithmetic * geometric);
        arithmetic = next_arithmetic;
    }

    return static_cast<T>(pi_v<wide_t> / (wide_t{2} * arithmetic));
}

template<class T>
T ellint_1_fallback(T k, T phi) {
    if (std::isnan(k) || std::isnan(phi) || std::abs(k) > T{1}) {
        return quiet_nan<T>();
    }
    if (phi == T{}) {
        return phi;
    }

    const T half_pi = pi_v<T> / T{2};
    // At |k| == 1 the integrand is 1/|cos(theta)|, so F diverges as soon
    // as the integration path reaches theta == pi/2; a numeric quadrature
    // across that pole would return finite garbage instead.
    if (std::abs(k) == T{1} && std::abs(phi) >= half_pi) {
        return std::copysign(infinity<T>(), phi);
    }
    if (std::abs(phi) == half_pi) {
        return std::copysign(complete_ellint_1_agm(k), phi);
    }

    return elliptic_integral(phi, [&](T theta) {
        const T s = std::sin(theta);
        const T radicand = T{1} - k * k * s * s;
        if (radicand <= T{}) {
            return infinity<T>();
        }
        return T{1} / std::sqrt(radicand);
    });
}

template<class T>
T ellint_2_fallback(T k, T phi) {
    if (std::isnan(k) || std::isnan(phi) || std::abs(k) > T{1}) {
        return quiet_nan<T>();
    }
    return elliptic_integral(phi, [&](T theta) {
        const T s = std::sin(theta);
        const T radicand = T{1} - k * k * s * s;
        if (radicand < T{}) {
            return quiet_nan<T>();
        }
        return std::sqrt(radicand);
    });
}

template<class T>
T ellint_3_fallback(T k, T nu, T phi) {
    if (std::isnan(k) || std::isnan(nu) || std::isnan(phi) ||
        std::abs(k) > T{1}) {
        return quiet_nan<T>();
    }
    // Same divergence as ellint_1: at |k| == 1 the 1/sqrt(1 - k^2 sin^2)
    // factor behaves like 1/|cos(theta)|, so the integral diverges once the
    // path reaches theta == pi/2. For nu <= 1 the pole factor stays
    // nonnegative on the way there, making the divergence +inf; quadrature
    // across it would return finite garbage. (For nu > 1 the nu-pole is hit
    // first and the existing infinite-sample handling applies.)
    if (std::abs(k) == T{1} && nu <= T{1} &&
        std::abs(phi) >= pi_v<T> / T{2}) {
        return std::copysign(infinity<T>(), phi);
    }
    return elliptic_integral(phi, [&](T theta) {
        const T s = std::sin(theta);
        const T sin2 = s * s;
        const T radicand = T{1} - k * k * sin2;
        const T pole = T{1} - nu * sin2;
        if (radicand <= T{} || pole == T{}) {
            return infinity<T>();
        }
        return T{1} / (pole * std::sqrt(radicand));
    });
}

template<class T>
T comp_ellint_1_fallback(T k) {
    return complete_ellint_1_agm(k);
}

template<class T>
T comp_ellint_2_fallback(T k) {
    return ellint_2_fallback(k, pi_v<T> / T{2});
}

template<class T>
T comp_ellint_3_fallback(T k, T nu) {
    return ellint_3_fallback(k, nu, pi_v<T> / T{2});
}

struct cyl_bessel_series_result {
    long double value;
    long double error;
    bool converged;
};

inline auto cyl_bessel_power_series(
    long double nu,
    long double x,
    bool alternating)
    -> cyl_bessel_series_result {
    if (x < 0.0L || !std::isfinite(nu) || !std::isfinite(x)) {
        return {
            quiet_nan<long double>(),
            infinity<long double>(),
            false};
    }
    if (x == 0.0L) {
        // Exact-zero order discrimination, matching the J fallback: the
        // order-zero limit is 1 only for nu == 0, not for tiny nonzero nu.
        return {
            nu == 0.0L ? 1.0L : 0.0L,
            0.0L,
            true};
    }

    const long double half_x = x / 2.0L;
    const long double gamma_argument = nu + 1.0L;
    const long double log_gamma = std::lgamma(gamma_argument);
    if (!std::isfinite(log_gamma)) {
        return {
            quiet_nan<long double>(),
            infinity<long double>(),
            false};
    }
    long double gamma_sign = 1.0L;
    if (gamma_argument < 0.0L) {
        const long double sine =
            std::sin(pi_v<long double> * gamma_argument);
        if (sine == 0.0L) {
            return {
                quiet_nan<long double>(),
                infinity<long double>(),
                false};
        }
        gamma_sign = std::copysign(1.0L, sine);
    }

    const long double log_term =
        nu * std::log(half_x) - log_gamma;
    const long double max_log =
        std::log(std::numeric_limits<long double>::max());
    const long double min_log =
        std::log(std::numeric_limits<long double>::denorm_min());
    if (log_term > max_log) {
        return {
            std::copysign(infinity<long double>(), gamma_sign),
            infinity<long double>(),
            false};
    }

    long double term =
        log_term < min_log ? 0.0L : std::exp(log_term);
    term = std::copysign(term, gamma_sign);
    long double sum = term;
    long double compensation = 0.0L;
    long double absolute_sum = std::abs(term);
    bool converged = term == 0.0L;

    for (unsigned k = 1u; k <= 4096u && !converged; ++k) {
        const long double divisor =
            static_cast<long double>(k) *
            (nu + static_cast<long double>(k));
        if (divisor == 0.0L) {
            return {
                quiet_nan<long double>(),
                infinity<long double>(),
                false};
        }

        term *= (alternating ? -1.0L : 1.0L) *
            (half_x * half_x) / divisor;
        const long double adjusted = term - compensation;
        const long double updated = sum + adjusted;
        compensation = (updated - sum) - adjusted;
        sum = updated;
        absolute_sum += std::abs(term);

        const long double truncation = std::abs(term);
        converged =
            term == 0.0L ||
            (sum != 0.0L &&
             truncation <=
                 8.0L * std::numeric_limits<long double>::epsilon() *
                     std::abs(sum));
    }

    const long double error =
        std::abs(term) +
        8.0L * std::numeric_limits<long double>::epsilon() *
            absolute_sum;
    return {sum, error, converged};
}

template<class T>
T cyl_bessel_j_series(T nu, T x) {
    const auto result = cyl_bessel_power_series(
        static_cast<long double>(nu),
        static_cast<long double>(x),
        true);
    return static_cast<T>(result.value);
}

template<class T>
T cyl_bessel_i_series(T nu, T x) {
    if (std::isinf(x) && x > T{} && std::isfinite(nu)) {
        return infinity<T>();
    }
    const auto result = cyl_bessel_power_series(
        static_cast<long double>(nu),
        static_cast<long double>(x),
        false);
    if (result.converged) {
        return static_cast<T>(result.value);
    }
    // For x > 0 the series tail is eventually all positive, so an
    // unconverged partial sum is a lower bound of the true value. Once
    // that bound already exceeds T's finite range, +inf is the exact
    // T-representable answer rather than a guess; only in-range
    // nonconvergence remains a genuine NaN failure.
    if (x > T{} &&
        result.value >
            static_cast<long double>(std::numeric_limits<T>::max())) {
        return infinity<T>();
    }
    return quiet_nan<T>();
}

template<class T>
struct cyl_bessel_hankel_result {
    T j;
    T y;
    T error;
    bool converged;
};

template<class T>
auto cyl_bessel_hankel_asymptotic(
    T nu,
    T x,
    T tolerance = std::numeric_limits<T>::epsilon())
    -> cyl_bessel_hankel_result<T> {
    const T mu = T{4} * nu * nu;
    T p = T{1};
    T q = T{};
    T term = T{1};
    T previous = infinity<T>();
    T error = infinity<T>();
    bool converged = false;

    // Build the shared Hankel P/Q coefficient series and stop at its least
    // term. Callers must not use a result that did not reach their requested
    // tolerance: this series is asymptotic rather than convergent.
    for (unsigned k = 1; k <= 64u; ++k) {
        const T odd = static_cast<T>(2u * k - 1u);
        const T next = term * (mu - odd * odd) /
            (static_cast<T>(8u * k) * x);
        const T magnitude = std::abs(next);
        if (k > 1u && magnitude > previous) {
            break;
        }

        term = next;
        if ((k & 1u) == 0u) {
            const bool subtract = ((k / 2u) & 1u) != 0u;
            p += subtract ? -term : term;
        } else {
            const bool subtract = (((k - 1u) / 2u) & 1u) != 0u;
            q += subtract ? -term : term;
        }

        error = magnitude;
        if (magnitude <= tolerance *
                std::max(T{1}, std::abs(p) + std::abs(q))) {
            converged = true;
            break;
        }
        previous = magnitude;
    }

    const T phase = x - nu * pi_v<T> / T{2} - pi_v<T> / T{4};
    const T scale = std::sqrt(T{2} / (pi_v<T> * x));
    const T cos_phase = std::cos(phase);
    const T sin_phase = std::sin(phase);
    return {
        scale * (cos_phase * p - sin_phase * q),
        scale * (sin_phase * p + cos_phase * q),
        error,
        converged};
}

template<class T>
constexpr long double cyl_bessel_target_tolerance() noexcept {
    if constexpr (std::numeric_limits<T>::digits <= 24) {
        return 2.0e-6L;
    } else if constexpr (std::numeric_limits<T>::digits <= 53) {
        return 2.0e-12L;
    } else {
        return 2.0e-15L;
    }
}

inline auto cyl_bessel_integer_base_series(long double x)
    -> std::array<long double, 4> {
    const auto j0 = cyl_bessel_power_series(0.0L, x, true);
    const auto j1 = cyl_bessel_power_series(1.0L, x, true);
    const long double z = x * x / 4.0L;
    long double j0_term = 1.0L;
    long double harmonic = 0.0L;
    long double correction = 0.0L;
    long double derivative = 0.0L;

    for (unsigned k = 1u; k <= 4096u; ++k) {
        const long double order = static_cast<long double>(k);
        j0_term *= -z / (order * order);
        harmonic += 1.0L / order;
        const long double component = -harmonic * j0_term;
        correction += component;
        derivative += component * 2.0L * order / x;
        if (component == 0.0L ||
            (correction != 0.0L &&
             std::abs(component) <=
                 8.0L * std::numeric_limits<long double>::epsilon() *
                     std::abs(correction))) {
            break;
        }
    }

    const long double logarithm =
        std::log(x / 2.0L) + euler_gamma_v<long double>;
    const long double factor = 2.0L / pi_v<long double>;
    const long double y0 =
        factor * (logarithm * j0.value + correction);
    const long double y1 =
        factor * (-j0.value / x + logarithm * j1.value - derivative);
    return {j0.value, y0, j1.value, y1};
}

template<class T>
auto cyl_bessel_reduced_series_pair(long double nu, long double x)
    -> cyl_bessel_hankel_result<long double> {
    const long double target = cyl_bessel_target_tolerance<T>();
    const long double half_distance =
        std::abs(std::abs(nu) - 0.5L);
    if (half_distance <=
        64.0L * std::numeric_limits<long double>::epsilon()) {
        const long double scale =
            std::sqrt(2.0L / (pi_v<long double> * x));
        if (nu < 0.0L) {
            return {
                scale * std::cos(x),
                scale * std::sin(x),
                target,
                true};
        }
        return {
            scale * std::sin(x),
            -scale * std::cos(x),
            target,
            true};
    }
    if (std::abs(nu - 1.5L) <=
        64.0L * std::numeric_limits<long double>::epsilon()) {
        const long double scale =
            std::sqrt(2.0L / (pi_v<long double> * x));
        return {
            scale * (std::sin(x) / x - std::cos(x)),
            scale * (-std::cos(x) / x - std::sin(x)),
            target,
            true};
    }
    const long double integer_distance =
        std::abs(nu - std::round(nu));
    if (integer_distance <=
        64.0L * std::numeric_limits<long double>::epsilon()) {
        const auto values = cyl_bessel_integer_base_series(x);
        const bool first_order = std::round(nu) == 1.0L;
        return {
            values[first_order ? 2u : 0u],
            values[first_order ? 3u : 1u],
            target,
            true};
    }

    const auto j0 = cyl_bessel_power_series(nu, x, true);
    const auto j1 = cyl_bessel_power_series(nu + 1.0L, x, true);
    const auto j0_negative = cyl_bessel_power_series(-nu, x, true);
    const auto j1_negative =
        cyl_bessel_power_series(-(nu + 1.0L), x, true);
    const long double sin0 = std::sin(pi_v<long double> * nu);
    const long double sin1 =
        std::sin(pi_v<long double> * (nu + 1.0L));
    const long double cos0 = std::cos(pi_v<long double> * nu);
    const long double cos1 =
        std::cos(pi_v<long double> * (nu + 1.0L));
    if (sin0 == 0.0L || sin1 == 0.0L) {
        return {
            quiet_nan<long double>(),
            quiet_nan<long double>(),
            infinity<long double>(),
            false};
    }

    const long double y0 =
        (cos0 * j0.value - j0_negative.value) / sin0;
    const long double y1 =
        (cos1 * j1.value - j1_negative.value) / sin1;
    const long double j_error = std::max(j0.error, j1.error);
    const long double y0_error =
        (std::abs(cos0) * j0.error + j0_negative.error) /
        std::abs(sin0);
    const long double y1_error =
        (std::abs(cos1) * j1.error + j1_negative.error) /
        std::abs(sin1);
    const long double error =
        std::max({j_error, y0_error, y1_error});
    const long double scale = std::max({
        std::abs(j0.value),
        std::abs(j1.value),
        std::abs(y0),
        std::abs(y1),
        std::numeric_limits<long double>::min()});
    const bool converged =
        j0.converged &&
        j1.converged &&
        j0_negative.converged &&
        j1_negative.converged &&
        std::isfinite(y0) &&
        std::isfinite(y1) &&
        error <= target * scale;
    return {j0.value, y0, error, converged};
}

template<class T>
auto cyl_bessel_reduced_integral_pair(long double nu, long double x)
    -> cyl_bessel_hankel_result<long double> {
    const long double target = cyl_bessel_target_tolerance<T>();
    const long double tolerance = std::max(
        64.0L * std::numeric_limits<long double>::epsilon(),
        target * 0.01L);
    const unsigned segments = static_cast<unsigned>(std::max(
        8.0L,
        std::ceil(2.0L * (x + std::abs(nu)))));
    const auto finite_j = segmented_checked_simpson_integral(
        [=](long double theta) {
            return std::cos(x * std::sin(theta) - nu * theta);
        },
        0.0L,
        pi_v<long double>,
        segments,
        tolerance);
    const auto finite_y = segmented_checked_simpson_integral(
        [=](long double theta) {
            return std::sin(x * std::sin(theta) - nu * theta);
        },
        0.0L,
        pi_v<long double>,
        segments,
        tolerance);

    const long double upper = std::max(
        4.0L,
        std::asinh((std::abs(nu) + 64.0L) / x) + 1.0L);
    const auto tail_j = checked_simpson_integral(
        [=](long double t) {
            return std::exp(-x * std::sinh(t) - nu * t);
        },
        0.0L,
        upper,
        tolerance);
    const long double cos_order =
        std::cos(pi_v<long double> * nu);
    const auto tail_y = checked_simpson_integral(
        [=](long double t) {
            const long double decay = -x * std::sinh(t);
            return std::exp(decay + nu * t) +
                cos_order * std::exp(decay - nu * t);
        },
        0.0L,
        upper,
        tolerance);

    const long double sin_order =
        std::sin(pi_v<long double> * nu);
    const long double j =
        finite_j.value / pi_v<long double> -
        sin_order * tail_j.value / pi_v<long double>;
    const long double y =
        finite_y.value / pi_v<long double> -
        tail_y.value / pi_v<long double>;
    const long double error = std::max(
        (finite_j.error + std::abs(sin_order) * tail_j.error) /
            pi_v<long double>,
        (finite_y.error + tail_y.error) / pi_v<long double>);
    const long double scale = std::max({
        std::abs(j),
        std::abs(y),
        std::numeric_limits<long double>::min()});
    return {
        j,
        y,
        error,
        finite_j.converged &&
            finite_y.converged &&
            tail_j.converged &&
            tail_y.converged &&
            error <= target * scale};
}

template<class T>
auto cyl_bessel_reduced_pair(long double nu, long double x)
    -> cyl_bessel_hankel_result<long double> {
    if (x > 16.0L) {
        auto asymptotic = cyl_bessel_hankel_asymptotic(
            nu,
            x,
            cyl_bessel_target_tolerance<T>());
        if (asymptotic.converged) {
            return asymptotic;
        }
    }
    auto series = cyl_bessel_reduced_series_pair<T>(nu, x);
    if (series.converged) {
        return series;
    }
    return cyl_bessel_reduced_integral_pair<T>(nu, x);
}

inline long double cyl_bessel_j_miller(
    long double nu,
    long double x,
    long double y0,
    long double y1) {
    const auto n = static_cast<unsigned>(
        std::floor(nu + 0.5L));
    const long double reduced = nu - static_cast<long double>(n);
    const unsigned extra = 32u + static_cast<unsigned>(
        std::ceil(std::sqrt(40.0L * static_cast<long double>(n + 1u))));
    const unsigned top = n + extra;

    long double f_k_plus_1 = 0.0L;
    long double f_k = 1.0L;
    long double target = 0.0L;
    for (unsigned k = top; k > 0u; --k) {
        if (k == n) {
            target = f_k;
        }
        long double f_k_minus_1 =
            2.0L * (reduced + static_cast<long double>(k)) /
                x * f_k -
            f_k_plus_1;
        if (k - 1u == n) {
            target = f_k_minus_1;
        }
        if (std::abs(f_k_minus_1) > 1e200L) {
            f_k_minus_1 *= 1e-200L;
            f_k *= 1e-200L;
            f_k_plus_1 *= 1e-200L;
            target *= 1e-200L;
        }
        f_k_plus_1 = f_k;
        f_k = f_k_minus_1;
    }

    const long double denominator =
        f_k * y1 - f_k_plus_1 * y0;
    return target *
        (-2.0L / (pi_v<long double> * x)) /
        denominator;
}

template<class T>
auto cyl_bessel_jy_fallback(T nu, T x)
    -> cyl_bessel_hankel_result<T> {
    if (std::isnan(nu) || std::isnan(x) || nu < T{} || x < T{}) {
        const T nan = quiet_nan<T>();
        return {nan, nan, nan, false};
    }
    if (std::isinf(x)) {
        return {T{}, T{}, T{}, true};
    }
    if (x == T{}) {
        return {
            nu == T{} ? T{1} : T{},
            -infinity<T>(),
            T{},
            true};
    }

    const long double order = static_cast<long double>(nu);
    const long double argument = static_cast<long double>(x);
    if (!std::isfinite(order) || order >= 1024.0L) {
        const T nan = quiet_nan<T>();
        return {nan, nan, nan, false};
    }
    const auto n = static_cast<unsigned>(
        std::floor(order + 0.5L));
    const long double reduced =
        order - static_cast<long double>(n);
    const auto first =
        cyl_bessel_reduced_pair<T>(reduced, argument);
    const auto second =
        cyl_bessel_reduced_pair<T>(reduced + 1.0L, argument);

    if (n == 0u) {
        return {
            static_cast<T>(first.j),
            static_cast<T>(first.y),
            static_cast<T>(std::max(first.error, second.error)),
            first.converged && second.converged};
    }

    long double j_previous = first.j;
    long double j_current = second.j;
    long double y_previous = first.y;
    long double y_current = second.y;
    for (unsigned k = 1u; k < n; ++k) {
        const long double recurrence_order =
            reduced + static_cast<long double>(k);
        const long double next_j =
            2.0L * recurrence_order / argument * j_current -
            j_previous;
        const long double next_y =
            2.0L * recurrence_order / argument * y_current -
            y_previous;
        j_previous = j_current;
        j_current = next_j;
        y_previous = y_current;
        y_current = next_y;
    }

    if (argument < order) {
        if (argument < 1.0L ||
            !std::isfinite(y_previous) ||
            !std::isfinite(y_current)) {
            j_current = cyl_bessel_j_series(order, argument);
        } else {
            j_current = cyl_bessel_j_miller(
                order,
                argument,
                first.y,
                second.y);
        }
    }

    return {
        static_cast<T>(j_current),
        static_cast<T>(y_current),
        static_cast<T>(std::max(first.error, second.error)),
        first.converged && second.converged};
}

template<class T>
T cyl_bessel_j_fallback(T nu, T x) {
    const auto result = cyl_bessel_jy_fallback(nu, x);
    return result.converged ? result.j : quiet_nan<T>();
}

template<class T>
T cyl_bessel_y_fallback(T nu, T x) {
    const auto result = cyl_bessel_jy_fallback(nu, x);
    return result.converged ? result.y : quiet_nan<T>();
}

template<class T>
T cyl_bessel_k_integral(T nu, T x) {
    const long double order = std::abs(static_cast<long double>(nu));
    const long double argument = static_cast<long double>(x);
    const auto log_cosh = [](long double value) {
        value = std::abs(value);
        return value + std::log1p(std::exp(-2.0L * value)) -
            std::log(2.0L);
    };

    const auto log_integrand = [&](long double t) {
        return -argument * std::cosh(t) + log_cosh(order * t);
    };

    // K_nu(x) = integral exp(-x cosh(t)) cosh(nu t) dt. Locate the
    // integrand peak and remove its exponential scale before quadrature.
    long double peak = 0.0L;
    if (order * order > argument) {
        long double low = 0.0L;
        long double high = std::asinh(order / argument) + 1.0L;
        for (unsigned i = 0; i < 96u; ++i) {
            const long double mid = (low + high) / 2.0L;
            const long double derivative =
                -argument * std::sinh(mid) +
                order * std::tanh(order * mid);
            if (derivative > 0.0L) {
                low = mid;
            } else {
                high = mid;
            }
        }
        peak = (low + high) / 2.0L;
    }

    const long double log_scale =
        std::max(log_integrand(0.0L), log_integrand(peak));
    const long double upper = std::max(
        peak + 2.0L,
        std::asinh((order + 64.0L) / argument) + 1.0L);
    const auto scaled_integrand = [&](long double t) {
        return std::exp(log_integrand(t) - log_scale);
    };

    long double integral = 0.0L;
    if (peak > 0.0L) {
        integral += adaptive_simpson_integral(
            scaled_integrand, 0.0L, peak);
        integral += adaptive_simpson_integral(
            scaled_integrand, peak, upper);
    } else {
        integral = adaptive_simpson_integral(
            scaled_integrand, 0.0L, upper);
    }

    const long double log_value = log_scale + std::log(integral);
    if (log_value > std::log(
            static_cast<long double>(std::numeric_limits<T>::max()))) {
        return infinity<T>();
    }
    return static_cast<T>(std::exp(log_value));
}

template<class T>
T cyl_bessel_k_fallback(T nu, T x) {
    if (std::isnan(nu) || std::isnan(x) || x < T{}) {
        return quiet_nan<T>();
    }
    if (x == T{}) {
        return infinity<T>();
    }
    if (std::isinf(x)) {
        return T{};
    }
    return cyl_bessel_k_integral(nu, x);
}

template<class T>
T comp_ellint_1(T x) {
#ifdef __cpp_lib_math_special_functions
    return std::comp_ellint_1(x);
#else
    return comp_ellint_1_fallback(x);
#endif
}

template<class T>
T comp_ellint_2(T x) {
#ifdef __cpp_lib_math_special_functions
    return std::comp_ellint_2(x);
#else
    return comp_ellint_2_fallback(x);
#endif
}

template<class T>
T expint(T x) {
#ifdef __cpp_lib_math_special_functions
    return std::expint(x);
#else
    return expint_fallback(x);
#endif
}

template<class T>
T riemann_zeta(T x) {
#ifdef __cpp_lib_math_special_functions
    return std::riemann_zeta(x);
#else
    return riemann_zeta_fallback(x);
#endif
}

template<class T>
T beta(T x, T y) {
#ifdef __cpp_lib_math_special_functions
    return std::beta(x, y);
#else
    return beta_fallback(x, y);
#endif
}

template<class T>
T comp_ellint_3(T k, T nu) {
#ifdef __cpp_lib_math_special_functions
    return std::comp_ellint_3(k, nu);
#else
    return comp_ellint_3_fallback(k, nu);
#endif
}

template<class T>
T cyl_bessel_i(T nu, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::cyl_bessel_i(nu, x);
#else
    return cyl_bessel_i_series(nu, x);
#endif
}

template<class T>
T cyl_bessel_j(T nu, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::cyl_bessel_j(nu, x);
#else
    return cyl_bessel_j_fallback(nu, x);
#endif
}

template<class T>
T cyl_bessel_k(T nu, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::cyl_bessel_k(nu, x);
#else
    return cyl_bessel_k_fallback(nu, x);
#endif
}

template<class T>
T cyl_neumann(T nu, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::cyl_neumann(nu, x);
#else
    return cyl_bessel_y_fallback(nu, x);
#endif
}

template<class T>
T ellint_1(T k, T phi) {
#ifdef __cpp_lib_math_special_functions
    return std::ellint_1(k, phi);
#else
    return ellint_1_fallback(k, phi);
#endif
}

template<class T>
T ellint_2(T k, T phi) {
#ifdef __cpp_lib_math_special_functions
    return std::ellint_2(k, phi);
#else
    return ellint_2_fallback(k, phi);
#endif
}

template<class T>
T ellint_3(T k, T nu, T phi) {
#ifdef __cpp_lib_math_special_functions
    return std::ellint_3(k, nu, phi);
#else
    return ellint_3_fallback(k, nu, phi);
#endif
}

template<class T>
T hermite(unsigned n, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::hermite(n, x);
#else
    return hermite_fallback(n, x);
#endif
}

template<class T>
T laguerre(unsigned n, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::laguerre(n, x);
#else
    return laguerre_fallback(n, x);
#endif
}

template<class T>
T legendre(unsigned n, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::legendre(n, x);
#else
    return legendre_fallback(n, x);
#endif
}

template<class T>
T sph_bessel(unsigned n, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::sph_bessel(n, x);
#else
    return sph_bessel_fallback(n, x);
#endif
}

template<class T>
T sph_neumann(unsigned n, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::sph_neumann(n, x);
#else
    return sph_neumann_fallback(n, x);
#endif
}

template<class T>
T assoc_laguerre(unsigned n, unsigned m, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::assoc_laguerre(n, m, x);
#else
    return assoc_laguerre_fallback(n, m, x);
#endif
}

template<class T>
T assoc_legendre(unsigned l, unsigned m, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::assoc_legendre(l, m, x);
#else
    return assoc_legendre_fallback(l, m, x);
#endif
}

template<class T>
T sph_legendre(unsigned l, unsigned m, T theta) {
#ifdef __cpp_lib_math_special_functions
    return std::sph_legendre(l, m, theta);
#else
    return sph_legendre_fallback(l, m, theta);
#endif
}

} // namespace detail::special_math

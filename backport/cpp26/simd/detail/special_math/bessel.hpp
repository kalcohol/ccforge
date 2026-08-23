#pragma once

namespace detail::special_math {

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

} // namespace detail::special_math

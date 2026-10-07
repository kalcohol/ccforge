#pragma once

namespace detail::special_math {

template<class T>
T sph_bessel_fallback(unsigned n, T x) {
    if (std::isnan(x) || x < T{}) {
        return quiet_nan<T>();
    }
    if (n >= recurrence_order_limit) {
        return quiet_nan<T>();
    }
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
    if (std::isnan(x) || x < T{}) {
        return quiet_nan<T>();
    }
    if (n >= recurrence_order_limit) {
        return quiet_nan<T>();
    }
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

struct cyl_bessel_order_trig_result {
    long double cosine;
    long double sine;
};

inline auto cyl_bessel_order_trig(long double order)
    -> cyl_bessel_order_trig_result {
    const long double nearest = std::round(order);
    const long double delta = order - nearest;
    const long double parity =
        std::fmod(std::abs(nearest), 2.0L) == 1.0L ? -1.0L : 1.0L;
    if (delta == 0.0L) {
        return {parity, 0.0L};
    }
    if (std::abs(delta) == 0.5L) {
        return {0.0L, std::copysign(1.0L, parity * delta)};
    }
    return {
        parity * std::cos(pi_v<long double> * delta),
        parity * std::sin(pi_v<long double> * delta)};
}

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

    const T scale = std::sqrt(T{2} / (pi_v<T> * x));
    // Reduce x through libm before combining the bounded order phase.
    const auto order_trig = cyl_bessel_order_trig(
        static_cast<long double>(nu) / 2.0L);
    const long double inverse_sqrt_two = std::sqrt(0.5L);
    const T cos_offset = static_cast<T>(
        (order_trig.cosine - order_trig.sine) * inverse_sqrt_two);
    const T sin_offset = static_cast<T>(
        (order_trig.cosine + order_trig.sine) * inverse_sqrt_two);
    const T cos_x = std::cos(x);
    const T sin_x = std::sin(x);
    const T cos_phase = cos_x * cos_offset + sin_x * sin_offset;
    const T sin_phase = sin_x * cos_offset - cos_x * sin_offset;
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
auto cyl_bessel_near_integer_y_series(long double delta, long double x)
    -> std::array<cyl_bessel_series_result, 2> {
    const cyl_bessel_series_result failure{
        quiet_nan<long double>(), infinity<long double>(), false};
    if (!(x > 0.0L && x <= 2.0L && std::abs(delta) <= 0.5L)) {
        return {failure, failure};
    }

    // Temme's small-x Y_delta/Y_(delta+1) series (1976), with the odd
    // reciprocal-Gamma combination evaluated without subtracting near 1.
    // zeta(k)/k: log(1/Gamma(1+delta)) = even + delta*odd (DLMF 5.7).
    constexpr std::array even_coefficients{
        0.82246703342411321823620758332301259461L,
        0.27058080842778454787900092413529197569L,
        0.16955717699740818995241965496515342132L,
        0.12550966952474304242233565481358155816L,
        0.10009945751278180853371459589003190170L,
        0.08335384054610900402488649983731163925L,
        0.07143294629536133605923275322179538098L,
        0.06250095514121304074198328571797729513L};
    constexpr std::array odd_coefficients{
        0.40068563438653142846657938717048333025L,
        0.20738555102867398526627309729140683361L,
        0.14404989676884611811997107854997096566L,
        0.11133426586956469049087252991471245117L,
        0.09095401714582904223260929841149726695L,
        0.07693251641135219147282706434818133813L,
        0.06666870588242046803290344856737633751L};
    const long double epsilon = std::numeric_limits<long double>::epsilon();
    const long double tiny = std::numeric_limits<long double>::denorm_min();
    const long double target = cyl_bessel_target_tolerance<T>();
    const long double squared = delta * delta;
    const long double gamma_tail =
        (pi_v<long double> * pi_v<long double> / 6.0L) *
        std::pow(squared, 8) / (17.0L * (1.0L - squared));
    if (16.0L * gamma_tail > target / 8.0L) {
        return {failure, failure};
    }
    long double even = even_coefficients.back();
    for (std::size_t k = even_coefficients.size() - 1u; k > 0u; --k) {
        even = even * squared + even_coefficients[k - 1u];
    }
    even *= -squared;
    long double odd = odd_coefficients.back();
    for (std::size_t k = odd_coefficients.size() - 1u; k > 0u; --k) {
        odd = odd * squared + odd_coefficients[k - 1u];
    }
    odd = euler_gamma_v<long double> + squared * odd;
    const auto sinhc = [](long double value) {
        return value == 0.0L ? 1.0L : std::sinh(value) / value;
    };
    const auto sinc = [](long double value) {
        return value == 0.0L ? 1.0L : std::sin(value) / value;
    };
    const long double odd_log = delta * odd;
    const long double gamma1 = -std::exp(even) * odd * sinhc(odd_log);
    const long double gamma2 = std::exp(even) * std::cosh(odd_log);
    const long double logarithm = std::log(x) - std::log(2.0L);
    const long double sigma = -delta * logarithm;
    const long double factor =
        1.0L / (pi_v<long double> * sinc(pi_v<long double> * delta));
    const long double half_sinc = sinc(pi_v<long double> * delta / 2.0L);
    const long double e =
        pi_v<long double> * pi_v<long double> * delta * half_sinc * half_sinc / 2.0L;
    const long double first = gamma1 * std::cosh(sigma);
    const long double second = gamma2 * logarithm * sinhc(sigma);
    long double f = 2.0L * factor * (first - second);
    long double p = factor * std::exp(sigma + even - odd_log);
    long double q = factor * std::exp(-sigma + even + odd_log);
    const long double seed_error =
        64.0L * epsilon * (1.0L + std::abs(sigma)) + 16.0L * gamma_tail;
    long double f_error =
        seed_error * 2.0L * factor * (std::abs(first) + std::abs(second));
    long double p_error = seed_error * std::abs(p);
    long double q_error = seed_error * std::abs(q);
    const long double e_error = 16.0L * epsilon * std::abs(e);
    const long double z = (x / 2.0L) * (x / 2.0L);
    const long double magnitude = std::max(1.0L, std::abs(e));
    long double sum0 = f + e * q;
    long double sum1 = p;
    long double compensation0 = 0.0L;
    long double compensation1 = 0.0L;
    long double error0 = f_error + std::abs(e) * q_error +
        e_error * (std::abs(q) + q_error);
    long double error1 = p_error;
    long double absolute0 = std::abs(f) + std::abs(e * q);
    long double absolute1 = std::abs(p);
    std::array<cyl_bessel_series_result, 2> result{failure, failure};
    const auto add = [](long double term, long double& sum, long double& compensation) {
        const long double adjusted = term - compensation;
        const long double updated = sum + adjusted;
        compensation = (updated - sum) - adjusted;
        sum = updated;
    };
    for (unsigned k = 1u; k <= 64u; ++k) {
        const long double order = static_cast<long double>(k);
        const long double a = -z / (order * order - squared);
        const long double b = a / order;
        const long double c = -z / (order * (order - delta));
        const long double d = -z / (order * (order + delta));
        f_error = std::abs(a) * f_error + std::abs(b) * (p_error + q_error) +
            16.0L * epsilon * (std::abs(a * f) +
                std::abs(b) * (std::abs(p) + std::abs(q))) +
            tiny * (1.0L + std::abs(f) + std::abs(p) + std::abs(q));
        const long double old_p = p;
        const long double old_q = q;
        f = a * f + b * (p + q);
        p *= c;
        q *= d;
        p_error = std::abs(c) * p_error + 8.0L * epsilon * std::abs(p) +
            tiny * (1.0L + std::abs(old_p));
        q_error = std::abs(d) * q_error + 8.0L * epsilon * std::abs(q) +
            tiny * (1.0L + std::abs(old_q));
        const long double term0 = f + e * q;
        const long double term1 = p - order * term0;
        const long double term_error0 = f_error + std::abs(e) * q_error +
            e_error * (std::abs(q) + q_error) +
            4.0L * epsilon * (std::abs(f) + std::abs(e * q)) + tiny;
        error0 += term_error0;
        error1 += p_error + order * term_error0 +
            4.0L * epsilon * (std::abs(p) + order * std::abs(term0)) + tiny;
        absolute0 += std::abs(term0);
        absolute1 += std::abs(term1);
        add(term0, sum0, compensation0);
        add(term1, sum1, compensation1);

        // A positive recurrence majorant bounds all remaining terms, not
        // merely the last observed term (which may vanish by cancellation).
        const long double next = order + 1.0L;
        const long double denominator = next * next - squared;
        const long double upper_f = std::abs(f) + f_error;
        const long double upper_p = std::abs(p) + p_error;
        const long double upper_q = std::abs(q) + q_error;
        const long double upper_z = z * (1.0L + 4.0L * epsilon) + tiny;
        const long double rho = std::max(
            upper_z / denominator,
            upper_z / (next * denominator) +
                upper_z / (next * (next - std::abs(delta))));
        const long double tail =
            upper_z / denominator * upper_f +
            upper_z / (next * denominator) * (upper_p + upper_q) +
            upper_z / (next * (next - std::abs(delta))) * (upper_p + upper_q);
        const long double tail0 = magnitude * tail / (1.0L - rho);
        const long double tail1 = tail * (
            (1.0L + next * magnitude) / (1.0L - rho) +
            magnitude * rho / ((1.0L - rho) * (1.0L - rho)));
        const long double y0 = -sum0;
        const long double y1 = -(2.0L * sum1) / x;
        const long double y0_error = error0 + 8.0L * epsilon * absolute0 + tail0;
        const long double y1_error =
            (2.0L * (error1 + 8.0L * epsilon * absolute1 + tail1)) / x +
            4.0L * epsilon * std::abs(y1);
        result = {{{y0, y0_error, std::isfinite(y0) &&
                        y0_error <= target * std::max(1.0L, std::abs(y0))},
                   {y1, y1_error, std::isfinite(y1) &&
                        y1_error <= target * std::max(1.0L, std::abs(y1))}}};
        if (result[0].converged && result[1].converged) {
            return result;
        }
        if (!std::isfinite(y0_error) || !std::isfinite(y1_error)) {
            break;
        }
    }
    return {failure, failure};
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
    const long double nearest = std::round(nu);
    const long double delta = nu - nearest;
    if (delta == 0.0L) {
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
    const auto trig = cyl_bessel_order_trig(nu);
    const long double sin0 = trig.sine;
    const long double sin1 = -sin0;
    const long double cos0 = trig.cosine;
    const long double cos1 = -cos0;
    const long double epsilon = std::numeric_limits<long double>::epsilon();
    const long double phase_error =
        4.0L * epsilon * pi_v<long double> * std::abs(delta);
    const long double sine_error = phase_error + 8.0L * epsilon * std::abs(sin0) +
        std::numeric_limits<long double>::denorm_min();
    const long double cosine_error = phase_error + 8.0L * epsilon * std::abs(cos0);
    const long double denominator = std::nextafter(
        std::abs(sin0) - sine_error, 0.0L);
    const long double y0 =
        (cos0 * j0.value - j0_negative.value) / sin0;
    const long double y1 =
        (cos1 * j1.value - j1_negative.value) / sin1;
    const long double j_error = std::max(j0.error, j1.error);
    const auto reflection_error = [&](const auto& positive, const auto& negative,
                                      long double cosine, long double value) {
        if (!(denominator > 0.0L) || !std::isfinite(value)) {
            return infinity<long double>();
        }
        const long double numerator_error =
            std::abs(cosine) * positive.error + negative.error +
            cosine_error * (std::abs(positive.value) + positive.error) +
            4.0L * epsilon *
                (std::abs(cosine * positive.value) + std::abs(negative.value));
        return (numerator_error + std::abs(value) * sine_error +
                std::abs(sin0) * 4.0L * epsilon * std::abs(value)) / denominator;
    };
    const long double y0_error = reflection_error(j0, j0_negative, cos0, y0);
    long double y1_error = reflection_error(j1, j1_negative, cos1, y1);
    const long double adjacent_delta = (nu + 1.0L) - (nearest + 1.0L);
    const long double order_error = std::abs(adjacent_delta - delta);
    if (order_error != 0.0L) {
        if (!(std::abs(delta) > 2.0L * order_error && denominator > 0.0L)) {
            y1_error = infinity<long double>();
        } else {
            // Bound order sensitivity of the Gamma seed and the at most
            // 4096 series denominators; the interval stays away from poles.
            const long double sensitivity = order_error * (
                std::abs(std::log(x / 2.0L)) + 32.0L + 8.0L / std::abs(delta));
            y1_error += std::expm1(sensitivity) *
                ((j1.error + j1_negative.error) / (8.0L * epsilon)) / denominator;
        }
    }
    const long double error =
        std::max({j_error, y0_error, y1_error});
    const bool converged =
        j0.converged &&
        j1.converged &&
        j0_negative.converged &&
        j1_negative.converged &&
        std::isfinite(y0) &&
        std::isfinite(y1) &&
        j0.error <= target * std::max(1.0L, std::abs(j0.value)) &&
        j1.error <= target * std::max(1.0L, std::abs(j1.value)) &&
        y0_error <= target * std::max(1.0L, std::abs(y0)) &&
        y1_error <= target * std::max(1.0L, std::abs(y1));
    if (converged) {
        return {j0.value, y0, std::max(j0.error, y0_error), true};
    }
    if (nearest == 0.0L || nearest == 1.0L) {
        const auto near_integer = cyl_bessel_near_integer_y_series<T>(delta, x);
        const auto& selected = near_integer[nearest == 1.0L ? 1u : 0u];
        if (j0.converged && selected.converged) {
            return {j0.value, selected.value, std::max(j0.error, selected.error), true};
        }
    }
    return {j0.value, y0, error, false};
}

template<class Fun>
auto cyl_bessel_cached_simpson_step(
    Fun& fun, long double a, long double b, long double tolerance,
    long double whole, long double first, long double middle, long double last,
    unsigned depth) -> checked_integral_result {
    const long double mid = (a + b) / 2.0L;
    // Only the quarter points are new after bisecting this interval.
    const long double quarter = fun((a + mid) / 2.0L);
    const long double three_quarters = fun((mid + b) / 2.0L);
    const long double left =
        (mid - a) * (first + 4.0L * quarter + middle) / 6.0L;
    const long double right =
        (b - mid) * (middle + 4.0L * three_quarters + last) / 6.0L;
    const long double delta = left + right - whole;
    const long double error = std::abs(delta) / 15.0L;
    const long double corrected = left + right + delta / 15.0L;
    if (error <= tolerance) {
        return {corrected, error, true};
    }
    if (depth == 0u) {
        return {corrected, error, false};
    }
    const auto left_result = cyl_bessel_cached_simpson_step(
        fun, a, mid, tolerance / 2.0L, left, first, quarter, middle, depth - 1u);
    const auto right_result = cyl_bessel_cached_simpson_step(
        fun, mid, b, tolerance / 2.0L, right, middle, three_quarters, last, depth - 1u);
    return {
        left_result.value + right_result.value,
        left_result.error + right_result.error,
        left_result.converged && right_result.converged};
}

template<class Fun>
auto cyl_bessel_cached_simpson_integral(
    Fun&& fun, long double a, long double b, long double tolerance,
    unsigned depth = 18u) -> checked_integral_result {
    if (a == b) {
        return {0.0L, 0.0L, true};
    }
    const long double first = fun(a);
    const long double middle = fun((a + b) / 2.0L);
    const long double last = fun(b);
    const long double whole = (b - a) * (first + 4.0L * middle + last) / 6.0L;
    return cyl_bessel_cached_simpson_step(
        fun, a, b, tolerance, whole, first, middle, last, depth);
}

template<class Fun>
auto cyl_bessel_segmented_cached_simpson_integral(
    Fun&& fun, long double a, long double b, unsigned segments, long double tolerance)
    -> checked_integral_result {
    checked_integral_result result{0.0L, 0.0L, true};
    if (a == b || segments == 0u) {
        return result;
    }
    long double first = fun(a);
    for (unsigned i = 0u; i < segments; ++i) {
        const long double left =
            a + (b - a) * static_cast<long double>(i) /
                static_cast<long double>(segments);
        const long double right =
            a + (b - a) * static_cast<long double>(i + 1u) /
                static_cast<long double>(segments);
        const long double middle = fun((left + right) / 2.0L);
        const long double last = fun(right);
        const long double whole =
            (right - left) * (first + 4.0L * middle + last) / 6.0L;
        const auto part = cyl_bessel_cached_simpson_step(
            fun, left, right, tolerance / static_cast<long double>(segments),
            whole, first, middle, last, 18u);
        result.value += part.value;
        result.error += part.error;
        result.converged = result.converged && part.converged;
        first = last;
    }
    return result;
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
    const auto finite_j = cyl_bessel_segmented_cached_simpson_integral(
        [=](long double theta) {
            return std::cos(x * std::sin(theta) - nu * theta);
        },
        0.0L,
        pi_v<long double>,
        segments,
        tolerance);
    const auto finite_y = cyl_bessel_segmented_cached_simpson_integral(
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
    const auto tail_j = cyl_bessel_cached_simpson_integral(
        [=](long double t) {
            return std::exp(-x * std::sinh(t) - nu * t);
        },
        0.0L,
        upper,
        tolerance);
    const long double cos_order =
        std::cos(pi_v<long double> * nu);
    const auto tail_y = cyl_bessel_cached_simpson_integral(
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

struct cyl_bessel_scaled_value {
    long double fraction;
    int exponent;
};

inline auto cyl_bessel_scaled(long double value, int exponent = 0)
    -> cyl_bessel_scaled_value {
    if (value == 0.0L || !std::isfinite(value)) {
        return {value, 0};
    }
    int shift = 0;
    const long double fraction = std::frexp(value, &shift);
    return {fraction, exponent + shift};
}

inline auto cyl_bessel_scaled_multiply(
    cyl_bessel_scaled_value value, long double coefficient)
    -> cyl_bessel_scaled_value {
    if (coefficient == 0.0L) {
        return {0.0L, 0};
    }
    const auto factor = cyl_bessel_scaled(coefficient);
    return cyl_bessel_scaled(value.fraction * factor.fraction,
                             value.exponent + factor.exponent);
}

inline auto cyl_bessel_scaled_ratio(
    cyl_bessel_scaled_value value, long double numerator, long double denominator)
    -> cyl_bessel_scaled_value {
    const auto top = cyl_bessel_scaled(numerator);
    const auto bottom = cyl_bessel_scaled(denominator);
    return cyl_bessel_scaled((value.fraction * top.fraction) / bottom.fraction,
                             value.exponent + top.exponent - bottom.exponent);
}

inline auto cyl_bessel_scaled_sum(
    cyl_bessel_scaled_value left, cyl_bessel_scaled_value right, bool subtract = false)
    -> cyl_bessel_scaled_value {
    if (!std::isfinite(left.fraction) || !std::isfinite(right.fraction) ||
        (left.fraction == 0.0L && right.fraction == 0.0L)) {
        return cyl_bessel_scaled(subtract
            ? left.fraction - right.fraction : left.fraction + right.fraction);
    }
    if (left.fraction == 0.0L) {
        return {subtract ? -right.fraction : right.fraction, right.exponent};
    }
    if (right.fraction == 0.0L) {
        return left;
    }
    const int exponent = std::max(left.exponent, right.exponent);
    const long double a = std::ldexp(left.fraction, left.exponent - exponent);
    const long double b = std::ldexp(right.fraction, right.exponent - exponent);
    return cyl_bessel_scaled(subtract ? a - b : a + b, exponent);
}

inline long double cyl_bessel_unscale(cyl_bessel_scaled_value value) {
    return std::ldexp(value.fraction, value.exponent);
}

inline bool cyl_bessel_scaled_finite_in_work(cyl_bessel_scaled_value value) {
    return std::isfinite(value.fraction) &&
        value.exponent <= std::numeric_limits<long double>::max_exponent;
}

struct cyl_bessel_scaled_hankel_result {
    long double j;
    cyl_bessel_scaled_value y;
    long double error;
    bool converged;
};

template<class T>
auto cyl_bessel_positive_scaled_pair(long double order, long double argument)
    -> cyl_bessel_scaled_hankel_result {
    if (!std::isfinite(order) || order >= 1024.0L) {
        const long double nan = quiet_nan<long double>();
        return {nan, cyl_bessel_scaled(nan), nan, false};
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
            first.j,
            cyl_bessel_scaled(first.y),
            std::max(first.error, second.error),
            first.converged && second.converged};
    }

    long double j_previous = first.j;
    long double j_current = second.j;
    // Keep the exponent separate even when long double has only 53 bits.
    auto y_previous = cyl_bessel_scaled(first.y);
    auto y_current = cyl_bessel_scaled(second.y);
    for (unsigned k = 1u; k < n; ++k) {
        const long double recurrence_order =
            reduced + static_cast<long double>(k);
        const long double next_j =
            2.0L * recurrence_order / argument * j_current -
            j_previous;
        const auto next_y = cyl_bessel_scaled_sum(
            cyl_bessel_scaled_ratio(y_current, 2.0L * recurrence_order, argument),
            y_previous, true);
        j_previous = j_current;
        j_current = next_j;
        y_previous = y_current;
        y_current = next_y;
    }

    if (argument < order) {
        if (argument < 1.0L ||
            !cyl_bessel_scaled_finite_in_work(y_previous) ||
            !cyl_bessel_scaled_finite_in_work(y_current)) {
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
        j_current,
        y_current,
        std::max(first.error, second.error),
        first.converged && second.converged};
}

template<class T>
auto cyl_bessel_jy_fallback(T nu, T x)
    -> cyl_bessel_hankel_result<T> {
    if (std::isnan(nu) || std::isnan(x) || x < T{}) {
        const T nan = quiet_nan<T>();
        return {nan, nan, nan, false};
    }
    if (nu < T{}) {
        const long double order = -static_cast<long double>(nu);
        if (!std::isfinite(order)) {
            const T nan = quiet_nan<T>();
            return {nan, nan, nan, false};
        }
        const auto trig = cyl_bessel_order_trig(order);
        if (x == T{}) {
            const long double j = trig.sine == 0.0L
                ? std::copysign(0.0L, trig.cosine)
                : std::copysign(infinity<long double>(), trig.sine);
            const long double y = trig.cosine == 0.0L
                ? std::copysign(0.0L, trig.sine)
                : std::copysign(infinity<long double>(), -trig.cosine);
            return {static_cast<T>(j), static_cast<T>(y), T{}, true};
        }

        const auto positive = std::isinf(x)
            ? cyl_bessel_scaled_hankel_result{0.0L, {0.0L, 0}, 0.0L, true}
            : cyl_bessel_positive_scaled_pair<T>(order, static_cast<long double>(x));
        const auto positive_j = cyl_bessel_scaled(positive.j);
        // Apply reflection coefficients before restoring Y's exponent.
        const auto j = cyl_bessel_scaled_sum(
            cyl_bessel_scaled_multiply(positive_j, trig.cosine),
            cyl_bessel_scaled_multiply(positive.y, trig.sine), true);
        const auto y = cyl_bessel_scaled_sum(
            cyl_bessel_scaled_multiply(positive_j, trig.sine),
            cyl_bessel_scaled_multiply(positive.y, trig.cosine));
        return {
            static_cast<T>(cyl_bessel_unscale(j)),
            static_cast<T>(cyl_bessel_unscale(y)),
            static_cast<T>((std::abs(trig.cosine) + std::abs(trig.sine)) *
                          positive.error),
            positive.converged};
    }
    if (std::isinf(x)) {
        return {T{}, T{}, T{}, true};
    }
    if (x == T{}) {
        return {nu == T{} ? T{1} : T{}, -infinity<T>(), T{}, true};
    }

    const auto positive = cyl_bessel_positive_scaled_pair<T>(
        static_cast<long double>(nu), static_cast<long double>(x));
    return {
        static_cast<T>(positive.j),
        static_cast<T>(cyl_bessel_unscale(positive.y)),
        static_cast<T>(positive.error),
        positive.converged};
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
T cyl_bessel_i_fallback(T nu, T x) {
    if (std::isnan(nu) || std::isnan(x) || x < T{} ||
        !std::isfinite(nu)) {
        return quiet_nan<T>();
    }
    if (nu >= T{}) {
        return cyl_bessel_i_series(nu, x);
    }

    const long double order = -static_cast<long double>(nu);
    const auto trig = cyl_bessel_order_trig(order);
    if (x == T{}) {
        if (trig.sine == 0.0L) {
            return T{};
        }
        return static_cast<T>(
            std::copysign(infinity<long double>(), trig.sine));
    }

    const long double argument = static_cast<long double>(x);
    const long double positive = cyl_bessel_i_series(order, argument);
    if (trig.sine == 0.0L) {
        return static_cast<T>(positive);
    }
    const long double irregular = cyl_bessel_k_fallback(order, argument);
    return static_cast<T>(
        positive +
        2.0L / pi_v<long double> * trig.sine * irregular);
}

} // namespace detail::special_math

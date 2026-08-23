#pragma once

namespace detail::special_math {

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

    return sum / -std::expm1((T{1} - s) * std::log(T{2}));
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
    const T pole_delta = s - T{1};
    if (std::abs(pole_delta) <=
        std::sqrt(std::numeric_limits<T>::epsilon())) {
        using wide_t = conditional_t<
            (sizeof(T) < sizeof(long double)), long double, T>;
        constexpr wide_t euler_gamma =
            0.577215664901532860606512090082402431L;
        constexpr wide_t minus_stieltjes_one =
            0.072815845483676724860586375874901319L;
        constexpr wide_t stieltjes_two_over_two =
            -0.004845181596436159242775526986987055L;
        const wide_t delta = static_cast<wide_t>(pole_delta);
        return static_cast<T>(
            wide_t{1} / delta +
            euler_gamma +
            minus_stieltjes_one * delta +
            stieltjes_two_over_two * delta * delta);
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

} // namespace detail::special_math

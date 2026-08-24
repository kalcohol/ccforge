#pragma once

namespace detail::special_math {

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
    if (std::abs(k) == T{1}) {
        return static_cast<T>(std::asinh(std::tan(
            static_cast<long double>(phi))));
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
T ellint_3_zero_modulus(T nu, T phi) {
    using wide_t = conditional_t<(sizeof(T) < sizeof(double)), double, long double>;
    const wide_t wide_nu = static_cast<wide_t>(nu);
    const wide_t bound = std::abs(static_cast<wide_t>(phi));
    const wide_t half_pi = pi_v<wide_t> / wide_t{2};
    wide_t value{};

    if (wide_nu < wide_t{1}) {
        const wide_t scale = std::sqrt(wide_t{1} - wide_nu);
        const wide_t half_period = half_pi / scale;
        if (std::abs(phi) == pi_v<T> / T{2}) {
            return std::copysign(static_cast<T>(half_period), phi);
        }
        const wide_t full_periods = std::floor(bound / pi_v<wide_t>);
        const wide_t remainder = std::fmod(bound, pi_v<wide_t>);
        value = full_periods * wide_t{2} * half_period;
        if (remainder == half_pi) {
            value += half_period;
        } else if (remainder < half_pi) {
            value += std::atan(scale * std::tan(remainder)) / scale;
        } else {
            value += wide_t{2} * half_period -
                std::atan(scale * std::tan(pi_v<wide_t> - remainder)) /
                    scale;
        }
    } else if (wide_nu == wide_t{1}) {
        value = std::tan(bound);
    } else {
        const wide_t scale = std::sqrt(wide_nu - wide_t{1});
        const wide_t argument = scale * std::tan(bound);
        if (argument > wide_t{1}) {
            return quiet_nan<T>();
        }
        if (argument == wide_t{1}) {
            return std::copysign(infinity<T>(), phi);
        }
        value = std::atanh(argument) / scale;
    }

    return std::copysign(static_cast<T>(value), phi);
}

template<class T>
T ellint_3_unit_modulus(T nu, T phi) {
    // For |k| == 1, t = sin(phi) reduces the integrand to
    // 1 / ((1 - t^2) (1 - nu t^2)); partial fractions avoid the endpoint pole.
    const long double order = static_cast<long double>(nu);
    const long double angle = static_cast<long double>(phi);
    const long double sine = std::sin(angle);
    const long double tangent = std::tan(angle);
    const long double first = std::asinh(tangent);

    if (order == 0.0L) {
        return static_cast<T>(first);
    }
    if (order == 1.0L) {
        const long double cosine = std::cos(angle);
        return static_cast<T>(
            (sine / (cosine * cosine) + first) / 2.0L);
    }

    long double second;
    if (order > 0.0L) {
        const long double root = std::sqrt(order);
        second = std::atanh(root * sine) / root;
    } else {
        const long double root = std::sqrt(-order);
        second = std::atan(root * sine) / root;
    }
    return static_cast<T>((first - order * second) / (1.0L - order));
}

template<class T>
T ellint_3_fallback(T k, T nu, T phi) {
    if (std::isnan(k) || std::isnan(nu) || std::isnan(phi) ||
        std::abs(k) > T{1}) {
        return quiet_nan<T>();
    }
    // Pole classification; quadrature across any of these would return
    // finite garbage instead of the divergence.
    // - nu > 1: the pole factor 1 - nu sin^2(theta) has a first-order sign
    //   change at asin(1/sqrt(nu)) < pi/2. Ending exactly on the pole is a
    //   same-sign divergence (+inf); crossing it makes the integral
    //   undefined (domain error -> NaN).
    // - nu == 1: at pi/2 the pole is second order (cos^2), so both sides
    //   diverge with the same sign: +inf for any |phi| >= pi/2.
    // - nu < 1 with |k| == 1: same divergence as ellint_1, the
    //   1/sqrt(1 - k^2 sin^2) factor behaves like 1/|cos| at pi/2.
    const T half_pi = pi_v<T> / T{2};
    if (nu > T{1}) {
        if (std::abs(phi) >= half_pi) {
            return quiet_nan<T>();
        }
        const T pole_angle = std::asin(T{1} / std::sqrt(nu));
        if (std::abs(phi) > pole_angle) {
            return quiet_nan<T>();
        }
        if (std::abs(phi) == pole_angle) {
            return std::copysign(infinity<T>(), phi);
        }
    } else if (nu == T{1}) {
        if (std::abs(phi) >= half_pi) {
            return std::copysign(infinity<T>(), phi);
        }
    } else if (std::abs(k) == T{1} && std::abs(phi) >= half_pi) {
        return std::copysign(infinity<T>(), phi);
    }
    if (k == T{}) {
        return ellint_3_zero_modulus(nu, phi);
    }
    if (std::abs(k) == T{1}) {
        return ellint_3_unit_modulus(nu, phi);
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
    if (!std::isnan(k) && !std::isnan(nu) &&
        std::abs(k) < T{1} && nu < T{1} && k != T{}) {
        using wide_t = conditional_t<
            (sizeof(T) < sizeof(double)), double, long double>;
        const wide_t delta = wide_t{1} - static_cast<wide_t>(nu);
        if (delta < static_cast<wide_t>(1e-6L)) {
            const wide_t modulus = static_cast<wide_t>(k);
            const wide_t complementary =
                wide_t{1} - modulus * modulus;
            const auto integrand = [=](long double angle) {
                const long double sine = std::sin(angle);
                const long double cosine = std::cos(angle);
                const long double sine2 = sine * sine;
                const long double scaled_cosine2 =
                    delta * cosine * cosine;
                return std::sqrt(scaled_cosine2 + sine2) /
                    std::sqrt(
                        scaled_cosine2 + complementary * sine2);
            };
            const auto transformed = checked_simpson_integral(
                integrand,
                0.0L,
                pi_v<long double> / 2.0L,
                1e-14L,
                28u);
            return static_cast<T>(
                transformed.value / std::sqrt(delta));
        }
    }
    return ellint_3_fallback(k, nu, pi_v<T> / T{2});
}

} // namespace detail::special_math

#pragma once

namespace detail::special_math {

inline long double carlson_rf(long double x, long double y, long double z) {
    if (std::isnan(x) || std::isnan(y) || std::isnan(z) ||
        x < 0.0L || y < 0.0L || z < 0.0L) {
        return quiet_nan<long double>();
    }
    if ((x == 0.0L && y == 0.0L) ||
        (y == 0.0L && z == 0.0L) || (z == 0.0L && x == 0.0L)) {
        return infinity<long double>();
    }
    const long double scale = std::max({x, y, z});
    if (std::isinf(scale)) {
        return 0.0L;
    }
    x /= scale;
    y /= scale;
    z /= scale;
    const long double tolerance = std::pow(
        std::numeric_limits<long double>::epsilon() / 100.0L, 0.125L);
    for (unsigned iteration = 0; iteration < 128u; ++iteration) {
        const long double mean = (x + y + z) / 3.0L;
        const long double dx = (mean - x) / mean;
        const long double dy = (mean - y) / mean;
        const long double dz = (mean - z) / mean;
        if (std::max({std::abs(dx), std::abs(dy), std::abs(dz)}) < tolerance) {
            // Carlson duplication and the degree-seven expansion, DLMF 19.36.1.
            const long double e2 = dx * dy + dy * dz + dz * dx;
            const long double e3 = dx * dy * dz;
            const long double polynomial = 1.0L - e2 / 10.0L + e3 / 14.0L +
                e2 * e2 / 24.0L - 3.0L * e2 * e3 / 44.0L -
                5.0L * e2 * e2 * e2 / 208.0L + 3.0L * e3 * e3 / 104.0L +
                e2 * e2 * e3 / 16.0L;
            return polynomial / std::sqrt(mean) / std::sqrt(scale);
        }
        const long double sx = std::sqrt(x);
        const long double sy = std::sqrt(y);
        const long double sz = std::sqrt(z);
        const long double lambda = sx * sy + sy * sz + sz * sx;
        x = (x + lambda) / 4.0L;
        y = (y + lambda) / 4.0L;
        z = (z + lambda) / 4.0L;
    }
    return quiet_nan<long double>();
}

inline long double carlson_rc_sqrt(long double sqrt_x, long double sqrt_y) {
    if (!std::isfinite(sqrt_x) || !std::isfinite(sqrt_y) ||
        sqrt_x < 0.0L || sqrt_y <= 0.0L) {
        return quiet_nan<long double>();
    }
    const long double scale = std::max(sqrt_x, sqrt_y);
    sqrt_x /= scale;
    sqrt_y /= scale;
    if (sqrt_y == 0.0L) {
        return quiet_nan<long double>();
    }
    const long double tolerance = std::pow(
        std::numeric_limits<long double>::epsilon() / 100.0L, 0.125L);
    for (unsigned iteration = 0; iteration < 128u; ++iteration) {
        if (std::abs(sqrt_x - sqrt_y) <
            tolerance * std::max(sqrt_x, sqrt_y)) {
            return carlson_rf(sqrt_x * sqrt_x, sqrt_y * sqrt_y,
                sqrt_y * sqrt_y) / scale;
        }
        // RF(x,y,y) duplication in square roots avoids squaring a tiny y.
        const long double next_x = (sqrt_x + sqrt_y) / 2.0L;
        sqrt_y = std::sqrt(sqrt_y) * std::sqrt(next_x);
        sqrt_x = next_x;
    }
    return quiet_nan<long double>();
}

inline long double carlson_rc(long double x, long double y) {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0L || y <= 0.0L) {
        return quiet_nan<long double>();
    }
    return carlson_rc_sqrt(std::sqrt(x), std::sqrt(y));
}

inline long double carlson_rj(
    long double x, long double y, long double z, long double p) {
    // Positive-real core only: at most one of x,y,z is zero, and p > 0.
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(p) || x < 0.0L || y < 0.0L || z < 0.0L || p <= 0.0L ||
        (x == 0.0L && y == 0.0L) || (y == 0.0L && z == 0.0L) ||
        (z == 0.0L && x == 0.0L)) {
        return quiet_nan<long double>();
    }
    const long double scale = std::max({x, y, z, p});
    const long double scaled_x = x / scale;
    const long double scaled_y = y / scale;
    const long double scaled_z = z / scale;
    const long double scaled_p = p / scale;
    if ((x > 0.0L && scaled_x == 0.0L) ||
        (y > 0.0L && scaled_y == 0.0L) ||
        (z > 0.0L && scaled_z == 0.0L) || scaled_p == 0.0L) {
        return quiet_nan<long double>();
    }
    x = scaled_x;
    y = scaled_y;
    z = scaled_z;
    p = scaled_p;
    const long double tolerance = std::pow(
        std::numeric_limits<long double>::epsilon() / 100.0L, 0.125L);
    long double sum = 0.0L;
    long double correction = 0.0L;
    long double factor = 1.0L;
    for (unsigned iteration = 0; iteration < 128u; ++iteration) {
        const long double mean = (x + y + z + 2.0L * p) / 5.0L;
        const long double dx = (mean - x) / mean;
        const long double dy = (mean - y) / mean;
        const long double dz = (mean - z) / mean;
        const long double dp = (mean - p) / mean;
        if (std::max({std::abs(dx), std::abs(dy), std::abs(dz),
                std::abs(dp)}) < tolerance) {
            // DLMF 19.36.2 uses the five deviations dx,dy,dz,dp,dp.
            const long double pairs = dx * dy + dx * dz + dy * dz;
            const long double triple = dx * dy * dz;
            const long double dp2 = dp * dp;
            const long double e2 = pairs - 3.0L * dp2;
            const long double e3 = triple + 2.0L * dp * (pairs - dp2);
            const long double e4 = dp * (2.0L * triple + dp * pairs);
            const long double e5 = triple * dp2;
            const long double polynomial = 1.0L - 3.0L * e2 / 14.0L + e3 / 6.0L +
                9.0L * e2 * e2 / 88.0L - 3.0L * e4 / 22.0L -
                9.0L * e2 * e3 / 52.0L + 3.0L * e5 / 26.0L -
                e2 * e2 * e2 / 16.0L + 3.0L * e3 * e3 / 40.0L +
                3.0L * e2 * e4 / 20.0L + 45.0L * e2 * e2 * e3 / 272.0L -
                9.0L * (e3 * e4 + e2 * e5) / 68.0L;
            const long double value = sum +
                factor * polynomial / mean / std::sqrt(mean);
            if (!std::isfinite(value)) {
                return quiet_nan<long double>();
            }
            return value / scale / std::sqrt(scale);
        }
        const long double sx = std::sqrt(x);
        const long double sy = std::sqrt(y);
        const long double sz = std::sqrt(z);
        const long double lambda = sx * sy + sy * sz + sz * sx;
        // DLMF 19.26.22-23: keep alpha,beta unsquared for the RC call.
        const long double alpha = p * (sx + sy + sz) + sx * sy * sz;
        const long double beta = std::sqrt(p) * (p + lambda);
        const long double rc = carlson_rc_sqrt(alpha, beta);
        if (!std::isfinite(rc)) {
            return quiet_nan<long double>();
        }
        const long double term = 3.0L * factor * rc - correction;
        const long double next_sum = sum + term;
        correction = (next_sum - sum) - term;
        sum = next_sum;
        if (!std::isfinite(sum)) {
            return quiet_nan<long double>();
        }
        factor /= 4.0L;
        x = (x + lambda) / 4.0L;
        y = (y + lambda) / 4.0L;
        z = (z + lambda) / 4.0L;
        p = (p + lambda) / 4.0L;
    }
    return quiet_nan<long double>();
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
            return integrand(theta);
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

    if (k == T{} || std::isinf(phi)) {
        return phi;
    }
    const long double modulus = std::abs(static_cast<long double>(k));
    const long double complementary = (1.0L - modulus) * (1.0L + modulus);
    const long double bound = std::abs(static_cast<long double>(phi));
    const long double period = pi_v<long double>;
    int quotient_bits;
    const long double remainder = std::remquo(bound, period, &quotient_bits);
    long double periods = std::round((bound - remainder) / period);
    long double sine = std::sin(bound);
    long double cosine = std::cos(bound);
    if ((quotient_bits & 1) != 0) {
        sine = -sine;
        cosine = -cosine;
    }
    // Use libm's original-angle reduction near the pole, not pi - phi.
    // Correct the nearest-period choice if rounded pi selected the other side.
    if (cosine < 0.0L) {
        periods += std::copysign(1.0L, sine);
        sine = -sine;
    }
    const long double cosine2 = cosine * cosine;
    long double value = sine * carlson_rf(
        cosine2, cosine2 + complementary * sine * sine, 1.0L);
    if (periods != 0.0L) {
        value += periods * 2.0L * complete_ellint_1_agm(modulus);
    }
    return std::copysign(static_cast<T>(value), phi);
}

template<class T>
T ellint_2_fallback(T k, T phi) {
    if (std::isnan(k) || std::isnan(phi) || std::abs(k) > T{1}) {
        return quiet_nan<T>();
    }
    const long double modulus = static_cast<long double>(k);
    return elliptic_integral(phi, [&](long double theta) {
        const long double s = std::sin(theta);
        const long double radicand = 1.0L - modulus * modulus * s * s;
        if (radicand < 0.0L) {
            return quiet_nan<long double>();
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
    if (order > 0.0L) {
        const long double cosine = std::cos(angle);
        const long double cosine2 = cosine * cosine;
        const long double sine2 = sine * sine;
        const long double pole = cosine2 + (1.0L - order) * sine2;
        // DLMF 19.25.14: nonnegative terms avoid the nu == 1 cancellation.
        return static_cast<T>(sine * carlson_rf(cosine2, cosine2, 1.0L) +
            order * sine * sine2 * carlson_rj(
                cosine2, cosine2, 1.0L, pole) / 3.0L);
    }
    const long double root = std::sqrt(-order);
    const long double second = std::atan(root * sine) / root;
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
    const long double modulus = static_cast<long double>(k);
    const long double order = static_cast<long double>(nu);
    return elliptic_integral(phi, [&](long double theta) {
        const long double s = std::sin(theta);
        const long double sin2 = s * s;
        const long double radicand = 1.0L - modulus * modulus * sin2;
        const long double pole = 1.0L - order * sin2;
        if (radicand <= 0.0L || pole == 0.0L) {
            return infinity<long double>();
        }
        return 1.0L / (pole * std::sqrt(radicand));
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
        std::abs(k) < T{1} && nu >= T{} && nu < T{1} && k != T{}) {
        if (nu == T{}) {
            return complete_ellint_1_agm(k);
        }
        const long double modulus = std::abs(static_cast<long double>(k));
        const long double order = static_cast<long double>(nu);
        const long double complementary = (1.0L - modulus) * (1.0L + modulus);
        // DLMF 19.25.2; both terms are nonnegative in this branch.
        return static_cast<T>(carlson_rf(0.0L, complementary, 1.0L) +
            order * carlson_rj(0.0L, complementary, 1.0L, 1.0L - order) / 3.0L);
    }
    return ellint_3_fallback(k, nu, pi_v<T> / T{2});
}

} // namespace detail::special_math
